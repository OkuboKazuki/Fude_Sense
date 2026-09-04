#include "stdafx.h"
#include "CanvasView.h"
#include "RenderUtils.h"
#include "ReplayInk.h"
#include <algorithm>
#include <unordered_map>
#include <vector>

namespace {

// お手本の書体候補。各書体とも先頭から順に実在するものを使う。
// HG 系は Office 同梱で他環境には無いことがあるため、Windows 標準の
// UD デジタル教科書体、さらに明朝へと落とす。明朝は縦画が太く横画が細い
// 印刷用の書体で、なぞっても筆の運びとは合わないため最終手段。
// 書体名は GDI での実在を確認済み
// （和名は "HG正楷書体-PRO" とハイフンが入る点に注意）。
const wchar_t* const kSeikaishoFaces[] = {
    L"HGSeikaishotaiPRO",   // HG正楷書体-PRO（毛筆楷書）
    L"HG正楷書体-PRO",
    L"UD Digi Kyokasho N",  // UD デジタル教科書体（Windows 標準）
    L"Yu Mincho"
};
const wchar_t* const kKyokashoFaces[] = {
    L"HGKyokashotai",       // HG教科書体（学校書写の字形）
    L"HG教科書体",
    L"UD Digi Kyokasho N",
    L"Yu Mincho"
};
const wchar_t* const kGyoshoFaces[] = {
    L"HGGyoshotai",         // HG行書体
    L"HG行書体",
    // 行書が無い環境で明朝まで落とすと別物になるので、まず楷書を試す
    L"HGSeikaishotaiPRO",
    L"HG正楷書体-PRO",
    L"UD Digi Kyokasho N",
    L"Yu Mincho"
};

struct OtehonFaceList {
    const wchar_t* const* faces;
    int count;
};

OtehonFaceList GetOtehonFaces(OtehonFontStyle style) {
    switch (style) {
    case OtehonFontStyle::Kyokasho: return { kKyokashoFaces, _countof(kKyokashoFaces) };
    case OtehonFontStyle::Gyosho:   return { kGyoshoFaces,   _countof(kGyoshoFaces) };
    case OtehonFontStyle::Seikaisho:
    default:                        return { kSeikaishoFaces, _countof(kSeikaishoFaces) };
    }
}

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
    OtehonFontStyle style = OtehonFontStyle::Seikaisho;
    ~OtehonFontCache() { if (font) DeleteObject(font); }
};
OtehonFontCache g_otehonFont;

HFONT GetOtehonFont(HDC dc, int emSize, OtehonFontStyle style) {
    if (g_otehonFont.font && g_otehonFont.emSize == emSize && g_otehonFont.style == style) {
        return g_otehonFont.font;
    }
    if (g_otehonFont.font) {
        DeleteObject(g_otehonFont.font);
        g_otehonFont.font = nullptr;
    }

    // CreateFontW は存在しない書体名を渡しても無警告で別の書体に置換するため、
    // 実在するものを先に選ぶ。
    OtehonFaceList list = GetOtehonFaces(style);
    const wchar_t* face = list.faces[0];
    for (int i = 0; i < list.count; ++i) {
        if (FontExists(dc, list.faces[i])) {
            face = list.faces[i];
            break;
        }
    }

    // 高さは負値＝文字の em サイズ指定（正値は行の高さになり、内部レディングの
    // 分だけ字が小さくなる）。
    g_otehonFont.font = CreateFontW(-emSize, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, face);
    g_otehonFont.emSize = emSize;
    g_otehonFont.style = style;
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

// その書体の本来のフォント（フォールバック用の代替は含まない）が入っているか。
bool CanvasView::HasOtehonFont(HDC dc, OtehonFontStyle style) {
    OtehonFaceList list = GetOtehonFaces(style);
    // 候補の先頭2つが欧文名・和名の対で、その書体そのものを指す。
    // 3つ目以降は別書体への代替なので、実在しても「入っている」とは言えない。
    for (int i = 0; i < list.count && i < 2; ++i) {
        if (FontExists(dc, list.faces[i])) return true;
    }
    return false;
}

void CanvasView::DrawOtehonGlyph(HDC dc, const RECT& cell, const std::wstring& text, double opacity,
                                 OtehonFontStyle style) {
    using namespace RenderUtils;
    if (text.empty()) return;
    const wchar_t* ch = text.c_str();
    const int chLen = (int)text.size(); // サロゲートペアは2要素で1文字

    int cellW = RW(cell);
    int cellH = RH(cell);
    if (cellW <= 0 || cellH <= 0) return;

    // 中筆で升目いっぱいに書く実寸の手本にするため、em ボックスを升目とほぼ
    // 同じ大きさにする。字の墨は em ボックスより一回り小さく（em を 1000 とすると
    // 墨幅は「夢」で 816、「道」で 937）、em を升目より小さく取ると手本が
    // 升目の中で浮いてしまう。縦横比は崩さないので、縦長の升目（4字組みなど）
    // では上下に余白が残る。
    int emSize = (int)((std::min)(cellW, cellH) * 0.95);
    if (emSize <= 0) return;

    HFONT font = GetOtehonFont(dc, emSize, style);
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

    // マスごとに置いた字をすべて出す
    for (int i = 0; i < ui.gridCellCount; ++i) {
        DrawOtehonGlyph(dc, ui.rGridCell[i], state.otehon.GetCellText(i), opacity, state.otehon.fontStyle);
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
    // ゴースト筆跡は「これから書く部分」だけを出す。書き終わった墨の上へ重ねると、
    // SRCAND で墨の縁（かすれや輪郭の中間調）が暗くなり、実物とずれるため。
    ReplayLayer ghostFuture;            // まだ始まっていない画。先頭の画が変わった時だけ焼き直す
    ReplayLayer ghostCurrent;           // 今書いている画の、これから走る部分。毎フレーム焼き直す
    unsigned revision = 0;              // 焼き込み済みの記録リビジョン
    int paperW = 0;
    int paperH = 0;
    int futureFrom = -1;                // ghostFuture に焼いてある先頭の画
    bool futureValid = false;
    RECT currentBox = { 0, 0, 0, 0 };   // ghostCurrent の有効範囲（半紙ローカル）
    bool currentValid = false;
};

ReplayCacheState g_replayCache;

// 再生済みの墨。実際に書いたときと同じ GpuInk で描く。
ReplayInk g_replayInk;

const COLORREF kGhostColor = RGB(218, 222, 228);

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

// 1画ぶんのゴーストを、fromPoint 以降の点について描く（半紙ローカル座標）
void DrawGhostStroke(HDC dc, PenPool& pens, const StrokeData& s, size_t fromPoint, int pw, int ph) {
    for (size_t i = fromPoint; i < s.points.size(); ++i) {
        const auto& p = s.points[i];
        int px = static_cast<int>(p.normX * pw);
        int py = static_cast<int>(p.normY * ph);
        DrawReplayDot(dc, pens, px, py, (std::max)(2, static_cast<int>(p.width * 0.45)));

        // 描き始めの点は、直前の（もう墨が乗っている）点とは繋がない
        if (i > fromPoint) {
            const auto& q = s.points[i - 1];
            DrawReplaySegment(dc, pens,
                static_cast<int>(q.normX * pw), static_cast<int>(q.normY * ph), px, py,
                (std::max)(2, static_cast<int>((p.width + q.width) * 0.45)));
        }
    }
}

// 1画を囲む矩形（半紙ローカル）。線幅ぶん外側へ広げる。
RECT GhostStrokeBox(const StrokeData& s, int pw, int ph) {
    RECT box = { 0, 0, 0, 0 };
    if (s.points.empty()) return box;

    double minX = 1e9, minY = 1e9, maxX = -1e9, maxY = -1e9, maxW = 0.0;
    for (const auto& p : s.points) {
        double x = p.normX * pw;
        double y = p.normY * ph;
        minX = (std::min)(minX, x);
        minY = (std::min)(minY, y);
        maxX = (std::max)(maxX, x);
        maxY = (std::max)(maxY, y);
        maxW = (std::max)(maxW, p.width);
    }
    int pad = static_cast<int>(maxW * 0.5) + 4;
    box.left   = (std::max)(0,      static_cast<int>(minX) - pad);
    box.top    = (std::max)(0,      static_cast<int>(minY) - pad);
    box.right  = (std::min)(pw, static_cast<int>(maxX) + pad + 1);
    box.bottom = (std::min)(ph, static_cast<int>(maxY) + pad + 1);
    return box;
}

// まだ始まっていない画（fromStroke 以降）をまとめて焼く
void BakeFutureGhost(const AppState& state, int pw, int ph, size_t fromStroke) {
    ReplayLayer& layer = g_replayCache.ghostFuture;
    layer.FillWhite();

    const auto& strokes = state.trajectory.GetStrokes();
    if (fromStroke >= strokes.size()) return;

    HDC dc = layer.dc;
    PenPool pens(kGhostColor);
    HBRUSH brush = CreateSolidBrush(kGhostColor);
    HBRUSH oldBrush = (HBRUSH)SelectObject(dc, brush);
    HPEN oldPen = (HPEN)SelectObject(dc, pens.Get(1));

    for (size_t i = fromStroke; i < strokes.size(); ++i) {
        DrawGhostStroke(dc, pens, strokes[i], 0, pw, ph);
    }

    SelectObject(dc, oldPen);
    SelectObject(dc, oldBrush);
    DeleteObject(brush);
}

// 今書いている画の、まだ走っていない部分だけを焼く。
// 画を囲む矩形の中だけ塗り直すので、毎フレーム呼んでも半紙全面を触らない。
void BakeCurrentGhost(const AppState& state, int pw, int ph, size_t strokeIdx, size_t fromPoint) {
    ReplayLayer& layer = g_replayCache.ghostCurrent;
    const auto& strokes = state.trajectory.GetStrokes();
    if (strokeIdx >= strokes.size()) {
        g_replayCache.currentValid = false;
        return;
    }

    const StrokeData& s = strokes[strokeIdx];
    RECT box = GhostStrokeBox(s, pw, ph);
    g_replayCache.currentBox = box;
    g_replayCache.currentValid = (box.right > box.left && box.bottom > box.top);
    if (!g_replayCache.currentValid) return;

    HDC dc = layer.dc;
    FillRect(dc, &box, (HBRUSH)GetStockObject(WHITE_BRUSH));
    if (fromPoint >= s.points.size()) return;  // 走り終わった画。白のまま

    PenPool pens(kGhostColor);
    HBRUSH brush = CreateSolidBrush(kGhostColor);
    HBRUSH oldBrush = (HBRUSH)SelectObject(dc, brush);
    HPEN oldPen = (HPEN)SelectObject(dc, pens.Get(1));

    DrawGhostStroke(dc, pens, s, fromPoint, pw, ph);

    SelectObject(dc, oldPen);
    SelectObject(dc, oldBrush);
    DeleteObject(brush);
}

} // namespace

void CanvasView::ReleaseReplayCache() {
    g_replayCache.ghostFuture.Release();
    g_replayCache.ghostCurrent.Release();
    g_replayCache.futureValid = false;
    g_replayCache.currentValid = false;
    g_replayCache.futureFrom = -1;
    g_replayCache.revision = 0;
    g_replayCache.paperW = 0;
    g_replayCache.paperH = 0;
    g_replayInk.Release();
}

void CanvasView::DrawReplayCanvas(HDC dc, const AppState& state) {
    const RECT& rPaper = state.ui.rPaper;
    int pw = RenderUtils::RW(rPaper);
    int ph = RenderUtils::RH(rPaper);
    if (pw <= 0 || ph <= 0) return;

    const auto& strokes = state.trajectory.GetStrokes();
    if (!strokes.empty()) {
        // 1. 再生済みの墨。GpuInk の墨テクスチャは不透明で半紙全面を覆うため、
        //    ゴースト筆跡より先に置く。
        bool scrubbing = (state.replay.isDraggingSeekBar || state.replay.isDraggingWaveform);
        if (g_replayInk.Update(state.trajectory, state.replay.currentTimeMs, pw, ph, scrubbing)) {
            g_replayInk.Render(dc, rPaper.left, rPaper.top);
        }

        // 2. これから書く部分のゴースト筆跡。
        //    合成は SRCAND なので、既に墨が乗っている場所へ重ねると縁が暗くなる。
        //    走り終わった部分は出さない。
        int curIdx = state.trajectory.FindStrokeIndexAtTimeline(state.replay.currentTimeMs);
        if (curIdx < 0) curIdx = 0;
        size_t futureFrom = static_cast<size_t>(curIdx) + 1;

        unsigned rev = state.trajectory.GetRevision();
        bool keyChanged = (g_replayCache.revision != rev
            || g_replayCache.paperW != pw || g_replayCache.paperH != ph);

        if (g_replayCache.ghostFuture.Ensure(dc, pw, ph) && g_replayCache.ghostCurrent.Ensure(dc, pw, ph)) {
            if (keyChanged || !g_replayCache.futureValid
                || g_replayCache.futureFrom != static_cast<int>(futureFrom)) {
                BakeFutureGhost(state, pw, ph, futureFrom);
                g_replayCache.futureFrom = static_cast<int>(futureFrom);
                g_replayCache.revision = rev;
                g_replayCache.paperW = pw;
                g_replayCache.paperH = ph;
                g_replayCache.futureValid = true;
            }

            BakeCurrentGhost(state, pw, ph, static_cast<size_t>(curIdx),
                state.trajectory.GetVisiblePointCount(static_cast<size_t>(curIdx), state.replay.currentTimeMs));

            BitBlt(dc, rPaper.left, rPaper.top, pw, ph, g_replayCache.ghostFuture.dc, 0, 0, SRCAND);
            if (g_replayCache.currentValid) {
                const RECT& b = g_replayCache.currentBox;
                BitBlt(dc, rPaper.left + b.left, rPaper.top + b.top,
                    b.right - b.left, b.bottom - b.top,
                    g_replayCache.ghostCurrent.dc, b.left, b.top, SRCAND);
            }
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

