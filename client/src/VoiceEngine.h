#pragma once

#include <QObject>
#include <QByteArray>
#include <atomic>
#include <memory>

#ifdef Q_OS_LINUX
#  include <QAudioDevice>
#endif

#ifdef Q_OS_WIN
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#  include <mmsystem.h>
#endif

#ifdef Q_OS_LINUX
class QAudioSource;
class QAudioSink;
class QIODevice;
#endif

class ForjRNoiseReducer;

// ── VoiceEngine ────────────────────────────────────────────────────────────────
// Captures microphone audio via WinMM, does simple energy-based voice-activity
// detection, and plays back incoming PCM audio from remote participants.
//
// Audio format:  16 kHz · Mono · 16-bit PCM (little-endian)
// Frame size:    20 ms  → 320 samples → 640 bytes
class VoiceEngine : public QObject
{
    Q_OBJECT

public:
    static constexpr int kSampleRate    = 16000;
    static constexpr int kChannels      = 1;
    static constexpr int kBitsPerSample = 16;
    static constexpr int kFrameMs       = 20;
    static constexpr int kFrameSamples  = kSampleRate * kFrameMs / 1000;           // 320
    static constexpr int kFrameBytes    = kFrameSamples * kChannels * (kBitsPerSample / 8); // 640

    explicit VoiceEngine(QObject* parent = nullptr);
    ~VoiceEngine() override;

    // inputDeviceIndex / outputDeviceIndex:
    //   0  → WAVE_MAPPER (system default)
    //   1+ → WinMM device index (value - 1)
    bool start(int inputDeviceIndex = 0, int outputDeviceIndex = 0);
    void stop();
    [[nodiscard]] bool isRunning() const { return m_running.load(); }

    void setMuted(bool muted) {
        m_muted = muted;
        // Clear the speaking ring immediately when the user mutes
        if (muted && m_speaking) {
            m_speaking = false;
            m_silenceFrames = 0;
            emit speakingChanged(false);
        }
    }
    void setDeafened(bool deafened) { m_deafened = deafened; }
    [[nodiscard]] bool isMuted()    const { return m_muted; }
    [[nodiscard]] bool isDeafened() const { return m_deafened; }
    void setForjREnabled(bool enabled) { m_forjrEnabled = enabled; }
    [[nodiscard]] bool isForjREnabled() const { return m_forjrEnabled; }

    // Feed one 20-ms PCM frame received from a remote participant into playback.
    void playAudio(int userId, const QByteArray& pcm16leMono);

    // Play a short 440 Hz test tone (blocking-safe: queued to waveOut).
    // outputDeviceIndex follows start(): 0=default, 1+=specific device.
    void playTestTone(int outputDeviceIndex = 0);

signals:
    void audioFrame(const QByteArray& pcm16leMono);  // 20-ms frame — only when not muted
    void speakingChanged(bool speaking);              // VAD state transition
    void inputLevelChanged(float rms0to1);            // emitted every captured frame

private slots:
    void onAudioCaptured(const QByteArray& data);

private:
    std::atomic<bool> m_running{false};
    bool m_muted{false};
    bool m_deafened{false};
    bool m_forjrEnabled{true};
    bool m_speaking{false};

    static constexpr double kVadRmsThreshold = 600.0;
    static constexpr int    kSpeakHoldFrames = 10;
    int m_silenceFrames{0};

#ifdef Q_OS_WIN
    static constexpr int kNumInBufs  = 4;
    static constexpr int kNumOutBufs = 8;

    HWAVEIN  m_waveIn{nullptr};
    HWAVEOUT m_waveOut{nullptr};

    WAVEHDR m_inHdrs[kNumInBufs]{};
    char    m_inBufs[kNumInBufs][kFrameBytes]{};

    struct OutBuf {
        WAVEHDR    hdr{};
        QByteArray data;
    };
    OutBuf m_outPool[kNumOutBufs];

    static void CALLBACK waveInProc(HWAVEIN hwi, UINT msg,
                                    DWORD_PTR instance,
                                    DWORD_PTR param1, DWORD_PTR param2);
    void submitTestToneFrames();
#endif

#ifdef Q_OS_LINUX
    QAudioSource* m_audioIn{nullptr};
    QAudioSink*   m_audioOut{nullptr};
    QIODevice*    m_inputIo{nullptr};
    QIODevice*    m_outputIo{nullptr};
    QAudioDevice  m_selectedInput;
    QAudioDevice  m_selectedOutput;
    QByteArray    m_captureBuffer;
#endif

    std::unique_ptr<ForjRNoiseReducer> m_forjr;

    static double computeRms(const char* buf, int numBytes);
};
