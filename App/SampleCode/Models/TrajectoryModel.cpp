#include "stdafx.h"
#include "TrajectoryModel.h"
#include "RenderUtils.h"
#include <chrono>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <ctime>
#include <commdlg.h>
#include <algorithm>

TrajectorySession::TrajectorySession() {
    m_sessionStartTime = GetTickCount();
}

void TrajectorySession::Clear() {
    m_strokes.clear();
    ++m_revision;
    m_currentStroke = StrokeData();
    m_isRecordingStroke = false;
    m_realtime = RealtimeMetrics();
    m_sessionStartTime = GetTickCount();
    m_strokeTimelines.clear();
    m_totalReplayDurationMs = 0;
}

bool TrajectorySession::UndoLastStroke(StrokeData* outRemoved) {
    if (m_isRecordingStroke) {
        // まだ書いている途中の画は、記録に積む前に破棄する。
        // 画素側も同じ時点（この画を書き始める前）へ戻る。
        // 記録に載っていない画なので、復元のときに積み直すものは無い。
        m_currentStroke = StrokeData();
        m_isRecordingStroke = false;
        m_realtime.isPenDown = false;
        m_realtime.peakPressureInStroke = 0.0;
        return false;
    }
    if (m_strokes.empty()) return false;
    if (outRemoved) *outRemoved = m_strokes.back();
    m_strokes.pop_back();
    ++m_revision;
    return true;
}

void TrajectorySession::RedoStroke(const StrokeData& stroke) {
    m_strokes.push_back(stroke);
    ++m_revision;
}

void TrajectorySession::OnStrokeBegin(DWORD time) {
    m_isRecordingStroke = true;
    m_currentStroke = StrokeData();
    m_currentStroke.strokeId = static_cast<int>(m_strokes.size()) + 1;
    m_currentStroke.startTime = (time != 0) ? time : GetTickCount();
    m_currentStroke.maxPressure = 0.0;
    m_currentStroke.maxSpeed = 0.0;
    m_realtime.isPenDown = true;
    m_realtime.peakPressureInStroke = 0.0;
}

void TrajectorySession::AddPoint(const PenInputEvent& event, const RECT& rPaper, double width, double speed,
                                double pressureFactor, double dryness, double moveAngleRad) {
    ++m_revision;
    if (!m_isRecordingStroke) {
        OnStrokeBegin(event.time);
    }

    int paperW = RenderUtils::RW(rPaper);
    int paperH = RenderUtils::RH(rPaper);
    if (paperW <= 0) paperW = 1;
    if (paperH <= 0) paperH = 1;

    int px = event.x - rPaper.left;
    int py = event.y - rPaper.top;

    double normX = static_cast<double>(px) / static_cast<double>(paperW);
    double normY = static_cast<double>(py) / static_cast<double>(paperH);
    normX = RenderUtils::Clamp(normX, 0.0, 1.0);
    normY = RenderUtils::Clamp(normY, 0.0, 1.0);

    DWORD curTime = (event.time != 0) ? event.time : GetTickCount();
    DWORD relTime = (curTime >= m_currentStroke.startTime) ? (curTime - m_currentStroke.startTime) : static_cast<DWORD>(m_currentStroke.points.size() * 8);

    double azimuthDeg = event.azimuthRad * (180.0 / 3.14159265358979323846);
    while (azimuthDeg < 0.0) azimuthDeg += 360.0;
    while (azimuthDeg >= 360.0) azimuthDeg -= 360.0;

    StrokePoint pt;
    pt.timeMs = relTime;
    pt.normX = normX;
    pt.normY = normY;
    pt.paperX = px;
    pt.paperY = py;
    pt.pressure = event.pressure;
    pt.altitudeDeg = event.altitudeDegrees;
    pt.azimuthDeg = azimuthDeg;
    pt.speedPxPerSec = speed;
    pt.width = width;
    pt.pressureFactor = pressureFactor;
    pt.dryness = dryness;
    pt.moveAngleRad = moveAngleRad;

    m_currentStroke.points.push_back(pt);
    if (pt.pressure > m_currentStroke.maxPressure) m_currentStroke.maxPressure = pt.pressure;
    if (pt.speedPxPerSec > m_currentStroke.maxSpeed) m_currentStroke.maxSpeed = pt.speedPxPerSec;

    // リアルタイムメトリクスの更新
    m_realtime.currentPressure = pt.pressure;
    m_realtime.currentAltitude = pt.altitudeDeg;
    m_realtime.currentAzimuth = pt.azimuthDeg;
    m_realtime.currentSpeed = pt.speedPxPerSec;
    m_realtime.currentWidth = pt.width;
    m_realtime.isPenDown = true;
    if (pt.pressure > m_realtime.peakPressureInStroke) {
        m_realtime.peakPressureInStroke = pt.pressure;
    }
}

void TrajectorySession::OnStrokeEnd() {
    if (m_isRecordingStroke) {
        ++m_revision;
        m_isRecordingStroke = false;
        m_realtime.isPenDown = false;
        m_currentStroke.endTime = GetTickCount();

        if (!m_currentStroke.points.empty()) {
            double totalSpeed = 0.0;
            for (const auto& pt : m_currentStroke.points) {
                totalSpeed += pt.speedPxPerSec;
            }
            m_currentStroke.avgSpeed = totalSpeed / static_cast<double>(m_currentStroke.points.size());
            m_strokes.push_back(m_currentStroke);
        }
    }
}

const StrokeData* TrajectorySession::GetCurrentOrLastStroke() const {
    if (m_isRecordingStroke && !m_currentStroke.points.empty()) {
        return &m_currentStroke;
    }
    if (!m_strokes.empty()) {
        return &m_strokes.back();
    }
    return nullptr;
}

size_t TrajectorySession::GetTotalPointCount() const {
    size_t count = 0;
    for (const auto& s : m_strokes) {
        count += s.points.size();
    }
    if (m_isRecordingStroke) {
        count += m_currentStroke.points.size();
    }
    return count;
}

static std::string GetCurrentISOTimestamp() {
    auto now = std::chrono::system_clock::now();
    std::time_t tt = std::chrono::system_clock::to_time_t(now);
    std::tm tm;
    localtime_s(&tm, &tt);
    char buf[64];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", &tm);
    return std::string(buf);
}

bool TrajectorySession::ExportToJson(const std::wstring& filePath, PaperType paperType, Brush brushType, double hardness) const {
    std::ofstream ofs(filePath);
    if (!ofs.is_open()) return false;

    const char* paperNameStr = "Hanshi";
    switch (paperType) {
    case PaperType::Hanshi:   paperNameStr = "Hanshi (242x333)"; break;
    case PaperType::Jofuku:   paperNameStr = "Jofuku (350x680)"; break;
    case PaperType::Shikishi: paperNameStr = "Shikishi (242x272)"; break;
    case PaperType::Tanzaku:  paperNameStr = "Tanzaku (60x180)"; break;
    }

    const char* brushNameStr = "Medium";
    switch (brushType) {
    case Brush::Small:  brushNameStr = "Small"; break;
    case Brush::Medium: brushNameStr = "Medium"; break;
    case Brush::Large:  brushNameStr = "Large"; break;
    }

    ofs << std::fixed << std::setprecision(4);
    ofs << "{\n";
    ofs << "  \"format\": \"FudesenceStrokeArchive\",\n";
    ofs << "  \"version\": \"1.1\",\n";
    ofs << "  \"metadata\": {\n";
    ofs << "    \"application\": \"SHUJI STUDIO (Fudesence)\",\n";
    ofs << "    \"recordedAt\": \"" << GetCurrentISOTimestamp() << "\",\n";
    ofs << "    \"paperType\": \"" << paperNameStr << "\",\n";
    ofs << "    \"brushType\": \"" << brushNameStr << "\",\n";
    ofs << "    \"brushHardness\": " << hardness << ",\n";
    ofs << "    \"totalStrokes\": " << m_strokes.size() << ",\n";
    ofs << "    \"totalPoints\": " << GetTotalPointCount() << "\n";
    ofs << "  },\n";
    ofs << "  \"strokes\": [\n";

    for (size_t sIdx = 0; sIdx < m_strokes.size(); ++sIdx) {
        const auto& s = m_strokes[sIdx];
        ofs << "    {\n";
        ofs << "      \"strokeId\": " << s.strokeId << ",\n";
        ofs << "      \"durationMs\": " << (s.endTime >= s.startTime ? (s.endTime - s.startTime) : 0) << ",\n";
        ofs << "      \"maxPressure\": " << s.maxPressure << ",\n";
        ofs << "      \"avgSpeed\": " << s.avgSpeed << ",\n";
        ofs << "      \"pointCount\": " << s.points.size() << ",\n";
        ofs << "      \"points\": [\n";

        for (size_t pIdx = 0; pIdx < s.points.size(); ++pIdx) {
            const auto& p = s.points[pIdx];
            ofs << "        { \"t\": " << p.timeMs
                << ", \"nx\": " << p.normX
                << ", \"ny\": " << p.normY
                << ", \"p\": " << p.pressure
                << ", \"alt\": " << p.altitudeDeg
                << ", \"azm\": " << p.azimuthDeg
                << ", \"spd\": " << p.speedPxPerSec
                << ", \"w\": " << p.width
                << ", \"pf\": " << p.pressureFactor
                << ", \"dry\": " << p.dryness
                << ", \"dir\": " << p.moveAngleRad
                << " }" << (pIdx + 1 < s.points.size() ? "," : "") << "\n";
        }

        ofs << "      ]\n";
        ofs << "    }" << (sIdx + 1 < m_strokes.size() ? "," : "") << "\n";
    }

    ofs << "  ]\n";
    ofs << "}\n";

    return true;
}

bool TrajectorySession::ExportToCsv(const std::wstring& filePath) const {
    std::ofstream ofs(filePath);
    if (!ofs.is_open()) return false;

    ofs << "stroke_id,time_ms,norm_x,norm_y,paper_x,paper_y,pressure,altitude_deg,azimuth_deg,speed_px_sec,width,pressure_factor,dryness,move_angle_rad\n";
    ofs << std::fixed << std::setprecision(4);

    for (const auto& s : m_strokes) {
        for (const auto& p : s.points) {
            ofs << s.strokeId << ","
                << p.timeMs << ","
                << p.normX << ","
                << p.normY << ","
                << p.paperX << ","
                << p.paperY << ","
                << p.pressure << ","
                << p.altitudeDeg << ","
                << p.azimuthDeg << ","
                << p.speedPxPerSec << ","
                << p.width << ","
                << p.pressureFactor << ","
                << p.dryness << ","
                << p.moveAngleRad << "\n";
        }
    }

    return true;
}

bool TrajectorySession::PromptSaveArchiveJson(HWND hWnd, const TrajectorySession& session, PaperType paperType, Brush brushType, double hardness) {
    wchar_t szFileName[MAX_PATH] = L"";
    auto now = std::chrono::system_clock::now();
    std::time_t tt = std::chrono::system_clock::to_time_t(now);
    std::tm tm;
    localtime_s(&tm, &tt);
    swprintf_s(szFileName, MAX_PATH, L"Fudesence_Archive_%04d%02d%02d_%02d%02d%02d.json",
        tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);

    OPENFILENAMEW ofn = { 0 };
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hWnd;
    ofn.lpstrFilter = L"運筆アーカイブ JSON (*.json)\0*.json\0すべてのファイル (*.*)\0*.*\0";
    ofn.lpstrFile = szFileName;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrDefExt = L"json";
    ofn.Flags = OFN_EXPLORER | OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT;

    if (GetSaveFileNameW(&ofn)) {
        return session.ExportToJson(szFileName, paperType, brushType, hardness);
    }
    return false;
}

bool TrajectorySession::PromptSaveArchiveCsv(HWND hWnd, const TrajectorySession& session) {
    wchar_t szFileName[MAX_PATH] = L"";
    auto now = std::chrono::system_clock::now();
    std::time_t tt = std::chrono::system_clock::to_time_t(now);
    std::tm tm;
    localtime_s(&tm, &tt);
    swprintf_s(szFileName, MAX_PATH, L"Fudesence_Kinematics_%04d%02d%02d_%02d%02d%02d.csv",
        tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);

    OPENFILENAMEW ofn = { 0 };
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hWnd;
    ofn.lpstrFilter = L"運筆時系列データ CSV (*.csv)\0*.csv\0すべてのファイル (*.*)\0*.*\0";
    ofn.lpstrFile = szFileName;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrDefExt = L"csv";
    ofn.Flags = OFN_EXPLORER | OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT;

    if (GetSaveFileNameW(&ofn)) {
        return session.ExportToCsv(szFileName);
    }
    return false;
}

// タイムラインは m_strokes から決まるので、何度作り直しても
// 記録が変わらなければ結果は同じ。リビジョンを進めると、
// 解析タブへ入るたびにリプレイの墨を全部引き直すことになる。
void TrajectorySession::BuildReplayTimeline() {
    m_strokeTimelines.clear();
    m_totalReplayDurationMs = 0;

    if (m_strokes.empty()) return;

    DWORD curTimeline = 0;
    for (size_t i = 0; i < m_strokes.size(); ++i) {
        const auto& s = m_strokes[i];
        DWORD duration = 0;
        if (!s.points.empty()) {
            duration = s.points.back().timeMs;
            if (duration == 0) duration = static_cast<DWORD>(s.points.size() * 16);
        }
        if (duration < 50) duration = 50;

        if (i > 0) {
            // 画と画の間の空中移動時間（長すぎる待機時間は 300ms ~ 700ms に調整）
            DWORD gap = 400;
            if (s.startTime >= m_strokes[i - 1].endTime && m_strokes[i - 1].endTime > 0) {
                gap = s.startTime - m_strokes[i - 1].endTime;
                if (gap < 250) gap = 250;
                if (gap > 700) gap = 700;
            }
            curTimeline += gap;
        }

        StrokeTimelineInfo info;
        info.startTimelineMs = curTimeline;
        info.endTimelineMs = curTimeline + duration;
        m_strokeTimelines.push_back(info);

        curTimeline = info.endTimelineMs;
    }
    m_totalReplayDurationMs = curTimeline;
}

DWORD TrajectorySession::GetStrokeTimelineStart(size_t strokeIdx) const {
    if (strokeIdx < m_strokeTimelines.size()) {
        return m_strokeTimelines[strokeIdx].startTimelineMs;
    }
    return 0;
}

int TrajectorySession::FindStrokeIndexAtTimeline(DWORD timeMs) const {
    if (m_strokeTimelines.empty()) return -1;
    for (size_t i = 0; i < m_strokeTimelines.size(); ++i) {
        if (timeMs >= m_strokeTimelines[i].startTimelineMs && timeMs <= m_strokeTimelines[i].endTimelineMs) {
            return static_cast<int>(i);
        }
        if (timeMs < m_strokeTimelines[i].startTimelineMs) {
            return (i > 0) ? static_cast<int>(i - 1) : 0;
        }
    }
    return static_cast<int>(m_strokeTimelines.size() - 1);
}

bool TrajectorySession::GetReplaySample(DWORD timeMs, const RECT& rPaper, ReplaySample& outSample) const {
    if (m_strokes.empty() || m_strokeTimelines.empty()) return false;

    int pw = RenderUtils::RW(rPaper);
    int ph = RenderUtils::RH(rPaper);
    if (pw <= 0) pw = 1;
    if (ph <= 0) ph = 1;

    DWORD clampedTime = (timeMs > m_totalReplayDurationMs) ? m_totalReplayDurationMs : timeMs;
    outSample.timeMs = clampedTime;

    // 1. 各画の中にあるかを検索
    for (size_t i = 0; i < m_strokeTimelines.size(); ++i) {
        const auto& tl = m_strokeTimelines[i];
        const auto& s = m_strokes[i];
        if (s.points.empty()) continue;

        if (clampedTime >= tl.startTimelineMs && clampedTime <= tl.endTimelineMs) {
            outSample.strokeIndex = static_cast<int>(i);
            outSample.isPenDown = true;

            DWORD relTime = clampedTime - tl.startTimelineMs;
            if (s.points.size() == 1 || relTime <= s.points.front().timeMs) {
                outSample.point = s.points.front();
            } else if (relTime >= s.points.back().timeMs) {
                outSample.point = s.points.back();
            } else {
                // 線形補間
                size_t pIdx = 0;
                while (pIdx + 1 < s.points.size() && s.points[pIdx + 1].timeMs < relTime) {
                    pIdx++;
                }
                const auto& p1 = s.points[pIdx];
                const auto& p2 = s.points[pIdx + 1];
                double span = static_cast<double>(p2.timeMs - p1.timeMs);
                double t = (span > 0.0) ? (static_cast<double>(relTime - p1.timeMs) / span) : 0.0;
                t = RenderUtils::Clamp(t, 0.0, 1.0);

                outSample.point.timeMs = relTime;
                outSample.point.normX = p1.normX + (p2.normX - p1.normX) * t;
                outSample.point.normY = p1.normY + (p2.normY - p1.normY) * t;
                outSample.point.pressure = p1.pressure + (p2.pressure - p1.pressure) * t;
                outSample.point.altitudeDeg = p1.altitudeDeg + (p2.altitudeDeg - p1.altitudeDeg) * t;
                
                // 方位角の補間（360度境界処理）
                double dAzm = p2.azimuthDeg - p1.azimuthDeg;
                while (dAzm > 180.0) dAzm -= 360.0;
                while (dAzm < -180.0) dAzm += 360.0;
                outSample.point.azimuthDeg = p1.azimuthDeg + dAzm * t;
                while (outSample.point.azimuthDeg < 0.0) outSample.point.azimuthDeg += 360.0;
                while (outSample.point.azimuthDeg >= 360.0) outSample.point.azimuthDeg -= 360.0;

                outSample.point.speedPxPerSec = p1.speedPxPerSec + (p2.speedPxPerSec - p1.speedPxPerSec) * t;
                outSample.point.width = p1.width + (p2.width - p1.width) * t;
            }

            outSample.point.paperX = rPaper.left + static_cast<int>(outSample.point.normX * pw);
            outSample.point.paperY = rPaper.top + static_cast<int>(outSample.point.normY * ph);
            return true;
        }

        // 2. 画と画の間の空中移動 (Hover)
        if (i + 1 < m_strokeTimelines.size()) {
            const auto& nextTl = m_strokeTimelines[i + 1];
            const auto& nextS = m_strokes[i + 1];
            if (clampedTime > tl.endTimelineMs && clampedTime < nextTl.startTimelineMs) {
                outSample.strokeIndex = static_cast<int>(i);
                outSample.isPenDown = false; // 空中ホバー

                double gapSpan = static_cast<double>(nextTl.startTimelineMs - tl.endTimelineMs);
                double t = (gapSpan > 0.0) ? (static_cast<double>(clampedTime - tl.endTimelineMs) / gapSpan) : 0.0;
                t = RenderUtils::Clamp(t, 0.0, 1.0);

                const auto& pEnd = s.points.back();
                const auto& pStart = nextS.points.front();

                outSample.point.timeMs = clampedTime;
                outSample.point.normX = pEnd.normX + (pStart.normX - pEnd.normX) * t;
                outSample.point.normY = pEnd.normY + (pStart.normY - pEnd.normY) * t;
                outSample.point.pressure = 0.0;
                // 空中では筆が一度立ち上がる自然な動きを再現（最大85°）
                double arc = std::sin(t * 3.14159265358979323846);
                double baseAlt = pEnd.altitudeDeg + (pStart.altitudeDeg - pEnd.altitudeDeg) * t;
                outSample.point.altitudeDeg = baseAlt + (85.0 - baseAlt) * arc * 0.6;
                outSample.point.altitudeDeg = RenderUtils::Clamp(outSample.point.altitudeDeg, 20.0, 90.0);
                
                double dAzm = pStart.azimuthDeg - pEnd.azimuthDeg;
                while (dAzm > 180.0) dAzm -= 360.0;
                while (dAzm < -180.0) dAzm += 360.0;
                outSample.point.azimuthDeg = pEnd.azimuthDeg + dAzm * t;
                outSample.point.speedPxPerSec = 150.0;
                outSample.point.width = 0.0;

                outSample.point.paperX = rPaper.left + static_cast<int>(outSample.point.normX * pw);
                outSample.point.paperY = rPaper.top + static_cast<int>(outSample.point.normY * ph);
                return true;
            }
        }
    }

    // 末尾以降
    if (!m_strokes.empty() && !m_strokes.back().points.empty()) {
        const auto& lastP = m_strokes.back().points.back();
        outSample.strokeIndex = static_cast<int>(m_strokes.size() - 1);
        outSample.isPenDown = false;
        outSample.point = lastP;
        outSample.point.pressure = 0.0;
        outSample.point.paperX = rPaper.left + static_cast<int>(lastP.normX * pw);
        outSample.point.paperY = rPaper.top + static_cast<int>(lastP.normY * ph);
        return true;
    }

    return false;
}

