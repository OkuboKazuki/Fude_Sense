#include "stdafx.h"
#include "ImageExporter.h"
#include <shlobj.h>
#include <knownfolders.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <vector>

#pragma comment(lib, "windowscodecs.lib")

using Microsoft::WRL::ComPtr;

namespace {

// WIC (Windows Imaging Component) による PNG エンコード保存
bool SaveBitmapAsPng(HBITMAP hBmp, int width, int height, const wchar_t* filePath) {
    if (!hBmp || width <= 0 || height <= 0 || !filePath) return false;

    HRESULT hrCo = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    bool needUninit = SUCCEEDED(hrCo);

    bool success = false;
    bool fileCreated = false;
    do {
        ComPtr<IWICImagingFactory> pFactory;
        HRESULT hr = CoCreateInstance(
            CLSID_WICImagingFactory,
            NULL,
            CLSCTX_INPROC_SERVER,
            IID_PPV_ARGS(&pFactory)
        );
        if (FAILED(hr) || !pFactory) break;

        ComPtr<IWICBitmap> pWicBitmap;
        hr = pFactory->CreateBitmapFromHBITMAP(hBmp, NULL, WICBitmapIgnoreAlpha, &pWicBitmap);
        if (FAILED(hr) || !pWicBitmap) break;

        ComPtr<IWICStream> pStream;
        hr = pFactory->CreateStream(&pStream);
        if (FAILED(hr) || !pStream) break;

        hr = pStream->InitializeFromFilename(filePath, GENERIC_WRITE);
        if (FAILED(hr)) break;
        fileCreated = true;

        ComPtr<IWICBitmapEncoder> pEncoder;
        hr = pFactory->CreateEncoder(GUID_ContainerFormatPng, NULL, &pEncoder);
        if (FAILED(hr) || !pEncoder) break;

        hr = pEncoder->Initialize(pStream.Get(), WICBitmapEncoderNoCache);
        if (FAILED(hr)) break;

        ComPtr<IWICBitmapFrameEncode> pFrame;
        hr = pEncoder->CreateNewFrame(&pFrame, NULL);
        if (FAILED(hr) || !pFrame) break;

        hr = pFrame->Initialize(NULL);
        if (FAILED(hr)) break;

        hr = pFrame->SetSize(static_cast<UINT>(width), static_cast<UINT>(height));
        if (FAILED(hr)) break;

        WICPixelFormatGUID format = GUID_WICPixelFormat24bppBGR;
        hr = pFrame->SetPixelFormat(&format);
        if (FAILED(hr)) break;

        hr = pFrame->WriteSource(pWicBitmap.Get(), NULL);
        if (FAILED(hr)) break;

        hr = pFrame->Commit();
        if (FAILED(hr)) break;

        hr = pEncoder->Commit();
        if (FAILED(hr)) break;

        success = true;
    } while (false);

    // 作りかけの PNG を残すと、壊れたファイルがデスクトップに並ぶ。
    // ストリームはこの時点で解放済みなので消せる。
    if (!success && fileCreated) {
        DeleteFileW(filePath);
    }

    if (needUninit) {
        CoUninitialize();
    }
    return success;
}

// WIC 失敗時のフォールバック用 BMP 保存
bool SaveBitmapAsBmp(HDC memDC, HBITMAP memBmp, int pw, int ph, const wchar_t* filePath) {
    BITMAP bmp;
    GetObject(memBmp, sizeof(BITMAP), &bmp);

    BITMAPFILEHEADER bfh = { 0 };
    BITMAPINFOHEADER bih = { 0 };

    bih.biSize = sizeof(BITMAPINFOHEADER);
    bih.biWidth = pw;
    bih.biHeight = ph;
    bih.biPlanes = 1;
    bih.biBitCount = 24;
    bih.biCompression = BI_RGB;

    DWORD dwSize = ((pw * bih.biBitCount + 31) / 32) * 4 * ph;
    bfh.bfType = 0x4D42; // "BM"
    bfh.bfSize = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER) + dwSize;
    bfh.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);

    std::vector<BYTE> buf(dwSize);
    GetDIBits(memDC, memBmp, 0, ph, buf.data(), (BITMAPINFO*)&bih, DIB_RGB_COLORS);

    HANDLE hFile = CreateFileW(filePath, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) return false;

    // 開けただけでは成功ではない。容量不足などで途中までしか書けていないことがある
    auto writeAll = [hFile](const void* data, DWORD size) {
        DWORD written = 0;
        return WriteFile(hFile, data, size, &written, NULL) && written == size;
    };
    bool ok = writeAll(&bfh, sizeof(bfh))
        && writeAll(&bih, sizeof(bih))
        && writeAll(buf.data(), dwSize);
    CloseHandle(hFile);
    if (!ok) DeleteFileW(filePath);
    return ok;
}

} // namespace

bool ImageExporter::ExportCanvas(HWND hWnd, GpuInk& gpuInk, AppState& state, bool toClipboard) {
    using namespace RenderUtils;

    int pw = RW(state.ui.rPaper);
    int ph = RH(state.ui.rPaper);
    if (pw <= 0 || ph <= 0) return false;

    HDC screenDC = GetDC(hWnd);
    HDC memDC = CreateCompatibleDC(screenDC);
    HBITMAP memBmp = CreateCompatibleBitmap(screenDC, pw, ph);
    HBITMAP oldBmp = (HBITMAP)SelectObject(memDC, memBmp);

    // 1. 半紙背景
    RECT rLocal = { 0, 0, pw, ph };
    HBRUSH pb = CreateSolidBrush(RGB(248, 247, 242));
    FillRect(memDC, &rLocal, pb);
    DeleteObject(pb);

    // 2. 墨汁レンダリング
    // お手本はなぞるための下敷きであって作品の一部ではないため、書き出しには
    // 含めない（画面表示のみ）。下敷きの罫線を書き出さないのと同じ扱い。
    gpuInk.Render(memDC, 0, 0, pw, ph);

    bool success = false;
    if (toClipboard) {
        if (OpenClipboard(hWnd)) {
            EmptyClipboard();
            // クリップボードへ所有権を引き渡す（成功時はシステムがメモリを管理するため DeleteObject してはならない）
            if (SetClipboardData(CF_BITMAP, memBmp)) {
                state.SetSaveFeedback(L"✓ クリップボードにコピーしました");
                success = true;
            }
            CloseClipboard();
        }
    } else {
        PWSTR pDeskPath = nullptr;
        if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Desktop, 0, NULL, &pDeskPath))) {
            SYSTEMTIME st;
            GetLocalTime(&st);
            wchar_t filePath[MAX_PATH];
            swprintf_s(filePath, MAX_PATH, L"%s\\習字作品_%04d%02d%02d_%02d%02d%02d.png",
                pDeskPath, st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);

            // WIC による PNG 保存を実行
            if (SaveBitmapAsPng(memBmp, pw, ph, filePath)) {
                state.SetSaveFeedback(L"✓ デスクトップにPNG保存しました");
                success = true;
            } else {
                // WIC 失敗時は拡張子を .bmp にしてフォールバック保存
                swprintf_s(filePath, MAX_PATH, L"%s\\習字作品_%04d%02d%02d_%02d%02d%02d.bmp",
                    pDeskPath, st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
                if (SaveBitmapAsBmp(memDC, memBmp, pw, ph, filePath)) {
                    state.SetSaveFeedback(L"✓ デスクトップに保存しました(BMP)");
                    success = true;
                }
            }
            CoTaskMemFree(pDeskPath);
        }
    }

    if (!success) {
        state.SetSaveFeedback(toClipboard ? L"× クリップボードにコピーできませんでした"
                                          : L"× 画像を保存できませんでした", true);
    }

    SelectObject(memDC, oldBmp);
    // クリップボードへ正常に渡した場合はシステムが所有するため破棄しない
    if (!toClipboard || !success) {
        DeleteObject(memBmp);
    }
    DeleteDC(memDC);
    ReleaseDC(hWnd, screenDC);

    return success;
}
