#include "stdafx.h"
#include "Brushwork.h"
#include <algorithm>
#include <cmath>

namespace {

// ===========================================================================
// 較正の対象。ここだけを触れば運筆の点数の出方が変わる。
// ===========================================================================

// --- 筆の立て方 -------------------------------------------------------
// 高度角（0°=紙に平ら、90°=垂直）がこれを下回ったら「寝かせすぎ」と見なす。
// 45°にしないのは、実測のペン角がもともと寝ているため。BrushModel の
// TILT_WIDTH_COMPENSATION のところにある通り、Cintiq 16 で測った仰角の
// 中央値は 41°（tiltFactor 0.54）だった。45°で切ると普通に持っている
// 状態がまるごと減点対象になってしまう。
constexpr double kLyingAltitudeDeg = 30.0;

// 画のうちこれだけの割合が寝ていたら 0 点。
constexpr double kLyingRatioTolerance = 0.30;

// 高度角が全点これ以下なら、傾きを返さないタブレットと判断して測らない。
// 傾き非対応の環境では pkOrientation が 0 のまま届き、高度角 0°（＝完全に
// 寝かせている）と区別がつかないため、寝かせすぎの判定自体を止める。
constexpr double kTiltDeadDeg = 0.5;

// --- 速さの安定 -------------------------------------------------------
// 送筆のばらつき（標準偏差 / 平均）。これ以下なら 100 点、これ以上なら 0 点。
constexpr double kSpeedCvFloor = 0.35;
constexpr double kSpeedCvCeil  = 1.20;

// 1 パケットぶんの移動量で出した速度はサンプリング間隔の揺れを拾って跳ねる。
// この窓の移動量で見直して、運筆そのものの速さの揺れだけを残す。
constexpr double kSpeedWindowMs = 30.0;

// 起筆と収筆では筆が止まるのが正しいので、ばらつきの計算からは端を落とす。
// 落とすのは前後それぞれこの割合。
constexpr double kSpeedTrimRatio = 0.15;

// ばらつきを測るのに必要な点数（端を落としたあとの数）
constexpr int kSpeedMinPoints = 8;

// --- 抑揚 -------------------------------------------------------------
// 筆圧の上下の幅 (上位 - 下位) / 上位。これ以下なら 0 点（棒のような線）、
// これ以上で 100 点。書道では入筆・送筆・終筆で太さが変わるのが普通なので、
// 抑揚は大きいほうを良しとする。暴れている場合は上の「速さの安定」側で
// 減点されるため、ここに上限を設けて抑える必要はない。
constexpr double kSwingFloor = 0.15;
constexpr double kSwingCeil  = 0.45;

// 抑揚を測るときに端の外れ値を避けるための百分位
constexpr double kSwingLowPercentile  = 0.10;
constexpr double kSwingHighPercentile = 0.90;

// 抑揚を測るのに必要な点数
constexpr int kSwingMinPoints = 5;

// --- テンポ -----------------------------------------------------------
// 画の所要時間。この範囲なら 100 点、外側の境界で 0 点まで落ちる。
constexpr double kDurationGoodMinMs = 400.0;
constexpr double kDurationGoodMaxMs = 1800.0;
constexpr double kDurationFastMs    = 120.0;   // これ以下は走り書き
constexpr double kDurationSlowMs    = 4000.0;  // これ以上は筆が止まりすぎ

// 画間（前の画を離してから次を置くまで）。この範囲なら 100 点。
constexpr double kGapGoodMinMs = 150.0;
constexpr double kGapGoodMaxMs = 1200.0;
constexpr double kGapFastMs    = 0.0;
constexpr double kGapSlowMs    = 4000.0;

// 画間として信じる上限。これを超えた値は計測漏れ（途中で他の操作を挟んだ等）として捨てる。
constexpr double kGapValidMaxMs = 30000.0;

// テンポの中での所要時間と画間の配分
constexpr double kTempoDurationWeight = 0.6;
constexpr double kTempoGapWeight      = 0.4;

// --- 軸の重み ---------------------------------------------------------
// 合計 1.0。測れなかった軸があるときは、残った軸の重みで割り直す。
constexpr double kWeight[WORK_AXIS_COUNT] = {
    0.25, // 筆の立て方
    0.30, // 速さの安定
    0.25, // 抑揚
    0.20  // テンポ
};

// ===========================================================================

double Clamp01(double v) { return v < 0.0 ? 0.0 : (v > 1.0 ? 1.0 : v); }

// lo〜hi を 0〜1 へ直線で写す
double MapRange(double v, double lo, double hi) {
    if (hi <= lo) return 0.0;
    return Clamp01((v - lo) / (hi - lo));
}

// lo〜hi の内側なら 1.0、outLo / outHi まで離れると 0.0 へ落ちる台形
double MapBand(double v, double outLo, double lo, double hi, double outHi) {
    if (v < lo) return MapRange(v, outLo, lo);
    if (v > hi) return 1.0 - MapRange(v, hi, outHi);
    return 1.0;
}

// 昇順に並んだ列の百分位
double Percentile(const std::vector<double>& sorted, double p) {
    if (sorted.empty()) return 0.0;
    double idx = p * (double)(sorted.size() - 1);
    size_t lo = (size_t)std::floor(idx);
    size_t hi = (size_t)std::ceil(idx);
    if (hi >= sorted.size()) hi = sorted.size() - 1;
    double t = idx - (double)lo;
    return sorted[lo] * (1.0 - t) + sorted[hi] * t;
}

// 1パケットぶんではなく kSpeedWindowMs ぶんの移動量から速度を作り直す
std::vector<double> BuildSpeedSeries(const std::vector<StrokePoint>& pts) {
    std::vector<double> speeds;
    const size_t n = pts.size();
    if (n < 2) return speeds;

    // 通った道のりの累積
    std::vector<double> cum(n, 0.0);
    for (size_t i = 1; i < n; ++i) {
        double dx = (double)(pts[i].paperX - pts[i - 1].paperX);
        double dy = (double)(pts[i].paperY - pts[i - 1].paperY);
        cum[i] = cum[i - 1] + std::sqrt(dx * dx + dy * dy);
    }

    speeds.reserve(n - 1);
    size_t j = 0;
    for (size_t i = 1; i < n; ++i) {
        // 窓の下端を、i から kSpeedWindowMs 以上離れた最も近い点まで進める
        while (j + 1 < i && (double)pts[i].timeMs - (double)pts[j + 1].timeMs >= kSpeedWindowMs) {
            ++j;
        }
        double dt = (double)pts[i].timeMs - (double)pts[j].timeMs;
        if (dt <= 0.0) continue;
        speeds.push_back((cum[i] - cum[j]) / dt * 1000.0);
    }
    return speeds;
}

// 測れた軸だけの重み付き平均
double WeightedTotal(const WorkAxisScore (&axis)[WORK_AXIS_COUNT], bool& outScored) {
    double sum = 0.0;
    double weight = 0.0;
    for (int i = 0; i < WORK_AXIS_COUNT; ++i) {
        if (!axis[i].valid) continue;
        sum += axis[i].score * kWeight[i];
        weight += kWeight[i];
    }
    outScored = (weight > 0.0);
    return outScored ? sum / weight : 0.0;
}

} // namespace

double Brushwork::LyingAltitudeDeg() { return kLyingAltitudeDeg; }

StrokeWork Brushwork::MeasureStroke(const StrokeData& stroke, double gapBeforeMs) {
    StrokeWork w;
    w.strokeId = stroke.strokeId;
    w.pointCount = (int)stroke.points.size();
    w.gapBeforeMs = gapBeforeMs;

    const std::vector<StrokePoint>& pts = stroke.points;
    if (pts.empty()) return w;

    // 所要時間は点の相対時刻の差から採る。StrokeData::endTime は GetTickCount、
    // startTime はペンのパケット時刻なので、その差を取ると時計が混ざる。
    w.durationMs = (double)pts.back().timeMs - (double)pts.front().timeMs;
    if (w.durationMs < 0.0) w.durationMs = 0.0;

    // --- 筆の立て方 ---------------------------------------------------
    double altSum = 0.0;
    double altMax = 0.0;
    double altMin = 90.0;
    int lyingCount = 0;
    for (const StrokePoint& p : pts) {
        altSum += p.altitudeDeg;
        if (p.altitudeDeg > altMax) altMax = p.altitudeDeg;
        if (p.altitudeDeg < altMin) altMin = p.altitudeDeg;
        if (p.altitudeDeg < kLyingAltitudeDeg) ++lyingCount;
    }
    w.tiltValid = (altMax > kTiltDeadDeg);
    if (w.tiltValid) {
        w.altitudeMeanDeg = altSum / (double)pts.size();
        w.altitudeMinDeg = altMin;
        w.lyingRatio = (double)lyingCount / (double)pts.size();
        w.lyingMs = w.durationMs * w.lyingRatio;

        w.axis[(int)WorkAxis::Tilt].valid = true;
        w.axis[(int)WorkAxis::Tilt].score =
            (1.0 - Clamp01(w.lyingRatio / kLyingRatioTolerance)) * 100.0;
    }

    // --- 道のりの長さ。点か線かの手がかりとして速度とは別に持つ -------
    for (size_t i = 1; i < pts.size(); ++i) {
        double dx = (double)(pts[i].paperX - pts[i - 1].paperX);
        double dy = (double)(pts[i].paperY - pts[i - 1].paperY);
        w.pathLengthPx += std::sqrt(dx * dx + dy * dy);
    }

    // --- 速さの安定 ---------------------------------------------------
    std::vector<double> speeds = BuildSpeedSeries(pts);
    if (!speeds.empty()) {
        // 起筆・収筆で筆が止まるのは正しい運筆なので、端を落としてから見る
        size_t trim = (size_t)(speeds.size() * kSpeedTrimRatio);
        size_t begin = trim;
        size_t end = speeds.size() - trim;
        if (end <= begin || (int)(end - begin) < kSpeedMinPoints) {
            begin = 0;
            end = speeds.size();
        }
        if ((int)(end - begin) >= kSpeedMinPoints) {
            double sum = 0.0;
            for (size_t i = begin; i < end; ++i) sum += speeds[i];
            double mean = sum / (double)(end - begin);
            if (mean > 1.0) { // 止まったままの画は速さを語れない
                double var = 0.0;
                for (size_t i = begin; i < end; ++i) {
                    double d = speeds[i] - mean;
                    var += d * d;
                }
                var /= (double)(end - begin);
                w.speedValid = true;
                w.speedMean = mean;
                w.speedCv = std::sqrt(var) / mean;

                w.axis[(int)WorkAxis::Speed].valid = true;
                w.axis[(int)WorkAxis::Speed].score =
                    (1.0 - MapRange(w.speedCv, kSpeedCvFloor, kSpeedCvCeil)) * 100.0;
            }
        }
    }

    // --- 抑揚 ---------------------------------------------------------
    if ((int)pts.size() >= kSwingMinPoints) {
        std::vector<double> prs, wid;
        prs.reserve(pts.size());
        wid.reserve(pts.size());
        double prsSum = 0.0;
        for (const StrokePoint& p : pts) {
            prs.push_back(p.pressure);
            wid.push_back(p.width);
            prsSum += p.pressure;
        }
        std::sort(prs.begin(), prs.end());
        std::sort(wid.begin(), wid.end());

        double pLow = Percentile(prs, kSwingLowPercentile);
        double pHigh = Percentile(prs, kSwingHighPercentile);
        w.pressureMean = prsSum / (double)pts.size();
        w.widthMinPx = Percentile(wid, kSwingLowPercentile);
        w.widthMaxPx = Percentile(wid, kSwingHighPercentile);
        if (w.widthMaxPx > 0.01) {
            w.widthSwing = (w.widthMaxPx - w.widthMinPx) / w.widthMaxPx;
        }

        if (pHigh > 0.01) {
            w.swingValid = true;
            w.pressureSwing = (pHigh - pLow) / pHigh;

            w.axis[(int)WorkAxis::Swing].valid = true;
            w.axis[(int)WorkAxis::Swing].score =
                MapRange(w.pressureSwing, kSwingFloor, kSwingCeil) * 100.0;
        }
    }

    // --- テンポ -------------------------------------------------------
    // 所要時間は必ず測れる。画間は最初の画や、計測が怪しいときだけ欠ける。
    {
        double durScore = MapBand(w.durationMs, kDurationFastMs,
                                  kDurationGoodMinMs, kDurationGoodMaxMs, kDurationSlowMs) * 100.0;
        double score = durScore;
        if (w.gapBeforeMs >= 0.0) {
            double gapScore = MapBand(w.gapBeforeMs, kGapFastMs,
                                      kGapGoodMinMs, kGapGoodMaxMs, kGapSlowMs) * 100.0;
            score = durScore * kTempoDurationWeight + gapScore * kTempoGapWeight;
        }
        w.axis[(int)WorkAxis::Tempo].valid = true;
        w.axis[(int)WorkAxis::Tempo].score = score;
    }

    w.totalScore = WeightedTotal(w.axis, w.scored);
    return w;
}

BrushworkResult Brushwork::Measure(const TrajectorySession& session) {
    BrushworkResult r;

    const std::vector<StrokeData>& strokes = session.GetStrokes();
    if (strokes.empty()) {
        r.message = L"まだ何も書かれていません。\n半紙に字を書くと、お手本なしで書きぶりを測ります";
        return r;
    }

    r.strokes.reserve(strokes.size());
    for (size_t i = 0; i < strokes.size(); ++i) {
        // 画間。双方ともペンのパケット時刻なので時計は揃っている。
        // 前の画の終わりは startTime + 最後の点の相対時刻で出す
        // （StrokeData::endTime は GetTickCount なので混ぜられない）。
        double gap = -1.0;
        if (i > 0) {
            const StrokeData& prev = strokes[i - 1];
            if (!prev.points.empty()) {
                double prevEnd = (double)prev.startTime + (double)prev.points.back().timeMs;
                double d = (double)strokes[i].startTime - prevEnd;
                if (d >= 0.0 && d <= kGapValidMaxMs) gap = d;
            }
        }
        r.strokes.push_back(MeasureStroke(strokes[i], gap));
    }

    r.strokeCount = (int)r.strokes.size();

    // --- 全体へまとめる。画ごとの値の平均を採る ---------------------
    double axisSum[WORK_AXIS_COUNT] = { 0.0, 0.0, 0.0, 0.0 };
    int axisCount[WORK_AXIS_COUNT] = { 0, 0, 0, 0 };

    double altSum = 0.0, lyingSum = 0.0;
    int tiltN = 0;
    double spdSum = 0.0, cvSum = 0.0;
    int spdN = 0;
    double prsSwingSum = 0.0, widSwingSum = 0.0;
    int swingN = 0;
    double durSum = 0.0;
    double gapSum = 0.0;
    int gapN = 0;
    double totalSum = 0.0;
    int totalN = 0;

    for (const StrokeWork& w : r.strokes) {
        for (int a = 0; a < WORK_AXIS_COUNT; ++a) {
            if (!w.axis[a].valid) continue;
            axisSum[a] += w.axis[a].score;
            ++axisCount[a];
        }
        if (w.tiltValid) { altSum += w.altitudeMeanDeg; lyingSum += w.lyingRatio; ++tiltN; }
        if (w.speedValid) { spdSum += w.speedMean; cvSum += w.speedCv; ++spdN; }
        if (w.swingValid) { prsSwingSum += w.pressureSwing; widSwingSum += w.widthSwing; ++swingN; }
        durSum += w.durationMs;
        if (w.gapBeforeMs >= 0.0) { gapSum += w.gapBeforeMs; ++gapN; }
        if (w.scored) { totalSum += w.totalScore; ++totalN; }
    }

    for (int a = 0; a < WORK_AXIS_COUNT; ++a) {
        if (axisCount[a] <= 0) continue;
        r.axis[a].valid = true;
        r.axis[a].score = axisSum[a] / (double)axisCount[a];
    }

    r.tiltValid = (tiltN > 0);
    if (tiltN > 0) {
        r.altitudeMeanDeg = altSum / (double)tiltN;
        r.lyingRatio = lyingSum / (double)tiltN;
    }
    if (spdN > 0) {
        r.speedMean = spdSum / (double)spdN;
        r.speedCv = cvSum / (double)spdN;
    }
    if (swingN > 0) {
        r.pressureSwing = prsSwingSum / (double)swingN;
        r.widthSwing = widSwingSum / (double)swingN;
    }
    r.avgDurationMs = durSum / (double)r.strokes.size();
    r.avgGapMs = (gapN > 0) ? gapSum / (double)gapN : -1.0;

    r.scoredStrokes = totalN;
    r.totalScore = (totalN > 0) ? totalSum / (double)totalN : 0.0;
    r.valid = (totalN > 0);
    if (!r.valid) {
        r.message = L"書きぶりを測れる画がありません。\n点だけ、または記録が短すぎる画のようです";
    }
    return r;
}

const wchar_t* Brushwork::GradeLabel(double score) {
    if (score >= 90.0) return L"とても良い";
    if (score >= 75.0) return L"良い";
    if (score >= 60.0) return L"もう少し";
    if (score >= 40.0) return L"練習しよう";
    return L"崩れています";
}
