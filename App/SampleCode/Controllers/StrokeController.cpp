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
    m_hasPendingDirty = false;
    m_accumDirty = { 0, 0, 0, 0 };
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

        // 筆圧が抜ける（離筆に向かう）際は、ペンの腹の太さ影響が穂先の一点に自然収束する
        double tipConvergence = std::pow(pressureFactor, 0.4);
        double angleFactor = 1.0 + 0.3 * absAngleDiff * tipConvergence;
        double effectiveTiltFactor = tiltFactor * tipConvergence;

        // 角度（腹方向/刃方向）と離筆に応じた払いの調整:
        // 筆圧がしっかりかかっている接地運筆中は速度が出ても線が極端に痩せ細らないようにし、
        // 筆圧が抜けて離筆に向かう際（低筆圧時）に速度・角度と連動して穂先へ綺麗に収束させる
        double haraiReleaseFactor = (std::max)(0.0, 1.0 - pressureFactor);
        double speedHaraiEffect = (std::min)(dist / 8.0, 1.0) * haraiReleaseFactor;
        double haraiPower = 1.0 + (0.3 + 0.4 * absAngleDiff) * speedHaraiEffect;
        double haraiFactor = std::pow(pressureFactor, haraiPower);

        // 止め（筆を留めた際のわずかな溜まり）
        double tomeFactor = 1.0 + 0.08 * (1.0 - (std::min)(dist / 4.0, 1.0)) * std::pow(pressureFactor, 0.8);

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
            // 急激な線幅変動を平滑化しつつ、払い・跳ねには自然に追従
            double alphaWidth = (rawWidth < m_smoothedWidth) ? 0.4 : 0.25;
            m_smoothedWidth = m_smoothedWidth * (1.0 - alphaWidth) + rawWidth * alphaWidth;
        }

        // 運筆に伴うインク・水分の物理消費
        // WATER_LOSS_RATIO(1.35) を加味し、漢字1文字で約70%消費して25〜30%余るバランスに設定
        double stepDist = (dist > 0.0) ? dist : 1.0;
        double widthRatio = m_smoothedWidth / 36.0;
        double consumeAmount = stepDist * widthRatio * 0.00035;
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

        // セグメント周辺の Dirty Rect（軸平行境界ボックス + マージン）を累積
        double maxWidth = (std::max)(startWidth, m_smoothedWidth);
        int pad = static_cast<int>(std::ceil(maxWidth * 1.5 + 24.0));
        RECT rcDirty;
        rcDirty.left   = (std::max)(rPaper.left,   (std::min)(m_ptOld.x, clientPt.x) - pad);
        rcDirty.top    = (std::max)(rPaper.top,    (std::min)(m_ptOld.y, clientPt.y) - pad);
        rcDirty.right  = (std::min)(rPaper.right,  (std::max)(m_ptOld.x, clientPt.x) + pad);
        rcDirty.bottom = (std::min)(rPaper.bottom, (std::max)(m_ptOld.y, clientPt.y) + pad);

        if (!m_hasPendingDirty) {
            m_accumDirty = rcDirty;
            m_hasPendingDirty = true;
        } else {
            UnionRect(&m_accumDirty, &m_accumDirty, &rcDirty);
        }

        // 描画更新要求（InvalidateRect）の頻度を約120Hz（8ms間隔）にレート制御
        // （物理スタンプ投入は240Hzで即時実行しつつ、再描画の過剰呼び出しを抑制）
        constexpr DWORD kMinInvalidateIntervalMs = 8;
        DWORD now = (event.time != 0) ? event.time : GetTickCount();
        if (now - m_lastInvalidateTick >= kMinInvalidateIntervalMs) {
            InvalidateRect(hWnd, &m_accumDirty, FALSE);
            m_accumDirty = { 0, 0, 0, 0 };
            m_hasPendingDirty = false;
            m_lastInvalidateTick = now;
        }

        // 墨消費に伴う硯パネルの残量表示を適正頻度（約16Hz / 60ms間隔）で更新
        if (state.ui.rInkStoneLarge.right > state.ui.rInkStoneLarge.left) {
            if (now - m_lastInkStoneInvalidateTick >= 60) {
                InvalidateRect(hWnd, &state.ui.rInkStoneLarge, FALSE);
                m_lastInkStoneInvalidateTick = now;
            }
        }
    } else {
        bool wasActive = m_strokeActive;
        if (m_hasPendingDirty) {
            InvalidateRect(hWnd, &m_accumDirty, FALSE);
            m_accumDirty = { 0, 0, 0, 0 };
            m_hasPendingDirty = false;
        }
        ResetStroke(gpuInk, &state);
        if (wasActive) {
            // ストローク終了時のみ、半紙全体を更新してにじみ・終筆を反映
            InvalidateRect(hWnd, &rPaper, FALSE);
            if (state.ui.rInkStoneLarge.right > state.ui.rInkStoneLarge.left) {
                InvalidateRect(hWnd, &state.ui.rInkStoneLarge, FALSE);
            }
        }
    }

    m_ptOld = clientPt;
}
