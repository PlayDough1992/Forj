#include "ForjRNoiseReducer.h"

#include <QtGlobal>
#include <algorithm>
#include <cmath>

namespace {
constexpr float kHighPassAlpha = 0.995f;
constexpr float kLowPassAlpha = 0.22f;
constexpr float kNoiseLearnAlpha = 0.96f;
constexpr float kMinGain = 0.12f;
constexpr float kGainAttack = 0.55f;
constexpr float kGainRelease = 0.10f;
}

void ForjRNoiseReducer::reset()
{
    m_prevInput = 0.0f;
    m_prevHighPass = 0.0f;
    m_lowPassState = 0.0f;
    m_noiseRms = 140.0f;
    m_gain = 1.0f;
}

QByteArray ForjRNoiseReducer::processFrame(const QByteArray& pcm16leMono)
{
    if (pcm16leMono.isEmpty() || (pcm16leMono.size() % 2) != 0)
        return pcm16leMono;

    QByteArray out = pcm16leMono;
    auto* samples = reinterpret_cast<qint16*>(out.data());
    const int n = out.size() / 2;

    double rmsAccum = 0.0;
    for (int i = 0; i < n; ++i) {
        const float in = static_cast<float>(samples[i]);
        rmsAccum += static_cast<double>(in * in);
    }

    const float frameRms = (n > 0) ? static_cast<float>(std::sqrt(rmsAccum / n)) : 0.0f;
    const bool likelyNoiseOnly = frameRms < (m_noiseRms * 1.35f + 90.0f);
    if (likelyNoiseOnly)
        m_noiseRms = (kNoiseLearnAlpha * m_noiseRms) + ((1.0f - kNoiseLearnAlpha) * frameRms);

    const float speechThreshold = m_noiseRms * 2.2f + 140.0f;
    const bool likelySpeech = frameRms >= speechThreshold;

    float targetGain = 1.0f;
    if (!likelySpeech) {
        const float norm = (frameRms - (m_noiseRms * 1.05f)) / (m_noiseRms * 2.4f + 1.0f);
        targetGain = std::clamp(norm, kMinGain, 1.0f);
    }

    const float smooth = (targetGain > m_gain) ? kGainAttack : kGainRelease;
    m_gain = m_gain + smooth * (targetGain - m_gain);

    const float gateThreshold = m_noiseRms * 1.45f + 120.0f;

    for (int i = 0; i < n; ++i) {
        float x = static_cast<float>(samples[i]);

        // Re-run high-pass on output path to suppress low-frequency hum/rumble.
        const float hp = x - m_prevInput + (kHighPassAlpha * m_prevHighPass);
        m_prevInput = x;
        m_prevHighPass = hp;
        x = hp;

        // Downward expander around the adaptive noise floor.
        const float absX = std::fabs(x);
        if (absX < gateThreshold)
            x *= 0.35f;

        x *= m_gain;

        // Gentle low-pass to tame high-frequency hiss artifacts.
        m_lowPassState += kLowPassAlpha * (x - m_lowPassState);
        const float y = m_lowPassState;

        const int clipped = qBound(-32768, static_cast<int>(std::lrint(y)), 32767);
        samples[i] = static_cast<qint16>(clipped);
    }

    return out;
}
