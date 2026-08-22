#include "stdafx.h"
#include "StrokeController.h"
#include "RenderUtils.h"
#include <cmath>
#include <algorithm>

StrokeController::StrokeController() {
}

void StrokeController::ResetStroke(GpuInk& gpuInk) {
    m_strokeActive = false;
    m_smoothedPressure = 0.0;
    m_smoothedWidth = 0.0;
    if (gpuInk.IsInStroke()) {
        gpuInk.EndStroke();
    }
}

void StrokeController::ProcessPenEvent(HWND hWnd, const PenInputEvent& event, AppState& state, GpuInk& gpuInk) {
    // ペンのZ高度・傾き・方位角情報をGPU墨汁エンジンへ通知
    gpuInk.UpdatePenZ(event.z,
        static_cast<int>(event.altitudeDegrees),
        static_cast<int>(event.azimuthRad * (180.0 / 3.14159265358979323846)),
        event.pressure <= 0.0);

    // 筆圧のスムージング（急激な変化を抑制して滑らかな筆運びにする）
    double rawPrs = event.pressure;
    if (rawPrs > 0.0) {
        if (!m_strokeActive || m_smoothedPressure <= 0.0 || !gpuInk.IsInStroke()) {
            m_smoothedPressure = rawPrs;
        } else {
            double alphaPrs = (rawPrs < m_smoothedPressure) ? 0.85 : 0.35;
            m_smoothedPressure = m_smoothedPressure * (1.0 - alphaPrs) + rawPrs * alphaPrs;
        }
    } else {
        m_smoothedPressure = 0.0;
    }

    POINT clientPt = { event.x, event.y };
    const RECT& rPaper = state.ui.rPaper;
    const RECT& rSub = state.ui.rSub;

    // 半紙領域内かつモーダルやサブパネルに遮られていないか判定
    bool canDrawInk = !state.ui.showClearConfirm
        && PtInRect(&rPaper, clientPt)
        && !(state.ui.isSubPanelOpen && PtInRect(&rSub, clientPt));

    if (m_smoothedPressure > 0.0 && canDrawInk) {
        POINT paperPt = { clientPt.x - rPaper.left, clientPt.y - rPaper.top };
        POINT oldPaperPt = { m_ptOld.x - rPaper.left, m_ptOld.y - rPaper.top };

        if (!m_strokeActive || !gpuInk.IsInStroke()) {
            oldPaperPt = paperPt;
        }

        double dx = static_cast<double>(paperPt.x - oldPaperPt.x);
        double dy = static_cast<double>(paperPt.y - oldPaperPt.y);
        double dist = std::sqrt(dx * dx + dy * dy);

        if (dist > 300.0) {
            oldPaperPt = paperPt;
            dist = 0.0;
        }

        // 筆の硬さ（感度補正）を筆圧に適用
        double hardness = state.brush.hardness;
        if (std::isnan(hardness) || hardness < 0.1) hardness = 1.0;
        double pressureFactor = std::pow(m_smoothedPressure, hardness);
        if (std::isnan(pressureFactor) || std::isinf(pressureFactor)) pressureFactor = 0.5;

        gpuInk.SetPressureFactor(pressureFactor);

        // 傾き・方位角・運筆速度による筆幅と止め・払いの物理計算
        double tiltFactor = (90.0 - event.altitudeDegrees) / 90.0;
        if (tiltFactor < 0.0) tiltFactor = 0.0;

        double moveAngle = (dist > 1e-5) ? std::atan2(dy, dx) : 0.0;
        double tomeFactor = 1.0 + 0.1 * (1.0 - (std::min)(dist / 3.0, 1.0)) * std::pow(pressureFactor, 0.8);
        double haraiPower = 1.3 + 0.4 * (std::min)(dist, 10.0);
        double haraiFactor = std::pow(pressureFactor, haraiPower);
        double angleDiff = std::sin(event.azimuthRad - (moveAngle + 1.57079632679));
        double angleFactor = 1.0 + 0.3 * std::abs(angleDiff);

        double baseMaxWidth = state.brush.GetBaseMaxWidth();
        double rawWidth = baseMaxWidth * haraiFactor * tomeFactor * angleFactor * (1.0 + tiltFactor * 0.6);

        double startWidth = m_smoothedWidth;
        if (dist == 0.0 || !gpuInk.IsInStroke() || !m_strokeActive) {
            m_smoothedWidth = rawWidth;
            startWidth = rawWidth;
            m_strokeActive = true;
        } else {
            double alphaWidth = (rawWidth < m_smoothedWidth || dist > 3.0) ? 0.8 : 0.3;
            m_smoothedWidth = m_smoothedWidth * (1.0 - alphaWidth) + rawWidth * alphaWidth;
        }

        // インクの消費とカスレの物理連動
        // 筆の保水量（brushAmount）に応じて墨の濃度（alpha: 0~255）を算出
        double inkConsumeAmount = (dist + 0.5) * pressureFactor * (baseMaxWidth / 36.0) * 0.00015;
        state.ink.ConsumeInk(inkConsumeAmount);

        // 筆の墨が少なくなるとカスレが発生
        BYTE inkAlpha = 255;
        if (state.ink.brushAmount < 0.3) {
            double ratio = state.ink.brushAmount / 0.3; // 0.0 ~ 1.0
            inkAlpha = static_cast<BYTE>(std::max(10.0, 255.0 * std::pow(ratio, 0.7)));
        }

        if (state.ink.brushAmount > 0.001) {
            gpuInk.DrawSegmentLinear(oldPaperPt, paperPt, startWidth, m_smoothedWidth, inkAlpha);
        }
    } else {
        ResetStroke(gpuInk);
    }

    m_ptOld = clientPt;
    InvalidateRect(hWnd, NULL, FALSE);
}
