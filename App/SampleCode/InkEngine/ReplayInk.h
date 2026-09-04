#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <memory>
#include <vector>

#include "GpuInk.h"
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
    // 時刻が進んだときは差分のセグメントを描き足すだけ。巻き戻したとき、記録が
    // 変わったとき、半紙の大きさが変わったときは白紙から流し直す。
    // （墨は画素へ破壊的に積み上がるので、戻すには引き直すしかない）
    bool Update(const TrajectorySession& session, DWORD timeMs, int paperW, int paperH);

    // 半紙の位置へ墨を転送する。GpuInk の墨テクスチャは不透明なので、
    // 下敷きやゴースト筆跡はこの後に重ねる。
    void Render(HDC dc, int destX, int destY);

    void Release();

private:
    void FeedForward(const TrajectorySession& session, DWORD timeMs);

    std::unique_ptr<GpuInk> m_ink;
    unsigned m_revision = 0;            // 流し込み済みの記録リビジョン
    int m_paperW = 0;
    int m_paperH = 0;
    DWORD m_timeMs = 0;                 // 流し込み済みの時刻
    bool m_ready = false;
    std::vector<size_t> m_fedCount;     // 画ごとの、流し込み済み記録点数
    int m_openStroke = -1;              // GpuInk へ投入中の画。切り替わる前に EndStroke する
};
