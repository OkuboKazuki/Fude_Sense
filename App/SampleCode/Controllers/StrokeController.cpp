#include "stdafx.h"
#include "StrokeController.h"
#include "RenderUtils.h"
#include <cmath>
#include <algorithm>

StrokeController::StrokeController() {
}

void StrokeController::ResetStroke(GpuInk& gpuInk, AppState* pState) {
    m_strokeActive = false;
    m_smoothedPressure = 0.0;
    m_smoothedWidth = 0.0;
    m_lastTime = 0;
    if (gpuInk.IsInStroke()) {
        gpuInk.EndStroke();
    }
    if (pState) {
        pState->trajectory.OnStrokeEnd();
    }
}

void StrokeController::ProcessPenEvent(HWND hWnd, const PenInputEvent& event, AppState& state, GpuInk& gpuInk) {
    // ペンのZ高度・傾き・方位角情報をGPU墨汁エンジンへ通知
    gpuInk.UpdatePen(event.z, event.altitudeDegrees, event.azimuthRad, event.pressure <= 0.0);

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

        // 運筆速度の計算 (px/s)
        DWORD curTime = (event.time != 0) ? event.time : GetTickCount();
        double speed = 0.0;
        if (m_lastTime != 0 && curTime > m_lastTime) {
            double dtSec = static_cast<double>(curTime - m_lastTime) / 1000.0;
            if (dtSec > 0.0001) {
                speed = dist / dtSec;
            }
        }
        m_lastTime = curTime;

        // 筆の硬さ（感度補正）を筆圧に適用
        double hardness = state.brush.hardness;
        if (std::isnan(hardness) || hardness < 0.1) hardness = 1.0;
        double pressureFactor = std::pow(m_smoothedPressure, hardness);
        if (std::isnan(pressureFactor) || std::isinf(pressureFactor)) pressureFactor = 0.5;

        gpuInk.SetPressureFactor(pressureFactor);

        // 傾き・方位角・運筆速度による筆幅と止め・払いの物理計算
        double altitudeDegrees = event.altitudeDegrees;
        double tiltFactor = (90.0 - altitudeDegrees) / 90.0;
        if (tiltFactor < 0.0) tiltFactor = 0.0;

        if (dist >= 1.0) {
            m_lastMoveAngle = std::atan2(dy, dx);
        }
        double moveAngle = m_lastMoveAngle;

        double angleDiff = std::sin(event.azimuthRad - (moveAngle + 1.57079632679));
        double absAngleDiff = std::abs(angleDiff);

        // 角度（腹方向/刃方向）に応じた払いの調整:
        // 太くなる方向（腹側）で払った際にも綺麗に細く伸びるよう、角度に応じて払いの減衰指数を補正
        double angleHaraiPower = 1.3 + (0.2 + 0.3 * absAngleDiff) * (std::min)(dist, 10.0);
        double haraiFactor = std::pow(pressureFactor, angleHaraiPower);

        // 筆圧が抜ける（離筆に向かう）際は、ペンの腹の太さ影響が穂先の一点に自然収束する
        double tipConvergence = std::pow(pressureFactor, 0.4);
        double angleFactor = 1.0 + 0.3 * absAngleDiff * tipConvergence;
        double effectiveTiltFactor = tiltFactor * tipConvergence;

        double tomeFactor = 1.0 + 0.1 * (1.0 - (std::min)(dist / 3.0, 1.0)) * std::pow(pressureFactor, 0.8);

        double baseMaxWidth = state.brush.GetBaseMaxWidth();
        double rawWidth = baseMaxWidth * haraiFactor * tomeFactor * angleFactor * (1.0 + effectiveTiltFactor * 0.6);

        double startWidth = m_smoothedWidth;
        if (!gpuInk.IsInStroke() || !m_strokeActive) {
            m_smoothedWidth = rawWidth;
            startWidth = rawWidth;
            m_strokeActive = true;
        } else {
            // 線幅減少時（払い・跳ね）または高速移動時はアルファを大きくして即座に追従
            double alphaWidth = 0.3;
            if (rawWidth < m_smoothedWidth || dist > 3.0) {
                alphaWidth = 0.8;
            }
            m_smoothedWidth = m_smoothedWidth * (1.0 - alphaWidth) + rawWidth * alphaWidth;
        }

        // 運筆データアーカイブへ記録
        state.trajectory.AddPoint(event, rPaper, m_smoothedWidth, speed);

        // 墨の描画（常に高品位な墨汁濃度255で描画）
        gpuInk.DrawSegmentLinear(oldPaperPt, paperPt, startWidth, m_smoothedWidth, 255);
    } else {
        ResetStroke(gpuInk, &state);
    }

    m_ptOld = clientPt;
    InvalidateRect(hWnd, NULL, FALSE);
}
