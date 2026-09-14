#pragma once

#include <windows.h>
#include <string>

// 筆の種類
enum class Brush {
    Small,   // 小筆
    Medium,  // 中筆
    Large    // 大筆
};

inline const wchar_t* BrushName(Brush b) {
    switch (b) {
    case Brush::Small:  return L"小筆";
    case Brush::Medium: return L"中筆";
    case Brush::Large:  return L"大筆";
    }
    return L"中筆";
}

// 用紙の種類（実寸の縦横比に対応）
enum class PaperType {
    Hanshi     // 半紙   242x333  W:H=1:1.376
};

// 下敷き・升目格子パターン
enum class GridPattern {
    None,           // なし
    Cross1,         // 1字 (十字補助)
    Div2,           // 2文字 (上下2段)
    Grid4,          // 4文字 (2x2 田の字)
    Grid6,          // 6文字 (2x3)
    Grid8           // 8文字 (2x4)
};
constexpr int GRID_PATTERN_COUNT = 6;

// お手本の書体。練習する書きぶりに合わせて切り替える
enum class OtehonFontStyle {
    Seikaisho,  // 毛筆楷書（HG正楷書体-PRO）
    Kyokasho,   // 教科書体（HG教科書体）
    Gyosho      // 行書（HG行書体）
};
constexpr int OTEHON_FONT_COUNT = 3;

// 下敷き・罫線の配色テーマ
enum class GridColorTheme {
    RedLine,        // 定番朱赤線
    WhiteLine,      // 高級毛氈白線
    InkGray         // 薄墨点線
};

// ツールバー・ナビゲーションボタン
enum class TbButton {
    None,
    NavToggle,
    Brush,
    Paper,
    Analysis,
    Save,
    Otehon,
    InkRefill,
    ClearAll
};

// 左側フローティングパネルのタブ
enum class LeftTab {
    Brush,
    Paper,
    Analysis,
    Save,
    Otehon
};

constexpr double INK_MAX_VALUE = 1.0;

// 升目セル配列の上限（最大は Grid8 の 2x4）
constexpr int MAX_GRID_CELLS = 8;

// リプレイ再生用タイマの ID。再生中だけ回すため、
// 開始側（コントローラ）と停止側（WM_TIMER）の両方から触る。
constexpr UINT_PTR REPLAY_TIMER_ID = 101;
