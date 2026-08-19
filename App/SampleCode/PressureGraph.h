#pragma once
#include <windows.h>

// グラフウィンドウを作成
bool CreatePressureGraph(HINSTANCE hInstance);

// グラフウィンドウを閉じる
void DestroyPressureGraph();

// 筆圧データを追加
void AddPressureData(int pressure);

// グラフを更新
void RefreshPressureGraph();

// 最大筆圧を設定
void SetMaxPressure(int maxPressure);