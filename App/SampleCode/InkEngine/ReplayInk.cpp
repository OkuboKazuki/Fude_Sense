#include "stdafx.h"
#include "ReplayInk.h"

#include <cmath>

namespace {
constexpr double kPi = 3.14159265358979323846;
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
        if (!m_ink->Initialize(paperW, paperH)) {
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

    if (rewound || recordChanged) {
        m_ink->Clear();
        m_fedCount.assign(strokeCount, 0);
        m_openStroke = -1;
        m_timeMs = 0;
        m_revision = session.GetRevision();
    }

    FeedForward(session, timeMs);
    m_timeMs = timeMs;
    return true;
}

void ReplayInk::FeedForward(const TrajectorySession& session, DWORD timeMs) {
    const auto& strokes = session.GetStrokes();

    for (size_t si = 0; si < strokes.size(); ++si) {
        const auto& pts = strokes[si].points;
        if (pts.empty()) continue;

        DWORD start = session.GetStrokeTimelineStart(si);
        if (timeMs < start) break;  // これ以降の画はまだ始まっていない
        DWORD rel = timeMs - start;

        // 画内の相対時刻が現在時刻に届いている記録点までを流し込む。
        // 点と点の間を補間した先端は打たない。墨は重ねるほど濃くなるので、
        // 次のフレームで本来の記録点に打ち直されると濃さが狂うため。
        size_t visible = 0;
        while (visible < pts.size() && pts[visible].timeMs <= rel) ++visible;
        if (visible <= m_fedCount[si]) continue;

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

void ReplayInk::Render(HDC dc, int destX, int destY) {
    if (!m_ready || !m_ink) return;
    m_ink->Render(dc, destX, destY);
}
