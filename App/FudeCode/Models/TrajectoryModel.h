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

    // 以下は描画時に GpuInk へ渡した値そのもの。
    // 筆圧の平滑や墨量の消費は経路依存で、記録から計算し直しても
    // 同じ値にはならない（筆の太さや硬さを後から変えられるため）。
    // リプレイを実物と一致させるため、結果をそのまま控える。
    double pressureFactor = 0.0;   // 筆の硬さ補正後の筆圧 (0.0 ~ 1.0)
    double dryness = 0.0;          // この点を打った時点の筆の乾き具合
    double moveAngleRad = 0.0;     // 平滑済みの運筆方向（かすれの筋の軸）
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
        totalDurationMs = 0;
        isDraggingSeekBar = false;
        isDraggingWaveform = false;
        currentSample = ReplaySample();
        hasValidSample = false;
    }
};

// 揮毫セッション全体のアーカイブ管理
class TrajectorySession {
public:
    TrajectorySession();

    // 運筆イベントの記録
    void OnStrokeBegin(DWORD time);
    // width / pressureFactor / dryness / moveAngleRad は、呼び出し側が GpuInk へ
    // 渡すのと同じ値を渡す。リプレイはこれをそのまま流し込む。
    void AddPoint(const PenInputEvent& event, const RECT& rPaper, double width, double speed,
                  double pressureFactor, double dryness, double moveAngleRad);
    void OnStrokeEnd();
    // 直前の1画を記録から取り消す（「一画戻す」で画面と揃えるため）。
    // 取り消した1画を outRemoved へ返す（「一画復元」で積み直すのに使う）。
    // 書いている途中で取り消した場合は記録に載っていないので false を返す
    bool UndoLastStroke(StrokeData* outRemoved = nullptr);
    // 取り消した1画を記録へ積み直す（「一画復元」用）
    void RedoStroke(const StrokeData& stroke);
    void Clear();
    // 記録を半紙ごと90度回す（紙だけ表示の出入り用。墨の書き戻しと同じ向きに回す）。
    // counterClockwise は左回り（通常表示 → 紙だけ表示）。
    // oldPaper / newPaper は回す前後の画面上の半紙。線幅・速度・半紙内ピクセル座標は
    // 画面上の大きさで持っているので、新しい半紙の大きさへ直す。
    // タイムラインは作り直さないので、呼び出し側で BuildReplayTimeline し直すこと。
    void RotateQuarter(bool counterClockwise, const RECT& oldPaper, const RECT& newPaper);
    // 記録の外にある1画（「一画復元」の控えが持つ画）を、RotateQuarter と同じように回す
    static void RotateStrokeQuarter(StrokeData& stroke, bool counterClockwise, const RECT& oldPaper, const RECT& newPaper);

    // ゲッター
    const std::vector<StrokeData>& GetStrokes() const { return m_strokes; }
    const StrokeData* GetCurrentOrLastStroke() const;
    const RealtimeMetrics& GetRealtimeMetrics() const { return m_realtime; }
    size_t GetTotalPointCount() const;
    size_t GetTotalStrokeCount() const { return m_strokes.size(); }
    bool IsRecordingStroke() const { return m_isRecordingStroke; }

    // 記録が変わるたびに進む通し番号。リプレイ描画のキャッシュが
    // 古くなったかをこれで判定する。
    unsigned GetRevision() const { return m_revision; }

    // リプレイ用タイムライン＆サンプリング機能
    void BuildReplayTimeline();
    DWORD GetReplayTotalDurationMs() const { return m_totalReplayDurationMs; }
    bool GetReplaySample(DWORD timeMs, const RECT& rPaper, ReplaySample& outSample) const;
    DWORD GetStrokeTimelineStart(size_t strokeIdx) const;
    DWORD GetStrokeTimelineEnd(size_t strokeIdx) const;
    int FindStrokeIndexAtTimeline(DWORD timeMs) const;
    // 指定時刻までに打ち終えている記録点の数。
    // まだ始まっていない画は 0、書き終わった画は全点数。
    size_t GetVisiblePointCount(size_t strokeIdx, DWORD timeMs) const;

    // エクスポート機能 (JSON / CSV)
    bool ExportToJson(const std::wstring& filePath, PaperType paperType, Brush brushType, double hardness) const;
    bool ExportToCsv(const std::wstring& filePath) const;

    // ファイル保存ダイアログ呼び出し。取り消しと失敗を分けて返す
    // （取り消しでは何も表示せず、失敗したときだけ知らせるため）。
    enum class SaveResult { Saved, Canceled, Failed };
    static SaveResult PromptSaveArchiveJson(HWND hWnd, const TrajectorySession& session, PaperType paperType, Brush brushType, double hardness);
    static SaveResult PromptSaveArchiveCsv(HWND hWnd, const TrajectorySession& session);

private:
    std::vector<StrokeData> m_strokes;
    StrokeData m_currentStroke;
    bool m_isRecordingStroke = false;
    DWORD m_sessionStartTime = 0;
    RealtimeMetrics m_realtime;
    unsigned m_revision = 0;

    // リプレイ用内部タイムライン情報
    struct StrokeTimelineInfo {
        DWORD startTimelineMs = 0;
        DWORD endTimelineMs = 0;
    };
    std::vector<StrokeTimelineInfo> m_strokeTimelines;
    DWORD m_totalReplayDurationMs = 0;
};

