#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <vector>
#include "BrushworkResult.h"
#include "TrajectoryModel.h"

// ---------------------------------------------------------------------------
// お手本を使わずに書きぶりだけを測る。
//
// お手本との比較（ShapeCompare）は「どこに墨が乗ったか」しか見ないので、
// 同じ形でも走り書きと丁寧な運筆の区別がつかない。ここはその逆で、
// 形はいっさい見ず、筆の持ち方・速さ・抑揚・テンポだけを見る。
// お手本が1枚も置かれていなくても出せるので、練習の入口に使える。
//
// 素材は TrajectorySession に溜まっている StrokePoint だけ。
// 画素は読まないので軽く、1画書き終えるたびに測り直してよい。
//
// ★ 点数のしきい値は Brushwork.cpp の先頭に集約した暫定値。
//    実データを見て直すこと（Step 4 の較正でまとめて触る）。
// ---------------------------------------------------------------------------

class Brushwork {
public:
    // 記録済みの全画を測る。書いた画が無ければ valid=false と理由を返す。
    static BrushworkResult Measure(const TrajectorySession& session);

    // 点数に対応する短い評語。字形側（EvaluationScore::GradeLabel）と段を揃える。
    static const wchar_t* GradeLabel(double score);

    // 寝かせすぎと見なす高度角の境目。View の説明文でも使う。
    static double LyingAltitudeDeg();

private:
    // 1画ぶん。画の区切りは呼び出し側が渡す（画間は前の画との関係で決まる）。
    static StrokeWork MeasureStroke(const StrokeData& stroke, double gapBeforeMs);
};
