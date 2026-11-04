#include "stdafx.h"
#include "ArchiveDialogs.h"
#include <chrono>
#include <ctime>
#include <cwchar>
#include <commdlg.h>

namespace {

// 保存ダイアログが閉じた理由。CommDlgExtendedError が 0 ならユーザーの取り消し
ArchiveDialogs::Result DialogClosedResult() {
    return (CommDlgExtendedError() == 0) ? ArchiveDialogs::Result::Canceled : ArchiveDialogs::Result::Failed;
}

std::wstring FileNameOf(const std::wstring& path) {
    size_t slash = path.find_last_of(L"\\/");
    return (slash == std::wstring::npos) ? path : path.substr(slash + 1);
}

} // namespace

ArchiveDialogs::Result ArchiveDialogs::PromptSaveJson(HWND hWnd, const TrajectorySession& session, PaperType paperType, Brush brushType, double hardness) {
    wchar_t szFileName[MAX_PATH] = L"";
    auto now = std::chrono::system_clock::now();
    std::time_t tt = std::chrono::system_clock::to_time_t(now);
    std::tm tm;
    localtime_s(&tm, &tt);
    swprintf_s(szFileName, MAX_PATH, L"FudeSense_Archive_%04d%02d%02d_%02d%02d%02d.json",
        tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);

    OPENFILENAMEW ofn = { 0 };
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hWnd;
    ofn.lpstrFilter = L"運筆アーカイブ JSON (*.json)\0*.json\0すべてのファイル (*.*)\0*.*\0";
    ofn.lpstrFile = szFileName;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrDefExt = L"json";
    ofn.Flags = OFN_EXPLORER | OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT;

    if (!GetSaveFileNameW(&ofn)) return DialogClosedResult();
    if (session.ExportToJson(szFileName, paperType, brushType, hardness)) return Result::Saved;
    // 途中までしか書けていないファイルは残さない
    DeleteFileW(szFileName);
    return Result::Failed;
}

ArchiveDialogs::Result ArchiveDialogs::PromptSaveCsv(HWND hWnd, const TrajectorySession& session) {
    wchar_t szFileName[MAX_PATH] = L"";
    auto now = std::chrono::system_clock::now();
    std::time_t tt = std::chrono::system_clock::to_time_t(now);
    std::tm tm;
    localtime_s(&tm, &tt);
    swprintf_s(szFileName, MAX_PATH, L"FudeSense_Kinematics_%04d%02d%02d_%02d%02d%02d.csv",
        tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);

    OPENFILENAMEW ofn = { 0 };
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hWnd;
    ofn.lpstrFilter = L"運筆時系列データ CSV (*.csv)\0*.csv\0すべてのファイル (*.*)\0*.*\0";
    ofn.lpstrFile = szFileName;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrDefExt = L"csv";
    ofn.Flags = OFN_EXPLORER | OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT;

    if (!GetSaveFileNameW(&ofn)) return DialogClosedResult();
    if (session.ExportToCsv(szFileName)) return Result::Saved;
    DeleteFileW(szFileName);
    return Result::Failed;
}

ArchiveDialogs::Result ArchiveDialogs::PromptLoad(HWND hWnd, TrajectorySession& session, const RECT& rPaper,
                                                  std::wstring& outFileName, std::wstring& outError) {
    wchar_t szFileName[MAX_PATH] = L"";

    OPENFILENAMEW ofn = { 0 };
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hWnd;
    ofn.lpstrFilter = L"運筆アーカイブ (*.json;*.csv)\0*.json;*.csv\0運筆アーカイブ JSON (*.json)\0*.json\0運筆時系列データ CSV (*.csv)\0*.csv\0すべてのファイル (*.*)\0*.*\0";
    ofn.lpstrFile = szFileName;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrTitle = L"解析する運筆アーカイブを開く";
    ofn.Flags = OFN_EXPLORER | OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;

    if (!GetOpenFileNameW(&ofn)) return DialogClosedResult();
    if (!session.ImportFromFile(szFileName, rPaper, outError)) return Result::Failed;
    outFileName = FileNameOf(szFileName);
    return Result::Saved;
}
