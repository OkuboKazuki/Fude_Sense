#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <string>
#include "AppEnums.h"
#include "TrajectoryModel.h"

// 運筆アーカイブのファイル選択ダイアログを開き、選ばれたファイルを読み書きする
class ArchiveDialogs {
public:
    // 取り消しと失敗を分けて返す
    // （取り消しでは何も表示せず、失敗したときだけ知らせるため）。
    enum class Result { Saved, Canceled, Failed };

    static Result PromptSaveJson(HWND hWnd, const TrajectorySession& session, PaperType paperType, Brush brushType, double hardness);
    static Result PromptSaveCsv(HWND hWnd, const TrajectorySession& session);

    // ファイル選択ダイアログから読み込む。outFileName には拡張子付きのファイル名を返す
    static Result PromptLoad(HWND hWnd, TrajectorySession& session, const RECT& rPaper,
                             std::wstring& outFileName, std::wstring& outError);
};
