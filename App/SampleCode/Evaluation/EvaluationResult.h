#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <vector>
#include <string>

// ---------------------------------------------------------------------------
// お手本と墨を比べた結果の型だけを置く。
//
// AppState がこの結果を持ち、Evaluation が計算し、View が描く。
// 計算側（ShapeCompare.h）は AppState.h を必要とするので、型まで一緒に
// 置くと AppState.h -> ShapeCompare.h -> AppState.h で循環する。
// 型をここへ分けることでその輪を切っている。
// ---------------------------------------------------------------------------

// 二値マスク。お手本・墨の双方をこの形に落としてから比べる。
struct ShapeMask {
    int width = 0;
    int height = 0;
    std::vector<unsigned char> bits; // 1 = 図形あり

    bool IsEmpty() const { return width <= 0 || height <= 0 || bits.empty(); }

    unsigned char At(int x, int y) const {
        if (x < 0 || y < 0 || x >= width || y >= height) return 0;
        return bits[(size_t)y * width + x];
    }

    int Count() const;
    // 図形の外接矩形。図形が無ければ false。
    bool GetBoundingBox(RECT& out) const;
    // 図形の重心（ピクセル座標）。図形が無ければ false。
    bool GetCentroid(double& outX, double& outY) const;
};

// 2枚のマスクの一致具合。分母はすべてお手本の面積。
struct ShapeMetrics {
    bool valid = false;
    double iou = 0.0;      // 重なり率 (共通部分 / 和集合)
    double overflow = 0.0; // はみ出し率: お手本の外へ出た墨 / お手本の面積
    double missing = 0.0;  // 欠け率: 墨が乗らなかったお手本 / お手本の面積
    int otehonPixels = 0;
    int inkPixels = 0;
    int interPixels = 0;
};

// 1マスぶんの比較結果
struct CellCompare {
    int cellIndex = -1;
    std::wstring text;
    bool hasOtehon = false;
    bool hasInk = false;

    // 升目そのものを基準にした比較。位置・大きさのずれも一致率に含まれる。
    ShapeMetrics grid;
    // 双方の外接矩形を合わせてから比べたもの。形だけを見る。
    ShapeMetrics shape;

    // 後段（ずれの言語化）で使う素材。上の計算のついでに取れる。
    double centroidDx = 0.0;  // 重心のずれ。升目の幅・高さに対する比。右/下が正
    double centroidDy = 0.0;
    double sizeRatioX = 1.0;  // 外接矩形の大きさの比（墨 / お手本）
    double sizeRatioY = 1.0;

    // --- 点数（0〜100）。EvaluationScore が上の指標から写像する ---
    double shapeScore = 0.0; // 形が合っているか（外接矩形を合わせた一致率から）
    double posScore = 0.0;   // 升目の中の位置（重心のずれから）
    double sizeScore = 0.0;  // 大きさ（外接矩形の比から）
    double totalScore = 0.0; // 上記の重み付き合計

    // 半紙上の確認表示が使う。描画のたびに作り直すとお手本の
    // ラスタライズが走って重いので、比較時のものを持ち回す。
    ShapeMask otehonMask;
    ShapeMask inkMask;
};

struct CompareResult {
    bool valid = false;
    std::wstring message;           // 評価できなかった理由
    std::vector<CellCompare> cells; // お手本が置かれているマスのみ

    // 書いた字の総合点（0〜100）。書かれているマスの平均。
    double totalScore = 0.0;
    int scoredCells = 0;

    void Clear() {
        valid = false;
        message.clear();
        cells.clear();
        totalScore = 0.0;
        scoredCells = 0;
    }

    // 升目基準の一致率の平均。まだ点数ではなく、素の平均。
    double AverageGridIoU() const;
    double AverageShapeIoU() const;
};
