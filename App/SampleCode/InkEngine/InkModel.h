#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <algorithm>
#include "AppEnums.h"

// 水分の消費速度（顔料比）。水分は紙への毛管吸収と蒸発があるため顔料より速く失われる。
// これにより、水分が先に抜けても顔料が残り「濃墨・渇筆（濃いままかすれる）」が表現できる。
constexpr double WATER_LOSS_RATIO = 1.35;

// 墨・硯・運筆インク管理モデル（InkEngine のデータ構造）
//
// 顔料（brushAmount）と水分（brushWater）を独立した物理量として扱い、
// 硯の残量表示と筆の有効水分残量を同期させて直感的な操作感と物理挙動を両立させる。
struct InkModel {
    double stoneAmount = INK_MAX_VALUE; // 硯・筆の実効墨残量 (0.0 ～ 1.0)
    double brushAmount = INK_MAX_VALUE; // 筆に含まれる顔料濃度 (0.0 ～ 1.0)
    double brushWater = INK_MAX_VALUE;  // 筆に含まれる水分量 (0.0 ～ 1.0)

    // 硯で墨を補充 / 筆に墨を含ませる
    void Refill() {
        stoneAmount = INK_MAX_VALUE;
        brushAmount = INK_MAX_VALUE;
        brushWater = INK_MAX_VALUE;
    }

    // ストローク運筆に伴う物理インク消費
    void Consume(double amount) {
        brushAmount = (std::max)(0.0, brushAmount - amount);
        brushWater  = (std::max)(0.0, brushWater - amount * WATER_LOSS_RATIO);
        // 画面の残量表示（硯の液面）は、筆の有効残量（水分ベース）と一致させる
        stoneAmount = brushWater;
    }

    void ConsumeInk(double amount) {
        Consume(amount);
    }

    // 筆先が使えるインク濃度係数 (0.0 ~ 1.0)
    double GetInkFactor() const {
        return (std::min)(stoneAmount, brushAmount);
    }

    // 筆の乾き具合 (0.0: 潤沢 ~ 1.0: 渇筆)
    // 墨残量が30%以上ある間はかすれず潤沢な墨汁で書け、
    // 残量が30%を切ってから徐々にかすれが出始め、0%に向かって自然な渇筆となる。
    double GetDryness() const {
        if (brushWater >= 0.30) {
            return 0.0;
        }
        return (0.30 - brushWater) / 0.30;
    }
};
