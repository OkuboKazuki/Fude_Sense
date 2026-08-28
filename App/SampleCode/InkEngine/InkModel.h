#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <algorithm>
#include "AppEnums.h"

// 水分の消費速度（顔料比）。水分は紙への吸収と蒸発があるため顔料より速く失われる
constexpr double WATER_LOSS_RATIO = 1.5;

// 墨・硯・運筆インク管理モデル（InkEngine のデータ構造）
//
// 顔料（brushAmount）と水分（brushWater）は独立した物理量として扱う。
// 濃さは顔料が、にじみ・かすれの発生は水分が支配するため、両者を
// 1 つの値で兼ねると「濃墨・渇筆（濃くてかすれ、にじまない）」のような
// 状態が表現できなくなる。
struct InkModel {
    double stoneAmount = INK_MAX_VALUE; // 硯の墨残量 (0.0 ～ 1.0)
    double brushAmount = INK_MAX_VALUE; // 筆に含まれる墨量＝顔料 (0.0 ～ 1.0)
    double brushWater = INK_MAX_VALUE;  // 筆に含まれる水分 (0.0 ～ 1.0)

    // 硯で墨を補充 / 筆に墨を含ませる
    void Refill() {
        stoneAmount = INK_MAX_VALUE;
        brushAmount = INK_MAX_VALUE;
        brushWater = INK_MAX_VALUE;
    }

    // ストローク運筆に伴うインク消費
    void Consume(double amount) {
        brushAmount = (std::max)(0.0, brushAmount - amount);
        stoneAmount = (std::max)(0.0, stoneAmount - amount * 0.5);
        brushWater = (std::max)(0.0, brushWater - amount * WATER_LOSS_RATIO);
    }

    void ConsumeInk(double amount) {
        Consume(amount);
    }

    // 筆先が使えるインク濃度係数 (0.0 ~ 1.0)
    double GetInkFactor() const {
        return (std::min)(stoneAmount, brushAmount);
    }

    // 筆の乾き具合 (0.0: 潤沢 ~ 1.0: 渇筆)
    // かすれの発生度合いとにじみの抑制を支配する
    double GetDryness() const {
        return (std::max)(0.0, (std::min)(1.0, INK_MAX_VALUE - brushWater));
    }
};
