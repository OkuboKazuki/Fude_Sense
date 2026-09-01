#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include "AppState.h"

// 筆圧キャリブレーション専用の描画ビュー
class CalibrationView {
public:
    // キャリブレーション画面（計測中ガイダンスまたは結果モーダル）の描画
    static void Draw(HDC dc, int width, int height, const AppState& state);

private:
    // 計測中ステップのガイダンス・ターゲットガイド・リアルタイムゲージの描画
    static void DrawActiveStep(HDC dc, int width, int height, const AppState& state);

    // 解析結果・推奨硬さの提示モーダルダイアログの描画
    static void DrawResultModal(HDC dc, int width, int height, const AppState& state);
};
