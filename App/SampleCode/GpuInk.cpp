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
static constexpr double PRESSURE_TO_PIXELS = 24.0;
static constexpr double MAX_SPEED_PX_PER_SEC = 2000.0;
static constexpr double ELLIPSE_MIN_ELONGATION = 1.0;
static constexpr double ELLIPSE_MAX_ELONGATION = 1.5;

static inline int clampInk(int v) { return v < 0 ? 0 : (v > MAX_INK_PER_PIXEL ? MAX_INK_PER_PIXEL : v); }

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
	m_pixelBuffer.clear();
	m_pixelBuffer.shrink_to_fit();
	ResetDirtyRect_NoLock();
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
	return SUCCEEDED(hr);
}

bool GpuInk::Initialize(HWND /*hWnd*/, int width, int height)
{
	return Initialize(width, height);
}

bool GpuInk::Initialize(int width, int height)
{
	StopPropagationThread();

	std::lock_guard<std::mutex> lock(m_mutex);
	bool success = Initialize_NoLock(width, height);
	if (success)
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

void GpuInk::Resize(int width, int height)
{
	Initialize(width, height);
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
	if (m_width > 0 && m_height > 0)
	{
		size_t pixels = static_cast<size_t>(m_width) * static_cast<size_t>(m_height);
		if (!m_pixelBuffer.empty()) std::fill_n(m_pixelBuffer.data(), pixels, 0xFFFFFFFF);
		if (!m_ink.empty()) std::fill_n(m_ink.data(), pixels, 0);
		if (!m_deltaInk.empty()) std::fill_n(m_deltaInk.data(), pixels, 0);
		ResetDirtyRect_NoLock();

		if (m_pInkBitmap)
		{
			D2D1_RECT_U rect = D2D1::RectU(0, 0, m_width, m_height);
			m_pInkBitmap->CopyFromMemory(&rect, m_pixelBuffer.data(), m_width * sizeof(uint32_t));
		}
	}
}

void GpuInk::BeginStroke(POINT pt, UINT pressure)
{
	std::lock_guard<std::mutex> lock(m_mutex);
	EnsureInitialized();
	m_inStroke = true;
	m_lastPt = pt;
	m_lastPressure = pressure;
	m_smoothedWidth = 0.0;
	m_recentMaxSpeed = 0.0;
	m_recentMaxDist = 0.0;
	m_lastAcceleration = 0.0;
	m_recentMaxAccel = 0.0;

	m_lastTime = std::chrono::steady_clock::now();

	StampInterpolated(pt, pressure, pt, pressure, 0.0);
}

// 物理インク拡散シミュレーション (GPU/バッファ)
bool GpuInk::PropagateInk_NoLock()
{
	if (m_width <= 0 || m_height <= 0 || m_ink.empty()) return false;
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

	m_activeMinX = nextMinX;
	m_activeMaxX = nextMaxX;
	m_activeMinY = nextMinY;
	m_activeMaxY = nextMaxY;

	return true;
}

void GpuInk::StampInterpolated(POINT a, UINT pa, POINT b, UINT pb, double dtSeconds)
{
	int dx = b.x - a.x;
	int dy = b.y - a.y;
	double dist = std::hypot(static_cast<double>(dx), static_cast<double>(dy));

	extern int g_maxPressure;
	double maxPrs = (g_maxPressure > 0) ? static_cast<double>(g_maxPressure) : 1024.0;

	// ① 実効上限を 1.3 倍に広げて 100% 張り付き（カンスト）を防止
	double rawFactor = static_cast<double>(pb) / (maxPrs * 1.3);
	if (rawFactor > 1.0) rawFactor = 1.0;
	if (rawFactor < 0.0) rawFactor = 0.0;

	// ② ガンマ補正（1.5乗）で弱い力〜強い力の微妙な加減を引き延ばす
	double pressureFactor = std::pow(rawFactor, 1.5);

	// 傾き・角度計算 (ScribbleDemo 方式)
	double altitudeDegrees = (m_penAltitude > 0) ? (static_cast<double>(m_penAltitude) / 10.0) : 90.0;
	double azimuthRad = (static_cast<double>(m_penAzimuth) / 10.0) * (3.14159265358979323846 / 180.0);
	double tiltFactor = (90.0 - altitudeDegrees) / 90.0;
	if (tiltFactor < 0.0) tiltFactor = 0.0;
	if (tiltFactor > 1.0) tiltFactor = 1.0;

	double moveAngle = (dist > 1e-5) ? std::atan2(static_cast<double>(dy), static_cast<double>(dx)) : 0.0;

	// 「止め・はね・角度」による太さ補正 (ScribbleDemo 方式)
	// 止め
	double tomeFactor = 1.0 + 0.1 * (1.0 - (std::min)(dist / 3.0, 1.0)) * std::pow(pressureFactor, 0.8);
	// はらい、はね
	double haraiPower = 2.7 + 0.5 * (std::min)(dist, 10.0);
	double haraiFactor = std::pow(pressureFactor, haraiPower);
	// 角度
	double angleDiff = std::sin(azimuthRad - (moveAngle + 1.57079632679));
	double angleFactor = 1.0 + 0.3 * std::abs(angleDiff);

	// 最終的な太さ算出
	double baseMaxWidth = 36.0;
	double rawWidth = baseMaxWidth * haraiFactor * tomeFactor * angleFactor * (1.0 + tiltFactor * 0.6);

	// ローパスフィルタ (平滑化)
	if (m_lastPressure == 0 || dist > 40.0 || m_smoothedWidth <= 0.0)
	{
		m_smoothedWidth = rawWidth;
	}
	else
	{
		const double alpha = 0.3;
		m_smoothedWidth = m_smoothedWidth * (1.0 - alpha) + rawWidth * alpha;
	}

	double targetRadius = m_smoothedWidth / 2.0;
	if (targetRadius < 0.5) targetRadius = 0.5;

	int stampRadius = std::max(1, static_cast<int>(std::lround(targetRadius)));

	// 方向ベクトル
	double dirX = static_cast<double>(dx);
	double dirY = static_cast<double>(dy);
	double dirLen = std::sqrt(dirX * dirX + dirY * dirY);
	double ux = m_lastDirX;
	double uy = m_lastDirY;

	if (dirLen >= 1.0)
	{
		ux = dirX / dirLen;
		uy = dirY / dirLen;
		m_lastDirX = ux;
		m_lastDirY = uy;
	}

	m_recentMaxDist = std::max(m_recentMaxDist, dist);

	double speed = 0.0;
	if (dtSeconds > 0.002)
	{
		speed = dist / dtSeconds;
		speed = std::min(MAX_SPEED_PX_PER_SEC, speed);
		if (m_lastSpeed > 0.0)
		{
			double accel = (speed - m_lastSpeed) / dtSeconds;
			m_lastAcceleration = accel;
			m_recentMaxAccel = std::max(m_recentMaxAccel, accel);
		}
		m_lastSpeed = speed;
		m_recentMaxSpeed = std::max(m_recentMaxSpeed, speed);
	}

	double speedNorm = std::min(1.0, speed / MAX_SPEED_PX_PER_SEC);
	double elongation = ELLIPSE_MIN_ELONGATION + (ELLIPSE_MAX_ELONGATION - ELLIPSE_MIN_ELONGATION) * speedNorm;

	// スタンプ打刻（補間）
	double step = std::max(1.0, static_cast<double>(stampRadius) * 0.3);
	int steps = static_cast<int>(std::max(1.0, std::ceil(dist / step)));

	for (int i = 0; i <= steps; ++i)
	{
		double t = (steps == 0) ? 0.0 : static_cast<double>(i) / static_cast<double>(steps);
		POINT p;
		p.x = static_cast<LONG>(a.x + (b.x - a.x) * t + 0.5);
		p.y = static_cast<LONG>(a.y + (b.y - a.y) * t + 0.5);

		double stepPres = pressureFactor;
		uint8_t alpha = static_cast<uint8_t>(std::min(255u, static_cast<unsigned int>(30 + stepPres * 225.0)));

		StampBrush(p.x, p.y, stampRadius, alpha);
	}
}

KinematicsInfo GpuInk::GetKinematicsInfo()
{
	std::lock_guard<std::mutex> lock(m_mutex);
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
	info.currentAltitude = m_penAltitude;
	info.currentAzimuth = m_penAzimuth;
	info.isHovering = m_isHovering;
	return info;
}

void GpuInk::UpdatePenZ(int z, int altitude, int azimuth, bool hovering)
{
	std::lock_guard<std::mutex> lock(m_mutex);
	m_penZ = z;
	m_penAltitude = altitude;
	m_penAzimuth = azimuth;
	m_isHovering = hovering;
}

// ストローク終了 (ユーザー指示に基づき「跳ね払い」のスタンプ生成処理は削除)
void GpuInk::EndStroke()
{
	std::lock_guard<std::mutex> lock(m_mutex);

	double effectiveSpeed = std::min(MAX_SPEED_PX_PER_SEC, std::max(m_lastSpeed, m_recentMaxDist * 60.0));
	const double MIN_FLICK_SPEED = 35.0;
	bool hasSpeed = (effectiveSpeed > MIN_FLICK_SPEED) || (m_recentMaxDist >= 1.0);
	bool isStopping = (m_lastAcceleration < -3000.0);
	bool isFlick = hasSpeed && !isStopping;

	m_lastEndSpeed = m_lastSpeed;
	m_lastEndEffectiveSpeed = effectiveSpeed;
	m_lastEndAccel = m_lastAcceleration;
	m_lastIsFlick = isFlick;

	// 跳ね払い（flick tail）スタンプは行わず、シンプルにストローク終了
	m_inStroke = false;
}

void GpuInk::StampBrush(double cx, double cy, double radius, unsigned char alpha)
{
	if (radius <= 0.0) radius = 0.5;
	if (m_ink.empty() || m_pixelBuffer.empty()) return;

	uint32_t* pixels = m_pixelBuffer.data();

	// ペンの傾き (altitude/azimuth) から毛束の接地形状（しなり・広がり）を推定
	double altitudeDegrees = (m_penAltitude > 0) ? (static_cast<double>(m_penAltitude) / 10.0) : 90.0;
	double tiltFactor = (90.0 - altitudeDegrees) / 90.0;
	if (tiltFactor < 0.0) tiltFactor = 0.0;
	if (tiltFactor > 1.0) tiltFactor = 1.0;

	double azimuthRad = (static_cast<double>(m_penAzimuth) / 10.0) * (3.14159265358979323846 / 180.0);
	double angRad = azimuthRad + 1.57079632679; // 毛束の接地広がり方向

	double rad = radius;
	double semiMajor = rad * (1.0 + tiltFactor * 1.5);
	double semiMinor = std::max(0.5, rad * (1.0 - tiltFactor * 0.4));

	int ext = static_cast<int>(std::ceil(semiMajor));
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

			double normDistSq = (localX * localX) / (semiMajor * semiMajor) + (localY * localY) / (semiMinor * semiMinor);
			if (normDistSq > 1.0) continue;

			size_t idx = static_cast<size_t>(y) * static_cast<size_t>(m_width) + static_cast<size_t>(x);

			// 段階的インク加算 (Bの方法: 移動時の過剰滲み・ガタガタ防止と長押し滲みの両立)
			int prev = m_ink[idx];
			double distRatio = std::sqrt(normDistSq);
			double falloff = 1.0 - distRatio;
			int inkAdd = static_cast<int>(static_cast<double>(alpha) * 0.5 * (0.4 + 0.6 * falloff));
			if (inkAdd < 1) inkAdd = 1;

			int updated = std::min(MAX_INK_PER_PIXEL, prev + inkAdd);

			if (updated != prev)
			{
				stampChanged = true;
				ExpandDirtyRect_NoLock(x, y);
			}
			m_ink[idx] = updated;

			uint32_t pixelVal = CalculateInkPixel(m_ink[idx]);
			if (pixels[idx] != pixelVal) stampChanged = true;
			pixels[idx] = pixelVal;
		}
	}

	extern HWND g_hInkWnd;
	extern HWND g_mainWnd;
	if (stampChanged)
	{
		if (g_hInkWnd && IsWindow(g_hInkWnd)) InvalidateRect(g_hInkWnd, NULL, FALSE);
		if (g_mainWnd && IsWindow(g_mainWnd)) InvalidateRect(g_mainWnd, NULL, FALSE);
	}
}

void GpuInk::DrawSegmentLinear(POINT a, POINT b, double startWidth, double endWidth, uint8_t inkAlpha)
{
	std::lock_guard<std::mutex> lock(m_mutex);
	EnsureInitialized();
	m_inStroke = true;

	int dx = b.x - a.x;
	int dy = b.y - a.y;
	double dist = std::hypot(static_cast<double>(dx), static_cast<double>(dy));

	double startRadius = startWidth / 2.0;
	if (startRadius < 0.5) startRadius = 0.5;

	double endRadius = endWidth / 2.0;
	if (endRadius < 0.5) endRadius = 0.5;

	double dirX = static_cast<double>(dx);
	double dirY = static_cast<double>(dy);
	double dirLen = std::sqrt(dirX * dirX + dirY * dirY);

	if (dirLen >= 1.0)
	{
		m_lastDirX = dirX / dirLen;
		m_lastDirY = dirY / dirLen;
	}

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
		StampBrush(px, py, currentRadius, inkAlpha);
	}

	m_lastPt = b;
}

void GpuInk::DrawSegment(POINT a, POINT b, double strokeWidth, uint8_t inkAlpha)
{
	DrawSegmentLinear(a, b, strokeWidth, strokeWidth, inkAlpha);
}

void GpuInk::AddPoint(POINT pt, UINT pressure)
{
	std::lock_guard<std::mutex> lock(m_mutex);
	EnsureInitialized();

	auto now = std::chrono::steady_clock::now();
	double dtSec = 0.0;
	if (m_lastTime.time_since_epoch().count() != 0)
	{
		auto dur = now - m_lastTime;
		dtSec = std::chrono::duration_cast<std::chrono::duration<double>>(dur).count();
	}

	StampInterpolated(m_lastPt, m_lastPressure, pt, pressure, dtSec);

	m_lastPt = pt;
	m_lastPressure = pressure;
	m_lastTime = now;
}

void GpuInk::Render(HDC hdc, int destX, int destY)
{
	if (!hdc) return;

	std::lock_guard<std::mutex> lock(m_mutex);
	if (!m_pDCRenderTarget || !m_pInkBitmap || m_width <= 0 || m_height <= 0) return;

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

	RECT rc = { destX, destY, destX + m_width, destY + m_height };
	HRESULT hr = m_pDCRenderTarget->BindDC(hdc, &rc);
	if (SUCCEEDED(hr))
	{
		m_pDCRenderTarget->SetDpi(96.0f, 96.0f);
		m_pDCRenderTarget->BeginDraw();
		D2D1_RECT_F destRect = D2D1::RectF(0.0f, 0.0f, static_cast<float>(m_width), static_cast<float>(m_height));
		m_pDCRenderTarget->DrawBitmap(m_pInkBitmap, &destRect);
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
			if (g_mainWnd && IsWindow(g_mainWnd)) InvalidateRect(g_mainWnd, NULL, FALSE);
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
