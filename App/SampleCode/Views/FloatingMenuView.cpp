#include "stdafx.h"
#include "FloatingMenuView.h"
#include "AnalysisView.h"
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
        RECT rCard = { ui.rSub.left + 14, ui.rSubLarge.bottom + 12, ui.rSub.right - 14, ui.rSubLarge.bottom + 112 };
        Box(dc, rCard, RGB(32, 35, 42), RGB(50, 55, 68), 1, 6);

        HFONT fTitle = CreateCustomFont(13, FW_BOLD);
        RECT rTitle = { rCard.left + 14, rCard.top + 12, rCard.left + 160, rCard.top + 32 };
        DrawTextCustom(dc, rTitle, L"筆の硬さ（感度補正）", fTitle, RGB(220, 225, 235));

        wchar_t valBuf[64];
        double hardness = state.brush.hardness;
        const wchar_t* hardState = (hardness < 0.3) ? L"超極軟" : ((hardness < 0.7) ? L"柔らかめ" : ((hardness > 1.2) ? L"硬め" : L"標準"));
        swprintf_s(valBuf, 64, L"%.2f (%s)", hardness, hardState);
        RECT rVal = { rCard.right - 130, rCard.top + 12, rCard.right - 14, rCard.top + 32 };
        DrawTextCustom(dc, rVal, valBuf, fTitle, RGB(100, 160, 230), DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
        DeleteObject(fTitle);

        // トラック描画
        Fill(dc, ui.rHardnessTrack, RGB(18, 20, 24));
        double normHardness = (hardness - 0.1) / (2.0 - 0.1);
        normHardness = Clamp(normHardness, 0.0, 1.0);
        int thumbX = ui.rHardnessTrack.left + (int)(RW(ui.rHardnessTrack) * normHardness);

        RECT rLevel = ui.rHardnessTrack;
        rLevel.right = thumbX;
        Fill(dc, rLevel, RGB(65, 120, 190));

        RECT thumb = { thumbX - 5, ui.rHardnessTrack.top - 4, thumbX + 5, ui.rHardnessTrack.bottom + 4 };
        Box(dc, thumb, state.brush.isDraggingHardness ? RGB(180, 210, 255) : RGB(140, 180, 230), RGB(220, 235, 255), 1, 3);

        HFONT fSub = CreateCustomFont(11, FW_NORMAL);
        RECT rMinLab = { ui.rHardnessTrack.left, ui.rHardnessTrack.bottom + 6, ui.rHardnessTrack.left + 80, ui.rHardnessTrack.bottom + 22 };
        DrawTextCustom(dc, rMinLab, L"0.1 (極軟)", fSub, RGB(140, 145, 155));

        RECT rMaxLab = { ui.rHardnessTrack.right - 80, ui.rHardnessTrack.bottom + 6, ui.rHardnessTrack.right, ui.rHardnessTrack.bottom + 22 };
        DrawTextCustom(dc, rMaxLab, L"2.0 (極硬)", fSub, RGB(140, 145, 155), DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
        DeleteObject(fSub);

        // 🎯 筆圧キャリブレーション起動ボタン
        bool hovCalib = (ui.hoverSub == 4);
        Box(dc, ui.rSubCalibBtn, hovCalib ? RGB(45, 80, 130) : RGB(32, 48, 75), hovCalib ? RGB(85, 150, 235) : RGB(55, 95, 155), 1, 6);
        HFONT fCalibBtn = CreateCustomFont(13, FW_BOLD);
        Center(dc, ui.rSubCalibBtn, L"🎯 筆圧キャリブレーション (自動調整)", fCalibBtn, hovCalib ? RGB(255, 255, 255) : RGB(220, 235, 255));
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

        RECT rGridHeader = { ui.rSub.left + 14, ui.rPaperTile[2].bottom + 8, ui.rSub.right - 14, ui.rPaperTile[2].bottom + 26 };
        HFONT fgh = CreateCustomFont(13, FW_BOLD);
        DrawTextCustom(dc, rGridHeader, L"下敷き・升目ガイド", fgh, RGB(180, 185, 195));
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

        RECT rColTitle = { ui.rSub.left + 14, ui.rColorBtn[0].top - 18, ui.rSub.right - 14, ui.rColorBtn[0].top };
        HFONT fct = CreateCustomFont(13, FW_BOLD);
        DrawTextCustom(dc, rColTitle, L"下敷き・罫線の配色", fct, RGB(170, 175, 185));
        DeleteObject(fct);

        DrawColorThemeButton(dc, ui.rColorBtn[0], L"朱赤", RGB(225, 80, 80), state.paper.gridColor == GridColorTheme::RedLine, ui.hoverSub == 30);
        DrawColorThemeButton(dc, ui.rColorBtn[1], L"白線", RGB(235, 238, 245), state.paper.gridColor == GridColorTheme::WhiteLine, ui.hoverSub == 31);
        DrawColorThemeButton(dc, ui.rColorBtn[2], L"薄墨", RGB(140, 145, 155), state.paper.gridColor == GridColorTheme::InkGray, ui.hoverSub == 32);
    }
    else if (ui.leftTab == LeftTab::Analysis) {
        AnalysisView::Draw(dc, state);
    }
    else if (ui.leftTab == LeftTab::Save) {
        bool hovPng = (ui.hoverSub == 60);
        Box(dc, ui.rSaveBtnPng, hovPng ? RGB(45, 75, 120) : RGB(34, 48, 72), hovPng ? RGB(80, 140, 220) : RGB(55, 95, 160), 1, 8);
        HFONT fBtn1 = CreateCustomFont(15, FW_BOLD);
        Center(dc, ui.rSaveBtnPng, L"🖼️ 作品画像を保存 (PNG)", fBtn1, RGB(255, 255, 255));
        DeleteObject(fBtn1);

        bool hovClip = (ui.hoverSub == 61);
        Box(dc, ui.rSaveBtnClip, hovClip ? RGB(42, 46, 56) : RGB(30, 33, 40), hovClip ? RGB(70, 75, 90) : RGB(48, 52, 64), 1, 8);
        HFONT fBtn2 = CreateCustomFont(14, FW_NORMAL);
        Center(dc, ui.rSaveBtnClip, L"📋 クリップボードにコピー", fBtn2, RGB(220, 225, 235));
        DeleteObject(fBtn2);

        // JSONアーカイブ保存ボタン
        bool hovJson = (ui.hoverSub == 62);
        Box(dc, ui.rSaveBtnJson, hovJson ? RGB(32, 75, 60) : RGB(25, 52, 44), hovJson ? RGB(60, 170, 130) : RGB(40, 110, 85), 1, 8);
        HFONT fBtn3 = CreateCustomFont(15, FW_BOLD);
        Center(dc, ui.rSaveBtnJson, L"💾 運筆アーカイブ保存 (JSON)", fBtn3, RGB(230, 255, 245));
        DeleteObject(fBtn3);

        // CSV時系列保存ボタン
        bool hovCsv = (ui.hoverSub == 63);
        Box(dc, ui.rSaveBtnCsv, hovCsv ? RGB(60, 52, 30) : RGB(42, 38, 24), hovCsv ? RGB(180, 140, 50) : RGB(120, 95, 35), 1, 8);
        HFONT fBtn4 = CreateCustomFont(14, FW_NORMAL);
        Center(dc, ui.rSaveBtnCsv, L"📊 運筆データ出力 (CSV / 研究用)", fBtn4, RGB(255, 245, 220));
        DeleteObject(fBtn4);

        // 記録状況インジケータ
        RECT rInfoBox = { ui.rSub.left + 16, ui.rSaveBtnCsv.bottom + 14, ui.rSub.right - 16, ui.rSaveBtnCsv.bottom + 62 };
        Box(dc, rInfoBox, RGB(24, 27, 34), RGB(40, 45, 56), 1, 6);
        HFONT fRec = CreateCustomFont(11, FW_NORMAL);
        wchar_t recBuf[128];
        swprintf_s(recBuf, 128, L"記録中ストローク: %d 画 / 累積データ点: %d 点\n（筆圧・高度角・方位角・速度・正規化座標）",
            static_cast<int>(state.trajectory.GetTotalStrokeCount()), static_cast<int>(state.trajectory.GetTotalPointCount()));
        RECT rRecText = { rInfoBox.left + 8, rInfoBox.top + 6, rInfoBox.right - 8, rInfoBox.bottom - 6 };
        DrawTextCustom(dc, rRecText, recBuf, fRec, RGB(160, 175, 195), DT_CENTER | DT_WORDBREAK);
        DeleteObject(fRec);

        if (!ui.saveFeedback.empty() && (GetTickCount() - ui.saveFeedbackTime < 4000)) {
            RECT rMsg = { ui.rSub.left + 16, rInfoBox.bottom + 12, ui.rSub.right - 16, rInfoBox.bottom + 52 };
            Box(dc, rMsg, RGB(28, 56, 40), RGB(50, 130, 80), 1, 6);
            HFONT fMsg = CreateCustomFont(13, FW_BOLD);
            Center(dc, rMsg, ui.saveFeedback.c_str(), fMsg, RGB(180, 255, 200));
            DeleteObject(fMsg);
        }
    }
    else if (ui.leftTab == LeftTab::Otehon) {
        bool hovTog = (ui.hoverSub == 70);
        Box(dc, ui.rOtehonToggleBtn, state.otehon.isVisible ? RGB(36, 68, 105) : (hovTog ? RGB(42, 46, 56) : RGB(30, 33, 40)),
            state.otehon.isVisible ? RGB(70, 135, 220) : (hovTog ? RGB(66, 72, 86) : RGB(46, 50, 62)), 1, 8);
        HFONT fTog = CreateCustomFont(15, FW_BOLD);
        Center(dc, ui.rOtehonToggleBtn, state.otehon.isVisible ? L"✓ お手本表示: ON" : L"お手本表示: OFF", fTog, state.otehon.isVisible ? RGB(255, 255, 255) : RGB(190, 195, 205));
        DeleteObject(fTog);

        // 配置: 升目のミニマップ。押したマスへ選択中の文字を置く
        bool following = state.otehon.IsFollowingPen();
        HFONT fCellNo = nullptr;
        if (ui.gridCellCount > 0) {
            // マスは全て同じ大きさなので、文字サイズは先頭のマスから決める
            const RECT& r0 = ui.rOtehonCellBtn[0];
            int size = (std::min)(RW(r0), RH(r0)) * 7 / 10;
            fCellNo = CreateCustomFont(Clamp(size, 9, 22), FW_BOLD);
        }
        for (int i = 0; i < ui.gridCellCount; ++i) {
            const RECT& rc = ui.rOtehonCellBtn[i];
            if (RW(rc) <= 0 || RH(rc) <= 0) continue;
            const std::wstring& placed = state.otehon.GetCellText(i);
            // 固定中は配置済みのマス、追従中は現在の追従先を示す
            bool sel = following ? (state.otehon.activeCell == i) : !placed.empty();
            bool hov = (ui.hoverSub == 90 + i);
            COLORREF fill = sel ? (following ? RGB(34, 44, 58) : RGB(36, 68, 105))
                                : (hov ? RGB(42, 46, 56) : RGB(26, 29, 35));
            COLORREF edge = sel ? (following ? RGB(70, 90, 120) : RGB(70, 135, 220))
                                : (hov ? RGB(66, 72, 86) : RGB(56, 60, 72));
            Box(dc, rc, fill, edge, 1, 3);

            if (fCellNo && RW(rc) >= 16 && RH(rc) >= 14) {
                if (!placed.empty()) {
                    // 置いた字をそのまま出すと、半紙の仕上がりが一目で分かる
                    Center(dc, rc, placed.c_str(), fCellNo,
                        following ? RGB(150, 158, 172) : RGB(235, 240, 250));
                } else {
                    wchar_t noBuf[8];
                    wsprintfW(noBuf, L"%d", i + 1);
                    Center(dc, rc, noBuf, fCellNo, RGB(96, 104, 118));
                }
            }
        }
        if (fCellNo) DeleteObject(fCellNo);

        // 配置: ペン追従へ戻す
        bool hovFollow = (ui.hoverSub == 71);
        Box(dc, ui.rOtehonFollowBtn, following ? RGB(36, 68, 105) : (hovFollow ? RGB(42, 46, 56) : RGB(30, 33, 40)),
            following ? RGB(70, 135, 220) : (hovFollow ? RGB(66, 72, 86) : RGB(46, 50, 62)), 1, 6);
        HFONT fFollow = CreateCustomFont(14, FW_BOLD);
        Center(dc, ui.rOtehonFollowBtn, following ? L"✓ ペンに追従" : L"ペンに追従", fFollow,
            following ? RGB(255, 255, 255) : RGB(190, 195, 205));
        DeleteObject(fFollow);

        HFONT fHint = CreateCustomFont(12);
        RECT rHint = { ui.rOtehonFollowBtn.left, ui.rOtehonFollowBtn.bottom + 8, ui.rOtehonFollowBtn.right, ui.rOtehonCellMapBox.bottom };
        DrawTextCustom(dc, rHint, L"下の文字を選び、左の升目を\n押すと配置します（複数可）。\n同じ字をもう一度押すと消去", fHint,
            RGB(140, 148, 162), DT_LEFT | DT_TOP | DT_WORDBREAK);
        DeleteObject(fHint);

        RECT rOteHeader = { ui.rSub.left + 16, ui.rOtehonInputBox.top - 22, ui.rSub.right - 16, ui.rOtehonInputBox.top - 6 };
        HFONT foh = CreateCustomFont(13, FW_BOLD);
        DrawTextCustom(dc, rOteHeader, L"書きたい文字を入力（日本語入力で変換・確定）", foh, RGB(180, 185, 195));
        DeleteObject(foh);

        // 文字入力欄。入力中は枠を強調し、末尾にキャレットを描く
        bool typing = state.otehon.isTyping;
        bool hovInput = (ui.hoverSub == 73);
        Box(dc, ui.rOtehonInputBox, RGB(22, 24, 29),
            typing ? RGB(90, 155, 235) : (hovInput ? RGB(66, 72, 86) : RGB(50, 55, 68)), typing ? 2 : 1, 6);

        HFONT fInput = CreateCustomFont(20);
        RECT rInputText = { ui.rOtehonInputBox.left + 12, ui.rOtehonInputBox.top,
                            ui.rOtehonInputBox.right - 12, ui.rOtehonInputBox.bottom };
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
        int paletteCount = state.otehon.GetPaletteCount();
        for (int i = 0; i < paletteCount && i < 8; ++i) {
            bool act = (state.otehon.selectedIndex == i);
            bool hov = (ui.hoverSub == 80 + i);
            DrawTileCard(dc, ui.rOtehonTile[i], state.otehon.GetCharacter(i).c_str(), L"", act, hov);
        }

        RECT rCard = { ui.rSub.left + 14, ui.rOtehonOpacityTrack.top - 24, ui.rSub.right - 14, ui.rOtehonOpacityTrack.bottom + 28 };
        Box(dc, rCard, RGB(32, 35, 42), RGB(50, 55, 68), 1, 6);

        HFONT fTitle = CreateCustomFont(13, FW_BOLD);
        RECT rTitle = { rCard.left + 14, rCard.top + 6, rCard.left + 150, rCard.top + 24 };
        DrawTextCustom(dc, rTitle, L"お手本の透過度（濃淡）", fTitle, RGB(220, 225, 235));

        wchar_t valBuf[32];
        swprintf_s(valBuf, 32, L"%d%%", (int)(state.otehon.opacity * 100.0));
        RECT rVal = { rCard.right - 80, rCard.top + 8, rCard.right - 14, rCard.top + 26 };
        DrawTextCustom(dc, rVal, valBuf, fTitle, RGB(100, 160, 230), DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
        DeleteObject(fTitle);

        Fill(dc, ui.rOtehonOpacityTrack, RGB(18, 20, 24));
        int thumbX = ui.rOtehonOpacityTrack.left + (int)(RW(ui.rOtehonOpacityTrack) * state.otehon.opacity);
        RECT rLevel = ui.rOtehonOpacityTrack;
        rLevel.right = thumbX;
        Fill(dc, rLevel, RGB(65, 120, 190));

        RECT thumb = { thumbX - 5, ui.rOtehonOpacityTrack.top - 4, thumbX + 5, ui.rOtehonOpacityTrack.bottom + 4 };
        Box(dc, thumb, state.otehon.isDraggingOpacity ? RGB(180, 210, 255) : RGB(140, 180, 230), RGB(220, 235, 255), 1, 3);
    }
}

void FloatingMenuView::Draw(HDC dc, const AppState& state) {
    using namespace RenderUtils;
    const UIState& ui = state.ui;

    if (!ui.isSubPanelOpen) {
        bool hov = (ui.hoverTb == TbButton::NavToggle);
        Box(dc, ui.rTbNavToggle, hov ? RGB(45, 52, 68) : RGB(30, 33, 42), hov ? RGB(85, 140, 230) : RGB(52, 58, 74), 1, 8);
        HFONT f = CreateCustomFont(16, FW_BOLD);
        Center(dc, ui.rTbNavToggle, L"<<<", f, hov ? RGB(255, 255, 255) : RGB(195, 205, 225));
        DeleteObject(f);
        return;
    }

    Box(dc, ui.rSub, RGB(22, 24, 30), RGB(56, 62, 78), 2, 12);

    HPEN sp = CreatePen(PS_SOLID, 1, RGB(38, 42, 54));
    HPEN osp = (HPEN)SelectObject(dc, sp);
    MoveToEx(dc, ui.rSub.left + 12, ui.rSub.top + 54, nullptr);
    LineTo(dc, ui.rSub.right - 12, ui.rSub.top + 54);
    SelectObject(dc, osp);
    DeleteObject(sp);

    bool hovTog = (ui.hoverTb == TbButton::NavToggle);
    Box(dc, ui.rTbNavToggle, hovTog ? RGB(45, 52, 68) : RGB(32, 35, 46), hovTog ? RGB(85, 140, 230) : RGB(52, 58, 74), 1, 6);
    HFONT fTog = CreateCustomFont(16, FW_BOLD);
    Center(dc, ui.rTbNavToggle, L">>>", fTog, hovTog ? RGB(255, 255, 255) : RGB(195, 205, 225));
    DeleteObject(fTog);

    auto DrawTab = [&](RECT r, const wchar_t* label, bool active, bool hover) {
        COLORREF bg = active ? RGB(42, 68, 110) : (hover ? RGB(38, 42, 54) : RGB(28, 30, 38));
        COLORREF border = active ? RGB(75, 135, 225) : (hover ? RGB(65, 72, 90) : RGB(44, 48, 60));
        COLORREF text = active ? RGB(255, 255, 255) : (hover ? RGB(240, 244, 252) : RGB(170, 175, 185));
        Box(dc, r, bg, border, 1, 6);
        HFONT f = CreateCustomFont(13, active ? FW_BOLD : FW_NORMAL);
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
