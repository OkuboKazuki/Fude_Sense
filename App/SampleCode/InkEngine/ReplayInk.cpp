#include "stdafx.h"
#include "ReplayInk.h"

#include <cmath>

namespace {
constexpr double kPi = 3.14159265358979323846;

// にじみ1段階分の時間。書いているときの拡散スレッドと同じ刻み。
constexpr DWORD kDiffusionStepMs = 16;
// 1フレームで進める段数の上限。拡散は墨の広がった範囲全体を
// 走るので、高速再生で段数が伸びると描画が止まる。
constexpr int kMaxDiffusionStepsPerFrame = 4;
// 引き直した直後にまとめて進める段数。
// シークで飛んだ先でにじみが全く乗っていないのを避ける。
constexpr int kRebuildDiffusionSteps = 12;
// 画と画の間でまとめて進める段数の上限。長く考え込んだ場合でも、そこで
// 再生が止まらないようにする。拡散は落ち着くと StepPropagation が false を
// 返すので、通常はその手前で打ち切れる。
constexpr int kMaxGapDiffusionSteps = 60;
}

ReplayInk::~ReplayInk() {
    Release();
}

void ReplayInk::Release() {
    m_ink.reset();
    m_fedCount.clear();
    m_revision = 0;
    m_paperW = 0;
    m_paperH = 0;
    m_timeMs = 0;
    m_ready = false;
    m_openStroke = -1;
}

bool ReplayInk::Update(const TrajectorySession& session, DWORD timeMs, int paperW, int paperH) {
    if (paperW <= 0 || paperH <= 0) return false;

    if (!m_ink) {
        m_ink.reset(new GpuInk());
        m_ready = false;
    }

    // 初回、または半紙の大きさが変わったとき。
    // 記録は正規化座標で持っているので座標は追従できるが、線幅は記録時の
    // ピクセル値なので、大きさを変えると太さの比率はずれる。
    if (!m_ready || m_paperW != paperW || m_paperH != paperH) {
        // 拡散スレッドは立てない。立てると拡散のたびにウィンドウ全体の
        // 再描画を 60fps で要求し続け、解析タブを離れた後も重さが残る。
        if (!m_ink->Initialize(paperW, paperH, false)) {
            m_ready = false;
            return false;
        }
        m_paperW = paperW;
        m_paperH = paperH;
        m_ready = true;
        m_revision = 0;
        m_fedCount.clear();
    }

    const size_t strokeCount = session.GetStrokes().size();
    bool rewound = (timeMs < m_timeMs);
    bool recordChanged = (m_revision != session.GetRevision()) || (m_fedCount.size() != strokeCount);

    bool rebuilt = (rewound || recordChanged);
    if (rebuilt) {
        m_ink->Clear();
        m_fedCount.assign(strokeCount, 0);
        m_openStroke = -1;
        m_timeMs = 0;
        m_revision = session.GetRevision();
    }

    // にじみは時間発展なので、進める量を再生時刻の進みに合わせる。
    // 停止中は進まず、2倍速なら2倍速で進む。
    int steps = 0;
    if (rebuilt) {
        steps = kRebuildDiffusionSteps;
    } else if (timeMs > m_timeMs) {
        steps = static_cast<int>((timeMs - m_timeMs) / kDiffusionStepMs);
        if (steps > kMaxDiffusionStepsPerFrame) steps = kMaxDiffusionStepsPerFrame;
    }

    // 引き直しの最中は全画を一気に流し込むため、画ごとの間の拡散を積むと
    // 1フレームで数百段走ることになる。そこでは補わない。
    FeedForward(session, timeMs, rebuilt);
    AdvanceDiffusion(steps);
    m_timeMs = timeMs;
    return true;
}

// 画と画の間（空中移動）は、再生では 250〜700ms に丸められている
// （BuildReplayTimeline）。長く間を置いて書いていた場合、その間に紙の上で
// 進んだにじみが再生では足りない。テンポは丸めたままにして、にじみだけ
// 実際の間隔ぶん先へ進める。
//
// 実際の間隔は、両画の startTime の差から前の画の所要時間を引いて求める。
// endTime は GetTickCount 由来、startTime は Wintab のパケット時刻由来で
// 時計が違う可能性があるため、引き算は startTime 同士で閉じる。
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

void ReplayInk::FeedForward(const TrajectorySession& session, DWORD timeMs, bool skipGapDiffusion) {
    const auto& strokes = session.GetStrokes();

    for (size_t si = 0; si < strokes.size(); ++si) {
        const auto& pts = strokes[si].points;
        if (pts.empty()) continue;

        // これ以降の画はまだ始まっていない
        if (timeMs < session.GetStrokeTimelineStart(si)) break;

        // 点と点の間を補間した先端は打たない。墨は重ねるほど濃くなるので、
        // 次のフレームで本来の記録点に打ち直されると濃さが狂うため。
        size_t visible = session.GetVisiblePointCount(si, timeMs);
        if (visible <= m_fedCount[si]) continue;

        // この画の1点目を置く前に、手前の空中移動のにじみを補う
        if (!skipGapDiffusion && m_fedCount[si] == 0) {
            AdvanceDiffusion(GapDiffusionSteps(session, si));
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

    // 最後まで流し込んだ画は区切る。次の画が前の画の続きとして扱われると、
    // 払いや跳ねの判定（EndStroke の速度・加速度）が狂う。
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
