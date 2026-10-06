#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <algorithm>
#include <cmath>
#include "AppEnums.h"

// ペンで硯を1回押したときに足される墨の量。筆圧だけで決まる。
// 軽く触れただけで REFILL_MIN_AMOUNT、REFILL_FULL_PRESSURE 以上押し込むと
// 空から満タンになる量（最大筆圧は出しにくいので手前で最大にする）。
constexpr double REFILL_MIN_AMOUNT = 0.10;
constexpr double REFILL_FULL_PRESSURE = 0.8;

// 墨・硯・運筆インク管理モデル（単一パラメータ設計）
//
// 硯および筆の墨残量を単一の物理パラメータ `amount` (0.0 ～ 1.0) で一元管理し、
// 残量が一定水準（KASURE_START_LEVEL = 0.60）を下回った際に、
// 非線形カーブによって筆先の渇き度合い（dryness）を算出してかすれ（渇筆）を発生させる。
struct InkModel {
    double amount = INK_MAX_VALUE; // 墨残量 (0.0 ～ 1.0)

    // 硯で墨を満タンに補充
    void Refill() {
        amount = INK_MAX_VALUE;
    }

    // 押し始めの残量。1回押す間の補充は、ここへ筆圧に応じた量を足した値になる。
    double refillBaseAmount = 0.0;

    // 筆圧から足す墨の量を決める。弱く押すと少なく、強く押すほど多く足す。
    static double RefillAmountFromPressure(double pressure) {
        double p = (std::min)((std::max)(pressure / REFILL_FULL_PRESSURE, 0.0), 1.0);
        return REFILL_MIN_AMOUNT + (INK_MAX_VALUE - REFILL_MIN_AMOUNT) * p;
    }

    // ペンで押し始めたときに呼ぶ。今の残量を足し算の基準にする。
    void BeginPressRefill() {
        refillBaseAmount = amount;
    }

    // 押している間の筆圧で墨を足す。増え方は残量によらず筆圧だけで決まる。
    // 押している間に筆圧が上がればその分だけ継ぎ足し、弱めても減らさない。
    void PressRefill(double pressure) {
        double add = RefillAmountFromPressure(pressure);
        amount = (std::max)(amount, (std::min)(refillBaseAmount + add, INK_MAX_VALUE));
    }

    // ストローク運筆に伴う物理インク消費
    void Consume(double delta) {
        amount = (std::max)(0.0, amount - delta);
    }

    // 筆の乾き具合 (0.0: 潤沢 ~ 1.0: 渇筆)
    // 残量 60% までは完全に潤沢（かすれなし）を維持し、
    // それを切ってからのかすれを穏やかな非線形カーブで立ち上げることで、
    // 味わい深いかすれ（渇筆）が長いストロークにわたって持続するようにする。
    static constexpr double KASURE_START_LEVEL = 0.60;
    double GetDryness() const {
        if (amount >= KASURE_START_LEVEL) {
            return 0.0;
        }
        if (amount <= 0.0) {
            return 1.0;
        }
        double norm = (KASURE_START_LEVEL - amount) / KASURE_START_LEVEL;
        return std::pow(norm, 1.65);
    }
};
