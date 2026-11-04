#include "stdafx.h"
#include "AppState.h"
#include "MathUtils.h"

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

} // namespace

// 全消し確認モーダル。w×h の領域の中央に置く。
// 紙だけ表示では倒した向き（幅と高さを入れ替えた座標系）で組む。
void AppState::LayoutClearModal(int w, int h) {
    int modalW = 660;
    int modalH = 260;
    ui.rClearModalBox = { w / 2 - modalW / 2, h / 2 - modalH / 2, w / 2 + modalW / 2, h / 2 + modalH / 2 };
    int btnW = 140;
    int btnH = 40;
    int btnY = ui.rClearModalBox.bottom - 56;
    ui.rModalCancelBtn = { ui.rClearModalBox.left + 40, btnY, ui.rClearModalBox.left + 40 + btnW, btnY + btnH };
    ui.rModalClearBtn  = { ui.rClearModalBox.right - 40 - btnW, btnY, ui.rClearModalBox.right - 40, btnY + btnH };
}

// シークバーのツマミだけを再生時刻から置き直す。
// 再生中は毎フレーム動くが、他のレイアウトは変わらないため、
// Layout() 全体を呼び直す必要は無い。
void AppState::UpdateReplaySeekThumb() {
    double prog = (replay.totalDurationMs > 0) ? (static_cast<double>(replay.currentTimeMs) / static_cast<double>(replay.totalDurationMs)) : 0.0;
    prog = MathUtils::Clamp(prog, 0.0, 1.0);
    int trackW = ui.rReplaySeekTrack.right - ui.rReplaySeekTrack.left;
    int thumbX = ui.rReplaySeekTrack.left + static_cast<int>(trackW * prog);
    int thumbCy = (ui.rReplaySeekTrack.top + ui.rReplaySeekTrack.bottom) / 2;
    int thumbR = 8;
    ui.rReplaySeekThumb = { thumbX - thumbR, thumbCy - thumbR, thumbX + thumbR, thumbCy + thumbR };
}

// 紙だけ表示。机や毛氈は描かず、横に倒した半紙を比率を保って画面いっぱいに広げ、
// 右端の帯へ「墨を補充」「通常表示に戻る」「筆跡を消す」を並べる。
// 画面を右回りに90度倒して使う前提で、右端の帯が半紙の「下」になる。
// 倒した向きで見て、補充ボタンが下の中央、戻るボタンが右端（画面では上端）、
// 消すボタンが左端（画面では下端）に来る。
// 左メニュー・硯パネルは描かないので、当たり判定が残って運筆を横取り
// しないよう矩形ごと畳んでおく。
void AppState::LayoutPaperOnly(int w, int h) {
    using namespace MathUtils;

    const RECT kNone = { 0, 0, 0, 0 };
    ui.rSub = ui.rRight = kNone;
    ui.rTbNavToggle = ui.rTbBrush = ui.rTbPaper = ui.rTbAnalysis = ui.rTbSave = ui.rTbOtehon = kNone;
    ui.rInkStoneLarge = ui.rUndoBtn = ui.rRedoBtn = ui.rClearAllBtn = ui.rPaperOnlyBtn = kNone;
    ui.rInkBadge = ui.rInkPool = ui.rInkLand = kNone;
    ui.isDraggingInkAmount = false;
    ui.isWiping = false;
    // 全消し確認モーダル（Esc・筆跡を消すボタンで出す）は倒した向きで読めるよう、幅と高さを入れ替えて組む
    LayoutClearModal(h, w);
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
    using namespace MathUtils;

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
