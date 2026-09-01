#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <vector>
#include <string>
#include <cmath>
#include <algorithm>
#include "AppEnums.h"

// キャリブレーションの進行ステップ
enum class CalibrationStep {
    Inactive,           // 未起動（通常状態）
    Step1_Horizontal,   // 1本目: 普段の強さで横線
    Step2_Vertical,     // 2本目: 力強く縦線
    Step3_Free,         // 3本目: のびのびと曲線・斜め線
    Result              // 解析完了・結果提示
};

// 各ストロークの測定データ
struct CalibrationStroke {
    std::vector<double> pressures;
    double maxPressure = 0.0;
    double avgPressure = 0.0;

    void AddPoint(double prs) {
        if (prs > 0.01) {
            pressures.push_back(prs);
            if (prs > maxPressure) {
                maxPressure = prs;
            }
        }
    }

    void Finalize() {
        if (pressures.empty()) {
            avgPressure = 0.0;
            maxPressure = 0.0;
            return;
        }
        double sum = 0.0;
        for (double p : pressures) {
            sum += p;
        }
        avgPressure = sum / static_cast<double>(pressures.size());
    }
};

// キャリブレーション解析結果
struct CalibrationResult {
    double overallAvgPressure = 0.0;    // 全体平均筆圧 (0.0 ~ 1.0)
    double overallMaxPressure = 0.0;    // 全体最大筆圧 (0.0 ~ 1.0)
    double recommendedHardness = 0.50;  // 推奨される筆の硬さ (0.10 ~ 2.00)
    std::wstring pressureProfileName;  // 診断名 (例: "筆圧：軽やか", "筆圧：力強い")
    std::wstring adviceText;           // アドバイス文
};

// キャリブレーション管理モデル
class CalibrationModel {
public:
    CalibrationModel() = default;

    // キャリブレーション開始
    void Start() {
        m_step = CalibrationStep::Step1_Horizontal;
        m_strokes.clear();
        m_currentStroke = CalibrationStroke();
        m_result = CalibrationResult();
        m_currentLivePressure = 0.0;
    }

    // キャリブレーション終了・リセット
    void Reset() {
        m_step = CalibrationStep::Inactive;
        m_strokes.clear();
        m_currentStroke = CalibrationStroke();
        m_result = CalibrationResult();
        m_currentLivePressure = 0.0;
    }

    bool IsActive() const {
        return m_step != CalibrationStep::Inactive;
    }

    bool IsInDrawingStep() const {
        return m_step == CalibrationStep::Step1_Horizontal ||
               m_step == CalibrationStep::Step2_Vertical ||
               m_step == CalibrationStep::Step3_Free;
    }

    bool IsResult() const {
        return m_step == CalibrationStep::Result;
    }

    CalibrationStep GetStep() const {
        return m_step;
    }

    int GetCurrentStepIndex() const {
        switch (m_step) {
        case CalibrationStep::Step1_Horizontal: return 1;
        case CalibrationStep::Step2_Vertical:   return 2;
        case CalibrationStep::Step3_Free:       return 3;
        case CalibrationStep::Result:           return 4;
        default:                                return 0;
        }
    }

    void SetLivePressure(double prs) {
        m_currentLivePressure = prs;
    }

    double GetLivePressure() const {
        return m_currentLivePressure;
    }

    // ペンダウン中の筆圧サンプリング
    void RecordPoint(double prs) {
        if (!IsInDrawingStep()) return;
        m_currentLivePressure = prs;
        m_currentStroke.AddPoint(prs);
    }

    // ペンアップ時のストローク確定とステップ進行
    void OnStrokeEnd() {
        if (!IsInDrawingStep()) return;
        m_currentLivePressure = 0.0;

        // 有効なサンプル数（ノイズクリック等でなく一定長以上引いた場合）
        if (m_currentStroke.pressures.size() >= 5) {
            m_currentStroke.Finalize();
            m_strokes.push_back(m_currentStroke);
            m_currentStroke = CalibrationStroke();

            if (m_step == CalibrationStep::Step1_Horizontal) {
                m_step = CalibrationStep::Step2_Vertical;
            } else if (m_step == CalibrationStep::Step2_Vertical) {
                m_step = CalibrationStep::Step3_Free;
            } else if (m_step == CalibrationStep::Step3_Free) {
                AnalyzeAndCalculate();
                m_step = CalibrationStep::Result;
            }
        } else {
            // サンプルが少なすぎる場合はそのストロークを破棄してやり直させ
            m_currentStroke = CalibrationStroke();
        }
    }

    const CalibrationResult& GetResult() const {
        return m_result;
    }

    const std::vector<CalibrationStroke>& GetStrokes() const {
        return m_strokes;
    }

private:
    // 収集した筆圧データから筆の硬さを自動算出
    void AnalyzeAndCalculate() {
        if (m_strokes.empty()) {
            m_result.overallAvgPressure = 0.40;
            m_result.overallMaxPressure = 0.80;
            m_result.recommendedHardness = 0.50;
            m_result.pressureProfileName = L"標準的な筆圧";
            m_result.adviceText = L"バランスの良い標準的な硬さに設定します。";
            return;
        }

        double totalAvg = 0.0;
        double maxP = 0.0;
        size_t totalPoints = 0;

        for (const auto& s : m_strokes) {
            totalAvg += s.avgPressure;
            if (s.maxPressure > maxP) {
                maxP = s.maxPressure;
            }
            totalPoints += s.pressures.size();
        }
        double avgP = totalAvg / static_cast<double>(m_strokes.size());

        m_result.overallAvgPressure = avgP;
        m_result.overallMaxPressure = maxP;

        // 実効筆圧 P_eff = 65% 平均 + 35% ピーク
        double effectivePrs = 0.65 * avgP + 0.35 * maxP;
        if (effectivePrs < 0.08) effectivePrs = 0.08;
        if (effectivePrs > 0.95) effectivePrs = 0.95;

        // 理想目標係数 0.55 になる硬さ: P_eff ^ hardness = 0.55 => hardness = ln(0.55) / ln(P_eff)
        double target = 0.55;
        double rawHardness = std::log(target) / std::log(effectivePrs);

        // 0.10 〜 2.00 の範囲にクランプし、小数第2位に丸める
        if (rawHardness < 0.10) rawHardness = 0.10;
        if (rawHardness > 2.00) rawHardness = 2.00;
        m_result.recommendedHardness = std::round(rawHardness * 100.0) / 100.0;

        // 筆圧プロファイルの診断
        if (effectivePrs < 0.22) {
            m_result.pressureProfileName = L"軽やか・繊細な筆圧";
            m_result.adviceText = L"弱い筆圧でも豊かな墨が出るよう、筆を柔らかめに補正します。";
        } else if (effectivePrs < 0.38) {
            m_result.pressureProfileName = L"やや柔らかめの筆圧";
            m_result.adviceText = L"自然な運筆でかすれや太さの変化が出やすい感度に補正します。";
        } else if (effectivePrs < 0.55) {
            m_result.pressureProfileName = L"標準・安定した筆圧";
            m_result.adviceText = L"強弱が綺麗に表現できる標準的な感度に設定します。";
        } else if (effectivePrs < 0.72) {
            m_result.pressureProfileName = L"力強い・しっかりした筆圧";
            m_result.adviceText = L"強い筆圧でも線が潰れずダイナミックに書けるよう硬めに補正します。";
        } else {
            m_result.pressureProfileName = L"非常に強力な筆圧";
            m_result.adviceText = L"しっかり押し込んでも線の繊細さを保てるよう、筆を極硬に補正します。";
        }
    }

    CalibrationStep m_step = CalibrationStep::Inactive;
    std::vector<CalibrationStroke> m_strokes;
    CalibrationStroke m_currentStroke;
    CalibrationResult m_result;
    double m_currentLivePressure = 0.0;
};
