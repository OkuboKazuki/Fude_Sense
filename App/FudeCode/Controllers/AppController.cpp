#include "stdafx.h"
#include "AppController.h"
#include "RenderUtils.h"
#include "ScreenLayout.h"
#include "ImageExporter.h"
#include "ArchiveDialogs.h"
#include <imm.h>
#include <cmath>
#include <new>
#include <utility>

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

// 作品を書き出し、結果を保存タブのメッセージとして出す
static void ExportCanvasWithFeedback(HWND hWnd, GpuInk& gpuInk, AppState& state, bool toClipboard) {
    switch (ImageExporter::ExportCanvas(hWnd, gpuInk, state, toClipboard)) {
    case ImageExportResult::NothingToExport:
        break;
    case ImageExportResult::Failed:
        state.SetSaveFeedback(toClipboard ? L"× クリップボードにコピーできませんでした"
                                          : L"× 画像を保存できませんでした", true);
        break;
    case ImageExportResult::CopiedToClipboard:
        state.SetSaveFeedback(L"✓ クリップボードにコピーしました");
        break;
    case ImageExportResult::SavedPng:
        state.SetSaveFeedback(L"✓ デスクトップにPNG保存しました");
        break;
    case ImageExportResult::SavedBmp:
        state.SetSaveFeedback(L"✓ デスクトップに保存しました(BMP)");
        break;
    }
}

// 画面上の点を全消し確認モーダルの座標系へ移す。
// 紙だけ表示は画面を右回りに90度倒して見るので、倒した向きの右が画面の上、
// 下が画面の右になる。モーダルはその向きで組んであるため、点も同じ向きへ直す。
static POINT ToClearModalSpace(const UIState& ui, POINT pt, int clientHeight) {
    if (!ui.paperOnly) return pt;
    return { clientHeight - pt.y, pt.x };
}

// 墨残量ゲージ上の横位置 x に合わせて墨の残量を決める（左端が空、右端が満タン）。
// ゲージの両端は丸く、端ぴったりは狙いにくいので、少し内側で 0% / 100% に届くようにする
static void SetInkAmountFromBadgeX(AppState& state, int x) {
    const UIState& ui = state.ui;
    int pad = RH(ui.rInkBadge) / 2;
    double trackW = static_cast<double>(RW(ui.rInkBadge) - 2 * pad);
    if (trackW <= 0.0) return;
    double norm = static_cast<double>(x - ui.rInkBadge.left - pad) / trackW;
    state.ink.amount = Clamp(norm, 0.0, 1.0) * INK_MAX_VALUE;
}

// 点が硯の下の部分（磨り面とそのまわりの縁）にあるか。硯の残りの上の部分は補充に使う。
// 硯を磨り面の上端で上下に分ける。縁も近いほうへ含めるので、硯の中に押しても効かない所は残らない。
static bool IsOnInkStoneLand(const UIState& ui, POINT pt) {
    return PtIn(ui.rInkStoneLarge, pt) && pt.y >= ui.rInkLand.top;
}

// しごく長さは硯の幅を単位にする。硯パネルの拡大率が変わっても、
// 磨り面を端から端までしごいたときに落ちる量が同じになる。
// 数えるのは下の部分を通っている間だけ。上の墨溜まりや硯の外へ出ている間は減らさない。
bool AppController::WipeInkTo(POINT pt, double pressure, AppState& state) {
    UIState& ui = state.ui;
    double dx = static_cast<double>(pt.x - ui.wipeLastPt.x);
    double dy = static_cast<double>(pt.y - ui.wipeLastPt.y);
    ui.wipeLastPt = pt;

    int stoneW = RW(ui.rInkStoneLarge);
    if (stoneW <= 0 || !IsOnInkStoneLand(ui, pt)) return false;

    double before = state.ink.amount;
    state.ink.Wipe(std::sqrt(dx * dx + dy * dy) / stoneW, pressure);
    return state.ink.amount != before;
}

void AppController::ClearAllInk(HWND hWnd, AppState& state, GpuInk& gpuInk) {
    gpuInk.Clear();
    state.trajectory.Clear();
    state.undo.Clear();
    state.replay.Reset();

    // 読み込んだ記録を見ている間であれば、インポート状態を解除する
    if (state.viewingImport) {
        if (state.replay.state == ReplayState::Playing) {
            state.replay.state = ReplayState::Paused;
            KillTimer(hWnd, REPLAY_TIMER_ID);
        }
        state.viewingImport = false;
        state.importedName.clear();
        state.importedTrajectory = TrajectorySession();
    }

    // 全消し確定時は「筆」タブへ移動
    state.ui.leftTab = LeftTab::Brush;
    state.replay.state = ReplayState::Stopped;

    state.ui.showClearConfirm = false;
    // 消去した瞬間にペンが半紙へ接地したままだと、直後のパケットで墨が落ちてしまう。
    // ペンが紙から離れるまで運筆入力をロックする。
    state.ui.suppressPenUntilLift = true;
    // シークバーのツマミ位置は再生時刻から Layout で決まる。全消しで時刻を 0 へ戻した後に
    // 取り直さないと、ツマミだけ古い位置に残る。
    RECT rcClient = { 0, 0, 0, 0 };
    GetClientRect(hWnd, &rcClient);
    if (rcClient.right > 0 && rcClient.bottom > 0) {
        ScreenLayout::Layout(state.ui, state.paper, state.viewingImport, state.replay, rcClient.right - rcClient.left, rcClient.bottom - rcClient.top);
    }
    InvalidateRect(hWnd, NULL, FALSE);
}

void AppController::OnSize(HWND hWnd, int width, int height, AppState& state, GpuInk& gpuInk) {
    ScreenLayout::Layout(state.ui, state.paper, state.viewingImport, state.replay, width, height);
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

    state.AnalysisSession().BuildReplayTimeline();
    state.replay.totalDurationMs = state.AnalysisSession().GetReplayTotalDurationMs();
    if (state.replay.currentTimeMs > state.replay.totalDurationMs) {
        state.replay.currentTimeMs = state.replay.totalDurationMs;
    }
    state.replay.hasValidSample = state.AnalysisSession().GetReplaySample(
        state.replay.currentTimeMs, state.ui.rPaper, state.replay.currentSample);
    ScreenLayout::UpdateReplaySeekThumb(state.ui, state.replay);
}

// 解析の対象を切り替えた直後の表示。書き上がった状態（末尾）で一時停止にする
static void ShowAnalysisSessionFromEnd(HWND hWnd, AppState& state) {
    RECT rc = { 0, 0, 0, 0 };
    GetClientRect(hWnd, &rc);
    // 「自分の記録に戻る」ボタンの有無で解析タブの並びが変わる
    ScreenLayout::Layout(state.ui, state.paper, state.viewingImport, state.replay, rc.right - rc.left, rc.bottom - rc.top);

    AppController::SyncReplayTimeline(hWnd, state);
    state.replay.state = ReplayState::Paused;
    state.replay.currentTimeMs = state.replay.totalDurationMs;
    state.replay.hasValidSample = state.AnalysisSession().GetReplaySample(
        state.replay.currentTimeMs, state.ui.rPaper, state.replay.currentSample);
    ScreenLayout::UpdateReplaySeekThumb(state.ui, state.replay);
    InvalidateRect(hWnd, NULL, FALSE);
}

void AppController::ImportArchive(HWND hWnd, AppState& state) {
    if (state.replay.state == ReplayState::Playing) {
        state.replay.state = ReplayState::Paused;
        KillTimer(hWnd, REPLAY_TIMER_ID);
    }

    // 読み込みに失敗したときは、表示中の記録（前に読み込んだものを含む）に手を付けない
    std::wstring fileName, error;
    switch (ArchiveDialogs::PromptLoad(hWnd, state.importedTrajectory, state.ui.rPaper, fileName, error)) {
    case ArchiveDialogs::Result::Saved:
        state.viewingImport = true;
        state.importedName = fileName;
        ShowAnalysisSessionFromEnd(hWnd, state);
        break;
    case ArchiveDialogs::Result::Failed: {
        std::wstring msg = L"運筆アーカイブを読み込めませんでした。";
        if (!error.empty()) msg += L"\n\n" + error;
        MessageBoxW(hWnd, msg.c_str(), L"運筆アーカイブの読み込み", MB_OK | MB_ICONWARNING);
        break;
    }
    case ArchiveDialogs::Result::Canceled:
        break;
    }
    // ダイアログを閉じたペンが接地したままでも、半紙に墨を落とさない
    state.ui.suppressPenUntilLift = true;
}

void AppController::CloseImportedArchive(HWND hWnd, AppState& state) {
    if (state.replay.state == ReplayState::Playing) {
        state.replay.state = ReplayState::Paused;
        KillTimer(hWnd, REPLAY_TIMER_ID);
    }
    state.viewingImport = false;
    state.importedName.clear();
    state.importedTrajectory = TrajectorySession();  // 読み込んだ記録のメモリを手放す
    ShowAnalysisSessionFromEnd(hWnd, state);
}

void AppController::SelectTab(HWND hWnd, AppState& state, LeftTab newTab) {
    if (state.ui.leftTab == newTab) {
        return;
    }

    // 解析メニューでインポートした後、別のタブに移動したときにインポート状態を解除
    if (state.viewingImport && newTab != LeftTab::Analysis) {
        CloseImportedArchive(hWnd, state);
    }

    state.ui.leftTab = newTab;

    if (newTab == LeftTab::Analysis) {
        SyncReplayTimeline(hWnd, state);
        // 解析タブ移動時は巻き戻された状態（0ms）で一時停止にする
        state.replay.currentTimeMs = 0;
        state.replay.hasValidSample = state.AnalysisSession().GetReplaySample(
            0, state.ui.rPaper, state.replay.currentSample);
        ScreenLayout::UpdateReplaySeekThumb(state.ui, state.replay);
        state.replay.state = ReplayState::Paused;
    } else {
        if (state.replay.state == ReplayState::Playing) {
            KillTimer(hWnd, REPLAY_TIMER_ID);
        }
        state.replay.state = ReplayState::Stopped;
    }

    RECT rc = { 0, 0, 0, 0 };
    GetClientRect(hWnd, &rc);
    if (rc.right > 0 && rc.bottom > 0) {
        ScreenLayout::Layout(state.ui, state.paper, state.viewingImport, state.replay, rc.right - rc.left, rc.bottom - rc.top);
    }
    InvalidateRect(hWnd, NULL, FALSE);
}

// 「一画戻す」「一画復元」用に、今の墨を控える。
static bool CaptureInkForUndo(GpuInk& gpuInk, UndoHistory& undo, InkSnapshot& out) {
    try {
        return gpuInk.CaptureSnapshot(out);
    }
    catch (const std::bad_alloc&) {
        // 32 ビットの空きが尽きた。控えを全部手放して次の画に備える。
        // 履歴が無いだけで運筆自体は続けられる。
        undo.Clear();
        return false;
    }
}

bool AppController::PushUndoBeforeStroke(GpuInk& gpuInk, AppState& state) {
    InkSnapshot current;
    if (!CaptureInkForUndo(gpuInk, state.undo, current)) return false;
    return state.undo.PushBeforeStroke(std::move(current), state.ink, state.trajectory.GetTotalStrokeCount());
}

// 直前の1画を取り消す。
// 墨は画素バッファへ破壊的に積み上がり、にじみも時間で進むため、運筆データから
// 描き直しても同じ絵にはならない。画を書き始める直前に控えておいた状態
// （UndoHistory）を書き戻す。
bool AppController::UndoStroke(HWND hWnd, AppState& state, GpuInk& gpuInk) {
    // 墨・墨残量・運筆アーカイブ（解析グラフ・JSON/CSV）をまとめて1画戻す
    if (!state.undo.CanUndo()) return false;

    // 戻す前の墨を、復元用に控えておく
    InkSnapshot current;
    if (!CaptureInkForUndo(gpuInk, state.undo, current)) return false;

    if (!gpuInk.RestoreSnapshot(state.undo.UndoTargetInk())) {
        // 半紙の寸法が変わっているなど、もう書き戻せない控えなので捨てる
        state.undo.Clear();
        return false;
    }
    state.undo.CommitUndo(std::move(current), state.ink, state.trajectory);

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
    if (!state.undo.CanRedo()) return false;

    // 復元する前の墨を、また戻せるように控えておく
    InkSnapshot current;
    if (!CaptureInkForUndo(gpuInk, state.undo, current)) return false;

    if (!gpuInk.RestoreSnapshot(state.undo.RedoTargetInk())) {
        state.undo.Clear();
        return false;
    }
    state.undo.CommitRedo(std::move(current), state.ink, state.trajectory);

    SyncReplayTimeline(hWnd, state);

    state.ui.suppressPenUntilLift = true;
    InvalidateRect(hWnd, NULL, FALSE);
    return true;
}

// 半紙の縦横比が変わった際の後始末。
// 新しい半紙の固定論理解像度で GpuInk を作り直す（白紙になる）。
// 運筆記録は呼び出し側で扱うのでここでは消さない。
static void RelayoutForPaper(HWND hWnd, AppState& state, GpuInk& gpuInk) {
    RECT rc = { 0, 0, 0, 0 };
    GetClientRect(hWnd, &rc);
    ScreenLayout::Layout(state.ui, state.paper, state.viewingImport, state.replay, rc.right - rc.left, rc.bottom - rc.top);

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
    }

    // 「一画戻す」の控えは元の縦横の寸法で持っているので、呼び出し側で新しい半紙へ回す
    state.ui.showClearConfirm = false;
    // ペンが接地したままでも、新しい半紙で運筆を始めないようロックする
    state.ui.suppressPenUntilLift = true;
    InvalidateRect(hWnd, NULL, FALSE);
}

// 紙だけ表示の出入り。
// 紙だけ表示の半紙は、通常表示の半紙を画面の上で左回りに90度倒したもの
// （画面を右回りに倒して見ると元の向きに戻る）。墨のバッファも縦横が入れ替わるので、
// 切り替える前の墨を控えておき、新しい半紙へ回して書き戻す。書いていた字はそのまま残る。
// 運筆記録も同じ向きへ回して残す（解析タブのリプレイは記録から描くため、消すと字が消える）。
// 「一画戻す」「一画復元」の控えも同じ向きへ回して残す（消すと横画面から戻った後に戻せなくなる）。
void AppController::SetPaperOnly(HWND hWnd, bool on, AppState& state, GpuInk& gpuInk) {
    if (state.ui.paperOnly == on) return;

    // 紙だけ表示ではリプレイもモーダルも描かないので、止めて閉じておく
    if (state.replay.state == ReplayState::Playing) {
        state.replay.state = ReplayState::Paused;
        KillTimer(hWnd, REPLAY_TIMER_ID);
    }
    state.otehon.isTyping = false;
    state.otehon.isDraggingOpacity = false;
    state.brush.isDraggingHardness = false;
    state.ui.isDraggingInkAmount = false;
    state.ui.hoverPaperOnly = 0;

    // 書いていた墨を控える（半紙を作り直すと消えるため）
    InkSnapshot keep;
    bool hasInk = gpuInk.CaptureSnapshot(keep);
    const RECT oldPaper = state.ui.rPaper;

    state.ui.paperOnly = on;

    RelayoutForPaper(hWnd, state, gpuInk);

    // 紙だけ表示へ入るときは左回り、戻るときは右回りに回して書き戻す
    bool restored = hasInk && gpuInk.RestoreSnapshotRotated(keep, on);
    if (restored) {
        InvalidateRect(hWnd, NULL, FALSE);
    }

    // 「一画戻す」「一画復元」の控えも同じ向きへ回して残す。
    // 墨を書き戻せなかった（白紙になった）ときは、控えと画面が噛み合わないので捨てる。
    if (restored) {
        const int canvasW = gpuInk.GetWidth();
        const int canvasH = gpuInk.GetHeight();
        state.undo.RotateQuarter(
            [on, canvasW, canvasH](InkSnapshot& snap) { return GpuInk::RotateSnapshot(snap, on, canvasW, canvasH); },
            on, oldPaper, state.ui.rPaper);
    } else {
        state.undo.Clear();
    }

    // 運筆記録も墨と同じ向きへ回し、リプレイのタイムラインと再生位置を取り直す。
    // 書き上がった状態（末尾）を見ていたなら、紙だけ表示で書き足した画も含めて末尾に合わせる。
    bool wasAtEnd = (state.replay.currentTimeMs >= state.replay.totalDurationMs);
    state.trajectory.RotateQuarter(on, oldPaper, state.ui.rPaper);
    SyncReplayTimeline(hWnd, state);
    if (wasAtEnd) {
        state.replay.currentTimeMs = state.replay.totalDurationMs;
        state.replay.hasValidSample = state.AnalysisSession().GetReplaySample(
            state.replay.currentTimeMs, state.ui.rPaper, state.replay.currentSample);
        ScreenLayout::UpdateReplaySeekThumb(state.ui, state.replay);
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
        POINT mp = ToClearModalSpace(ui, pt, h);
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

    // 1.8 ホームボタン「🏠」（タイトル画面へ戻る）
    if (PtIn(ui.rTbHomeBtn, pt)) {
        if (state.viewingImport) {
            CloseImportedArchive(hWnd, state);
        }
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
        ScreenLayout::Layout(state.ui, state.paper, state.viewingImport, state.replay, w, h);
        InvalidateRect(hWnd, NULL, FALSE);
        return true;
    }

    // 3. 開いているときのフローティングメニュータブ・詳細操作
    if (ui.isSubPanelOpen) {
        if (PtIn(ui.rTbBrush, pt)) {
            SelectTab(hWnd, state, LeftTab::Brush);
            return true;
        } else if (PtIn(ui.rTbPaper, pt)) {
            SelectTab(hWnd, state, LeftTab::Paper);
            return true;
        } else if (PtIn(ui.rTbAnalysis, pt)) {
            SelectTab(hWnd, state, LeftTab::Analysis);
            return true;
        } else if (PtIn(ui.rTbSave, pt)) {
            SelectTab(hWnd, state, LeftTab::Save);
            return true;
        } else if (PtIn(ui.rTbOtehon, pt)) {
            SelectTab(hWnd, state, LeftTab::Otehon);
            return true;
        }

        // 詳細パネル内部の操作
        if (PtIn(ui.rSub, pt)) {
            if (ui.leftTab == LeftTab::Analysis) {
                // 0. 運筆アーカイブの読み込み / 自分の記録に戻る
                if (PtIn(ui.rAnalysisImportBtn, pt)) {
                    ImportArchive(hWnd, state);
                    return true;
                }
                if (state.viewingImport && PtIn(ui.rAnalysisBackBtn, pt)) {
                    CloseImportedArchive(hWnd, state);
                    return true;
                }

                // 記録が1画も無い間は再生・シーク系の操作を受け付けない（ボタンも無効表示）
                bool hasRecording = state.AnalysisSession().GetTotalStrokeCount() > 0;
                // 1. 最初に戻る (↺)
                if (hasRecording && PtIn(ui.rReplayResetBtn, pt)) {
                    state.replay.currentTimeMs = 0;
                    state.replay.hasValidSample = state.AnalysisSession().GetReplaySample(0, state.ui.rPaper, state.replay.currentSample);
                    ScreenLayout::UpdateReplaySeekThumb(state.ui, state.replay);
                    InvalidateRect(hWnd, &ui.rPaper, FALSE);
                    InvalidateRect(hWnd, &ui.rSub, FALSE);
                    return true;
                }
                // 2. 前画 (⏮)
                else if (hasRecording && PtIn(ui.rReplayPrevBtn, pt)) {
                    int curIdx = state.AnalysisSession().FindStrokeIndexAtTimeline(state.replay.currentTimeMs);
                    DWORD curStart = state.AnalysisSession().GetStrokeTimelineStart(curIdx);
                    if (state.replay.currentTimeMs > curStart + 180) {
                        state.replay.currentTimeMs = curStart;
                    } else if (curIdx > 0) {
                        state.replay.currentTimeMs = state.AnalysisSession().GetStrokeTimelineStart(curIdx - 1);
                    } else {
                        state.replay.currentTimeMs = 0;
                    }
                    state.replay.hasValidSample = state.AnalysisSession().GetReplaySample(state.replay.currentTimeMs, state.ui.rPaper, state.replay.currentSample);
                    ScreenLayout::UpdateReplaySeekThumb(state.ui, state.replay);
                    InvalidateRect(hWnd, &ui.rPaper, FALSE);
                    InvalidateRect(hWnd, &ui.rSub, FALSE);
                    return true;
                }
                // 3. 再生 / 一時停止 (▶ / ❚❚)
                else if (hasRecording && PtIn(ui.rReplayPlayBtn, pt)) {
                    if (state.replay.totalDurationMs == 0) {
                        state.AnalysisSession().BuildReplayTimeline();
                        state.replay.totalDurationMs = state.AnalysisSession().GetReplayTotalDurationMs();
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
                    state.replay.hasValidSample = state.AnalysisSession().GetReplaySample(state.replay.currentTimeMs, state.ui.rPaper, state.replay.currentSample);
                    ScreenLayout::UpdateReplaySeekThumb(state.ui, state.replay);
                    InvalidateRect(hWnd, &ui.rPaper, FALSE);
                    InvalidateRect(hWnd, &ui.rSub, FALSE);
                    return true;
                }
                // 4. 次画 (⏭)
                else if (hasRecording && PtIn(ui.rReplayNextBtn, pt)) {
                    int curIdx = state.AnalysisSession().FindStrokeIndexAtTimeline(state.replay.currentTimeMs);
                    if (curIdx + 1 < static_cast<int>(state.AnalysisSession().GetTotalStrokeCount())) {
                        state.replay.currentTimeMs = state.AnalysisSession().GetStrokeTimelineStart(curIdx + 1);
                    } else {
                        state.replay.currentTimeMs = state.replay.totalDurationMs;
                    }
                    state.replay.hasValidSample = state.AnalysisSession().GetReplaySample(state.replay.currentTimeMs, state.ui.rPaper, state.replay.currentSample);
                    ScreenLayout::UpdateReplaySeekThumb(state.ui, state.replay);
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
                    state.replay.hasValidSample = state.AnalysisSession().GetReplaySample(state.replay.currentTimeMs, state.ui.rPaper, state.replay.currentSample);
                    ScreenLayout::UpdateReplaySeekThumb(state.ui, state.replay);
                    InvalidateRect(hWnd, &ui.rPaper, FALSE);
                    InvalidateRect(hWnd, &ui.rSub, FALSE);
                    return true;
                }
                // 7. 波形グラフのクリック・ドラッグによるシーク
                const RECT& rPlotHit = ui.rAnalysisGraphPlot;
                if (hasRecording && PtIn(rPlotHit, pt)) {
                    state.replay.isDraggingWaveform = true;
                    SetCapture(hWnd);
                    double plotW = static_cast<double>(RW(rPlotHit));
                    double norm = (plotW > 0.0) ? static_cast<double>(pt.x - rPlotHit.left) / plotW : 0.0;
                    norm = Clamp(norm, 0.0, 1.0);
                    state.replay.currentTimeMs = static_cast<DWORD>(state.replay.totalDurationMs * norm);
                    state.replay.hasValidSample = state.AnalysisSession().GetReplaySample(state.replay.currentTimeMs, state.ui.rPaper, state.replay.currentSample);
                    ScreenLayout::UpdateReplaySeekThumb(state.ui, state.replay);
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
                }
            } else if (ui.leftTab == LeftTab::Paper) {
                // 下敷き・升目切り替え
                for (int i = 0; i < GRID_PATTERN_COUNT; ++i) {
                    if (PtIn(ui.rGridTile[i], pt)) {
                        state.paper.gridPattern = static_cast<GridPattern>(i);
                        // 升目のセル分割は Layout で算出しているため、
                        // パターン変更時は再レイアウトが必要
                        ScreenLayout::Layout(state.ui, state.paper, state.viewingImport, state.replay, w, h);
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
                    state.ui.suppressPenUntilLift = true;
                    ExportCanvasWithFeedback(hWnd, gpuInk, state, false);
                    InvalidateRect(hWnd, &ui.rSub, FALSE);
                    return true;
                } else if (PtIn(ui.rSaveBtnClip, pt)) {
                    state.ui.suppressPenUntilLift = true;
                    ExportCanvasWithFeedback(hWnd, gpuInk, state, true);
                    InvalidateRect(hWnd, &ui.rSub, FALSE);
                    return true;
                } else if (PtIn(ui.rSaveBtnJson, pt)) {
                    // ダイアログを開く前と閉じた直後、ペンが離れるまで半紙への運筆をロック
                    state.ui.suppressPenUntilLift = true;
                    switch (ArchiveDialogs::PromptSaveJson(hWnd, state.trajectory, state.paper.type, state.brush.type, state.brush.hardness)) {
                    case ArchiveDialogs::Result::Saved:
                        state.SetSaveFeedback(L"✓ 運筆アーカイブ(JSON)を保存しました");
                        break;
                    case ArchiveDialogs::Result::Failed:
                        state.SetSaveFeedback(L"× JSON を保存できませんでした", true);
                        break;
                    case ArchiveDialogs::Result::Canceled:
                        break;
                    }
                    // ダイアログの「保存」ボタンを押したペンが半紙に接地したまま露出しても描画させない
                    state.ui.suppressPenUntilLift = true;
                    InvalidateRect(hWnd, &ui.rSub, FALSE);
                    return true;
                } else if (PtIn(ui.rSaveBtnCsv, pt)) {
                    // ダイアログを開く前と閉じた直後、ペンが離れるまで半紙への運筆をロック
                    state.ui.suppressPenUntilLift = true;
                    switch (ArchiveDialogs::PromptSaveCsv(hWnd, state.trajectory)) {
                    case ArchiveDialogs::Result::Saved:
                        state.SetSaveFeedback(L"✓ 運筆データ(CSV)を出力しました");
                        break;
                    case ArchiveDialogs::Result::Failed:
                        state.SetSaveFeedback(L"× CSV を出力できませんでした", true);
                        break;
                    case ArchiveDialogs::Result::Canceled:
                        break;
                    }
                    // ダイアログの「保存」ボタンを押したペンが半紙に接地したまま露出しても描画させない
                    state.ui.suppressPenUntilLift = true;
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
    if (PtIn(ui.rInkBadge, pt)) {
        // 墨残量ゲージ: 押した位置へ残量を合わせ、離すまで左右の動きに追従させる
        ui.isDraggingInkAmount = true;
        SetCapture(hWnd);
        SetInkAmountFromBadgeX(state, pt.x);
        // 押したままのペンがそのまま運筆を始めないよう、離すまでロックする
        ui.suppressPenUntilLift = true;
        InvalidateRect(hWnd, &ui.rRight, FALSE);
        return true;
    } else if (IsOnInkStoneLand(ui, pt)) {
        // 硯の下の部分は補充ではなく、筆をしごいて墨を落とす。離すまでの動きで減らす
        ui.isWiping = true;
        ui.wipeByMouse = (penPressure < 0.0);
        ui.wipeLastPt = pt;
        // 押したままのペンがそのまま運筆を始めないよう、離すまでロックする
        ui.suppressPenUntilLift = true;
        InvalidateRect(hWnd, &ui.rRight, FALSE);
        return true;
    } else if (PtIn(ui.rInkStoneLarge, pt) || PtIn(ui.rInkRefillBtn, pt)) {
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
    if (state.ui.isDraggingInkAmount) {
        state.ui.isDraggingInkAmount = false;
        ReleaseCapture();
        handled = true;
    }
    if (state.replay.isDraggingWaveform) {
        state.replay.isDraggingWaveform = false;
        ReleaseCapture();
        handled = true;
    }

    // ボタンが離れた＝紙から離れたので運筆ロックを解除（マウス操作時の解除経路）。
    // これがないとホームボタンでタイトルへ戻った後、マウスのクリックが
    // ロックに阻まれてスタジオ画面へ入れなくなる。
    state.ui.suppressPenUntilLift = false;
    state.ui.isWiping = false;
    return handled;
}

void AppController::ReleasePenLock(AppState& state) {
    state.ui.suppressPenUntilLift = false;
}

void AppController::OnPenOutOfRange(AppState& state) {
    // ペンが圏外へ出た＝紙から離れたので運筆ロックを解除
    state.ui.suppressPenUntilLift = false;
    state.ui.isPenRefilling = false;
    state.ui.isWiping = false;
}

// 前回の再生タイマ刻。停止中は 0。
static DWORD s_lastReplayTick = 0;

void AppController::OnTimer(HWND hWnd, WPARAM timerId, AppState& state) {
    if (timerId == TRANSITION_TIMER_ID) {
        if (!state.isTransitioning) {
            KillTimer(hWnd, TRANSITION_TIMER_ID);
            return;
        }

        DWORD now = GetTickCount();
        DWORD elapsed = now - state.transitionStartTime;
        if (elapsed >= state.transitionDurationMs) {
            state.isTransitioning = false;
            state.transitionProgress = 1.0f;
            state.currentScreen = AppScreen::Studio;
            state.ui.leftTab = LeftTab::Brush;
            KillTimer(hWnd, TRANSITION_TIMER_ID);
        } else {
            state.transitionProgress = static_cast<float>(elapsed) / static_cast<float>(state.transitionDurationMs);
        }
        InvalidateRect(hWnd, NULL, FALSE);
        return;
    }

    if (timerId == TITLE_ANIM_TIMER_ID) {
        if (state.currentScreen != AppScreen::Title || state.isTransitioning) {
            KillTimer(hWnd, TITLE_ANIM_TIMER_ID);
            return;
        }
        InvalidateRect(hWnd, NULL, FALSE);
        return;
    }

    if (timerId == REPLAY_TIMER_ID) {
        // 再生中でなくなったらタイマを止める。再び張るのは再生ボタン側。
        if (state.ui.leftTab != LeftTab::Analysis || state.replay.state != ReplayState::Playing) {
            KillTimer(hWnd, REPLAY_TIMER_ID);
            s_lastReplayTick = 0;
            return;
        }

        DWORD now = GetTickCount();
        DWORD dt = (s_lastReplayTick != 0) ? (now - s_lastReplayTick) : 16;
        if (dt > 100) dt = 16;
        s_lastReplayTick = now;

        DWORD advanceMs = static_cast<DWORD>(dt * state.replay.playbackSpeed);
        if (advanceMs < 1 && state.replay.playbackSpeed > 0.0) advanceMs = 1;

        state.replay.currentTimeMs += advanceMs;
        if (state.replay.currentTimeMs >= state.replay.totalDurationMs) {
            state.replay.currentTimeMs = state.replay.totalDurationMs;
            state.replay.state = ReplayState::Paused;
            KillTimer(hWnd, REPLAY_TIMER_ID);
            s_lastReplayTick = 0;
        }

        state.replay.hasValidSample = state.AnalysisSession().GetReplaySample(
            state.replay.currentTimeMs, state.ui.rPaper, state.replay.currentSample);

        // 動くのは半紙のリプレイと解析パネルだけ。Layout() 全体も全画面の
        // 無効化も要らない。
        ScreenLayout::UpdateReplaySeekThumb(state.ui, state.replay);
        InvalidateRect(hWnd, &state.ui.rPaper, FALSE);
        InvalidateRect(hWnd, &state.ui.rSub, FALSE);
    }
}

void AppController::OnKeyDown(HWND hWnd, WPARAM key, AppState& state, GpuInk& gpuInk) {
    // F9 は「紙だけ表示（横向き）」の切り替え。硯パネルのボタンと同じ働き。
    if (key == VK_F9) {
        SetPaperOnly(hWnd, !state.ui.paperOnly, state, gpuInk);
        return;
    }

    // 文字入力中はショートカット（Esc=全消しの確認 / Ctrl+Z・Ctrl+Y=一画戻す・復元）を止める。
    // Esc と BackSpace は WM_CHAR 側で入力終了・1文字削除として処理する。
    if (state.otehon.isTyping) return;

    switch (key) {
    case VK_ESCAPE:
        // 確認モーダル表示中の Esc は「キャンセル」として閉じる
        if (state.ui.showClearConfirm) {
            state.ui.showClearConfirm = false;
            InvalidateRect(hWnd, NULL, FALSE);
            break;
        }

        // 硯パネルの「全消し」ボタンと同じ確認モーダルを出す。
        // 紙だけ表示では倒した向きで読めるよう回して描く（ModalView::DrawClearConfirm）。
        state.ui.hoverClearModal = 0;
        state.ui.showClearConfirm = true;
        InvalidateRect(hWnd, NULL, FALSE);
        break;
    case 'Z':
    case 'z':
        // Ctrl+Z で一画戻す、Ctrl+Shift+Z で一画復元
        if (GetKeyState(VK_CONTROL) < 0) {
            if (GetKeyState(VK_SHIFT) < 0) {
                RedoStroke(hWnd, state, gpuInk);
            } else {
                UndoStroke(hWnd, state, gpuInk);
            }
        }
        break;
    case 'Y':
    case 'y':
        // Ctrl+Y でも一画復元
        if (GetKeyState(VK_CONTROL) < 0) {
            RedoStroke(hWnd, state, gpuInk);
        }
        break;
    default:
        break;
    }
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
            POINT mp = ToClearModalSpace(ui, pt, h);
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

    if (ui.isDraggingInkAmount && (wParam & MK_LBUTTON)) {
        SetInkAmountFromBadgeX(state, pt.x);
        InvalidateRect(hWnd, &ui.rRight, FALSE);
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
        state.replay.hasValidSample = state.AnalysisSession().GetReplaySample(state.replay.currentTimeMs, state.ui.rPaper, state.replay.currentSample);
        ScreenLayout::UpdateReplaySeekThumb(state.ui, state.replay);
        InvalidateRect(hWnd, &ui.rPaper, FALSE);
        InvalidateRect(hWnd, &ui.rSub, FALSE);
        return true;
    }

    if (state.replay.isDraggingWaveform && (wParam & MK_LBUTTON)) {
        const RECT& rPlotHit = ui.rAnalysisGraphPlot;
        double plotW = static_cast<double>(RW(rPlotHit));
        double norm = (plotW > 0.0) ? static_cast<double>(pt.x - rPlotHit.left) / plotW : 0.0;
        norm = Clamp(norm, 0.0, 1.0);
        state.replay.currentTimeMs = static_cast<DWORD>(state.replay.totalDurationMs * norm);
        state.replay.hasValidSample = state.AnalysisSession().GetReplaySample(state.replay.currentTimeMs, state.ui.rPaper, state.replay.currentSample);
        ScreenLayout::UpdateReplaySeekThumb(state.ui, state.replay);
        InvalidateRect(hWnd, &ui.rPaper, FALSE);
        InvalidateRect(hWnd, &ui.rSub, FALSE);
        return true;
    }

    // マウスで硯の磨り面をしごいている間。筆圧が無いので中くらいの強さとして扱う
    if (ui.isWiping && ui.wipeByMouse) {
        if (wParam & MK_LBUTTON) {
            if (WipeInkTo(pt, REFILL_FULL_PRESSURE * 0.5, state)) {
                InvalidateRect(hWnd, &ui.rRight, FALSE);
            }
        } else {
            ui.isWiping = false;
        }
    }

    // ホバー状態の更新
    TbButton oldTb = ui.hoverTb;
    int oldSub = ui.hoverSub;
    int oldStone = ui.hoverInkStone;
    int oldModal = ui.hoverClearModal;
    int oldReplay = ui.hoverReplayBtn;

    ui.hoverTb = TbButton::None;
    ui.hoverSub = 0;
    ui.hoverInkStone = 0;
    ui.hoverClearModal = 0;
    ui.hoverReplayBtn = 0;

    if (ui.showClearConfirm) {
        if (PtIn(ui.rModalClearBtn, pt)) ui.hoverClearModal = 1;
        else if (PtIn(ui.rModalCancelBtn, pt)) ui.hoverClearModal = 2;
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
                    bool hasRecording = state.AnalysisSession().GetTotalStrokeCount() > 0;
                    if (PtIn(ui.rAnalysisImportBtn, pt)) ui.hoverReplayBtn = 9;
                    else if (state.viewingImport && PtIn(ui.rAnalysisBackBtn, pt)) ui.hoverReplayBtn = 10;
                    else if (hasRecording && PtIn(ui.rReplayResetBtn, pt)) ui.hoverReplayBtn = 1;
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
        else if (PtIn(ui.rInkBadge, pt)) ui.hoverInkStone = 7;
        else if (IsOnInkStoneLand(ui, pt)) ui.hoverInkStone = 8;
        else if (PtIn(ui.rInkStoneLarge, pt)) ui.hoverInkStone = 1;
    }

    if (oldTb != ui.hoverTb || oldSub != ui.hoverSub || oldStone != ui.hoverInkStone || oldModal != ui.hoverClearModal || oldReplay != ui.hoverReplayBtn) {
        InvalidateRect(hWnd, NULL, FALSE);
    }

    return true;
}

bool AppController::OnSetCursor(HWND hWnd, LPARAM lParam, const AppState& state) {
    // クライアント領域外（タイトルバーやウィンドウ枠など）はOS標準のサイズ変更カーソル等に委ねる
    if (LOWORD(lParam) != HTCLIENT) {
        return false;
    }

    // タイトル画面表示中、または墨染めフェード遷移中は通常の矢印カーソル
    if (state.currentScreen != AppScreen::Studio || state.isTransitioning) {
        SetCursor(LoadCursor(NULL, IDC_ARROW));
        return true;
    }

    POINT pt;
    if (!GetCursorPos(&pt) || !ScreenToClient(hWnd, &pt)) {
        return false;
    }

    // 全消し確認モーダル表示中はモーダル操作のため矢印カーソル
    if (state.ui.showClearConfirm) {
        SetCursor(LoadCursor(NULL, IDC_ARROW));
        return true;
    }

    // 紙だけ表示中の場合
    if (state.ui.paperOnly) {
        // 右端の操作帯（ボタン領域）上なら矢印カーソル
        if (PtIn(state.ui.rPaperOnlyBar, pt)) {
            SetCursor(LoadCursor(NULL, IDC_ARROW));
            return true;
        }
        // 半紙領域内なら十字カーソル
        if (PtIn(state.ui.rPaper, pt)) {
            static HCURSOR hCursorCross = LoadCursor(NULL, IDC_CROSS);
            SetCursor(hCursorCross);
            return true;
        }
        SetCursor(LoadCursor(NULL, IDC_ARROW));
        return true;
    }

    // 通常スタジオ画面
    // お手本テキスト入力ボックス上であれば I ビーム（テキストカーソル）
    if (state.ui.leftTab == LeftTab::Otehon && PtIn(state.ui.rOtehonInputBox, pt)) {
        SetCursor(LoadCursor(NULL, IDC_IBEAM));
        return true;
    }

    // 半紙（キャンバス）領域内であれば十字カーソル
    if (PtIn(state.ui.rPaper, pt)) {
        static HCURSOR hCursorCross = LoadCursor(NULL, IDC_CROSS);
        SetCursor(hCursorCross);
        return true;
    }

    // UIパネル（左メニュー・右の硯・文机背景など）は標準矢印カーソル
    static HCURSOR hCursorArrow = LoadCursor(NULL, IDC_ARROW);
    SetCursor(hCursorArrow);
    return true;
}


