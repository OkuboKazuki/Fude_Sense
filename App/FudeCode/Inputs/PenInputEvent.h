#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>

// デバイス非依存の正規化ペン入力イベント構造体
// どのような液タブ・ペンタブ（Wintab, Windows Ink, Pointer API等）でも共通のデータ表現
struct PenInputEvent {
    int x = 0;                     // クライアント領域基準の X 座標 (px)
    int y = 0;                     // クライアント領域基準の Y 座標 (px)
    int z = 0;                     // ペン先とタブレット表面の距離/ホバーZ (0: 接触)
    double pressure = 0.0;         // 正規化筆圧 (0.0: 無圧/非接触 ~ 1.0: 最大筆圧)
    double altitudeDegrees = 90.0; // 筆先高度角 (0.0: 水平 ~ 90.0: 垂直)
    double azimuthRad = 0.0;       // 筆先方位角 (0 ~ 2π ラジアン)
    bool inRange = false;          // タブレット検出範囲内フラグ
    bool isEraser = false;         // 消しゴム側かペン先側か
    DWORD time = 0;                // タイムスタンプ (ms)
};
