#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <map>
#include <string>
#include "WintabUtils.h"

#define PACKETDATA	(PK_X | PK_Y | PK_Z | PK_BUTTONS | PK_NORMAL_PRESSURE | PK_TANGENT_PRESSURE | PK_TIME | PK_ORIENTATION)
#define PACKETMODE	PK_BUTTONS
#include "pktdef.h"

struct TabletInfo {
    int maxPressure = 1024;
    COLORREF penColor = RGB(0, 0, 0);
    char name[32] = { 0 };
    LONG tabletXExt = 0;
    LONG tabletYExt = 0;
    bool displayTablet = false;
    int maxZ = 0;
};

class WintabManager {
public:
    WintabManager() = default;
    ~WintabManager() { CloseContexts(); }

    bool OpenContexts(HWND hWnd);
    void CloseContexts();
    void Cleanup();

    bool HasContext(HCTX hCtx) const {
        return m_contextMap.find(hCtx) != m_contextMap.end();
    }

    HCTX GetPrimaryContext() const {
        if (!m_contextMap.empty()) {
            return m_contextMap.begin()->first;
        }
        return nullptr;
    }

    double GetMaxPressure(HCTX hCtx) const {
        auto it = m_contextMap.find(hCtx);
        if (it != m_contextMap.end() && it->second.maxPressure > 0) {
            return static_cast<double>(it->second.maxPressure);
        }
        return m_maxPressure > 0 ? static_cast<double>(m_maxPressure) : 1024.0;
    }

    bool IsSystemContext() const { return m_openSystemContext; }
    void SetSystemContext(bool openSystem) { m_openSystemContext = openSystem; }

    const std::map<HCTX, TabletInfo>& GetContextMap() const { return m_contextMap; }

private:
    std::map<HCTX, TabletInfo> m_contextMap;
    bool m_openSystemContext = true;
    int m_maxPressure = 1024;
};
