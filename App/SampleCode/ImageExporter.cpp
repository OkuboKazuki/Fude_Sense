#include "stdafx.h"
#include "ImageExporter.h"
#include <shlobj.h>
#include <vector>

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

    // 2. お手本文字
    if (state.otehon.isVisible && state.otehon.selectedIndex >= 0 && state.otehon.selectedIndex < 8) {
        const wchar_t* ch = state.otehon.GetCurrentCharacter();
        int size = (int)((std::min)(pw, ph) * 0.72);
        if (size > 0) {
            HFONT fOtehon = CreateFontW(size, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                DEFAULT_PITCH | FF_DONTCARE, L"Yu Mincho");
            int grayVal = (int)(248 - state.otehon.opacity * 105.0);
            grayVal = Clamp(grayVal, 80, 245);
            HFONT old = (HFONT)SelectObject(memDC, fOtehon);
            SetBkMode(memDC, TRANSPARENT);
            SetTextColor(memDC, RGB(grayVal, (int)(grayVal * 0.98), (int)(grayVal * 0.95)));
            DrawTextW(memDC, ch, 1, &rLocal, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            SelectObject(memDC, old);
            DeleteObject(fOtehon);
        }
    }

    // 3. 墨汁レンダリング
    gpuInk.Render(memDC, 0, 0, pw, ph);

    bool success = false;
    if (toClipboard) {
        if (OpenClipboard(hWnd)) {
            EmptyClipboard();
            SetClipboardData(CF_BITMAP, memBmp);
            CloseClipboard();
            state.SetSaveFeedback(L"✓ クリップボードにコピーしました");
            success = true;
        }
    } else {
        wchar_t deskPath[MAX_PATH];
        if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_DESKTOPDIRECTORY, NULL, 0, deskPath))) {
            SYSTEMTIME st;
            GetLocalTime(&st);
            wchar_t filePath[MAX_PATH];
            swprintf_s(filePath, MAX_PATH, L"%s\\習字作品_%04d%02d%02d_%02d%02d%02d.bmp",
                deskPath, st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);

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
            if (hFile != INVALID_HANDLE_VALUE) {
                DWORD written = 0;
                WriteFile(hFile, &bfh, sizeof(bfh), &written, NULL);
                WriteFile(hFile, &bih, sizeof(bih), &written, NULL);
                WriteFile(hFile, buf.data(), dwSize, &written, NULL);
                CloseHandle(hFile);
                state.SetSaveFeedback(L"✓ デスクトップに保存しました");
                success = true;
            }
        }
    }

    SelectObject(memDC, oldBmp);
    DeleteObject(memBmp);
    DeleteDC(memDC);
    ReleaseDC(hWnd, screenDC);

    return success;
}
