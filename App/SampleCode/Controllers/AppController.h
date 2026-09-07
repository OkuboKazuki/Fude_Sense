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
    static bool OnLButtonDown(HWND hWnd, POINT pt, AppState& state, GpuInk& gpuInk);
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

    // 半紙だけ表示（集中モード）の出入りと、半紙の向き（縦／横）の切り替え。
    // どちらも半紙の画素数が変わるので、墨の作り直しと控えの破棄までまとめて行う。
    static void SetPaperOnly(HWND hWnd, bool on, AppState& state, GpuInk& gpuInk);
    static void TogglePaperOrientation(HWND hWnd, AppState& state, GpuInk& gpuInk);
};
