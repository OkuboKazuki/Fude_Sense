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
    m_currentStroke = StrokeData();
    m_isRecordingStroke = false;
    m_realtime = RealtimeMetrics();
    m_sessionStartTime = GetTickCount();
}

void TrajectorySession::UndoLastStroke() {
    if (m_isRecordingStroke) {
        // まだ書いている途中の画は、記録に積む前に破棄する。
        // 画素側も同じ時点（この画を書き始める前）へ戻る。
        m_currentStroke = StrokeData();
        m_isRecordingStroke = false;
        m_realtime.isPenDown = false;
        m_realtime.peakPressureInStroke = 0.0;
        return;
    }
    if (!m_strokes.empty()) {
        m_strokes.pop_back();
    }
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

void TrajectorySession::AddPoint(const PenInputEvent& event, const RECT& rPaper, double width, double speed) {
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
    ofs << "  \"version\": \"1.0\",\n";
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

    ofs << "stroke_id,time_ms,norm_x,norm_y,paper_x,paper_y,pressure,altitude_deg,azimuth_deg,speed_px_sec,width\n";
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
                << p.width << "\n";
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
