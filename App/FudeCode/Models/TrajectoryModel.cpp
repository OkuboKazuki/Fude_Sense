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
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>

TrajectorySession::TrajectorySession() {
    m_sessionStartTime = GetTickCount();
}

void TrajectorySession::BumpRevision() {
    static unsigned s_lastRevision = 0;
    m_revision = ++s_lastRevision;
}

void TrajectorySession::Clear() {
    m_strokes.clear();
    BumpRevision();
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
    BumpRevision();
    return true;
}

void TrajectorySession::RedoStroke(const StrokeData& stroke) {
    m_strokes.push_back(stroke);
    BumpRevision();
}

// 線幅・速度は画面上のピクセルで持っている。墨の半紙は長辺をそろえて回すので、
// 画面上の長辺の比で直せば、書いたときと同じ太さで引き直せる。
static double QuarterTurnScale(const RECT& oldPaper, const RECT& newPaper) {
    int oldLong = (std::max)(RenderUtils::RW(oldPaper), RenderUtils::RH(oldPaper));
    int newLong = (std::max)(RenderUtils::RW(newPaper), RenderUtils::RH(newPaper));
    return (oldLong > 0 && newLong > 0) ? static_cast<double>(newLong) / static_cast<double>(oldLong) : 1.0;
}

void TrajectorySession::RotateStrokeQuarter(StrokeData& stroke, bool counterClockwise, const RECT& oldPaper, const RECT& newPaper) {
    const double kPi = 3.14159265358979323846;
    double scale = QuarterTurnScale(oldPaper, newPaper);
    int newW = (std::max)(1, RenderUtils::RW(newPaper));
    int newH = (std::max)(1, RenderUtils::RH(newPaper));

    for (StrokePoint& p : stroke.points) {
        // 左回り: 元の (u, v) は (v, 1 - u) へ移る。右回り: 元の (u, v) は (1 - v, u) へ移る。
        // （GpuInk::RestoreSnapshotRotated と同じ対応）
        double u = p.normX;
        double v = p.normY;
        if (counterClockwise) { p.normX = v;       p.normY = 1.0 - u; }
        else                  { p.normX = 1.0 - v; p.normY = u; }
        p.paperX = static_cast<int>(p.normX * newW);
        p.paperY = static_cast<int>(p.normY * newH);

        // 向きを持つ値も半紙と一緒に回す。画面の y は下向きなので、左回りは角度が 90 度減る。
        double turnDeg = counterClockwise ? -90.0 : 90.0;
        p.azimuthDeg += turnDeg;
        while (p.azimuthDeg < 0.0) p.azimuthDeg += 360.0;
        while (p.azimuthDeg >= 360.0) p.azimuthDeg -= 360.0;
        p.moveAngleRad += turnDeg * (kPi / 180.0);

        p.width *= scale;
        p.speedPxPerSec *= scale;
    }
    stroke.maxSpeed *= scale;
    stroke.avgSpeed *= scale;
    stroke.paperW = newW;
    stroke.paperH = newH;
}

void TrajectorySession::RotateQuarter(bool counterClockwise, const RECT& oldPaper, const RECT& newPaper) {
    for (StrokeData& s : m_strokes) RotateStrokeQuarter(s, counterClockwise, oldPaper, newPaper);
    RotateStrokeQuarter(m_currentStroke, counterClockwise, oldPaper, newPaper);
    double scale = QuarterTurnScale(oldPaper, newPaper);
    m_realtime.currentSpeed *= scale;
    m_realtime.currentWidth *= scale;
    BumpRevision();
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
                                double pressureFactor, double dryness, double moveAngleRad, double inkAmount) {
    BumpRevision();
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
    pt.inkAmount = inkAmount;

    m_currentStroke.points.push_back(pt);
    m_currentStroke.paperW = paperW;
    m_currentStroke.paperH = paperH;
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
        BumpRevision();
        m_isRecordingStroke = false;
        m_realtime.isPenDown = false;
        // 書き始め（startTime）はペンのタイムスタンプなので、書き終わりも同じ時計で取る。
        // GetTickCount と Wintab の pkTime は基準がずれることがあり、混ぜると画の間隔が狂う。
        m_currentStroke.endTime = m_currentStroke.points.empty()
            ? m_currentStroke.startTime
            : m_currentStroke.startTime + m_currentStroke.points.back().timeMs;

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
    }

    const char* brushNameStr = "Medium";
    switch (brushType) {
    case Brush::Small:  brushNameStr = "Small"; break;
    case Brush::Medium: brushNameStr = "Medium"; break;
    case Brush::Large:  brushNameStr = "Large"; break;
    }

    ofs << std::fixed << std::setprecision(4);
    ofs << "{\n";
    ofs << "  \"format\": \"FudeSenseStrokeArchive\",\n";
    ofs << "  \"version\": \"1.2\",\n";
    ofs << "  \"metadata\": {\n";
    ofs << "    \"application\": \"Fude Sense\",\n";
    ofs << "    \"recordedAt\": \"" << GetCurrentISOTimestamp() << "\",\n";
    ofs << "    \"paperType\": \"" << paperNameStr << "\",\n";
    ofs << "    \"brushType\": \"" << brushNameStr << "\",\n";
    ofs << "    \"brushHardness\": " << hardness << ",\n";
    ofs << "    \"totalStrokes\": " << m_strokes.size() << ",\n";
    ofs << "    \"totalPoints\": " << GetTotalPointCount() << "\n";
    ofs << "  },\n";
    ofs << "  \"strokes\": [\n";

    // 画の時刻は 1画目の書き始めから数える
    const DWORD origin = m_strokes.empty() ? 0 : m_strokes.front().startTime;
    for (size_t sIdx = 0; sIdx < m_strokes.size(); ++sIdx) {
        const auto& s = m_strokes[sIdx];
        ofs << "    {\n";
        ofs << "      \"strokeId\": " << s.strokeId << ",\n";
        ofs << "      \"durationMs\": " << (s.endTime >= s.startTime ? (s.endTime - s.startTime) : 0) << ",\n";
        // 読み込んだときに画と画の間の時間を再現するための時刻
        ofs << "      \"startMs\": " << (s.startTime - origin) << ",\n";
        ofs << "      \"endMs\": " << (s.endTime - origin) << ",\n";
        // 線幅 w・速度 spd の基準になる半紙の大きさ (px)
        ofs << "      \"paperWidthPx\": " << s.paperW << ",\n";
        ofs << "      \"paperHeightPx\": " << s.paperH << ",\n";
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
                << ", \"ink\": " << p.inkAmount
                << " }" << (pIdx + 1 < s.points.size() ? "," : "") << "\n";
        }

        ofs << "      ]\n";
        ofs << "    }" << (sIdx + 1 < m_strokes.size() ? "," : "") << "\n";
    }

    ofs << "  ]\n";
    ofs << "}\n";

    // 開けただけでは成功ではない。容量不足などで書き込みが途中で止まっていないかを見る
    ofs.close();
    return !ofs.fail();
}

bool TrajectorySession::ExportToCsv(const std::wstring& filePath) const {
    std::ofstream ofs(filePath);
    if (!ofs.is_open()) return false;

    ofs << "stroke_id,time_ms,norm_x,norm_y,paper_x,paper_y,pressure,altitude_deg,azimuth_deg,speed_px_sec,width,pressure_factor,dryness,move_angle_rad,ink_amount,stroke_start_ms,stroke_end_ms,paper_w_px,paper_h_px\n";
    ofs << std::fixed << std::setprecision(4);

    // 画の時刻は 1画目の書き始めから数える
    const DWORD origin = m_strokes.empty() ? 0 : m_strokes.front().startTime;
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
                << p.moveAngleRad << ","
                << p.inkAmount << ","
                << (s.startTime - origin) << ","
                << (s.endTime - origin) << ","
                << s.paperW << ","
                << s.paperH << "\n";
        }
    }

    ofs.close();
    return !ofs.fail();
}

// 保存ダイアログが閉じた理由。CommDlgExtendedError が 0 ならユーザーの取り消し
static TrajectorySession::SaveResult DialogClosedResult() {
    return (CommDlgExtendedError() == 0) ? TrajectorySession::SaveResult::Canceled : TrajectorySession::SaveResult::Failed;
}

TrajectorySession::SaveResult TrajectorySession::PromptSaveArchiveJson(HWND hWnd, const TrajectorySession& session, PaperType paperType, Brush brushType, double hardness) {
    wchar_t szFileName[MAX_PATH] = L"";
    auto now = std::chrono::system_clock::now();
    std::time_t tt = std::chrono::system_clock::to_time_t(now);
    std::tm tm;
    localtime_s(&tm, &tt);
    swprintf_s(szFileName, MAX_PATH, L"FudeSense_Archive_%04d%02d%02d_%02d%02d%02d.json",
        tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);

    OPENFILENAMEW ofn = { 0 };
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hWnd;
    ofn.lpstrFilter = L"運筆アーカイブ JSON (*.json)\0*.json\0すべてのファイル (*.*)\0*.*\0";
    ofn.lpstrFile = szFileName;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrDefExt = L"json";
    ofn.Flags = OFN_EXPLORER | OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT;

    if (!GetSaveFileNameW(&ofn)) return DialogClosedResult();
    if (session.ExportToJson(szFileName, paperType, brushType, hardness)) return SaveResult::Saved;
    // 途中までしか書けていないファイルは残さない
    DeleteFileW(szFileName);
    return SaveResult::Failed;
}

TrajectorySession::SaveResult TrajectorySession::PromptSaveArchiveCsv(HWND hWnd, const TrajectorySession& session) {
    wchar_t szFileName[MAX_PATH] = L"";
    auto now = std::chrono::system_clock::now();
    std::time_t tt = std::chrono::system_clock::to_time_t(now);
    std::tm tm;
    localtime_s(&tm, &tt);
    swprintf_s(szFileName, MAX_PATH, L"FudeSense_Kinematics_%04d%02d%02d_%02d%02d%02d.csv",
        tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);

    OPENFILENAMEW ofn = { 0 };
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hWnd;
    ofn.lpstrFilter = L"運筆時系列データ CSV (*.csv)\0*.csv\0すべてのファイル (*.*)\0*.*\0";
    ofn.lpstrFile = szFileName;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrDefExt = L"csv";
    ofn.Flags = OFN_EXPLORER | OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT;

    if (!GetSaveFileNameW(&ofn)) return DialogClosedResult();
    if (session.ExportToCsv(szFileName)) return SaveResult::Saved;
    DeleteFileW(szFileName);
    return SaveResult::Failed;
}

// ---------------------------------------------------------------------------
// 運筆アーカイブの読み込み
// 書き出した JSON / CSV を読み、リプレイと解析にかけられる記録へ戻す。
// 他人のファイルを開くので、欠けた値・範囲外の値・桁違いの大きさには
// 備えておき、取り込めない場合は理由を返して何も変えない。
// ---------------------------------------------------------------------------
namespace {

// 取り込む記録の上限。壊れたファイルでメモリを使い切らないため
constexpr size_t kImportMaxFileBytes = 256u * 1024u * 1024u;
constexpr size_t kImportMaxStrokes = 5000;
constexpr size_t kImportMaxPoints = 2000000;
// 1画の長さの上限 (ms)。これを超える時刻は壊れた値として切り詰める
constexpr double kImportMaxStrokeMs = 10.0 * 60.0 * 1000.0;
// 画の時刻の基準。0 は「時刻なし」と区別できないので少し先から並べる
constexpr DWORD kImportTimeBase = 1000;
// 画と画の間の時間がファイルに無いときに空ける時間（BuildReplayTimeline の既定と同じ）
constexpr DWORD kImportDefaultGapMs = 400;

// 読み込み途中の1画。半紙の大きさと画の時刻は、ファイルに無ければ後で補う
struct ImportedStroke {
    StrokeData data;
    bool hasTiming = false;
    double startMs = 0.0;
    double endMs = 0.0;
    double paperW = 0.0;  // 0 なら不明
    double paperH = 0.0;
    bool hasPaperXY = false; // paperX / paperY から半紙の大きさを逆算できるか
};

bool ParseDouble(const char* begin, const char* end, double& out) {
    // strtod は終端の無い範囲を読めないので、短い数値だけを写して読む
    char buf[64];
    size_t len = static_cast<size_t>(end - begin);
    while (len > 0 && (*begin == ' ' || *begin == '\t')) { ++begin; --len; }
    while (len > 0 && (begin[len - 1] == ' ' || begin[len - 1] == '\t' || begin[len - 1] == '\r')) --len;
    if (len == 0 || len >= sizeof(buf)) return false;
    memcpy(buf, begin, len);
    buf[len] = '\0';
    char* stop = nullptr;
    double v = strtod(buf, &stop);
    if (stop != buf + len || !std::isfinite(v)) return false;
    out = v;
    return true;
}

// 書き出した形式（キーの順・空白は問わない）を読む小さな JSON リーダー。
// 値をすべて木に組むと点ごとに大きなメモリを食うので、知っているキーだけ
// その場で取り出し、それ以外は読み飛ばす。
class ArchiveJsonReader {
public:
    explicit ArchiveJsonReader(const std::string& text)
        : m_p(text.data()), m_end(text.data() + text.size()) {}

    bool Read(std::vector<ImportedStroke>& out, std::wstring& err) {
        bool formatOk = true;
        bool ok = ParseObject([&](const std::string& key) -> bool {
            if (key == "format") {
                std::string fmt;
                if (!ParseString(fmt)) return false;
                formatOk = (fmt == "FudeSenseStrokeArchive");
                return true;
            }
            if (key == "strokes") {
                return ParseArray([&]() -> bool {
                    if (out.size() >= kImportMaxStrokes) { m_tooLarge = true; return false; }
                    out.emplace_back();
                    return ParseStroke(out.back());
                });
            }
            return SkipValue(0);
        });
        if (m_tooLarge) { err = L"記録が大きすぎるため読み込めません。"; return false; }
        if (!ok) { err = L"JSON の形式が正しくありません（ファイルが壊れている可能性があります）。"; return false; }
        if (!formatOk) { err = L"Fude Sense の運筆アーカイブではありません。"; return false; }
        return true;
    }

private:
    const char* m_p;
    const char* m_end;
    size_t m_pointCount = 0;
    bool m_tooLarge = false;

    void SkipWs() {
        while (m_p < m_end && (*m_p == ' ' || *m_p == '\t' || *m_p == '\r' || *m_p == '\n')) ++m_p;
    }
    bool Consume(char c) {
        SkipWs();
        if (m_p < m_end && *m_p == c) { ++m_p; return true; }
        return false;
    }

    bool ParseString(std::string& out) {
        out.clear();
        if (!Consume('"')) return false;
        while (m_p < m_end) {
            char c = *m_p++;
            if (c == '"') return true;
            if (c == '\\') {
                if (m_p >= m_end) return false;
                char e = *m_p++;
                switch (e) {
                case 'n': out.push_back('\n'); break;
                case 't': out.push_back('\t'); break;
                case 'r': out.push_back('\r'); break;
                case 'b': out.push_back('\b'); break;
                case 'f': out.push_back('\f'); break;
                case 'u':
                    // キーにも値にも使わないので、文字としては取り出さずに読み飛ばす
                    if (m_end - m_p < 4) return false;
                    m_p += 4;
                    out.push_back('?');
                    break;
                default: out.push_back(e); break;
                }
            } else {
                out.push_back(c);
            }
        }
        return false;
    }

    bool ParseNumber(double& out) {
        SkipWs();
        const char* start = m_p;
        while (m_p < m_end && (isdigit(static_cast<unsigned char>(*m_p)) || *m_p == '-' || *m_p == '+'
                               || *m_p == '.' || *m_p == 'e' || *m_p == 'E')) {
            ++m_p;
        }
        return ParseDouble(start, m_p, out);
    }

    // 数値なら out へ入れて has を立てる。null や文字列など数値以外の値は
    // 読み飛ばして has を下ろす。false を返すのは JSON として壊れているときだけ
    bool ReadOptionalNumber(double& out, bool& has) {
        has = false;
        SkipWs();
        if (m_p >= m_end) return false;
        char c = *m_p;
        if (c == '-' || c == '+' || c == '.' || isdigit(static_cast<unsigned char>(c))) {
            has = ParseNumber(out);
            return has;
        }
        return SkipValue(0);
    }

    bool SkipValue(int depth) {
        if (depth > 64) return false;
        SkipWs();
        if (m_p >= m_end) return false;
        char c = *m_p;
        if (c == '"') { std::string s; return ParseString(s); }
        if (c == '{') return ParseObject([&](const std::string&) { return SkipValue(depth + 1); });
        if (c == '[') return ParseArray([&]() { return SkipValue(depth + 1); });
        if (m_end - m_p >= 4 && (strncmp(m_p, "true", 4) == 0 || strncmp(m_p, "null", 4) == 0)) { m_p += 4; return true; }
        if (m_end - m_p >= 5 && strncmp(m_p, "false", 5) == 0) { m_p += 5; return true; }
        double d = 0.0;
        return ParseNumber(d);
    }

    // onMember(key) は値を1つ読み切って true を返す
    template <class F>
    bool ParseObject(F onMember) {
        if (!Consume('{')) return false;
        if (Consume('}')) return true;
        for (;;) {
            std::string key;
            if (!ParseString(key)) return false;
            if (!Consume(':')) return false;
            if (!onMember(key)) return false;
            if (Consume(',')) continue;
            return Consume('}');
        }
    }

    template <class F>
    bool ParseArray(F onItem) {
        if (!Consume('[')) return false;
        if (Consume(']')) return true;
        for (;;) {
            if (!onItem()) return false;
            if (Consume(',')) continue;
            return Consume(']');
        }
    }

    bool ParseStroke(ImportedStroke& s) {
        bool hasStart = false, hasEnd = false;
        bool ok = ParseObject([&](const std::string& key) -> bool {
            bool has = false;
            if (key == "points") {
                return ParseArray([&]() -> bool {
                    if (++m_pointCount > kImportMaxPoints) { m_tooLarge = true; return false; }
                    s.data.points.emplace_back();
                    return ParsePoint(s.data.points.back());
                });
            }
            if (key == "startMs")       return ReadOptionalNumber(s.startMs, hasStart);
            if (key == "endMs")         return ReadOptionalNumber(s.endMs, hasEnd);
            if (key == "paperWidthPx")  return ReadOptionalNumber(s.paperW, has);
            if (key == "paperHeightPx") return ReadOptionalNumber(s.paperH, has);
            return SkipValue(0);
        });
        s.hasTiming = hasStart && hasEnd;
        return ok;
    }

    bool ParsePoint(StrokePoint& p) {
        return ParseObject([&](const std::string& key) -> bool {
            double v = 0.0;
            bool has = false;
            if (!ReadOptionalNumber(v, has)) return false;
            if (!has) return true;
            if      (key == "t")   p.timeMs = static_cast<DWORD>(RenderUtils::Clamp(v, 0.0, kImportMaxStrokeMs));
            else if (key == "nx")  p.normX = v;
            else if (key == "ny")  p.normY = v;
            else if (key == "p")   p.pressure = v;
            else if (key == "alt") p.altitudeDeg = v;
            else if (key == "azm") p.azimuthDeg = v;
            else if (key == "spd") p.speedPxPerSec = v;
            else if (key == "w")   p.width = v;
            else if (key == "pf")  p.pressureFactor = v;
            else if (key == "dry") p.dryness = v;
            else if (key == "dir") p.moveAngleRad = v;
            else if (key == "ink") p.inkAmount = v;
            return true;
        });
    }
};

// CSV の1行をカンマで区切る（書き出す CSV に引用符付きの値は無い）
void SplitCsvLine(const std::string& line, std::vector<std::pair<const char*, const char*>>& out) {
    out.clear();
    const char* p = line.data();
    const char* end = p + line.size();
    const char* fieldStart = p;
    for (; p <= end; ++p) {
        if (p == end || *p == ',') {
            out.emplace_back(fieldStart, p);
            fieldStart = p + 1;
        }
    }
}

std::string TrimLower(const char* b, const char* e) {
    while (b < e && (*b == ' ' || *b == '\t' || *b == '"')) ++b;
    while (e > b && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r' || e[-1] == '"')) --e;
    std::string s(b, e);
    for (char& c : s) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
    return s;
}

bool ReadArchiveCsv(const std::string& text, std::vector<ImportedStroke>& out, std::wstring& err) {
    enum Col { StrokeId, TimeMs, NormX, NormY, PaperX, PaperY, Pressure, Altitude, Azimuth, Speed, Width,
               PressureFactor, Dryness, MoveAngle, InkAmount, StrokeStart, StrokeEnd, PaperW, PaperH, ColCount };
    static const char* const kNames[ColCount] = {
        "stroke_id", "time_ms", "norm_x", "norm_y", "paper_x", "paper_y", "pressure", "altitude_deg",
        "azimuth_deg", "speed_px_sec", "width", "pressure_factor", "dryness", "move_angle_rad",
        "ink_amount", "stroke_start_ms", "stroke_end_ms", "paper_w_px", "paper_h_px"
    };
    int colIndex[ColCount];
    for (int& c : colIndex) c = -1;

    std::vector<std::pair<const char*, const char*>> fields;
    bool haveHeader = false;
    double lastStrokeId = 0.0;
    size_t pointCount = 0;
    size_t lineNo = 0;

    size_t pos = 0;
    while (pos < text.size()) {
        size_t nl = text.find('\n', pos);
        if (nl == std::string::npos) nl = text.size();
        std::string line = text.substr(pos, nl - pos);
        pos = nl + 1;
        ++lineNo;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;

        SplitCsvLine(line, fields);
        if (!haveHeader) {
            for (size_t i = 0; i < fields.size(); ++i) {
                std::string name = TrimLower(fields[i].first, fields[i].second);
                for (int c = 0; c < ColCount; ++c) {
                    if (name == kNames[c]) colIndex[c] = static_cast<int>(i);
                }
            }
            const int required[] = { StrokeId, TimeMs, NormX, NormY, Width };
            for (int c : required) {
                if (colIndex[c] < 0) {
                    err = L"Fude Sense の運筆データ CSV ではありません（必要な列 ";
                    err += std::wstring(kNames[c], kNames[c] + strlen(kNames[c]));
                    err += L" がありません）。";
                    return false;
                }
            }
            haveHeader = true;
            continue;
        }

        auto get = [&](int col, double& v) -> bool {
            int idx = colIndex[col];
            if (idx < 0 || static_cast<size_t>(idx) >= fields.size()) return false;
            return ParseDouble(fields[idx].first, fields[idx].second, v);
        };

        double strokeId = 0.0, t = 0.0, nx = 0.0, ny = 0.0, w = 0.0;
        if (!get(StrokeId, strokeId) || !get(TimeMs, t) || !get(NormX, nx) || !get(NormY, ny) || !get(Width, w)) {
            err = L"CSV の " + std::to_wstring(lineNo) + L" 行目の値を読めませんでした。";
            return false;
        }

        // 画番号が変わったところで次の画に移る
        if (out.empty() || strokeId != lastStrokeId) {
            if (out.size() >= kImportMaxStrokes) { err = L"記録が大きすぎるため読み込めません。"; return false; }
            out.emplace_back();
            lastStrokeId = strokeId;
            ImportedStroke& s = out.back();
            double start = 0.0, end = 0.0;
            if (get(StrokeStart, start) && get(StrokeEnd, end)) {
                s.hasTiming = true;
                s.startMs = start;
                s.endMs = end;
            }
            get(PaperW, s.paperW);
            get(PaperH, s.paperH);
        }
        if (++pointCount > kImportMaxPoints) { err = L"記録が大きすぎるため読み込めません。"; return false; }

        ImportedStroke& s = out.back();
        StrokePoint p;
        p.timeMs = static_cast<DWORD>(RenderUtils::Clamp(t, 0.0, kImportMaxStrokeMs));
        p.normX = nx;
        p.normY = ny;
        p.width = w;
        double v = 0.0;
        double px = 0.0, py = 0.0;
        if (get(PaperX, px) && get(PaperY, py)) {
            p.paperX = static_cast<int>(px);
            p.paperY = static_cast<int>(py);
            s.hasPaperXY = true;
        }
        if (get(Pressure, v)) p.pressure = v;
        p.pressureFactor = p.pressure;  // 列が無い古い CSV では筆圧そのものを使う
        if (get(PressureFactor, v)) p.pressureFactor = v;
        if (get(Altitude, v)) p.altitudeDeg = v;
        if (get(Azimuth, v)) p.azimuthDeg = v;
        if (get(Speed, v)) p.speedPxPerSec = v;
        if (get(Dryness, v)) p.dryness = v;
        if (get(MoveAngle, v)) p.moveAngleRad = v;
        if (get(InkAmount, v)) p.inkAmount = v;
        s.data.points.push_back(p);
    }

    if (!haveHeader) {
        err = L"CSV に見出し行がありません。";
        return false;
    }
    return true;
}

// 書いたときの半紙の長辺 (px)。ファイルに無ければ、半紙内ピクセル座標と
// 正規化座標の比から逆算する。どちらも無ければ 0（不明）。
double SourcePaperLongEdge(const ImportedStroke& s) {
    if (s.paperW > 0.0 && s.paperH > 0.0) return (std::max)(s.paperW, s.paperH);
    if (!s.hasPaperXY) return 0.0;
    double sumW = 0.0, sumH = 0.0;
    int nW = 0, nH = 0;
    for (const StrokePoint& p : s.data.points) {
        // 端に近い点は正規化で切り詰められていることがあるので使わない
        if (p.normX > 0.05 && p.normX < 0.95) { sumW += p.paperX / p.normX; ++nW; }
        if (p.normY > 0.05 && p.normY < 0.95) { sumH += p.paperY / p.normY; ++nH; }
    }
    double w = (nW > 0) ? sumW / nW : 0.0;
    double h = (nH > 0) ? sumH / nH : 0.0;
    return (std::max)(w, h);
}

double FiniteOr(double v, double fallback) {
    return std::isfinite(v) ? v : fallback;
}

// 読んだ値を、いまの半紙で再生できる記録に整える
bool FinalizeImport(std::vector<ImportedStroke>& in, const RECT& rPaper, std::vector<StrokeData>& out, std::wstring& err) {
    const double kPi = 3.14159265358979323846;
    int curW = (std::max)(1, RenderUtils::RW(rPaper));
    int curH = (std::max)(1, RenderUtils::RH(rPaper));
    double curLong = static_cast<double>((std::max)(curW, curH));

    // 画と画の間の時間は、全部の画に時刻があるときだけ使う
    bool allTimed = true;
    for (const ImportedStroke& s : in) {
        if (!s.data.points.empty() && !s.hasTiming) allTimed = false;
    }

    out.clear();
    DWORD prevEnd = 0;
    for (ImportedStroke& src : in) {
        if (src.data.points.empty()) continue;

        double srcLong = SourcePaperLongEdge(src);
        double scale = (srcLong > 0.0) ? curLong / srcLong : 1.0;
        if (!std::isfinite(scale) || scale <= 0.0) scale = 1.0;

        StrokeData s;
        s.points = std::move(src.data.points);
        DWORD prevT = 0;
        double totalSpeed = 0.0;
        for (StrokePoint& p : s.points) {
            // 時刻は戻らないものとして並べる（壊れた記録で補間が逆走しないように）
            if (p.timeMs < prevT) p.timeMs = prevT;
            prevT = p.timeMs;

            p.normX = RenderUtils::Clamp(FiniteOr(p.normX, 0.0), 0.0, 1.0);
            p.normY = RenderUtils::Clamp(FiniteOr(p.normY, 0.0), 0.0, 1.0);
            p.paperX = static_cast<int>(p.normX * curW);
            p.paperY = static_cast<int>(p.normY * curH);
            p.pressure = RenderUtils::Clamp(FiniteOr(p.pressure, 0.0), 0.0, 1.0);
            p.pressureFactor = RenderUtils::Clamp(FiniteOr(p.pressureFactor, p.pressure), 0.0, 1.0);
            p.dryness = RenderUtils::Clamp(FiniteOr(p.dryness, 0.0), 0.0, 1.0);
            p.altitudeDeg = RenderUtils::Clamp(FiniteOr(p.altitudeDeg, 90.0), 0.0, 90.0);
            p.azimuthDeg = std::fmod(FiniteOr(p.azimuthDeg, 0.0), 360.0);
            if (p.azimuthDeg < 0.0) p.azimuthDeg += 360.0;
            p.moveAngleRad = std::fmod(FiniteOr(p.moveAngleRad, 0.0), 2.0 * kPi);
            // 線幅は半紙の長辺の半分まで（それ以上は壊れた値）
            p.width = RenderUtils::Clamp(FiniteOr(p.width, 0.0) * scale, 0.0, curLong * 0.5);
            p.speedPxPerSec = (std::max)(0.0, FiniteOr(p.speedPxPerSec, 0.0) * scale);
            p.inkAmount = (std::isfinite(p.inkAmount) && p.inkAmount >= 0.0) ? (std::min)(p.inkAmount, 1.0) : -1.0;

            if (p.pressure > s.maxPressure) s.maxPressure = p.pressure;
            if (p.speedPxPerSec > s.maxSpeed) s.maxSpeed = p.speedPxPerSec;
            totalSpeed += p.speedPxPerSec;
        }
        s.avgSpeed = totalSpeed / static_cast<double>(s.points.size());
        s.strokeId = static_cast<int>(out.size()) + 1;
        s.paperW = curW;
        s.paperH = curH;

        DWORD duration = s.points.back().timeMs;
        if (allTimed) {
            double start = RenderUtils::Clamp(FiniteOr(src.startMs, 0.0), 0.0, 24.0 * 3600.0 * 1000.0);
            double end = RenderUtils::Clamp(FiniteOr(src.endMs, start), start, 24.0 * 3600.0 * 1000.0);
            s.startTime = kImportTimeBase + static_cast<DWORD>(start);
            s.endTime = kImportTimeBase + static_cast<DWORD>(end);
            if (s.endTime < s.startTime + duration) s.endTime = s.startTime + duration;
        } else {
            s.startTime = out.empty() ? kImportTimeBase : prevEnd + kImportDefaultGapMs;
            s.endTime = s.startTime + duration;
        }
        prevEnd = s.endTime;
        out.push_back(std::move(s));
    }

    if (out.empty()) {
        err = L"運筆の記録が見つかりませんでした（画が1つもありません）。";
        return false;
    }
    return true;
}

std::wstring FileNameOf(const std::wstring& path) {
    size_t slash = path.find_last_of(L"\\/");
    return (slash == std::wstring::npos) ? path : path.substr(slash + 1);
}

} // namespace

bool TrajectorySession::ImportFromFile(const std::wstring& filePath, const RECT& rPaper, std::wstring& outError) {
    std::ifstream ifs(filePath, std::ios::binary);
    if (!ifs.is_open()) {
        outError = L"ファイルを開けませんでした。";
        return false;
    }
    ifs.seekg(0, std::ios::end);
    std::streamoff size = ifs.tellg();
    ifs.seekg(0, std::ios::beg);
    if (size <= 0) {
        outError = L"ファイルが空です。";
        return false;
    }
    if (static_cast<unsigned long long>(size) > kImportMaxFileBytes) {
        outError = L"ファイルが大きすぎるため読み込めません。";
        return false;
    }
    std::string text(static_cast<size_t>(size), '\0');
    if (!ifs.read(&text[0], size)) {
        outError = L"ファイルを読み込めませんでした。";
        return false;
    }
    // Excel などで保存し直した UTF-8 には先頭に BOM が付く
    if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEF
        && static_cast<unsigned char>(text[1]) == 0xBB && static_cast<unsigned char>(text[2]) == 0xBF) {
        text.erase(0, 3);
    }

    // 拡張子ではなく中身で見分ける（名前を付け替えたファイルも読めるように）
    size_t first = text.find_first_not_of(" \t\r\n");
    bool isJson = (first != std::string::npos && text[first] == '{');

    std::vector<ImportedStroke> raw;
    if (isJson) {
        ArchiveJsonReader reader(text);
        if (!reader.Read(raw, outError)) return false;
    } else {
        if (!ReadArchiveCsv(text, raw, outError)) return false;
    }

    std::vector<StrokeData> strokes;
    if (!FinalizeImport(raw, rPaper, strokes, outError)) return false;

    Clear();
    m_strokes = std::move(strokes);
    BumpRevision();
    return true;
}

TrajectorySession::SaveResult TrajectorySession::PromptLoadArchive(HWND hWnd, TrajectorySession& session, const RECT& rPaper,
                                                                   std::wstring& outFileName, std::wstring& outError) {
    wchar_t szFileName[MAX_PATH] = L"";

    OPENFILENAMEW ofn = { 0 };
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hWnd;
    ofn.lpstrFilter = L"運筆アーカイブ (*.json;*.csv)\0*.json;*.csv\0運筆アーカイブ JSON (*.json)\0*.json\0運筆時系列データ CSV (*.csv)\0*.csv\0すべてのファイル (*.*)\0*.*\0";
    ofn.lpstrFile = szFileName;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrTitle = L"解析する運筆アーカイブを開く";
    ofn.Flags = OFN_EXPLORER | OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;

    if (!GetOpenFileNameW(&ofn)) return DialogClosedResult();
    if (!session.ImportFromFile(szFileName, rPaper, outError)) return SaveResult::Failed;
    outFileName = FileNameOf(szFileName);
    return SaveResult::Saved;
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

DWORD TrajectorySession::GetStrokeTimelineEnd(size_t strokeIdx) const {
    if (strokeIdx < m_strokeTimelines.size()) {
        return m_strokeTimelines[strokeIdx].endTimelineMs;
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

size_t TrajectorySession::GetVisiblePointCount(size_t strokeIdx, DWORD timeMs) const {
    if (strokeIdx >= m_strokes.size() || strokeIdx >= m_strokeTimelines.size()) return 0;
    const auto& pts = m_strokes[strokeIdx].points;
    DWORD start = m_strokeTimelines[strokeIdx].startTimelineMs;
    if (timeMs < start) return 0;
    DWORD rel = timeMs - start;
    size_t n = 0;
    while (n < pts.size() && pts[n].timeMs <= rel) ++n;
    return n;
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
                outSample.point.inkAmount = (p1.inkAmount >= 0.0 && p2.inkAmount >= 0.0)
                    ? p1.inkAmount + (p2.inkAmount - p1.inkAmount) * t
                    : p1.inkAmount;
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
                // 空中では墨は減らないので、書き終えた時点の残量のまま
                outSample.point.inkAmount = pEnd.inkAmount;

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

