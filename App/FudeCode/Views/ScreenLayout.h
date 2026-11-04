#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "AppState.h"

// 画面のレイアウト計算。座標を一か所で求め、描画とクリック判定で同じ値を使う。
// 書き換えるのは UIState だけで、ほかのモデルは読むだけにする。
class ScreenLayout {
public:
    // ウィンドウサイズに応じた全UI要素のレイアウト計算
    static void Layout(UIState& ui, const PaperModel& paper, bool viewingImport, const ReplayModel& replay, int clientWidth, int clientHeight);

    // シークバーのツマミ位置だけを再計算する
    static void UpdateReplaySeekThumb(UIState& ui, const ReplayModel& replay);
};
