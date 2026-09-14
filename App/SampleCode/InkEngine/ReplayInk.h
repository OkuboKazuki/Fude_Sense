#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <memory>
#include <vector>

#include "GpuInk.h"
#include "InkSnapshot.h"
#include "TrajectoryModel.h"

// リプレイ専用の墨。
//
// 解析タブの再生は長らく GDI の円と太線で描いていたため、実際に書いた作品とは
// 別物に見えていた。ここでは記録した運筆を、実際の描画とまったく同じ GpuInk へ
// 流し込む。にじみ・かすれ・線幅の抑揚は GpuInk 側の物理計算がそのまま効く。
//
// 記録には「描画時に GpuInk へ渡した値」（線幅・筆圧補正値・乾き具合・運筆方向）が
// 入っているので、筆や硬さの設定を後から変えても再生の見た目は変わらない。
class ReplayInk {
public:
    ~ReplayInk();

    // timeMs 時点までの墨を用意する。
    // 時刻が進んだときは差分のセグメントを描き足すだけ。巻き戻したときは、
    // 手前の控え（チェックポイント）まで書き戻してから、そこだけ引き直す。
    // scrubbing はシークバー等をドラッグ中かどうか。ドラッグ中は毎フレーム
    // 引き直しが走るので、重い処理（にじみのまとめ進めと控えの取得）を省く。
    bool Update(const TrajectorySession& session, DWORD timeMs, int paperW, int paperH, bool scrubbing);

    // 半紙の位置へ墨を転送する。GpuInk の墨テクスチャは不透明なので、
    // 下敷きやゴースト筆跡はこの後に重ねる。
    void Render(HDC dc, int destX, int destY);

    void Release();

private:
    // 画の切れ目で控えた墨の状態。巻き戻しはここから引き直す。
    struct Checkpoint {
        DWORD timeMs = 0;                // その画が始まる時刻（タイムライン上）
        std::vector<size_t> fedCount;    // その時点の流し込み済み点数
        InkSnapshot snap;
    };

    void FeedForward(const TrajectorySession& session, DWORD timeMs, bool skipGapDiffusion, bool allowCapture);
    void AdvanceDiffusion(int steps);
    // 画の手前の空中移動で、足りていないにじみの段数
    int GapDiffusionSteps(const TrajectorySession& session, size_t strokeIdx) const;

    // 巻き戻し先に使える控えを書き戻す。使えなければ false（白紙から引き直す）
    bool RestoreNearest(DWORD timeMs);
    void CaptureCheckpoint(DWORD timeMs);
    void ClearCheckpoints();

    std::unique_ptr<GpuInk> m_ink;
    unsigned m_revision = 0;            // 流し込み済みの記録リビジョン
    int m_paperW = 0;
    int m_paperH = 0;
    DWORD m_timeMs = 0;                 // 流し込み済みの時刻
    bool m_ready = false;
    std::vector<size_t> m_fedCount;     // 画ごとの、流し込み済み記録点数
    int m_openStroke = -1;              // GpuInk へ投入中の画。切り替わる前に EndStroke する

    std::vector<Checkpoint> m_checkpoints;   // 時刻の昇順
    size_t m_checkpointBytes = 0;
    DWORD m_checkpointIntervalMs = 0;
    bool m_wasScrubbing = false;
    bool m_settled = false;
};
