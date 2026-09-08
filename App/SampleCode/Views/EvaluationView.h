#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include "AppState.h"

// 左メニュー「評価」タブの中身。
// 計算そのものは Evaluation/ShapeCompare が持ち、ここは結果を描くだけ。
class EvaluationView {
public:
    static void Draw(HDC dc, const AppState& state);

private:
    // 1マスぶんの明細行。detail が false なら点数の帯だけに畳む。
    static void DrawCellRow(HDC dc, const RECT& row, const CellCompare& cell, bool detail);
    // 0.0〜1.0 の値を横棒で示す
    static void DrawBar(HDC dc, const RECT& box, double value, COLORREF fill);
};
