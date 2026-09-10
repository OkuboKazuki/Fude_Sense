#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <vector>
#include <string>

// ---------------------------------------------------------------------------
// お手本を使わずに、書きぶり（運筆）だけを測った結果の型を置く。
//
// お手本との比較（EvaluationResult.h）とは別系統の評価。置いた字が何であろうと、
// お手本が1つも無くても出せる。中身は StrokePoint に記録済みの
// 筆圧・高度角・速度・線幅・時刻だけから作る。
//
// 計算は Brushwork.h が行い、AppState がこの結果を持ち、View が描く。
// 型をここへ分けているのは EvaluationResult.h と同じ理由（循環の回避）。
// ---------------------------------------------------------------------------

// 運筆の評価軸。点数の内訳と、後段（ずれの言語化）の入口を兼ねる。
enum class WorkAxis {
    Tilt,     // 筆の立て方（高度角）
    Speed,    // 速さの安定
    Swing,    // 抑揚（筆圧・線幅の変化）
    Tempo     // テンポ（画の所要時間と画間）
};
constexpr int WORK_AXIS_COUNT = 4;

inline const wchar_t* WorkAxisName(WorkAxis axis) {
    switch (axis) {
    case WorkAxis::Tilt:  return L"筆の立て方";
    case WorkAxis::Speed: return L"速さの安定";
    case WorkAxis::Swing: return L"抑揚";
    case WorkAxis::Tempo: return L"テンポ";
    }
    return L"";
}

// 1本の軸ぶんの結果。測れなかった軸は valid=false になり、
// 平均にも総合点にも混ざらない（測れない軸で点が下がらないようにする）。
struct WorkAxisScore {
    bool valid = false;
    double score = 0.0; // 0〜100
};

// 1画ぶんの書きぶり
struct StrokeWork {
    int strokeId = 0;
    int pointCount = 0;
    double durationMs = 0.0;    // 画の所要時間（パケット時刻の差）
    double gapBeforeMs = -1.0;  // 直前の画を離してからこの画を始めるまで。測れないときは負
    double pathLengthPx = 0.0;  // 筆が通った道のりの長さ

    // --- 筆の立て方 -----------------------------------------------------
    // 傾きを返さないタブレットでは高度角が常に 0 で届くので、その場合は
    // tiltValid=false とし、寝かせすぎとは見なさない。
    bool tiltValid = false;
    double altitudeMeanDeg = 0.0;  // 高度角の平均（90°=垂直）
    double altitudeMinDeg = 0.0;   // 最も寝かせたところ
    double lyingRatio = 0.0;       // しきい値より寝ていた点の割合
    double lyingMs = 0.0;          // その区間の長さ

    // --- 速さ -----------------------------------------------------------
    bool speedValid = false;
    double speedMean = 0.0;        // 送筆の平均速度 (px/s)
    double speedCv = 0.0;          // 速度のばらつき（標準偏差 / 平均）

    // --- 抑揚 -----------------------------------------------------------
    bool swingValid = false;
    double pressureMean = 0.0;
    double pressureSwing = 0.0;    // 筆圧の抑揚 (上位 - 下位) / 上位
    double widthMinPx = 0.0;
    double widthMaxPx = 0.0;
    double widthSwing = 0.0;       // 線幅の変化 (max - min) / max

    // --- 点数 -----------------------------------------------------------
    WorkAxisScore axis[WORK_AXIS_COUNT];
    bool scored = false;
    double totalScore = 0.0;       // 測れた軸だけの重み付き平均
};

struct BrushworkResult {
    bool valid = false;
    std::wstring message;          // 測れなかった理由
    std::vector<StrokeWork> strokes;

    int strokeCount = 0;
    int scoredStrokes = 0;

    // 全体。点の多い画に引っ張られないよう、画ごとの値の平均を採る。
    bool tiltValid = false;
    double altitudeMeanDeg = 0.0;
    double lyingRatio = 0.0;
    double speedMean = 0.0;
    double speedCv = 0.0;
    double pressureSwing = 0.0;
    double widthSwing = 0.0;
    double avgDurationMs = 0.0;
    double avgGapMs = -1.0;        // 負なら測れていない（1画しかない等）

    WorkAxisScore axis[WORK_AXIS_COUNT];
    double totalScore = 0.0;

    void Clear() { *this = BrushworkResult(); }
};
