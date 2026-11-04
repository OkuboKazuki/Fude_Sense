#pragma once

#include <deque>
#include <vector>
#include <cstddef>
#include <functional>
#include "InkSnapshot.h"
#include "InkModel.h"
#include "TrajectoryModel.h"

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
//
// 墨エンジンには触れない。墨の控えを取るのと書き戻すのは呼び出し側で行い、
// ここでは InkSnapshot の保管と、墨残量・運筆アーカイブをまとめた更新だけを行う。
class UndoHistory {
public:
    // 画の書き始めに、墨を置く前の状態を控える。
    // 新しい画を書いた時点で復元の枝は捨てる（戻す→書く→復元 は成立しない）
    bool PushBeforeStroke(InkSnapshot&& inkSnapshot, const InkModel& ink, size_t strokeCount);

    // 直前の1画の取り消しで書き戻す墨の控え。CanUndo() のときだけ呼ぶ
    const InkSnapshot& UndoTargetInk() const { return m_undo.back().ink; }

    // 墨を書き戻したあとに呼び、墨残量・運筆アーカイブを戻して履歴を進める。
    // currentInk は書き戻す前の墨で、復元用に控える。
    void CommitUndo(InkSnapshot&& currentInk, InkModel& ink, TrajectorySession& trajectory);

    // 戻しすぎた1画の復元で書き戻す墨の控え。CanRedo() のときだけ呼ぶ
    const InkSnapshot& RedoTargetInk() const { return m_redo.back().ink; }

    // 墨を書き戻したあとに呼び、墨残量・運筆アーカイブを進めて履歴を更新する。
    // currentInk は書き戻す前の墨で、また戻せるように控える。
    void CommitRedo(InkSnapshot&& currentInk, InkModel& ink, TrajectorySession& trajectory);

    void Clear();

    // 紙だけ表示の出入りで半紙が90度倒れたとき、控えも同じ向きへ残す。
    // 墨の控えは rotateInk で新しい半紙の向きと寸法へ、復元用の1画は
    // TrajectorySession::RotateQuarter と同じように回す。
    // 回せなかった場合は、書き戻せない控えを残さないよう履歴を全部捨てて false を返す。
    bool RotateQuarter(const std::function<bool(InkSnapshot&)>& rotateInk, bool counterClockwise, const RECT& oldPaper, const RECT& newPaper);

    bool CanUndo() const { return !m_undo.empty(); }
    bool CanRedo() const { return !m_redo.empty(); }
    int Depth() const { return static_cast<int>(m_undo.size()); }
    int RedoDepth() const { return static_cast<int>(m_redo.size()); }
    size_t TotalBytes() const { return m_bytes; }

private:
    // 墨の控えと今の墨残量を、ひと組の控えにまとめる
    static UndoEntry MakeEntry(InkSnapshot&& inkSnapshot, const InkModel& ink);
    void Trim();
    void DropOldest(std::deque<UndoEntry>& stack);
    void ClearRedo();

    std::deque<UndoEntry> m_undo;
    std::deque<UndoEntry> m_redo;
    size_t m_bytes = 0;
};
