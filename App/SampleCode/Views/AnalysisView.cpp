#include "stdafx.h"
#include "AnalysisView.h"
#include "RenderUtils.h"
#include <cwchar>
#include <cmath>
#include <algorithm>

void AnalysisView::Draw(HDC dc, const AppState& state) {
    DrawMetricsCard(dc, state.ui.rAnalysisMetricsBox, state);
    DrawTiltCompass(dc, state.ui.rAnalysisCompassBox, state);
    DrawWaveformGraph(dc, state.ui.rAnalysisGraphBox, state);
}

void AnalysisView::DrawMetricsCard(HDC dc, const RECT& rBox, const AppState& state) {
    using namespace RenderUtils;
    Box(dc, rBox, RGB(28, 31, 38), RGB(48, 54, 68), 1, 8);

    const RealtimeMetrics& m = state.trajectory.GetRealtimeMetrics();
    size_t strokeCount = state.trajectory.GetTotalStrokeCount();
    if (state.trajectory.IsRecordingStroke()) {
        strokeCount += 1;
    }

    // 4分割メトリクスグリッド
    int w = RW(rBox);
    int colW = (w - 20) / 4;

    auto DrawMetricItem = [&](int colIdx, const wchar_t* title, const wchar_t* valStr, COLORREF valColor) {
        RECT rItem = { rBox.left + 10 + colIdx * colW, rBox.top + 6, rBox.left + 10 + (colIdx + 1) * colW - 4, rBox.bottom - 6 };
        
        RECT rTitle = { rItem.left, rItem.top, rItem.right, rItem.top + 22 };
        HFONT fTitle = CreateCustomFont(13, FW_BOLD);
        Center(dc, rTitle, title, fTitle, RGB(150, 160, 180));
        DeleteObject(fTitle);

        RECT rVal = { rItem.left, rItem.top + 22, rItem.right, rItem.bottom };
        HFONT fVal = CreateCustomFont(20, FW_BOLD);
        Center(dc, rVal, valStr, fVal, valColor);
        DeleteObject(fVal);
    };

    wchar_t bufPrs[32];
    swprintf_s(bufPrs, 32, L"%.0f%%", m.currentPressure * 100.0);
    DrawMetricItem(0, L"筆圧 (圧)", bufPrs, m.currentPressure > 0.05 ? RGB(80, 210, 255) : RGB(160, 170, 185));

    wchar_t bufTilt[32];
    swprintf_s(bufTilt, 32, L"%.0f°", m.currentAltitude);
    DrawMetricItem(1, L"高度角 (傾)", bufTilt, RGB(120, 230, 160));

    wchar_t bufSpeed[32];
    swprintf_s(bufSpeed, 32, L"%.0f", m.currentSpeed);
    DrawMetricItem(2, L"速度 (px/s)", bufSpeed, RGB(255, 195, 80));

    wchar_t bufStroke[32];
    swprintf_s(bufStroke, 32, L"%d 画", static_cast<int>(strokeCount));
    DrawMetricItem(3, L"総画数", bufStroke, RGB(220, 160, 255));
}

void AnalysisView::DrawTiltCompass(HDC dc, const RECT& rBox, const AppState& state) {
    using namespace RenderUtils;
    Box(dc, rBox, RGB(24, 27, 34), RGB(45, 52, 66), 1, 8);

    RECT rHeader = { rBox.left + 12, rBox.top + 8, rBox.right - 12, rBox.top + 28 };
    HFONT fHeader = CreateCustomFont(14, FW_BOLD);
    DrawTextCustom(dc, rHeader, L"筆姿勢・傾きレーダー（方位＆高度角）", fHeader, RGB(200, 210, 230));
    DeleteObject(fHeader);

    int boxW = RW(rBox);
    int boxH = RH(rBox);
    int centerX = rBox.left + boxW / 2 - 40;
    int centerY = rBox.top + 30 + (boxH - 36) / 2;
    int r1 = centerX - rBox.left - 18;
    int r2 = (boxH - 46) / 2;
    int radius = (r1 < r2) ? r1 : r2;
    if (radius < 30) radius = 30;

    // 背景同心円 (90° 中心、60°, 30°, 0° 外周)
    HPEN gridPen = CreatePen(PS_SOLID, 1, RGB(42, 48, 62));
    HPEN oldPen = (HPEN)SelectObject(dc, gridPen);
    HBRUSH oldBrush = (HBRUSH)SelectObject(dc, GetStockObject(NULL_BRUSH));

    Ellipse(dc, centerX - radius, centerY - radius, centerX + radius, centerY + radius);
    Ellipse(dc, centerX - radius * 2 / 3, centerY - radius * 2 / 3, centerX + radius * 2 / 3, centerY + radius * 2 / 3);
    Ellipse(dc, centerX - radius / 3, centerY - radius / 3, centerX + radius / 3, centerY + radius / 3);

    // 十字線
    MoveToEx(dc, centerX - radius - 4, centerY, nullptr);
    LineTo(dc, centerX + radius + 4, centerY);
    MoveToEx(dc, centerX, centerY - radius - 4, nullptr);
    LineTo(dc, centerX, centerY + radius + 4);

    SelectObject(dc, oldPen);
    DeleteObject(gridPen);

    // 方位ラベル
    HFONT fLabel = CreateCustomFont(12, FW_BOLD);
    RECT rN = { centerX - 18, centerY - radius - 16, centerX + 18, centerY - radius };
    Center(dc, rN, L"上 (N)", fLabel, RGB(140, 150, 168));
    RECT rS = { centerX - 18, centerY + radius, centerX + 18, centerY + radius + 16 };
    Center(dc, rS, L"下 (S)", fLabel, RGB(140, 150, 168));
    DeleteObject(fLabel);

    const RealtimeMetrics& m = state.trajectory.GetRealtimeMetrics();
    double alt = Clamp(m.currentAltitude, 0.0, 90.0);
    double azmRad = m.currentAzimuth * (3.14159265358979323846 / 180.0);

    // 傾きオフセット計算（高度90°で中心、0°で外周）
    double tiltNorm = (90.0 - alt) / 90.0;
    int tipX = centerX + static_cast<int>(radius * tiltNorm * std::cos(azmRad));
    int tipY = centerY - static_cast<int>(radius * tiltNorm * std::sin(azmRad)); // 上方向がY-

    // 筆軸ライン（中心からペン先へ）
    HPEN linePen = CreatePen(PS_SOLID, 2, RGB(80, 200, 255));
    oldPen = (HPEN)SelectObject(dc, linePen);
    MoveToEx(dc, centerX, centerY, nullptr);
    LineTo(dc, tipX, tipY);
    SelectObject(dc, oldPen);
    DeleteObject(linePen);

    // ペン先現在位置マーカー
    int markerR = 6;
    HBRUSH mBrush = CreateSolidBrush(m.isPenDown ? RGB(255, 100, 100) : RGB(80, 220, 255));
    HPEN mPen = CreatePen(PS_SOLID, 1, RGB(255, 255, 255));
    oldBrush = (HBRUSH)SelectObject(dc, mBrush);
    oldPen = (HPEN)SelectObject(dc, mPen);
    Ellipse(dc, tipX - markerR, tipY - markerR, tipX + markerR, tipY + markerR);
    SelectObject(dc, oldPen);
    SelectObject(dc, oldBrush);
    DeleteObject(mPen);
    DeleteObject(mBrush);

    // 右側数値詳細リスト
    int infoLeft = centerX + radius + 18;
    int infoTop = rBox.top + 36;
    HFONT fInfo = CreateCustomFont(13, FW_NORMAL);
    HFONT fInfoB = CreateCustomFont(15, FW_BOLD);

    auto DrawInfoRow = [&](int rowIdx, const wchar_t* label, const wchar_t* val, COLORREF c) {
        int y = infoTop + rowIdx * 30;
        RECT rL = { infoLeft, y, infoLeft + 58, y + 24 };
        DrawTextCustom(dc, rL, label, fInfo, RGB(150, 160, 178));
        RECT rV = { infoLeft + 60, y, rBox.right - 8, y + 24 };
        DrawTextCustom(dc, rV, val, fInfoB, c);
    };

    wchar_t bAzm[32], bAlt[32], bState[32];
    swprintf_s(bAzm, 32, L"%.1f°", m.currentAzimuth);
    swprintf_s(bAlt, 32, L"%.1f°", alt);
    swprintf_s(bState, 32, L"%s", m.isPenDown ? L"着筆中" : L"ホバー");

    DrawInfoRow(0, L"方位角:", bAzm, RGB(230, 240, 255));
    DrawInfoRow(1, L"高度角:", bAlt, RGB(120, 230, 160));
    DrawInfoRow(2, L"状態:", bState, m.isPenDown ? RGB(255, 120, 120) : RGB(100, 190, 255));

    DeleteObject(fInfo);
    DeleteObject(fInfoB);
}

void AnalysisView::DrawWaveformGraph(HDC dc, const RECT& rBox, const AppState& state) {
    using namespace RenderUtils;
    Box(dc, rBox, RGB(20, 23, 30), RGB(45, 52, 66), 1, 8);

    // ヘッダー & 凡例
    RECT rHeader = { rBox.left + 12, rBox.top + 8, rBox.left + 180, rBox.top + 28 };
    HFONT fHeader = CreateCustomFont(14, FW_BOLD);
    DrawTextCustom(dc, rHeader, L"運筆波形（筆圧・速度推移）", fHeader, RGB(200, 210, 230));
    DeleteObject(fHeader);

    // 凡例
    HFONT fLegend = CreateCustomFont(12, FW_BOLD);
    RECT rLeg1 = { rBox.right - 180, rBox.top + 8, rBox.right - 95, rBox.top + 26 };
    DrawTextCustom(dc, rLeg1, L"■ 筆圧 (0-100%)", fLegend, RGB(80, 210, 255));

    RECT rLeg2 = { rBox.right - 90, rBox.top + 8, rBox.right - 10, rBox.top + 26 };
    DrawTextCustom(dc, rLeg2, L"■ 速度 (px/s)", fLegend, RGB(255, 190, 70));
    DeleteObject(fLegend);

    // グラフプロット領域
    RECT rPlot = { rBox.left + 42, rBox.top + 34, rBox.right - 14, rBox.bottom - 24 };
    Fill(dc, rPlot, RGB(14, 16, 21));

    // 目盛り線（0%, 50%, 100%）
    HPEN gridPen = CreatePen(PS_DOT, 1, RGB(38, 44, 58));
    HPEN oldPen = (HPEN)SelectObject(dc, gridPen);

    int plotW = RW(rPlot);
    int plotH = RH(rPlot);

    int y0 = rPlot.bottom;
    int y50 = rPlot.top + plotH / 2;
    int y100 = rPlot.top;

    MoveToEx(dc, rPlot.left, y50, nullptr); LineTo(dc, rPlot.right, y50);
    MoveToEx(dc, rPlot.left, y100, nullptr); LineTo(dc, rPlot.right, y100);

    SelectObject(dc, oldPen);
    DeleteObject(gridPen);

    // 目盛りラベル
    HFONT fTick = CreateCustomFont(12, FW_BOLD);
    RECT rL100 = { rBox.left + 2, y100 - 8, rPlot.left - 4, y100 + 12 };
    DrawTextCustom(dc, rL100, L"1.0", fTick, RGB(130, 140, 160), DT_RIGHT | DT_SINGLELINE);
    RECT rL50 = { rBox.left + 2, y50 - 8, rPlot.left - 4, y50 + 12 };
    DrawTextCustom(dc, rL50, L"0.5", fTick, RGB(130, 140, 160), DT_RIGHT | DT_SINGLELINE);
    RECT rL0 = { rBox.left + 2, y0 - 12, rPlot.left - 4, y0 + 6 };
    DrawTextCustom(dc, rL0, L"0.0", fTick, RGB(130, 140, 160), DT_RIGHT | DT_SINGLELINE);
    DeleteObject(fTick);

    // プロットデータの描画
    const StrokeData* pStroke = state.trajectory.GetCurrentOrLastStroke();
    if (!pStroke || pStroke->points.empty()) {
        HFONT fEmpty = CreateCustomFont(14, FW_NORMAL);
        Center(dc, rPlot, L"（筆を半紙に下ろすとリアルタイム波形が表示されます）", fEmpty, RGB(110, 120, 140));
        DeleteObject(fEmpty);
        return;
    }

    const auto& pts = pStroke->points;
    size_t count = pts.size();

    // 速度の最大値スケール計算
    double maxSpd = (std::max)(500.0, pStroke->maxSpeed * 1.1);

    // 1. 速度波形描画 (オレンジ破線/細線)
    HPEN spdPen = CreatePen(PS_SOLID, 1, RGB(220, 160, 50));
    oldPen = (HPEN)SelectObject(dc, spdPen);

    for (size_t i = 0; i < count; ++i) {
        double normTime = (count > 1) ? static_cast<double>(i) / static_cast<double>(count - 1) : 0.5;
        int gx = rPlot.left + static_cast<int>(plotW * normTime);
        double spdNorm = Clamp(pts[i].speedPxPerSec / maxSpd, 0.0, 1.0);
        int gy = rPlot.bottom - static_cast<int>(plotH * spdNorm);

        if (i == 0) MoveToEx(dc, gx, gy, nullptr);
        else LineTo(dc, gx, gy);
    }
    SelectObject(dc, oldPen);
    DeleteObject(spdPen);

    // 2. 筆圧波形描画 (太いシアン線)
    HPEN prsPen = CreatePen(PS_SOLID, 2, RGB(80, 215, 255));
    oldPen = (HPEN)SelectObject(dc, prsPen);

    int peakX = rPlot.left, peakY = rPlot.bottom;
    double maxP = 0.0;

    for (size_t i = 0; i < count; ++i) {
        double normTime = (count > 1) ? static_cast<double>(i) / static_cast<double>(count - 1) : 0.5;
        int gx = rPlot.left + static_cast<int>(plotW * normTime);
        double pNorm = Clamp(pts[i].pressure, 0.0, 1.0);
        int gy = rPlot.bottom - static_cast<int>(plotH * pNorm);

        if (i == 0) MoveToEx(dc, gx, gy, nullptr);
        else LineTo(dc, gx, gy);

        if (pts[i].pressure > maxP) {
            maxP = pts[i].pressure;
            peakX = gx;
            peakY = gy;
        }
    }
    SelectObject(dc, oldPen);
    DeleteObject(prsPen);

    // ピーク筆圧マーカー
    if (maxP > 0.05) {
        HBRUSH pBrush = CreateSolidBrush(RGB(255, 80, 80));
        HBRUSH oldB = (HBRUSH)SelectObject(dc, pBrush);
        HPEN pPen = CreatePen(PS_SOLID, 1, RGB(255, 255, 255));
        oldPen = (HPEN)SelectObject(dc, pPen);

        Ellipse(dc, peakX - 5, peakY - 5, peakX + 5, peakY + 5);

        SelectObject(dc, oldPen);
        SelectObject(dc, oldB);
        DeleteObject(pPen);
        DeleteObject(pBrush);
    }
}
