#include "stdafx.h"
#include "EvaluationScore.h"
#include <algorithm>
#include <cmath>

namespace {

// ===========================================================================
// 較正の対象。ここだけを触れば点数の出方が変わる。
// ===========================================================================

// --- 形 ---------------------------------------------------------------
// 外接矩形を合わせた一致率（形だけの比較）を点数へ写す範囲。
// 筆の線は毛筆フォントより太さが揺れるので、上手く書けても 1.00 には届かない。
// kShapeCeil に届いたら 100 点、kShapeFloor 以下は 0 点として間を直線で結ぶ。
constexpr double kShapeFloor = 0.20;
constexpr double kShapeCeil  = 0.70;

// --- 位置 -------------------------------------------------------------
// 重心のずれ（升目の一辺に対する比）。これだけ離れたら 0 点。
// 升目の 15% ずれるとはっきり寄って見える、という見立ての暫定値。
constexpr double kPosTolerance = 0.15;

// --- 大きさ -----------------------------------------------------------
// 外接矩形の比が 1.0 からどれだけ離れたら 0 点になるか。
// 0.40 は「お手本の 1.4 倍／0.6 倍」に相当する。
constexpr double kSizeTolerance = 0.40;

// --- 重み（合計 1.0）--------------------------------------------------
// 習字では字形が最も重く、次に升目の中での位置、大きさは補助という配分。
constexpr double kWeightShape = 0.60;
constexpr double kWeightPos   = 0.25;
constexpr double kWeightSize  = 0.15;

// ===========================================================================

double Clamp01(double v) { return v < 0.0 ? 0.0 : (v > 1.0 ? 1.0 : v); }

// lo〜hi を 0〜1 へ直線で写す
double MapRange(double v, double lo, double hi) {
    if (hi <= lo) return 0.0;
    return Clamp01((v - lo) / (hi - lo));
}

} // namespace

void EvaluationScore::Apply(CompareResult& result) {
    double sum = 0.0;
    int n = 0;

    for (CellCompare& c : result.cells) {
        c.shapeScore = 0.0;
        c.posScore = 0.0;
        c.sizeScore = 0.0;
        c.totalScore = 0.0;

        // 書かれていないマスは平均に混ぜない。書いていない字で
        // 点が下がると、途中まで書いた段階の点数が意味を失う。
        if (!c.grid.valid || !c.hasInk) continue;

        // 形: 位置と大きさを落としたうえでの一致率
        c.shapeScore = MapRange(c.shape.iou, kShapeFloor, kShapeCeil) * 100.0;

        // 位置: 重心のずれの大きさ
        double dist = std::sqrt(c.centroidDx * c.centroidDx + c.centroidDy * c.centroidDy);
        c.posScore = (1.0 - Clamp01(dist / kPosTolerance)) * 100.0;

        // 大きさ: 縦横それぞれの比のうち、悪いほうを採る
        double dx = std::fabs(c.sizeRatioX - 1.0);
        double dy = std::fabs(c.sizeRatioY - 1.0);
        double worst = (dx > dy) ? dx : dy;
        c.sizeScore = (1.0 - Clamp01(worst / kSizeTolerance)) * 100.0;

        c.totalScore = c.shapeScore * kWeightShape
                     + c.posScore   * kWeightPos
                     + c.sizeScore  * kWeightSize;

        sum += c.totalScore;
        ++n;
    }

    result.scoredCells = n;
    result.totalScore = (n > 0) ? sum / n : 0.0;
}

const wchar_t* EvaluationScore::GradeLabel(double score) {
    if (score >= 90.0) return L"とても良い";
    if (score >= 75.0) return L"良い";
    if (score >= 60.0) return L"もう少し";
    if (score >= 40.0) return L"練習しよう";
    return L"崩れています";
}
