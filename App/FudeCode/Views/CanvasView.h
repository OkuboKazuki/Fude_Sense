#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include "AppState.h"
#include "UIState.h"
#include "GpuInk.h"

class CanvasView {
public:
    // キャンバス・半紙関連の描画
    static void DrawBackground(HDC dc, const UIState& ui);
    static void DrawOtehon(HDC dc, const AppState& state, const UIState& ui);

    // 指定した矩形にお手本を1字、em ボックス基準で内接描画する
    // （画像エクスポートからも使う）
    // rotateCcw = true で字を左回りに90度倒して描く（紙だけ表示の半紙の向きに合わせる用）
    static void DrawOtehonGlyph(HDC dc, const RECT& cell, const std::wstring& text, double opacity,
                                OtehonFontStyle style, bool rotateCcw = false);

    // 書体がこの PC に入っているか。無い書体を選ぶと GDI が黙って別の書体へ
    // 置き換えるため、UI 側で選べないことを示すのに使う。
    static bool HasOtehonFont(HDC dc, OtehonFontStyle style);
    static void DrawGrid(HDC dc, const AppState& state, const UIState& ui);
    static void RenderInk(HDC dc, GpuInk& gpuInk, const UIState& ui);

    // 書き出す作品画像を合成する。半紙の背景に墨を重ねたビットマップを返す。
    // 破棄は呼び出し側で行う。作れなかったときは nullptr を返す。
    static HBITMAP ComposeArtwork(HWND hWnd, GpuInk& gpuInk, int width, int height);

    // リプレイ用半紙描画
    static void DrawReplayCanvas(HDC dc, GpuInk& gpuInk, const AppState& state, const UIState& ui);

    // リプレイ描画用のオフスクリーンレイヤを破棄する（終了時）
    static void ReleaseReplayCache();

    static void Draw3DBrushPose(HDC dc, const UIState& ui, const StrokePoint& pose, bool isPenDown);

private:
    static void DrawReplayTrajectory(HDC dc, const TrajectorySession& session, const AppState& state, const RECT& rPaper, int pw, int ph);
    static void DrawCross(HDC dc, int x, int y, int s);
};

