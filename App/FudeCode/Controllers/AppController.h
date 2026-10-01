#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include "AppState.h"
#include "GpuInk.h"

class AppController {
public:
    // UIイベントハンドリング
    // penPressure: ペンで押したときの筆圧 (0.0 ~ 1.0)。マウスなど筆圧の無い入力は負値。
    // 硯・「墨を補充」はこの筆圧で補充量を変える。
    static bool OnLButtonDown(HWND hWnd, POINT pt, AppState& state, GpuInk& gpuInk, double penPressure = -1.0);
    static bool OnLButtonUp(HWND hWnd, POINT pt, AppState& state);
    static bool OnMouseMove(HWND hWnd, POINT pt, WPARAM wParam, AppState& state);
    // お手本の文字入力（IME 変換確定後の文字が WM_CHAR で届く）。
    // 入力中なら true を返し、呼び出し側は他のキー処理を行わない。
    static bool OnChar(HWND hWnd, wchar_t ch, AppState& state);
    static void OnSize(HWND hWnd, int width, int height, AppState& state, GpuInk& gpuInk);
    static void ClearAllInk(HWND hWnd, AppState& state, GpuInk& gpuInk);
    // 直前の1画を取り消す（墨・墨残量・運筆アーカイブをまとめて戻す）
    static bool UndoStroke(HWND hWnd, AppState& state, GpuInk& gpuInk);
    // 戻しすぎた1画を復元する
    static bool RedoStroke(HWND hWnd, AppState& state, GpuInk& gpuInk);
    // 記録が変わったときにリプレイのタイムラインを作り直す
    static void SyncReplayTimeline(HWND hWnd, AppState& state);

    // 紙だけ表示（横向きの半紙を画面いっぱいに出す）の出入り。
    // 半紙の縦横が入れ替わるので、書いた墨と運筆記録は90度回して引き継ぐ。
    // 「一画戻す」の控えだけは書き戻せないので消す。
    static void SetPaperOnly(HWND hWnd, bool on, AppState& state, GpuInk& gpuInk);
};
