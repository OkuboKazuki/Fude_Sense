#include "stdafx.h"
#include "WintabAdapter.h"
#include "WintabUtils.h"
#include <cmath>

bool WintabAdapter::ConvertPacket(HWND hWnd, WPARAM wParam, LPARAM lParam, WintabManager& wintab, PenInputEvent& outEvent) {
    HCTX hCtx = (HCTX)lParam;
    if (!wintab.HasContext(hCtx)) {
        hCtx = wintab.GetPrimaryContext();
    }

    PACKET pkt = { 0 };
    if (!gpWTPacket || !gpWTPacket(hCtx, static_cast<int>(wParam), &pkt)) {
        return false;
    }

    POINT pt = { pkt.pkX, pkt.pkY };
    if (wintab.IsSystemContext()) {
        ScreenToClient(hWnd, &pt);
    }

    double maxPrs = wintab.GetMaxPressure(hCtx);
    if (maxPrs <= 0.0) maxPrs = 1024.0;

    UINT rawPrs = pkt.pkNormalPressure;
    UINT minThreshold = static_cast<UINT>(maxPrs * 0.01);
    if (rawPrs <= minThreshold) {
        rawPrs = 0;
    }

    double normPressure = static_cast<double>(rawPrs) / maxPrs;
    if (normPressure > 1.0) normPressure = 1.0;
    if (normPressure < 0.0) normPressure = 0.0;

    outEvent.x = pt.x;
    outEvent.y = pt.y;
    outEvent.z = static_cast<int>(pkt.pkZ);
    outEvent.pressure = normPressure;
    outEvent.altitudeDegrees = static_cast<double>(pkt.pkOrientation.orAltitude) / 10.0;
    outEvent.azimuthRad = (static_cast<double>(pkt.pkOrientation.orAzimuth) / 10.0) * (3.14159265358979323846 / 180.0);
    outEvent.inRange = true;
    outEvent.isEraser = false;
    outEvent.time = pkt.pkTime;

    return true;
}
