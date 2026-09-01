#include "stdafx.h"
#include "CalibrationView.h"
#include "RenderUtils.h"
#include <cwchar>
#include <cmath>
#include <algorithm>

using namespace RenderUtils;

void CalibrationView::Draw(HDC dc, int width, int height, const AppState& state) {
    if (!state.calibration.IsActive()) return;

    if (state.calibration.IsInDrawingStep()) {
        DrawActiveStep(dc, width, height, state);
    } else if (state.calibration.IsResult()) {
        DrawResultModal(dc, width, height, state);
    }
}

void CalibrationView::DrawActiveStep(HDC dc, int width, int height, const AppState& state) {
    const auto& calib = state.calibration;
    int stepIdx = calib.GetCurrentStepIndex();
    const RECT& rPaper = state.ui.rPaper;

    // 1. 画面上部 ガイダンスヘッダーバナー
    int bannerW = (std::min)(560, width - 40);
    int bannerH = 68;
    int bannerX = width / 2 - bannerW / 2;
    int bannerY = (std::max)(12, static_cast<int>(rPaper.top - bannerH - 12));
    if (bannerY < 12) bannerY = 12;

    RECT rBanner = { bannerX, bannerY, bannerX + bannerW, bannerY + bannerH };
    Box(dc, rBanner, RGB(24, 28, 38), RGB(60, 90, 140), 2, 8);

    // ステップインジケーター (1/3, 2/3, 3/3)
    int badgeW = 70;
    RECT rBadge = { rBanner.left + 14, rBanner.top + 14, rBanner.left + 14 + badgeW, rBanner.bottom - 14 };
    Box(dc, rBadge, RGB(40, 75, 130), RGB(80, 150, 240), 1, 6);
    HFONT fBadge = CreateCustomFont(15, FW_BOLD);
    wchar_t badgeBuf[16];
    swprintf_s(badgeBuf, 16, L"%d / 3", stepIdx);
    Center(dc, rBadge, badgeBuf, fBadge, RGB(230, 245, 255));
    DeleteObject(fBadge);

    // ガイダンス指示文
    const wchar_t* stepTitle = L"";
    const wchar_t* stepSub = L"普段通りの書き心地で線を引いてください";
    switch (calib.GetStep()) {
    case CalibrationStep::Step1_Horizontal:
        stepTitle = L"1本目: 普段の強さで「横線」を引いてください";
        stepSub = L"いつもの力加減で、左から右へすっと線を引きます";
        break;
    case CalibrationStep::Step2_Vertical:
        stepTitle = L"2本目: 普段の強さで「縦線」を引いてください";
        stepSub = L"上から下へ、しっかりとした力加減で線を引きます";
        break;
    case CalibrationStep::Step3_Free:
        stepTitle = L"3本目: のびのびと「曲線・斜め線」を引いてください";
        stepSub = L"止め・払いや強弱を意識して、自由に線を引きます";
        break;
    default:
        break;
    }

    HFONT fTitle = CreateCustomFont(14, FW_BOLD);
    RECT rT = { rBadge.right + 14, rBanner.top + 12, rBanner.right - 14, rBanner.top + 32 };
    DrawTextCustom(dc, rT, stepTitle, fTitle, RGB(240, 245, 255));
    DeleteObject(fTitle);

    HFONT fSub = CreateCustomFont(12, FW_NORMAL);
    RECT rS = { rBadge.right + 14, rBanner.top + 36, rBanner.right - 14, rBanner.bottom - 10 };
    DrawTextCustom(dc, rS, stepSub, fSub, RGB(160, 175, 200));
    DeleteObject(fSub);

    // 2. 半紙下部 リアルタイム筆圧ゲージ
    int gaugeW = (std::min)(480, width - 40);
    int gaugeH = 46;
    int gaugeX = width / 2 - gaugeW / 2;
    int gaugeY = (std::min)(height - 80, static_cast<int>(rPaper.bottom + 12));
    if (gaugeY + gaugeH > height - 30) gaugeY = height - gaugeH - 32;

    RECT rGaugeBox = { gaugeX, gaugeY, gaugeX + gaugeW, gaugeY + gaugeH };
    Box(dc, rGaugeBox, RGB(22, 25, 32), RGB(50, 60, 80), 1, 6);

    HFONT fGaugeLab = CreateCustomFont(12, FW_BOLD);
    RECT rGLab = { rGaugeBox.left + 12, rGaugeBox.top + 14, rGaugeBox.left + 100, rGaugeBox.bottom - 14 };
    DrawTextCustom(dc, rGLab, L"リアルタイム筆圧", fGaugeLab, RGB(180, 195, 215));
    DeleteObject(fGaugeLab);

    RECT rBarTrack = { rGLab.right + 8, rGaugeBox.top + 16, rGaugeBox.right - 70, rGaugeBox.bottom - 16 };
    Fill(dc, rBarTrack, RGB(14, 16, 20));

    double livePrs = calib.GetLivePressure();
    livePrs = Clamp(livePrs, 0.0, 1.0);
    int fillW = static_cast<int>(RW(rBarTrack) * livePrs);
    if (fillW > 0) {
        RECT rBarFill = rBarTrack;
        rBarFill.right = rBarTrack.left + fillW;
        COLORREF barColor = (livePrs < 0.3) ? RGB(60, 150, 230) : ((livePrs < 0.7) ? RGB(80, 200, 140) : RGB(235, 120, 60));
        Fill(dc, rBarFill, barColor);
    }

    // パーセント数値
    HFONT fVal = CreateCustomFont(13, FW_BOLD);
    RECT rVal = { rBarTrack.right + 8, rGaugeBox.top + 14, rGaugeBox.right - 10, rGaugeBox.bottom - 14 };
    wchar_t prsBuf[16];
    swprintf_s(prsBuf, 16, L"%.0f%%", livePrs * 100.0);
    DrawTextCustom(dc, rVal, prsBuf, fVal, RGB(220, 230, 245), DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
    DeleteObject(fVal);
}

void CalibrationView::DrawResultModal(HDC dc, int width, int height, const AppState& state) {
    const UIState& ui = state.ui;
    const auto& res = state.calibration.GetResult();

    // 1. 全画面の半透明暗転オーバーレイ (ストライプフィル)
    for (int y = 0; y < height; y += 2) {
        RECT line = { 0, y, width, y + 1 };
        Fill(dc, line, RGB(0, 0, 0));
    }

    // 2. モーダルダイアログカード
    Box(dc, ui.rCalibModalBox, RGB(26, 30, 40), RGB(60, 85, 130), 2, 10);

    // タイトルバー
    HFONT fTitle = CreateCustomFont(18, FW_BOLD);
    RECT rT = { ui.rCalibModalBox.left + 24, ui.rCalibModalBox.top + 18, ui.rCalibModalBox.right - 24, ui.rCalibModalBox.top + 48 };
    DrawTextCustom(dc, rT, L"🎯 筆圧キャリブレーション完了", fTitle, RGB(245, 248, 255));
    DeleteObject(fTitle);

    // 診断バッジ & プロファイル名
    RECT rBadge = { ui.rCalibModalBox.left + 24, ui.rCalibModalBox.top + 56, ui.rCalibModalBox.left + 220, ui.rCalibModalBox.top + 88 };
    Box(dc, rBadge, RGB(35, 60, 100), RGB(70, 130, 220), 1, 6);
    HFONT fBadge = CreateCustomFont(14, FW_BOLD);
    Center(dc, rBadge, res.pressureProfileName.c_str(), fBadge, RGB(220, 240, 255));
    DeleteObject(fBadge);

    // 測定結果メトリクス (平均筆圧・最大筆圧)
    wchar_t bufMetrics[128];
    swprintf_s(bufMetrics, 128, L"平均筆圧: %.0f%%  /  最大筆圧: %.0f%%", res.overallAvgPressure * 100.0, res.overallMaxPressure * 100.0);
    HFONT fMet = CreateCustomFont(13, FW_NORMAL);
    RECT rM = { rBadge.right + 16, ui.rCalibModalBox.top + 60, ui.rCalibModalBox.right - 24, ui.rCalibModalBox.top + 84 };
    DrawTextCustom(dc, rM, bufMetrics, fMet, RGB(180, 190, 210), DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    DeleteObject(fMet);

    // アドバイス説明文
    HFONT fAdv = CreateCustomFont(13, FW_NORMAL);
    RECT rAdv = { ui.rCalibModalBox.left + 24, ui.rCalibModalBox.top + 98, ui.rCalibModalBox.right - 24, ui.rCalibModalBox.top + 130 };
    DrawTextCustom(dc, rAdv, res.adviceText.c_str(), fAdv, RGB(200, 215, 235));
    DeleteObject(fAdv);

    // 推奨硬さと現在の硬さの比較ビジュアルカード
    RECT rCompCard = { ui.rCalibModalBox.left + 24, ui.rCalibModalBox.top + 136, ui.rCalibModalBox.right - 24, ui.rCalibModalBox.top + 260 };
    Box(dc, rCompCard, RGB(18, 22, 30), RGB(42, 50, 68), 1, 8);

    HFONT fLabel = CreateCustomFont(13, FW_BOLD);
    RECT rCurLab = { rCompCard.left + 16, rCompCard.top + 12, rCompCard.left + 140, rCompCard.top + 34 };
    DrawTextCustom(dc, rCurLab, L"現在の筆の硬さ", fLabel, RGB(150, 160, 175));

    wchar_t curBuf[32];
    swprintf_s(curBuf, 32, L"%.2f", state.brush.hardness);
    RECT rCurVal = { rCurLab.right, rCompCard.top + 12, rCompCard.left + 220, rCompCard.top + 34 };
    DrawTextCustom(dc, rCurVal, curBuf, fLabel, RGB(180, 190, 205));

    RECT rRecLab = { rCompCard.left + 16, rCompCard.top + 42, rCompCard.left + 140, rCompCard.top + 64 };
    DrawTextCustom(dc, rRecLab, L"★ 推奨される硬さ", fLabel, RGB(100, 210, 255));

    wchar_t recBuf[64];
    const wchar_t* hardStr = (res.recommendedHardness < 0.3) ? L"超極軟" : ((res.recommendedHardness < 0.7) ? L"柔らかめ" : ((res.recommendedHardness > 1.2) ? L"硬め" : L"標準"));
    swprintf_s(recBuf, 64, L"%.2f (%s)", res.recommendedHardness, hardStr);
    RECT rRecVal = { rRecLab.right, rCompCard.top + 42, rCompCard.right - 16, rCompCard.top + 64 };
    DrawTextCustom(dc, rRecVal, recBuf, fLabel, RGB(80, 225, 180));
    DeleteObject(fLabel);

    // スライダーゲージ比較バー
    RECT rSliderTrack = { rCompCard.left + 16, rCompCard.top + 76, rCompCard.right - 16, rCompCard.top + 88 };
    Fill(dc, rSliderTrack, RGB(12, 14, 18));

    // 推奨値のインジケーター
    double normRec = (res.recommendedHardness - 0.1) / (2.0 - 0.1);
    normRec = Clamp(normRec, 0.0, 1.0);
    int recX = rSliderTrack.left + static_cast<int>(RW(rSliderTrack) * normRec);

    RECT rRecFill = rSliderTrack;
    rRecFill.right = recX;
    Fill(dc, rRecFill, RGB(45, 140, 180));

    RECT rMarker = { recX - 6, rSliderTrack.top - 4, recX + 6, rSliderTrack.bottom + 4 };
    Box(dc, rMarker, RGB(100, 230, 220), RGB(220, 255, 250), 1, 3);

    HFONT fScale = CreateCustomFont(11, FW_NORMAL);
    RECT rMinLab = { rSliderTrack.left, rSliderTrack.bottom + 4, rSliderTrack.left + 80, rSliderTrack.bottom + 20 };
    DrawTextCustom(dc, rMinLab, L"0.1 (極軟)", fScale, RGB(120, 130, 145));

    RECT rMaxLab = { rSliderTrack.right - 80, rSliderTrack.bottom + 4, rSliderTrack.right, rSliderTrack.bottom + 20 };
    DrawTextCustom(dc, rMaxLab, L"2.0 (極硬)", fScale, RGB(120, 130, 145), DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
    DeleteObject(fScale);

    // 3. アクションボタン群
    // [ ✓ 推奨設定を適用する ]
    bool hovApply = (ui.hoverCalib == 1);
    Box(dc, ui.rCalibApplyBtn, hovApply ? RGB(38, 145, 120) : RGB(28, 115, 95), hovApply ? RGB(70, 220, 175) : RGB(45, 170, 135), 1, 6);
    HFONT fBtnPrimary = CreateCustomFont(14, FW_BOLD);
    Center(dc, ui.rCalibApplyBtn, L"✓ 推奨設定を適用", fBtnPrimary, RGB(255, 255, 255));
    DeleteObject(fBtnPrimary);

    // [ 🔄 もう一度計測 ]
    bool hovRetry = (ui.hoverCalib == 2);
    Box(dc, ui.rCalibRetryBtn, hovRetry ? RGB(52, 60, 78) : RGB(38, 44, 58), hovRetry ? RGB(90, 110, 145) : RGB(62, 72, 95), 1, 6);
    HFONT fBtnSec = CreateCustomFont(13, FW_NORMAL);
    Center(dc, ui.rCalibRetryBtn, L"🔄 もう一度計測", fBtnSec, hovRetry ? RGB(255, 255, 255) : RGB(210, 220, 235));

    // [ ✕ 閉じる ]
    bool hovClose = (ui.hoverCalib == 3);
    Box(dc, ui.rCalibCloseBtn, hovClose ? RGB(45, 48, 58) : RGB(32, 35, 44), hovClose ? RGB(75, 80, 95) : RGB(52, 56, 68), 1, 6);
    Center(dc, ui.rCalibCloseBtn, L"✕ 閉じる", fBtnSec, hovClose ? RGB(240, 240, 245) : RGB(170, 175, 185));
    DeleteObject(fBtnSec);
}
