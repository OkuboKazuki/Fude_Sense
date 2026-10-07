#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <algorithm>
#include <cmath>
#include "AppEnums.h"

// ペンで硯を1回押したときに足される墨の量。筆圧だけで決まる。
// 軽く触れただけで REFILL_MIN_AMOUNT、REFILL_FULL_PRESSURE 以上押し込むと
// REFILL_MAX_AMOUNT（最大筆圧は出しにくいので手前で最大にする）。
// 空の筆は1回では満タンにならず、何度か押して含ませる。
constexpr double REFILL_MIN_AMOUNT = 0.05;
constexpr double REFILL_MAX_AMOUNT = 0.60;
constexpr double REFILL_FULL_PRESSURE = 0.8;

// 墨・硯・運筆インク管理モデル（単一パラメータ設計）
//
// 硯および筆の墨残量を単一の物理パラメータ `amount` (0.0 ～ 1.0) で一元管理し、
// そこから筆先の乾き具合（dryness）を求める。dryness は GpuInk へ渡され、
// 墨を置かない画素を決めることでかすれ（渇筆）になる。
// 実際に白い筋が見え始めるのは残量 25～30% あたり（GetDryness のコメント参照）。
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
        return REFILL_MIN_AMOUNT + (REFILL_MAX_AMOUNT - REFILL_MIN_AMOUNT) * p;
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
    //
    // KASURE_START_LEVEL は「dryness が 0 から上がり始める残量」であって、
    // 「目に見えてかすれ始める残量」ではない。残量と見た目の関係はおおよそ次のとおり
    // （幅 30px の横線を引き、にじみが落ち着いた後の白い画素の割合を試算した値。
    //   CPU 版・GPU 版どちらのにじみ計算でもほぼ同じになる）。
    //
    //   残量   dryness  墨を置かない画素  にじみ後も白く残る画素
    //   60%    0.00       0%              0%
    //   40%    0.16       1.6%            0%
    //   35%    0.24       4.7%            0%    ← 隙間はあるがにじみで埋まる
    //   30%    0.32      12%              0.3%
    //   25%    0.41      27%              6%    ← ここで初めてかすれが見える
    //   20%    0.51      49%             25%
    //   10%    0.74      91%             86%
    //
    // 見え始めが遅れる理由は次の 3 つ。
    //   1. 指数 1.65 のカーブは、60% を切った直後はほとんど増えない。
    //   2. GpuInk のかすれ判定に使うノイズ（毛束 + 紙目）は値が中央に集まり、
    //      0 付近の値がめったに出ない。dryness が小さいうちは隙間がごくわずかしかできない。
    //   3. 水分が多いうちは、にじみが隣の隙間を濡らして墨を流し込み、小さな隙間を埋める。
    // 解析パネルの墨ゲージは、この値を下回ると色が変わる。
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
