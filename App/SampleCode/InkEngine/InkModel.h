#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <algorithm>
#include "AppEnums.h"

// 墨・硯・運筆インク管理モデル（InkEngine のデータ構造）
struct InkModel {
    double stoneAmount = INK_MAX_VALUE; // 硯の墨残量 (0.0 ～ 1.0)
    double brushAmount = INK_MAX_VALUE; // 筆に含まれる墨量 (0.0 ～ 1.0)

    // 硯で墨を補充 / 筆に墨を含ませる
    void Refill() {
        stoneAmount = INK_MAX_VALUE;
        brushAmount = INK_MAX_VALUE;
    }

    // ストローク運筆に伴うインク消費
    void Consume(double amount) {
        brushAmount = (std::max)(0.0, brushAmount - amount);
        stoneAmount = (std::max)(0.0, stoneAmount - amount * 0.5);
    }

    void ConsumeInk(double amount) {
        Consume(amount);
    }

    // 筆先が使えるインク濃度係数 (0.0 ~ 1.0)
    double GetInkFactor() const {
        return (std::min)(stoneAmount, brushAmount);
    }
};
