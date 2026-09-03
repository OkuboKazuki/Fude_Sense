#include "stdafx.h"
#include "AppController.h"
#include "RenderUtils.h"
#include "ImageExporter.h"

using namespace RenderUtils;

void AppController::ClearAllInk(HWND hWnd, AppState& state, GpuInk& gpuInk) {
    gpuInk.Clear();
    state.trajectory.Clear();
    state.ui.showClearConfirm = false;
    // 消去した瞬間にペンが半紙へ接地したままだと、直後のパケットで墨が落ちてしまう。
    // ペンが紙から離れるまで運筆入力をロックする。
    state.ui.suppressPenUntilLift = true;
    InvalidateRect(hWnd, NULL, FALSE);
}

void AppController::OnSize(HWND hWnd, int width, int height, AppState& state, GpuInk& gpuInk) {
    state.Layout(width, height);
    int pw = RW(state.ui.rPaper);
    int ph = RH(state.ui.rPaper);
    if (pw > 0 && ph > 0) {
        gpuInk.Resize(pw, ph);
    }
    InvalidateRect(hWnd, NULL, FALSE);
}

bool AppController::OnLButtonDown(HWND hWnd, POINT pt, AppState& state, GpuInk& gpuInk) {
    UIState& ui = state.ui;
    RECT clientRect = { 0, 0, 0, 0 };
    GetClientRect(hWnd, &clientRect);
    int w = clientRect.right - clientRect.left;
    int h = clientRect.bottom - clientRect.top;

    // 1. 全消し確認モーダル表示中のクリック
    if (ui.showClearConfirm) {
        // モーダルは半紙の上に重なっている。どのボタンを押した場合でも、
        // モーダルを閉じた直後に接地したままのペンが運筆を始めないようロックする。
        ui.suppressPenUntilLift = true;
        if (PtIn(ui.rModalClearBtn, pt)) {
            ClearAllInk(hWnd, state, gpuInk);
            return true;
        } else if (PtIn(ui.rModalCancelBtn, pt) || !PtIn(ui.rClearModalBox, pt)) {
            ui.showClearConfirm = false;
            InvalidateRect(hWnd, NULL, FALSE);
            return true;
        }
        return true;
    }

    // 1.5 筆圧キャリブレーション結果モーダル表示中のクリック
    if (state.calibration.IsResult()) {
        ui.suppressPenUntilLift = true;
        if (PtIn(ui.rCalibApplyBtn, pt)) {
            state.brush.hardness = state.calibration.GetResult().recommendedHardness;
            state.calibration.Reset();
            gpuInk.Clear();
            state.trajectory.Clear();
            state.ink.Refill();
            state.SetSaveFeedback(L"✓ 筆の硬さを自動調整しました");
            InvalidateRect(hWnd, NULL, FALSE);
            return true;
        } else if (PtIn(ui.rCalibRetryBtn, pt)) {
            gpuInk.Clear();
            state.trajectory.Clear();
            state.ink.Refill();
            state.calibration.Start();
            InvalidateRect(hWnd, NULL, FALSE);
            return true;
        } else if (PtIn(ui.rCalibCloseBtn, pt) || !PtIn(ui.rCalibModalBox, pt)) {
            state.calibration.Reset();
            gpuInk.Clear();
            state.trajectory.Clear();
            state.ink.Refill();
            InvalidateRect(hWnd, NULL, FALSE);
            return true;
        }
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
            InvalidateRect(hWnd, NULL, FALSE);
            return true;
        } else if (PtIn(ui.rTbPaper, pt)) {
            ui.leftTab = LeftTab::Paper;
            InvalidateRect(hWnd, NULL, FALSE);
            return true;
        } else if (PtIn(ui.rTbAnalysis, pt)) {
            ui.leftTab = LeftTab::Analysis;
            InvalidateRect(hWnd, NULL, FALSE);
            return true;
        } else if (PtIn(ui.rTbSave, pt)) {
            ui.leftTab = LeftTab::Save;
            InvalidateRect(hWnd, NULL, FALSE);
            return true;
        } else if (PtIn(ui.rTbOtehon, pt)) {
            ui.leftTab = LeftTab::Otehon;
            InvalidateRect(hWnd, NULL, FALSE);
            return true;
        }

        // 詳細パネル内部の操作
        if (PtIn(ui.rSub, pt)) {
            if (ui.leftTab == LeftTab::Brush) {
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
                    InvalidateRect(hWnd, &ui.rStatus, FALSE);
                    return true;
                } else if (PtIn(ui.rSubMedium, pt)) {
                    state.brush.type = Brush::Medium;
                    InvalidateRect(hWnd, &ui.rSub, FALSE);
                    InvalidateRect(hWnd, &ui.rStatus, FALSE);
                    return true;
                } else if (PtIn(ui.rSubLarge, pt)) {
                    state.brush.type = Brush::Large;
                    InvalidateRect(hWnd, &ui.rSub, FALSE);
                    InvalidateRect(hWnd, &ui.rStatus, FALSE);
                    return true;
                } else if (PtIn(ui.rSubCalibBtn, pt)) {
                    gpuInk.Clear();
                    state.trajectory.Clear();
                    state.ink.Refill();
                    state.calibration.Start();
                    ui.suppressPenUntilLift = true;
                    InvalidateRect(hWnd, NULL, FALSE);
                    return true;
                }
            } else if (ui.leftTab == LeftTab::Paper) {
                // 用紙種類切り替え
                for (int i = 0; i < 4; ++i) {
                    if (PtIn(ui.rPaperTile[i], pt)) {
                        state.paper.type = static_cast<PaperType>(i);
                        state.Layout(w, h);
                        int pw = RW(ui.rPaper);
                        int ph = RH(ui.rPaper);
                        if (pw > 0 && ph > 0) gpuInk.Resize(pw, ph);
                        InvalidateRect(hWnd, NULL, FALSE);
                        return true;
                    }
                }

                // 下敷き・升目切り替え
                for (int i = 0; i < 9; ++i) {
                    if (PtIn(ui.rGridTile[i], pt)) {
                        state.paper.gridPattern = static_cast<GridPattern>(i);
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

                for (int i = 0; i < 8; ++i) {
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

    // 4. 右側 硯・墨補充・全消し
    if (PtIn(ui.rInkStoneLarge, pt) || PtIn(ui.rInkRefillBtn, pt)) {
        state.ink.Refill();
        InvalidateRect(hWnd, &ui.rRight, FALSE);
        InvalidateRect(hWnd, &ui.rStatus, FALSE);
        return true;
    } else if (PtIn(ui.rClearAllBtn, pt)) {
        ui.showClearConfirm = true;
        InvalidateRect(hWnd, NULL, FALSE);
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
    return handled;
}

bool AppController::OnMouseMove(HWND hWnd, POINT pt, WPARAM wParam, AppState& state) {
    UIState& ui = state.ui;

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

    // ホバー状態の更新
    TbButton oldTb = ui.hoverTb;
    int oldSub = ui.hoverSub;
    int oldStone = ui.hoverInkStone;
    int oldModal = ui.hoverClearModal;
    int oldCalib = ui.hoverCalib;

    ui.hoverTb = TbButton::None;
    ui.hoverSub = 0;
    ui.hoverInkStone = 0;
    ui.hoverClearModal = 0;
    ui.hoverCalib = 0;

    if (ui.showClearConfirm) {
        if (PtIn(ui.rModalClearBtn, pt)) ui.hoverClearModal = 1;
        else if (PtIn(ui.rModalCancelBtn, pt)) ui.hoverClearModal = 2;
    } else if (state.calibration.IsResult()) {
        if (PtIn(ui.rCalibApplyBtn, pt)) ui.hoverCalib = 1;
        else if (PtIn(ui.rCalibRetryBtn, pt)) ui.hoverCalib = 2;
        else if (PtIn(ui.rCalibCloseBtn, pt)) ui.hoverCalib = 3;
    } else {
        if (PtIn(ui.rTbNavToggle, pt)) ui.hoverTb = TbButton::NavToggle;
        else if (ui.isSubPanelOpen) {
            if (PtIn(ui.rTbBrush, pt)) ui.hoverTb = TbButton::Brush;
            else if (PtIn(ui.rTbPaper, pt)) ui.hoverTb = TbButton::Paper;
            else if (PtIn(ui.rTbAnalysis, pt)) ui.hoverTb = TbButton::Analysis;
            else if (PtIn(ui.rTbSave, pt)) ui.hoverTb = TbButton::Save;
            else if (PtIn(ui.rTbOtehon, pt)) ui.hoverTb = TbButton::Otehon;

            if (PtIn(ui.rSub, pt)) {
                if (ui.leftTab == LeftTab::Brush) {
                    if (PtIn(ui.rSubSmall, pt)) ui.hoverSub = 1;
                    else if (PtIn(ui.rSubMedium, pt)) ui.hoverSub = 2;
                    else if (PtIn(ui.rSubLarge, pt)) ui.hoverSub = 3;
                    else if (PtIn(ui.rSubCalibBtn, pt)) ui.hoverSub = 4;
                } else if (ui.leftTab == LeftTab::Paper) {
                    for (int i = 0; i < 4; ++i) if (PtIn(ui.rPaperTile[i], pt)) ui.hoverSub = 50 + i;
                    for (int i = 0; i < 9; ++i) if (PtIn(ui.rGridTile[i], pt)) ui.hoverSub = 10 + i;
                    for (int i = 0; i < 3; ++i) if (PtIn(ui.rColorBtn[i], pt)) ui.hoverSub = 30 + i;
                } else if (ui.leftTab == LeftTab::Save) {
                    if (PtIn(ui.rSaveBtnPng, pt)) ui.hoverSub = 60;
                    else if (PtIn(ui.rSaveBtnClip, pt)) ui.hoverSub = 61;
                    else if (PtIn(ui.rSaveBtnJson, pt)) ui.hoverSub = 62;
                    else if (PtIn(ui.rSaveBtnCsv, pt)) ui.hoverSub = 63;
                } else if (ui.leftTab == LeftTab::Otehon) {
                    if (PtIn(ui.rOtehonToggleBtn, pt)) ui.hoverSub = 70;
                    for (int i = 0; i < 8; ++i) if (PtIn(ui.rOtehonTile[i], pt)) ui.hoverSub = 80 + i;
                }
            }
        }

        if (PtIn(ui.rInkRefillBtn, pt)) ui.hoverInkStone = 2;
        else if (PtIn(ui.rClearAllBtn, pt)) ui.hoverInkStone = 3;
        else if (PtIn(ui.rInkStoneLarge, pt)) ui.hoverInkStone = 1;
    }

    if (oldTb != ui.hoverTb || oldSub != ui.hoverSub || oldStone != ui.hoverInkStone || oldModal != ui.hoverClearModal || oldCalib != ui.hoverCalib) {
        InvalidateRect(hWnd, NULL, FALSE);
    }

    return true;
}
