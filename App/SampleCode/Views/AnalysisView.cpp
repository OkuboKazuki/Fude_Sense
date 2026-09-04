#include "stdafx.h"
#include "AnalysisView.h"
#include "RenderUtils.h"
#include <cwchar>
#include <cmath>
#include <algorithm>

void AnalysisView::Draw(HDC dc, const AppState& state) {
    DrawReplayControls(dc, state.ui.rAnalysisReplayBox, state);
    DrawMetricsCard(dc, state.ui.rAnalysisMetricsBox, state);
    DrawTiltCompass(dc, state.ui.rAnalysisCompassBox, state);
    DrawWaveformGraph(dc, state.ui.rAnalysisGraphBox, state);
}

void AnalysisView::DrawReplayControls(HDC dc, const RECT& rBox, const AppState& state) {
    using namespace RenderUtils;
    Box(dc, rBox, RGB(26, 29, 36), RGB(46, 52, 66), 1, 8);

    const auto& rep = state.replay;
    const auto& session = state.trajectory;
    size_t strokeCount = session.GetTotalStrokeCount();
    // 記録が1画も無い間は再生系を無効表示にする（操作側も受け付けない）
    bool hasRecording = (strokeCount > 0);
    const COLORREF DIS_BG     = RGB(26, 29, 36);
    const COLORREF DIS_BORDER = RGB(44, 48, 60);
    const COLORREF DIS_TEXT   = RGB(88, 95, 110);

    // 1. ヘッダー: タイトル & 時刻表示
    RECT rHeader = { rBox.left + 14, rBox.top + 8, rBox.left + 220, rBox.top + 28 };
    HFONT fHeader = CreateCustomFont(14, FW_BOLD);
    DrawTextCustom(dc, rHeader, L"運筆リプレイ（時系列再生・解析）", fHeader, RGB(210, 220, 240));
    DeleteObject(fHeader);

    double curSec = static_cast<double>(rep.currentTimeMs) / 1000.0;
    double totSec = static_cast<double>(rep.totalDurationMs) / 1000.0;
    int curStrokeDisplay = (rep.currentSample.strokeIndex >= 0) ? (rep.currentSample.strokeIndex + 1) : ((strokeCount > 0) ? 1 : 0);

    wchar_t timeBuf[64];
    if (strokeCount > 0) {
        swprintf_s(timeBuf, 64, L"%.2fs / %.2fs (第 %d / %d 画)", curSec, totSec, curStrokeDisplay, static_cast<int>(strokeCount));
    } else {
        swprintf_s(timeBuf, 64, L"0.00s / 0.00s (筆記待ち)");
    }

    RECT rTime = { rBox.right - 260, rBox.top + 8, rBox.right - 14, rBox.top + 28 };
    HFONT fTime = CreateCustomFont(13, FW_BOLD);
    DrawTextCustom(dc, rTime, timeBuf, fTime, RGB(160, 190, 230), DT_RIGHT | DT_SINGLELINE | DT_VCENTER);
    DeleteObject(fTime);

    // 2. シークバー
    const RECT& rTrack = state.ui.rReplaySeekTrack;
    Box(dc, rTrack, RGB(16, 18, 24), RGB(50, 56, 70), 1, 4);


    // プログレスバー（再生済み部分）
    double prog = (rep.totalDurationMs > 0) ? (static_cast<double>(rep.currentTimeMs) / static_cast<double>(rep.totalDurationMs)) : 0.0;
    prog = Clamp(prog, 0.0, 1.0);
    int trackW = RW(rTrack);
    int progW = static_cast<int>(trackW * prog);
    if (progW > 0) {
        RECT rProg = { rTrack.left, rTrack.top, rTrack.left + progW, rTrack.bottom };
        Fill(dc, rProg, RGB(40, 140, 220));
    }

    // 各画の開始位置目盛り
    if (strokeCount > 1 && rep.totalDurationMs > 0) {
        HPEN tickPen = CreatePen(PS_SOLID, 1, RGB(180, 200, 230));
        HPEN oldP = (HPEN)SelectObject(dc, tickPen);
        for (size_t i = 1; i < strokeCount; ++i) {
            DWORD st = session.GetStrokeTimelineStart(i);
            int tx = rTrack.left + static_cast<int>(trackW * (static_cast<double>(st) / static_cast<double>(rep.totalDurationMs)));
            MoveToEx(dc, tx, rTrack.top, nullptr);
            LineTo(dc, tx, rTrack.bottom);
        }
        SelectObject(dc, oldP);
        DeleteObject(tickPen);
    }

    // シークバーのツマミ (Thumb)
    const RECT& rThumb = state.ui.rReplaySeekThumb;
    bool isThumbActive = hasRecording && (state.ui.hoverReplayBtn == 8 || rep.isDraggingSeekBar);
    COLORREF thumbColor = !hasRecording ? RGB(56, 61, 74) : (isThumbActive ? RGB(255, 230, 100) : RGB(230, 240, 255));
    COLORREF thumbBorder = !hasRecording ? DIS_BORDER : (isThumbActive ? RGB(255, 255, 255) : RGB(80, 160, 240));
    Box(dc, rThumb, thumbColor, thumbBorder, 2, 4);

    // 3. 再生操作ボタン
    HFONT fBtnIcon = CreateCustomFont(15, FW_BOLD);

    // 最初に戻る (↺)
    bool hReset = (state.ui.hoverReplayBtn == 1);
    Box(dc, state.ui.rReplayResetBtn, !hasRecording ? DIS_BG : (hReset ? RGB(45, 52, 68) : RGB(34, 38, 48)), !hasRecording ? DIS_BORDER : (hReset ? RGB(100, 120, 160) : RGB(58, 66, 84)), 1, 6);
    Center(dc, state.ui.rReplayResetBtn, L"↺", fBtnIcon, hasRecording ? RGB(200, 215, 235) : DIS_TEXT);

    // 前画 (⏮)
    bool hPrev = (state.ui.hoverReplayBtn == 2);
    Box(dc, state.ui.rReplayPrevBtn, !hasRecording ? DIS_BG : (hPrev ? RGB(45, 52, 68) : RGB(34, 38, 48)), !hasRecording ? DIS_BORDER : (hPrev ? RGB(100, 120, 160) : RGB(58, 66, 84)), 1, 6);
    Center(dc, state.ui.rReplayPrevBtn, L"⏮", fBtnIcon, hasRecording ? RGB(200, 215, 235) : DIS_TEXT);

    // 再生 / 一時停止 (▶ / ❚❚)
    bool isPlaying = hasRecording && (rep.state == ReplayState::Playing);
    bool hPlay = (state.ui.hoverReplayBtn == 3);
    COLORREF playBg = !hasRecording ? DIS_BG : isPlaying ? (hPlay ? RGB(200, 70, 60) : RGB(170, 50, 45)) : (hPlay ? RGB(40, 160, 230) : RGB(28, 130, 200));
    COLORREF playBorder = !hasRecording ? DIS_BORDER : (isPlaying ? RGB(255, 110, 100) : RGB(80, 190, 255));
    Box(dc, state.ui.rReplayPlayBtn, playBg, playBorder, 1, 6);
    Center(dc, state.ui.rReplayPlayBtn, isPlaying ? L"❚❚ 停止" : L"▶ 再生", fBtnIcon, hasRecording ? RGB(255, 255, 255) : DIS_TEXT);

    // 次画 (⏭)
    bool hNext = (state.ui.hoverReplayBtn == 4);
    Box(dc, state.ui.rReplayNextBtn, !hasRecording ? DIS_BG : (hNext ? RGB(45, 52, 68) : RGB(34, 38, 48)), !hasRecording ? DIS_BORDER : (hNext ? RGB(100, 120, 160) : RGB(58, 66, 84)), 1, 6);
    Center(dc, state.ui.rReplayNextBtn, L"⏭", fBtnIcon, hasRecording ? RGB(200, 215, 235) : DIS_TEXT);

    DeleteObject(fBtnIcon);

    // 4. 再生速度切替ボタン (0.5x, 1.0x, 2.0x)
    HFONT fSpd = CreateCustomFont(13, FW_BOLD);
    const double spdVals[3] = { 0.5, 1.0, 2.0 };
    const wchar_t* spdLabels[3] = { L"0.5x", L"1.0x", L"2.0x" };

    for (int i = 0; i < 3; ++i) {
        bool isSel = (std::abs(rep.playbackSpeed - spdVals[i]) < 0.05);
        bool isHov = (state.ui.hoverReplayBtn == (5 + i));

        COLORREF sBg = isSel ? RGB(35, 75, 125) : (isHov ? RGB(42, 48, 62) : RGB(30, 34, 44));
        COLORREF sBorder = isSel ? RGB(80, 170, 255) : (isHov ? RGB(70, 80, 100) : RGB(50, 56, 72));
        COLORREF sText = isSel ? RGB(240, 250, 255) : (isHov ? RGB(200, 210, 230) : RGB(140, 150, 170));

        Box(dc, state.ui.rReplaySpeedBtn[i], sBg, sBorder, isSel ? 2 : 1, 6);
        Center(dc, state.ui.rReplaySpeedBtn[i], spdLabels[i], fSpd, sText);
    }
    DeleteObject(fSpd);
}

void AnalysisView::DrawMetricsCard(HDC dc, const RECT& rBox, const AppState& state) {
    using namespace RenderUtils;
    Box(dc, rBox, RGB(28, 31, 38), RGB(48, 54, 68), 1, 8);

    double curPrs = 0.0;
    double curAlt = 90.0;
    double curSpd = 0.0;
    size_t strokeCount = state.trajectory.GetTotalStrokeCount();

    if (state.replay.hasValidSample) {
        const auto& p = state.replay.currentSample.point;
        curPrs = p.pressure;
        curAlt = p.altitudeDeg;
        curSpd = p.speedPxPerSec;
    } else {
        const RealtimeMetrics& m = state.trajectory.GetRealtimeMetrics();
        curPrs = m.currentPressure;
        curAlt = m.currentAltitude;
        curSpd = m.currentSpeed;
        if (state.trajectory.IsRecordingStroke()) {
            strokeCount += 1;
        }
    }

    // 4分割メトリクスグリッド
    int w = RW(rBox);
    int colW = (w - 20) / 4;

    auto DrawMetricItem = [&](int colIdx, const wchar_t* title, const wchar_t* valStr, COLORREF valColor) {
        RECT rItem = { rBox.left + 10 + colIdx * colW, rBox.top + 6, rBox.left + 10 + (colIdx + 1) * colW - 4, rBox.bottom - 6 };
        
        RECT rTitle = { rItem.left, rItem.top, rItem.right, rItem.top + 20 };
        HFONT fTitle = CreateCustomFont(12, FW_BOLD);
        Center(dc, rTitle, title, fTitle, RGB(150, 160, 180));
        DeleteObject(fTitle);

        RECT rVal = { rItem.left, rItem.top + 20, rItem.right, rItem.bottom };
        HFONT fVal = CreateCustomFont(18, FW_BOLD);
        Center(dc, rVal, valStr, fVal, valColor);
        DeleteObject(fVal);
    };

    wchar_t bufPrs[32];
    swprintf_s(bufPrs, 32, L"%.0f%%", curPrs * 100.0);
    DrawMetricItem(0, L"筆圧 (圧)", bufPrs, curPrs > 0.05 ? RGB(80, 210, 255) : RGB(160, 170, 185));

    wchar_t bufTilt[32];
    swprintf_s(bufTilt, 32, L"%.0f°", curAlt);
    COLORREF altColor = (curAlt >= 60.0) ? RGB(120, 230, 160) : ((curAlt >= 45.0) ? RGB(255, 210, 80) : RGB(255, 110, 100));
    DrawMetricItem(1, L"高度角 (傾)", bufTilt, altColor);

    wchar_t bufSpeed[32];
    swprintf_s(bufSpeed, 32, L"%.0f", curSpd);
    DrawMetricItem(2, L"速度 (px/s)", bufSpeed, RGB(255, 195, 80));

    wchar_t bufStroke[32];
    swprintf_s(bufStroke, 32, L"%d 画", static_cast<int>(strokeCount));
    DrawMetricItem(3, L"総画数", bufStroke, RGB(220, 160, 255));
}

void AnalysisView::DrawTiltCompass(HDC dc, const RECT& rBox, const AppState& state) {
    using namespace RenderUtils;
    Box(dc, rBox, RGB(24, 27, 34), RGB(45, 52, 66), 1, 8);

    RECT rHeader = { rBox.left + 12, rBox.top + 8, rBox.right - 12, rBox.top + 26 };
    HFONT fHeader = CreateCustomFont(13, FW_BOLD);
    DrawTextCustom(dc, rHeader, L"筆姿勢・傾きレーダー（方位＆高度角）", fHeader, RGB(200, 210, 230));
    DeleteObject(fHeader);

    int boxW = RW(rBox);
    int boxH = RH(rBox);
    int centerX = rBox.left + boxW / 2 - 40;
    int centerY = rBox.top + 26 + (boxH - 32) / 2;
    int r1 = centerX - rBox.left - 18;
    int r2 = (boxH - 40) / 2;
    int radius = (r1 < r2) ? r1 : r2;
    if (radius < 26) radius = 26;

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
    HFONT fLabel = CreateCustomFont(11, FW_BOLD);
    RECT rN = { centerX - 18, centerY - radius - 14, centerX + 18, centerY - radius };
    Center(dc, rN, L"上 (N)", fLabel, RGB(140, 150, 168));
    RECT rS = { centerX - 18, centerY + radius, centerX + 18, centerY + radius + 14 };
    Center(dc, rS, L"下 (S)", fLabel, RGB(140, 150, 168));
    DeleteObject(fLabel);

    double alt = 90.0;
    double azm = 0.0;
    bool isPenDown = false;

    if (state.replay.hasValidSample) {
        const auto& p = state.replay.currentSample.point;
        alt = p.altitudeDeg;
        azm = p.azimuthDeg;
        isPenDown = state.replay.currentSample.isPenDown;
    } else {
        const RealtimeMetrics& m = state.trajectory.GetRealtimeMetrics();
        alt = m.currentAltitude;
        azm = m.currentAzimuth;
        isPenDown = m.isPenDown;
    }

    alt = Clamp(alt, 0.0, 90.0);
    double azmRad = azm * (3.14159265358979323846 / 180.0);

    // 傾きオフセット計算（高度90°で中心、0°で外周）
    double tiltNorm = (90.0 - alt) / 90.0;
    int tipX = centerX + static_cast<int>(radius * tiltNorm * std::sin(azmRad));
    int tipY = centerY - static_cast<int>(radius * tiltNorm * std::cos(azmRad)); // 上方向がY-

    // 筆軸ライン（中心からペン先へ）
    HPEN linePen = CreatePen(PS_SOLID, 2, RGB(80, 200, 255));
    oldPen = (HPEN)SelectObject(dc, linePen);
    MoveToEx(dc, centerX, centerY, nullptr);
    LineTo(dc, tipX, tipY);
    SelectObject(dc, oldPen);
    DeleteObject(linePen);

    // ペン先マーカー
    int markerR = 5;
    COLORREF markerColor = isPenDown ? RGB(255, 100, 100) : RGB(80, 220, 255);
    HBRUSH mBrush = CreateSolidBrush(markerColor);
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
    int infoTop = rBox.top + 32;
    HFONT fInfo = CreateCustomFont(12, FW_NORMAL);
    HFONT fInfoB = CreateCustomFont(14, FW_BOLD);

    auto DrawInfoRow = [&](int rowIdx, const wchar_t* label, const wchar_t* val, COLORREF c) {
        int y = infoTop + rowIdx * 28;
        RECT rL = { infoLeft, y, infoLeft + 54, y + 22 };
        DrawTextCustom(dc, rL, label, fInfo, RGB(150, 160, 178));
        RECT rV = { infoLeft + 56, y, rBox.right - 8, y + 22 };
        DrawTextCustom(dc, rV, val, fInfoB, c);
    };

    wchar_t bAzm[32], bAlt[32], bState[32];
    swprintf_s(bAzm, 32, L"%.1f°", azm);
    swprintf_s(bAlt, 32, L"%.1f°", alt);
    swprintf_s(bState, 32, L"%s", isPenDown ? L"着筆中" : L"空中");

    DrawInfoRow(0, L"方位角:", bAzm, RGB(230, 240, 255));
    COLORREF altColor = (alt >= 60.0) ? RGB(120, 230, 160) : ((alt >= 45.0) ? RGB(255, 210, 80) : RGB(255, 110, 100));
    DrawInfoRow(1, L"高度角:", bAlt, altColor);
    DrawInfoRow(2, L"状態:", bState, isPenDown ? RGB(255, 120, 120) : RGB(100, 190, 255));

    DeleteObject(fInfo);
    DeleteObject(fInfoB);
}

void AnalysisView::DrawWaveformGraph(HDC dc, const RECT& rBox, const AppState& state) {
    using namespace RenderUtils;
    Box(dc, rBox, RGB(20, 23, 30), RGB(45, 52, 66), 1, 8);

    // ヘッダー & 凡例
    RECT rHeader = { rBox.left + 12, rBox.top + 8, rBox.left + 180, rBox.top + 28 };
    HFONT fHeader = CreateCustomFont(13, FW_BOLD);
    DrawTextCustom(dc, rHeader, L"運筆波形（筆圧・速度推移）", fHeader, RGB(200, 210, 230));
    DeleteObject(fHeader);

    // 凡例
    HFONT fLegend = CreateCustomFont(11, FW_BOLD);
    RECT rLeg1 = { rBox.right - 180, rBox.top + 8, rBox.right - 95, rBox.top + 26 };
    DrawTextCustom(dc, rLeg1, L"■ 筆圧 (0-100%)", fLegend, RGB(80, 210, 255));

    RECT rLeg2 = { rBox.right - 90, rBox.top + 8, rBox.right - 10, rBox.top + 26 };
    DrawTextCustom(dc, rLeg2, L"■ 速度 (px/s)", fLegend, RGB(255, 190, 70));
    DeleteObject(fLegend);

    // グラフプロット領域
    RECT rPlot = { rBox.left + 38, rBox.top + 32, rBox.right - 14, rBox.bottom - 20 };
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
    HFONT fTick = CreateCustomFont(11, FW_BOLD);
    RECT rL100 = { rBox.left + 2, y100 - 6, rPlot.left - 4, y100 + 10 };
    DrawTextCustom(dc, rL100, L"1.0", fTick, RGB(130, 140, 160), DT_RIGHT | DT_SINGLELINE);
    RECT rL50 = { rBox.left + 2, y50 - 6, rPlot.left - 4, y50 + 10 };
    DrawTextCustom(dc, rL50, L"0.5", fTick, RGB(130, 140, 160), DT_RIGHT | DT_SINGLELINE);
    RECT rL0 = { rBox.left + 2, y0 - 10, rPlot.left - 4, y0 + 6 };
    DrawTextCustom(dc, rL0, L"0.0", fTick, RGB(130, 140, 160), DT_RIGHT | DT_SINGLELINE);
    DeleteObject(fTick);

    const auto& strokes = state.trajectory.GetStrokes();
    DWORD totalDur = state.replay.totalDurationMs;

    if (strokes.empty() || totalDur == 0) {
        HFONT fEmpty = CreateCustomFont(13, FW_NORMAL);
        Center(dc, rPlot, L"（筆記した軌跡の波形と再生位置が表示されます）", fEmpty, RGB(110, 120, 140));
        DeleteObject(fEmpty);
        return;
    }

    // 速度最大値スケール計算
    double maxSpd = 500.0;
    for (const auto& s : strokes) {
        if (s.maxSpeed > maxSpd) maxSpd = s.maxSpeed;
    }
    maxSpd *= 1.1;

    // 1. 速度波形描画 (オレンジ破線/細線)
    HPEN spdPen = CreatePen(PS_SOLID, 1, RGB(220, 160, 50));
    oldPen = (HPEN)SelectObject(dc, spdPen);

    for (size_t sIdx = 0; sIdx < strokes.size(); ++sIdx) {
        const auto& s = strokes[sIdx];
        if (s.points.empty()) continue;
        DWORD strokeStartTimeline = state.trajectory.GetStrokeTimelineStart(sIdx);

        bool firstPt = true;
        for (const auto& pt : s.points) {
            DWORD ptTimeline = strokeStartTimeline + pt.timeMs;
            double normT = static_cast<double>(ptTimeline) / static_cast<double>(totalDur);
            int gx = rPlot.left + static_cast<int>(plotW * Clamp(normT, 0.0, 1.0));
            double spdNorm = Clamp(pt.speedPxPerSec / maxSpd, 0.0, 1.0);
            int gy = rPlot.bottom - static_cast<int>(plotH * spdNorm);

            if (firstPt) {
                MoveToEx(dc, gx, gy, nullptr);
                firstPt = false;
            } else {
                LineTo(dc, gx, gy);
            }
        }
    }
    SelectObject(dc, oldPen);
    DeleteObject(spdPen);

    // 2. 筆圧波形描画 (太いシアン線)
    HPEN prsPen = CreatePen(PS_SOLID, 2, RGB(80, 215, 255));
    oldPen = (HPEN)SelectObject(dc, prsPen);

    for (size_t sIdx = 0; sIdx < strokes.size(); ++sIdx) {
        const auto& s = strokes[sIdx];
        if (s.points.empty()) continue;
        DWORD strokeStartTimeline = state.trajectory.GetStrokeTimelineStart(sIdx);

        bool firstPt = true;
        for (const auto& pt : s.points) {
            DWORD ptTimeline = strokeStartTimeline + pt.timeMs;
            double normT = static_cast<double>(ptTimeline) / static_cast<double>(totalDur);
            int gx = rPlot.left + static_cast<int>(plotW * Clamp(normT, 0.0, 1.0));
            double pNorm = Clamp(pt.pressure, 0.0, 1.0);
            int gy = rPlot.bottom - static_cast<int>(plotH * pNorm);

            if (firstPt) {
                MoveToEx(dc, gx, gy, nullptr);
                firstPt = false;
            } else {
                LineTo(dc, gx, gy);
            }
        }
    }
    SelectObject(dc, oldPen);
    DeleteObject(prsPen);

    // 3. リプレイ再生位置シークヘッド（縦線＋ヘッド三角形）
    double curProg = static_cast<double>(state.replay.currentTimeMs) / static_cast<double>(totalDur);
    curProg = Clamp(curProg, 0.0, 1.0);
    int headX = rPlot.left + static_cast<int>(plotW * curProg);

    HPEN headPen = CreatePen(PS_SOLID, 2, RGB(255, 230, 80));
    oldPen = (HPEN)SelectObject(dc, headPen);
    MoveToEx(dc, headX, rPlot.top, nullptr);
    LineTo(dc, headX, rPlot.bottom);
    SelectObject(dc, oldPen);
    DeleteObject(headPen);

    // シークヘッドの三角形
    POINT tri[3] = {
        { headX - 5, rPlot.top },
        { headX + 5, rPlot.top },
        { headX, rPlot.top + 7 }
    };
    HBRUSH triB = CreateSolidBrush(RGB(255, 230, 80));
    HBRUSH oldB = (HBRUSH)SelectObject(dc, triB);
    HPEN noPen = CreatePen(PS_NULL, 0, RGB(0, 0, 0));
    oldPen = (HPEN)SelectObject(dc, noPen);
    Polygon(dc, tri, 3);
    SelectObject(dc, oldPen);
    SelectObject(dc, oldB);
    DeleteObject(noPen);
    DeleteObject(triB);
}

