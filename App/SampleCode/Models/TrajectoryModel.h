#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <vector>
#include <string>
#include "PenInputEvent.h"
#include "AppEnums.h"

// 運筆の各サンプリング点データ（書道の物理情報・幾何学情報を保持）
struct StrokePoint {
    DWORD timeMs = 0;              // ストローク開始からの経過時間 (ms)
    double normX = 0.0;            // 半紙内正規化X (0.0: 左端 ~ 1.0: 右端)
    double normY = 0.0;            // 半紙内正規化Y (0.0: 上端 ~ 1.0: 下端)
    int paperX = 0;                // 半紙内ピクセルX
    int paperY = 0;                // 半紙内ピクセルY
    double pressure = 0.0;         // 筆圧 (0.0 ~ 1.0)
    double altitudeDeg = 90.0;     // 筆先高度角 (0° ~ 90°)
    double azimuthDeg = 0.0;       // 筆先方位角 (0° ~ 360°)
    double speedPxPerSec = 0.0;    // 運筆速度 (px/sec)
    double width = 0.0;            // 計算線幅 (px)
};

// 1画（ストローク）データ
struct StrokeData {
    int strokeId = 1;
    DWORD startTime = 0;
    DWORD endTime = 0;
    std::vector<StrokePoint> points;
    double maxPressure = 0.0;
    double maxSpeed = 0.0;
    double avgSpeed = 0.0;
};

// リアルタイム解析用メトリクス
struct RealtimeMetrics {
    double currentPressure = 0.0;
    double currentAltitude = 90.0;
    double currentAzimuth = 0.0;
    double currentSpeed = 0.0;
    double currentWidth = 0.0;
    bool isPenDown = false;
    double peakPressureInStroke = 0.0;
};

// リプレイ状態
enum class ReplayState {
    Stopped,
    Playing,
    Paused
};

// リプレイサンプリング姿勢情報
struct ReplaySample {
    DWORD timeMs = 0;
    int strokeIndex = -1;       // 現在の画インデックス (-1: 空中移動中または無効)
    bool isPenDown = false;      // 着筆中かどうか
    StrokePoint point;          // 補間された運筆姿勢
};

// リプレイ制御モデル
struct ReplayModel {
    ReplayState state = ReplayState::Stopped;
    DWORD currentTimeMs = 0;
    DWORD totalDurationMs = 0;
    double playbackSpeed = 1.0; // 0.5, 1.0, 2.0
    bool isDraggingSeekBar = false;
    bool isDraggingWaveform = false;
    ReplaySample currentSample;
    bool hasValidSample = false;

    void Reset() {
        state = ReplayState::Stopped;
        currentTimeMs = 0;
        isDraggingSeekBar = false;
        isDraggingWaveform = false;
        hasValidSample = false;
    }
};

// 揮毫セッション全体のアーカイブ管理
class TrajectorySession {
public:
    TrajectorySession();

    // 運筆イベントの記録
    void OnStrokeBegin(DWORD time);
    void AddPoint(const PenInputEvent& event, const RECT& rPaper, double width, double speed);
    void OnStrokeEnd();
    void Clear();

    // ゲッター
    const std::vector<StrokeData>& GetStrokes() const { return m_strokes; }
    const StrokeData* GetCurrentOrLastStroke() const;
    const RealtimeMetrics& GetRealtimeMetrics() const { return m_realtime; }
    size_t GetTotalPointCount() const;
    size_t GetTotalStrokeCount() const { return m_strokes.size(); }
    bool IsRecordingStroke() const { return m_isRecordingStroke; }

    // リプレイ用タイムライン＆サンプリング機能
    void BuildReplayTimeline();
    DWORD GetReplayTotalDurationMs() const { return m_totalReplayDurationMs; }
    bool GetReplaySample(DWORD timeMs, const RECT& rPaper, ReplaySample& outSample) const;
    void GetReplayVisiblePoints(DWORD timeMs, const RECT& rPaper, std::vector<std::vector<StrokePoint>>& outStrokes) const;
    DWORD GetStrokeTimelineStart(size_t strokeIdx) const;
    int FindStrokeIndexAtTimeline(DWORD timeMs) const;

    // エクスポート機能 (JSON / CSV)
    bool ExportToJson(const std::wstring& filePath, PaperType paperType, Brush brushType, double hardness) const;
    bool ExportToCsv(const std::wstring& filePath) const;

    // ファイル保存ダイアログ呼び出し
    static bool PromptSaveArchiveJson(HWND hWnd, const TrajectorySession& session, PaperType paperType, Brush brushType, double hardness);
    static bool PromptSaveArchiveCsv(HWND hWnd, const TrajectorySession& session);

private:
    std::vector<StrokeData> m_strokes;
    StrokeData m_currentStroke;
    bool m_isRecordingStroke = false;
    DWORD m_sessionStartTime = 0;
    RealtimeMetrics m_realtime;

    // リプレイ用内部タイムライン情報
    struct StrokeTimelineInfo {
        DWORD startTimelineMs = 0;
        DWORD endTimelineMs = 0;
    };
    std::vector<StrokeTimelineInfo> m_strokeTimelines;
    DWORD m_totalReplayDurationMs = 0;
};

