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

    // 2. 墨汁レンダリング
    // お手本はなぞるための下敷きであって作品の一部ではないため、書き出しには
    // 含めない（画面表示のみ）。下敷きの罫線を書き出さないのと同じ扱い。
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
