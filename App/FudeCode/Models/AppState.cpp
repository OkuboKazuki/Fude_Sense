#include "stdafx.h"
#include "AppState.h"

namespace {

// 升目の分割数（列×行）をパターンから求める。
// 列は縦書きの「行（ぎょう）」に相当し、右列から左列へ向かって書き進める。
void GetGridDivision(GridPattern pattern, int& cols, int& rows) {
    cols = 1;
    rows = 1;

    switch (pattern) {
    case GridPattern::Div2:  cols = 1; rows = 2; break;
    case GridPattern::Grid4: cols = 2; rows = 2; break;
    case GridPattern::Grid6: cols = 2; rows = 3; break;
    case GridPattern::Grid8: cols = 2; rows = 4; break;
    case GridPattern::None:
    case GridPattern::Cross1:
    default:
        break; // 1字用（1マス）
    }
}

// 全消し確認モーダル。w×h の領域の中央に置く。
// 紙だけ表示では倒した向き（幅と高さを入れ替えた座標系）で組む。
void LayoutClearModal(UIState& ui, int w, int h) {
    int modalW = 660;
    int modalH = 260;
    ui.rClearModalBox = { w / 2 - modalW / 2, h / 2 - modalH / 2, w / 2 + modalW / 2, h / 2 + modalH / 2 };
    int btnW = 140;
    int btnH = 40;
    int btnY = ui.rClearModalBox.bottom - 56;
    ui.rModalCancelBtn = { ui.rClearModalBox.left + 40, btnY, ui.rClearModalBox.left + 40 + btnW, btnY + btnH };
    ui.rModalClearBtn  = { ui.rClearModalBox.right - 40 - btnW, btnY, ui.rClearModalBox.right - 40, btnY + btnH };
}

} // namespace

// 画面上の点を全消し確認モーダルの座標系へ移す。
// 紙だけ表示は画面を右回りに90度倒して見るので、倒した向きの右が画面の上、
// 下が画面の右になる。モーダルはその向きで組んであるため、点も同じ向きへ直す。
POINT AppState::ToClearModalSpace(POINT pt, int clientHeight) const {
    if (!ui.paperOnly) return pt;
    return { clientHeight - pt.y, pt.x };
}

// シークバーのツマミだけを再生時刻から置き直す。
// 再生中は毎フレーム動くが、他のレイアウトは変わらないため、
// Layout() 全体を呼び直す必要は無い。
void AppState::UpdateReplaySeekThumb() {
    double prog = (replay.totalDurationMs > 0) ? (static_cast<double>(replay.currentTimeMs) / static_cast<double>(replay.totalDurationMs)) : 0.0;
    prog = RenderUtils::Clamp(prog, 0.0, 1.0);
    int trackW = ui.rReplaySeekTrack.right - ui.rReplaySeekTrack.left;
    int thumbX = ui.rReplaySeekTrack.left + static_cast<int>(trackW * prog);
    int thumbCy = (ui.rReplaySeekTrack.top + ui.rReplaySeekTrack.bottom) / 2;
    int thumbR = 8;
    ui.rReplaySeekThumb = { thumbX - thumbR, thumbCy - thumbR, thumbX + thumbR, thumbCy + thumbR };
}

void AppState::Layout(int w, int h) {
    using namespace RenderUtils;

    // 紙だけ表示は通常の UI を組まない別レイアウト
    if (ui.paperOnly) {
        LayoutPaperOnly(w, h);
        return;
    }

    const int rightW = 270;

    ui.rRight = { w - rightW, 0, w, h };
    ui.rCanvasArea = { 0, 0, ui.rRight.left, h };

    // 1. フローティングメニュー & メニュー開閉ボタン（大きめサイズ・一回り拡大）
    if (!ui.isSubPanelOpen) {
        ui.rTbHomeBtn = { 20, 20, 72, 72 };
        ui.rTbNavToggle = { 80, 20, 144, 72 };
        ui.rSub = { 0, 0, 0, 0 };
        ui.rTbBrush = ui.rTbPaper = ui.rTbAnalysis = ui.rTbSave = ui.rTbOtehon = { 0, 0, 0, 0 };
    } else {
        int menuW = 560;
        int menuH = (std::min)(h - 36, 840);
        if (menuH < 600) menuH = 600;
        ui.rSub = { 20, 20, 20 + menuW, 20 + menuH };

        ui.rTbHomeBtn = { ui.rSub.left + 16, ui.rSub.top + 14, ui.rSub.left + 64, ui.rSub.top + 66 };
        ui.rTbNavToggle = { ui.rSub.right - 64, ui.rSub.top + 14, ui.rSub.right - 16, ui.rSub.top + 66 };

        int tabStartX = ui.rTbHomeBtn.right + 12;
        int tabAvailW = (ui.rTbNavToggle.left - 12) - tabStartX;
        int tabW = (tabAvailW - 4 * 8) / 5;
        int tabH = 52;
        int tabY = ui.rSub.top + 14;

        ui.rTbBrush    = { tabStartX + 0 * (tabW + 8), tabY, tabStartX + 0 * (tabW + 8) + tabW, tabY + tabH };
        ui.rTbPaper    = { tabStartX + 1 * (tabW + 8), tabY, tabStartX + 1 * (tabW + 8) + tabW, tabY + tabH };
        ui.rTbAnalysis = { tabStartX + 2 * (tabW + 8), tabY, tabStartX + 2 * (tabW + 8) + tabW, tabY + tabH };
        ui.rTbSave     = { tabStartX + 3 * (tabW + 8), tabY, tabStartX + 3 * (tabW + 8) + tabW, tabY + tabH };
        ui.rTbOtehon   = { tabStartX + 4 * (tabW + 8), tabY, tabStartX + 4 * (tabW + 8) + tabW, tabY + tabH };

        int topOff = ui.rSub.top + 84;

        // 筆タブ
        int cardH = 86;
        ui.rSubSmall  = { ui.rSub.left + 18, topOff, ui.rSub.right - 18, topOff + cardH };
        ui.rSubMedium = { ui.rSub.left + 18, topOff + (cardH + 10), ui.rSub.right - 18, topOff + (cardH + 10) + cardH };
        ui.rSubLarge  = { ui.rSub.left + 18, topOff + (cardH + 10) * 2, ui.rSub.right - 18, topOff + (cardH + 10) * 2 + cardH };
        int cardBottom = topOff + (cardH + 10) * 2 + cardH;
        ui.rHardnessTrack = { ui.rSub.left + 40, cardBottom + 74, ui.rSub.right - 40, cardBottom + 88 };

        // 紙タブ
        // 用紙は半紙に固定なので、先頭は下敷きの見出しから始める
        int gridTop = topOff + 36;
        int tileW = (menuW - 56) / 2;
        int tileH = 58;
        for (int i = 0; i < GRID_PATTERN_COUNT; ++i) {
            int col = i % 2;
            int row = i / 2;
            ui.rGridTile[i] = { ui.rSub.left + 18 + col * (tileW + 12), gridTop + row * (tileH + 8), ui.rSub.left + 18 + col * (tileW + 12) + tileW, gridTop + row * (tileH + 8) + tileH };
        }

        int colorY = gridTop + 3 * (tileH + 8) + 30;
        int colBtnW = (menuW - 60) / 3;
        for (int i = 0; i < 3; ++i) {
            ui.rColorBtn[i] = { ui.rSub.left + 18 + i * (colBtnW + 10), colorY, ui.rSub.left + 18 + i * (colBtnW + 10) + colBtnW, colorY + 48 };
        }

        // 解析（グラフ・リプレイ）タブ
        // 先頭に運筆アーカイブの読み込み。読み込んだ記録を表示中は「自分の記録に戻る」を右に並べる
        int importH = 40;
        int importRight = ui.rSub.right - 18;
        if (viewingImport) {
            int backW = 190;
            ui.rAnalysisBackBtn = { importRight - backW, topOff, importRight, topOff + importH };
            importRight = ui.rAnalysisBackBtn.left - 10;
        } else {
            ui.rAnalysisBackBtn = { 0, 0, 0, 0 };
        }
        ui.rAnalysisImportBtn = { ui.rSub.left + 18, topOff, importRight, topOff + importH };
        int analysisTop = topOff + importH + 10;

        int availSubH = ui.rSub.bottom - analysisTop - 18;
        int replayH = 126;
        int metricsH = 88;
        int compassH = 168;
        int graphH = (std::max)(150, availSubH - replayH - metricsH - compassH - 42);

        ui.rAnalysisReplayBox  = { ui.rSub.left + 18, analysisTop, ui.rSub.right - 18, analysisTop + replayH };
        ui.rAnalysisMetricsBox = { ui.rSub.left + 18, ui.rAnalysisReplayBox.bottom + 12, ui.rSub.right - 18, ui.rAnalysisReplayBox.bottom + 12 + metricsH };
        ui.rAnalysisCompassBox = { ui.rSub.left + 18, ui.rAnalysisMetricsBox.bottom + 12, ui.rSub.right - 18, ui.rAnalysisMetricsBox.bottom + 12 + compassH };
        ui.rAnalysisGraphBox   = { ui.rSub.left + 18, ui.rAnalysisCompassBox.bottom + 12, ui.rSub.right - 18, ui.rAnalysisCompassBox.bottom + 12 + graphH };

        // リプレイ操作コントロール内矩形
        int repLeft = ui.rAnalysisReplayBox.left;
        int repRight = ui.rAnalysisReplayBox.right;
        int repTop = ui.rAnalysisReplayBox.top;

        int seekY = repTop + 34;
        int seekH = 12;
        ui.rReplaySeekTrack = { repLeft + 16, seekY, repRight - 16, seekY + seekH };

        UpdateReplaySeekThumb();

        int btnY = repTop + 62;
        int btnH = 46;
        ui.rReplayResetBtn = { repLeft + 16, btnY, repLeft + 16 + 42, btnY + btnH };
        ui.rReplayPrevBtn  = { ui.rReplayResetBtn.right + 8, btnY, ui.rReplayResetBtn.right + 8 + 44, btnY + btnH };
        ui.rReplayPlayBtn  = { ui.rReplayPrevBtn.right + 8, btnY, ui.rReplayPrevBtn.right + 8 + 88, btnY + btnH };
        ui.rReplayNextBtn  = { ui.rReplayPlayBtn.right + 8, btnY, ui.rReplayPlayBtn.right + 8 + 44, btnY + btnH };

        int spdW = 48;
        int spdH = 36;
        int spdY = btnY + (btnH - spdH) / 2;
        ui.rReplaySpeedBtn[2] = { repRight - 16 - spdW, spdY, repRight - 16, spdY + spdH };
        ui.rReplaySpeedBtn[1] = { ui.rReplaySpeedBtn[2].left - 6 - spdW, spdY, ui.rReplaySpeedBtn[2].left - 6, spdY + spdH };
        ui.rReplaySpeedBtn[0] = { ui.rReplaySpeedBtn[1].left - 6 - spdW, spdY, ui.rReplaySpeedBtn[1].left - 6, spdY + spdH };


        // 保存タブ
        int saveBtnH = 68;
        ui.rSaveBtnPng  = { ui.rSub.left + 20, topOff + 12, ui.rSub.right - 20, topOff + 12 + saveBtnH };
        ui.rSaveBtnClip = { ui.rSub.left + 20, ui.rSaveBtnPng.bottom + 14, ui.rSub.right - 20, ui.rSaveBtnPng.bottom + 14 + saveBtnH };
        ui.rSaveBtnJson = { ui.rSub.left + 20, ui.rSaveBtnClip.bottom + 20, ui.rSub.right - 20, ui.rSaveBtnClip.bottom + 20 + saveBtnH };
        ui.rSaveBtnCsv  = { ui.rSub.left + 20, ui.rSaveBtnJson.bottom + 14, ui.rSub.right - 20, ui.rSaveBtnJson.bottom + 14 + saveBtnH };


        // お手本タブ
        ui.rOtehonToggleBtn = { ui.rSub.left + 20, topOff, ui.rSub.right - 20, topOff + 60 };

        // 配置: 左に升目のミニマップ（配置先の選択）、右に書体の切り替えと操作の説明を出す。
        // ミニマップ内の各マスの矩形は、升目のセルが確定してから 2-2 で算出する。
        int mapTop = ui.rOtehonToggleBtn.bottom + 12;
        ui.rOtehonCellMapBox = { ui.rSub.left + 20, mapTop, ui.rSub.left + 20 + 240, mapTop + 110 };

        // 書体の切り替え。ミニマップ右の余白へ横並びで置き、下に操作の説明を続ける
        int fontBtnLeft = ui.rOtehonCellMapBox.right + 14;
        int fontBtnW = (ui.rSub.right - 20 - fontBtnLeft - 2 * 8) / OTEHON_FONT_COUNT;
        for (int i = 0; i < OTEHON_FONT_COUNT; ++i) {
            int x = fontBtnLeft + i * (fontBtnW + 8);
            ui.rOtehonFontBtn[i] = { x, mapTop, x + fontBtnW, mapTop + 34 };
        }

        // 書きたい文字の入力欄
        ui.rOtehonInputBox = { ui.rSub.left + 20, ui.rOtehonCellMapBox.bottom + 28,
                               ui.rSub.right - 20, ui.rOtehonCellMapBox.bottom + 28 + 44 };

        int oTop = ui.rOtehonInputBox.bottom + 12;
        int oW = (menuW - 68) / 4;
        int oH = 70;
        for (int i = 0; i < 8; ++i) {
            int col = i % 4;
            int row = i / 4;
            ui.rOtehonTile[i] = { ui.rSub.left + 20 + col * (oW + 10), oTop + row * (oH + 10), ui.rSub.left + 20 + col * (oW + 10) + oW, oTop + row * (oH + 10) + oH };
        }
        ui.rOtehonOpacityTrack = { ui.rSub.left + 40, oTop + 2 * (oH + 10) + 54, ui.rSub.right - 40, oTop + 2 * (oH + 10) + 68 };
    }

    // 2. キャンバス / 半紙（画面中央にアスペクト比を維持して配置）
    double ratioW = 242.0, ratioH = 333.0;
    paper.GetAspectRatio(ratioW, ratioH);

    int maxPaperH = h - 56;
    if (maxPaperH < 100) maxPaperH = 100;

    // 画面中央に配置したときの最大幅（硯や左パネルとの干渉を防ぎつつ最大化）
    int maxPaperW = (int)(w * 0.54);
    if (maxPaperW < 100) maxPaperW = 100;

    int paperH = maxPaperH;
    int paperW = (int)(paperH * (ratioW / ratioH));
    if (paperW > maxPaperW) {
        paperW = maxPaperW;
        paperH = (int)(paperW * (ratioH / ratioW));
    }

    // ★ 画面全体の水平中央に固定配置 ★
    int canvasCenterX = w / 2;
    int canvasCenterY = h / 2;

    ui.rPaper.left = canvasCenterX - paperW / 2;
    ui.rPaper.right = ui.rPaper.left + paperW;
    ui.rPaper.top = canvasCenterY - paperH / 2;
    ui.rPaper.bottom = ui.rPaper.top + paperH;

    // 2-2. 下敷き升目のジオメトリ
    LayoutGridGeometry();

    // 3. 右側 硯・墨量・全消し（★ 半紙の右端と画面の右端との真ん中 ★）
    // 基準の大きさ（拡大率 1.0）。ペンで押しやすいよう、空きがあれば最大 INK_STONE_MAX_SCALE 倍まで広げる。
    const int baseStoneW = 210;
    const int baseBtnH = 46;
    const int baseViewBtnH = 44;
    const int baseSpacing = 12;
    const int baseHeaderSpace = 42;
    const int baseViewGap = 20;
    const double INK_STONE_MAX_SCALE = 1.8;
    int baseTotalH = baseHeaderSpace + (int)(baseStoneW * 1.34) + 4 * (baseSpacing + baseBtnH)
                   + baseViewGap + baseViewBtnH;

    // 半紙の右の空き（左右に余白を残す）と、上下に余白を残した高さに収まる倍率
    int rightSpaceW = w - ui.rPaper.right;
    double fitW = (double)(rightSpaceW - 40) / baseStoneW;
    double fitH = (double)(h - 48) / baseTotalH;
    double scale = (std::min)(INK_STONE_MAX_SCALE, (std::min)(fitW, fitH));
    if (scale < 1.0) scale = 1.0;  // 狭い窓では従来の大きさのまま
    ui.inkStoneScale = scale;
    auto S = [scale](int v) { return (int)(v * scale + 0.5); };

    int stoneW = S(baseStoneW);
    int stoneH = (int)(stoneW * 1.34);
    int refillBtnH = S(baseBtnH);
    int undoBtnH = S(baseBtnH);
    int redoBtnH = S(baseBtnH);
    int clearBtnH = S(baseBtnH);
    int viewBtnH = S(baseViewBtnH);    // 紙だけ表示に入るボタン
    int spacing = S(baseSpacing);
    int headerSpace = S(baseHeaderSpace); // 墨残量ヘッダー用スペース

    int viewGap = S(baseViewGap);     // 硯まわりの操作と表示切り替えの間の区切り
    int totalBlockH = headerSpace + stoneH + spacing + refillBtnH + spacing + undoBtnH
                    + spacing + redoBtnH + spacing + clearBtnH
                    + viewGap + viewBtnH;
    int blockStartY = canvasCenterY - totalBlockH / 2;
    if (blockStartY < 24) blockStartY = 24;

    // 半紙の右端から画面右端までの中心X
    int rightSpaceLeft = ui.rPaper.right;
    int rightSpaceRight = w;
    int stoneCenterX = (rightSpaceLeft + rightSpaceRight) / 2;
    int stoneX = stoneCenterX - stoneW / 2;
    if (stoneX + stoneW > w - 10) stoneX = w - stoneW - 10;
    if (stoneX < ui.rPaper.right + 10) stoneX = ui.rPaper.right + 10;

    int stoneY = blockStartY + headerSpace;

    ui.rRight = { rightSpaceLeft, 0, w, h };
    ui.rInkStoneLarge = { stoneX, stoneY, stoneX + stoneW, stoneY + stoneH };
    // 墨残量ゲージ（硯の真上、中央寄せ）
    int badgeW = S(200);
    int badgeH = S(34);
    int badgeX = stoneX + (stoneW - badgeW) / 2;
    int badgeY = stoneY - badgeH - S(8);
    ui.rInkBadge = { badgeX, badgeY, badgeX + badgeW, badgeY + badgeH };
    ui.rInkRefillBtn  = { stoneX, stoneY + stoneH + spacing, stoneX + stoneW, stoneY + stoneH + spacing + refillBtnH };
    ui.rUndoBtn      = { stoneX, ui.rInkRefillBtn.bottom + spacing, stoneX + stoneW, ui.rInkRefillBtn.bottom + spacing + undoBtnH };
    ui.rRedoBtn       = { stoneX, ui.rUndoBtn.bottom + spacing, stoneX + stoneW, ui.rUndoBtn.bottom + spacing + redoBtnH };
    ui.rClearAllBtn   = { stoneX, ui.rRedoBtn.bottom + spacing, stoneX + stoneW, ui.rRedoBtn.bottom + spacing + clearBtnH };

    // 表示の切り替え。墨や履歴の操作とは用途が違うので、少し間を空けて下へ置く。
    int viewTop = ui.rClearAllBtn.bottom + viewGap;
    ui.rPaperOnlyBtn = { stoneX, viewTop, stoneX + stoneW, viewTop + viewBtnH };
    ui.rPaperOnlyExitBtn = ui.rPaperOnlyClearBtn = ui.rPaperOnlyBar = { 0, 0, 0, 0 };

    // 4. 全消し確認モーダルダイアログ
    LayoutClearModal(ui, w, h);
}

// ゲージの両端は丸く、端ぴったりは狙いにくいので、少し内側で 0% / 100% に届くようにする
void AppState::SetInkAmountFromBadgeX(int x) {
    using namespace RenderUtils;
    int pad = RH(ui.rInkBadge) / 2;
    double trackW = static_cast<double>(RW(ui.rInkBadge) - 2 * pad);
    if (trackW <= 0.0) return;
    double norm = static_cast<double>(x - ui.rInkBadge.left - pad) / trackW;
    ink.amount = Clamp(norm, 0.0, 1.0) * INK_MAX_VALUE;
}

// 紙だけ表示。机や毛氈は描かず、横に倒した半紙を比率を保って画面いっぱいに広げ、
// 右端の帯へ「墨を補充」「通常表示に戻る」「筆跡を消す」を並べる。
// 画面を右回りに90度倒して使う前提で、右端の帯が半紙の「下」になる。
// 倒した向きで見て、補充ボタンが下の中央、戻るボタンが右端（画面では上端）、
// 消すボタンが左端（画面では下端）に来る。
// 左メニュー・硯パネルは描かないので、当たり判定が残って運筆を横取り
// しないよう矩形ごと畳んでおく。
void AppState::LayoutPaperOnly(int w, int h) {
    using namespace RenderUtils;

    const RECT kNone = { 0, 0, 0, 0 };
    ui.rSub = ui.rRight = kNone;
    ui.rTbNavToggle = ui.rTbBrush = ui.rTbPaper = ui.rTbAnalysis = ui.rTbSave = ui.rTbOtehon = kNone;
    ui.rInkStoneLarge = ui.rUndoBtn = ui.rRedoBtn = ui.rClearAllBtn = ui.rPaperOnlyBtn = kNone;
    ui.rInkBadge = kNone;
    ui.isDraggingInkAmount = false;
    // 全消し確認モーダル（Esc・筆跡を消すボタンで出す）は倒した向きで読めるよう、幅と高さを入れ替えて組む
    LayoutClearModal(ui, h, w);
    ui.rCanvasArea = { 0, 0, w, h };

    const int barW = 96;
    ui.rPaperOnlyBar = { (std::max)(100, w - barW), 0, w, h };

    // 半紙の縦横比を保ったまま、帯の左の領域へ収めて中央に置く。
    // 倒して使うので、画面上では半紙の長辺が横になる。
    double ratioW = 242.0, ratioH = 333.0;
    paper.GetAspectRatio(ratioW, ratioH);
    double longSide = (std::max)(ratioW, ratioH);
    double shortSide = (std::min)(ratioW, ratioH);

    int areaW = static_cast<int>(ui.rPaperOnlyBar.left);
    int areaH = (std::max)(100, h);
    int paperW = areaW;
    int paperH = static_cast<int>(areaW * shortSide / longSide);
    if (paperH > areaH) {
        paperH = areaH;
        paperW = static_cast<int>(areaH * longSide / shortSide);
    }
    int paperX = (areaW - paperW) / 2;
    int paperY = (h - paperH) / 2;
    ui.rPaper = { paperX, paperY, paperX + paperW, paperY + paperH };

    LayoutGridGeometry();

    // ボタンは倒した向きで横長になるよう、画面上では縦長に取る。
    // 窓が低くて重なるときも、補充ボタンは中央のまま長さだけ詰める。
    const int pad = 16;
    const int btnThick = 64;
    const int exitLen = 190;
    int left = ui.rPaperOnlyBar.left + (barW - btnThick) / 2;
    int right = left + btnThick;
    ui.rPaperOnlyExitBtn = { left, pad, right, pad + exitLen };
    // 戻るボタンと上下対称に置くので、補充ボタンの詰め方は戻るボタン側だけ見ればよい
    ui.rPaperOnlyClearBtn = { left, h - pad - exitLen, right, h - pad };

    int cy = h / 2;
    int refillHalf = (std::min)(180, cy - static_cast<int>(ui.rPaperOnlyExitBtn.bottom) - 12);
    if (refillHalf < 60) refillHalf = 60;
    ui.rInkRefillBtn = { left, cy - refillHalf, right, cy + refillHalf };
}

// 下敷き升目のジオメトリ。半紙（rPaper）から罫線の外枠・各セル・お手本配置用の
// ミニマップまでを一括で算出する。内側の罫線とセル境界を一致させるため、
// 通常表示と紙だけ表示のどちらからもここを通す（CanvasView::DrawGrid と共有）。
void AppState::LayoutGridGeometry() {
    using namespace RenderUtils;

    {
        // 余白は半紙の短辺から決める。紙だけ表示で倒しても通常表示と同じ幅になる。
        int margin = (std::max)(6, (std::min)(RW(ui.rPaper), RH(ui.rPaper)) / 48);
        if (paper.gridPattern == GridPattern::None) {
            // 罫線なしのときは半紙全体を1マスとして扱う
            ui.rGridBorder = ui.rPaper;
        } else {
            ui.rGridBorder = { ui.rPaper.left + margin, ui.rPaper.top + margin,
                               ui.rPaper.right - margin, ui.rPaper.bottom - margin };
        }

        int bw = RW(ui.rGridBorder);
        int bh = RH(ui.rGridBorder);

        // 列数・行数は半紙を縦に置いた向きで決める
        int cols = 1, rows = 1;
        GetGridDivision(paper.gridPattern, cols, rows);

        // 紙だけ表示の半紙は、通常表示の半紙を左回りに90度倒したもの。
        // 升目も一緒に倒すので、画面上の列と行が入れ替わる。
        // gridCols / gridRows は画面上の分割数（CanvasView::DrawGrid が罫線を引くのに使う）。
        const bool rotated = ui.paperOnly;
        ui.gridCols = rotated ? rows : cols;
        ui.gridRows = rotated ? cols : rows;

        ui.gridCellCount = 0;
        for (int i = 0; i < cols; ++i) {
            int col = cols - 1 - i; // 縦書きは右列から左列へ
            for (int row = 0; row < rows; ++row) {
                if (ui.gridCellCount >= MAX_GRID_CELLS) break;
                if (!rotated) {
                    ui.rGridCell[ui.gridCellCount++] = {
                        ui.rGridBorder.left + (bw * col) / cols,
                        ui.rGridBorder.top + (bh * row) / rows,
                        ui.rGridBorder.left + (bw * (col + 1)) / cols,
                        ui.rGridBorder.top + (bh * (row + 1)) / rows
                    };
                } else {
                    // 左回りに倒すと、縦置きの (u, v) は画面の (v, 1 - u) へ移る。
                    // 縦置きの行が画面の列に、縦置きの右の列が画面の上になる。
                    ui.rGridCell[ui.gridCellCount++] = {
                        ui.rGridBorder.left + (bw * row) / rows,
                        ui.rGridBorder.top + (bh * (cols - col - 1)) / cols,
                        ui.rGridBorder.left + (bw * (row + 1)) / rows,
                        ui.rGridBorder.top + (bh * (cols - col)) / cols
                    };
                }
            }
        }

        // お手本の配置先を選ぶミニマップ。実際のセルを枠内へ相似縮小したものなので、
        // 升目の割り付けと必ず一致する（並びも rGridCell と同じ書字順）。
        const RECT& mapBox = ui.rOtehonCellMapBox;
        int boxW = RW(mapBox);
        int boxH = RH(mapBox);
        if (bw > 0 && bh > 0 && boxW > 0 && boxH > 0) {
            int mapH = boxH;
            int mapW = (bh > 0) ? (mapH * bw / bh) : boxW;
            if (mapW > boxW) {
                mapW = boxW;
                mapH = (bw > 0) ? (mapW * bh / bw) : boxH;
            }
            int mapX = mapBox.left + (boxW - mapW) / 2;
            int mapY = mapBox.top + (boxH - mapH) / 2;

            for (int i = 0; i < ui.gridCellCount; ++i) {
                const RECT& c = ui.rGridCell[i];
                ui.rOtehonCellBtn[i] = {
                    mapX + (c.left   - ui.rGridBorder.left) * mapW / bw,
                    mapY + (c.top    - ui.rGridBorder.top)  * mapH / bh,
                    mapX + (c.right  - ui.rGridBorder.left) * mapW / bw,
                    mapY + (c.bottom - ui.rGridBorder.top)  * mapH / bh
                };
            }
        }
        for (int i = ui.gridCellCount; i < MAX_GRID_CELLS; ++i) {
            ui.rOtehonCellBtn[i] = { 0, 0, 0, 0 };
        }
    }
}
