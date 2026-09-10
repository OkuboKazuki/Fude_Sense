#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <vector>
#include <string>
#include "EvaluationResult.h"
#include "AppState.h"
#include "GpuInk.h"

// ---------------------------------------------------------------------------
// お手本と墨を同じ土俵へ乗せて、形のずれを数値にする。
//
// お手本はフォントのグリフなので筆順・画数を持たない。ここで測れるのは
// 「どこに墨が乗ったか」だけで、書き順や画数の正誤は対象外。
// また毛筆楷書フォントでも筆で書いた字とは太さの出方が違うため、
// 一致率 100% は原理的に出ない。点数化はここではなく後段で行う。
//
// 結果の型は EvaluationResult.h にある（AppState が持つため）。
// ---------------------------------------------------------------------------

class ShapeCompare {
public:
    // 現在の半紙の墨と、置かれているお手本を比較する。
    // refDC は互換 DC を作る元にするだけで、そこへは描かない。
    static CompareResult Evaluate(HDC refDC, const AppState& state, GpuInk& gpuInk);

    // --- 部品（単体で確かめられるよう公開しておく）---

    // お手本グリフを升目と同じ大きさのマスクにする。
    static bool BuildOtehonMask(HDC refDC, const RECT& cell, const std::wstring& text,
                                OtehonFontStyle style, ShapeMask& out);
    // 墨のスナップショットから升目ぶんを切り出してマスクにする。
    // cellInPaper は半紙ローカル座標。
    static bool BuildInkMask(const std::vector<int>& ink, int inkW, int inkH,
                             const RECT& cellInPaper, ShapeMask& out);

    // 同じ大きさの2枚をそのまま比べる（升目基準）。
    static ShapeMetrics CompareDirect(const ShapeMask& otehon, const ShapeMask& ink);
    // それぞれの外接矩形を正方形へ引き伸ばしてから比べる（形だけ）。
    static ShapeMetrics CompareNormalized(const ShapeMask& otehon, const ShapeMask& ink);
};
