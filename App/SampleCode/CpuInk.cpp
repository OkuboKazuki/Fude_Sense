#include "CpuInk.h"
#include <cmath>
#include <cassert>
#include <algorithm>
#include <vector>
#include <cstring>
#include <thread>
#include <chrono>
#include <climits>

#pragma comment(lib, "Msimg32.lib")

// ============================================================================
// Ink Simulation Parameters
// ============================================================================

// Propagation Threshold
static constexpr int PROPAGATION_THRESHOLD = 440;

// Propagation amount per step
static constexpr int PROPAGATION_AMOUNT = 200;

// Frame propagation fraction
static constexpr double FRAME_PROPAGATION_FRACTION = 1;

// Pressure radius scale
static constexpr double PRESSURE_RADIUS_SCALE = 0.5;

// Max ink amount per pixel
static constexpr int MAX_INK_PER_PIXEL = 1100;

// Brush parameters
static constexpr double PRESSURE_TO_PIXELS = 24.0;
static constexpr double MAX_SPEED_PX_PER_SEC = 2000.0;
static constexpr double ELLIPSE_MIN_ELONGATION = 1.0;
static constexpr double ELLIPSE_MAX_ELONGATION = 1.5;

// Clamp ink helper
static inline int clampInk(int v) { return v < 0 ? 0 : (v > MAX_INK_PER_PIXEL ? MAX_INK_PER_PIXEL : v); }

CpuInk::CpuInk()
{
	m_ink.clear();
	m_deltaInk.clear();
}

CpuInk::~CpuInk()
{
	StopPropagationThread();
	ReleaseResources();
}

void CpuInk::ResetDirtyRect_NoLock()
{
	m_dirtyMinX = INT_MAX;
	m_dirtyMinY = INT_MAX;
	m_dirtyMaxX = -1;
	m_dirtyMaxY = -1;
}

void CpuInk::ExpandDirtyRect_NoLock(int x, int y)
{
	if (x < m_dirtyMinX) m_dirtyMinX = x;
	if (x > m_dirtyMaxX) m_dirtyMaxX = x;
	if (y < m_dirtyMinY) m_dirtyMinY = y;
	if (y > m_dirtyMaxY) m_dirtyMaxY = y;
}

void CpuInk::ReleaseResources_NoLock()
{
	if (m_memDC)
	{
		if (m_oldBitmap)
		{
			SelectObject(m_memDC, m_oldBitmap);
			m_oldBitmap = NULL;
		}
		DeleteDC(m_memDC);
		m_memDC = NULL;
	}

	if (m_hBitmap)
	{
		DeleteObject(m_hBitmap);
		m_hBitmap = NULL;
	}
	m_bits = nullptr;
	m_ink.clear();
	m_deltaInk.clear();
	ResetDirtyRect_NoLock();
	m_width = m_height = 0;
	m_inStroke = false;
}

void CpuInk::ReleaseResources()
{
	StopPropagationThread();
	std::lock_guard<std::mutex> lock(m_mutex);
	ReleaseResources_NoLock();
}

bool CpuInk::Initialize_NoLock(int width, int height)
{
	if (width <= 0 || height <= 0) return false;

	ReleaseResources_NoLock();

	m_width = width;
	m_height = height;

	HDC screenDC = GetDC(NULL);
	if (!screenDC) return false;

	m_memDC = CreateCompatibleDC(screenDC);
	if (!m_memDC)
	{
		ReleaseDC(NULL, screenDC);
		return false;
	}

	BITMAPINFO bmi = { 0 };
	bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
	bmi.bmiHeader.biWidth = m_width;
	bmi.bmiHeader.biHeight = -m_height;
	bmi.bmiHeader.biPlanes = 1;
	bmi.bmiHeader.biBitCount = 32;
	bmi.bmiHeader.biCompression = BI_RGB;

	void* bits = nullptr;
	m_hBitmap = CreateDIBSection(m_memDC, &bmi, DIB_RGB_COLORS, &bits, NULL, 0);
	if (!m_hBitmap || !bits)
	{
		if (m_memDC)
		{
			DeleteDC(m_memDC);
			m_memDC = NULL;
		}
		ReleaseDC(NULL, screenDC);
		return false;
	}

	m_oldBitmap = SelectObject(m_memDC, m_hBitmap);
	m_bits = bits;

	{
		size_t pixels = static_cast<size_t>(m_width) * static_cast<size_t>(m_height);
		uint32_t* p = reinterpret_cast<uint32_t*>(m_bits);
		std::fill_n(p, pixels, 0xFFFFFFFF);
		m_ink.assign(pixels, 0);
		m_deltaInk.assign(pixels, 0);
		ResetDirtyRect_NoLock();
	}

	ReleaseDC(NULL, screenDC);
	return true;
}

bool CpuInk::Initialize(int width, int height)
{
	StopPropagationThread();

	std::lock_guard<std::mutex> lock(m_mutex);
	bool success = Initialize_NoLock(width, height);
	if (success)
	{
		try
		{
			m_runPropagation.store(true);
			m_propagationThread = std::thread(&CpuInk::PropagationThreadLoop, this);
		}
		catch (...)
		{
			m_runPropagation.store(false);
		}
	}
	return success;
}

void CpuInk::Resize(int width, int height)
{
	Initialize(width, height);
}

void CpuInk::EnsureInitialized()
{
	if (!m_memDC || !m_hBitmap || !m_bits)
	{
		Initialize_NoLock(800, 600);
		if (!m_runPropagation.load())
		{
			try
			{
				m_runPropagation.store(true);
				m_propagationThread = std::thread(&CpuInk::PropagationThreadLoop, this);
			}
			catch (...)
			{
				m_runPropagation.store(false);
			}
		}
	}
}

uint32_t CpuInk::Premultiply(uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
	uint32_t aa = a;
	uint32_t rr = (r * aa + 127) / 255;
	uint32_t gg = (g * aa + 127) / 255;
	uint32_t bb = (b * aa + 127) / 255;
	return (static_cast<uint32_t>(aa) << 24) | (rr << 16) | (gg << 8) | bb;
}

void CpuInk::Clear()
{
	std::lock_guard<std::mutex> lock(m_mutex);
	if (m_bits && m_width > 0 && m_height > 0)
	{
		uint32_t* p = reinterpret_cast<uint32_t*>(m_bits);
		size_t pixels = static_cast<size_t>(m_width) * static_cast<size_t>(m_height);
		std::fill_n(p, pixels, 0xFFFFFFFF);
		if (!m_ink.empty())
		{
			std::fill_n(m_ink.data(), m_ink.size(), 0);
		}
		if (!m_deltaInk.empty())
		{
			std::fill_n(m_deltaInk.data(), m_deltaInk.size(), 0);
		}
		ResetDirtyRect_NoLock();
	}
}

void CpuInk::BeginStroke(POINT pt, UINT pressure)
{
	std::lock_guard<std::mutex> lock(m_mutex);
	EnsureInitialized();
	m_inStroke = true;
	m_lastPt = pt;
	m_lastPressure = pressure;
	m_strokeInkLeft = 1.0; // Ink capacity 100%
	m_recentMaxSpeed = 0.0;
	m_recentMaxDist = 0.0;
	m_lastAcceleration = 0.0;
	m_recentMaxAccel = 0.0;

	m_lastTime = std::chrono::steady_clock::now();

	StampInterpolated(pt, pressure, pt, pressure, 0.0);
}

// Physical ink diffusion simulation
bool CpuInk::PropagateInk_NoLock()
{
	if (m_width <= 0 || m_height <= 0 || m_ink.empty()) return false;
	if (m_dirtyMinX > m_dirtyMaxX || m_dirtyMinY > m_dirtyMaxY) return false;

	int startX = std::max(0, m_dirtyMinX - 1);
	int endX = std::min(m_width - 1, m_dirtyMaxX + 1);
	int startY = std::max(0, m_dirtyMinY - 1);
	int endY = std::min(m_height - 1, m_dirtyMaxY + 1);

	// Zero out m_deltaInk within dirty bounds
	for (int y = startY; y <= endY; ++y)
	{
		size_t rowOffset = static_cast<size_t>(y) * static_cast<size_t>(m_width);
		std::fill_n(m_deltaInk.data() + rowOffset + startX, (endX - startX + 1), 0);
	}

	bool changed = false;
	int nextMinX = m_dirtyMinX;
	int nextMaxX = m_dirtyMaxX;
	int nextMinY = m_dirtyMinY;
	int nextMaxY = m_dirtyMaxY;

	for (int y = startY; y <= endY; ++y)
	{
		size_t rowOffset = static_cast<size_t>(y) * static_cast<size_t>(m_width);
		for (int x = startX; x <= endX; ++x)
		{
			size_t i = rowOffset + x;
			int curInk = m_ink[i];
			if (curInk <= PROPAGATION_THRESHOLD) continue;

			int availableInk = curInk - PROPAGATION_THRESHOLD;
			if (availableInk <= 0) continue;

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
				int maxOutflow = std::min(PROPAGATION_AMOUNT, availableInk);
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
				changed = true;
			}
			else
			{
				m_deltaInk[i] -= 10;
				changed = true;
			}
		}
	}

	if (!changed) return false;

	uint32_t* pixels = reinterpret_cast<uint32_t*>(m_bits);

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
					uint32_t pixelVal = (m_ink[i] > 1) ? 0xFF000000 : 0xFFFFFFFF;
					pixels[i] = pixelVal;
				}
			}
		}
	}

	m_dirtyMinX = nextMinX;
	m_dirtyMaxX = nextMaxX;
	m_dirtyMinY = nextMinY;
	m_dirtyMaxY = nextMaxY;

	return true;
}

// Interpolated stamp drawing between point a and b
void CpuInk::StampInterpolated(POINT a, UINT pa, POINT b, UINT pb, double dtSeconds)
{
	int dx = b.x - a.x;
	int dy = b.y - a.y;
	double dist = std::sqrt(double(dx) * dx + double(dy) * dy);

	extern int g_maxPressure;
	int devMax = (g_maxPressure > 0) ? g_maxPressure : 1024;
	double pressureRatioA = std::min(1.0, std::max(0.0, static_cast<double>(pa) / static_cast<double>(devMax)));
	double pressureRatioB = std::min(1.0, std::max(0.0, static_cast<double>(pb) / static_cast<double>(devMax)));

	double dirX = static_cast<double>(dx);
	double dirY = static_cast<double>(dy);
	double dirLen = std::sqrt(dirX*dirX + dirY*dirY);
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
	else
	{
		speed = std::min(MAX_SPEED_PX_PER_SEC, dist * 60.0);
		if (speed > 0.0)
		{
			m_lastSpeed = speed;
			m_recentMaxSpeed = std::max(m_recentMaxSpeed, speed);
		}
	}

	double speedNorm = std::min(1.0, speed / MAX_SPEED_PX_PER_SEC);
	double elongation = ELLIPSE_MIN_ELONGATION + (ELLIPSE_MAX_ELONGATION - ELLIPSE_MIN_ELONGATION) * speedNorm;

	const double MAX_INTERPOLATE_DIST = std::max(32.0, PRESSURE_TO_PIXELS * 4.0);

	if (pa == 0 || pb == 0 || dist > MAX_INTERPOLATE_DIST)
	{
		double presRatio = (pb > 0) ? pressureRatioB : pressureRatioA;
		// Pen pressure controls radius from 10% to 100% (presRatio: 0.0 -> 10%, 1.0 -> 100%)
		double targetRadius = PRESSURE_TO_PIXELS * (0.10 + 0.90 * presRatio);
		const double speedThicknessReduction = 0.65;
		double radiusAfterSpeed = targetRadius * (1.0 - speedNorm * speedThicknessReduction);
		
		if (m_lastRadius > 0.0)
		{
			radiusAfterSpeed = std::max(m_lastRadius * 0.75, std::min(m_lastRadius, radiusAfterSpeed));
		}

		int radius = std::max(1, static_cast<int>(std::lround(radiusAfterSpeed)));
		uint8_t alpha = static_cast<uint8_t>(std::min(255u, static_cast<unsigned int>(30 + presRatio * 225.0)));
		uint32_t prem = Premultiply(0, 0, 0, alpha);

		// If pb == 0 (pen up), EndStroke() renders the flick tail, so skip extra stamp
		if (pb != 0)
		{
			StampBrush(b.x, b.y, radius, dirX, dirY, elongation, alpha, prem);
		}
		return;
	}

	int ra = std::max(1, static_cast<int>(pressureRatioA * PRESSURE_TO_PIXELS));
	int rb = std::max(1, static_cast<int>(pressureRatioB * PRESSURE_TO_PIXELS));
	int maxr = std::max(ra, rb);

	const double spacingFactor = 0.5;
	double step = std::max(1.0, static_cast<double>(maxr) * spacingFactor);

	int steps = static_cast<int>(std::max(1.0, std::ceil(dist / step)));
	for (int i = 0; i <= steps; ++i)
	{
		double t = steps == 0 ? 0.0 : double(i) / double(steps);
		POINT p;
		p.x = static_cast<LONG>(a.x + (b.x - a.x) * t + 0.5);
		p.y = static_cast<LONG>(a.y + (b.y - a.y) * t + 0.5);

		double presRatio = pressureRatioA + (pressureRatioB - pressureRatioA) * t;

		// Pen pressure controls radius from 10% to 100% (presRatio: 0.0 -> 10%, 1.0 -> 100%)
		double targetRadius = PRESSURE_TO_PIXELS * (0.10 + 0.90 * presRatio);

		// Speed-based thickness reduction (0.65 -> max 65% reduction at max speed)
		const double speedThicknessReduction = 0.65;
		double localSpeedNorm = std::min(1.0, speed / MAX_SPEED_PX_PER_SEC);
		double radiusAfterSpeed = targetRadius * (1.0 - localSpeedNorm * speedThicknessReduction);

		// Radius low-pass filter for smooth transition (TAU = 30ms)
		const double TAU = 0.03;
		double alphaS = 1.0;
		if (dtSeconds > 1e-9)
		{
			alphaS = 1.0 - std::exp(-dtSeconds / TAU);
		}
		else
		{
			alphaS = 1.0;
		}

		if (m_lastRadius > 0.0 && radiusAfterSpeed > m_lastRadius * 1.25)
		{
			radiusAfterSpeed = m_lastRadius * 1.25;
		}
		if (m_lastRadius > 0.0 && radiusAfterSpeed < m_lastRadius * 0.75)
		{
			radiusAfterSpeed = m_lastRadius * 0.75;
		}

		if (m_lastRadius <= 0.0 || dtSeconds < 1e-9)
		{
			m_lastRadius = std::min(PRESSURE_TO_PIXELS * 0.5, radiusAfterSpeed);
		}
		else
		{
			m_lastRadius = m_lastRadius * (1.0 - alphaS) + radiusAfterSpeed * alphaS;
		}

		double consumed = (step * 0.0004) * (presRatio + 0.3);
		m_strokeInkLeft = std::max(0.20, m_strokeInkLeft - consumed);

		int stampRadius = std::max(1, static_cast<int>(std::lround(m_lastRadius)));
		uint8_t alpha = static_cast<uint8_t>(std::min(255u, static_cast<unsigned int>((30 + presRatio * 225.0) * m_strokeInkLeft)));
		uint32_t prem = Premultiply(0, 0, 0, alpha);

		StampBrush(p.x, p.y, stampRadius, ux, uy, elongation, alpha, prem);
	}
}

KinematicsInfo CpuInk::GetKinematicsInfo()
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
	return info;
}

// End of stroke: generates flick tail on pen-up
void CpuInk::EndStroke()
{
	std::lock_guard<std::mutex> lock(m_mutex);

	// 直前速度と直前移動量から実効速度を算出
	double effectiveSpeed = std::min(MAX_SPEED_PX_PER_SEC, std::max(m_lastSpeed, m_recentMaxDist * 60.0));

	const double MIN_FLICK_SPEED = 35.0;
	bool hasSpeed = (effectiveSpeed > MIN_FLICK_SPEED) || (m_recentMaxDist >= 1.0);
	bool isStopping = (m_lastAcceleration < -3000.0);

	// 速度があり、かつ急減速停止（STOP）でない場合に「跳ね」と判断
	bool isFlick = hasSpeed && !isStopping;

	m_lastEndSpeed = m_lastSpeed;
	m_lastEndEffectiveSpeed = effectiveSpeed;
	m_lastEndAccel = m_lastAcceleration;
	m_lastIsFlick = isFlick;

	if (isFlick && m_bits && m_width > 0 && m_height > 0)
	{
		extern int g_maxPressure;
		int devMax = (g_maxPressure > 0) ? g_maxPressure : 1024;
		double basePressureRatio = std::min(1.0, std::max(0.0, static_cast<double>(m_lastPressure) / static_cast<double>(devMax)));

		double ux = m_lastDirX;
		double uy = m_lastDirY;
		double len = std::sqrt(ux*ux + uy*uy);
		if (len < 1e-6) { ux = 1.0; uy = 0.0; len = 1.0; }
		ux /= len; uy /= len;

		double startRadius = (m_lastRadius > 0.0) ? m_lastRadius : 1.6;

		double excessSpeed = std::max(0.0, effectiveSpeed - 10.0);
		double accelBonus = std::min(25.0, std::max(0.0, m_recentMaxAccel * 0.008));
		double calculatedLen = startRadius * 2.5 + excessSpeed * 0.025 + accelBonus;
		double totalTailLen = std::min(100.0, std::max(startRadius * 1.5, calculatedLen));

		double stepPx = 0.5;
		int tailSteps = static_cast<int>(totalTailLen / stepPx);

		// Smooth linear tapering flick tail starting from original stroke radius
		for (int i = 1; i <= tailSteps; ++i)
		{
			double dist = i * stepPx;
			int px = static_cast<int>(m_lastPt.x + ux * dist + 0.5);
			int py = static_cast<int>(m_lastPt.y + uy * dist + 0.5);

			double t = static_cast<double>(i) / static_cast<double>(tailSteps);
			double fadeRadius = 1.0 - t;
			double fadeAlpha = (1.0 - t) * (1.0 - t);

			int radius = static_cast<int>(std::lround(startRadius * fadeRadius));
			if (radius <= 0) break;

			uint8_t alpha = static_cast<uint8_t>(std::min(85.0, (20.0 + basePressureRatio * 65.0) * fadeAlpha));
			if (alpha < 5) break;

			StampBrush(px, py, radius, ux, uy, 1.0, alpha, Premultiply(0, 0, 0, alpha));
		}
	}

	m_inStroke = false;
}

void CpuInk::StampBrush(int cx, int cy, int radius, double /*dirX*/, double /*dirY*/, double /*elongation*/, unsigned char alpha, unsigned int premultColor)
{
	if (!m_bits) return;
	if (radius <= 0) radius = 1;
	if (m_ink.empty()) return;

	uint32_t* pixels = reinterpret_cast<uint32_t*>(m_bits);
	uint8_t brushA = static_cast<uint8_t>(alpha);

	int ext = static_cast<int>(std::ceil(static_cast<double>(radius)));
	int left = std::max(0, cx - ext);
	int right = std::min(m_width - 1, cx + ext);
	int top = std::max(0, cy - ext);
	int bottom = std::min(m_height - 1, cy + ext);

	bool stampChanged = false;

	for (int y = top; y <= bottom; ++y)
	{
		int dy = y - cy;
		for (int x = left; x <= right; ++x)
		{
			int dx = x - cx;
			double dist = std::sqrt(static_cast<double>(dx * dx + dy * dy));
			if (dist > radius) continue;

			double fall = 1.0 - (dist / static_cast<double>(radius));
			if (fall <= 0.0) continue;

			size_t idx = static_cast<size_t>(y) * static_cast<size_t>(m_width) + static_cast<size_t>(x);

			int baseAdd = static_cast<int>(brushA * fall + 0.5);
			if (baseAdd <= 0) continue;

			double normalizedRadius = static_cast<double>(radius) / (PRESSURE_TO_PIXELS > 0.0 ? PRESSURE_TO_PIXELS : 24.0);
			double thicknessScale = std::max(0.15, std::min(1.0, normalizedRadius));

			int add = static_cast<int>(std::lround(baseAdd * thicknessScale)) * 4;
			if (add <= 0) add = 4;

			bool isSoftStamp = (brushA < 100);
			if (isSoftStamp)
			{
				int cur = m_ink[idx];
				int maxAllowed = PROPAGATION_THRESHOLD - 15;
				if (cur + add > maxAllowed)
				{
					add = std::max(0, maxAllowed - cur);
				}
			}

			int prev = m_ink[idx];
			int updated = clampInk(prev + add);
			if (updated != prev)
			{
				stampChanged = true;
				ExpandDirtyRect_NoLock(x, y);
			}
			m_ink[idx] = updated;

			uint32_t pixelVal = (m_ink[idx] > 1) ? 0xFF000000 : 0xFFFFFFFF;
			if (pixels[idx] != pixelVal) stampChanged = true;
			pixels[idx] = pixelVal;
		}
	}

	extern HWND g_hInkWnd;
	extern HWND g_mainWnd;
	if (stampChanged)
	{
		if (g_hInkWnd && IsWindow(g_hInkWnd))
		{
			InvalidateRect(g_hInkWnd, NULL, FALSE);
		}
		if (g_mainWnd && IsWindow(g_mainWnd))
		{
			InvalidateRect(g_mainWnd, NULL, FALSE);
		}
	}
}

void CpuInk::AddPoint(POINT pt, UINT pressure)
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
	else
	{
		dtSec = 0.0;
	}

	StampInterpolated(m_lastPt, m_lastPressure, pt, pressure, dtSec);

	m_lastPt = pt;
	m_lastPressure = pressure;
	m_lastTime = now;
}

void CpuInk::Render(HDC hdc, int destX, int destY)
{
	if (!hdc) return;

	std::lock_guard<std::mutex> lock(m_mutex);
	if (!m_memDC || !m_bits || m_width <= 0 || m_height <= 0) return;

	BLENDFUNCTION bf;
	bf.BlendOp = AC_SRC_OVER;
	bf.BlendFlags = 0;
	bf.SourceConstantAlpha = 255;
	bf.AlphaFormat = AC_SRC_ALPHA;

	AlphaBlend(hdc, destX, destY, m_width, m_height, m_memDC, 0, 0, m_width, m_height, bf);
}

void CpuInk::GetInkSnapshot(std::vector<int>& outInk, int& outWidth, int& outHeight)
{
	std::lock_guard<std::mutex> lock(m_mutex);
	outWidth = m_width;
	outHeight = m_height;
	outInk = m_ink;
}

void CpuInk::PropagationThreadLoop()
{
	using namespace std::chrono_literals;
	const auto frameTime = 16ms;

	while (m_runPropagation.load())
	{
		bool updated = false;
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			if (m_bits && m_width > 0 && m_height > 0 && !m_ink.empty())
			{
				updated = PropagateInk_NoLock();
			}
		}

		if (updated)
		{
			extern HWND g_hInkWnd;
			extern HWND g_mainWnd;
			if (g_hInkWnd && IsWindow(g_hInkWnd))
			{
				InvalidateRect(g_hInkWnd, NULL, FALSE);
			}
			if (g_mainWnd && IsWindow(g_mainWnd))
			{
				InvalidateRect(g_mainWnd, NULL, FALSE);
			}
		}

		std::this_thread::sleep_for(frameTime);
	}
}

void CpuInk::StopPropagationThread()
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