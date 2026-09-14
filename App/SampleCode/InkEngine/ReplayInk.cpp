#include "stdafx.h"
#include "ReplayInk.h"

#include <algorithm>
#include <cmath>

namespace {
constexpr double kPi = 3.14159265358979323846;

// にじみ1段階分の時間。書いているときの拡散スレッドと同じ刻み。
constexpr DWORD kDiffusionStepMs = 16;
// 1フレームで進める段数の上限。通常再生時のフレーム落ち連鎖を防ぎ滑らかさを維持。
constexpr int kMaxDiffusionStepsPerFrame = 2;
// 引き直した直後にまとめて進める段数。シーク先でも自然なにじみを即座に乗せる。
constexpr int kRebuildDiffusionSteps = 8;
// 画と画の間（空中移動時）でまとめて進める段数の上限。画の切り替わりスパイクを解消。
constexpr int kMaxGapDiffusionSteps = 10;
// 巻き戻し用に控える墨の状態の最大数と、その合計サイズの上限。
constexpr size_t kMaxCheckpoints = 12;
constexpr size_t kCheckpointByteBudget = 64u * 1024u * 1024u;
// 控えを取る間隔の下限。
constexpr DWORD kMinCheckpointIntervalMs = 400;
}

ReplayInk::~ReplayInk() {
    Release();
}

void ReplayInk::ClearCheckpoints() {
    m_checkpoints.clear();
    m_checkpointBytes = 0;
}

void ReplayInk::Release() {
    m_ink.reset();
    m_fedCount.clear();
    ClearCheckpoints();
    m_revision = 0;
    m_paperW = 0;
    m_paperH = 0;
    m_timeMs = 0;
    m_ready = false;
    m_openStroke = -1;
    m_checkpointIntervalMs = 0;
    m_wasScrubbing = false;
    m_settled = false;
}

// 巻き戻し先の手前で控えてある状態を書き戻す。
// 見つかれば、そこから目的の時刻までを引き直すだけで済む。
bool ReplayInk::RestoreNearest(DWORD timeMs) {
    for (size_t i = m_checkpoints.size(); i > 0; --i) {
        const Checkpoint& cp = m_checkpoints[i - 1];
        if (cp.timeMs > timeMs) continue;
        if (!m_ink->RestoreSnapshot(cp.snap)) return false;
        m_fedCount = cp.fedCount;
        m_openStroke = -1;
        m_timeMs = cp.timeMs;
        return true;
    }
    return false;
}

void ReplayInk::CaptureCheckpoint(DWORD timeMs) {
    if (m_checkpoints.size() >= kMaxCheckpoints) return;
    if (m_checkpointBytes >= kCheckpointByteBudget) return;

    Checkpoint cp;
    cp.timeMs = timeMs;
    cp.fedCount = m_fedCount;
    if (!m_ink->CaptureSnapshot(cp.snap)) return;

    m_checkpointBytes += cp.snap.ByteSize();
    m_checkpoints.push_back(std::move(cp));
}

bool ReplayInk::Update(const TrajectorySession& session, DWORD timeMs, int paperW, int paperH, bool scrubbing) {
    if (paperW <= 0 || paperH <= 0) return false;

    if (!m_ink) {
        m_ink.reset(new GpuInk());
        m_ready = false;
    }

    // 初回、または半紙の大きさが変わったとき。
    if (!m_ready || m_paperW != paperW || m_paperH != paperH) {
        if (!m_ink->Initialize(paperW, paperH, false)) {
            m_ready = false;
            return false;
        }
        m_paperW = paperW;
        m_paperH = paperH;
        m_ready = true;
        m_revision = 0;
        m_fedCount.clear();
        ClearCheckpoints();
    }

    const size_t strokeCount = session.GetStrokes().size();
    bool rewound = (timeMs < m_timeMs);
    bool recordChanged = (m_revision != session.GetRevision()) || (m_fedCount.size() != strokeCount);

    // 記録そのものが変わったら控えは使えない
    if (recordChanged) {
        ClearCheckpoints();
        m_revision = session.GetRevision();
        m_checkpointIntervalMs = (std::max)(kMinCheckpointIntervalMs,
            session.GetReplayTotalDurationMs() / static_cast<DWORD>(kMaxCheckpoints));
    }

    // 墨は画素へ破壊的に積み上がるので、巻き戻すには引き直す。
    // 手前の控えまで書き戻して差分だけを引く。
    bool rebuilt = (rewound || recordChanged);
    bool restored = false;
    if (rebuilt) {
        restored = !recordChanged && RestoreNearest(timeMs);
        if (!restored) {
            m_ink->Clear();
            m_fedCount.assign(strokeCount, 0);
            m_openStroke = -1;
            m_timeMs = 0;
        }
    }

    // にじみは時間発展なので、進める量を再生時刻の進みに合わせる。
    int steps = 0;
    if (rebuilt) {
        steps = scrubbing ? 0 : kRebuildDiffusionSteps;
    } else if (m_wasScrubbing && !scrubbing) {
        steps = kRebuildDiffusionSteps;
    } else if (timeMs > m_timeMs) {
        steps = static_cast<int>((timeMs - m_timeMs) / kDiffusionStepMs);
        if (steps > kMaxDiffusionStepsPerFrame) steps = kMaxDiffusionStepsPerFrame;
    }

    // ドラッグ中(scrubbing)以外は、再構築時であっても画ごとの空中拡散を適用する
    bool skipGap = scrubbing;
    FeedForward(session, timeMs, skipGap, !scrubbing);
    AdvanceDiffusion(steps);

    // 全画の再生が終端に達した場合、残った水分を自然乾燥・拡散させる（1度だけ実行）
    DWORD totalDur = session.GetReplayTotalDurationMs();
    if (!scrubbing && totalDur > 0 && timeMs >= totalDur) {
        if (!m_settled) {
            m_ink->SettleDiffusion(2);
            m_settled = true;
        }
    } else {
        m_settled = false;
    }

    m_timeMs = timeMs;
    m_wasScrubbing = scrubbing;
    return true;
}

int ReplayInk::GapDiffusionSteps(const TrajectorySession& session, size_t strokeIdx) const {
    const auto& strokes = session.GetStrokes();
    if (strokeIdx == 0 || strokeIdx >= strokes.size()) return 0;

    const StrokeData& prev = strokes[strokeIdx - 1];
    const StrokeData& cur = strokes[strokeIdx];
    if (prev.points.empty() || cur.startTime <= prev.startTime) return 0;

    DWORD prevDuration = prev.points.back().timeMs;
    DWORD elapsed = cur.startTime - prev.startTime;
    if (elapsed <= prevDuration) return 0;
    DWORD realGap = elapsed - prevDuration;

    // 再生側はこの間を既に通過しているので、その分は差し引く
    DWORD startCur = session.GetStrokeTimelineStart(strokeIdx);
    DWORD endPrev = session.GetStrokeTimelineEnd(strokeIdx - 1);
    DWORD playedGap = (startCur > endPrev) ? (startCur - endPrev) : 0;
    if (realGap <= playedGap) return 0;

    int steps = static_cast<int>((realGap - playedGap) / kDiffusionStepMs);
    if (steps > kMaxGapDiffusionSteps) steps = kMaxGapDiffusionSteps;
    return steps;
}

void ReplayInk::FeedForward(const TrajectorySession& session, DWORD timeMs, bool skipGapDiffusion, bool allowCapture) {
    const auto& strokes = session.GetStrokes();

    for (size_t si = 0; si < strokes.size(); ++si) {
        const auto& pts = strokes[si].points;
        if (pts.empty()) continue;

        // これ以降の画はまだ始まっていない
        if (timeMs < session.GetStrokeTimelineStart(si)) break;

        size_t visible = session.GetVisiblePointCount(si, timeMs);
        if (visible <= m_fedCount[si]) continue;

        if (m_fedCount[si] == 0) {
            // この画の1点目を置く前に、手前の空中移動のにじみを補う
            if (!skipGapDiffusion) {
                AdvanceDiffusion(GapDiffusionSteps(session, si));
            }

            // 画の切れ目は、巻き戻しの足がかりとして墨の状態を控えておく。
            // 一定の間隔を空けて、記録全体をまばらに覆う。
            DWORD startMs = session.GetStrokeTimelineStart(si);
            if (allowCapture && si > 0 && m_checkpointIntervalMs > 0
                && (m_checkpoints.empty() || startMs >= m_checkpoints.back().timeMs + m_checkpointIntervalMs)) {
                if (m_openStroke >= 0) m_ink->EndStroke();
                m_openStroke = -1;
                CaptureCheckpoint(startMs);
            }
        }

        if (m_openStroke != static_cast<int>(si)) {
            if (m_openStroke >= 0) m_ink->EndStroke();
            m_openStroke = static_cast<int>(si);
        }

        for (size_t i = m_fedCount[si]; i < visible; ++i) {
            const StrokePoint& p = pts[i];
            const StrokePoint& prev = (i > 0) ? pts[i - 1] : p;

            // 描画時と同じ順序でペンの姿勢と筆圧を渡す。StampBrush は
            // これらから毛束の接地形状（しなり・広がり）を決める。
            m_ink->UpdatePen(0, p.altitudeDeg, p.azimuthDeg * (kPi / 180.0), false);
            m_ink->SetPressureFactor(p.pressureFactor);

            StrokeSegment seg;
            seg.a = { static_cast<LONG>(prev.normX * m_paperW), static_cast<LONG>(prev.normY * m_paperH) };
            seg.b = { static_cast<LONG>(p.normX * m_paperW),    static_cast<LONG>(p.normY * m_paperH) };
            seg.startWidth = prev.width;
            seg.endWidth = p.width;
            seg.dirX = std::cos(p.moveAngleRad);
            seg.dirY = std::sin(p.moveAngleRad);
            seg.dryness = p.dryness;
            seg.inkAlpha = 255;
            m_ink->DrawSegmentLinear(seg);
        }
        m_fedCount[si] = visible;
    }

    // 最後まで流し込んだ画は区切る。
    if (m_openStroke >= 0 && static_cast<size_t>(m_openStroke) < strokes.size()
        && m_fedCount[m_openStroke] >= strokes[m_openStroke].points.size()) {
        m_ink->EndStroke();
        m_openStroke = -1;
    }
}

void ReplayInk::AdvanceDiffusion(int steps) {
    if (!m_ready || !m_ink) return;
    for (int i = 0; i < steps; ++i) {
        if (!m_ink->StepPropagation()) break;  // 変化が止まったら打ち切り
    }
}

void ReplayInk::Render(HDC dc, int destX, int destY) {
    if (!m_ready || !m_ink) return;
    m_ink->Render(dc, destX, destY);
}
