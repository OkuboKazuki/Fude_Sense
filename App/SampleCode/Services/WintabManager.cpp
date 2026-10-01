#include "stdafx.h"
#include "WintabManager.h"
#include <cstdio>

bool WintabManager::OpenContexts(HWND hWnd) {
    if (!LoadWintab()) {
        OutputDebugStringA("Wintab not available\n");
        return false;
    }

    if (!gpWTInfoA(0, 0, NULL)) {
        OutputDebugStringA("WinTab Services Not Available.\n");
        return false;
    }

    int ctxIndex = 0;
    int gnOpenContexts = 0;
    int gnAttachedDevices = 0;

    CloseContexts();

    gpWTInfoA(WTI_INTERFACE, IFC_NDEVICES, &gnAttachedDevices);

    do {
        int foundCtx = 0;
        LOGCONTEXTA lcMine = { 0 };
        AXIS tabletX = { 0 };
        AXIS tabletY = { 0 };
        AXIS Pressure = { 0 };
        AXIS axisZ = { 0 };

        if (m_openSystemContext) {
            foundCtx = gpWTInfoA(WTI_DEFSYSCTX, 0, &lcMine);
        } else {
            foundCtx = gpWTInfoA(WTI_DDCTXS + ctxIndex, 0, &lcMine);
        }

        if (foundCtx > 0) {
            UINT result = 0;
            gpWTInfoA(WTI_DEVICES + ctxIndex, DVC_HARDWARE, &result);
            bool displayTablet = (result & HWC_INTEGRATED) != 0;

            lcMine.lcPktData = PACKETDATA;
            lcMine.lcOptions |= CXO_MESSAGES | CXO_SYSTEM;
            lcMine.lcPktMode = PACKETMODE;
            lcMine.lcMoveMask = PACKETDATA;
            lcMine.lcBtnUpMask = lcMine.lcBtnDnMask;

            if (gpWTInfoA(WTI_DEVICES + ctxIndex, DVC_X, &tabletX) != sizeof(AXIS)) {
                ctxIndex++;
                if (m_openSystemContext) break;
                continue;
            }
            gpWTInfoA(WTI_DEVICES + ctxIndex, DVC_Y, &tabletY);
            gpWTInfoA(WTI_DEVICES + ctxIndex, DVC_NPRESSURE, &Pressure);
            gpWTInfoA(WTI_DEVICES + ctxIndex, DVC_Z, &axisZ);

            m_maxPressure = Pressure.axMax;
            lcMine.lcOutExtY = -lcMine.lcOutExtY;

            HCTX hCtx = gpWTOpenA(hWnd, (LPLOGCONTEXT)&lcMine, TRUE);
            if (hCtx) {
                TabletInfo info;
                info.maxPressure = Pressure.axMax;
                m_contextMap[hCtx] = info;
                gnOpenContexts++;
            }
        } else {
            break;
        }

        if (m_openSystemContext) break;
        ctxIndex++;
    } while (true);

    return gnOpenContexts > 0;
}

void WintabManager::CloseContexts() {
    for (auto& pair : m_contextMap) {
        if (pair.first != nullptr && gpWTClose) {
            gpWTClose(pair.first);
        }
    }
    m_contextMap.clear();
}

void WintabManager::Cleanup() {
    CloseContexts();
    UnloadWintab();
}
