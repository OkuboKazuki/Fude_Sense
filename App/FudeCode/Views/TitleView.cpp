#include "stdafx.h"
#include "TitleView.h"
#include "RenderUtils.h"
#include <wincodec.h>
#include <vector>
#include <string>
#include <cmath>
#include <algorithm>

#pragma comment(lib, "msimg32.lib")
#pragma comment(lib, "windowscodecs.lib")

static IWICImagingFactory*   s_pWicFactory = nullptr;
static IWICFormatConverter*  s_pLogoConverter = nullptr;
static HBITMAP               s_hScaledBmp = nullptr;
static HDC                   s_scaledDC = nullptr;
static HBITMAP               s_oldScaledBmp = nullptr;
static int                   s_scaledW = 0;
static int                   s_scaledH = 0;
static int                   s_logoSourceW = 0;
static int                   s_logoSourceH = 0;
static bool                  s_logoLoadAttempted = false;

void TitleView::ReleaseResources() {
    if (s_scaledDC) {
        if (s_oldScaledBmp) {
            SelectObject(s_scaledDC, s_oldScaledBmp);
            s_oldScaledBmp = nullptr;
        }
        DeleteDC(s_scaledDC);
        s_scaledDC = nullptr;
    }
    if (s_hScaledBmp) {
        DeleteObject(s_hScaledBmp);
        s_hScaledBmp = nullptr;
    }
    if (s_pLogoConverter) {
        s_pLogoConverter->Release();
        s_pLogoConverter = nullptr;
    }
    if (s_pWicFactory) {
        s_pWicFactory->Release();
        s_pWicFactory = nullptr;
    }
    s_scaledW = 0;
    s_scaledH = 0;
    s_logoSourceW = 0;
    s_logoSourceH = 0;
    s_logoLoadAttempted = false;
}

void TitleView::EnsureLogoLoaded() {
    if (s_logoLoadAttempted) return;
    s_logoLoadAttempted = true;

    std::vector<std::wstring> candidates;

    // 1. exe と同じ場所 (PostBuildEvent でコピーされる)
    wchar_t exePath[MAX_PATH] = {};
    if (GetModuleFileNameW(nullptr, exePath, MAX_PATH) > 0) {
        wchar_t* lastSlash = wcsrchr(exePath, L'\\');
        if (lastSlash) {
            *(lastSlash + 1) = L'\0';
            candidates.push_back(std::wstring(exePath) + L"title_logo.png");
            candidates.push_back(std::wstring(exePath) + L"..\\title_logo.png");
            candidates.push_back(std::wstring(exePath) + L"..\\..\\title_logo.png");
        }
    }

    // 2. カレントディレクトリ基準
    candidates.push_back(L"title_logo.png");
    candidates.push_back(L"App\\FudeCode\\title_logo.png");
    candidates.push_back(L"FudeCode\\title_logo.png");

    std::wstring foundPath;
    for (const auto& path : candidates) {
        DWORD attr = GetFileAttributesW(path.c_str());
        if (attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY)) {
            foundPath = path;
            break;
        }
    }

    if (foundPath.empty()) return;

    // WIC 画像デコード
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&s_pWicFactory));
    if (FAILED(hr) || !s_pWicFactory) return;

    IWICBitmapDecoder* pDecoder = nullptr;
    hr = s_pWicFactory->CreateDecoderFromFilename(foundPath.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &pDecoder);
    if (SUCCEEDED(hr) && pDecoder) {
        IWICBitmapFrameDecode* pFrame = nullptr;
        hr = pDecoder->GetFrame(0, &pFrame);
        if (SUCCEEDED(hr) && pFrame) {
            hr = s_pWicFactory->CreateFormatConverter(&s_pLogoConverter);
            if (SUCCEEDED(hr) && s_pLogoConverter) {
                // Premultiplied BGRA (AlphaBlend に最適)
                hr = s_pLogoConverter->Initialize(pFrame, GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom);
                if (SUCCEEDED(hr)) {
                    UINT w = 0, h = 0;
                    s_pLogoConverter->GetSize(&w, &h);
                    if (w > 0 && h > 0) {
                        s_logoSourceW = static_cast<int>(w);
                        s_logoSourceH = static_cast<int>(h);
                    }
                }
            }
            pFrame->Release();
        }
        pDecoder->Release();
    }
}

static bool EnsureScaledLogo(int targetW, int targetH) {
    if (!s_pWicFactory || !s_pLogoConverter || targetW <= 0 || targetH <= 0) return false;
    if (s_scaledDC && s_scaledW == targetW && s_scaledH == targetH) return true;

    if (s_scaledDC) {
        if (s_oldScaledBmp) {
            SelectObject(s_scaledDC, s_oldScaledBmp);
            s_oldScaledBmp = nullptr;
        }
        DeleteDC(s_scaledDC);
        s_scaledDC = nullptr;
    }
    if (s_hScaledBmp) {
        DeleteObject(s_hScaledBmp);
        s_hScaledBmp = nullptr;
    }
    s_scaledW = 0;
    s_scaledH = 0;

    // WIC 高品質バイキュービック・スケーラーで滑らかにリサイズ
    IWICBitmapScaler* pScaler = nullptr;
    HRESULT hr = s_pWicFactory->CreateBitmapScaler(&pScaler);
    if (FAILED(hr) || !pScaler) return false;

    hr = pScaler->Initialize(s_pLogoConverter, targetW, targetH, WICBitmapInterpolationModeHighQualityCubic);
    if (SUCCEEDED(hr)) {
        BITMAPINFO bmi = {};
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = targetW;
        bmi.bmiHeader.biHeight = -targetH; // Top-down
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;

        void* pBits = nullptr;
        HDC screenDC = GetDC(nullptr);
        s_hScaledBmp = CreateDIBSection(screenDC, &bmi, DIB_RGB_COLORS, &pBits, nullptr, 0);
        if (s_hScaledBmp && pBits) {
            UINT stride = targetW * 4;
            UINT bufferSize = stride * targetH;
            pScaler->CopyPixels(nullptr, stride, bufferSize, static_cast<BYTE*>(pBits));

            s_scaledDC = CreateCompatibleDC(screenDC);
            s_oldScaledBmp = (HBITMAP)SelectObject(s_scaledDC, s_hScaledBmp);
            s_scaledW = targetW;
            s_scaledH = targetH;
        }
        ReleaseDC(nullptr, screenDC);
    }
    pScaler->Release();
    return (s_scaledDC != nullptr);
}

void TitleView::Draw(HDC dc, int width, int height, const AppState& state) {
    DrawBackground(dc, width, height);
    DrawCenterContent(dc, width, height, state);
}

void TitleView::DrawBackground(HDC dc, int width, int height) {
    using namespace RenderUtils;

    // 半紙と同じ清らかな白背景
    RECT rBg = { 0, 0, width, height };
    Fill(dc, rBg, RGB(255, 255, 255));

    // ごく繊細な和モダン・外枠アクセント
    HPEN pBorder = CreatePen(PS_SOLID, 1, RGB(235, 238, 245));
    HPEN oldP = (HPEN)SelectObject(dc, pBorder);
    HBRUSH oldB = (HBRUSH)SelectObject(dc, GetStockObject(HOLLOW_BRUSH));

    int pad = 24;
    Rectangle(dc, pad, pad, width - pad, height - pad);

    SelectObject(dc, oldB);
    SelectObject(dc, oldP);
    DeleteObject(pBorder);
}

void TitleView::DrawCenterContent(HDC dc, int width, int height, const AppState& state) {
    using namespace RenderUtils;

    EnsureLogoLoaded();

    int cx = width / 2;
    int cy = height / 2;

    int logoY = cy - 100;
    int logoH = 0;

    // 1. 手書き毛筆ロゴ「Fude Sense」の高品質アンチエイリアス描画（大判表示）
    if (s_pLogoConverter && s_logoSourceW > 0 && s_logoSourceH > 0) {
        float maxW = (std::min)(static_cast<float>(width) * 0.85f, 1080.0f);
        float maxH = static_cast<float>(height) * 0.52f;
        float scaleW = maxW / static_cast<float>(s_logoSourceW);
        float scaleH = maxH / static_cast<float>(s_logoSourceH);
        float scale = (std::min)(scaleW, scaleH);

        int targetW = static_cast<int>(s_logoSourceW * scale);
        int targetH = static_cast<int>(s_logoSourceH * scale);
        logoH = targetH;

        int logoX = cx - targetW / 2;
        logoY = cy - targetH / 2 - 40;

        if (EnsureScaledLogo(targetW, targetH)) {
            BLENDFUNCTION bf = {};
            bf.BlendOp = AC_SRC_OVER;
            bf.BlendFlags = 0;
            bf.SourceConstantAlpha = 255;
            bf.AlphaFormat = AC_SRC_ALPHA;

            AlphaBlend(dc, logoX, logoY, targetW, targetH, s_scaledDC, 0, 0, targetW, targetH, bf);
        }
    } else {
        RECT rMainTitle = { cx - 500, cy - 90, cx + 500, cy + 20 };
        HFONT fMainTitle = CreateCustomFont(72, FW_BOLD);
        DrawTextCustom(dc, rMainTitle, L"Fude Sense", fMainTitle, RGB(24, 26, 34), DT_CENTER | DT_SINGLELINE | DT_VCENTER);
        DeleteObject(fMainTitle);
        logoH = 90;
        logoY = cy - 45;
    }

    // 2. 「タップして硯へ向かう」ブレスアニメーション（タイトル文字と画面最下部の中間位置に配置）
    int logoBottom = logoY + logoH;
    int promptY = logoBottom + (height - logoBottom) / 2 - 24;
    RECT rPrompt = { cx - 400, promptY, cx + 400, promptY + 48 };

    // 呼吸するようにゆったり明滅するブレスアニメーション (sin波)
    DWORD tick = GetTickCount();
    double pulse = (std::sin(static_cast<double>(tick) * 0.0038) + 1.0) * 0.5; // 0.0 ~ 1.0

    // 濃い漆黒の墨色 (RGB 18, 20, 26) から 非常に淡い薄墨 (RGB 215, 218, 226) へと濃淡が大きく変化
    int inkVal = static_cast<int>(18.0 + (1.0 - pulse) * 196.0); // 18 (濃墨) ~ 214 (薄墨)
    COLORREF textColor = RGB(inkVal, inkVal + 2, inkVal + 6);

    HFONT fPrompt = CreateCustomFont(28, FW_BOLD);
    Center(dc, rPrompt, L"— タップして硯へ向かう —", fPrompt, textColor);
    DeleteObject(fPrompt);
}

