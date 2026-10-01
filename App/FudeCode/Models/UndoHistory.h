#pragma once

#include <vector>
#include <cstddef>
#include "InkSnapshot.h"
#include "InkModel.h"
#include "TrajectoryModel.h"

class GpuInk;

// 「一画戻す」で遡れる画数。「一画復元」も同じ数だけ持てる。
constexpr int UNDO_MAX_STROKES = 30;

// 履歴全体（戻す用＋復元用）の容量上限。Win32/x86（32 ビット）ビルドで
// ユーザー空間が 2GB しか無いため、画数だけでなくバイト数でも歯止めをかける。
// 超えた分は古い控えから捨てる。
constexpr size_t UNDO_MAX_BYTES = 384u * 1024u * 1024u;

// 1 画分の控え。墨（画素）と墨残量、運筆アーカイブの1画をひと組で持つ。
// 別々に戻すと、画面と墨残量・解析データがずれる。
struct UndoEntry {
    InkSnapshot ink;
    InkModel inkModel;
    StrokeData stroke;       // 復元用の控えだけが持つ、戻したときに外した1画
    bool hasStroke = false;
    size_t bytes = 0;        // 履歴容量の集計用（積むときに確定させる）
};

// 「一画戻す」「一画復元」の履歴。
//
// 墨は GpuInk の画素バッファへ破壊的に積み上がり、にじみが時間で進むため、
// 運筆データから描き直しても同じ絵にはならない。そこで画を書き始める直前の
// 状態を控えておき、戻すときはその状態を書き戻す。
//
// 戻す・復元はどちらも「今の状態を控えて、控えてあった状態を書き戻し、
// 控えを反対側の山へ移す」という同じ操作なので、戻したあとに復元し直す、
// といった往復も同じ仕組みで扱える。
class UndoHistory {
public:
    // 画の書き始めに、墨を置く前の状態を控える。
    // 新しい画を書いた時点で復元の枝は捨てる（戻す→書く→復元 は成立しない）
    bool PushBeforeStroke(GpuInk& gpuInk, const InkModel& ink, size_t strokeCount);

    // 直前の1画を取り消す。墨・墨残量・運筆アーカイブをまとめて戻す
    bool Undo(GpuInk& gpuInk, InkModel& ink, TrajectorySession& trajectory);

    // 戻しすぎた1画を復元する
    bool Redo(GpuInk& gpuInk, InkModel& ink, TrajectorySession& trajectory);

    void Clear();

    // 紙だけ表示の出入りで半紙が90度倒れたとき、控えも同じ向きへ回して残す。
    // 墨の控えは新しい半紙の寸法 canvasW x canvasH へ、復元用の1画は
    // TrajectorySession::RotateQuarter と同じように回す。
    // 回せなかった場合は、書き戻せない控えを残さないよう履歴を全部捨てて false を返す。
    bool RotateQuarter(bool counterClockwise, int canvasW, int canvasH, const RECT& oldPaper, const RECT& newPaper);

    bool CanUndo() const { return !m_undo.empty(); }
    bool CanRedo() const { return !m_redo.empty(); }
    int Depth() const { return static_cast<int>(m_undo.size()); }
    int RedoDepth() const { return static_cast<int>(m_redo.size()); }
    size_t TotalBytes() const { return m_bytes; }

private:
    // 今の状態を控えへ取る（戻す・復元の直前に使う）
    bool CaptureCurrent(GpuInk& gpuInk, const InkModel& ink, UndoEntry& out);
    void Trim();
    void DropOldest(std::vector<UndoEntry>& stack);
    void ClearRedo();

    std::vector<UndoEntry> m_undo;
    std::vector<UndoEntry> m_redo;
    size_t m_bytes = 0;
};
