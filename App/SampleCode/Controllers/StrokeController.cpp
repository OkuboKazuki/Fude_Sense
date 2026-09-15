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
    m_smoothedDist = 0.0;
    m_smoothedDirX = 0.0;
    m_smoothedDirY = 0.0;
    if (gpuInk.IsInStroke()) {
        gpuInk.EndStroke();
    }
    if (pState) {
        pState->trajectory.OnStrokeEnd();
        pState->calibration.OnStrokeEnd();
    }
}

void StrokeController::ProcessPenEvent(HWND hWnd, const PenInputEvent& event, AppState& state, GpuInk& gpuInk) {
    // ペンのZ高度・傾き・方位角情報をGPU墨汁エンジンへ通知
    gpuInk.UpdatePen(event.z, event.altitudeDegrees, event.azimuthRad, event.pressure <= 0.0);

    // UI操作（半紙に重なる全消しモーダル等）の直後は、ペンが紙から一度離れるまで
    // 運筆を受け付けない。接地したままのペンのパケットで墨が落ちるのを防ぐ。
    if (state.ui.suppressPenUntilLift) {
        if (event.pressure > 0.0) {
            ResetStroke(gpuInk, &state);
            m_ptOld = { event.x, event.y };
            InvalidateRect(hWnd, NULL, FALSE);
            return;
        }
        state.ui.suppressPenUntilLift = false;
    }

    // 筆圧のスムージング（急激な変化を抑制して滑らかな筆運びにする）
    double rawPrs = event.pressure;
    if (rawPrs > 0.0) {
        if (!m_strokeActive || m_smoothedPressure <= 0.0 || !gpuInk.IsInStroke()) {
            m_smoothedPressure = rawPrs;
        } else {
            double alphaPrs = (rawPrs < m_smoothedPressure) ? 0.6 : 0.35;
            m_smoothedPressure = m_smoothedPressure * (1.0 - alphaPrs) + rawPrs * alphaPrs;
        }
    } else {
        m_smoothedPressure = 0.0;
    }

    POINT clientPt = { event.x, event.y };
    const RECT& rPaper = state.ui.rPaper;
    const RECT& rSub = state.ui.rSub;

    // 半紙領域内かつモーダルやサブパネルに遮られていないか判定
    // 解析タブはリプレイ（過去の運筆の再生）を半紙へ描く。そのままだと新しい運筆が
    // GpuInk へ入っても画面に出ず、記録だけが増えてタイムラインとずれるため、
    // 解析タブ表示中は運筆そのものを受け付けない。
    bool canDrawInk = !state.ui.showClearConfirm
        && !state.calibration.IsResult()
        && state.ui.leftTab != LeftTab::Analysis
        && PtInRect(&rPaper, clientPt)
        && !(state.ui.isSubPanelOpen && PtInRect(&rSub, clientPt));

    if (m_smoothedPressure > 0.0 && canDrawInk) {
        // キャリブレーション中の筆圧サンプリング
        if (state.calibration.IsInDrawingStep()) {
            state.calibration.RecordPoint(m_smoothedPressure);
        }
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

        // 運筆距離の平滑化（ピクセル整数の 2px <-> 3px 量子化振動を吸収）
        if (!m_strokeActive || !gpuInk.IsInStroke()) {
            m_smoothedDist = dist;
            m_smoothedDirX = 0.0;
            m_smoothedDirY = 0.0;
        } else {
            m_smoothedDist = m_smoothedDist * 0.7 + dist * 0.3;
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

        // 進行方向ベクトルの平滑化（整数の 0px/1px 切り替わりによる角度バタつきを防止）
        if (dist >= 1.0) {
            double curDirX = dx / dist;
            double curDirY = dy / dist;
            if (!m_strokeActive || (m_smoothedDirX == 0.0 && m_smoothedDirY == 0.0)) {
                m_smoothedDirX = curDirX;
                m_smoothedDirY = curDirY;
            } else {
                double alphaDir = (dist > 4.0) ? 0.35 : 0.20;
                m_smoothedDirX = m_smoothedDirX * (1.0 - alphaDir) + curDirX * alphaDir;
                m_smoothedDirY = m_smoothedDirY * (1.0 - alphaDir) + curDirY * alphaDir;
            }
            m_lastMoveAngle = std::atan2(m_smoothedDirY, m_smoothedDirX);
        }
        double moveAngle = m_lastMoveAngle;

        double angleDiff = std::sin(event.azimuthRad - (moveAngle + 1.57079632679));
        double absAngleDiff = std::abs(angleDiff);

        // 角度（腹方向/刃方向）に応じた払いの調整:
        // 平滑化した移動距離 m_smoothedDist を使用し、パケットごとの乱高下を抑止
        double angleHaraiPower = 1.3 + (0.2 + 0.3 * absAngleDiff) * (std::min)(m_smoothedDist, 8.0);
        double haraiFactor = std::pow(pressureFactor, angleHaraiPower);

        // 筆圧が抜ける（離筆に向かう）際は、ペンの腹の太さ影響が穂先の一点に自然収束する
        double tipConvergence = std::pow(pressureFactor, 0.4);
        double angleFactor = 1.0 + 0.3 * absAngleDiff * tipConvergence;
        double effectiveTiltFactor = tiltFactor * tipConvergence;

        double tomeFactor = 1.0 + 0.1 * (1.0 - (std::min)(m_smoothedDist / 3.0, 1.0)) * std::pow(pressureFactor, 0.8);

        double baseMaxWidth = state.brush.GetBaseMaxWidth();
        double rawWidth = baseMaxWidth * haraiFactor * tomeFactor * angleFactor * (1.0 + effectiveTiltFactor * 0.6);

        double startWidth = m_smoothedWidth;
        if (!gpuInk.IsInStroke() || !m_strokeActive) {
            // 新しい画の書き始め。墨を置く前の状態を控えて「一画戻す」に備える。
            // ここは墨の消費・アーカイブ記録・描画のいずれよりも前になる。
            state.undo.PushBeforeStroke(gpuInk, state.ink, state.trajectory.GetTotalStrokeCount());
            m_smoothedWidth = rawWidth;
            startWidth = rawWidth;
            m_strokeActive = true;
        } else {
            // 線幅変化の追従
            // 急激な線幅減少（払い・跳ね）時は素直に追従しつつ、通常の運筆では滑らかな粘りを維持
            double alphaWidth = 0.25;
            if (rawWidth < m_smoothedWidth) {
                alphaWidth = 0.45;
            } else if (dist > 6.0) {
                alphaWidth = 0.35;
            }
            m_smoothedWidth = m_smoothedWidth * (1.0 - alphaWidth) + rawWidth * alphaWidth;
        }

        // 運筆に伴うインク・水分の物理消費
        // アーカイブ記録より先に行う。直後の seg.dryness はこの消費を織り込んだ
        // 値を読むため、順序を入れ替えるとかすれが1セグメント分遅れる。
        double stepDist = (dist > 0.0) ? dist : 1.0;
        double widthRatio = m_smoothedWidth / 36.0;
        double consumeAmount = stepDist * widthRatio * 0.00015;
        state.ink.Consume(consumeAmount);

        // 運筆データアーカイブへ記録
        state.trajectory.AddPoint(event, rPaper, m_smoothedWidth, speed,
            pressureFactor, state.ink.GetDryness(), m_lastMoveAngle);

        StrokeSegment seg;
        seg.a = oldPaperPt;
        seg.b = paperPt;
        seg.startWidth = startWidth;
        seg.endWidth = m_smoothedWidth;
        seg.dirX = std::cos(m_lastMoveAngle);
        seg.dirY = std::sin(m_lastMoveAngle);
        seg.dryness = state.ink.GetDryness();
        seg.inkAlpha = 255;
        gpuInk.DrawSegmentLinear(seg);

        // セグメント周辺の Dirty Rect（軸平行境界ボックス + マージン）のみを局所更新
        double maxWidth = (std::max)(startWidth, m_smoothedWidth);
        int pad = static_cast<int>(std::ceil(maxWidth * 1.5 + 24.0));
        RECT rcDirty;
        rcDirty.left   = (std::max)(rPaper.left,   (std::min)(m_ptOld.x, clientPt.x) - pad);
        rcDirty.top    = (std::max)(rPaper.top,    (std::min)(m_ptOld.y, clientPt.y) - pad);
        rcDirty.right  = (std::min)(rPaper.right,  (std::max)(m_ptOld.x, clientPt.x) + pad);
        rcDirty.bottom = (std::min)(rPaper.bottom, (std::max)(m_ptOld.y, clientPt.y) + pad);
        InvalidateRect(hWnd, &rcDirty, FALSE);

        // 墨消費に伴う硯パネルの残量表示を更新
        if (state.ui.rInkStoneLarge.right > state.ui.rInkStoneLarge.left) {
            InvalidateRect(hWnd, &state.ui.rInkStoneLarge, FALSE);
        }
    } else {
        bool wasActive = m_strokeActive;
        ResetStroke(gpuInk, &state);
        if (wasActive) {
            // ストローク終了時のみ、半紙全体を更新してにじみ・終筆を反映
            InvalidateRect(hWnd, &rPaper, FALSE);
        }
    }

    m_ptOld = clientPt;
}
