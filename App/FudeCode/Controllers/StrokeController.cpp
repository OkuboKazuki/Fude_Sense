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
    m_lastRawPressure = event.pressure;

    // 硯・「墨を補充」をペンで押している間は、強く押し込むほど墨を継ぎ足す
    if (state.ui.isPenRefilling) {
        if (event.pressure > 0.0) {
            double before = state.ink.amount;
            state.ink.PressRefill(event.pressure);
            if (state.ink.amount != before) {
                InvalidateRect(hWnd, state.ui.paperOnly ? &state.ui.rInkRefillBtn : &state.ui.rRight, FALSE);
            }
        } else {
            state.ui.isPenRefilling = false;
        }
    }

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
    // 解析タブ表示中は運筆そのものを受け付けない（紙だけ表示ではリプレイを描かないので受け付ける）。
    // タイトル画面表示中や遷移アニメーション中も受け付けない。
    bool canDrawInk = (state.currentScreen == AppScreen::Studio)
        && !state.isTransitioning
        && !state.ui.showClearConfirm
        && (state.ui.paperOnly || state.ui.leftTab != LeftTab::Analysis)
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
        // 太くなる方向（腹側）で払った際にも綺麗に細く伸びるよう、角度に応じて払いの減衰指数を補正
        double angleHaraiPower = 1.3 + (0.2 + 0.3 * absAngleDiff) * (std::min)(dist, 10.0);
        double haraiFactor = std::pow(pressureFactor, angleHaraiPower);

        // 筆圧が抜ける（離筆に向かう）際は、ペンの腹の太さ影響が穂先の一点に自然収束する
        double tipConvergence = std::pow(pressureFactor, 0.4);
        double angleFactor = 1.0 + 0.3 * absAngleDiff * tipConvergence;
        double effectiveTiltFactor = tiltFactor * tipConvergence;

        // 止め（筆を留めた際のわずかな溜まり）
        double tomeFactor = 1.0 + 0.08 * (1.0 - (std::min)(m_smoothedDist / 4.0, 1.0)) * std::pow(pressureFactor, 0.8);

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
        // 消費量は「移動距離 × 線幅」に比例する（線幅 36px なら 1px あたり 0.0002 / 1.02）。
        // 満タン（残量 1.0 = 100% 表示）のまま書ける墨の量を 1.02 倍にするため、消費を 1.02 で割る。
        // 乾いてくると（dryness > 0、残量 60% 未満）消費を最大 45% 減らし、
        // かすれが続く距離を伸ばす。dryness = 0 の間は消費量は変わらない。
        constexpr double USABLE_INK_SCALE = 1.02;
        double stepDist = (dist > 0.0) ? dist : 1.0;
        double widthRatio = m_smoothedWidth / 36.0;
        double drynessFactor = 1.0 - state.ink.GetDryness() * 0.45;
        double consumeAmount = stepDist * widthRatio * (0.00020 / USABLE_INK_SCALE) * drynessFactor;
        state.ink.Consume(consumeAmount);

        // 運筆データアーカイブへ記録
        state.trajectory.AddPoint(event, rPaper, m_smoothedWidth, speed,
            pressureFactor, state.ink.GetDryness(), m_lastMoveAngle, state.ink.amount);

        // 内部固定論理解像度へのスケーリング変換
        int inkW = gpuInk.GetWidth();
        int inkH = gpuInk.GetHeight();
        int paperW = RenderUtils::RW(rPaper);
        int paperH = RenderUtils::RH(rPaper);
        double scaleX = (paperW > 0 && inkW > 0) ? (static_cast<double>(inkW) / static_cast<double>(paperW)) : 1.0;
        double scaleY = (paperH > 0 && inkH > 0) ? (static_cast<double>(inkH) / static_cast<double>(paperH)) : 1.0;
        double scaleAvg = (scaleX + scaleY) * 0.5;

        StrokeSegment seg;
        seg.a = {
            static_cast<LONG>(std::round(oldPaperPt.x * scaleX)),
            static_cast<LONG>(std::round(oldPaperPt.y * scaleY))
        };
        seg.b = {
            static_cast<LONG>(std::round(paperPt.x * scaleX)),
            static_cast<LONG>(std::round(paperPt.y * scaleY))
        };
        seg.startWidth = startWidth * scaleAvg;
        seg.endWidth = m_smoothedWidth * scaleAvg;
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

        // 墨消費に伴う硯パネル（液面＋墨残量テキスト）をリアルタイムに滑らかに更新（約30Hz / 33ms間隔）
        if (state.ui.rInkStoneLarge.right > state.ui.rInkStoneLarge.left) {
            if (now - m_lastInkStoneInvalidateTick >= 33) {
                RECT rcStoneArea = {
                    state.ui.rInkStoneLarge.left,
                    state.ui.rInkStoneLarge.top - 36,
                    state.ui.rInkStoneLarge.right,
                    state.ui.rInkStoneLarge.bottom
                };
                InvalidateRect(hWnd, &rcStoneArea, FALSE);
                m_lastInkStoneInvalidateTick = now;
            }
        }
        // 紙だけ表示では残量を「墨を補充」ボタンに出している（硯パネルと同じく約30Hz）
        if (state.ui.paperOnly && now - m_lastInkStoneInvalidateTick >= 33) {
            InvalidateRect(hWnd, &state.ui.rInkRefillBtn, FALSE);
            m_lastInkStoneInvalidateTick = now;
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
                RECT rcStoneArea = {
                    state.ui.rInkStoneLarge.left,
                    state.ui.rInkStoneLarge.top - 36,
                    state.ui.rInkStoneLarge.right,
                    state.ui.rInkStoneLarge.bottom
                };
                InvalidateRect(hWnd, &rcStoneArea, FALSE);
            }
        }
    }

    m_ptOld = clientPt;
}
