#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <string>
#include "GpuInk.h"

// 作品の書き出しの結果。表示するメッセージは呼び出し側で決める。
enum class ImageExportResult {
    NothingToExport,   // 半紙の大きさが 0 で、書き出すものが無い
    Failed,
    CopiedToClipboard,
    SavedPng,
    SavedBmp           // PNG の保存に失敗し、BMP で保存した
};

class ImageExporter {
public:
    // キャンバス作品をクリップボードまたはデスクトップの画像ファイルへ保存
    // rPaper: 半紙の矩形。書き出す画像の大きさに使う
    static ImageExportResult ExportCanvas(HWND hWnd, GpuInk& gpuInk, const RECT& rPaper, bool toClipboard);
};
