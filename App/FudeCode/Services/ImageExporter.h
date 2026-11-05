#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <string>

// 作品の書き出しの結果。表示するメッセージは呼び出し側で決める。
enum class ImageExportResult {
    Failed,
    CopiedToClipboard,
    SavedPng,
    SavedBmp           // PNG の保存に失敗し、BMP で保存した
};

class ImageExporter {
public:
    // 作品画像をクリップボードへ渡す。
    // CopiedToClipboard を返したときは所有権がシステムへ移るので、呼び出し側で破棄してはならない。
    // Failed を返したときは、呼び出し側で破棄する。
    static ImageExportResult CopyToClipboard(HWND hWnd, HBITMAP hBmp);

    // 作品画像をデスクトップの画像ファイルへ保存する。hBmp は呼び出し側で破棄する。
    static ImageExportResult SaveToDesktop(HWND hWnd, HBITMAP hBmp, int width, int height);
};
