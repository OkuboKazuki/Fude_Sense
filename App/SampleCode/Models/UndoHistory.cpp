#include "stdafx.h"
#include "UndoHistory.h"
#include "GpuInk.h"
#include <new>

bool UndoHistory::PushBeforeStroke(GpuInk& gpuInk, const InkModel& ink, size_t strokeCount) {
    UndoEntry entry;
    entry.inkModel = ink;
    entry.strokeCount = strokeCount;

    try {
        if (!gpuInk.CaptureSnapshot(entry.ink)) return false;
        entry.bytes = entry.ink.ByteSize();
        m_entries.push_back(std::move(entry));
    }
    catch (const std::bad_alloc&) {
        // 32 ビットの空きが尽きた。控えを全部手放して、次の画に備える。
        // 履歴が無いだけで運筆自体は続けられる。
        Clear();
        return false;
    }
    m_bytes += m_entries.back().bytes;

    while (static_cast<int>(m_entries.size()) > UNDO_MAX_STROKES
        || (m_bytes > UNDO_MAX_BYTES && m_entries.size() > 1)) {
        DropOldest();
    }
    return true;
}

bool UndoHistory::Undo(GpuInk& gpuInk, InkModel& ink, size_t& outStrokeCount) {
    if (m_entries.empty()) return false;

    UndoEntry& top = m_entries.back();
    if (!gpuInk.RestoreSnapshot(top.ink)) {
        // 半紙の寸法が変わっているなど、もう書き戻せない控えなので捨てる
        Clear();
        return false;
    }

    ink = top.inkModel;
    outStrokeCount = top.strokeCount;

    m_bytes = (m_bytes >= top.bytes) ? (m_bytes - top.bytes) : 0;
    m_entries.pop_back();
    return true;
}

void UndoHistory::Clear() {
    std::vector<UndoEntry>().swap(m_entries);
    m_bytes = 0;
}

void UndoHistory::DropOldest() {
    if (m_entries.empty()) return;
    const size_t bytes = m_entries.front().bytes;
    m_bytes = (m_bytes >= bytes) ? (m_bytes - bytes) : 0;
    m_entries.erase(m_entries.begin());
}
