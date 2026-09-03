#include "stdafx.h"
#include "CanvasView.h"
#include "RenderUtils.h"
#include <algorithm>

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
    const RECT& rPaper = state.ui.rPaper;
    HBRUSH pb = CreateSolidBrush(RGB(248, 247, 242));
    HPEN pp = CreatePen(PS_SOLID, 1, RGB(175, 172, 162));
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
