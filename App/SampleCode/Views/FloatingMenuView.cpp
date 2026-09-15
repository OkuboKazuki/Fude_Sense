#include "stdafx.h"
#include "FloatingMenuView.h"
#include "AnalysisView.h"
#include "CanvasView.h"
#include "RenderUtils.h"
#include <cwchar>

void FloatingMenuView::DrawSub(HDC dc, const AppState& state) {
    if (!state.ui.isSubPanelOpen) return;

    using namespace RenderUtils;
    const UIState& ui = state.ui;

    if (ui.leftTab == LeftTab::Brush) {
        DrawSubCard(dc, ui.rSubSmall, L"小筆", L"かな・名入れ・細線", 2, state.brush.type == Brush::Small, ui.hoverSub == 1);
        DrawSubCard(dc, ui.rSubMedium, L"中筆", L"標準的な楷書・行書", 5, state.brush.type == Brush::Medium, ui.hoverSub == 2);
        DrawSubCard(dc, ui.rSubLarge, L"大筆", L"作品・力強い大字", 9, state.brush.type == Brush::Large, ui.hoverSub == 3);

        // 筆の硬さカード
        RECT rCard = { ui.rSub.left + 18, ui.rSubLarge.bottom + 14, ui.rSub.right - 18, ui.rSubLarge.bottom + 134 };
        Box(dc, rCard, RGB(32, 35, 44), RGB(52, 58, 72), 1, 10);

        HFONT fTitle = CreateCustomFont(20, FW_BOLD);
        RECT rTitle = { rCard.left + 18, rCard.top + 14, rCard.left + 240, rCard.top + 42 };
        DrawTextCustom(dc, rTitle, L"筆の硬さ（感度補正）", fTitle, RGB(225, 230, 240));

        wchar_t valBuf[64];
        double hardness = state.brush.hardness;
        const wchar_t* hardState = (hardness < 0.3) ? L"超極軟" : ((hardness < 0.7) ? L"柔らかめ" : ((hardness > 1.2) ? L"硬め" : L"標準"));
        swprintf_s(valBuf, 64, L"%.2f (%s)", hardness, hardState);
        RECT rVal = { rCard.right - 200, rCard.top + 14, rCard.right - 18, rCard.top + 42 };
        DrawTextCustom(dc, rVal, valBuf, fTitle, RGB(110, 180, 255), DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
        DeleteObject(fTitle);

        // トラック描画
        Fill(dc, ui.rHardnessTrack, RGB(18, 20, 26));
        double normHardness = (hardness - 0.1) / (2.0 - 0.1);
        normHardness = Clamp(normHardness, 0.0, 1.0);
        int thumbX = ui.rHardnessTrack.left + (int)(RW(ui.rHardnessTrack) * normHardness);

        RECT rLevel = ui.rHardnessTrack;
        rLevel.right = thumbX;
        Fill(dc, rLevel, RGB(70, 130, 210));

        RECT thumb = { thumbX - 7, ui.rHardnessTrack.top - 6, thumbX + 7, ui.rHardnessTrack.bottom + 6 };
        Box(dc, thumb, state.brush.isDraggingHardness ? RGB(190, 220, 255) : RGB(145, 190, 245), RGB(230, 242, 255), 1, 4);

        HFONT fSub = CreateCustomFont(15, FW_NORMAL);
        RECT rMinLab = { ui.rHardnessTrack.left, ui.rHardnessTrack.bottom + 6, ui.rHardnessTrack.left + 100, ui.rHardnessTrack.bottom + 26 };
        DrawTextCustom(dc, rMinLab, L"0.1 (極軟)", fSub, RGB(155, 160, 172));

        RECT rMaxLab = { ui.rHardnessTrack.right - 100, ui.rHardnessTrack.bottom + 6, ui.rHardnessTrack.right, ui.rHardnessTrack.bottom + 26 };
        DrawTextCustom(dc, rMaxLab, L"2.0 (極硬)", fSub, RGB(155, 160, 172), DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
        DeleteObject(fSub);

        // 🎯 筆圧キャリブレーション起動ボタン
        bool hovCalib = (ui.hoverSub == 4);
        Box(dc, ui.rSubCalibBtn, hovCalib ? RGB(48, 88, 145) : RGB(34, 52, 84), hovCalib ? RGB(95, 165, 255) : RGB(60, 105, 170), 1, 8);
        HFONT fCalibBtn = CreateCustomFont(18, FW_BOLD);
        Center(dc, ui.rSubCalibBtn, L"🎯 筆圧キャリブレーション (自動調整)", fCalibBtn, hovCalib ? RGB(255, 255, 255) : RGB(225, 240, 255));
        DeleteObject(fCalibBtn);
    }
    else if (ui.leftTab == LeftTab::Paper) {
        const wchar_t* pTitles[4] = { L"半紙", L"条幅", L"色紙", L"短冊" };
        const wchar_t* pSubs[4]   = { L"242×333", L"350×680", L"242×272", L"60×180" };
        for (int i = 0; i < 4; ++i) {
            bool act = ((int)state.paper.type == i);
            bool hov = (ui.hoverSub == 50 + i);
            DrawTileCard(dc, ui.rPaperTile[i], pTitles[i], pSubs[i], act, hov);
        }

        RECT rGridHeader = { ui.rSub.left + 18, ui.rPaperTile[2].bottom + 8, ui.rSub.right - 18, ui.rPaperTile[2].bottom + 32 };
        HFONT fgh = CreateCustomFont(20, FW_BOLD);
        DrawTextCustom(dc, rGridHeader, L"下敷き・升目ガイド", fgh, RGB(210, 216, 228));
        DeleteObject(fgh);

        const wchar_t* titles[9] = {
            L"なし", L"1字 (十字)", L"2文字 (2段)",
            L"4文字 (田)", L"6文字 (2x3)", L"8文字 (2x4)",
            L"3行 罫線", L"4行 罫線", L"米字格 (対角)"
        };
        const wchar_t* subs[9] = {
            L"無地半紙", L"中心ガイド", L"二文字熟語",
            L"四字熟語", L"六文字配列", L"八文字配列",
            L"行書・かな", L"条幅・古典", L"臨書・骨格"
        };

        for (int i = 0; i < 9; ++i) {
            bool act = ((int)state.paper.gridPattern == i);
            bool hov = (ui.hoverSub == 10 + i);
            DrawTileCard(dc, ui.rGridTile[i], titles[i], subs[i], act, hov);
        }

        RECT rColTitle = { ui.rSub.left + 18, ui.rColorBtn[0].top - 24, ui.rSub.right - 18, ui.rColorBtn[0].top - 2 };
        HFONT fct = CreateCustomFont(20, FW_BOLD);
        DrawTextCustom(dc, rColTitle, L"下敷き・罫線の配色", fct, RGB(210, 216, 228));
        DeleteObject(fct);

        DrawColorThemeButton(dc, ui.rColorBtn[0], L"朱赤", RGB(235, 85, 85), state.paper.gridColor == GridColorTheme::RedLine, ui.hoverSub == 30);
        DrawColorThemeButton(dc, ui.rColorBtn[1], L"白線", RGB(240, 244, 252), state.paper.gridColor == GridColorTheme::WhiteLine, ui.hoverSub == 31);
        DrawColorThemeButton(dc, ui.rColorBtn[2], L"薄墨", RGB(150, 155, 168), state.paper.gridColor == GridColorTheme::InkGray, ui.hoverSub == 32);
    }
    else if (ui.leftTab == LeftTab::Analysis) {
        AnalysisView::Draw(dc, state);
    }
    else if (ui.leftTab == LeftTab::Save) {
        bool hovPng = (ui.hoverSub == 60);
        Box(dc, ui.rSaveBtnPng, hovPng ? RGB(48, 82, 132) : RGB(36, 52, 80), hovPng ? RGB(90, 155, 245) : RGB(60, 105, 175), 1, 8);
        HFONT fBtn1 = CreateCustomFont(20, FW_BOLD);
        Center(dc, ui.rSaveBtnPng, L"🖼️ 作品画像を保存 (PNG)", fBtn1, RGB(255, 255, 255));
        DeleteObject(fBtn1);

        bool hovClip = (ui.hoverSub == 61);
        Box(dc, ui.rSaveBtnClip, hovClip ? RGB(46, 52, 64) : RGB(34, 38, 46), hovClip ? RGB(78, 86, 102) : RGB(54, 60, 74), 1, 8);
        HFONT fBtn2 = CreateCustomFont(19, FW_NORMAL);
        Center(dc, ui.rSaveBtnClip, L"📋 クリップボードにコピー", fBtn2, RGB(225, 230, 242));
        DeleteObject(fBtn2);

        // JSONアーカイブ保存ボタン
        bool hovJson = (ui.hoverSub == 62);
        Box(dc, ui.rSaveBtnJson, hovJson ? RGB(36, 85, 68) : RGB(28, 58, 48), hovJson ? RGB(70, 185, 142) : RGB(46, 122, 95), 1, 8);
        HFONT fBtn3 = CreateCustomFont(20, FW_BOLD);
        Center(dc, ui.rSaveBtnJson, L"💾 運筆アーカイブ保存 (JSON)", fBtn3, RGB(235, 255, 248));
        DeleteObject(fBtn3);

        // CSV時系列保存ボタン
        bool hovCsv = (ui.hoverSub == 63);
        Box(dc, ui.rSaveBtnCsv, hovCsv ? RGB(68, 58, 34) : RGB(48, 42, 26), hovCsv ? RGB(200, 155, 55) : RGB(135, 105, 40), 1, 8);
        HFONT fBtn4 = CreateCustomFont(19, FW_NORMAL);
        Center(dc, ui.rSaveBtnCsv, L"📊 運筆データ出力 (CSV / 研究用)", fBtn4, RGB(255, 246, 225));
        DeleteObject(fBtn4);

        // 記録状況インジケータ
        RECT rInfoBox = { ui.rSub.left + 20, ui.rSaveBtnCsv.bottom + 16, ui.rSub.right - 20, ui.rSaveBtnCsv.bottom + 74 };
        Box(dc, rInfoBox, RGB(26, 29, 38), RGB(44, 50, 64), 1, 8);
        HFONT fRec = CreateCustomFont(15, FW_NORMAL);
        wchar_t recBuf[128];
        swprintf_s(recBuf, 128, L"記録中ストローク: %d 画 / 累積データ点: %d 点\n（筆圧・高度角・方位角・速度・正規化座標）",
            static_cast<int>(state.trajectory.GetTotalStrokeCount()), static_cast<int>(state.trajectory.GetTotalPointCount()));
        RECT rRecText = { rInfoBox.left + 10, rInfoBox.top + 6, rInfoBox.right - 10, rInfoBox.bottom - 6 };
        DrawTextCustom(dc, rRecText, recBuf, fRec, RGB(175, 190, 210), DT_CENTER | DT_WORDBREAK);
        DeleteObject(fRec);

        if (!ui.saveFeedback.empty() && (GetTickCount() - ui.saveFeedbackTime < 4000)) {
            RECT rMsg = { ui.rSub.left + 20, rInfoBox.bottom + 12, ui.rSub.right - 20, rInfoBox.bottom + 58 };
            Box(dc, rMsg, RGB(30, 62, 45), RGB(55, 145, 90), 1, 8);
            HFONT fMsg = CreateCustomFont(18, FW_BOLD);
            Center(dc, rMsg, ui.saveFeedback.c_str(), fMsg, RGB(190, 255, 210));
            DeleteObject(fMsg);
        }
    }
    else if (ui.leftTab == LeftTab::Otehon) {
        bool hovTog = (ui.hoverSub == 70);
        Box(dc, ui.rOtehonToggleBtn, state.otehon.isVisible ? RGB(40, 76, 120) : (hovTog ? RGB(46, 52, 64) : RGB(32, 36, 46)),
            state.otehon.isVisible ? RGB(80, 150, 245) : (hovTog ? RGB(72, 80, 96) : RGB(50, 56, 70)), 1, 8);
        HFONT fTog = CreateCustomFont(22, FW_BOLD);
        Center(dc, ui.rOtehonToggleBtn, state.otehon.isVisible ? L"✓ お手本表示: ON" : L"お手本表示: OFF", fTog, state.otehon.isVisible ? RGB(255, 255, 255) : RGB(200, 205, 218));
        DeleteObject(fTog);

        // 配置: 升目のミニマップ。押したマスへ選択中の文字を置く
        HFONT fCellNo = nullptr;
        if (ui.gridCellCount > 0) {
            // マスは全て同じ大きさなので、文字サイズは先頭のマスから決める
            const RECT& r0 = ui.rOtehonCellBtn[0];
            int size = (std::min)(RW(r0), RH(r0)) * 7 / 10;
            fCellNo = CreateCustomFont(Clamp(size, 10, 26), FW_BOLD);
        }
        for (int i = 0; i < ui.gridCellCount; ++i) {
            const RECT& rc = ui.rOtehonCellBtn[i];
            if (RW(rc) <= 0 || RH(rc) <= 0) continue;
            const std::wstring& placed = state.otehon.GetCellText(i);
            // 配置済みのマスを強調する
            bool sel = !placed.empty();
            bool hov = (ui.hoverSub == 90 + i);
            COLORREF fill = sel ? RGB(36, 68, 105) : (hov ? RGB(42, 46, 56) : RGB(26, 29, 35));
            COLORREF edge = sel ? RGB(70, 135, 220) : (hov ? RGB(66, 72, 86) : RGB(56, 60, 72));
            Box(dc, rc, fill, edge, 1, 3);

            if (fCellNo && RW(rc) >= 16 && RH(rc) >= 14) {
                if (!placed.empty()) {
                    // 置いた字をそのまま出すと、半紙の仕上がりが一目で分かる
                    Center(dc, rc, placed.c_str(), fCellNo, RGB(235, 240, 250));
                } else {
                    wchar_t noBuf[8];
                    wsprintfW(noBuf, L"%d", i + 1);
                    Center(dc, rc, noBuf, fCellNo, RGB(96, 104, 118));
                }
            }
        }
        if (fCellNo) DeleteObject(fCellNo);

        // 書体の切り替え。この PC に入っていない書体は、選んでも GDI が別の書体へ
        // 置き換えてしまい見た目が変わらないため、文字色を沈めて区別する。
        static const wchar_t* const kFontLabels[OTEHON_FONT_COUNT] = { L"楷書", L"教科書体", L"行書" };
        HFONT fFontBtn = CreateCustomFont(17, FW_BOLD);
        for (int i = 0; i < OTEHON_FONT_COUNT; ++i) {
            const RECT& rf = ui.rOtehonFontBtn[i];
            OtehonFontStyle style = static_cast<OtehonFontStyle>(i);
            bool selFont = (state.otehon.fontStyle == style);
            bool hovFont = (ui.hoverSub == 74 + i);
            Box(dc, rf, selFont ? RGB(36, 68, 105) : (hovFont ? RGB(42, 46, 56) : RGB(30, 33, 40)),
                selFont ? RGB(70, 135, 220) : (hovFont ? RGB(66, 72, 86) : RGB(46, 50, 62)), 1, 6);
            COLORREF fg = selFont ? RGB(255, 255, 255)
                                  : (CanvasView::HasOtehonFont(dc, style) ? RGB(200, 205, 218)
                                                                          : RGB(108, 114, 128));
            Center(dc, rf, kFontLabels[i], fFontBtn, fg);
        }
        DeleteObject(fFontBtn);

        // 書体ボタンの下へ操作の説明を出す
        HFONT fHint = CreateCustomFont(15);
        RECT rHint = { ui.rOtehonFontBtn[0].left, ui.rOtehonFontBtn[0].bottom + 8,
                       ui.rSub.right - 20, ui.rOtehonCellMapBox.bottom };
        DrawTextCustom(dc, rHint, L"下の文字を選び、左の升目を\n押すと配置します（複数可）。\n同じ字をもう一度押すと消去", fHint,
            RGB(140, 148, 162), DT_LEFT | DT_TOP | DT_WORDBREAK);
        DeleteObject(fHint);

        RECT rOteHeader = { ui.rSub.left + 20, ui.rOtehonInputBox.top - 26, ui.rSub.right - 20, ui.rOtehonInputBox.top - 6 };
        HFONT foh = CreateCustomFont(20, FW_BOLD);
        DrawTextCustom(dc, rOteHeader, L"書きたい文字を入力（日本語入力で変換・確定）", foh, RGB(210, 216, 228));
        DeleteObject(foh);

        // 文字入力欄。入力中は枠を強調し、末尾にキャレットを描く
        bool typing = state.otehon.isTyping;
        bool hovInput = (ui.hoverSub == 73);
        Box(dc, ui.rOtehonInputBox, RGB(22, 24, 29),
            typing ? RGB(90, 155, 235) : (hovInput ? RGB(66, 72, 86) : RGB(50, 55, 68)), typing ? 2 : 1, 6);

        HFONT fInput = CreateCustomFont(26);
        RECT rInputText = { ui.rOtehonInputBox.left + 14, ui.rOtehonInputBox.top,
                            ui.rOtehonInputBox.right - 14, ui.rOtehonInputBox.bottom };
        if (!state.otehon.inputText.empty()) {
            DrawTextCustom(dc, rInputText, state.otehon.inputText.c_str(), fInput, RGB(235, 240, 250));
        } else if (!typing) {
            DrawTextCustom(dc, rInputText, L"ここを押して文字を入力（例: 春夏秋冬）", fInput, RGB(96, 104, 118));
        }
        if (typing) {
            // キャレット（入力位置の目印）
            SIZE ext{ 0, 0 };
            HFONT oldF = (HFONT)SelectObject(dc, fInput);
            if (!state.otehon.inputText.empty()) {
                GetTextExtentPoint32W(dc, state.otehon.inputText.c_str(),
                    (int)state.otehon.inputText.size(), &ext);
            }
            SelectObject(dc, oldF);
            int caretX = rInputText.left + ext.cx + 2;
            RECT rCaret = { caretX, ui.rOtehonInputBox.top + 8, caretX + 2, ui.rOtehonInputBox.bottom - 8 };
            Fill(dc, rCaret, RGB(150, 200, 255));
        }
        DeleteObject(fInput);

        // 入力した文字が候補タイルになる（未入力なら既定の8字）
        HFONT fChar = CreateCustomFont(34, FW_BOLD, L"Yu Mincho");
        int paletteCount = state.otehon.GetPaletteCount();
        for (int i = 0; i < paletteCount && i < 8; ++i) {
            bool act = (state.otehon.selectedIndex == i);
            bool hov = (ui.hoverSub == 80 + i);
            COLORREF bg = act ? RGB(42, 68, 108) : (hov ? RGB(48, 52, 64) : RGB(32, 35, 42));
            COLORREF border = act ? RGB(85, 145, 235) : (hov ? RGB(78, 86, 104) : RGB(48, 52, 64));
            COLORREF tc = act ? RGB(255, 255, 255) : (hov ? RGB(248, 250, 255) : RGB(210, 215, 225));
            Box(dc, ui.rOtehonTile[i], bg, border, 1, 8);
            Center(dc, ui.rOtehonTile[i], state.otehon.GetCharacter(i).c_str(), fChar, tc);
        }
        DeleteObject(fChar);

        RECT rCard = { ui.rSub.left + 18, ui.rOtehonOpacityTrack.top - 28, ui.rSub.right - 18, ui.rOtehonOpacityTrack.bottom + 32 };
        Box(dc, rCard, RGB(32, 35, 44), RGB(52, 58, 72), 1, 8);

        HFONT fTitle = CreateCustomFont(20, FW_BOLD);
        RECT rTitle = { rCard.left + 18, rCard.top + 8, rCard.left + 240, rCard.top + 32 };
        DrawTextCustom(dc, rTitle, L"お手本の透過度（濃淡）", fTitle, RGB(225, 230, 240));

        wchar_t valBuf[32];
        swprintf_s(valBuf, 32, L"%d%%", (int)(state.otehon.opacity * 100.0));
        RECT rVal = { rCard.right - 100, rCard.top + 8, rCard.right - 18, rCard.top + 32 };
        DrawTextCustom(dc, rVal, valBuf, fTitle, RGB(110, 180, 255), DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
        DeleteObject(fTitle);

        Fill(dc, ui.rOtehonOpacityTrack, RGB(18, 20, 26));
        int thumbX = ui.rOtehonOpacityTrack.left + (int)(RW(ui.rOtehonOpacityTrack) * state.otehon.opacity);
        RECT rLevel = ui.rOtehonOpacityTrack;
        rLevel.right = thumbX;
        Fill(dc, rLevel, RGB(70, 130, 210));

        RECT thumb = { thumbX - 7, ui.rOtehonOpacityTrack.top - 6, thumbX + 7, ui.rOtehonOpacityTrack.bottom + 6 };
        Box(dc, thumb, state.otehon.isDraggingOpacity ? RGB(190, 220, 255) : RGB(145, 190, 245), RGB(230, 242, 255), 1, 4);
    }
}

void FloatingMenuView::Draw(HDC dc, const AppState& state) {
    using namespace RenderUtils;
    const UIState& ui = state.ui;

    if (!ui.isSubPanelOpen) {
        bool hovHome = (ui.hoverTb == TbButton::Home);
        Box(dc, ui.rTbHomeBtn, hovHome ? RGB(52, 62, 84) : RGB(34, 38, 50), hovHome ? RGB(100, 165, 255) : RGB(62, 72, 92), 2, 8);
        HFONT fHome = CreateCustomFont(22, FW_BOLD);
        Center(dc, ui.rTbHomeBtn, L"🏠", fHome, hovHome ? RGB(255, 255, 255) : RGB(210, 220, 240));
        DeleteObject(fHome);

        bool hov = (ui.hoverTb == TbButton::NavToggle);
        Box(dc, ui.rTbNavToggle, hov ? RGB(52, 62, 84) : RGB(34, 38, 50), hov ? RGB(100, 165, 255) : RGB(62, 72, 92), 2, 8);
        HFONT f = CreateCustomFont(22, FW_BOLD);
        Center(dc, ui.rTbNavToggle, L">>>", f, hov ? RGB(255, 255, 255) : RGB(210, 220, 240));
        DeleteObject(f);
        return;
    }

    Box(dc, ui.rSub, RGB(24, 26, 34), RGB(62, 70, 88), 1, 10);

    HPEN sp = CreatePen(PS_SOLID, 1, RGB(42, 48, 62));
    HPEN osp = (HPEN)SelectObject(dc, sp);
    MoveToEx(dc, ui.rSub.left + 16, ui.rSub.top + 74, nullptr);
    LineTo(dc, ui.rSub.right - 16, ui.rSub.top + 74);
    SelectObject(dc, osp);
    DeleteObject(sp);

    bool hovHome = (ui.hoverTb == TbButton::Home);
    Box(dc, ui.rTbHomeBtn, hovHome ? RGB(52, 62, 84) : RGB(34, 38, 50), hovHome ? RGB(100, 165, 255) : RGB(62, 72, 92), 1, 8);
    HFONT fHome = CreateCustomFont(20, FW_BOLD);
    Center(dc, ui.rTbHomeBtn, L"🏠", fHome, hovHome ? RGB(255, 255, 255) : RGB(210, 220, 240));
    DeleteObject(fHome);

    bool hovTog = (ui.hoverTb == TbButton::NavToggle);
    Box(dc, ui.rTbNavToggle, hovTog ? RGB(52, 62, 84) : RGB(34, 38, 50), hovTog ? RGB(100, 165, 255) : RGB(62, 72, 92), 1, 8);
    HFONT fTog = CreateCustomFont(22, FW_BOLD);
    Center(dc, ui.rTbNavToggle, L"<<<", fTog, hovTog ? RGB(255, 255, 255) : RGB(210, 220, 240));
    DeleteObject(fTog);

    auto DrawTab = [&](RECT r, const wchar_t* label, bool active, bool hover) {
        COLORREF bg = active ? RGB(45, 75, 124) : (hover ? RGB(42, 48, 62) : RGB(30, 34, 44));
        COLORREF border = active ? RGB(85, 150, 245) : (hover ? RGB(72, 82, 102) : RGB(48, 54, 68));
        COLORREF text = active ? RGB(255, 255, 255) : (hover ? RGB(245, 248, 255) : RGB(185, 190, 202));
        Box(dc, r, bg, border, 1, 8);
        HFONT f = CreateCustomFont(20, active ? FW_BOLD : FW_NORMAL);
        Center(dc, r, label, f, text);
        DeleteObject(f);
    };

    DrawTab(ui.rTbBrush,    L"筆",   ui.leftTab == LeftTab::Brush,    ui.hoverTb == TbButton::Brush);
    DrawTab(ui.rTbPaper,    L"紙",   ui.leftTab == LeftTab::Paper,    ui.hoverTb == TbButton::Paper);
    DrawTab(ui.rTbAnalysis, L"解析", ui.leftTab == LeftTab::Analysis, ui.hoverTb == TbButton::Analysis);
    DrawTab(ui.rTbSave,     L"保存", ui.leftTab == LeftTab::Save,     ui.hoverTb == TbButton::Save);
    DrawTab(ui.rTbOtehon,   L"手本", ui.leftTab == LeftTab::Otehon,   ui.hoverTb == TbButton::Otehon);

    DrawSub(dc, state);
}
