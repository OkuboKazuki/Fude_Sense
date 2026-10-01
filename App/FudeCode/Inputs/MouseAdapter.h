#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include "PenInputEvent.h"

class MouseAdapter {
public:
    // マウスメッセージ（WM_LBUTTONDOWN, WM_MOUSEMOVE, WM_LBUTTONUP）から PenInputEvent を生成
    static PenInputEvent CreatePenEvent(POINT pt, bool isDown, DWORD time = 0) {
        PenInputEvent event;
        event.x = pt.x;
        event.y = pt.y;
        event.z = 0;
        event.pressure = isDown ? 0.5 : 0.0; // マウス押下時は標準筆圧 0.5
        event.altitudeDegrees = 90.0;        // 垂直
        event.azimuthRad = 0.0;
        event.inRange = isDown;
        event.isEraser = false;
        event.time = (time != 0) ? time : GetTickCount();
        return event;
    }
};
