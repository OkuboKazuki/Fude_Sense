#include "stdafx.h"
#include "CanvasView.h"
#include "RenderUtils.h"
#include <algorithm>
#include <unordered_map>
#include <vector>

namespace {

// お手本の書体。なぞる手本なので毛筆の楷書を第一候補とし、無い環境では
// 教科書体 → 明朝へ落とす。明朝は縦画が太く横画が細い印刷用の書体で、
// なぞっても筆の運びとは合わないため最終手段。
// HG 系は Office 同梱で他環境には無いことがあるため、Windows 標準の
// 教科書体、さらに明朝へと落とす。書体名は GDI での実在を確認済み
// （和名は "HG正楷書体-PRO" とハイフンが入る点に注意）。
const wchar_t* const kOtehonFontFaces[] = {
    L"HGSeikaishotaiPRO",   // HG正楷書体-PRO（毛筆楷書）
    L"HG正楷書体-PRO",
    L"UD Digi Kyokasho N",  // UD デジタル教科書体（Windows 標準）
    L"Yu Mincho"
};

int CALLBACK FontFoundProc(const LOGFONTW*, const TEXTMETRICW*, DWORD, LPARAM lParam) {
    *reinterpret_cast<bool*>(lParam) = true;
    return 0; // 1件見つかれば十分
}

bool FontExists(HDC dc, const wchar_t* face) {
    LOGFONTW lf{};
    lf.lfCharSet = DEFAULT_CHARSET;
    wcsncpy_s(lf.lfFaceName, LF_FACESIZE, face, _TRUNCATE);
    bool found = false;
    EnumFontFamiliesExW(dc, &lf, FontFoundProc, reinterpret_cast<LPARAM>(&found), 0);
    return found;
}

// お手本フォントのキャッシュ。em サイズが変わるのはウィンドウや升目の
// 変更時だけなので、1つ保持して使い回す（毎フレームの CreateFontW を避ける）。
struct OtehonFontCache {
    HFONT font = nullptr;
    int emSize = 0;
    ~OtehonFontCache() { if (font) DeleteObject(font); }
};
OtehonFontCache g_otehonFont;

HFONT GetOtehonFont(HDC dc, int emSize) {
    if (g_otehonFont.font && g_otehonFont.emSize == emSize) return g_otehonFont.font;
    if (g_otehonFont.font) {
        DeleteObject(g_otehonFont.font);
        g_otehonFont.font = nullptr;
    }

    // CreateFontW は存在しない書体名を渡しても無警告で別の書体に置換するため、
    // 実在するものを先に選ぶ。
    const wchar_t* face = kOtehonFontFaces[0];
    for (const wchar_t* candidate : kOtehonFontFaces) {
        if (FontExists(dc, candidate)) {
            face = candidate;
            break;
        }
    }

    // 高さは負値＝文字の em サイズ指定（正値は行の高さになり、内部レディングの
    // 分だけ字が小さくなる）。
    g_otehonFont.font = CreateFontW(-emSize, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, face);
    g_otehonFont.emSize = emSize;
    return g_otehonFont.font;
}

} // namespace

void CanvasView::DrawBackground(HDC dc, const AppState& state) {
    using namespace RenderUtils;
    const RECT& rPaper = state.ui.rPaper;
    if (RW(rPaper) <= 0 || RH(rPaper) <= 0) return;

    // 1. 本物の書道用下敷き（毛氈：縦長フェルト布マット）
    int marginX = (std::max)(22, RW(rPaper) / 18);
    int marginTop = (std::max)(26, RH(rPaper) / 16);
    int marginBottom = (std::max)(32, RH(rPaper) / 13);

    RECT rMat = {
        rPaper.left - marginX,
        rPaper.top - marginTop,
        rPaper.right + marginX,
        rPaper.bottom + marginBottom
    };

    // 毛氈本体（品格のある濃紺インディゴフェルト布地・影なしでスッキリ描画）
    Box(dc, rMat, RGB(20, 26, 40), RGB(46, 54, 76), 1, 6);

    // 2. 和紙（半紙）本体
    HBRUSH pb = CreateSolidBrush(RGB(252, 251, 248));
    HPEN pp = CreatePen(PS_SOLID, 1, RGB(220, 216, 206));
    HBRUSH ob = (HBRUSH)SelectObject(dc, pb);
    HPEN op = (HPEN)SelectObject(dc, pp);
    Rectangle(dc, rPaper.left, rPaper.top, rPaper.right, rPaper.bottom);
    SelectObject(dc, op);
    SelectObject(dc, ob);
    DeleteObject(pp);
    DeleteObject(pb);
}

void CanvasView::DrawOtehonGlyph(HDC dc, const RECT& cell, const std::wstring& text, double opacity) {
    using namespace RenderUtils;
    if (text.empty()) return;
    const wchar_t* ch = text.c_str();
    const int chLen = (int)text.size(); // サロゲートペアは2要素で1文字

    int cellW = RW(cell);
    int cellH = RH(cell);
    if (cellW <= 0 || cellH <= 0) return;

    // 縦書きの書写では、字は上下いっぱいに書いて余白は左右に残す。
    // 横基準と縦基準で比率を分け、厳しい方に合わせる。単純に短辺 x 0.85 とすると、
    // 2文字（横長のマス）のように高さが効く下敷きで字が小さくなりすぎる。
    int emSize = (int)((std::min)(cellW * 0.85, cellH * 0.95));
    if (emSize <= 0) return;

    HFONT font = GetOtehonFont(dc, emSize);
    if (!font) return;

    // 墨のテクスチャは不透明な白ビットマップで半紙全面を覆うため（GpuInk::Render）、
    // お手本は墨の合成「後」に重ねる必要がある。ただし素直に上書きすると、
    // なぞった墨をお手本が隠してしまう。そこで一旦オフスクリーンへ白地＋薄墨色で
    // 字を描き、SRCAND（チャンネルごとの論理積＝暗い方が残る）で転送する。
    // 白い紙の上にはお手本が出て、墨の上では墨が残る。
    HDC layerDC = CreateCompatibleDC(dc);
    if (!layerDC) return;
    HBITMAP layerBmp = CreateCompatibleBitmap(dc, cellW, cellH);
    if (!layerBmp) {
        DeleteDC(layerDC);
        return;
    }
    HBITMAP oldLayerBmp = (HBITMAP)SelectObject(layerDC, layerBmp);

    RECT local = { 0, 0, cellW, cellH };
    FillRect(layerDC, &local, (HBRUSH)GetStockObject(WHITE_BRUSH));

    HFONT oldFont = (HFONT)SelectObject(layerDC, font);

    // DrawTextW の DT_VCENTER は行ボックス基準の中央揃えで、内部レディングの分だけ
    // 字が上へずれる。飾りなら気付かないが、なぞる手本では数pxのずれがそのまま
    // 練習の狂いになる。em ボックス（漢字の仮想ボディ）がマスの中心に一致するよう
    // ベースラインを自前で求めて描く。
    TEXTMETRICW tm{};
    GetTextMetricsW(layerDC, &tm);
    SIZE ext{};
    GetTextExtentPoint32W(layerDC, ch, chLen, &ext);

    int emTop = (cellH - emSize) / 2;
    int baseline = emTop + (tm.tmAscent - tm.tmInternalLeading);
    int x = (cellW - ext.cx) / 2;

    int grayVal = (int)(248 - opacity * 105.0);
    grayVal = Clamp(grayVal, 80, 245);

    SetTextAlign(layerDC, TA_LEFT | TA_BASELINE);
    SetBkMode(layerDC, TRANSPARENT);
    SetTextColor(layerDC, RGB(grayVal, (int)(grayVal * 0.98), (int)(grayVal * 0.95)));
    TextOutW(layerDC, x, baseline, ch, chLen);

    BitBlt(dc, cell.left, cell.top, cellW, cellH, layerDC, 0, 0, SRCAND);

    SelectObject(layerDC, oldFont);
    SelectObject(layerDC, oldLayerBmp);
    DeleteObject(layerBmp);
    DeleteDC(layerDC);
}

void CanvasView::DrawOtehon(HDC dc, const AppState& state) {
    if (!state.otehon.isVisible) return;

    const UIState& ui = state.ui;
    const double opacity = state.otehon.opacity;

    if (state.otehon.IsFollowingPen()) {
        // ペン追従は1字だけ。書いている升目に重ねる。升目なし（GridPattern::None）は
        // 半紙全体が1マス、1字用の下敷き（Cross1 / StarGrid）は内枠が1マスになる。
        DrawOtehonGlyph(dc, state.GetOtehonCell(), state.otehon.GetCurrentCharacter(), opacity);
        return;
    }

    // 固定表示はマスごとに置いた字をすべて出す
    for (int i = 0; i < ui.gridCellCount; ++i) {
        DrawOtehonGlyph(dc, ui.rGridCell[i], state.otehon.GetCellText(i), opacity);
    }
}

void CanvasView::DrawCross(HDC dc, int x, int y, int s) {
    MoveToEx(dc, x - s, y, nullptr); LineTo(dc, x + s, y);
    MoveToEx(dc, x, y - s, nullptr); LineTo(dc, x, y + s);
}

void CanvasView::DrawGrid(HDC dc, const AppState& state) {
    if (state.paper.gridPattern == GridPattern::None) return;

    using namespace RenderUtils;
    const UIState& ui = state.ui;

    // 外枠・セル分割は AppState::Layout が算出済みのものを使う。
    // ここで再計算すると、お手本の配置に使うセルと罫線がずれる。
    const RECT& rBorder = ui.rGridBorder;
    if (RW(rBorder) <= 0 || RH(rBorder) <= 0) return;

    COLORREF lineColor = RGB(228, 90, 90);
    if (state.paper.gridColor == GridColorTheme::WhiteLine) lineColor = RGB(220, 222, 230);
    else if (state.paper.gridColor == GridColorTheme::InkGray) lineColor = RGB(160, 165, 175);

    HPEN gp = CreatePen(PS_SOLID, 1, lineColor);
    HPEN gpDash = CreatePen(PS_DOT, 1, lineColor);
    HPEN op = (HPEN)SelectObject(dc, gp);

    MoveToEx(dc, rBorder.left, rBorder.top, nullptr);
    LineTo(dc, rBorder.right, rBorder.top);
    LineTo(dc, rBorder.right, rBorder.bottom);
    LineTo(dc, rBorder.left, rBorder.bottom);
    LineTo(dc, rBorder.left, rBorder.top);

    int bw = RW(rBorder);
    int bh = RH(rBorder);

    // 升目の内側罫線（セル境界）。
    // Lines3 / Lines4 は縦罫線のみの下敷きで、行方向の区切りはお手本配置用の
    // 仮想的なものなので線としては描かない。
    bool drawRowLines = (state.paper.gridPattern != GridPattern::Lines3
                      && state.paper.gridPattern != GridPattern::Lines4);

    for (int c = 1; c < ui.gridCols; ++c) {
        int x = rBorder.left + (bw * c) / ui.gridCols;
        MoveToEx(dc, x, rBorder.top, nullptr); LineTo(dc, x, rBorder.bottom);
    }
    if (drawRowLines) {
        for (int r = 1; r < ui.gridRows; ++r) {
            int y = rBorder.top + (bh * r) / ui.gridRows;
            MoveToEx(dc, rBorder.left, y, nullptr); LineTo(dc, rBorder.right, y);
        }
    }

    // パターン固有の補助線（セル境界ではない装飾）
    switch (state.paper.gridPattern) {
    case GridPattern::Cross1:
    {
        // 1字用。マス中央の十字と四分割位置の目印
        int mx = rBorder.left + bw / 2;
        int my = rBorder.top + bh / 2;
        MoveToEx(dc, mx, rBorder.top, nullptr); LineTo(dc, mx, rBorder.bottom);
        MoveToEx(dc, rBorder.left, my, nullptr); LineTo(dc, rBorder.right, my);
        DrawCross(dc, rBorder.left + bw / 4, rBorder.top + bh / 4, 10);
        DrawCross(dc, rBorder.left + 3 * bw / 4, rBorder.top + bh / 4, 10);
        DrawCross(dc, rBorder.left + 3 * bw / 4, rBorder.top + 3 * bh / 4, 10);
        DrawCross(dc, rBorder.left + bw / 4, rBorder.top + 3 * bh / 4, 10);
        break;
    }
    case GridPattern::Div2:
    {
        // 中央の縦線は字の中心を示す点線ガイド（セル境界ではない）
        int mx = rBorder.left + bw / 2;
        SelectObject(dc, gpDash);
        MoveToEx(dc, mx, rBorder.top, nullptr); LineTo(dc, mx, rBorder.bottom);
        SelectObject(dc, gp);
        break;
    }
    case GridPattern::Grid4:
    {
        // 各マスの中心に目印
        for (int i = 0; i < ui.gridCellCount; ++i) {
            const RECT& cell = ui.rGridCell[i];
            DrawCross(dc, (cell.left + cell.right) / 2, (cell.top + cell.bottom) / 2, 12);
        }
        break;
    }
    case GridPattern::StarGrid:
    {
        int mx = rBorder.left + bw / 2;
        int my = rBorder.top + bh / 2;
        MoveToEx(dc, mx, rBorder.top, nullptr); LineTo(dc, mx, rBorder.bottom);
        MoveToEx(dc, rBorder.left, my, nullptr); LineTo(dc, rBorder.right, my);

        SelectObject(dc, gpDash);
        MoveToEx(dc, rBorder.left, rBorder.top, nullptr); LineTo(dc, rBorder.right, rBorder.bottom);
        MoveToEx(dc, rBorder.right, rBorder.top, nullptr); LineTo(dc, rBorder.left, rBorder.bottom);
        SelectObject(dc, gp);
        break;
    }
    default:
        break;
    }

    SelectObject(dc, op);
    DeleteObject(gpDash);
    DeleteObject(gp);
}

void CanvasView::RenderInk(HDC dc, GpuInk& gpuInk, const AppState& state) {
    gpuInk.Render(dc, state.ui.rPaper.left, state.ui.rPaper.top);
}

// ---------------------------------------------------------------------------
// リプレイ描画のオフスクリーンキャッシュ
//
// 再生中は毎フレーム半紙を描き直すことになるが、記録済みの運筆は時刻が進んでも
// 変わらない。そこで半紙と同じ大きさの2枚のレイヤへ焼き込み、画面へは BitBlt
// だけを行う。
//   ghost  : 全画の薄いゴースト線。記録が変わった時だけ焼き直す
//   active : 再生済みの墨。時刻が進んだ分だけ描き足し、巻き戻した時だけ焼き直す
// どちらも白地に描いて SRCAND で重ねる（GpuInk の墨テクスチャと同じ合成）。
// ---------------------------------------------------------------------------
namespace {

// 線幅ごとにペンを使い回すプール。点ごとに CreatePen / DeleteObject を繰り返すと
// 1画あたり数百個のGDIオブジェクトが生き死にするため、幅をキーにして持ち回る。
class PenPool {
public:
    explicit PenPool(COLORREF color) : m_color(color) {}
    ~PenPool() {
        for (auto& kv : m_pens) DeleteObject(kv.second);
    }
    HPEN Get(int width) {
        if (width < 1) width = 1;
        auto it = m_pens.find(width);
        if (it != m_pens.end()) return it->second;
        HPEN pen = CreatePen(PS_SOLID, width, m_color);
        m_pens[width] = pen;
        return pen;
    }
private:
    COLORREF m_color;
    std::unordered_map<int, HPEN> m_pens;
};

struct ReplayLayer {
    HDC dc = nullptr;
    HBITMAP bmp = nullptr;
    HBITMAP oldBmp = nullptr;
    int w = 0;
    int h = 0;

    bool Ensure(HDC ref, int W, int H) {
        if (dc && w == W && h == H) return true;
        Release();
        dc = CreateCompatibleDC(ref);
        if (!dc) return false;
        bmp = CreateCompatibleBitmap(ref, W, H);
        if (!bmp) {
            DeleteDC(dc);
            dc = nullptr;
            return false;
        }
        oldBmp = (HBITMAP)SelectObject(dc, bmp);
        w = W;
        h = H;
        FillWhite();
        return true;
    }
    void FillWhite() {
        if (!dc) return;
        RECT r = { 0, 0, w, h };
        FillRect(dc, &r, (HBRUSH)GetStockObject(WHITE_BRUSH));
    }
    void Release() {
        if (dc) {
            SelectObject(dc, oldBmp);
            DeleteDC(dc);
            dc = nullptr;
        }
        if (bmp) {
            DeleteObject(bmp);
            bmp = nullptr;
        }
        oldBmp = nullptr;
        w = 0;
        h = 0;
    }
};

struct ReplayCacheState {
    ReplayLayer ghost;
    ReplayLayer active;
    unsigned revision = 0;              // 焼き込み済みの記録リビジョン
    int paperW = 0;
    int paperH = 0;
    bool valid = false;                 // ghost / bakedCount が現在の記録と一致しているか
    DWORD activeTimeMs = 0;             // active レイヤへ焼き込み済みの時刻
    std::vector<size_t> bakedCount;     // 画ごとの、焼き込み済み記録点数
};

ReplayCacheState g_replayCache;

const COLORREF kGhostColor = RGB(218, 222, 228);
const COLORREF kReplayInkColor = RGB(22, 24, 28);

// 記録点の丸。線分用の太いペンを選んだまま描くと丸まで太るので、必ず1pxへ戻す。
void DrawReplayDot(HDC dc, PenPool& pens, int px, int py, int radius) {
    SelectObject(dc, pens.Get(1));
    Ellipse(dc, px - radius, py - radius, px + radius, py + radius);
}

void DrawReplaySegment(HDC dc, PenPool& pens, int fromX, int fromY, int toX, int toY, int width) {
    SelectObject(dc, pens.Get(width));
    MoveToEx(dc, fromX, fromY, nullptr);
    LineTo(dc, toX, toY);
}

// 全画のゴースト線を ghost レイヤへ焼き込む
void BakeGhostLayer(const AppState& state, int pw, int ph) {
    ReplayLayer& layer = g_replayCache.ghost;
    layer.FillWhite();

    HDC dc = layer.dc;
    PenPool pens(kGhostColor);
    HBRUSH brush = CreateSolidBrush(kGhostColor);
    HBRUSH oldBrush = (HBRUSH)SelectObject(dc, brush);
    HPEN oldPen = (HPEN)SelectObject(dc, pens.Get(1));

    for (const auto& s : state.trajectory.GetStrokes()) {
        for (size_t i = 0; i < s.points.size(); ++i) {
            const auto& p = s.points[i];
            int px = static_cast<int>(p.normX * pw);
            int py = static_cast<int>(p.normY * ph);
            DrawReplayDot(dc, pens, px, py, (std::max)(2, static_cast<int>(p.width * 0.45)));

            if (i > 0) {
                const auto& q = s.points[i - 1];
                DrawReplaySegment(dc, pens,
                    static_cast<int>(q.normX * pw), static_cast<int>(q.normY * ph), px, py,
                    (std::max)(2, static_cast<int>((p.width + q.width) * 0.45)));
            }
        }
    }

    SelectObject(dc, oldPen);
    SelectObject(dc, oldBrush);
    DeleteObject(brush);
}

// 再生済みの墨を active レイヤへ描き足す。fromScratch なら白紙から焼き直す。
void BakeActiveLayer(const AppState& state, int pw, int ph, bool fromScratch) {
    ReplayLayer& layer = g_replayCache.active;
    const auto& strokes = state.trajectory.GetStrokes();

    if (fromScratch || g_replayCache.bakedCount.size() != strokes.size()) {
        layer.FillWhite();
        g_replayCache.bakedCount.assign(strokes.size(), 0);
    }

    DWORD timeMs = state.replay.currentTimeMs;
    HDC dc = layer.dc;
    PenPool pens(kReplayInkColor);
    HBRUSH brush = CreateSolidBrush(kReplayInkColor);
    HBRUSH oldBrush = (HBRUSH)SelectObject(dc, brush);
    HPEN oldPen = (HPEN)SelectObject(dc, pens.Get(1));

    for (size_t si = 0; si < strokes.size(); ++si) {
        const auto& pts = strokes[si].points;
        if (pts.empty()) continue;

        DWORD start = state.trajectory.GetStrokeTimelineStart(si);
        if (timeMs < start) break;  // これ以降の画はまだ始まっていない
        DWORD rel = timeMs - start;

        // 画内の相対時刻が現在時刻に届いている記録点までが表示対象
        size_t visible = 0;
        while (visible < pts.size() && pts[visible].timeMs <= rel) ++visible;

        for (size_t i = g_replayCache.bakedCount[si]; i < visible; ++i) {
            const auto& p = pts[i];
            int px = static_cast<int>(p.normX * pw);
            int py = static_cast<int>(p.normY * ph);
            DrawReplayDot(dc, pens, px, py, (std::max)(2, static_cast<int>(p.width * 0.5)));

            if (i > 0) {
                const auto& q = pts[i - 1];
                DrawReplaySegment(dc, pens,
                    static_cast<int>(q.normX * pw), static_cast<int>(q.normY * ph), px, py,
                    (std::max)(2, static_cast<int>((p.width + q.width) * 0.5)));
            }
        }
        if (visible > g_replayCache.bakedCount[si]) {
            g_replayCache.bakedCount[si] = visible;
        }
    }

    // 走っている最中の画は、補間した先端まで線を伸ばして滑らかに繋ぐ。
    // 先端は次のフレームで記録点に追い越されるので、焼き込んだままで構わない。
    const auto& sample = state.replay.currentSample;
    if (state.replay.hasValidSample && sample.isPenDown &&
        sample.strokeIndex >= 0 && sample.strokeIndex < static_cast<int>(strokes.size())) {
        const auto& pts = strokes[sample.strokeIndex].points;
        size_t baked = g_replayCache.bakedCount[sample.strokeIndex];
        int px = static_cast<int>(sample.point.normX * pw);
        int py = static_cast<int>(sample.point.normY * ph);
        DrawReplayDot(dc, pens, px, py, (std::max)(2, static_cast<int>(sample.point.width * 0.5)));
        if (baked > 0 && baked <= pts.size()) {
            const auto& q = pts[baked - 1];
            DrawReplaySegment(dc, pens,
                static_cast<int>(q.normX * pw), static_cast<int>(q.normY * ph), px, py,
                (std::max)(2, static_cast<int>((sample.point.width + q.width) * 0.5)));
        }
    }

    SelectObject(dc, oldPen);
    SelectObject(dc, oldBrush);
    DeleteObject(brush);
    g_replayCache.activeTimeMs = timeMs;
}

} // namespace

void CanvasView::ReleaseReplayCache() {
    g_replayCache.ghost.Release();
    g_replayCache.active.Release();
    g_replayCache.bakedCount.clear();
    g_replayCache.valid = false;
    g_replayCache.revision = 0;
    g_replayCache.paperW = 0;
    g_replayCache.paperH = 0;
    g_replayCache.activeTimeMs = 0;
}

void CanvasView::DrawReplayCanvas(HDC dc, const AppState& state) {
    const RECT& rPaper = state.ui.rPaper;
    int pw = RenderUtils::RW(rPaper);
    int ph = RenderUtils::RH(rPaper);
    if (pw <= 0 || ph <= 0) return;

    if (!state.trajectory.GetStrokes().empty()) {
        unsigned rev = state.trajectory.GetRevision();
        bool keyChanged = (!g_replayCache.valid || g_replayCache.revision != rev
            || g_replayCache.paperW != pw || g_replayCache.paperH != ph);

        if (g_replayCache.ghost.Ensure(dc, pw, ph) && g_replayCache.active.Ensure(dc, pw, ph)) {
            if (keyChanged) {
                BakeGhostLayer(state, pw, ph);
                g_replayCache.revision = rev;
                g_replayCache.paperW = pw;
                g_replayCache.paperH = ph;
                g_replayCache.valid = true;
            }
            // 巻き戻した時だけ白紙から焼き直す。進んだ時は差分を描き足すだけ。
            bool rewound = (state.replay.currentTimeMs < g_replayCache.activeTimeMs);
            BakeActiveLayer(state, pw, ph, keyChanged || rewound);

            BitBlt(dc, rPaper.left, rPaper.top, pw, ph, g_replayCache.ghost.dc, 0, 0, SRCAND);
            BitBlt(dc, rPaper.left, rPaper.top, pw, ph, g_replayCache.active.dc, 0, 0, SRCAND);
        }
    }

    if (state.replay.hasValidSample) {
        Draw3DBrushPose(dc, state, state.replay.currentSample.point, state.replay.currentSample.isPenDown);
    }
}

void CanvasView::Draw3DBrushPose(HDC dc, const AppState& state, const StrokePoint& pose, bool isPenDown) {
    using namespace RenderUtils;
    int cx = pose.paperX;
    int cy = pose.paperY;

    // 半紙外周から離れすぎている場合は描画しない
    if (cx < state.ui.rPaper.left - 50 || cx > state.ui.rPaper.right + 50 ||
        cy < state.ui.rPaper.top - 50 || cy > state.ui.rPaper.bottom + 50) {
        return;
    }

    double alt = Clamp(pose.altitudeDeg, 5.0, 90.0);
    double azmRad = pose.azimuthDeg * (3.14159265358979323846 / 180.0);
    double tiltNorm = (90.0 - alt) / 90.0;

    // 1. 半紙への投影の影 (Shadow)
    double shLen = 42.0 * tiltNorm;
    int shEndX = cx + static_cast<int>(shLen * std::sin(azmRad));
    int shEndY = cy - static_cast<int>(shLen * std::cos(azmRad));
    if (tiltNorm > 0.05) {
        HPEN shPen = CreatePen(PS_SOLID, 6, RGB(200, 196, 188));
        HPEN oldP = (HPEN)SelectObject(dc, shPen);
        MoveToEx(dc, cx, cy, nullptr);
        LineTo(dc, shEndX, shEndY);
        SelectObject(dc, oldP);
        DeleteObject(shPen);
    }

    // 2. 筆の立ち具合リング（姿勢判定カラーリング）
    // 60°以上: 青緑（立っている・基本姿勢）、45°〜60°: 山吹色、45°未満: 赤（寝かせ警告）
    COLORREF postureColor = (alt >= 60.0) ? RGB(40, 205, 140) : ((alt >= 45.0) ? RGB(255, 195, 50) : RGB(255, 65, 65));
    int ringR = (std::max)(12, static_cast<int>(pose.width * 0.5) + 8);

    HPEN ringPen = CreatePen(isPenDown ? PS_SOLID : PS_DOT, 2, postureColor);
    HBRUSH nullB = (HBRUSH)GetStockObject(NULL_BRUSH);
    HPEN oldPen = (HPEN)SelectObject(dc, ringPen);
    HBRUSH oldBrush = (HBRUSH)SelectObject(dc, nullB);

    Ellipse(dc, cx - ringR, cy - ringR, cx + ringR, cy + ringR);

    // 中心接地点マーカー
    HBRUSH dotB = CreateSolidBrush(isPenDown ? postureColor : RGB(140, 150, 170));
    SelectObject(dc, dotB);
    Ellipse(dc, cx - 3, cy - 3, cx + 3, cy + 3);
    DeleteObject(dotB);

    SelectObject(dc, oldPen);
    SelectObject(dc, oldBrush);
    DeleteObject(ringPen);

    // 3. 3D 筆軸ベクトルの算出
    // 画面奥（上方向）を基準に、高度角と方位角から立体的な筆軸方向を算出
    double dirX = -std::sin(azmRad) * tiltNorm;
    double dirY = std::cos(azmRad) * tiltNorm - (alt / 90.0) * 0.85;
    double len = std::sqrt(dirX * dirX + dirY * dirY);
    if (len < 0.001) {
        dirX = 0.0; dirY = -1.0; len = 1.0;
    }
    dirX /= len;
    dirY /= len;

    double normX = -dirY;
    double normY = dirX;

    double tipLen = 22.0;
    double shaftLen = 95.0;

    int tipBaseX = cx + static_cast<int>(dirX * tipLen);
    int tipBaseY = cy + static_cast<int>(dirY * tipLen);

    int shaftTopX = cx + static_cast<int>(dirX * shaftLen);
    int shaftTopY = cy + static_cast<int>(dirY * shaftLen);

    // 4. 3D 穂先 (Tuft) の描画（黒〜濃茶の円錐形）
    double tipHalfW = 6.0;
    POINT tuftPts[3] = {
        { cx, cy },
        { tipBaseX + static_cast<int>(normX * tipHalfW), tipBaseY + static_cast<int>(normY * tipHalfW) },
        { tipBaseX - static_cast<int>(normX * tipHalfW), tipBaseY - static_cast<int>(normY * tipHalfW) }
    };
    HBRUSH tuftBrush = CreateSolidBrush(RGB(28, 30, 36));
    HPEN tuftPen = CreatePen(PS_SOLID, 1, RGB(18, 20, 24));
    oldBrush = (HBRUSH)SelectObject(dc, tuftBrush);
    oldPen = (HPEN)SelectObject(dc, tuftPen);
    Polygon(dc, tuftPts, 3);
    SelectObject(dc, oldPen);
    SelectObject(dc, oldBrush);
    DeleteObject(tuftPen);
    DeleteObject(tuftBrush);

    // 5. 3D 筆管（軸）の描画（竹・木目調シリンダー）
    double shaftHalfW = 5.0;
    POINT shaftPts[4] = {
        { tipBaseX + static_cast<int>(normX * shaftHalfW), tipBaseY + static_cast<int>(normY * shaftHalfW) },
        { shaftTopX + static_cast<int>(normX * (shaftHalfW - 1)), shaftTopY + static_cast<int>(normY * (shaftHalfW - 1)) },
        { shaftTopX - static_cast<int>(normX * (shaftHalfW - 1)), shaftTopY - static_cast<int>(normY * (shaftHalfW - 1)) },
        { tipBaseX - static_cast<int>(normX * shaftHalfW), tipBaseY - static_cast<int>(normY * shaftHalfW) }
    };
    HBRUSH shaftBrush = CreateSolidBrush(RGB(170, 125, 75));
    HPEN shaftPen = CreatePen(PS_SOLID, 1, RGB(80, 50, 25));
    oldBrush = (HBRUSH)SelectObject(dc, shaftBrush);
    oldPen = (HPEN)SelectObject(dc, shaftPen);
    Polygon(dc, shaftPts, 4);

    // 筆軸ハイライト線
    HPEN hlPen = CreatePen(PS_SOLID, 2, RGB(225, 185, 135));
    SelectObject(dc, hlPen);
    MoveToEx(dc, tipBaseX + static_cast<int>(normX * 1.5), tipBaseY + static_cast<int>(normY * 1.5), nullptr);
    LineTo(dc, shaftTopX + static_cast<int>(normX * 1.5), shaftTopY + static_cast<int>(normY * 1.5));
    DeleteObject(hlPen);

    // 穂首の巻線バンド（ホワイトリング）
    HPEN bandPen = CreatePen(PS_SOLID, 3, RGB(245, 240, 225));
    SelectObject(dc, bandPen);
    MoveToEx(dc, tipBaseX + static_cast<int>(normX * (shaftHalfW + 0.5)), tipBaseY + static_cast<int>(normY * (shaftHalfW + 0.5)), nullptr);
    LineTo(dc, tipBaseX - static_cast<int>(normX * (shaftHalfW + 0.5)), tipBaseY - static_cast<int>(normY * (shaftHalfW + 0.5)));
    DeleteObject(bandPen);

    SelectObject(dc, oldPen);
    SelectObject(dc, oldBrush);
    DeleteObject(shaftPen);
    DeleteObject(shaftBrush);

    // 6. 姿勢・高度角バッジ（ヘッドアップディスプレイ）
    wchar_t poseText[32];
    if (alt >= 60.0) {
        swprintf_s(poseText, 32, L"%.0f° [直立]", alt);
    } else if (alt >= 45.0) {
        swprintf_s(poseText, 32, L"%.0f° [やや傾]", alt);
    } else {
        swprintf_s(poseText, 32, L"%.0f° [寝かせ警告!]", alt);
    }

    int badgeW = (alt < 45.0) ? 104 : 84;
    int badgeH = 22;
    int badgeX = shaftTopX + ((dirX >= 0) ? 10 : (-badgeW - 10));
    int badgeY = shaftTopY - 12;

    RECT rBadge = { badgeX, badgeY, badgeX + badgeW, badgeY + badgeH };
    Box(dc, rBadge, RGB(24, 28, 36), postureColor, 1, 4);

    HFONT fBadge = CreateCustomFont(11, FW_BOLD);
    Center(dc, rBadge, poseText, fBadge, postureColor);
    DeleteObject(fBadge);
}

