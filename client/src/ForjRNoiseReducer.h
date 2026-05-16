#pragma once

#include <QByteArray>

// ForjR: lightweight real-time mic denoiser for 16 kHz mono 16-bit PCM frames.
// Pipeline: high-pass filter -> adaptive noise gate/expander -> soft low-pass smoothing.
class ForjRNoiseReducer
{
public:
    ForjRNoiseReducer() = default;

    void reset();
    QByteArray processFrame(const QByteArray& pcm16leMono);

private:
    float m_prevInput{0.0f};
    float m_prevHighPass{0.0f};
    float m_lowPassState{0.0f};
    float m_noiseRms{140.0f};
    float m_gain{1.0f};
};
