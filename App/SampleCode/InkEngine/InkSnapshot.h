#pragma once

#include <vector>
#include <cstdint>
#include <cstddef>

// 一画分のインク状態のスナップショット（「一画戻す」用）。
//
// 画面へ出している ARGB バッファ（GpuInk::m_pixelBuffer）は墨量から一意に
// 決まる（CalculateInkPixel）ので持たず、書き戻すときに組み直す。拡散の
// 作業用バッファ（GpuInk::m_deltaInk）も 1 パスごとに作り直されるため不要。
// 残る墨量・水分は半紙のほとんどが白紙で同じ値が続くため RLE で保持する
// （圧縮が効かない場合は生データへ落とす）。
struct InkSnapshot {
    int width = 0;
    int height = 0;
    bool inkCompressed = false;   // false のときは int32 の生データ
    bool wetCompressed = false;   // false のときは uint8 の生データ
    std::vector<uint8_t> ink;     // 墨量 (GpuInk::m_ink)
    std::vector<uint8_t> wet;     // 紙の水分 (GpuInk::m_wetField)

    bool IsEmpty() const { return width <= 0 || height <= 0; }

    size_t ByteSize() const { return ink.size() + wet.size(); }
};
