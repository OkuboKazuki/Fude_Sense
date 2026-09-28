#include "stdafx.h"
#include "AppController.h"
#include "RenderUtils.h"
#include "ImageExporter.h"
#include <imm.h>

#pragma comment(lib, "imm32.lib")

using namespace RenderUtils;

// IME の変換候補ウィンドウを入力欄の位置へ寄せる。
// 指定しないとウィンドウ左上に出てしまい、どこへ入力しているのか分からない。
static void SetOtehonImePosition(HWND hWnd, const RECT& inputBox) {
    HIMC hImc = ImmGetContext(hWnd);
    if (!hImc) return;
    COMPOSITIONFORM cf{};
    cf.dwStyle = CFS_POINT;
    cf.ptCurrentPos.x = inputBox.left + 12;
    cf.ptCurrentPos.y = inputBox.top + 6;
    ImmSetCompositionWindow(hImc, &cf);
    ImmReleaseContext(hWnd, hImc);
}

// 硯・「墨を補充」を押したときの補充。
// マウスは筆圧が無いので満タンにする。ペンは押した筆圧に応じた量だけ含ませ、
// 離すまでの間に強く押し込めば StrokeController 側で継ぎ足す。
static void RefillByPress(AppState& state, double penPressure) {
    if (penPressure < 0.0) {
        state.ink.Refill();
        return;
    }
    state.ink.BeginPressRefill();
    state.ink.PressRefill(penPressure);
    state.ui.isPenRefilling = true;
    // 押したままのペンがそのまま運筆を始めないよう、離すまでロックする
    state.ui.suppressPenUntilLift = true;
}

void AppController::ClearAllInk(HWND hWnd, AppState& state, GpuInk& gpuInk) {
    gpuInk.Clear();
    state.trajectory.Clear();
    state.undo.Clear();
    state.replay.Reset();
    state.ui.showClearConfirm = false;
    // 消去した瞬間にペンが半紙へ接地したままだと、直後のパケットで墨が落ちてしまう。
    // ペンが紙から離れるまで運筆入力をロックする。
    state.ui.suppressPenUntilLift = true;
    // シークバーのツマミ位置は再生時刻から Layout で決まる。全消しで時刻を 0 へ戻した後に
    // 取り直さないと、ツマミだけ古い位置に残る。
    RECT rcClient = { 0, 0, 0, 0 };
    GetClientRect(hWnd, &rcClient);
    if (rcClient.right > 0 && rcClient.bottom > 0) {
        state.Layout(rcClient.right - rcClient.left, rcClient.bottom - rcClient.top);
    }
    InvalidateRect(hWnd, NULL, FALSE);
}

void AppController::OnSize(HWND hWnd, int width, int height, AppState& state, GpuInk& gpuInk) {
    state.Layout(width, height);
    InvalidateRect(hWnd, NULL, FALSE);
}

// 記録（m_strokes）が変わったらリプレイのタイムラインを作り直す。
// 一画戻す／復元でも画数は変わるため、解析タブに入った時だけ作り直していると
// 消したはずの画がリプレイに残る。再生中に記録が動くと、再生位置が消えた画の
// 上を走ることになるので、先に再生を止める。
void AppController::SyncReplayTimeline(HWND hWnd, AppState& state) {
    if (state.replay.state == ReplayState::Playing) {
        state.replay.state = ReplayState::Paused;
        KillTimer(hWnd, REPLAY_TIMER_ID);
    }

    state.trajectory.BuildReplayTimeline();
    state.replay.totalDurationMs = state.trajectory.GetReplayTotalDurationMs();
    if (state.replay.currentTimeMs > state.replay.totalDurationMs) {
        state.replay.currentTimeMs = state.replay.totalDurationMs;
    }
    state.replay.hasValidSample = state.trajectory.GetReplaySample(
        state.replay.currentTimeMs, state.ui.rPaper, state.replay.currentSample);
    state.UpdateReplaySeekThumb();
}

// 直前の1画を取り消す。
// 墨は画素バッファへ破壊的に積み上がり、にじみも時間で進むため、運筆データから
// 描き直しても同じ絵にはならない。画を書き始める直前に控えておいた状態
// （UndoHistory）を書き戻す。
bool AppController::UndoStroke(HWND hWnd, AppState& state, GpuInk& gpuInk) {
    // 墨・墨残量・運筆アーカイブ（解析グラフ・JSON/CSV）をまとめて1画戻す
    if (!state.undo.Undo(gpuInk, state.ink, state.trajectory)) return false;

    SyncReplayTimeline(hWnd, state);

    // 戻した瞬間にペンが半紙へ接地したままだと、直後のパケットで墨が落ちてしまう。
    // ペンが紙から離れるまで運筆入力をロックする（全消しと同じ扱い）。
    state.ui.suppressPenUntilLift = true;
    InvalidateRect(hWnd, NULL, FALSE);
    return true;
}

// 戻しすぎた1画を復元する。戻したときに控えておいた状態を書き戻すだけなので、
// 取り消しと同じ経路を逆向きに辿る。
bool AppController::RedoStroke(HWND hWnd, AppState& state, GpuInk& gpuInk) {
    if (!state.undo.Redo(gpuInk, state.ink, state.trajectory)) return false;

    SyncReplayTimeline(hWnd, state);

    state.ui.suppressPenUntilLift = true;
    InvalidateRect(hWnd, NULL, FALSE);
    return true;
}

// 用紙種類・縦横比が変わった際の後始末。
// 新しい用紙の固定論理解像度で GpuInk を再初期化し、白紙にする。
static void RelayoutForPaper(HWND hWnd, AppState& state, GpuInk& gpuInk) {
    RECT rc = { 0, 0, 0, 0 };
    GetClientRect(hWnd, &rc);
    state.Layout(rc.right - rc.left, rc.bottom - rc.top);

    int canvasW = 0, canvasH = 0;
    state.paper.GetCanvasSize(canvasW, canvasH);
    // 紙だけ表示の半紙は画面に合わせた横長なので、縦長の固定解像度のままだと
    // 墨が横へ引き伸ばされる。長辺をそろえたまま、表示中の半紙と同じ縦横比にする。
    int pw = RW(state.ui.rPaper);
    int ph = RH(state.ui.rPaper);
    if (state.ui.paperOnly && pw > 0 && ph > 0) {
        int longEdge = (std::max)(canvasW, canvasH);
        if (pw >= ph) {
            canvasW = longEdge;
            canvasH = static_cast<int>(std::round(static_cast<double>(longEdge) * ph / pw));
        } else {
            canvasH = longEdge;
            canvasW = static_cast<int>(std::round(static_cast<double>(longEdge) * pw / ph));
        }
    }
    if (canvasW > 0 && canvasH > 0) {
        gpuInk.Initialize(canvasW, canvasH);
        state.undo.Clear();
        state.trajectory.Clear();
        state.replay.Reset();
    }

    // 墨・記録・控えを消し、ペンが接地したままでも運筆を始めないようロックする
    AppController::ClearAllInk(hWnd, state, gpuInk);
}

// 紙だけ表示の出入り。
// 紙だけ表示の半紙は、通常表示の半紙を画面の上で左回りに90度倒したもの
// （画面を右回りに倒して見ると元の向きに戻る）。墨のバッファも縦横が入れ替わるので、
// 切り替える前の墨を控えておき、新しい半紙へ回して書き戻す。書いていた字はそのまま残る。
// 運筆記録と「一画戻す」の控えは元の向きの座標で持っているので、これまでどおり消す。
void AppController::SetPaperOnly(HWND hWnd, bool on, AppState& state, GpuInk& gpuInk) {
    if (state.ui.paperOnly == on) return;

    // 紙だけ表示ではリプレイもモーダルも描かないので、止めて閉じておく
    if (state.replay.state == ReplayState::Playing) {
        state.replay.state = ReplayState::Paused;
        KillTimer(hWnd, REPLAY_TIMER_ID);
    }
    if (state.calibration.IsActive()) state.calibration.Reset();
    state.otehon.isTyping = false;
    state.otehon.isDraggingOpacity = false;
    state.brush.isDraggingHardness = false;
    state.ui.hoverPaperOnly = 0;

    // 書いていた墨を控える（半紙を作り直すと消えるため）
    InkSnapshot keep;
    bool hasInk = gpuInk.CaptureSnapshot(keep);

    state.ui.paperOnly = on;

    RelayoutForPaper(hWnd, state, gpuInk);

    // 紙だけ表示へ入るときは左回り、戻るときは右回りに回して書き戻す
    if (hasInk && gpuInk.RestoreSnapshotRotated(keep, on)) {
        InvalidateRect(hWnd, NULL, FALSE);
    }
}

bool AppController::OnLButtonDown(HWND hWnd, POINT pt, AppState& state, GpuInk& gpuInk, double penPressure) {
    UIState& ui = state.ui;
    RECT clientRect = { 0, 0, 0, 0 };
    GetClientRect(hWnd, &clientRect);
    int w = clientRect.right - clientRect.left;
    int h = clientRect.bottom - clientRect.top;

    // 0. タイトル画面でのクリック / タップ処理
    if (state.currentScreen == AppScreen::Title) {
        if (state.isTransitioning) return true;
        if (state.ui.suppressPenUntilLift) return true;

        state.pendingStartTab = LeftTab::Brush;
        state.ui.suppressPenUntilLift = true; // スタジオ画面への遷移完了後にペンが離れるまで描画を抑制

        // 墨染めフェード遷移を開始
        state.isTransitioning = true;
        state.transitionStartTime = GetTickCount();
        state.transitionProgress = 0.0f;
        SetTimer(hWnd, TRANSITION_TIMER_ID, 16, NULL);
        KillTimer(hWnd, TITLE_ANIM_TIMER_ID);
        InvalidateRect(hWnd, NULL, FALSE);
        return true;
    }

    // 1. 全消し確認モーダル表示中のクリック（紙だけ表示でも出る）
    if (ui.showClearConfirm) {
        // モーダルは半紙の上に重なっている。どのボタンを押した場合でも、
        // モーダルを閉じた直後に接地したままのペンが運筆を始めないようロックする。
        ui.suppressPenUntilLift = true;
        POINT mp = state.ToClearModalSpace(pt, h);
        if (PtIn(ui.rModalClearBtn, mp)) {
            ClearAllInk(hWnd, state, gpuInk);
            return true;
        } else if (PtIn(ui.rModalCancelBtn, mp) || !PtIn(ui.rClearModalBox, mp)) {
            ui.showClearConfirm = false;
            InvalidateRect(hWnd, NULL, FALSE);
            return true;
        }
        return true;
    }

    // 紙だけ表示中は半紙の右の3ボタンだけを見る。
    // それ以外は false を返して素通しし、半紙への運筆をそのまま通す。
    if (ui.paperOnly) {
        // 通常表示の「全消し」ボタンと同じ確認モーダルを出す（倒した向きで描く）
        if (PtIn(ui.rPaperOnlyClearBtn, pt)) {
            ui.hoverPaperOnly = 0;
            ui.hoverClearModal = 0;
            ui.showClearConfirm = true;
            ui.suppressPenUntilLift = true;
            InvalidateRect(hWnd, NULL, FALSE);
            return true;
        }
        if (PtIn(ui.rPaperOnlyExitBtn, pt)) {
            SetPaperOnly(hWnd, false, state, gpuInk);
            return true;
        }
        if (PtIn(ui.rInkRefillBtn, pt)) {
            RefillByPress(state, penPressure);
            ui.suppressPenUntilLift = true;
            InvalidateRect(hWnd, &ui.rInkRefillBtn, FALSE);
            return true;
        }
        return false;
    }

    // 入力欄以外を押したら文字入力を終える
    if (state.otehon.isTyping && !PtIn(ui.rOtehonInputBox, pt)) {
        state.otehon.isTyping = false;
        InvalidateRect(hWnd, &ui.rSub, FALSE);
    }

    // 1.5 筆圧キャリブレーション結果モーダル表示中のクリック
    if (state.calibration.IsResult()) {
        ui.suppressPenUntilLift = true;
        if (PtIn(ui.rCalibApplyBtn, pt)) {
            state.brush.hardness = state.calibration.GetResult().recommendedHardness;
            state.calibration.Reset();
            gpuInk.Clear();
            state.trajectory.Clear();
            state.replay.Reset();
            state.ink.Refill();
            state.SetSaveFeedback(L"✓ 筆の硬さを自動調整しました");
            InvalidateRect(hWnd, NULL, FALSE);
            return true;
        } else if (PtIn(ui.rCalibRetryBtn, pt)) {
            gpuInk.Clear();
            state.trajectory.Clear();
            state.replay.Reset();
            state.ink.Refill();
            state.calibration.Start();
            InvalidateRect(hWnd, NULL, FALSE);
            return true;
        } else if (PtIn(ui.rCalibCloseBtn, pt) || !PtIn(ui.rCalibModalBox, pt)) {
            state.calibration.Reset();
            gpuInk.Clear();
            state.trajectory.Clear();
            state.replay.Reset();
            state.ink.Refill();
            InvalidateRect(hWnd, NULL, FALSE);
            return true;
        }
        return true;
    }

    // 1.8 ホームボタン「🏠」（タイトル画面へ戻る）
    if (PtIn(ui.rTbHomeBtn, pt)) {
        state.currentScreen = AppScreen::Title;
        state.isTransitioning = false;
        state.transitionProgress = 0.0f;
        state.replay.state = ReplayState::Stopped;
        state.ui.suppressPenUntilLift = true; // タイトル画面に戻った直後に同じペン押下で再度スタジオ画面へ遷移するのを防止
        KillTimer(hWnd, REPLAY_TIMER_ID);
        KillTimer(hWnd, TRANSITION_TIMER_ID);
        SetTimer(hWnd, TITLE_ANIM_TIMER_ID, 33, NULL);
        InvalidateRect(hWnd, NULL, FALSE);
        return true;
    }

    // 2. 左上トグルボタン「<<<」「>>>」
    if (PtIn(ui.rTbNavToggle, pt)) {
        ui.isSubPanelOpen = !ui.isSubPanelOpen;
        state.Layout(w, h);
        InvalidateRect(hWnd, NULL, FALSE);
        return true;
    }

    // 3. 開いているときのフローティングメニュータブ・詳細操作
    if (ui.isSubPanelOpen) {
        if (PtIn(ui.rTbBrush, pt)) {
            ui.leftTab = LeftTab::Brush;
            state.replay.state = ReplayState::Stopped;
            InvalidateRect(hWnd, NULL, FALSE);
            return true;
        } else if (PtIn(ui.rTbPaper, pt)) {
            ui.leftTab = LeftTab::Paper;
            state.replay.state = ReplayState::Stopped;
            InvalidateRect(hWnd, NULL, FALSE);
            return true;
        } else if (PtIn(ui.rTbAnalysis, pt)) {
            ui.leftTab = LeftTab::Analysis;
            SyncReplayTimeline(hWnd, state);
            if (state.replay.currentTimeMs == 0) {
                // 開いた直後は書き上がった状態を見せる
                state.replay.currentTimeMs = state.replay.totalDurationMs;
                state.replay.hasValidSample = state.trajectory.GetReplaySample(state.replay.currentTimeMs, state.ui.rPaper, state.replay.currentSample);
            }
            state.replay.state = ReplayState::Paused;
            state.Layout(w, h);
            InvalidateRect(hWnd, NULL, FALSE);
            return true;
        } else if (PtIn(ui.rTbSave, pt)) {
            ui.leftTab = LeftTab::Save;
            state.replay.state = ReplayState::Stopped;
            InvalidateRect(hWnd, NULL, FALSE);
            return true;
        } else if (PtIn(ui.rTbOtehon, pt)) {
            ui.leftTab = LeftTab::Otehon;
            state.replay.state = ReplayState::Stopped;
            InvalidateRect(hWnd, NULL, FALSE);
            return true;
        }

        // 詳細パネル内部の操作
        if (PtIn(ui.rSub, pt)) {
            if (ui.leftTab == LeftTab::Analysis) {
                // 記録が1画も無い間は再生・シーク系の操作を受け付けない（ボタンも無効表示）
                bool hasRecording = state.trajectory.GetTotalStrokeCount() > 0;
                // 1. 最初に戻る (↺)
                if (hasRecording && PtIn(ui.rReplayResetBtn, pt)) {
                    state.replay.currentTimeMs = 0;
                    state.replay.hasValidSample = state.trajectory.GetReplaySample(0, state.ui.rPaper, state.replay.currentSample);
                    state.UpdateReplaySeekThumb();
                    InvalidateRect(hWnd, &ui.rPaper, FALSE);
                    InvalidateRect(hWnd, &ui.rSub, FALSE);
                    return true;
                }
                // 2. 前画 (⏮)
                else if (hasRecording && PtIn(ui.rReplayPrevBtn, pt)) {
                    int curIdx = state.trajectory.FindStrokeIndexAtTimeline(state.replay.currentTimeMs);
                    DWORD curStart = state.trajectory.GetStrokeTimelineStart(curIdx);
                    if (state.replay.currentTimeMs > curStart + 180) {
                        state.replay.currentTimeMs = curStart;
                    } else if (curIdx > 0) {
                        state.replay.currentTimeMs = state.trajectory.GetStrokeTimelineStart(curIdx - 1);
                    } else {
                        state.replay.currentTimeMs = 0;
                    }
                    state.replay.hasValidSample = state.trajectory.GetReplaySample(state.replay.currentTimeMs, state.ui.rPaper, state.replay.currentSample);
                    state.UpdateReplaySeekThumb();
                    InvalidateRect(hWnd, &ui.rPaper, FALSE);
                    InvalidateRect(hWnd, &ui.rSub, FALSE);
                    return true;
                }
                // 3. 再生 / 一時停止 (▶ / ❚❚)
                else if (hasRecording && PtIn(ui.rReplayPlayBtn, pt)) {
                    if (state.replay.totalDurationMs == 0) {
                        state.trajectory.BuildReplayTimeline();
                        state.replay.totalDurationMs = state.trajectory.GetReplayTotalDurationMs();
                    }
                    if (state.replay.state == ReplayState::Playing) {
                        state.replay.state = ReplayState::Paused;
                        KillTimer(hWnd, REPLAY_TIMER_ID);
                    } else {
                        if (state.replay.currentTimeMs >= state.replay.totalDurationMs) {
                            state.replay.currentTimeMs = 0;
                        }
                        state.replay.state = ReplayState::Playing;
                        // 再生中だけタイマを回す。適正間隔（20ms / 50fps）で描画詰まりを防止。
                        SetTimer(hWnd, REPLAY_TIMER_ID, 20, NULL);
                    }
                    state.replay.hasValidSample = state.trajectory.GetReplaySample(state.replay.currentTimeMs, state.ui.rPaper, state.replay.currentSample);
                    state.UpdateReplaySeekThumb();
                    InvalidateRect(hWnd, &ui.rPaper, FALSE);
                    InvalidateRect(hWnd, &ui.rSub, FALSE);
                    return true;
                }
                // 4. 次画 (⏭)
                else if (hasRecording && PtIn(ui.rReplayNextBtn, pt)) {
                    int curIdx = state.trajectory.FindStrokeIndexAtTimeline(state.replay.currentTimeMs);
                    if (curIdx + 1 < static_cast<int>(state.trajectory.GetTotalStrokeCount())) {
                        state.replay.currentTimeMs = state.trajectory.GetStrokeTimelineStart(curIdx + 1);
                    } else {
                        state.replay.currentTimeMs = state.replay.totalDurationMs;
                    }
                    state.replay.hasValidSample = state.trajectory.GetReplaySample(state.replay.currentTimeMs, state.ui.rPaper, state.replay.currentSample);
                    state.UpdateReplaySeekThumb();
                    InvalidateRect(hWnd, &ui.rPaper, FALSE);
                    InvalidateRect(hWnd, &ui.rSub, FALSE);
                    return true;
                }
                // 5. 再生速度切替 (0.5x, 1.0x, 2.0x)
                else if (PtIn(ui.rReplaySpeedBtn[0], pt)) {
                    state.replay.playbackSpeed = 0.5;
                    InvalidateRect(hWnd, &ui.rAnalysisReplayBox, FALSE);
                    return true;
                } else if (PtIn(ui.rReplaySpeedBtn[1], pt)) {
                    state.replay.playbackSpeed = 1.0;
                    InvalidateRect(hWnd, &ui.rAnalysisReplayBox, FALSE);
                    return true;
                } else if (PtIn(ui.rReplaySpeedBtn[2], pt)) {
                    state.replay.playbackSpeed = 2.0;
                    InvalidateRect(hWnd, &ui.rAnalysisReplayBox, FALSE);
                    return true;
                }
                // 6. シークバーのクリック・ドラッグ開始
                RECT rSeekHit = ui.rReplaySeekTrack;
                rSeekHit.top -= 6; rSeekHit.bottom += 6;
                if (hasRecording && PtIn(rSeekHit, pt)) {
                    state.replay.isDraggingSeekBar = true;
                    SetCapture(hWnd);
                    double trackW = static_cast<double>(RW(ui.rReplaySeekTrack));
                    double norm = (trackW > 0.0) ? static_cast<double>(pt.x - ui.rReplaySeekTrack.left) / trackW : 0.0;
                    norm = Clamp(norm, 0.0, 1.0);
                    state.replay.currentTimeMs = static_cast<DWORD>(state.replay.totalDurationMs * norm);
                    state.replay.hasValidSample = state.trajectory.GetReplaySample(state.replay.currentTimeMs, state.ui.rPaper, state.replay.currentSample);
                    state.UpdateReplaySeekThumb();
                    InvalidateRect(hWnd, &ui.rPaper, FALSE);
                    InvalidateRect(hWnd, &ui.rSub, FALSE);
                    return true;
                }
                // 7. 波形グラフのクリック・ドラッグによるシーク
                RECT rPlotHit = { ui.rAnalysisGraphBox.left + 38, ui.rAnalysisGraphBox.top + 32, ui.rAnalysisGraphBox.right - 14, ui.rAnalysisGraphBox.bottom - 20 };
                if (hasRecording && PtIn(rPlotHit, pt)) {
                    state.replay.isDraggingWaveform = true;
                    SetCapture(hWnd);
                    double plotW = static_cast<double>(RW(rPlotHit));
                    double norm = (plotW > 0.0) ? static_cast<double>(pt.x - rPlotHit.left) / plotW : 0.0;
                    norm = Clamp(norm, 0.0, 1.0);
                    state.replay.currentTimeMs = static_cast<DWORD>(state.replay.totalDurationMs * norm);
                    state.replay.hasValidSample = state.trajectory.GetReplaySample(state.replay.currentTimeMs, state.ui.rPaper, state.replay.currentSample);
                    state.UpdateReplaySeekThumb();
                    InvalidateRect(hWnd, &ui.rPaper, FALSE);
                    InvalidateRect(hWnd, &ui.rSub, FALSE);
                    return true;
                }
            } else if (ui.leftTab == LeftTab::Brush) {

                if (RW(ui.rHardnessTrack) > 0) {
                    RECT hitBox = ui.rHardnessTrack;
                    hitBox.top -= 6; hitBox.bottom += 6;
                    if (PtIn(hitBox, pt)) {
                        state.brush.isDraggingHardness = true;
                        SetCapture(hWnd);
                        double trackW = static_cast<double>(RW(ui.rHardnessTrack));
                        double norm = (trackW > 0.0) ? static_cast<double>(pt.x - ui.rHardnessTrack.left) / trackW : 0.2;
                        norm = Clamp(norm, 0.0, 1.0);
                        state.brush.hardness = 0.1 + norm * (2.0 - 0.1);
                        InvalidateRect(hWnd, &ui.rSub, FALSE);
                        return true;
                    }
                }

                if (PtIn(ui.rSubSmall, pt)) {
                    state.brush.type = Brush::Small;
                    InvalidateRect(hWnd, &ui.rSub, FALSE);
                    return true;
                } else if (PtIn(ui.rSubMedium, pt)) {
                    state.brush.type = Brush::Medium;
                    InvalidateRect(hWnd, &ui.rSub, FALSE);
                    return true;
                } else if (PtIn(ui.rSubLarge, pt)) {
                    state.brush.type = Brush::Large;
                    InvalidateRect(hWnd, &ui.rSub, FALSE);
                    return true;
                } else if (PtIn(ui.rSubCalibBtn, pt)) {
                    gpuInk.Clear();
                    state.trajectory.Clear();
                    state.replay.Reset();
                    state.ink.Refill();
                    state.calibration.Start();
                    ui.suppressPenUntilLift = true;
                    InvalidateRect(hWnd, NULL, FALSE);
                    return true;
                }
            } else if (ui.leftTab == LeftTab::Paper) {
                // 下敷き・升目切り替え
                for (int i = 0; i < GRID_PATTERN_COUNT; ++i) {
                    if (PtIn(ui.rGridTile[i], pt)) {
                        state.paper.gridPattern = static_cast<GridPattern>(i);
                        // 升目のセル分割は Layout で算出しているため、
                        // パターン変更時は再レイアウトが必要
                        state.Layout(w, h);
                        state.otehon.ClearCellChars();
                        InvalidateRect(hWnd, NULL, FALSE);
                        return true;
                    }
                }

                // 罫線配色切り替え
                for (int i = 0; i < 3; ++i) {
                    if (PtIn(ui.rColorBtn[i], pt)) {
                        state.paper.gridColor = static_cast<GridColorTheme>(i);
                        InvalidateRect(hWnd, NULL, FALSE);
                        return true;
                    }
                }
            } else if (ui.leftTab == LeftTab::Save) {
                if (PtIn(ui.rSaveBtnPng, pt)) {
                    ImageExporter::ExportCanvas(hWnd, gpuInk, state, false);
                    InvalidateRect(hWnd, &ui.rSub, FALSE);
                    return true;
                } else if (PtIn(ui.rSaveBtnClip, pt)) {
                    ImageExporter::ExportCanvas(hWnd, gpuInk, state, true);
                    InvalidateRect(hWnd, &ui.rSub, FALSE);
                    return true;
                } else if (PtIn(ui.rSaveBtnJson, pt)) {
                    if (TrajectorySession::PromptSaveArchiveJson(hWnd, state.trajectory, state.paper.type, state.brush.type, state.brush.hardness)) {
                        state.SetSaveFeedback(L"✓ 運筆アーカイブ(JSON)を保存しました");
                    }
                    InvalidateRect(hWnd, &ui.rSub, FALSE);
                    return true;
                } else if (PtIn(ui.rSaveBtnCsv, pt)) {
                    if (TrajectorySession::PromptSaveArchiveCsv(hWnd, state.trajectory)) {
                        state.SetSaveFeedback(L"✓ 運筆データ(CSV)を出力しました");
                    }
                    InvalidateRect(hWnd, &ui.rSub, FALSE);
                    return true;
                }
            } else if (ui.leftTab == LeftTab::Otehon) {
                if (PtIn(ui.rOtehonToggleBtn, pt)) {
                    state.otehon.isVisible = !state.otehon.isVisible;
                    InvalidateRect(hWnd, NULL, FALSE);
                    return true;
                }

                // 書体の切り替え（楷書 / 教科書体 / 行書）
                for (int i = 0; i < OTEHON_FONT_COUNT; ++i) {
                    if (PtIn(ui.rOtehonFontBtn[i], pt)) {
                        state.otehon.fontStyle = static_cast<OtehonFontStyle>(i);
                        InvalidateRect(hWnd, NULL, FALSE);
                        return true;
                    }
                }

                // 配置: ミニマップのマスへ、選択中の文字を置く（同じ字なら取り消し）
                for (int i = 0; i < ui.gridCellCount; ++i) {
                    if (PtIn(ui.rOtehonCellBtn[i], pt)) {
                        state.otehon.PlaceAtCell(i, state.otehon.GetCurrentCharacter());
                        state.otehon.isVisible = true;
                        InvalidateRect(hWnd, NULL, FALSE);
                        return true;
                    }
                }

                // 文字入力欄
                if (PtIn(ui.rOtehonInputBox, pt)) {
                    state.otehon.isTyping = true;
                    SetOtehonImePosition(hWnd, ui.rOtehonInputBox);
                    InvalidateRect(hWnd, &ui.rSub, FALSE);
                    return true;
                }

                for (int i = 0; i < state.otehon.GetPaletteCount() && i < 8; ++i) {
                    if (PtIn(ui.rOtehonTile[i], pt)) {
                        state.otehon.selectedIndex = i;
                        state.otehon.isVisible = true;
                        InvalidateRect(hWnd, NULL, FALSE);
                        return true;
                    }
                }

                if (RW(ui.rOtehonOpacityTrack) > 0) {
                    RECT hitBox = ui.rOtehonOpacityTrack;
                    hitBox.top -= 6; hitBox.bottom += 6;
                    if (PtIn(hitBox, pt)) {
                        state.otehon.isDraggingOpacity = true;
                        SetCapture(hWnd);
                        double trackW = static_cast<double>(RW(ui.rOtehonOpacityTrack));
                        double norm = (trackW > 0.0) ? static_cast<double>(pt.x - ui.rOtehonOpacityTrack.left) / trackW : 0.35;
                        state.otehon.opacity = Clamp(norm, 0.05, 1.0);
                        InvalidateRect(hWnd, NULL, FALSE);
                        return true;
                    }
                }
            }
            return true;
        }
    }

    // 4. 右側 硯・墨補充・一画戻す・一画復元・全消し
    if (PtIn(ui.rInkStoneLarge, pt) || PtIn(ui.rInkRefillBtn, pt)) {
        RefillByPress(state, penPressure);
        InvalidateRect(hWnd, &ui.rRight, FALSE);
        return true;
    } else if (PtIn(ui.rUndoBtn, pt)) {
        UndoStroke(hWnd, state, gpuInk);
        return true;
    } else if (PtIn(ui.rRedoBtn, pt)) {
        RedoStroke(hWnd, state, gpuInk);
        return true;
    } else if (PtIn(ui.rClearAllBtn, pt)) {
        ui.showClearConfirm = true;
        InvalidateRect(hWnd, NULL, FALSE);
        return true;
    } else if (PtIn(ui.rPaperOnlyBtn, pt)) {
        SetPaperOnly(hWnd, true, state, gpuInk);
        return true;
    }

    return false;
}

bool AppController::OnLButtonUp(HWND hWnd, POINT pt, AppState& state) {
    bool handled = false;
    if (state.brush.isDraggingHardness) {
        state.brush.isDraggingHardness = false;
        ReleaseCapture();
        handled = true;
    }
    if (state.otehon.isDraggingOpacity) {
        state.otehon.isDraggingOpacity = false;
        ReleaseCapture();
        handled = true;
    }
    if (state.replay.isDraggingSeekBar) {
        state.replay.isDraggingSeekBar = false;
        ReleaseCapture();
        handled = true;
    }
    if (state.replay.isDraggingWaveform) {
        state.replay.isDraggingWaveform = false;
        ReleaseCapture();
        handled = true;
    }
    return handled;
}

bool AppController::OnChar(HWND hWnd, wchar_t ch, AppState& state) {
    if (!state.otehon.isTyping) return false;

    switch (ch) {
    case L'\b':
        state.otehon.BackspaceInput();
        break;
    case L'\r':
    case L'\n':
    case 0x1B: // Esc
        state.otehon.isTyping = false;
        break;
    default:
        state.otehon.AppendInputChar(ch);
        break;
    }

    InvalidateRect(hWnd, NULL, FALSE);
    return true;
}

bool AppController::OnMouseMove(HWND hWnd, POINT pt, WPARAM wParam, AppState& state) {
    UIState& ui = state.ui;
    RECT clientRect = { 0, 0, 0, 0 };
    GetClientRect(hWnd, &clientRect);
    int w = clientRect.right - clientRect.left;
    int h = clientRect.bottom - clientRect.top;

    // タイトル画面表示中はマウス移動での追加処理なし
    if (state.currentScreen == AppScreen::Title) {
        return true;
    }

    // 紙だけ表示中は半紙の右の3ボタンだけがホバー対象
    if (ui.paperOnly) {
        // 全消し確認モーダルが開いている間は、そのボタンだけを見る
        if (ui.showClearConfirm) {
            int oldModal = ui.hoverClearModal;
            POINT mp = state.ToClearModalSpace(pt, h);
            ui.hoverClearModal = 0;
            if (PtIn(ui.rModalClearBtn, mp)) ui.hoverClearModal = 1;
            else if (PtIn(ui.rModalCancelBtn, mp)) ui.hoverClearModal = 2;
            if (oldModal != ui.hoverClearModal) InvalidateRect(hWnd, NULL, FALSE);
            return true;
        }

        int oldPaperOnly = ui.hoverPaperOnly;
        ui.hoverPaperOnly = 0;
        if (PtIn(ui.rPaperOnlyExitBtn, pt)) ui.hoverPaperOnly = 1;
        else if (PtIn(ui.rInkRefillBtn, pt)) ui.hoverPaperOnly = 2;
        else if (PtIn(ui.rPaperOnlyClearBtn, pt)) ui.hoverPaperOnly = 3;
        if (oldPaperOnly != ui.hoverPaperOnly) {
            InvalidateRect(hWnd, &ui.rPaperOnlyExitBtn, FALSE);
            InvalidateRect(hWnd, &ui.rInkRefillBtn, FALSE);
            InvalidateRect(hWnd, &ui.rPaperOnlyClearBtn, FALSE);
        }
        return true;
    }

    // スライダードラッグ中の更新
    if (state.brush.isDraggingHardness && (wParam & MK_LBUTTON)) {
        double trackW = static_cast<double>(RW(ui.rHardnessTrack));
        double norm = (trackW > 0.0) ? static_cast<double>(pt.x - ui.rHardnessTrack.left) / trackW : 0.2;
        norm = Clamp(norm, 0.0, 1.0);
        state.brush.hardness = 0.1 + norm * (2.0 - 0.1);
        InvalidateRect(hWnd, &ui.rSub, FALSE);
        return true;
    }

    if (state.otehon.isDraggingOpacity && (wParam & MK_LBUTTON)) {
        double trackW = static_cast<double>(RW(ui.rOtehonOpacityTrack));
        double norm = (trackW > 0.0) ? static_cast<double>(pt.x - ui.rOtehonOpacityTrack.left) / trackW : 0.35;
        state.otehon.opacity = Clamp(norm, 0.05, 1.0);
        InvalidateRect(hWnd, NULL, FALSE);
        return true;
    }

    if (state.replay.isDraggingSeekBar && (wParam & MK_LBUTTON)) {
        double trackW = static_cast<double>(RW(ui.rReplaySeekTrack));
        double norm = (trackW > 0.0) ? static_cast<double>(pt.x - ui.rReplaySeekTrack.left) / trackW : 0.0;
        norm = Clamp(norm, 0.0, 1.0);
        state.replay.currentTimeMs = static_cast<DWORD>(state.replay.totalDurationMs * norm);
        state.replay.hasValidSample = state.trajectory.GetReplaySample(state.replay.currentTimeMs, state.ui.rPaper, state.replay.currentSample);
        state.UpdateReplaySeekThumb();
        InvalidateRect(hWnd, &ui.rPaper, FALSE);
        InvalidateRect(hWnd, &ui.rSub, FALSE);
        return true;
    }

    if (state.replay.isDraggingWaveform && (wParam & MK_LBUTTON)) {
        RECT rPlotHit = { ui.rAnalysisGraphBox.left + 38, ui.rAnalysisGraphBox.top + 32, ui.rAnalysisGraphBox.right - 14, ui.rAnalysisGraphBox.bottom - 20 };
        double plotW = static_cast<double>(RW(rPlotHit));
        double norm = (plotW > 0.0) ? static_cast<double>(pt.x - rPlotHit.left) / plotW : 0.0;
        norm = Clamp(norm, 0.0, 1.0);
        state.replay.currentTimeMs = static_cast<DWORD>(state.replay.totalDurationMs * norm);
        state.replay.hasValidSample = state.trajectory.GetReplaySample(state.replay.currentTimeMs, state.ui.rPaper, state.replay.currentSample);
        state.UpdateReplaySeekThumb();
        InvalidateRect(hWnd, &ui.rPaper, FALSE);
        InvalidateRect(hWnd, &ui.rSub, FALSE);
        return true;
    }

    // ホバー状態の更新
    TbButton oldTb = ui.hoverTb;
    int oldSub = ui.hoverSub;
    int oldStone = ui.hoverInkStone;
    int oldModal = ui.hoverClearModal;
    int oldCalib = ui.hoverCalib;
    int oldReplay = ui.hoverReplayBtn;

    ui.hoverTb = TbButton::None;
    ui.hoverSub = 0;
    ui.hoverInkStone = 0;
    ui.hoverClearModal = 0;
    ui.hoverCalib = 0;
    ui.hoverReplayBtn = 0;

    if (ui.showClearConfirm) {
        if (PtIn(ui.rModalClearBtn, pt)) ui.hoverClearModal = 1;
        else if (PtIn(ui.rModalCancelBtn, pt)) ui.hoverClearModal = 2;
    } else if (state.calibration.IsResult()) {
        if (PtIn(ui.rCalibApplyBtn, pt)) ui.hoverCalib = 1;
        else if (PtIn(ui.rCalibRetryBtn, pt)) ui.hoverCalib = 2;
        else if (PtIn(ui.rCalibCloseBtn, pt)) ui.hoverCalib = 3;
    } else {
        if (PtIn(ui.rTbHomeBtn, pt)) ui.hoverTb = TbButton::Home;
        else if (PtIn(ui.rTbNavToggle, pt)) ui.hoverTb = TbButton::NavToggle;
        else if (ui.isSubPanelOpen) {
            if (PtIn(ui.rTbBrush, pt)) ui.hoverTb = TbButton::Brush;
            else if (PtIn(ui.rTbPaper, pt)) ui.hoverTb = TbButton::Paper;
            else if (PtIn(ui.rTbAnalysis, pt)) ui.hoverTb = TbButton::Analysis;
            else if (PtIn(ui.rTbSave, pt)) ui.hoverTb = TbButton::Save;
            else if (PtIn(ui.rTbOtehon, pt)) ui.hoverTb = TbButton::Otehon;

            if (PtIn(ui.rSub, pt)) {
                if (ui.leftTab == LeftTab::Analysis) {
                    bool hasRecording = state.trajectory.GetTotalStrokeCount() > 0;
                    if (hasRecording && PtIn(ui.rReplayResetBtn, pt)) ui.hoverReplayBtn = 1;
                    else if (hasRecording && PtIn(ui.rReplayPrevBtn, pt)) ui.hoverReplayBtn = 2;
                    else if (hasRecording && PtIn(ui.rReplayPlayBtn, pt)) ui.hoverReplayBtn = 3;
                    else if (hasRecording && PtIn(ui.rReplayNextBtn, pt)) ui.hoverReplayBtn = 4;
                    else if (PtIn(ui.rReplaySpeedBtn[0], pt)) ui.hoverReplayBtn = 5;
                    else if (PtIn(ui.rReplaySpeedBtn[1], pt)) ui.hoverReplayBtn = 6;
                    else if (PtIn(ui.rReplaySpeedBtn[2], pt)) ui.hoverReplayBtn = 7;
                    else if (hasRecording) {
                        RECT rSeekH = ui.rReplaySeekTrack; rSeekH.top -= 6; rSeekH.bottom += 6;
                        if (PtIn(rSeekH, pt) || PtIn(ui.rReplaySeekThumb, pt)) ui.hoverReplayBtn = 8;
                    }
                } else if (ui.leftTab == LeftTab::Brush) {
                    if (PtIn(ui.rSubSmall, pt)) ui.hoverSub = 1;
                    else if (PtIn(ui.rSubMedium, pt)) ui.hoverSub = 2;
                    else if (PtIn(ui.rSubLarge, pt)) ui.hoverSub = 3;
                    else if (PtIn(ui.rSubCalibBtn, pt)) ui.hoverSub = 4;
                } else if (ui.leftTab == LeftTab::Paper) {
                    for (int i = 0; i < GRID_PATTERN_COUNT; ++i) if (PtIn(ui.rGridTile[i], pt)) ui.hoverSub = 10 + i;
                    for (int i = 0; i < 3; ++i) if (PtIn(ui.rColorBtn[i], pt)) ui.hoverSub = 30 + i;
                } else if (ui.leftTab == LeftTab::Save) {
                    if (PtIn(ui.rSaveBtnPng, pt)) ui.hoverSub = 60;
                    else if (PtIn(ui.rSaveBtnClip, pt)) ui.hoverSub = 61;
                    else if (PtIn(ui.rSaveBtnJson, pt)) ui.hoverSub = 62;
                    else if (PtIn(ui.rSaveBtnCsv, pt)) ui.hoverSub = 63;
                } else if (ui.leftTab == LeftTab::Otehon) {
                    if (PtIn(ui.rOtehonToggleBtn, pt)) ui.hoverSub = 70;
                    if (PtIn(ui.rOtehonInputBox, pt)) ui.hoverSub = 73;
                    for (int i = 0; i < OTEHON_FONT_COUNT; ++i) {
                        if (PtIn(ui.rOtehonFontBtn[i], pt)) ui.hoverSub = 74 + i;
                    }
                    for (int i = 0; i < ui.gridCellCount; ++i) if (PtIn(ui.rOtehonCellBtn[i], pt)) ui.hoverSub = 90 + i;
                    for (int i = 0; i < state.otehon.GetPaletteCount() && i < 8; ++i) {
                        if (PtIn(ui.rOtehonTile[i], pt)) ui.hoverSub = 80 + i;
                    }
                }
            }
        }

        if (PtIn(ui.rInkRefillBtn, pt)) ui.hoverInkStone = 2;
        else if (PtIn(ui.rClearAllBtn, pt)) ui.hoverInkStone = 3;
        else if (PtIn(ui.rUndoBtn, pt)) ui.hoverInkStone = 4;
        else if (PtIn(ui.rRedoBtn, pt)) ui.hoverInkStone = 5;
        else if (PtIn(ui.rPaperOnlyBtn, pt)) ui.hoverInkStone = 6;
        else if (PtIn(ui.rInkStoneLarge, pt)) ui.hoverInkStone = 1;
    }

    if (oldTb != ui.hoverTb || oldSub != ui.hoverSub || oldStone != ui.hoverInkStone || oldModal != ui.hoverClearModal || oldCalib != ui.hoverCalib || oldReplay != ui.hoverReplayBtn) {
        InvalidateRect(hWnd, NULL, FALSE);
    }

    return true;
}

