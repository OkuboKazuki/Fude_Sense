#include "stdafx.h"
#include "EvaluationView.h"
#include "EvaluationScore.h"
#include "Brushwork.h"
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

// ミリ秒を「0.82 秒」の形で書く
void FormatSeconds(wchar_t* buf, size_t count, double ms) {
    swprintf_s(buf, count, L"%.2f 秒", ms / 1000.0);
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

void EvaluationView::DrawScoreHeadline(HDC dc, const RECT& box, double score, const wchar_t* note) {
    using namespace RenderUtils;
    Box(dc, box, RGB(20, 23, 30), RGB(46, 54, 70), 1, 10);

    COLORREF sc = ScoreColor(score);

    // 大きな点数。100点満点であることを添える。
    wchar_t big[32];
    swprintf_s(big, 32, L"%.0f", score);
    HFONT fBig = CreateCustomFont(72, FW_BOLD);
    HFONT oldF = (HFONT)SelectObject(dc, fBig);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, sc);
    SIZE ext{};
    GetTextExtentPoint32W(dc, big, (int)wcslen(big), &ext);
    int cx = (box.left + box.right) / 2;
    int bigX = cx - (ext.cx + 46) / 2;
    TextOutW(dc, bigX, box.top + 18, big, (int)wcslen(big));
    SelectObject(dc, oldF);
    DeleteObject(fBig);

    HFONT fUnit = CreateCustomFont(22, FW_NORMAL);
    oldF = (HFONT)SelectObject(dc, fUnit);
    SetTextColor(dc, RGB(150, 160, 180));
    TextOutW(dc, bigX + ext.cx + 6, box.top + 58, L"/ 100", 5);
    SelectObject(dc, oldF);
    DeleteObject(fUnit);

    // 評語
    HFONT fGrade = CreateCustomFont(22, FW_BOLD);
    RECT rGrade = { box.left + 12, box.top + 96, box.right - 12, box.top + 124 };
    Center(dc, rGrade, EvaluationScore::GradeLabel(score), fGrade, sc);
    DeleteObject(fGrade);

    // 但し書き。点数の根拠を隠さない。
    HFONT fNote = CreateCustomFont(13, FW_NORMAL);
    RECT rNote = { box.left + 12, box.top + 126, box.right - 12, box.bottom - 4 };
    DrawTextCustom(dc, rNote, note, fNote, RGB(125, 135, 155), DT_CENTER | DT_SINGLELINE);
    DeleteObject(fNote);
}

void EvaluationView::DrawScoreNotice(HDC dc, const RECT& box, const wchar_t* message) {
    using namespace RenderUtils;
    Box(dc, box, RGB(20, 23, 30), RGB(46, 54, 70), 1, 10);

    HFONT f = CreateCustomFont(17, FW_NORMAL);
    RECT rt = { box.left + 16, box.top + 16, box.right - 16, box.bottom - 16 };
    DrawTextCustom(dc, rt, message, f, RGB(160, 170, 190), DT_CENTER | DT_WORDBREAK);
    DeleteObject(f);
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

void EvaluationView::DrawWorkAxisRow(HDC dc, const RECT& row, const wchar_t* name,
                                     const WorkAxisScore& score, const wchar_t* detail) {
    using namespace RenderUtils;
    if (RW(row) <= 0 || RH(row) <= 0) return;

    Box(dc, row, RGB(26, 29, 38), RGB(44, 50, 64), 1, 6);

    HFONT fName = CreateCustomFont(15, FW_BOLD);
    RECT rName = { row.left + 10, row.top + 4, row.left + 110, row.top + 24 };
    DrawTextCustom(dc, rName, name, fName, RGB(215, 222, 238), DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    DeleteObject(fName);

    HFONT fDetail = CreateCustomFont(13, FW_NORMAL);
    RECT rDetail = { row.left + 10, row.top + 24, row.right - 66, row.bottom - 2 };
    DrawTextCustom(dc, rDetail, detail, fDetail, RGB(135, 145, 165), DT_LEFT | DT_SINGLELINE);
    DeleteObject(fDetail);

    if (!score.valid) {
        // 測れなかった軸。点数の代わりに「—」を置き、総合点にも混ざらない。
        HFONT fNone = CreateCustomFont(20, FW_BOLD);
        RECT rNone = { row.right - 62, row.top + 4, row.right - 10, row.bottom - 4 };
        Center(dc, rNone, L"—", fNone, RGB(120, 128, 145));
        DeleteObject(fNone);
        return;
    }

    COLORREF c = ScoreColor(score.score);

    RECT rBar = { row.left + 116, row.top + 8, row.right - 66, row.top + 20 };
    DrawBar(dc, rBar, score.score / 100.0, c);

    wchar_t num[32];
    swprintf_s(num, 32, L"%.0f", score.score);
    HFONT fNum = CreateCustomFont(24, FW_BOLD);
    RECT rNum = { row.right - 62, row.top + 4, row.right - 10, row.bottom - 4 };
    Center(dc, rNum, num, fNum, c);
    DeleteObject(fNum);
}

void EvaluationView::DrawStrokeRow(HDC dc, const RECT& row, const StrokeWork& work) {
    using namespace RenderUtils;
    if (RW(row) <= 0 || RH(row) <= 0) return;

    Box(dc, row, RGB(24, 27, 35), RGB(42, 48, 62), 1, 5);

    wchar_t label[32];
    swprintf_s(label, 32, L"第%d画", work.strokeId);
    HFONT fLbl = CreateCustomFont(14, FW_BOLD);
    RECT rLbl = { row.left + 8, row.top, row.left + 62, row.bottom };
    DrawTextCustom(dc, rLbl, label, fLbl, RGB(200, 208, 225), DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    DeleteObject(fLbl);

    // 所要時間・画間・速さのばらつき・寝かせ具合を1行に詰める。
    // どれも「この画の何を直すか」を指す手がかりなので、点数より素の値を出す。
    wchar_t dur[32];
    FormatSeconds(dur, 32, work.durationMs);

    wchar_t gap[32];
    if (work.gapBeforeMs >= 0.0) {
        wchar_t g[32];
        FormatSeconds(g, 32, work.gapBeforeMs);
        swprintf_s(gap, 32, L"間 %s", g);
    } else {
        swprintf_s(gap, 32, L"間 —");
    }

    wchar_t spd[32];
    if (work.speedValid) swprintf_s(spd, 32, L"ばらつき %.2f", work.speedCv);
    else swprintf_s(spd, 32, L"ばらつき —");

    wchar_t tilt[32];
    if (work.tiltValid) swprintf_s(tilt, 32, L"寝かせ %.0f%%", work.lyingRatio * 100.0);
    else swprintf_s(tilt, 32, L"寝かせ —");

    wchar_t buf[160];
    swprintf_s(buf, 160, L"%s  %s  %s  %s", dur, gap, spd, tilt);

    HFONT fDetail = CreateCustomFont(13, FW_NORMAL);
    RECT rDetail = { row.left + 66, row.top, row.right - 54, row.bottom };
    DrawTextCustom(dc, rDetail, buf, fDetail, RGB(140, 150, 170), DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    DeleteObject(fDetail);

    if (!work.scored) return;

    wchar_t num[32];
    swprintf_s(num, 32, L"%.0f", work.totalScore);
    HFONT fNum = CreateCustomFont(18, FW_BOLD);
    RECT rNum = { row.right - 50, row.top, row.right - 8, row.bottom };
    Center(dc, rNum, num, fNum, ScoreColor(work.totalScore));
    DeleteObject(fNum);
}

void EvaluationView::DrawSectionSwitch(HDC dc, const AppState& state) {
    using namespace RenderUtils;
    const UIState& ui = state.ui;

    const wchar_t* labels[2] = { L"字形（お手本と比べる）", L"運筆（書きぶり）" };
    for (int i = 0; i < 2; ++i) {
        bool active = ((int)ui.evalSection == i);
        bool hover = (ui.hoverSub == 103 + i);
        Box(dc, ui.rEvalSectionBtn[i],
            active ? RGB(40, 76, 120) : (hover ? RGB(46, 52, 64) : RGB(30, 34, 44)),
            active ? RGB(80, 150, 245) : (hover ? RGB(72, 80, 96) : RGB(50, 56, 70)), 1, 8);
        HFONT f = CreateCustomFont(16, active ? FW_BOLD : FW_NORMAL);
        Center(dc, ui.rEvalSectionBtn[i], labels[i], f,
               active ? RGB(235, 244, 255) : RGB(185, 195, 215));
        DeleteObject(f);
    }
}

void EvaluationView::DrawWorkSection(HDC dc, const AppState& state) {
    using namespace RenderUtils;
    const UIState& ui = state.ui;
    const BrushworkResult& r = state.brushwork;

    if (!r.valid) {
        const wchar_t* msg = r.message.empty()
            ? L"半紙に字を書くと、お手本なしで書きぶりを測ります"
            : r.message.c_str();
        DrawScoreNotice(dc, ui.rEvalWorkScoreBox, msg);
        return;
    }

    wchar_t note[160];
    swprintf_s(note, 160, L"%d 画を採点（立て方 25%% / 速さ 30%% / 抑揚 25%% / テンポ 20%%）",
               r.scoredStrokes);
    DrawScoreHeadline(dc, ui.rEvalWorkScoreBox, r.totalScore, note);

    // --- 軸ごとの内訳。素の値を添えて、点数の出どころを示す ---
    wchar_t detail[WORK_AXIS_COUNT][160];

    if (r.tiltValid) {
        swprintf_s(detail[(int)WorkAxis::Tilt], 160,
                   L"高度角 平均 %.0f°（90°=垂直）／%.0f° より寝ていた区間 %.0f%%",
                   r.altitudeMeanDeg, Brushwork::LyingAltitudeDeg(), r.lyingRatio * 100.0);
    } else {
        swprintf_s(detail[(int)WorkAxis::Tilt], 160,
                   L"このタブレットは筆の傾きを返さないため測れません");
    }

    if (r.axis[(int)WorkAxis::Speed].valid) {
        swprintf_s(detail[(int)WorkAxis::Speed], 160,
                   L"送筆の平均 %.0f px/s／ばらつき %.2f（小さいほど一定の速さ）",
                   r.speedMean, r.speedCv);
    } else {
        swprintf_s(detail[(int)WorkAxis::Speed], 160,
                   L"速さを測れるほど長い画がありません");
    }

    if (r.axis[(int)WorkAxis::Swing].valid) {
        swprintf_s(detail[(int)WorkAxis::Swing], 160,
                   L"筆圧の幅 %.2f／線幅の変化 %.0f%%（大きいほど太細がはっきり）",
                   r.pressureSwing, r.widthSwing * 100.0);
    } else {
        swprintf_s(detail[(int)WorkAxis::Swing], 160,
                   L"抑揚を測れるほど長い画がありません");
    }

    {
        wchar_t dur[32];
        FormatSeconds(dur, 32, r.avgDurationMs);
        if (r.avgGapMs >= 0.0) {
            wchar_t gap[32];
            FormatSeconds(gap, 32, r.avgGapMs);
            swprintf_s(detail[(int)WorkAxis::Tempo], 160,
                       L"1画あたり %s／画間 %s", dur, gap);
        } else {
            swprintf_s(detail[(int)WorkAxis::Tempo], 160,
                       L"1画あたり %s／画間は1画だけでは測れません", dur);
        }
    }

    for (int i = 0; i < WORK_AXIS_COUNT; ++i) {
        DrawWorkAxisRow(dc, ui.rEvalWorkAxisRow[i], WorkAxisName((WorkAxis)i),
                        r.axis[i], detail[i]);
    }

    // --- 画ごと ---
    int headerTop = ui.rEvalWorkAxisRow[WORK_AXIS_COUNT - 1].bottom + 8;
    HFONT fHead = CreateCustomFont(15, FW_BOLD);
    RECT rHead = { ui.rEvalWorkAxisRow[0].left, headerTop, ui.rEvalWorkAxisRow[0].right, headerTop + 22 };
    DrawTextCustom(dc, rHead, L"画ごと", fHead, RGB(205, 213, 230), DT_LEFT | DT_SINGLELINE);
    DeleteObject(fHead);

    // パネルに入る行数を数える
    int visibleRows = 0;
    for (int i = 0; i < MAX_EVAL_STROKE_ROWS; ++i) {
        if (RW(ui.rEvalStrokeRow[i]) > 0) ++visibleRows;
    }
    if (visibleRows <= 0) return;

    // 入りきらないときは最後の1行を「ほか N 画」に使う。
    // ただし1行しか入らない窓では、注記よりも画そのものを出す。
    int total = (int)r.strokes.size();
    int shown = total;
    if (total > visibleRows) {
        shown = (visibleRows > 1) ? visibleRows - 1 : visibleRows;
    }

    for (int i = 0; i < shown; ++i) {
        DrawStrokeRow(dc, ui.rEvalStrokeRow[i], r.strokes[i]);
    }

    if (shown < total) {
        wchar_t more[64];
        swprintf_s(more, 64, L"… ほか %d 画", total - shown);
        HFONT fMore = CreateCustomFont(13, FW_NORMAL);
        DrawTextCustom(dc, ui.rEvalStrokeRow[shown], more, fMore,
                       RGB(130, 140, 160), DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        DeleteObject(fMore);
    }
}

void EvaluationView::DrawShapeSection(HDC dc, const AppState& state) {
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

    if (!r.valid) {
        const wchar_t* msg = r.message.empty()
            ? L"お手本をマスに置いて字を書いたあと、\n「お手本と比べる」を押してください"
            : r.message.c_str();
        DrawScoreNotice(dc, rScore, msg);
        return;
    }

    wchar_t note[128];
    swprintf_s(note, 128, L"%d 字を採点（形 60%% / 位置 25%% / 大きさ 15%%）", r.scoredCells);
    DrawScoreHeadline(dc, rScore, r.totalScore, note);

    // 詳細の開閉
    bool hovDet = (ui.hoverSub == 102);
    bool det = ui.showEvalDetail;
    Box(dc, ui.rEvalDetailBtn, det ? RGB(40, 76, 120) : (hovDet ? RGB(46, 52, 64) : RGB(32, 36, 46)),
        det ? RGB(80, 150, 245) : (hovDet ? RGB(72, 80, 96) : RGB(50, 56, 70)), 1, 8);
    HFONT fDet = CreateCustomFont(17, FW_NORMAL);
    Center(dc, ui.rEvalDetailBtn, det ? L"▲ 詳細を閉じる" : L"▼ 詳細を見る", fDet,
           det ? RGB(225, 240, 255) : RGB(195, 205, 222));
    DeleteObject(fDet);

    // マスごと
    for (const CellCompare& c : r.cells) {
        if (c.cellIndex < 0 || c.cellIndex >= MAX_GRID_CELLS) continue;
        const RECT& row = ui.rEvalCellRow[c.cellIndex];
        if (RW(row) <= 0) continue; // パネルに入らなかった行
        DrawCellRow(dc, row, c, ui.showEvalDetail);
    }
}

void EvaluationView::Draw(HDC dc, const AppState& state) {
    DrawSectionSwitch(dc, state);

    if (state.ui.evalSection == EvalSection::Brushwork) {
        DrawWorkSection(dc, state);
    } else {
        DrawShapeSection(dc, state);
    }
}
