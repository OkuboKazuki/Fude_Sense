#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <cmath>
#include "PenInputEvent.h"

// Windows 8 / 10 / 11 Pointer API (WM_POINTER*) を PenInputEvent に変換するアダプタ
class WindowsPointerAdapter {
public:
    static bool ConvertPointer(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam, PenInputEvent& outEvent) {
        UINT32 pointerId = GET_POINTERID_WPARAM(wParam);
        POINTER_INPUT_TYPE pointerType = PT_POINTER;

        if (!GetPointerType(pointerId, &pointerType)) {
            return false;
        }

        // ペン入力（デジタイザーペン）のみを処理（タッチや通常マウスは除外）
        if (pointerType != PT_PEN) {
            return false;
        }

        POINTER_PEN_INFO penInfo = {};
        if (!GetPointerPenInfo(pointerId, &penInfo)) {
            return false;
        }

        POINT pt = penInfo.pointerInfo.ptPixelLocation;
        ScreenToClient(hWnd, &pt);

        outEvent.x = pt.x;
        outEvent.y = pt.y;
        outEvent.z = (penInfo.pointerInfo.pointerFlags & POINTER_FLAG_INCONTACT) ? 0 : 1;

        // 正規化筆圧 (Windows Pointer API の筆圧範囲は 0 〜 1024)
        double normPressure = static_cast<double>(penInfo.pressure) / 1024.0;
        if (normPressure > 1.0) normPressure = 1.0;
        if (normPressure < 0.0) normPressure = 0.0;
        outEvent.pressure = (penInfo.pointerInfo.pointerFlags & POINTER_FLAG_INCONTACT) ? normPressure : 0.0;

        // tiltX, tiltY (-90度 〜 +90度) から仰角 (altitude) と方位角 (azimuth) を算出
        double tx = static_cast<double>(penInfo.tiltX);
        double ty = static_cast<double>(penInfo.tiltY);
        double txRad = tx * (3.14159265358979323846 / 180.0);
        double tyRad = ty * (3.14159265358979323846 / 180.0);
        double tanX = std::tan(txRad);
        double tanY = std::tan(tyRad);
        double r = std::hypot(tanX, tanY);
        double altRad = std::atan2(1.0, r);
        double altitudeDeg = altRad * (180.0 / 3.14159265358979323846);
        double azimuthRad = std::atan2(tanX, -ty != 0.0 ? -tanY : 0.0001);
        if (azimuthRad < 0.0) azimuthRad += 2.0 * 3.14159265358979323846;

        outEvent.altitudeDegrees = altitudeDeg;
        outEvent.azimuthRad = azimuthRad;
        outEvent.inRange = (penInfo.pointerInfo.pointerFlags & POINTER_FLAG_INRANGE) != 0;
        outEvent.isEraser = (penInfo.penFlags & PEN_FLAG_ERASER) != 0;
        outEvent.time = penInfo.pointerInfo.dwTime;

        return true;
    }
};
