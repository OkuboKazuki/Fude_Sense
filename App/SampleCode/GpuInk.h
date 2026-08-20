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

struct KinematicsInfo
{
	double currentSpeed;
	double currentAccel;
	double recentMaxSpeed;
	double recentMaxAccel;
	double recentMaxDist;
	double lastEndSpeed;
	double lastEndEffectiveSpeed;
	double lastEndAccel;
	bool lastIsFlick;

	int currentZ;
	int currentAltitude;
	int currentAzimuth;
	bool isHovering;
};

// GPU ベースの墨汁インクシステム (Direct2D / Direct3D 11)
class GpuInk
{
public:
	GpuInk();
	~GpuInk();

	// 初期化 / リサイズ
	bool Initialize(HWND hWnd, int width, int height);
	bool Initialize(int width, int height);
	void Resize(int width, int height);

	// ストローク操作
	void BeginStroke(POINT pt, UINT pressure);
	void AddPoint(POINT pt, UINT pressure);
	void DrawSegmentLinear(POINT a, POINT b, double startWidth, double endWidth, uint8_t inkAlpha = 255);
	void DrawSegment(POINT a, POINT b, double strokeWidth, uint8_t inkAlpha = 255);
	void EndStroke(); // 跳ね払い（flick tail）処理なし
	bool IsInStroke() const { return m_inStroke; }

	KinematicsInfo GetKinematicsInfo();
	void UpdatePenZ(int z, int altitude, int azimuth, bool hovering);

	// 描画 (ウィンドウの WM_PAINT ハンドラ)
	void Render(HDC hdc, int destX = 0, int destY = 0);

	// クリア
	void Clear();

	// デバッグ用: 現在の墨汁量をスレッドセーフにコピーして取得
	void GetInkSnapshot(std::vector<int>& outInk, int& outWidth, int& outHeight);

private:
	void ReleaseResources_NoLock();
	void ReleaseResources();

	bool Initialize_NoLock(int width, int height);
	void EnsureInitialized();

	void StampBrush(double cx, double cy, double radius, unsigned char alpha);
	void StampInterpolated(POINT a, UINT pa, POINT b, UINT pb, double dtSeconds = 0.0);

	bool PropagateInk_NoLock();
	void PropagationThreadLoop();
	void StopPropagationThread();

	void ResetDirtyRect_NoLock();
	void ExpandDirtyRect_NoLock(int x, int y);

private:
	int m_width = 0;
	int m_height = 0;

	// Direct2D リソース
	ID2D1Factory* m_pD2DFactory = nullptr;
	ID2D1DCRenderTarget* m_pDCRenderTarget = nullptr;
	ID2D1Bitmap* m_pInkBitmap = nullptr;

	// メモリバッファ (CPU / GPU 物理にじみ計算)
	std::vector<int> m_ink;
	std::vector<int> m_deltaInk;
	std::vector<uint32_t> m_pixelBuffer;

	int m_activeMinX = INT_MAX;
	int m_activeMinY = INT_MAX;
	int m_activeMaxX = -1;
	int m_activeMaxY = -1;

	int m_uploadMinX = INT_MAX;
	int m_uploadMinY = INT_MAX;
	int m_uploadMaxX = -1;
	int m_uploadMaxY = -1;

	std::mutex m_mutex;

	bool m_inStroke = false;
	POINT m_lastPt = { 0, 0 };
	UINT m_lastPressure = 0;

	double m_lastSpeed = 0.0;
	double m_recentMaxSpeed = 0.0;
	double m_recentMaxDist = 0.0;
	double m_lastAcceleration = 0.0;
	double m_recentMaxAccel = 0.0;

	double m_lastEndSpeed = 0.0;
	double m_lastEndEffectiveSpeed = 0.0;
	double m_lastEndAccel = 0.0;
	bool m_lastIsFlick = false;

	int m_penZ = 0;
	int m_penAltitude = 0;
	int m_penAzimuth = 0;
	bool m_isHovering = true;

	std::chrono::steady_clock::time_point m_lastTime;

	double m_smoothedWidth = 0.0;
	double m_lastDirX = 1.0;
	double m_lastDirY = 0.0;

	std::thread m_propagationThread;
	std::atomic<bool> m_runPropagation{ false };
};
