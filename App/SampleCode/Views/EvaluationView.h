#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include "AppState.h"

// 左メニュー「評価」タブの中身。
// 計算そのものは Evaluation/ShapeCompare（字形）と Evaluation/Brushwork（運筆）が
// 持ち、ここは結果を描くだけ。
class EvaluationView {
public:
    static void Draw(HDC dc, const AppState& state);

private:
    // 字形 / 運筆 の切り替え
    static void DrawSectionSwitch(HDC dc, const AppState& state);

    // お手本と比べた字形の評価
    static void DrawShapeSection(HDC dc, const AppState& state);
    // お手本を使わない運筆の評価
    static void DrawWorkSection(HDC dc, const AppState& state);

    // 総合点の大枠。字形・運筆で同じ見た目にする。
    static void DrawScoreHeadline(HDC dc, const RECT& box, double score, const wchar_t* note);
    // 点数が出ていないときの案内
    static void DrawScoreNotice(HDC dc, const RECT& box, const wchar_t* message);

    // 1マスぶんの明細行。detail が false なら点数の帯だけに畳む。
    static void DrawCellRow(HDC dc, const RECT& row, const CellCompare& cell, bool detail);
    // 運筆: 軸ごとの内訳（筆の立て方 / 速さ / 抑揚 / テンポ）
    static void DrawWorkAxisRow(HDC dc, const RECT& row, const wchar_t* name,
                                const WorkAxisScore& score, const wchar_t* detail);
    // 運筆: 1画ぶんの明細行
    static void DrawStrokeRow(HDC dc, const RECT& row, const StrokeWork& work);

    // 0.0〜1.0 の値を横棒で示す
    static void DrawBar(HDC dc, const RECT& box, double value, COLORREF fill);
};
