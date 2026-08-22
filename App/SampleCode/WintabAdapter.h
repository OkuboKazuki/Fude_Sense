#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include "PenInputEvent.h"
#include "WintabManager.h"

class WintabAdapter {
public:
    // Wintab の WT_PACKET メッセージから PenInputEvent を生成・正規化
    static bool ConvertPacket(HWND hWnd, WPARAM wParam, LPARAM lParam, WintabManager& wintab, PenInputEvent& outEvent);
};
