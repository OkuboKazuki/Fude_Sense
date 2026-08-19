#pragma once

// Prevent Windows headers from defining min/max macros which break std::min/std::max
#ifndef NOMINMAX
#define NOMINMAX
#endif

// CPU ベースの墨汁インクシステム (GDI)
#include <windows.h>
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

// シンプルな CPU ベースの墨汁描画バッファ (GDI 互換画面表示)
class CpuInk
{
public:
	CpuInk();
	~CpuInk();

	// 初期化 / リサイズ
	bool Initialize(int width, int height);
	void Resize(int width, int height);

	// ストローク操作
	void BeginStroke(POINT pt, UINT pressure);
	void AddPoint(POINT pt, UINT pressure);
	void EndStroke();
	bool IsInStroke() const { return m_inStroke; }

	KinematicsInfo GetKinematicsInfo();

	// 描画 (ウィンドウの WM_PAINT ハンドラ)
	void Render(HDC hdc, int destX = 0, int destY = 0);

	// クリア
	void Clear();

	// デバッグ用: 現在の墨汁量をスレッドセーフにコピーして取得
	// outInk に各ピクセルの ink 値 (row-major)、outWidth/outHeight に解像度を返す
	void GetInkSnapshot(std::vector<int>& outInk, int& outWidth, int& outHeight);

private:
	void ReleaseResources_NoLock(); // ロック未獲得時の解放
	void ReleaseResources();        // 外部から安全に呼び出せるリソース解放

	// ロック獲得済み前提の初期化
	bool Initialize_NoLock(int width, int height);

	void EnsureInitialized();
	// StampBrush 楕円形状 brush (方向ベクトルと長軸倍率に対応)
	void StampBrush(int cx, int cy, int radius, double dirX, double dirY, double elongation, unsigned char alpha, unsigned int premultColor);
	// StampInterpolated ２点間 (a -> b) の補間スタンプ
	void StampInterpolated(POINT a, UINT pa, POINT b, UINT pb, double dtSeconds = 0.0);
	uint32_t Premultiply(uint8_t r, uint8_t g, uint8_t b, uint8_t a);

	// 1フレーム分のにじみ拡散計算 (ロック獲得済み前提。変化があったかを返す)
	bool PropagateInk_NoLock();

	// 拡散バックグラウンドスレッドループ
	void PropagationThreadLoop();

	// 拡散スレッドの安全停止ユーティリティ
	void StopPropagationThread();

	// Dirty Rect 管理ヘルパー
	void ResetDirtyRect_NoLock();
	void ExpandDirtyRect_NoLock(int x, int y);

private:
	int m_width = 0;
	int m_height = 0;

	HBITMAP m_hBitmap = NULL;
	HDC m_memDC = NULL;
	HGDIOBJ m_oldBitmap = NULL;
	void* m_bits = nullptr;

	std::vector<int> m_ink;
	std::vector<int> m_deltaInk;

	int m_dirtyMinX = INT_MAX;
	int m_dirtyMinY = INT_MAX;
	int m_dirtyMaxX = -1;
	int m_dirtyMaxY = -1;

	std::mutex m_mutex;

	bool m_inStroke = false;
	POINT m_lastPt = { 0, 0 };
	UINT m_lastPressure = 0;
	double m_strokeInkLeft = 1.0;

	double m_lastSpeed = 0.0;
	double m_recentMaxSpeed = 0.0;
	double m_recentMaxDist = 0.0;
	double m_lastAcceleration = 0.0;
	double m_recentMaxAccel = 0.0;

	double m_lastEndSpeed = 0.0;
	double m_lastEndEffectiveSpeed = 0.0;
	double m_lastEndAccel = 0.0;
	bool m_lastIsFlick = false;

	std::chrono::steady_clock::time_point m_lastTime;

	double m_lastRadius = 0.0;
	double m_lastDirX = 1.0;
	double m_lastDirY = 0.0;

	std::thread m_propagationThread;
	std::atomic<bool> m_runPropagation{ false };
};