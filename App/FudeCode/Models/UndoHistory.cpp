#include "stdafx.h"
#include "UndoHistory.h"
#include "GpuInk.h"
#include <new>

bool UndoHistory::CaptureCurrent(GpuInk& gpuInk, const InkModel& ink, UndoEntry& out) {
    try {
        if (!gpuInk.CaptureSnapshot(out.ink)) return false;
    }
    catch (const std::bad_alloc&) {
        // 32 ビットの空きが尽きた。控えを全部手放して次の画に備える。
        // 履歴が無いだけで運筆自体は続けられる。
        Clear();
        return false;
    }
    out.inkModel = ink;
    out.bytes = out.ink.ByteSize();
    return true;
}

bool UndoHistory::PushBeforeStroke(GpuInk& gpuInk, const InkModel& ink, size_t strokeCount) {
    (void)strokeCount;

    UndoEntry entry;
    if (!CaptureCurrent(gpuInk, ink, entry)) return false;

    try {
        m_undo.push_back(std::move(entry));
        m_bytes += m_undo.back().bytes;
    }
    catch (const std::bad_alloc&) {
        Clear();
        return false;
    }

    // 新しい画を書いたので、戻した先へ復元する道は無くなる
    ClearRedo();
    Trim();
    return true;
}

bool UndoHistory::Undo(GpuInk& gpuInk, InkModel& ink, TrajectorySession& trajectory) {
    if (m_undo.empty()) return false;

    // 戻す前の状態を、復元用に控えておく
    UndoEntry redoEntry;
    if (!CaptureCurrent(gpuInk, ink, redoEntry)) return false;

    UndoEntry& top = m_undo.back();
    if (!gpuInk.RestoreSnapshot(top.ink)) {
        // 半紙の寸法が変わっているなど、もう書き戻せない控えなので捨てる
        Clear();
        return false;
    }
    ink = top.inkModel;

    m_bytes = (m_bytes >= top.bytes) ? (m_bytes - top.bytes) : 0;
    m_undo.pop_back();

    // ここから先は、失敗しても画面と墨残量はもう戻っている。
    // 復元用の控えを持てなかった場合は、復元だけを諦める。
    try {
        // 画素を戻した時点に合わせて、運筆アーカイブからも1画外す。
        // 外した1画は復元のときに積み直せるよう控えへ持たせる
        redoEntry.hasStroke = trajectory.UndoLastStroke(&redoEntry.stroke);
        m_redo.push_back(std::move(redoEntry));
        m_bytes += m_redo.back().bytes;
    }
    catch (const std::bad_alloc&) {
        ClearRedo();
    }

    Trim();
    return true;
}

bool UndoHistory::Redo(GpuInk& gpuInk, InkModel& ink, TrajectorySession& trajectory) {
    if (m_redo.empty()) return false;

    // 復元する前の状態を、また戻せるように控えておく
    UndoEntry undoEntry;
    if (!CaptureCurrent(gpuInk, ink, undoEntry)) return false;

    UndoEntry& top = m_redo.back();
    if (!gpuInk.RestoreSnapshot(top.ink)) {
        Clear();
        return false;
    }
    ink = top.inkModel;

    // 復元した画を運筆アーカイブへ積み直す。
    // 失敗しても画面は復元済みなので、履歴だけを諦める。
    try {
        if (top.hasStroke) {
            trajectory.RedoStroke(top.stroke);
        }
    }
    catch (const std::bad_alloc&) {
    }

    m_bytes = (m_bytes >= top.bytes) ? (m_bytes - top.bytes) : 0;
    m_redo.pop_back();

    try {
        m_undo.push_back(std::move(undoEntry));
        m_bytes += m_undo.back().bytes;
    }
    catch (const std::bad_alloc&) {
        // 戻す側の控えを持てなかっただけ。復元自体は成立している
    }

    Trim();
    return true;
}

bool UndoHistory::RotateQuarter(bool counterClockwise, int canvasW, int canvasH, const RECT& oldPaper, const RECT& newPaper) {
    try {
        size_t bytes = 0;
        for (std::deque<UndoEntry>* stack : { &m_undo, &m_redo }) {
            for (UndoEntry& e : *stack) {
                if (!GpuInk::RotateSnapshot(e.ink, counterClockwise, canvasW, canvasH)) {
                    Clear();
                    return false;
                }
                if (e.hasStroke) {
                    TrajectorySession::RotateStrokeQuarter(e.stroke, counterClockwise, oldPaper, newPaper);
                }
                // 回すと RLE の効き方が変わるので、容量を数え直す
                e.bytes = e.ink.ByteSize();
                bytes += e.bytes;
            }
        }
        m_bytes = bytes;
    }
    catch (const std::bad_alloc&) {
        Clear();
        return false;
    }
    Trim();
    return true;
}

void UndoHistory::Clear() {
    std::deque<UndoEntry>().swap(m_undo);
    std::deque<UndoEntry>().swap(m_redo);
    m_bytes = 0;
}

void UndoHistory::ClearRedo() {
    for (const UndoEntry& e : m_redo) {
        m_bytes = (m_bytes >= e.bytes) ? (m_bytes - e.bytes) : 0;
    }
    std::deque<UndoEntry>().swap(m_redo);
}

void UndoHistory::Trim() {
    while (static_cast<int>(m_undo.size()) > UNDO_MAX_STROKES) DropOldest(m_undo);
    while (static_cast<int>(m_redo.size()) > UNDO_MAX_STROKES) DropOldest(m_redo);

    // 容量が上限を超えたら、まず古い「戻す用」から、次に古い「復元用」から捨てる。
    // 直前の1画を戻せる・復元できる状態（それぞれの末尾）は最後まで残す。
    while (m_bytes > UNDO_MAX_BYTES && m_undo.size() > 1) DropOldest(m_undo);
    while (m_bytes > UNDO_MAX_BYTES && m_redo.size() > 1) DropOldest(m_redo);
}

void UndoHistory::DropOldest(std::deque<UndoEntry>& stack) {
    if (stack.empty()) return;
    const size_t bytes = stack.front().bytes;
    m_bytes = (m_bytes >= bytes) ? (m_bytes - bytes) : 0;
    stack.pop_front();
}
