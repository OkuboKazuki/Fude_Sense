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

static HBITMAP s_hLogoBmp = nullptr;
static HDC     s_logoDC = nullptr;
static HBITMAP s_oldLogoBmp = nullptr;
static int     s_logoSourceW = 0;
static int     s_logoSourceH = 0;
static bool    s_logoLoadAttempted = false;

void TitleView::ReleaseResources() {
    if (s_logoDC) {
        if (s_oldLogoBmp) {
            SelectObject(s_logoDC, s_oldLogoBmp);
            s_oldLogoBmp = nullptr;
        }
        DeleteDC(s_logoDC);
        s_logoDC = nullptr;
    }
    if (s_hLogoBmp) {
        DeleteObject(s_hLogoBmp);
        s_hLogoBmp = nullptr;
    }
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
    candidates.push_back(L"App\\SampleCode\\title_logo.png");
    candidates.push_back(L"SampleCode\\title_logo.png");

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

    IWICImagingFactory* pFactory = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pFactory));
    if (FAILED(hr) || !pFactory) return;

    IWICBitmapDecoder* pDecoder = nullptr;
    hr = pFactory->CreateDecoderFromFilename(foundPath.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &pDecoder);
    if (SUCCEEDED(hr) && pDecoder) {
        IWICBitmapFrameDecode* pFrame = nullptr;
        hr = pDecoder->GetFrame(0, &pFrame);
        if (SUCCEEDED(hr) && pFrame) {
            IWICFormatConverter* pConverter = nullptr;
            hr = pFactory->CreateFormatConverter(&pConverter);
            if (SUCCEEDED(hr) && pConverter) {
                // Premultiplied BGRA (AlphaBlend に最適)
                hr = pConverter->Initialize(pFrame, GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom);
                if (SUCCEEDED(hr)) {
                    UINT w = 0, h = 0;
                    pConverter->GetSize(&w, &h);
                    if (w > 0 && h > 0) {
                        s_logoSourceW = static_cast<int>(w);
                        s_logoSourceH = static_cast<int>(h);

                        BITMAPINFO bmi = {};
                        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
                        bmi.bmiHeader.biWidth = s_logoSourceW;
                        bmi.bmiHeader.biHeight = -s_logoSourceH; // Top-down
                        bmi.bmiHeader.biPlanes = 1;
                        bmi.bmiHeader.biBitCount = 32;
                        bmi.bmiHeader.biCompression = BI_RGB;

                        void* pBits = nullptr;
                        HDC screenDC = GetDC(nullptr);
                        s_hLogoBmp = CreateDIBSection(screenDC, &bmi, DIB_RGB_COLORS, &pBits, nullptr, 0);
                        if (s_hLogoBmp && pBits) {
                            UINT stride = s_logoSourceW * 4;
                            UINT bufferSize = stride * s_logoSourceH;
                            pConverter->CopyPixels(nullptr, stride, bufferSize, static_cast<BYTE*>(pBits));

                            s_logoDC = CreateCompatibleDC(screenDC);
                            s_oldLogoBmp = (HBITMAP)SelectObject(s_logoDC, s_hLogoBmp);
                        }
                        ReleaseDC(nullptr, screenDC);
                    }
                }
                pConverter->Release();
            }
            pFrame->Release();
        }
        pDecoder->Release();
    }
    pFactory->Release();
}

void TitleView::Draw(HDC dc, int width, int height, const AppState& state) {
    DrawBackground(dc, width, height);
    DrawCenterContent(dc, width, height, state);
}

void TitleView::DrawBackground(HDC dc, int width, int height) {
    using namespace RenderUtils;

    // 半紙と同じ清らかな白背景（生成りの温かみを持たせた純白）
    RECT rBg = { 0, 0, width, height };
    Fill(dc, rBg, RGB(255, 255, 255));

    // ごく繊細な和モダン・外枠アクセント（上品な極細ライン）
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

    // 1. 手書き毛筆ロゴ「Fude Sense」の描画（画面いっぱいに迫力ある大判表示）
    if (s_logoDC && s_logoSourceW > 0 && s_logoSourceH > 0) {
        // 画面幅・高さに応じたダイナミックなサイズ調整（最大幅1080px、最大高さ画面の52%）
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

        BLENDFUNCTION bf = {};
        bf.BlendOp = AC_SRC_OVER;
        bf.BlendFlags = 0;
        bf.SourceConstantAlpha = 255;
        bf.AlphaFormat = AC_SRC_ALPHA;

        AlphaBlend(dc, logoX, logoY, targetW, targetH, s_logoDC, 0, 0, s_logoSourceW, s_logoSourceH, bf);
    } else {
        // フォールバック（画像がまだない場合のテキスト描画）
        RECT rMainTitle = { cx - 500, cy - 90, cx + 500, cy + 20 };
        HFONT fMainTitle = CreateCustomFont(72, FW_BOLD);
        DrawTextCustom(dc, rMainTitle, L"Fude Sense", fMainTitle, RGB(24, 26, 34), DT_CENTER | DT_SINGLELINE | DT_VCENTER);
        DeleteObject(fMainTitle);
        logoH = 90;
        logoY = cy - 45;
    }

    // 2. 「タップして硯に向かう」ブレスアニメーション
    int promptY = (std::min)(logoY + logoH + 40, height - 90);
    RECT rPrompt = { cx - 190, promptY, cx + 190, promptY + 48 };

    // 呼吸するようにゆったり明滅するブレスアニメーション (sin波)
    DWORD tick = GetTickCount();
    double pulse = (std::sin(static_cast<double>(tick) * 0.0035) + 1.0) * 0.5; // 0.0 ~ 1.0
    int borderVal = 190 - static_cast<int>(pulse * 65.0); // 190 ~ 125

    COLORREF textColor = RGB(28, 32, 42);
    COLORREF frameBg = RGB(250, 252, 255);
    COLORREF frameBorder = RGB(borderVal, borderVal + 5, borderVal + 18);

    Box(dc, rPrompt, frameBg, frameBorder, 1, 24);

    HFONT fPrompt = CreateCustomFont(16, FW_BOLD);
    Center(dc, rPrompt, L"— タップして硯に向かう —", fPrompt, textColor);
    DeleteObject(fPrompt);
}

