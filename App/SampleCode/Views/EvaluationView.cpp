#include "stdafx.h"
#include "EvaluationView.h"
#include "EvaluationScore.h"
#include "RenderUtils.h"
#include <cwchar>

namespace {

// 点数の色分け。EvaluationScore::GradeLabel と段を揃える。
COLORREF ScoreColor(double score) {
    if (score >= 90.0) return RGB(110, 215, 150);
    if (score >= 75.0) return RGB(140, 205, 120);
    if (score >= 60.0) return RGB(215, 190, 90);
    if (score >= 40.0) return RGB(220, 150, 85);
    return RGB(215, 110, 100);
}

} // namespace

void EvaluationView::DrawBar(HDC dc, const RECT& box, double value, COLORREF fill) {
    using namespace RenderUtils;
    Box(dc, box, RGB(24, 27, 34), RGB(48, 54, 68), 1, 3);
    if (value <= 0.0) return;
    if (value > 1.0) value = 1.0;

    int w = RW(box) - 2;
    int filled = (int)(w * value);
    if (filled < 1) filled = 1;
    RECT inner = { box.left + 1, box.top + 1, box.left + 1 + filled, box.bottom - 1 };
    HBRUSH br = CreateSolidBrush(fill);
    FillRect(dc, &inner, br);
    DeleteObject(br);
}

void EvaluationView::DrawCellRow(HDC dc, const RECT& row, const CellCompare& cell, bool detail) {
    using namespace RenderUtils;
    if (RW(row) <= 0 || RH(row) <= 0) return;

    Box(dc, row, RGB(26, 29, 38), RGB(44, 50, 64), 1, 6);

    // 左端に対象の字
    RECT rChar = { row.left + 8, row.top + 6, row.left + 48, row.bottom - 6 };
    HFONT fChar = CreateCustomFont(26, FW_BOLD);
    Center(dc, rChar, cell.text.c_str(), fChar, RGB(235, 240, 250));
    DeleteObject(fChar);

    if (!cell.grid.valid || !cell.hasInk) {
        RECT rNo = { rChar.right + 10, row.top, row.right - 10, row.bottom };
        HFONT fNo = CreateCustomFont(15, FW_NORMAL);
        DrawTextCustom(dc, rNo, L"まだ書かれていません", fNo,
                       RGB(140, 148, 165), DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        DeleteObject(fNo);
        return;
    }

    // 右端にこの字の点数
    wchar_t num[32];
    swprintf_s(num, 32, L"%.0f", cell.totalScore);
    HFONT fNum = CreateCustomFont(28, FW_BOLD);
    RECT rNum = { row.right - 78, row.top + 4, row.right - 10, row.top + 42 };
    Center(dc, rNum, num, fNum, ScoreColor(cell.totalScore));
    DeleteObject(fNum);

    int x = rChar.right + 10;
    int barRight = row.right - 86;

    if (!detail) {
        // 畳んでいるときは点数の帯だけ
        RECT rB = { x, row.top + 17, barRight, row.top + 29 };
        DrawBar(dc, rB, cell.totalScore / 100.0, ScoreColor(cell.totalScore));
        return;
    }

    // 詳細: 形 / 位置 / 大きさ の内訳
    HFONT fLbl = CreateCustomFont(13, FW_NORMAL);
    const wchar_t* names[3] = { L"形", L"位置", L"大きさ" };
    double vals[3] = { cell.shapeScore, cell.posScore, cell.sizeScore };
    for (int i = 0; i < 3; ++i) {
        int y = row.top + 6 + i * 16;
        RECT rL = { x, y, x + 40, y + 14 };
        DrawTextCustom(dc, rL, names[i], fLbl, RGB(165, 175, 195), DT_LEFT | DT_SINGLELINE);
        RECT rB = { x + 44, y + 2, barRight, y + 12 };
        DrawBar(dc, rB, vals[i] / 100.0, ScoreColor(vals[i]));
    }

    // 生の指標。点数の根拠を確かめたいときのために出す。
    wchar_t buf[160];
    swprintf_s(buf, 160,
        L"一致 %.2f  はみ出し %.0f%%  欠け %.0f%%  ずれ %+.0f%%,%+.0f%%  大きさ %.2f/%.2f",
        cell.shape.iou, cell.grid.overflow * 100.0, cell.grid.missing * 100.0,
        cell.centroidDx * 100.0, cell.centroidDy * 100.0,
        cell.sizeRatioX, cell.sizeRatioY);
    RECT rRaw = { x, row.top + 56, row.right - 10, row.bottom - 2 };
    DrawTextCustom(dc, rRaw, buf, fLbl, RGB(135, 145, 165), DT_LEFT | DT_SINGLELINE);
    DeleteObject(fLbl);
}

void EvaluationView::Draw(HDC dc, const AppState& state) {
    using namespace RenderUtils;
    const UIState& ui = state.ui;
    const CompareResult& r = state.evaluation;

    // 「お手本と比べる」
    bool hovRun = (ui.hoverSub == 100);
    Box(dc, ui.rEvalRunBtn, hovRun ? RGB(48, 82, 132) : RGB(36, 52, 80),
        hovRun ? RGB(90, 155, 245) : RGB(60, 105, 175), 1, 8);
    HFONT fRun = CreateCustomFont(20, FW_BOLD);
    Center(dc, ui.rEvalRunBtn, L"お手本と比べる", fRun, RGB(255, 255, 255));
    DeleteObject(fRun);

    // 総合点
    RECT rScore = { ui.rSub.left + 18, ui.rEvalRunBtn.bottom + 14,
                    ui.rSub.right - 18, ui.rEvalRunBtn.bottom + 14 + 150 };
    Box(dc, rScore, RGB(20, 23, 30), RGB(46, 54, 70), 1, 10);

    if (!r.valid) {
        HFONT f = CreateCustomFont(17, FW_NORMAL);
        const wchar_t* msg = r.message.empty()
            ? L"お手本をマスに置いて字を書いたあと、\n「お手本と比べる」を押してください"
            : r.message.c_str();
        RECT rt = { rScore.left + 16, rScore.top + 16, rScore.right - 16, rScore.bottom - 16 };
        DrawTextCustom(dc, rt, msg, f, RGB(160, 170, 190), DT_CENTER | DT_WORDBREAK);
        DeleteObject(f);
        return;
    }

    COLORREF sc = ScoreColor(r.totalScore);

    // 大きな点数。100点満点であることを添える。
    wchar_t big[32];
    swprintf_s(big, 32, L"%.0f", r.totalScore);
    HFONT fBig = CreateCustomFont(72, FW_BOLD);
    HFONT oldF = (HFONT)SelectObject(dc, fBig);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, sc);
    SIZE ext{};
    GetTextExtentPoint32W(dc, big, (int)wcslen(big), &ext);
    int cx = (rScore.left + rScore.right) / 2;
    int bigX = cx - (ext.cx + 46) / 2;
    TextOutW(dc, bigX, rScore.top + 18, big, (int)wcslen(big));
    SelectObject(dc, oldF);
    DeleteObject(fBig);

    HFONT fUnit = CreateCustomFont(22, FW_NORMAL);
    oldF = (HFONT)SelectObject(dc, fUnit);
    SetTextColor(dc, RGB(150, 160, 180));
    TextOutW(dc, bigX + ext.cx + 6, rScore.top + 58, L"/ 100", 5);
    SelectObject(dc, oldF);
    DeleteObject(fUnit);

    // 評語
    HFONT fGrade = CreateCustomFont(22, FW_BOLD);
    RECT rGrade = { rScore.left + 12, rScore.top + 96, rScore.right - 12, rScore.top + 124 };
    Center(dc, rGrade, EvaluationScore::GradeLabel(r.totalScore), fGrade, sc);
    DeleteObject(fGrade);

    // 但し書き。点数の根拠が「重なり」であることを隠さない。
    HFONT fNote = CreateCustomFont(13, FW_NORMAL);
    wchar_t note[128];
    swprintf_s(note, 128, L"%d 字を採点（形 60%% / 位置 25%% / 大きさ 15%%）", r.scoredCells);
    RECT rNote = { rScore.left + 12, rScore.top + 126, rScore.right - 12, rScore.bottom - 4 };
    DrawTextCustom(dc, rNote, note, fNote, RGB(125, 135, 155), DT_CENTER | DT_SINGLELINE);
    DeleteObject(fNote);

    // 詳細の開閉
    bool hovDet = (ui.hoverSub == 102);
    bool det = ui.showEvalDetail;
    Box(dc, ui.rEvalDetailBtn, det ? RGB(40, 76, 120) : (hovDet ? RGB(46, 52, 64) : RGB(32, 36, 46)),
        det ? RGB(80, 150, 245) : (hovDet ? RGB(72, 80, 96) : RGB(50, 56, 70)), 1, 8);
    HFONT fDet = CreateCustomFont(17, FW_NORMAL);
    Center(dc, ui.rEvalDetailBtn, det ? L"▲ 詳細を閉じる" : L"▼ 詳細を見る", fDet,
           det ? RGB(225, 240, 255) : RGB(195, 205, 222));
    DeleteObject(fDet);

    // 半紙の上に重ねる表示
    bool hovOv = (ui.hoverSub == 101);
    bool on = ui.showEvalOverlay;
    Box(dc, ui.rEvalOverlayBtn, on ? RGB(40, 76, 120) : (hovOv ? RGB(46, 52, 64) : RGB(32, 36, 46)),
        on ? RGB(80, 150, 245) : (hovOv ? RGB(72, 80, 96) : RGB(50, 56, 70)), 1, 8);
    HFONT fOv = CreateCustomFont(16, FW_NORMAL);
    Center(dc, ui.rEvalOverlayBtn,
           on ? L"半紙に重ねる：オン（緑=一致 青=はみ出し 橙=欠け）" : L"半紙に重ねる：オフ",
           fOv, on ? RGB(225, 240, 255) : RGB(190, 200, 218));
    DeleteObject(fOv);

    // マスごと
    for (const CellCompare& c : r.cells) {
        if (c.cellIndex < 0 || c.cellIndex >= MAX_GRID_CELLS) continue;
        const RECT& row = ui.rEvalCellRow[c.cellIndex];
        if (RW(row) <= 0) continue; // パネルに入らなかった行
        DrawCellRow(dc, row, c, ui.showEvalDetail);
    }
}
