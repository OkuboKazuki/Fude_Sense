#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <algorithm>
#include <cmath>
#include "AppEnums.h"

// 水分の消費速度（顔料比）。水分は紙への毛管吸収と蒸発があるため顔料より速く失われる。
// これにより、水分が先に抜けても顔料が残り「濃墨・渇筆（濃いままかすれる）」が表現できる。
constexpr double WATER_LOSS_RATIO = 1.35;

// ペンで硯を1回押したときに足される墨の量。残量に関係なく筆圧だけで決まる。
// 軽く触れただけで REFILL_MIN_AMOUNT、REFILL_FULL_PRESSURE 以上押し込むと
// 空から満タンになる量（最大筆圧は出しにくいので手前で最大にする）。
constexpr double REFILL_MIN_AMOUNT = 0.10;
constexpr double REFILL_FULL_PRESSURE = 0.8;

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

    // 押し始めの残量。1回押す間の補充は、ここへ筆圧に応じた量を足した値になる。
    double refillBaseAmount = 0.0;
    double refillBaseWater = 0.0;

    // 筆圧から足す墨の量を決める。弱く押すと少なく、強く押すほど多く足す。
    static double RefillAmountFromPressure(double pressure) {
        double p = (std::min)((std::max)(pressure / REFILL_FULL_PRESSURE, 0.0), 1.0);
        return REFILL_MIN_AMOUNT + (INK_MAX_VALUE - REFILL_MIN_AMOUNT) * p;
    }

    // ペンで押し始めたときに呼ぶ。今の残量を足し算の基準にする。
    void BeginPressRefill() {
        refillBaseAmount = brushAmount;
        refillBaseWater = brushWater;
    }

    // 押している間の筆圧で墨を足す。増え方は残量によらず筆圧だけで決まる。
    // 押している間に筆圧が上がればその分だけ継ぎ足し、弱めても減らさない。
    void PressRefill(double pressure) {
        double add = RefillAmountFromPressure(pressure);
        brushAmount = (std::max)(brushAmount, (std::min)(refillBaseAmount + add, INK_MAX_VALUE));
        brushWater = (std::max)(brushWater, (std::min)(refillBaseWater + add, INK_MAX_VALUE));
        stoneAmount = brushWater;
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
    // 残量45%までは完全に潤沢（かすれなし）を維持し、
    // 45%を切ってからのかすれをより穏やかな非線形カーブで立ち上げることで、
    // 味わい深いかすれ（渇筆）が長いストロークにわたって持続するようにする。
    double GetDryness() const {
        if (brushWater >= 0.45) {
            return 0.0;
        }
        if (brushWater <= 0.0) {
            return 1.0;
        }
        double norm = (0.45 - brushWater) / 0.45;
        return std::pow(norm, 1.65);
    }
};
