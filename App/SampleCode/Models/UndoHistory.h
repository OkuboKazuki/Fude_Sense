#pragma once

#include <vector>
#include <cstddef>
#include "InkSnapshot.h"
#include "InkModel.h"

class GpuInk;

// 「一画戻す」で遡れる画数。
constexpr int UNDO_MAX_STROKES = 30;

// 履歴全体の容量上限。Win32/x86（32 ビット）ビルドでユーザー空間が 2GB しか
// 無いため、画数だけでなくバイト数でも歯止めをかける。超えた分は古い画から捨てる。
constexpr size_t UNDO_MAX_BYTES = 384u * 1024u * 1024u;

// 1 画分の控え。墨（画素）と墨残量、運筆アーカイブの画数をひと組で持つ。
// 別々に戻すと、画面と墨残量・解析データがずれる。
struct UndoEntry {
    InkSnapshot ink;
    InkModel inkModel;
    size_t strokeCount = 0;  // 控えた時点の運筆アーカイブの画数
    size_t bytes = 0;        // 履歴容量の集計用（push 時に確定させる）
};

// 「一画戻す」の履歴。
//
// 墨は GpuInk の画素バッファへ破壊的に積み上がり、にじみが時間で進むため、
// 運筆データから描き直しても同じ絵にはならない。そこで画を書き始める直前の
// 状態を控えておき、戻すときはその状態を書き戻す。
class UndoHistory {
public:
    // 画の書き始めに、墨を置く前の状態を控える
    bool PushBeforeStroke(GpuInk& gpuInk, const InkModel& ink, size_t strokeCount);

    // 直前の1画を取り消す。墨（画素）と墨残量を書き戻し、
    // 戻した時点の運筆アーカイブの画数を outStrokeCount へ返す
    bool Undo(GpuInk& gpuInk, InkModel& ink, size_t& outStrokeCount);

    void Clear();

    bool CanUndo() const { return !m_entries.empty(); }
    int Depth() const { return static_cast<int>(m_entries.size()); }
    size_t TotalBytes() const { return m_bytes; }

private:
    void DropOldest();

    std::vector<UndoEntry> m_entries;
    size_t m_bytes = 0;
};
