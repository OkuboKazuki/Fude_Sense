#include "stdafx.h"
#include "ShapeCompare.h"
#include "EvaluationScore.h"
#include "CanvasView.h"
#include "RenderUtils.h"
#include <algorithm>
#include <cmath>

namespace {

// --- 調整対象の定数はここへ集める（後段の点数化でも触ることになる）---------

// 墨があるとみなすしきい値。m_ink は 0〜450 で、表示は 255 で黒に飽和する。
// にじみの薄い縁まで拾うと字が太る側へ狂うので、はっきり見える濃さで切る。
constexpr int kInkThreshold = 96;

// お手本マスクを作るときの不透明度。CanvasView::DrawOtehonGlyph は
// 248 - opacity*105 の灰色で描くので、1.0 のとき灰色 143 になる。
constexpr double kOtehonMaskOpacity = 1.0;

// 白(255)と上の灰色(143)の中間。半分以上グリフに覆われた画素を図形とみなす。
constexpr int kOtehonGrayThreshold = (255 + 143) / 2;

// 形だけを比べるときに、双方の外接矩形を引き伸ばす先の一辺。
constexpr int kNormalizedSize = 128;

// --------------------------------------------------------------------------

int ClampInt(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

// お手本のみ / 墨のみ / 共通 の画素数から指標を組み立てる
ShapeMetrics MakeMetrics(int otehonPixels, int inkPixels, int interPixels) {
    ShapeMetrics m;
    m.otehonPixels = otehonPixels;
    m.inkPixels = inkPixels;
    m.interPixels = interPixels;
    if (otehonPixels <= 0) return m; // お手本が無ければ分母が作れない

    int unionPixels = otehonPixels + inkPixels - interPixels;
    m.iou      = (unionPixels > 0) ? (double)interPixels / (double)unionPixels : 0.0;
    m.overflow = (double)(inkPixels - interPixels) / (double)otehonPixels;
    m.missing  = (double)(otehonPixels - interPixels) / (double)otehonPixels;
    m.valid = true;
    return m;
}

// マスクの外接矩形を kNormalizedSize 四方へ最近傍で引き伸ばす。
// 位置と大きさの差を落として、形だけを比べられるようにする。
ShapeMask NormalizeToBox(const ShapeMask& src) {
    ShapeMask dst;
    RECT bb{};
    if (!src.GetBoundingBox(bb)) return dst;

    int bw = bb.right - bb.left;
    int bh = bb.bottom - bb.top;
    if (bw <= 0 || bh <= 0) return dst;

    dst.width = kNormalizedSize;
    dst.height = kNormalizedSize;
    dst.bits.assign((size_t)kNormalizedSize * kNormalizedSize, 0);

    for (int y = 0; y < kNormalizedSize; ++y) {
        int sy = bb.top + (int)((y + 0.5) * bh / kNormalizedSize);
        sy = ClampInt(sy, bb.top, bb.bottom - 1);
        for (int x = 0; x < kNormalizedSize; ++x) {
            int sx = bb.left + (int)((x + 0.5) * bw / kNormalizedSize);
            sx = ClampInt(sx, bb.left, bb.right - 1);
            dst.bits[(size_t)y * kNormalizedSize + x] = src.At(sx, sy);
        }
    }
    return dst;
}

} // namespace

// ---------------------------------------------------------------------------
// ShapeMask / CompareResult （型は EvaluationResult.h）
// ---------------------------------------------------------------------------

double CompareResult::AverageGridIoU() const {
    double sum = 0.0;
    int n = 0;
    for (const CellCompare& c : cells) {
        if (!c.grid.valid) continue;
        sum += c.grid.iou;
        ++n;
    }
    return (n > 0) ? sum / n : 0.0;
}

double CompareResult::AverageShapeIoU() const {
    double sum = 0.0;
    int n = 0;
    for (const CellCompare& c : cells) {
        if (!c.shape.valid) continue;
        sum += c.shape.iou;
        ++n;
    }
    return (n > 0) ? sum / n : 0.0;
}

int ShapeMask::Count() const {
    int n = 0;
    for (unsigned char b : bits) n += (b ? 1 : 0);
    return n;
}

bool ShapeMask::GetBoundingBox(RECT& out) const {
    if (IsEmpty()) return false;
    int minX = width, minY = height, maxX = -1, maxY = -1;
    for (int y = 0; y < height; ++y) {
        const unsigned char* row = &bits[(size_t)y * width];
        for (int x = 0; x < width; ++x) {
            if (!row[x]) continue;
            if (x < minX) minX = x;
            if (x > maxX) maxX = x;
            if (y < minY) minY = y;
            if (y > maxY) maxY = y;
        }
    }
    if (maxX < 0) return false;
    out.left = minX;
    out.top = minY;
    out.right = maxX + 1;   // 右下は半開区間
    out.bottom = maxY + 1;
    return true;
}

bool ShapeMask::GetCentroid(double& outX, double& outY) const {
    if (IsEmpty()) return false;
    double sx = 0.0, sy = 0.0;
    int n = 0;
    for (int y = 0; y < height; ++y) {
        const unsigned char* row = &bits[(size_t)y * width];
        for (int x = 0; x < width; ++x) {
            if (!row[x]) continue;
            sx += x;
            sy += y;
            ++n;
        }
    }
    if (n == 0) return false;
    outX = sx / n;
    outY = sy / n;
    return true;
}

// ---------------------------------------------------------------------------
// マスクの生成
// ---------------------------------------------------------------------------

bool ShapeCompare::BuildOtehonMask(HDC refDC, const RECT& cell, const std::wstring& text,
                                   OtehonFontStyle style, ShapeMask& out) {
    using namespace RenderUtils;
    out = ShapeMask();
    if (text.empty()) return false;

    int w = RW(cell);
    int h = RH(cell);
    if (w <= 0 || h <= 0) return false;

    // 画面と同じ字形・同じ位置にするため、専用に描き直さず
    // CanvasView::DrawOtehonGlyph をそのまま白地へ流す。ここで em ボックスの
    // 取り方を作り直すと、画面に見えているお手本とマスクがずれる。
    HDC memDC = CreateCompatibleDC(refDC);
    if (!memDC) return false;

    // GetPixel を升目ぶん回すと1マスで数万回の呼び出しになる。
    // DIB セクションへ描いて、画素はメモリから直接読む。
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;  // 上から下（トップダウン）
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    void* pixels = nullptr;
    HBITMAP bmp = CreateDIBSection(memDC, &bi, DIB_RGB_COLORS, &pixels, nullptr, 0);
    if (!bmp || !pixels) {
        if (bmp) DeleteObject(bmp);
        DeleteDC(memDC);
        return false;
    }
    HBITMAP oldBmp = (HBITMAP)SelectObject(memDC, bmp);

    RECT local = { 0, 0, w, h };
    FillRect(memDC, &local, (HBRUSH)GetStockObject(WHITE_BRUSH));
    CanvasView::DrawOtehonGlyph(memDC, local, text, kOtehonMaskOpacity, style);
    GdiFlush(); // DIB へ描き込みが反映されてから読む

    out.width = w;
    out.height = h;
    out.bits.assign((size_t)w * h, 0);
    const unsigned char* src = static_cast<const unsigned char*>(pixels);
    for (int y = 0; y < h; ++y) {
        const unsigned char* row = src + (size_t)y * w * 4;
        for (int x = 0; x < w; ++x) {
            // 32bpp は BGRA 並び。灰色で描かれるので緑で代表させる。
            if (row[x * 4 + 1] < kOtehonGrayThreshold) {
                out.bits[(size_t)y * w + x] = 1;
            }
        }
    }

    SelectObject(memDC, oldBmp);
    DeleteObject(bmp);
    DeleteDC(memDC);
    return out.Count() > 0;
}

bool ShapeCompare::BuildInkMask(const std::vector<int>& ink, int inkW, int inkH,
                                const RECT& cellInPaper, ShapeMask& out) {
    using namespace RenderUtils;
    out = ShapeMask();
    if (ink.empty() || inkW <= 0 || inkH <= 0) return false;

    int w = RW(cellInPaper);
    int h = RH(cellInPaper);
    if (w <= 0 || h <= 0) return false;

    out.width = w;
    out.height = h;
    out.bits.assign((size_t)w * h, 0);

    for (int y = 0; y < h; ++y) {
        int sy = cellInPaper.top + y;
        if (sy < 0 || sy >= inkH) continue;
        for (int x = 0; x < w; ++x) {
            int sx = cellInPaper.left + x;
            if (sx < 0 || sx >= inkW) continue;
            if (ink[(size_t)sy * inkW + sx] >= kInkThreshold) {
                out.bits[(size_t)y * w + x] = 1;
            }
        }
    }
    return true; // 墨が1画素も無いのも正常な結果として扱う
}

// ---------------------------------------------------------------------------
// 比較
// ---------------------------------------------------------------------------

ShapeMetrics ShapeCompare::CompareDirect(const ShapeMask& otehon, const ShapeMask& ink) {
    if (otehon.IsEmpty() || ink.IsEmpty()) return ShapeMetrics();
    if (otehon.width != ink.width || otehon.height != ink.height) return ShapeMetrics();

    int o = 0, i = 0, both = 0;
    size_t n = otehon.bits.size();
    for (size_t k = 0; k < n; ++k) {
        bool a = otehon.bits[k] != 0;
        bool b = ink.bits[k] != 0;
        if (a) ++o;
        if (b) ++i;
        if (a && b) ++both;
    }
    return MakeMetrics(o, i, both);
}

ShapeMetrics ShapeCompare::CompareNormalized(const ShapeMask& otehon, const ShapeMask& ink) {
    ShapeMask a = NormalizeToBox(otehon);
    ShapeMask b = NormalizeToBox(ink);
    if (a.IsEmpty() || b.IsEmpty()) return ShapeMetrics();
    return CompareDirect(a, b);
}

// ---------------------------------------------------------------------------
// 全体の評価
// ---------------------------------------------------------------------------

CompareResult ShapeCompare::Evaluate(HDC refDC, const AppState& state, GpuInk& gpuInk) {
    using namespace RenderUtils;
    CompareResult result;

    if (!state.otehon.HasPlacedChar()) {
        result.message = L"お手本がマスに置かれていません";
        return result;
    }

    std::vector<int> ink;
    int inkW = 0, inkH = 0;
    gpuInk.GetInkSnapshot(ink, inkW, inkH);
    if (ink.empty() || inkW <= 0 || inkH <= 0) {
        result.message = L"墨のデータがありません";
        return result;
    }

    const UIState& ui = state.ui;
    const RECT& rPaper = ui.rPaper;

    for (int i = 0; i < ui.gridCellCount; ++i) {
        const std::wstring& text = state.otehon.GetCellText(i);
        if (text.empty()) continue;

        CellCompare cc;
        cc.cellIndex = i;
        cc.text = text;

        const RECT& cell = ui.rGridCell[i];
        ShapeMask otehonMask;
        cc.hasOtehon = BuildOtehonMask(refDC, cell, text, state.otehon.fontStyle, otehonMask);
        if (!cc.hasOtehon) {
            result.cells.push_back(cc);
            continue;
        }

        // 升目はクライアント座標、墨のバッファは半紙ローカル。原点をずらして合わせる。
        RECT cellInPaper = {
            cell.left - rPaper.left, cell.top - rPaper.top,
            cell.right - rPaper.left, cell.bottom - rPaper.top
        };
        ShapeMask inkMask;
        if (!BuildInkMask(ink, inkW, inkH, cellInPaper, inkMask)) {
            result.cells.push_back(cc);
            continue;
        }
        cc.hasInk = inkMask.Count() > 0;

        cc.grid = CompareDirect(otehonMask, inkMask);
        cc.shape = CompareNormalized(otehonMask, inkMask);

        // 重心のずれと大きさの比。升目の寸法で割り、解像度に依存しない値にする。
        double ocx = 0.0, ocy = 0.0, icx = 0.0, icy = 0.0;
        if (otehonMask.GetCentroid(ocx, ocy) && inkMask.GetCentroid(icx, icy)) {
            int cw = RW(cell), ch = RH(cell);
            if (cw > 0) cc.centroidDx = (icx - ocx) / cw;
            if (ch > 0) cc.centroidDy = (icy - ocy) / ch;
        }
        RECT ob{}, ib{};
        if (otehonMask.GetBoundingBox(ob) && inkMask.GetBoundingBox(ib)) {
            int ow = ob.right - ob.left, oh = ob.bottom - ob.top;
            if (ow > 0) cc.sizeRatioX = (double)(ib.right - ib.left) / ow;
            if (oh > 0) cc.sizeRatioY = (double)(ib.bottom - ib.top) / oh;
        }

        result.cells.push_back(std::move(cc));
    }

    if (result.cells.empty()) {
        result.message = L"比較できるマスがありません";
        return result;
    }

    // 生の指標を 0〜100 へ写す。しきい値は EvaluationScore.cpp に集約。
    EvaluationScore::Apply(result);

    result.valid = true;
    return result;
}
