#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <d2d1.h>
#include <d2d1_1.h>
#include <vector>
#include <cstdint>
#include <mutex>
#include <thread>
#include <atomic>
#include <chrono>

#include "InkSnapshot.h"
#include "GpuSimulator.h"

// 1 セグメント分の運筆パラメータ
// 運筆方向・乾き具合など、今後の描画表現に必要な情報をまとめて受け渡す
struct StrokeSegment
{
	POINT a = { 0, 0 };          // セグメント始点（半紙座標）
	POINT b = { 0, 0 };          // セグメント終点（半紙座標）
	double startWidth = 1.0;     // 始点の線幅 (px)
	double endWidth = 1.0;       // 終点の線幅 (px)

	// 運筆方向の単位ベクトル。(0, 0) の場合は a→b から自動算出する
	double dirX = 0.0;
	double dirY = 0.0;

	double dryness = 0.0;        // 筆の乾き具合 (0.0: 潤沢 ~ 1.0: 渇筆)
	uint8_t inkAlpha = 255;      // 墨汁濃度 (0 ~ 255)
};

// GPU ベースの墨汁インクシステム (Direct2D / Direct3D 11)
class GpuInk
{
public:
	GpuInk();
	~GpuInk();

	// 初期化 / リサイズ
	bool Initialize(HWND hWnd, int width, int height);
	// runPropagationThread = false で、にじみを進める背景スレッドを立てない。
	// リプレイのように、時間の進め方を呼び出し側が決める場合に使う。
	bool Initialize(int width, int height, bool runPropagationThread = true);
	void Resize(int width, int height);

	// ストローク操作
	void DrawSegmentLinear(const StrokeSegment& seg);

	// 旧シグネチャ（互換用ラッパー。内部で StrokeSegment を組み立てる）
	void DrawSegmentLinear(POINT a, POINT b, double startWidth, double endWidth, uint8_t inkAlpha = 255);
	void DrawSegment(POINT a, POINT b, double strokeWidth, uint8_t inkAlpha = 255);
	void EndStroke();

	// にじみを1段階進める。背景スレッドを立てていないときに使う。
	// 戻り値は画素が変わったかどうか。
	bool StepPropagation();
	// 水分が落ち着くまで（または最大ステップ数まで）にじみをまとめて進める
	int SettleDiffusion(int maxSteps = 200);
	bool IsInStroke() const { return m_inStroke.load(std::memory_order_relaxed); }

	void UpdatePen(int z, double altitudeDegrees, double azimuthRad, bool hovering);
	void UpdatePenZ(int z, int altitudeTenthDegrees, int azimuthTenthDegrees, bool hovering);
	void SetPressureFactor(double factor);

	// 描画 (ウィンドウの WM_PAINT ハンドラ)
	// dispW/dispH を指定するとその解像度にスケーリングして描画（ズーム対応）
	void Render(HDC hdc, int destX = 0, int destY = 0, int dispW = -1, int dispH = -1);

	// クリア
	void Clear();

	// 「一画戻す」用。CaptureSnapshot は画を書き始める直前の墨・水分を控え、
	// RestoreSnapshot はその状態を書き戻す。どちらも拡散スレッドと同じ
	// ミューテックスで守る。半紙の寸法が変わっていると書き戻せない。
	bool CaptureSnapshot(InkSnapshot& out);
	bool RestoreSnapshot(const InkSnapshot& snap);
	// 控えを90度回して書き戻す（紙だけ表示の出入りで半紙が倒れるとき用）。
	// counterClockwise = true で左回り。寸法は今の半紙に合わせて拾い直す。
	bool RestoreSnapshotRotated(const InkSnapshot& snap, bool counterClockwise);
	// 控えそのものを90度回し、dstW x dstH の寸法へ拾い直す（「一画戻す」の履歴を
	// 紙だけ表示の出入りに追従させる用）。半紙の画素には触れない。
	static bool RotateSnapshot(InkSnapshot& snap, bool counterClockwise, int dstW, int dstH);

	// デバッグ用: 現在の墨汁量をスレッドセーフにコピーして取得
	void GetInkSnapshot(std::vector<int>& outInk, int& outWidth, int& outHeight);

	int GetWidth() const { return m_width; }
	int GetHeight() const { return m_height; }

private:
	void ReleaseResources_NoLock();
	void ReleaseResources();

	bool Initialize_NoLock(int width, int height);
	// 描画先と墨のビットマップを作り直す。ビットマップの中身は m_pixelBuffer から取る。
	bool CreateRenderTarget_NoLock();
	void EnsureInitialized();

	void StampBrush(double cx, double cy, double radius, unsigned char alpha);

	bool PropagateInk_NoLock();
	void PropagationThreadLoop();
	void StopPropagationThread();

	// 墨量から画面用の ARGB バッファを組み直す（書き戻し後に使う）
	void RebuildPixels_NoLock();
	// 墨・水分を書き戻したあとの後始末（画素の組み直し・にじみ範囲・GPU への再転送）
	void FinishRestore_NoLock();

	void ResetDirtyRect_NoLock();
	void ExpandDirtyRect_NoLock(int x, int y);

private:
	int m_width = 0;
	int m_height = 0;
	int m_paperOffsetX = 0;
	int m_paperOffsetY = 0;
	int m_dispWidth = 0;
	int m_dispHeight = 0;

	// Direct2D リソース
	ID2D1Factory* m_pD2DFactory = nullptr;
	ID2D1DCRenderTarget* m_pDCRenderTarget = nullptr;
	ID2D1Bitmap* m_pInkBitmap = nullptr;

	// メモリバッファ (CPU / GPU 物理にじみ計算)
	std::vector<int> m_ink;
	std::vector<int> m_deltaInk;
	std::vector<uint32_t> m_pixelBuffer;

	// 紙の水分場。にじみの駆動と、乾いた紙への墨の流入阻止に用いる
	// (Win32/x86 構成のため 1 画素 1 バイトに抑える)
	std::vector<uint8_t> m_wetField;

	int m_activeMinX = INT_MAX;
	int m_activeMinY = INT_MAX;
	int m_activeMaxX = -1;
	int m_activeMaxY = -1;

	int m_uploadMinX = INT_MAX;
	int m_uploadMinY = INT_MAX;
	int m_uploadMaxX = -1;
	int m_uploadMaxY = -1;

	std::mutex m_mutex;
	std::mutex m_penMutex; // ペンの姿勢・圧力パラメータの同期用

	std::atomic<bool> m_inStroke{ false };
	POINT m_lastPt = { 0, 0 };

	double m_penAltitudeDegrees = 90.0;
	double m_penAzimuthRad = 0.0;
	double m_pressureFactor = 0.0;
	double m_strokeDryness = 0.0; // 描画中セグメントの乾き具合 (StrokeSegment::dryness)

	double m_lastDirX = 1.0;
	double m_lastDirY = 0.0;

	// かすれの毛束レーンを測る運筆座標系の原点（描画中セグメントの始点）
	double m_segOriginX = 0.0;
	double m_segOriginY = 0.0;

	std::thread m_propagationThread;
	std::atomic<bool> m_runPropagation{ false };

	// Direct3D 11 Compute Shader GPU 浸透シミュレータ
	GpuSimulator m_gpuSim;
	bool m_needsGpuUpload = false;
	int  m_gpuDiffusionSteps = 0;       // シミュレーション安全収束用カウンタ
};
