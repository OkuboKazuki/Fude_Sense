#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "AppState.h"

// 画面のレイアウト計算の窓口。Controller とエントリポイントはここを呼ぶ。
class ScreenLayout {
public:
    // ウィンドウサイズに応じた全UI要素のレイアウト計算
    static void Layout(AppState& state, int clientWidth, int clientHeight);

    // シークバーのツマミ位置だけを再計算する
    static void UpdateReplaySeekThumb(AppState& state);
};
