#include "stdafx.h"
#include "RenderUtils.h"
#include <cmath>
#include <vector>
#include <string>
#include <wincodec.h>
#pragma comment(lib, "windowscodecs.lib")

namespace RenderUtils {

    static HBITMAP s_hDeskSourceBmp = nullptr;
    static int s_deskSourceW = 0;
    static int s_deskSourceH = 0;
    static bool s_deskImageLoadAttempted = false;

    static HBITMAP s_hWoodDeskCacheBmp = nullptr;
    static int s_woodDeskCacheW = 0;
    static int s_woodDeskCacheH = 0;

    static void LoadDeskImageFromFile() {
        if (s_deskImageLoadAttempted) return;
        s_deskImageLoadAttempted = true;

        std::vector<std::wstring> candidates;

        // 1. exe と同じ場所。ビルド後にここへコピーしている（FudeSense.vcxproj の
        //    PostBuildEvent）ので、配布時はこれだけで見つかる。
        wchar_t exePath[MAX_PATH] = {};
        if (GetModuleFileNameW(nullptr, exePath, MAX_PATH) > 0) {
            wchar_t* lastSlash = wcsrchr(exePath, L'\\');
            if (lastSlash) {
                *(lastSlash + 1) = L'\0';
                candidates.push_back(std::wstring(exePath) + L"desk_texture.jpg");
                // 2. exe の親・祖父ディレクトリ。Debug/ や Release/ から
                //    ソースツリーの App\SampleCode\ を辿るための保険。
                candidates.push_back(std::wstring(exePath) + L"..\\desk_texture.jpg");
                candidates.push_back(std::wstring(exePath) + L"..\\..\\desk_texture.jpg");
            }
        }

        // 3. カレントディレクトリ基準。Visual Studio から作業ディレクトリを
        //    変えて起動した場合の保険。見つからなければプロシージャル木目へ落ちる。
        candidates.push_back(L"desk_texture.jpg");
        candidates.push_back(L"App\\SampleCode\\desk_texture.jpg");
        candidates.push_back(L"SampleCode\\desk_texture.jpg");

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
                    hr = pConverter->Initialize(pFrame, GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom);
                    if (SUCCEEDED(hr)) {
                        UINT w = 0, h = 0;
                        pConverter->GetSize(&w, &h);
                        if (w > 0 && h > 0) {
                            s_deskSourceW = static_cast<int>(w);
                            s_deskSourceH = static_cast<int>(h);

                            BITMAPINFO bmi = {};
                            bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
                            bmi.bmiHeader.biWidth = s_deskSourceW;
                            bmi.bmiHeader.biHeight = -s_deskSourceH; // Top-down
                            bmi.bmiHeader.biPlanes = 1;
                            bmi.bmiHeader.biBitCount = 32;
                            bmi.bmiHeader.biCompression = BI_RGB;

                            void* pBits = nullptr;
                            HDC screenDC = GetDC(nullptr);
                            s_hDeskSourceBmp = CreateDIBSection(screenDC, &bmi, DIB_RGB_COLORS, &pBits, nullptr, 0);
                            ReleaseDC(nullptr, screenDC);

                            if (s_hDeskSourceBmp && pBits) {
                                UINT stride = s_deskSourceW * 4;
                                UINT bufferSize = stride * s_deskSourceH;
                                pConverter->CopyPixels(nullptr, stride, bufferSize, static_cast<BYTE*>(pBits));
                            }
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

    HFONT CreateCustomFont(int size, int weight, const wchar_t* face) {
        return CreateFontW(size, 0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
            DEFAULT_PITCH | FF_DONTCARE, face);
    }

    void Fill(HDC dc, const RECT& r, COLORREF color) {
        HBRUSH b = CreateSolidBrush(color);
        if (!b) return;
        FillRect(dc, &r, b);
        DeleteObject(b);
    }

    void Box(HDC dc, const RECT& r, COLORREF fill, COLORREF border, int bw, int round) {
        HBRUSH b = CreateSolidBrush(fill);
        HPEN p = CreatePen(PS_SOLID, bw, border);
        if (!b || !p) {
            if (b) DeleteObject(b);
            if (p) DeleteObject(p);
            return;
        }
        HBRUSH ob = (HBRUSH)SelectObject(dc, b);
        HPEN op = (HPEN)SelectObject(dc, p);
        RoundRect(dc, r.left, r.top, r.right, r.bottom, round, round);
        SelectObject(dc, op);
        SelectObject(dc, ob);
        DeleteObject(p);
        DeleteObject(b);
    }

    void DrawShadow(HDC dc, const RECT& r, int blurSize, int round) {
        if (blurSize <= 0) return;
        HBRUSH ob = (HBRUSH)SelectObject(dc, GetStockObject(NULL_BRUSH));
        for (int i = blurSize; i >= 1; --i) {
            // 机の木目に自然に溶け込むダークシャドウ
            int shade = 10 + (int)(16.0 * ((double)i / blurSize));
            COLORREF c = RGB(shade, shade * 0.75, shade * 0.6);
            HPEN p = CreatePen(PS_SOLID, 2, c);
            HPEN op = (HPEN)SelectObject(dc, p);
            RoundRect(dc, r.left - i + 3, r.top - i + 5, r.right + i + 3, r.bottom + i + 5, round + i * 2, round + i * 2);
            SelectObject(dc, op);
            DeleteObject(p);
        }
        SelectObject(dc, ob);
    }

    void DrawWoodDesk(HDC dc, int width, int height) {
        if (width <= 0 || height <= 0) return;

        LoadDeskImageFromFile();

        // 1. 画像からロードされた本物の木目テクスチャを描画
        if (s_hDeskSourceBmp && s_deskSourceW > 0 && s_deskSourceH > 0) {
            if (s_hWoodDeskCacheBmp && (s_woodDeskCacheW != width || s_woodDeskCacheH != height)) {
                DeleteObject(s_hWoodDeskCacheBmp);
                s_hWoodDeskCacheBmp = nullptr;
            }

            if (!s_hWoodDeskCacheBmp) {
                s_woodDeskCacheW = width;
                s_woodDeskCacheH = height;

                HDC srcDC = CreateCompatibleDC(dc);
                HDC dstDC = CreateCompatibleDC(dc);

                s_hWoodDeskCacheBmp = CreateCompatibleBitmap(dc, width, height);

                HBITMAP oldSrc = (HBITMAP)SelectObject(srcDC, s_hDeskSourceBmp);
                HBITMAP oldDst = (HBITMAP)SelectObject(dstDC, s_hWoodDeskCacheBmp);

                SetStretchBltMode(dstDC, HALFTONE);
                SetBrushOrgEx(dstDC, 0, 0, nullptr);

                // アスペクト比を保ちつつ画面全体をカバー
                double scaleX = (double)width / s_deskSourceW;
                double scaleY = (double)height / s_deskSourceH;
                double scale = (std::max)(scaleX, scaleY);

                int drawW = (int)(s_deskSourceW * scale);
                int drawH = (int)(s_deskSourceH * scale);
                int offX = (width - drawW) / 2;
                int offY = (height - drawH) / 2;

                StretchBlt(dstDC, offX, offY, drawW, drawH, srcDC, 0, 0, s_deskSourceW, s_deskSourceH, SRCCOPY);

                SelectObject(srcDC, oldSrc);
                SelectObject(dstDC, oldDst);
                DeleteDC(srcDC);
                DeleteDC(dstDC);
            }

            if (s_hWoodDeskCacheBmp) {
                HDC memDC = CreateCompatibleDC(dc);
                HBITMAP oldBmp = (HBITMAP)SelectObject(memDC, s_hWoodDeskCacheBmp);
                BitBlt(dc, 0, 0, width, height, memDC, 0, 0, SRCCOPY);
                SelectObject(memDC, oldBmp);
                DeleteDC(memDC);
                return;
            }
        }

        // 2. フォールバック: プロシージャル木目
        if (s_hWoodDeskCacheBmp && (s_woodDeskCacheW != width || s_woodDeskCacheH != height)) {
            DeleteObject(s_hWoodDeskCacheBmp);
            s_hWoodDeskCacheBmp = nullptr;
        }

        if (!s_hWoodDeskCacheBmp) {
            s_woodDeskCacheW = width;
            s_woodDeskCacheH = height;

            BITMAPINFO bmi = {};
            bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            bmi.bmiHeader.biWidth = width;
            bmi.bmiHeader.biHeight = -height;
            bmi.bmiHeader.biPlanes = 1;
            bmi.bmiHeader.biBitCount = 32;
            bmi.bmiHeader.biCompression = BI_RGB;

            void* pBits = nullptr;
            s_hWoodDeskCacheBmp = CreateDIBSection(dc, &bmi, DIB_RGB_COLORS, &pBits, nullptr, 0);

            if (s_hWoodDeskCacheBmp && pBits) {
                uint32_t* pixels = static_cast<uint32_t*>(pBits);
                const int boardH = 190;
                double cx = width * 0.5;
                double cy = height * 0.5;

                for (int y = 0; y < height; ++y) {
                    int boardIdx = y / boardH;
                    int inBoardY = y % boardH;
                    double boardSeed = (boardIdx * 19) % 7 - 3.0;
                    double wave = std::sin(y * 0.032 + std::sin(y * 0.007) * 4.2);
                    double fine = std::sin(y * 0.38 + std::cos(y * 0.06) * 2.2) * 0.45;

                    for (int x = 0; x < width; ++x) {
                        double grainX = std::sin((x * 0.0018 + wave * 0.45) * 3.14159);
                        double grain = wave * 0.65 + fine + grainX * 0.35;

                        double edgeShadow = 0.0;
                        if (inBoardY <= 2) {
                            edgeShadow = (2 - inBoardY + 1) * -9.0;
                        } else if (inBoardY == 3) {
                            edgeShadow = 6.0;
                        }

                        double dx = (x - cx) / cx;
                        double dy = (y - cy) / cy;
                        double vig = 1.0 - (dx * dx * 0.20 + dy * dy * 0.16);

                        double r = (54.0 + boardSeed + grain * 8.0 + edgeShadow) * vig;
                        double g = (37.0 + boardSeed * 0.8 + grain * 5.5 + edgeShadow * 0.8) * vig;
                        double b = (26.0 + boardSeed * 0.6 + grain * 3.8 + edgeShadow * 0.6) * vig;

                        int ir = Clamp(static_cast<int>(r), 16, 240);
                        int ig = Clamp(static_cast<int>(g), 10, 220);
                        int ib = Clamp(static_cast<int>(b), 6, 200);

                        pixels[y * width + x] = (static_cast<uint32_t>(ir) << 16) |
                                               (static_cast<uint32_t>(ig) << 8)  |
                                                static_cast<uint32_t>(ib);
                    }
                }
            }
        }

        if (s_hWoodDeskCacheBmp) {
            HDC memDC = CreateCompatibleDC(dc);
            HBITMAP oldBmp = (HBITMAP)SelectObject(memDC, s_hWoodDeskCacheBmp);
            BitBlt(dc, 0, 0, width, height, memDC, 0, 0, SRCCOPY);
            SelectObject(memDC, oldBmp);
            DeleteDC(memDC);
        }
    }

    void DrawTextCustom(HDC dc, RECT r, const wchar_t* s, HFONT f, COLORREF c, UINT align) {
        HFONT old = f ? (HFONT)SelectObject(dc, f) : nullptr;
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, c);
        DrawTextW(dc, s, -1, &r, align | DT_NOPREFIX);
        if (f && old) SelectObject(dc, old);
    }

    void Center(HDC dc, RECT r, const wchar_t* s, HFONT f, COLORREF c) {
        DrawTextCustom(dc, r, s, f, c, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }

    void DrawPanelTitle(HDC dc, const RECT& rPanel, const wchar_t* title) {
        RECT rTitle = { rPanel.left + 18, 16, rPanel.right - 18, 44 };
        HFONT f = CreateCustomFont(18, FW_BOLD);
        DrawTextCustom(dc, rTitle, title, f, RGB(245, 248, 252));
        DeleteObject(f);
    }

    void DrawTileCard(HDC dc, RECT r, const wchar_t* title, const wchar_t* sub, bool active, bool hover) {
        COLORREF bg, border, textMain, textSub;
        if (active) {
            bg = RGB(42, 68, 108);
            border = RGB(85, 145, 235);
            textMain = RGB(255, 255, 255);
            textSub = RGB(175, 215, 255);
        } else if (hover) {
            bg = RGB(48, 52, 64);
            border = RGB(78, 86, 104);
            textMain = RGB(248, 250, 255);
            textSub = RGB(190, 196, 210);
        } else {
            bg = RGB(32, 35, 42);
            border = RGB(48, 52, 64);
            textMain = RGB(205, 210, 220);
            textSub = RGB(140, 146, 160);
        }

        Box(dc, r, bg, border, 1, 8);
        HFONT f1 = CreateCustomFont(22, FW_BOLD);
        HFONT f2 = CreateCustomFont(15, FW_NORMAL);
        RECT r1 = { r.left + 4, r.top + 4, r.right - 4, r.top + 34 };
        RECT r2 = { r.left + 4, r.top + 34, r.right - 4, r.bottom - 4 };
        Center(dc, r1, title, f1, textMain);
        Center(dc, r2, sub, f2, textSub);
        DeleteObject(f1);
        DeleteObject(f2);
    }

    void DrawColorThemeButton(HDC dc, RECT r, const wchar_t* label, COLORREF dotColor, bool active, bool hover) {
        COLORREF bg = active ? RGB(45, 66, 100) : (hover ? RGB(46, 50, 60) : RGB(32, 35, 42));
        COLORREF border = active ? RGB(85, 145, 235) : (hover ? RGB(75, 82, 98) : RGB(48, 52, 64));
        Box(dc, r, bg, border, 1, 8);

        HBRUSH db = CreateSolidBrush(dotColor);
        HBRUSH odb = (HBRUSH)SelectObject(dc, db);
        HPEN dp = CreatePen(PS_SOLID, 1, active ? RGB(255, 255, 255) : RGB(110, 115, 128));
        HPEN odp = (HPEN)SelectObject(dc, dp);
        int dotSize = 16;
        int dotLeft = r.left + 12;
        int dotTop = r.top + (RH(r) - dotSize) / 2;
        Ellipse(dc, dotLeft, dotTop, dotLeft + dotSize, dotTop + dotSize);
        SelectObject(dc, odp);
        SelectObject(dc, odb);
        DeleteObject(dp);
        DeleteObject(db);

        HFONT f = CreateCustomFont(19, active ? FW_BOLD : FW_NORMAL);
        RECT tr = { r.left + 32, r.top, r.right - 4, r.bottom };
        Center(dc, tr, label, f, active ? RGB(255, 255, 255) : RGB(215, 220, 230));
        DeleteObject(f);
    }

    void DrawSubCard(HDC dc, RECT r, const wchar_t* name, const wchar_t* detail, int strokeWidth, bool active, bool hover) {
        COLORREF bg, border;
        if (active) {
            bg = RGB(42, 64, 98);
            border = RGB(85, 140, 220);
        } else if (hover) {
            bg = RGB(46, 50, 62);
            border = RGB(75, 82, 100);
        } else {
            bg = RGB(34, 37, 46);
            border = RGB(50, 54, 66);
        }

        Box(dc, r, bg, border, 1, 10);

        RECT tipBox = { r.left + 16, r.top + 14, r.left + 60, r.bottom - 14 };
        int cy = (tipBox.top + tipBox.bottom) / 2;
        HPEN sp = CreatePen(PS_SOLID, (int)(strokeWidth * 1.8 + 2), active ? RGB(110, 195, 255) : RGB(175, 180, 192));
        HPEN osp = (HPEN)SelectObject(dc, sp);
        MoveToEx(dc, tipBox.left + 4, cy, nullptr);
        LineTo(dc, tipBox.right - 4, cy);
        SelectObject(dc, osp);
        DeleteObject(sp);

        HFONT f1 = CreateCustomFont(26, FW_BOLD);
        HFONT f2 = CreateCustomFont(16, FW_NORMAL);
        RECT a = { r.left + 70, r.top + 10, r.right - 14, r.top + 48 };
        RECT b = { r.left + 70, r.top + 48, r.right - 14, r.bottom - 10 };
        DrawTextCustom(dc, a, name, f1, active ? RGB(255, 255, 255) : RGB(238, 242, 248));
        DrawTextCustom(dc, b, detail, f2, active ? RGB(185, 218, 255) : RGB(160, 166, 178));
        DeleteObject(f1);
        DeleteObject(f2);
    }

} // namespace RenderUtils
