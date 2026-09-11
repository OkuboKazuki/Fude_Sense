#include "GpuInk.h"
#include <cmath>
#include <cassert>
#include <algorithm>
#include <vector>
#include <cstring>
#include <thread>
#include <chrono>
#include <climits>

#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")

// ============================================================================
// Ink Simulation Parameters (GpuInk)
// ============================================================================

static constexpr int PROPAGATION_THRESHOLD = 350;
static constexpr int PROPAGATION_AMOUNT = 40;
static constexpr int MAX_INK_PER_PIXEL = 450;
static constexpr double MAX_SPEED_PX_PER_SEC = 2000.0;

// スタンプ内の濃度プロファイル。
// 中心側は平坦に飽和させ、外周 STAMP_RIM_RATIO の帯だけを急峻に落とす。
// 内部に勾配を作ると全画素がにじみの送り出し側に回ってしまい
// (平坦なら蒸発 -10/tick で済むところが流出 -40/tick になる)、
// にじみが過剰になるため中心側は必ず平坦に保つこと。
// 大きくすると筆跡の縁が柔らかくなる。
static constexpr double STAMP_RIM_RATIO = 0.22;

// 紙の水分場（m_wetField）のパラメータ
// にじみは墨の濃さではなく水分が駆動する。乾いた紙には墨が流れ込まないため、
// 水分を持たない画素（かすれの隙間など）は塗り潰されずに残る。
static constexpr int WET_MAX = 255;          // 画素あたりの最大水分量
static constexpr int WET_THRESHOLD = 8;      // これ以下は「乾いた紙」として扱う
static constexpr int WET_DRY_RATE = 1;       // 1 ティックあたりの乾燥量
// 隣接画素へ水を運ぶ際の減衰量。にじみの到達距離を決めるのはこの値である。
// 水は 1 画素進むごとに本値だけ減り、WET_THRESHOLD を切ると止まる。潤沢な筆
// (wet = WET_MAX) からの到達ホップ数は (WET_MAX - WET_THRESHOLD) / 本値 - 1 で、
// 40 なら約 5px。墨は濡れた画素にしか進めないので、これがにじみ半径の上限になる。
static constexpr int WET_SPREAD_LOSS = 40;
static constexpr double WET_MARGIN_PX = 2.0; // 墨の接地範囲より外側に水分を広げる幅

// かすれ（渇筆）のパラメータ
//
// かすれは「濃度を下げる」のではなく「墨を置かない画素を決める」ことで作る。
// 置くと決めた画素には従来どおり飽和濃度を与え、置かない画素は墨も水分も
// 受け取らない (m_ink = 0 のまま)。水を置かないので、後続のにじみでも
// 白い隙間が塗り潰されることはない。判定は次の 1 本の式に集約される。
//
//   毛束マスク(u) + 紙目マスク(x, y) > dryness * KASURE_THRESHOLD_SCALE
//
// u は運筆方向に垂直な距離。毛束マスクを u だけの関数にするのが要で、
// これにより同じ画素を覆う何枚ものスタンプが必ず同じ判定を返す。
// スタンプ中心からの相対座標で測ると 1 画素が 8 枚前後のスタンプから
// 別々の判定を受け、最大値合成で「全スタンプが隙間と認めた点」だけが
// 生き残るため、筋ではなく孤立したドットになってしまう。
static constexpr double KASURE_LANE_WIDTH_PX = 2.6;    // 毛束 1 本ぶんの幅 (px)
static constexpr double KASURE_GRAIN_SCALE_PX = 2.2;   // 紙目の粒の大きさ (px)
static constexpr double KASURE_BRISTLE_WEIGHT = 0.62;  // 毛束マスクの寄与
static constexpr double KASURE_GRAIN_WEIGHT = 0.38;    // 紙目マスクの寄与 (合計 1.0)
static constexpr double KASURE_THRESHOLD_SCALE = 0.85; // 乾き切っても残る墨の余地

static inline double KasureHash(uint32_t h)
{
	h ^= h >> 15;
	h *= 2246822519u;
	h ^= h >> 13;
	h *= 3266489917u;
	h ^= h >> 16;
	return static_cast<double>(h & 0xFFFFFFu) / static_cast<double>(0xFFFFFFu);
}

static inline double KasureFade(double t)
{
	return t * t * (3.0 - 2.0 * t);
}

// 毛束マスク: 運筆方向に垂直な距離 u だけで決まる 1 次元ノイズ (0.0 ～ 1.0)。
// u 方向にしか変化しないため、値の等しい帯が運筆方向へ真っ直ぐ伸びて筋になる。
static inline double BristleMask(double u)
{
	double t = u / KASURE_LANE_WIDTH_PX;
	double base = std::floor(t);
	int i = static_cast<int>(base);
	double f = KasureFade(t - base);
	double a = KasureHash(static_cast<uint32_t>(i) * 2654435761u);
	double b = KasureHash(static_cast<uint32_t>(i + 1) * 2654435761u);
	return a + (b - a) * f;
}

// 紙目マスク: 紙座標に固定された 2 次元ノイズ (0.0 ～ 1.0)。
// 筆にもストロークにも依存しないので、重ね書きしても同じ場所が同じようにざらつく。
static inline double PaperGrainMask(int x, int y)
{
	double gx = static_cast<double>(x) / KASURE_GRAIN_SCALE_PX;
	double gy = static_cast<double>(y) / KASURE_GRAIN_SCALE_PX;
	double bx = std::floor(gx);
	double by = std::floor(gy);
	int ix = static_cast<int>(bx);
	int iy = static_cast<int>(by);
	double fx = KasureFade(gx - bx);
	double fy = KasureFade(gy - by);

	auto corner = [](int cx, int cy) {
		return KasureHash(static_cast<uint32_t>(cx) * 374761393u
			+ static_cast<uint32_t>(cy) * 668265263u);
	};

	double n00 = corner(ix, iy);
	double n10 = corner(ix + 1, iy);
	double n01 = corner(ix, iy + 1);
	double n11 = corner(ix + 1, iy + 1);

	double nx0 = n00 + (n10 - n00) * fx;
	double nx1 = n01 + (n11 - n01) * fx;
	return nx0 + (nx1 - nx0) * fy;
}

// 同じ値が続く区間を [長さ(uint32)][値(T)] の並びに畳む。半紙はほとんどが
// 白紙（墨量 0・水分 0）なので、これだけで控えの容量が大きく減る。
// 畳んだ結果が生データより大きくなる場合は、生データをそのまま入れて
// compressed = false を返す（市松模様のような最悪ケースの保険）。
template <typename T>
static void RleEncode(const T* src, size_t count, std::vector<uint8_t>& out, bool& compressed)
{
	const size_t rawBytes = count * sizeof(T);

	// 画の書き始めごとに走る処理なので、走査中に領域を伸ばさない。
	// 生データ分を先に確保して生ポインタで詰め、最後に使った分へ切り詰める。
	// （Debug ビルドでは vector::insert の反復子チェックが効いて 20 倍以上遅くなる）
	out.clear();
	out.resize(rawBytes);
	uint8_t* dst = out.data();

	size_t used = 0;
	bool tooBig = false;
	size_t i = 0;
	while (i < count)
	{
		const T value = src[i];
		size_t run = 1;
		while (i + run < count && src[i + run] == value) ++run;

		if (used + sizeof(uint32_t) + sizeof(T) > rawBytes) { tooBig = true; break; }

		const uint32_t len = static_cast<uint32_t>(run);
		std::memcpy(dst + used, &len, sizeof(len));
		used += sizeof(len);
		std::memcpy(dst + used, &value, sizeof(T));
		used += sizeof(T);

		i += run;
	}

	if (tooBig)
	{
		// 市松模様のように畳めない絵。生データをそのまま持つ
		std::memcpy(dst, src, rawBytes);
		compressed = false;
	}
	else
	{
		out.resize(used);
		compressed = true;
	}
	out.shrink_to_fit();
}

template <typename T>
static bool RleDecode(const std::vector<uint8_t>& src, bool compressed, T* dst, size_t count)
{
	const size_t rawBytes = count * sizeof(T);
	if (!compressed)
	{
		if (src.size() != rawBytes) return false;
		std::memcpy(dst, src.data(), rawBytes);
		return true;
	}

	size_t pos = 0;
	size_t written = 0;
	while (pos + sizeof(uint32_t) + sizeof(T) <= src.size())
	{
		uint32_t len = 0;
		std::memcpy(&len, src.data() + pos, sizeof(len));
		pos += sizeof(len);
		T value{};
		std::memcpy(&value, src.data() + pos, sizeof(T));
		pos += sizeof(T);

		if (len == 0 || written + len > count) return false;
		std::fill_n(dst + written, len, value);
		written += len;
	}
	return written == count;
}

static inline uint32_t CalculateInkPixel(int inkAmount)
{
	if (inkAmount <= 0) return 0xFFFFFFFF; // 白キャンバス（インクなし）
	int inkVal = (inkAmount > 255) ? 255 : inkAmount; // 255を超えたら255に固定
	uint32_t color = static_cast<uint32_t>(255 - inkVal); // 濃さに応じて白(255)～黒(0)
	return 0xFF000000 | (color << 16) | (color << 8) | color;
}

GpuInk::GpuInk()
{
	m_ink.clear();
	m_deltaInk.clear();
	m_wetField.clear();
	m_pixelBuffer.clear();

	// Direct2D ファクトリの生成
	D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &m_pD2DFactory);
}

GpuInk::~GpuInk()
{
	StopPropagationThread();
	ReleaseResources();

	if (m_pD2DFactory)
	{
		m_pD2DFactory->Release();
		m_pD2DFactory = nullptr;
	}
}

void GpuInk::ResetDirtyRect_NoLock()
{
	m_activeMinX = INT_MAX;
	m_activeMinY = INT_MAX;
	m_activeMaxX = -1;
	m_activeMaxY = -1;

	m_uploadMinX = INT_MAX;
	m_uploadMinY = INT_MAX;
	m_uploadMaxX = -1;
	m_uploadMaxY = -1;
}

void GpuInk::ExpandDirtyRect_NoLock(int x, int y)
{
	if (x < m_activeMinX) m_activeMinX = x;
	if (x > m_activeMaxX) m_activeMaxX = x;
	if (y < m_activeMinY) m_activeMinY = y;
	if (y > m_activeMaxY) m_activeMaxY = y;

	if (x < m_uploadMinX) m_uploadMinX = x;
	if (x > m_uploadMaxX) m_uploadMaxX = x;
	if (y < m_uploadMinY) m_uploadMinY = y;
	if (y > m_uploadMaxY) m_uploadMaxY = y;
}

void GpuInk::ReleaseResources_NoLock()
{
	if (m_pInkBitmap)
	{
		m_pInkBitmap->Release();
		m_pInkBitmap = nullptr;
	}

	if (m_pDCRenderTarget)
	{
		m_pDCRenderTarget->Release();
		m_pDCRenderTarget = nullptr;
	}

	m_ink.clear();
	m_ink.shrink_to_fit();
	m_deltaInk.clear();
	m_deltaInk.shrink_to_fit();
	m_wetField.clear();
	m_wetField.shrink_to_fit();
	m_pixelBuffer.clear();
	m_pixelBuffer.shrink_to_fit();
	ResetDirtyRect_NoLock();
	m_gpuSim.Release();
	m_needsGpuUpload = false;
	m_width = m_height = 0;
	m_inStroke = false;
}

void GpuInk::ReleaseResources()
{
	StopPropagationThread();
	std::lock_guard<std::mutex> lock(m_mutex);
	ReleaseResources_NoLock();
}

bool GpuInk::Initialize_NoLock(int width, int height)
{
	if (width <= 0 || height <= 0) return false;

	ReleaseResources_NoLock();

	m_width = width;
	m_height = height;

	if (!m_pD2DFactory)
	{
		D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &m_pD2DFactory);
		if (!m_pD2DFactory) return false;
	}

	// DC Render Target プロパティ
	D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(
		D2D1_RENDER_TARGET_TYPE_DEFAULT,
		D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
		0, 0,
		D2D1_RENDER_TARGET_USAGE_NONE,
		D2D1_FEATURE_LEVEL_DEFAULT
	);

	HRESULT hr = m_pD2DFactory->CreateDCRenderTarget(&props, &m_pDCRenderTarget);
	if (FAILED(hr) || !m_pDCRenderTarget)
	{
		return false;
	}


	// Dynamic Bitmap 描画用バッファ
	size_t pixels = static_cast<size_t>(m_width) * static_cast<size_t>(m_height);
	m_pixelBuffer.assign(pixels, 0xFFFFFFFF);
	m_ink.assign(pixels, 0);
	m_deltaInk.assign(pixels, 0);
	m_wetField.assign(pixels, 0);

	D2D1_BITMAP_PROPERTIES bitmapProps = D2D1::BitmapProperties(
		D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE)
	);

	hr = m_pDCRenderTarget->CreateBitmap(
		D2D1::SizeU(m_width, m_height),
		m_pixelBuffer.data(),
		m_width * sizeof(uint32_t),
		&bitmapProps,
		&m_pInkBitmap
	);

	ResetDirtyRect_NoLock();
	m_gpuSim.Initialize(m_width, m_height);
	m_needsGpuUpload = true;
	return SUCCEEDED(hr);
}

bool GpuInk::Initialize(HWND /*hWnd*/, int width, int height)
{
	return Initialize(width, height);
}

bool GpuInk::Initialize(int width, int height, bool runPropagationThread)
{
	StopPropagationThread();

	std::lock_guard<std::mutex> lock(m_mutex);
	bool success = Initialize_NoLock(width, height);
	if (success && runPropagationThread)
	{
		try
		{
			m_runPropagation.store(true);
			m_propagationThread = std::thread(&GpuInk::PropagationThreadLoop, this);
		}
		catch (...)
		{
			m_runPropagation.store(false);
		}
	}
	return success;
}

bool GpuInk::StepPropagation()
{
	std::lock_guard<std::mutex> lock(m_mutex);
	if (m_width <= 0 || m_height <= 0 || m_ink.empty()) return false;
	return PropagateInk_NoLock();
}

void GpuInk::Resize(int width, int height)
{
	if (width <= 0 || height <= 0) return;
	if (width == m_width && height == m_height) return;

	std::lock_guard<std::mutex> lock(m_mutex);
	if (width == m_width && height == m_height) return;

	if (!m_pDCRenderTarget || !m_pInkBitmap || m_width <= 0 || m_height <= 0)
	{
		Initialize_NoLock(width, height);
		return;
	}

	int oldW = m_width;
	int oldH = m_height;
	std::vector<int> oldInk = std::move(m_ink);
	std::vector<uint8_t> oldWet = std::move(m_wetField);
	std::vector<uint32_t> oldPixels = std::move(m_pixelBuffer);

	if (m_pInkBitmap) { m_pInkBitmap->Release(); m_pInkBitmap = nullptr; }
	if (m_pDCRenderTarget) { m_pDCRenderTarget->Release(); m_pDCRenderTarget = nullptr; }

	m_width = width;
	m_height = height;
	size_t newPixels = static_cast<size_t>(width) * static_cast<size_t>(height);
	m_ink.assign(newPixels, 0);
	m_deltaInk.assign(newPixels, 0);
	m_wetField.assign(newPixels, 0);
	m_pixelBuffer.assign(newPixels, 0xFFFFFFFF);

	if (!oldInk.empty() && oldW > 0 && oldH > 0)
	{
		for (int ny = 0; ny < height; ++ny)
		{
			int oy = (ny * oldH) / height;
			if (oy >= oldH) oy = oldH - 1;
			for (int nx = 0; nx < width; ++nx)
			{
				int ox = (nx * oldW) / width;
				if (ox >= oldW) ox = oldW - 1;

				size_t oldIdx = static_cast<size_t>(oy) * oldW + ox;
				size_t newIdx = static_cast<size_t>(ny) * width + nx;
				m_ink[newIdx] = oldInk[oldIdx];
				if (oldIdx < oldWet.size()) m_wetField[newIdx] = oldWet[oldIdx];
				m_pixelBuffer[newIdx] = oldPixels[oldIdx];
			}
		}
	}

	D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(
		D2D1_RENDER_TARGET_TYPE_DEFAULT,
		D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
		0, 0, D2D1_RENDER_TARGET_USAGE_NONE, D2D1_FEATURE_LEVEL_DEFAULT
	);
	m_pD2DFactory->CreateDCRenderTarget(&props, &m_pDCRenderTarget);

	D2D1_BITMAP_PROPERTIES bitmapProps = D2D1::BitmapProperties(
		D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE)
	);
	if (m_pDCRenderTarget)
	{
		m_pDCRenderTarget->CreateBitmap(
			D2D1::SizeU(m_width, m_height),
			m_pixelBuffer.data(),
			m_width * sizeof(uint32_t),
			&bitmapProps,
			&m_pInkBitmap
		);
	}

	ResetDirtyRect_NoLock();
	m_gpuSim.Resize(width, height);
	m_needsGpuUpload = true;
	m_uploadMinX = 0; m_uploadMinY = 0;
	m_uploadMaxX = m_width - 1; m_uploadMaxY = m_height - 1;
}

void GpuInk::EnsureInitialized()
{
	if (!m_pDCRenderTarget || !m_pInkBitmap)
	{
		Initialize_NoLock(800, 600);
		if (!m_runPropagation.load())
		{
			try
			{
				m_runPropagation.store(true);
				m_propagationThread = std::thread(&GpuInk::PropagationThreadLoop, this);
			}
			catch (...)
			{
				m_runPropagation.store(false);
			}
		}
	}
}

void GpuInk::Clear()
{
	std::lock_guard<std::mutex> lock(m_mutex);

	// 運筆の途中で全消しされた場合でも、前のストロークの続きとして
	// 描画が再開されないようストローク状態を終了させる。
	m_inStroke = false;

	if (m_width > 0 && m_height > 0)
	{
		size_t pixels = static_cast<size_t>(m_width) * static_cast<size_t>(m_height);
		if (!m_pixelBuffer.empty()) std::fill_n(m_pixelBuffer.data(), pixels, 0xFFFFFFFF);
		if (!m_ink.empty()) std::fill_n(m_ink.data(), pixels, 0);
		if (!m_deltaInk.empty()) std::fill_n(m_deltaInk.data(), pixels, 0);
		if (!m_wetField.empty()) std::fill_n(m_wetField.data(), pixels, static_cast<uint8_t>(0));
		ResetDirtyRect_NoLock();

		if (m_pInkBitmap)
		{
			D2D1_RECT_U rect = D2D1::RectU(0, 0, m_width, m_height);
			m_pInkBitmap->CopyFromMemory(&rect, m_pixelBuffer.data(), m_width * sizeof(uint32_t));
		}
		m_needsGpuUpload = true;
	}
}

bool GpuInk::CaptureSnapshot(InkSnapshot& out)
{
	std::lock_guard<std::mutex> lock(m_mutex);

	if (m_width <= 0 || m_height <= 0) return false;
	const size_t pixels = static_cast<size_t>(m_width) * static_cast<size_t>(m_height);
	if (m_ink.size() != pixels || m_wetField.size() != pixels) return false;

	out.width = m_width;
	out.height = m_height;
	RleEncode(m_ink.data(), pixels, out.ink, out.inkCompressed);
	RleEncode(m_wetField.data(), pixels, out.wet, out.wetCompressed);
	return true;
}

bool GpuInk::RestoreSnapshot(const InkSnapshot& snap)
{
	std::lock_guard<std::mutex> lock(m_mutex);

	if (snap.IsEmpty()) return false;
	// ウィンドウや用紙が変わって半紙の大きさが違えば、もう書き戻せない
	if (snap.width != m_width || snap.height != m_height) return false;

	const size_t pixels = static_cast<size_t>(m_width) * static_cast<size_t>(m_height);
	if (m_ink.size() != pixels || m_wetField.size() != pixels) return false;

	if (!RleDecode(snap.ink, snap.inkCompressed, m_ink.data(), pixels)) return false;
	if (!RleDecode(snap.wet, snap.wetCompressed, m_wetField.data(), pixels)) return false;

	// 拡散の作業用バッファは 1 パスごとに作り直されるので 0 に戻すだけでよい
	if (!m_deltaInk.empty()) std::fill_n(m_deltaInk.data(), pixels, 0);

	// 運筆の途中で戻された場合でも、前の画の続きとして描画が再開されないようにする
	m_inStroke = false;

	RebuildPixels_NoLock();

	// 書き戻した時点の絵で止める。ここで拡散の対象範囲を半紙全面にすると、
	// 戻すたびに全画素を舐める重いパスが走るため、範囲は空にしておく。
	// 次の画を置いた時点で、その周りから拡散が再開する。
	ResetDirtyRect_NoLock();

	if (m_pInkBitmap && !m_pixelBuffer.empty())
	{
		D2D1_RECT_U rect = D2D1::RectU(0, 0, m_width, m_height);
		m_pInkBitmap->CopyFromMemory(&rect, m_pixelBuffer.data(), m_width * sizeof(uint32_t));
	}
	m_needsGpuUpload = true;
	return true;
}

void GpuInk::RebuildPixels_NoLock()
{
	if (m_pixelBuffer.size() != m_ink.size()) return;
	for (size_t i = 0; i < m_ink.size(); ++i)
	{
		m_pixelBuffer[i] = CalculateInkPixel(m_ink[i]);
	}
}

// 物理インク拡散シミュレーション (Direct3D 11 Compute Shader GPU 加速 / CPU フォールバック)
bool GpuInk::PropagateInk_NoLock()
{
	if (m_width <= 0 || m_height <= 0 || m_ink.empty()) return false;
	if (m_wetField.size() != m_ink.size()) return false;

	// 1. Direct3D 11 Compute Shader GPU 加速パス
	if (m_gpuSim.IsAvailable())
	{
		if (m_needsGpuUpload)
		{
			m_gpuSim.UploadFromCpu(m_ink.data(), m_wetField.data(), m_width, m_height);
			m_needsGpuUpload = false;
		}

		if (m_activeMinX <= m_activeMaxX && m_activeMinY <= m_activeMaxY)
		{
			if (m_gpuSim.StepSimulation())
			{
				int rMinX = std::max(0, m_activeMinX - 2);
				int rMinY = std::max(0, m_activeMinY - 2);
				int rMaxX = std::min(m_width - 1, m_activeMaxX + 2);
				int rMaxY = std::min(m_height - 1, m_activeMaxY + 2);

				// 全画面ではなく、にじみ進行領域（Dirty Rect）のみ局所リードバック（GPU-CPU転送最適化）
				m_gpuSim.DownloadToPixelsRegion(m_pixelBuffer.data(), m_width, m_height, rMinX, rMinY, rMaxX, rMaxY);
				m_gpuSim.DownloadInkAndWetRegion(m_ink.data(), m_wetField.data(), m_width, m_height, rMinX, rMinY, rMaxX, rMaxY);

				// Direct2D へのアップロード範囲も Dirty Rect に限定
				m_uploadMinX = std::min(m_uploadMinX, rMinX);
				m_uploadMinY = std::min(m_uploadMinY, rMinY);
				m_uploadMaxX = std::max(m_uploadMaxX, rMaxX);
				m_uploadMaxY = std::max(m_uploadMaxY, rMaxY);

				m_activeMinX = std::max(0, m_activeMinX - 1);
				m_activeMinY = std::max(0, m_activeMinY - 1);
				m_activeMaxX = std::min(m_width - 1, m_activeMaxX + 1);
				m_activeMaxY = std::min(m_height - 1, m_activeMaxY + 1);

				return true;
			}
		}
		return false;
	}

	// 2. フォールバック: 従来の CPU セルラー・オートマトン計算
	if (m_activeMinX > m_activeMaxX || m_activeMinY > m_activeMaxY) return false;

	int startX = std::max(0, m_activeMinX - 1);
	int endX = std::min(m_width - 1, m_activeMaxX + 1);
	int startY = std::max(0, m_activeMinY - 1);
	int endY = std::min(m_height - 1, m_activeMaxY + 1);

	for (int y = startY; y <= endY; ++y)
	{
		size_t rowOffset = static_cast<size_t>(y) * static_cast<size_t>(m_width);
		std::fill_n(m_deltaInk.data() + rowOffset + startX, (endX - startX + 1), 0);
	}

	bool changed = false;
	int nextMinX = INT_MAX;
	int nextMaxX = -1;
	int nextMinY = INT_MAX;
	int nextMaxY = -1;

	for (int y = startY; y <= endY; ++y)
	{
		size_t rowOffset = static_cast<size_t>(y) * static_cast<size_t>(m_width);
		for (int x = startX; x <= endX; ++x)
		{
			size_t i = rowOffset + x;
			int curInk = m_ink[i];
			if (curInk <= PROPAGATION_THRESHOLD) continue;

			// にじみを駆動するのは墨の濃さではなく水分。
			// 渇筆で置かれた墨は水分を持たないため、ここで止まりかすれが保存される。
			// 水は墨に先んじて紙を濡らすので、送り出す側の水分から減衰分を引いた
			// 量を「隣へ運べる水」とみなす。
			int carriedWet = static_cast<int>(m_wetField[i]) - WET_SPREAD_LOSS;
			if (carriedWet <= WET_THRESHOLD) continue;

			const int offsets[4][2] = { { -1, 0 }, { 1, 0 }, { 0, -1 }, { 0, 1 } };
			size_t validNeighbors[4];
			int validNX[4], validNY[4];
			int count = 0;

			for (int j = 0; j < 4; ++j)
			{
				int nx = x + offsets[j][0];
				int ny = y + offsets[j][1];
				if (nx < 0 || nx >= m_width || ny < 0 || ny >= m_height) continue;
				size_t ni = static_cast<size_t>(ny) * static_cast<size_t>(m_width) + static_cast<size_t>(nx);

				if (curInk > m_ink[ni])
				{
					// 墨が進む先を水が濡らす（毛管流には濡れた経路が要る）
					if (m_wetField[ni] < carriedWet)
					{
						m_wetField[ni] = static_cast<uint8_t>(carriedWet);
					}

					validNeighbors[count] = ni;
					validNX[count] = nx;
					validNY[count] = ny;
					count++;
				}
			}

			if (count > 0)
			{
				int maxOutflow = PROPAGATION_AMOUNT;
				int flowPerNeighbor = maxOutflow / count;
				if (flowPerNeighbor <= 0 && maxOutflow > 0) flowPerNeighbor = 1;

				int totalOutflow = 0;
				for (int k = 0; k < count; ++k)
				{
					size_t ni = validNeighbors[k];
					m_deltaInk[ni] += flowPerNeighbor;
					totalOutflow += flowPerNeighbor;

					if (validNX[k] < nextMinX) nextMinX = validNX[k];
					if (validNX[k] > nextMaxX) nextMaxX = validNX[k];
					if (validNY[k] < nextMinY) nextMinY = validNY[k];
					if (validNY[k] > nextMaxY) nextMaxY = validNY[k];
				}
				m_deltaInk[i] -= totalOutflow;

				if (x < nextMinX) nextMinX = x;
				if (x > nextMaxX) nextMaxX = x;
				if (y < nextMinY) nextMinY = y;
				if (y > nextMaxY) nextMaxY = y;

				changed = true;
			}
			else
			{
				m_deltaInk[i] -= 10;
				if (x < nextMinX) nextMinX = x;
				if (x > nextMaxX) nextMaxX = x;
				if (y < nextMinY) nextMinY = y;
				if (y > nextMaxY) nextMaxY = y;
				changed = true;
			}
		}
	}

	if (!changed)
	{
		m_activeMinX = INT_MAX;
		m_activeMaxX = -1;
		m_activeMinY = INT_MAX;
		m_activeMaxY = -1;
		return false;
	}

	uint32_t* pixels = m_pixelBuffer.data();

	for (int y = startY; y <= endY; ++y)
	{
		size_t rowOffset = static_cast<size_t>(y) * static_cast<size_t>(m_width);
		for (int x = startX; x <= endX; ++x)
		{
			size_t i = rowOffset + x;
			int d = m_deltaInk[i];
			if (d != 0)
			{
				int val = m_ink[i] + d;
				m_ink[i] = std::max(0, std::min(MAX_INK_PER_PIXEL, val));

				if (pixels)
				{
					uint32_t pixelVal = CalculateInkPixel(m_ink[i]);
					if (pixels[i] != pixelVal)
					{
						pixels[i] = pixelVal;
						if (x < m_uploadMinX) m_uploadMinX = x;
						if (x > m_uploadMaxX) m_uploadMaxX = x;
						if (y < m_uploadMinY) m_uploadMinY = y;
						if (y > m_uploadMaxY) m_uploadMaxY = y;
					}
				}
			}
		}
	}

	// 紙の乾燥。水分を失った画素はにじみの経路から外れ、以後は墨が動かなくなる
	for (int y = startY; y <= endY; ++y)
	{
		size_t rowOffset = static_cast<size_t>(y) * static_cast<size_t>(m_width);
		for (int x = startX; x <= endX; ++x)
		{
			size_t i = rowOffset + x;
			if (m_wetField[i] > 0)
			{
				int dried = static_cast<int>(m_wetField[i]) - WET_DRY_RATE;
				m_wetField[i] = static_cast<uint8_t>(std::max(0, dried));
			}
		}
	}

	m_activeMinX = nextMinX;
	m_activeMaxX = nextMaxX;
	m_activeMinY = nextMinY;
	m_activeMaxY = nextMaxY;

	return true;
}



KinematicsInfo GpuInk::GetKinematicsInfo()
{
	std::lock_guard<std::mutex> lock(m_kinematicsMutex);
	KinematicsInfo info;
	info.currentSpeed = m_lastSpeed;
	info.currentAccel = m_lastAcceleration;
	info.recentMaxSpeed = m_recentMaxSpeed;
	info.recentMaxAccel = m_recentMaxAccel;
	info.recentMaxDist = m_recentMaxDist;
	info.lastEndSpeed = m_lastEndSpeed;
	info.lastEndEffectiveSpeed = m_lastEndEffectiveSpeed;
	info.lastEndAccel = m_lastEndAccel;
	info.lastIsFlick = m_lastIsFlick;
	info.currentZ = m_penZ;
	info.currentAltitude = static_cast<int>(std::round(m_penAltitudeDegrees));
	info.currentAzimuth = static_cast<int>(std::round(m_penAzimuthRad * (180.0 / 3.14159265358979323846)));
	info.isHovering = m_isHovering;
	return info;
}

void GpuInk::UpdatePen(int z, double altitudeDegrees, double azimuthRad, bool hovering)
{
	std::lock_guard<std::mutex> lock(m_kinematicsMutex);
	m_penZ = z;
	m_penAltitudeDegrees = (altitudeDegrees > 0.0) ? altitudeDegrees : 90.0;
	m_penAzimuthRad = azimuthRad;
	m_isHovering = hovering;
}

void GpuInk::UpdatePenZ(int z, int altitudeTenthDegrees, int azimuthTenthDegrees, bool hovering)
{
	double altDeg = (altitudeTenthDegrees > 0) ? (static_cast<double>(altitudeTenthDegrees) / 10.0) : 90.0;
	double azRad = (static_cast<double>(azimuthTenthDegrees) / 10.0) * (3.14159265358979323846 / 180.0);
	UpdatePen(z, altDeg, azRad, hovering);
}

void GpuInk::SetPressureFactor(double factor)
{
	std::lock_guard<std::mutex> lock(m_kinematicsMutex);
	if (factor < 0.0) factor = 0.0;
	if (factor > 1.0) factor = 1.0;
	m_pressureFactor = factor;
}

// ストローク終了 (ユーザー指示に基づき「跳ね払い」のスタンプ生成処理は削除)
void GpuInk::EndStroke()
{
	{
		std::lock_guard<std::mutex> lock(m_kinematicsMutex);

		double effectiveSpeed = std::min(MAX_SPEED_PX_PER_SEC, std::max(m_lastSpeed, m_recentMaxDist * 60.0));
		const double MIN_FLICK_SPEED = 35.0;
		bool hasSpeed = (effectiveSpeed > MIN_FLICK_SPEED) || (m_recentMaxDist >= 1.0);
		bool isStopping = (m_lastAcceleration < -3000.0);
		bool isFlick = hasSpeed && !isStopping;

		m_lastEndSpeed = m_lastSpeed;
		m_lastEndEffectiveSpeed = effectiveSpeed;
		m_lastEndAccel = m_lastAcceleration;
		m_lastIsFlick = isFlick;
	}

	std::lock_guard<std::mutex> lock(m_mutex);
	m_inStroke = false;
}

void GpuInk::StampBrush(double cx, double cy, double radius, unsigned char alpha)
{
	if (radius <= 0.0) radius = 0.5;
	if (m_ink.empty() || m_pixelBuffer.empty()) return;

	uint32_t* pixels = m_pixelBuffer.data();

	// ペンの傾き (altitude/azimuth) と圧力を安全に取得
	double penAltDeg = 90.0;
	double penAzRad = 0.0;
	double penPressFactor = 0.0;
	{
		std::lock_guard<std::mutex> kLock(m_kinematicsMutex);
		penAltDeg = m_penAltitudeDegrees;
		penAzRad = m_penAzimuthRad;
		penPressFactor = m_pressureFactor;
	}

	// ペンの傾き (altitude/azimuth) から毛束の接地形状（しなり・広がり）を推定
	double altitudeDegrees = (penAltDeg > 0.0) ? penAltDeg : 90.0;
	double tiltFactor = (90.0 - altitudeDegrees) / 90.0;
	if (tiltFactor < 0.0) tiltFactor = 0.0;
	if (tiltFactor > 1.0) tiltFactor = 1.0;

	double azimuthRad = penAzRad;
	double angRad = azimuthRad + 1.57079632679; // 毛束の接地広がり方向

	double rad = radius;
	double semiMajor = rad * (1.0 + tiltFactor * 1.5);
	double semiMinor = std::max(0.5, rad * (1.0 - tiltFactor * 0.4));

	// 傾き方向（ペンが倒れている方向）の単位ベクトル
	double tiltDirX = std::sin(azimuthRad);
	double tiltDirY = -std::cos(azimuthRad);

	// 正規化圧力が 1 の時に先端（入力座標）が楕円の端（境界）に位置するようにシフト
	// 楕円の傾き方向の半径は semiMajor
	// 正規化圧力 pNorm (0.0 〜 1.0) と傾き (tiltFactor) に応じてシフト量を算出
	// pNorm = 1.0, tiltFactor = 1.0 の時、シフト量は exactly semiMajor となり、先端が楕円の端に位置する
	double pNorm = penPressFactor;
	if (pNorm <= 0.0 && radius > 0.5)
	{
		pNorm = std::min(1.0, (radius - 0.5) / 18.0);
	}

	double offset = semiMajor * tiltFactor * pNorm;

	cx += tiltDirX * offset;
	cy += tiltDirY * offset;

	// 水分は墨の接地範囲よりわずかに外側まで広がる。
	// にじみは水で濡れた紙にしか進めないため、墨の到達域を確保するための余白。
	double wetSemiMajor = semiMajor + WET_MARGIN_PX;
	double wetSemiMinor = semiMinor + WET_MARGIN_PX;

	double drynessClamped = std::max(0.0, std::min(1.0, m_strokeDryness));
	int wetDeposit = static_cast<int>(WET_MAX * (1.0 - drynessClamped));

	// かすれの判定基準。乾くほど基準が上がり、墨を置ける画素が減る。
	// 潤沢な筆 (dryness = 0) では基準が 0 になり判定そのものを行わない。
	double kasureBar = drynessClamped * KASURE_THRESHOLD_SCALE;
	bool kasureActive = (kasureBar > 0.0);

	// 運筆座標系の横軸。進行方向に垂直な単位ベクトル。
	double lateralX = -m_lastDirY;
	double lateralY = m_lastDirX;

	int ext = static_cast<int>(std::ceil(wetSemiMajor));
	int icx = static_cast<int>(std::lround(cx));
	int icy = static_cast<int>(std::lround(cy));

	int left = std::max(0, icx - ext);
	int right = std::min(m_width - 1, icx + ext);
	int top = std::max(0, icy - ext);
	int bottom = std::min(m_height - 1, icy + ext);

	double cosA = std::cos(angRad);
	double sinA = std::sin(angRad);

	bool stampChanged = false;

	for (int y = top; y <= bottom; ++y)
	{
		double dy = static_cast<double>(y) - cy;
		for (int x = left; x <= right; ++x)
		{
			double dx = static_cast<double>(x) - cx;

			double localX = dx * cosA + dy * sinA;
			double localY = -dx * sinA + dy * cosA;

			size_t idx = static_cast<size_t>(y) * static_cast<size_t>(m_width) + static_cast<size_t>(x);

			// 墨の楕円は水分の楕円に必ず含まれるので、外れた時点で以降は不要
			double wetNormDistSq = (localX * localX) / (wetSemiMajor * wetSemiMajor)
				+ (localY * localY) / (wetSemiMinor * wetSemiMinor);
			if (wetNormDistSq > 1.0) continue;

			// かすれ判定。毛が触れない画素は墨も水分も置かずに素通りする。
			// 横方向の距離 u はスタンプ中心ではなくセグメント始点から測るため、
			// 同じ画素を覆うすべてのスタンプが同一の u ＝ 同一の判定を返す。
			if (kasureActive)
			{
				double u = (static_cast<double>(x) - m_segOriginX) * lateralX
					+ (static_cast<double>(y) - m_segOriginY) * lateralY;
				double density = KASURE_BRISTLE_WEIGHT * BristleMask(u)
					+ KASURE_GRAIN_WEIGHT * PaperGrainMask(x, y);
				if (density <= kasureBar) continue;
			}

			// 水分の付着（墨より一回り広い範囲）。乾いていく一方なので最大値を採る
			if (idx < m_wetField.size() && m_wetField[idx] < wetDeposit)
			{
				m_wetField[idx] = static_cast<uint8_t>(wetDeposit);
			}

			double normDistSq = (localX * localX) / (semiMajor * semiMajor) + (localY * localY) / (semiMinor * semiMinor);
			if (normDistSq > 1.0) continue;

			// 墨の堆積は加算ではなく最大値で合成する。
			//
			// 1 画素は隣接するスタンプに 8 個前後重ねられるため、加算だと
			// 重なり数のばらつきが周期的な濃淡（ビーズ化）になる。従来それが
			// 見えなかったのは alpha=255 で MAX_INK_PER_PIXEL に飽和して
			// 潰れていたからで、濃度を下げた瞬間に露出する。
			// そこで 1 スタンプに飽和後の濃度を直接与え、最大値で合成する。
			int prev = m_ink[idx];
			double distRatio = std::sqrt(normDistSq);
			double coverage = (1.0 - distRatio) / STAMP_RIM_RATIO;
			coverage = std::max(0.0, std::min(1.0, coverage));
			int deposit = static_cast<int>(MAX_INK_PER_PIXEL * coverage
				* (static_cast<double>(alpha) / 255.0));
			deposit = std::max(1, std::min(MAX_INK_PER_PIXEL, deposit));

			// にじみで薄まった画素は次のスタンプで再び濃度が戻るため、
			// 長押しの溜まり（継続的な墨の供給）はそのまま成立する。
			if (deposit <= prev) continue;

			m_ink[idx] = deposit;
			stampChanged = true;
			ExpandDirtyRect_NoLock(x, y);

			uint32_t pixelVal = CalculateInkPixel(m_ink[idx]);
			pixels[idx] = pixelVal;
		}
	}

	extern HWND g_hInkWnd;
	if (stampChanged)
	{
		m_needsGpuUpload = true;
		if (g_hInkWnd && IsWindow(g_hInkWnd)) InvalidateRect(g_hInkWnd, NULL, FALSE);
	}
}

void GpuInk::DrawSegmentLinear(const StrokeSegment& seg)
{
	std::lock_guard<std::mutex> lock(m_mutex);
	EnsureInitialized();

	POINT a = seg.a;
	POINT b = seg.b;

	double startWidth = seg.startWidth;
	double endWidth = seg.endWidth;
	if (std::isnan(startWidth) || startWidth < 0.5) startWidth = 1.0;
	if (std::isnan(endWidth) || endWidth < 0.5) endWidth = 1.0;
	if (startWidth > 500.0) startWidth = 500.0;
	if (endWidth > 500.0) endWidth = 500.0;
	m_inStroke = true;

	// StampBrush が参照する乾き具合を更新（水分の付着量を決める）
	m_strokeDryness = seg.dryness;

	int dx = b.x - a.x;
	int dy = b.y - a.y;
	double dist = std::hypot(static_cast<double>(dx), static_cast<double>(dy));

	double startRadius = startWidth / 2.0;
	if (startRadius < 0.5) startRadius = 0.5;

	double endRadius = endWidth / 2.0;
	if (endRadius < 0.5) endRadius = 0.5;

	// 運筆方向: 呼び出し側の指定を優先し、未指定 (0, 0) なら a→b から算出する
	double dirLen = std::hypot(seg.dirX, seg.dirY);
	if (dirLen >= 1e-6)
	{
		m_lastDirX = seg.dirX / dirLen;
		m_lastDirY = seg.dirY / dirLen;
	}
	else
	{
		double dirX = static_cast<double>(dx);
		double dirY = static_cast<double>(dy);
		double segLen = std::sqrt(dirX * dirX + dirY * dirY);

		if (segLen >= 1.0)
		{
			m_lastDirX = dirX / segLen;
			m_lastDirY = dirY / segLen;
		}
	}

	// かすれの毛束レーンを固定する原点。セグメント始点は前セグメントの終点と
	// 一致し、その終点は前セグメントの中心線上にあるため、原点を乗り換えても
	// 横方向の距離 u は連続する＝筋がストローク全体で途切れない。
	m_segOriginX = static_cast<double>(a.x);
	m_segOriginY = static_cast<double>(a.y);

	double minRadius = std::min(startRadius, endRadius);
	double maxRadius = std::max(startRadius, endRadius);
	// 打刻刻み幅: 細い部分や払いの減衰時は特にスタンプを密にして段差（ガタガタ）を排除
	double step = std::max(0.5, std::min(maxRadius * 0.25, minRadius * 0.4 + 0.3));
	int steps = static_cast<int>(std::max(1.0, std::ceil(dist / step)));

	for (int i = 0; i <= steps; ++i)
	{
		double t = (steps == 0) ? 0.0 : static_cast<double>(i) / static_cast<double>(steps);
		double px = static_cast<double>(a.x) + static_cast<double>(b.x - a.x) * t;
		double py = static_cast<double>(a.y) + static_cast<double>(b.y - a.y) * t;

		double currentRadius = startRadius * (1.0 - t) + endRadius * t;
		StampBrush(px, py, currentRadius, seg.inkAlpha);
	}

	m_lastPt = b;
}

void GpuInk::DrawSegmentLinear(POINT a, POINT b, double startWidth, double endWidth, uint8_t inkAlpha)
{
	StrokeSegment seg;
	seg.a = a;
	seg.b = b;
	seg.startWidth = startWidth;
	seg.endWidth = endWidth;
	seg.inkAlpha = inkAlpha;
	DrawSegmentLinear(seg);
}

void GpuInk::DrawSegment(POINT a, POINT b, double strokeWidth, uint8_t inkAlpha)
{
	DrawSegmentLinear(a, b, strokeWidth, strokeWidth, inkAlpha);
}



void GpuInk::Render(HDC hdc, int destX, int destY, int dispW, int dispH)
{
	if (!hdc) return;

	// dispW/dispH が指定されていない場合は 1:1 描画
	if (dispW <= 0) dispW = m_width;
	if (dispH <= 0) dispH = m_height;

	{
		std::lock_guard<std::mutex> lock(m_mutex);
		if (!m_pDCRenderTarget || !m_pInkBitmap || m_width <= 0 || m_height <= 0) return;

		m_paperOffsetX = destX;
		m_paperOffsetY = destY;

		// Direct2D ビットマップへ未更新ピクセルバッファを転送/更新
		if (m_uploadMinX <= m_uploadMaxX && m_uploadMinY <= m_uploadMaxY)
		{
			uint32_t minX = static_cast<uint32_t>(std::max(0, m_uploadMinX));
			uint32_t minY = static_cast<uint32_t>(std::max(0, m_uploadMinY));
			uint32_t maxX = static_cast<uint32_t>(std::min(m_width - 1, m_uploadMaxX));
			uint32_t maxY = static_cast<uint32_t>(std::min(m_height - 1, m_uploadMaxY));

			D2D1_RECT_U dirtyRect = D2D1::RectU(minX, minY, maxX + 1, maxY + 1);
			size_t offset = static_cast<size_t>(minY) * static_cast<size_t>(m_width) + static_cast<size_t>(minX);
			const uint32_t* srcPtr = m_pixelBuffer.data() + offset;

			m_pInkBitmap->CopyFromMemory(&dirtyRect, srcPtr, m_width * sizeof(uint32_t));

			m_uploadMinX = INT_MAX;
			m_uploadMinY = INT_MAX;
			m_uploadMaxX = -1;
			m_uploadMaxY = -1;
		}
	}

	// Direct2D の GPU / DC 描画は CPU バッファに依存しないため、m_mutex 解放後に実行
	RECT rc = { destX, destY, destX + dispW, destY + dispH };
	HRESULT hr = m_pDCRenderTarget->BindDC(hdc, &rc);
	if (SUCCEEDED(hr))
	{
		m_pDCRenderTarget->SetDpi(96.0f, 96.0f);
		m_pDCRenderTarget->BeginDraw();
		D2D1_RECT_F srcRect  = D2D1::RectF(0.0f, 0.0f, static_cast<float>(m_width), static_cast<float>(m_height));
		D2D1_RECT_F destRect = D2D1::RectF(0.0f, 0.0f, static_cast<float>(dispW),   static_cast<float>(dispH));
		m_pDCRenderTarget->DrawBitmap(m_pInkBitmap, &destRect, 1.0f,
			D2D1_BITMAP_INTERPOLATION_MODE_LINEAR, &srcRect);
		m_pDCRenderTarget->EndDraw();
	}
}

void GpuInk::GetInkSnapshot(std::vector<int>& outInk, int& outWidth, int& outHeight)
{
	std::lock_guard<std::mutex> lock(m_mutex);
	outWidth = m_width;
	outHeight = m_height;
	outInk = m_ink;
}

void GpuInk::PropagationThreadLoop()
{
	using namespace std::chrono_literals;
	const auto frameTime = 16ms;

	while (m_runPropagation.load())
	{
		bool updated = false;
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			if (m_width > 0 && m_height > 0 && !m_ink.empty())
			{
				updated = PropagateInk_NoLock();
			}
		}

		if (updated)
		{
			extern HWND g_hInkWnd;
			extern HWND g_mainWnd;
			if (g_hInkWnd && IsWindow(g_hInkWnd)) InvalidateRect(g_hInkWnd, NULL, FALSE);
			if (g_mainWnd && IsWindow(g_mainWnd))
			{
				RECT rcPaper;
				{
					std::lock_guard<std::mutex> lock(m_mutex);
					rcPaper.left = m_paperOffsetX;
					rcPaper.top = m_paperOffsetY;
					rcPaper.right = m_paperOffsetX + m_width;
					rcPaper.bottom = m_paperOffsetY + m_height;
				}
				if (rcPaper.right > rcPaper.left && rcPaper.bottom > rcPaper.top)
				{
					InvalidateRect(g_mainWnd, &rcPaper, FALSE);
				}
				else
				{
					InvalidateRect(g_mainWnd, NULL, FALSE);
				}
			}
		}

		std::this_thread::sleep_for(frameTime);
	}
}

void GpuInk::StopPropagationThread()
{
	m_runPropagation.store(false);
	if (m_propagationThread.joinable())
	{
		try
		{
			m_propagationThread.join();
		}
		catch (...)
		{
		}
	}
}
