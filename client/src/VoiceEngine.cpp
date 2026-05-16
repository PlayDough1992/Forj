#include "VoiceEngine.h"

#include <QMetaObject>
#include <QTimer>
#ifdef Q_OS_LINUX
#  include <QAudioFormat>
#  include <QAudioSource>
#  include <QAudioSink>
#  include <QMediaDevices>
#  include <QIODevice>
#endif
#include <cmath>
#include <cstring>

// ── Win32 implementation ───────────────────────────────────────────────────────

double VoiceEngine::computeRms(const char* buf, int numBytes)
{
    const int n = numBytes / 2;
    if (n <= 0) return 0.0;
    const auto* s = reinterpret_cast<const qint16*>(buf);
    double sum = 0.0;
    for (int i = 0; i < n; ++i) {
        const double v = s[i];
        sum += v * v;
    }
    return std::sqrt(sum / n);
}

#ifdef Q_OS_WIN

void CALLBACK VoiceEngine::waveInProc(HWAVEIN hwi, UINT msg,
                                      DWORD_PTR instance,
                                      DWORD_PTR param1, DWORD_PTR /*param2*/)
{
    if (msg != WIM_DATA) return;
    auto* self = reinterpret_cast<VoiceEngine*>(instance);
    auto* hdr  = reinterpret_cast<WAVEHDR*>(param1);
    // Guard: if stopped, do NOT re-queue — prevents freeze/deadlock on waveInReset
    if (!self->m_running.load()) return;

    if (hdr->dwBytesRecorded > 0) {
        QByteArray data(hdr->lpData, static_cast<int>(hdr->dwBytesRecorded));
        QMetaObject::invokeMethod(self, "onAudioCaptured",
                                  Qt::QueuedConnection,
                                  Q_ARG(QByteArray, data));
    }
    waveInAddBuffer(hwi, hdr, sizeof(WAVEHDR));
}

#endif  // Q_OS_WIN

// ── VoiceEngine ────────────────────────────────────────────────────────────────

VoiceEngine::VoiceEngine(QObject* parent) : QObject(parent) {}

VoiceEngine::~VoiceEngine() { stop(); }

bool VoiceEngine::start(int inputDeviceIndex, int outputDeviceIndex)
{
#ifdef Q_OS_WIN
    WAVEFORMATEX fmt{};
    fmt.wFormatTag      = WAVE_FORMAT_PCM;
    fmt.nChannels       = kChannels;
    fmt.nSamplesPerSec  = kSampleRate;
    fmt.wBitsPerSample  = kBitsPerSample;
    fmt.nBlockAlign     = kChannels * (kBitsPerSample / 8);
    fmt.nAvgBytesPerSec = kSampleRate * fmt.nBlockAlign;

    const UINT inDev  = inputDeviceIndex  <= 0 ? WAVE_MAPPER
                                               : static_cast<UINT>(inputDeviceIndex  - 1);
    const UINT outDev = outputDeviceIndex <= 0 ? WAVE_MAPPER
                                               : static_cast<UINT>(outputDeviceIndex - 1);

    if (waveInOpen(&m_waveIn, inDev, &fmt,
                   reinterpret_cast<DWORD_PTR>(&waveInProc),
                   reinterpret_cast<DWORD_PTR>(this),
                   CALLBACK_FUNCTION) != MMSYSERR_NOERROR)
        return false;

    if (waveOutOpen(&m_waveOut, outDev, &fmt, 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR) {
        waveInClose(m_waveIn);
        m_waveIn = nullptr;
        return false;
    }

    for (int i = 0; i < kNumInBufs; ++i) {
        std::memset(&m_inHdrs[i], 0, sizeof(WAVEHDR));
        m_inHdrs[i].lpData         = m_inBufs[i];
        m_inHdrs[i].dwBufferLength = kFrameBytes;
        waveInPrepareHeader(m_waveIn, &m_inHdrs[i], sizeof(WAVEHDR));
        waveInAddBuffer   (m_waveIn, &m_inHdrs[i], sizeof(WAVEHDR));
    }

    // Set m_running BEFORE waveInStart so callback never sees it as false mid-stream
    m_running.store(true);
    waveInStart(m_waveIn);
    return true;
#elif defined(Q_OS_LINUX)
    const auto inputs = QMediaDevices::audioInputs();
    const auto outputs = QMediaDevices::audioOutputs();

    QAudioFormat fmt;
    fmt.setSampleRate(kSampleRate);
    fmt.setChannelCount(kChannels);
    fmt.setSampleFormat(QAudioFormat::Int16);

    const QAudioDevice defaultIn = QMediaDevices::defaultAudioInput();
    const QAudioDevice defaultOut = QMediaDevices::defaultAudioOutput();

    m_selectedInput = defaultIn;
    m_selectedOutput = defaultOut;

    if (inputDeviceIndex > 0 && (inputDeviceIndex - 1) < inputs.size())
        m_selectedInput = inputs[inputDeviceIndex - 1];
    if (outputDeviceIndex > 0 && (outputDeviceIndex - 1) < outputs.size())
        m_selectedOutput = outputs[outputDeviceIndex - 1];

    if (!m_selectedInput.isNull() && !m_selectedInput.isFormatSupported(fmt))
        fmt = m_selectedInput.preferredFormat();

    m_audioIn = new QAudioSource(m_selectedInput, fmt, this);
    m_audioOut = new QAudioSink(m_selectedOutput, fmt, this);
    m_outputIo = m_audioOut->start();
    m_inputIo = m_audioIn->start();

    if (!m_inputIo || !m_outputIo) {
        stop();
        return false;
    }

    m_running.store(true);
    connect(m_inputIo, &QIODevice::readyRead, this, [this] {
        if (!m_running.load() || !m_inputIo) return;
        m_captureBuffer.append(m_inputIo->readAll());
        while (m_captureBuffer.size() >= kFrameBytes) {
            const QByteArray frame = m_captureBuffer.left(kFrameBytes);
            m_captureBuffer.remove(0, kFrameBytes);
            onAudioCaptured(frame);
        }
    });
    return true;
#else
    Q_UNUSED(inputDeviceIndex)
    Q_UNUSED(outputDeviceIndex)
    return false;
#endif
}

void VoiceEngine::stop()
{
    // Set false BEFORE waveInReset so the callback stops re-queuing (prevents UI freeze)
    m_running.store(false);

#ifdef Q_OS_WIN
    if (m_waveIn) {
        waveInStop(m_waveIn);
        waveInReset(m_waveIn);
        for (int i = 0; i < kNumInBufs; ++i)
            waveInUnprepareHeader(m_waveIn, &m_inHdrs[i], sizeof(WAVEHDR));
        waveInClose(m_waveIn);
        m_waveIn = nullptr;
    }
    if (m_waveOut) {
        waveOutReset(m_waveOut);
        for (int i = 0; i < kNumOutBufs; ++i) {
            auto& ob = m_outPool[i];
            if (ob.hdr.dwFlags & WHDR_PREPARED)
                waveOutUnprepareHeader(m_waveOut, &ob.hdr, sizeof(WAVEHDR));
        }
        waveOutClose(m_waveOut);
        m_waveOut = nullptr;
    }
#elif defined(Q_OS_LINUX)
    if (m_audioIn) {
        m_audioIn->stop();
        m_audioIn->deleteLater();
        m_audioIn = nullptr;
    }
    if (m_audioOut) {
        m_audioOut->stop();
        m_audioOut->deleteLater();
        m_audioOut = nullptr;
    }
    m_inputIo = nullptr;
    m_outputIo = nullptr;
    m_captureBuffer.clear();
#endif

    if (m_speaking) {
        m_speaking = false;
        emit speakingChanged(false);
    }
}

void VoiceEngine::playAudio(int /*userId*/, const QByteArray& pcm)
{
    if (m_deafened) return;   // deafened: don't play incoming audio
#ifdef Q_OS_WIN
    if (!m_waveOut || pcm.isEmpty()) return;

    for (int i = 0; i < kNumOutBufs; ++i) {
        auto& ob = m_outPool[i];
        // Skip buffers still being played by the driver
        if (ob.hdr.dwFlags & WHDR_INQUEUE) continue;

        if (ob.hdr.dwFlags & WHDR_PREPARED)
            waveOutUnprepareHeader(m_waveOut, &ob.hdr, sizeof(WAVEHDR));

        ob.data = pcm;
        std::memset(&ob.hdr, 0, sizeof(WAVEHDR));
        ob.hdr.lpData         = ob.data.data();
        ob.hdr.dwBufferLength = static_cast<DWORD>(ob.data.size());

        if (waveOutPrepareHeader(m_waveOut, &ob.hdr, sizeof(WAVEHDR)) == MMSYSERR_NOERROR)
            waveOutWrite(m_waveOut, &ob.hdr, sizeof(WAVEHDR));
        return;
    }
    // All buffers busy — drop this frame (expected under load; voice is loss-tolerant)
#elif defined(Q_OS_LINUX)
    if (!m_outputIo || pcm.isEmpty()) return;
    m_outputIo->write(pcm);
#else
    Q_UNUSED(pcm)
#endif
}

void VoiceEngine::playTestTone()
{
#ifdef Q_OS_WIN
    if (!m_waveOut) {
        WAVEFORMATEX fmt{};
        fmt.wFormatTag      = WAVE_FORMAT_PCM;
        fmt.nChannels       = kChannels;
        fmt.nSamplesPerSec  = kSampleRate;
        fmt.wBitsPerSample  = kBitsPerSample;
        fmt.nBlockAlign     = kChannels * (kBitsPerSample / 8);
        fmt.nAvgBytesPerSec = kSampleRate * fmt.nBlockAlign;
        if (waveOutOpen(&m_waveOut, WAVE_MAPPER, &fmt, 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR)
            return;
        submitTestToneFrames();
        // Close after buffers drain (~600 ms)
        QTimer::singleShot(600, this, [this] {
            if (m_waveOut && !m_running.load()) {
                waveOutReset(m_waveOut);
                for (int i = 0; i < kNumOutBufs; ++i) {
                    auto& ob = m_outPool[i];
                    if (ob.hdr.dwFlags & WHDR_PREPARED)
                        waveOutUnprepareHeader(m_waveOut, &ob.hdr, sizeof(WAVEHDR));
                }
                waveOutClose(m_waveOut);
                m_waveOut = nullptr;
            }
        });
    } else {
        submitTestToneFrames();
    }
#elif defined(Q_OS_LINUX)
    if (!m_audioOut || !m_outputIo) {
        QAudioFormat fmt;
        fmt.setSampleRate(kSampleRate);
        fmt.setChannelCount(kChannels);
        fmt.setSampleFormat(QAudioFormat::Int16);

        const QAudioDevice outDev = QMediaDevices::defaultAudioOutput();
        if (outDev.isNull()) return;

        m_audioOut = new QAudioSink(outDev, fmt, this);
        m_outputIo = m_audioOut->start();
        if (!m_outputIo) {
            m_audioOut->deleteLater();
            m_audioOut = nullptr;
            return;
        }

        QTimer::singleShot(700, this, [this] {
            if (!m_running.load() && m_audioOut) {
                m_audioOut->stop();
                m_audioOut->deleteLater();
                m_audioOut = nullptr;
                m_outputIo = nullptr;
            }
        });
    }

    QByteArray tone;
    tone.resize((kSampleRate / 2) * 2);
    auto* samples = reinterpret_cast<qint16*>(tone.data());
    const int sampleCount = tone.size() / 2;
    for (int i = 0; i < sampleCount; ++i) {
        const double t = static_cast<double>(i) / kSampleRate;
        samples[i] = static_cast<qint16>(20000.0 * std::sin(2.0 * M_PI * 440.0 * t));
    }
    m_outputIo->write(tone);
#endif
}

#ifdef Q_OS_WIN
void VoiceEngine::submitTestToneFrames()
{
    if (!m_waveOut) return;
    constexpr int totalFrames = 500 / kFrameMs;  // 25 frames = 500 ms
    for (int f = 0; f < totalFrames; ++f) {
        for (int i = 0; i < kNumOutBufs; ++i) {
            auto& ob = m_outPool[i];
            if (ob.hdr.dwFlags & WHDR_INQUEUE) continue;
            if (ob.hdr.dwFlags & WHDR_PREPARED)
                waveOutUnprepareHeader(m_waveOut, &ob.hdr, sizeof(WAVEHDR));
            ob.data.resize(kFrameBytes);
            auto* samples = reinterpret_cast<qint16*>(ob.data.data());
            const int offset = f * kFrameSamples;
            for (int s = 0; s < kFrameSamples; ++s) {
                const double t = static_cast<double>(offset + s) / kSampleRate;
                samples[s] = static_cast<qint16>(20000.0 * std::sin(2.0 * M_PI * 440.0 * t));
            }
            std::memset(&ob.hdr, 0, sizeof(WAVEHDR));
            ob.hdr.lpData         = ob.data.data();
            ob.hdr.dwBufferLength = static_cast<DWORD>(ob.data.size());
            if (waveOutPrepareHeader(m_waveOut, &ob.hdr, sizeof(WAVEHDR)) == MMSYSERR_NOERROR)
                waveOutWrite(m_waveOut, &ob.hdr, sizeof(WAVEHDR));
            break;
        }
    }
}
#endif

void VoiceEngine::onAudioCaptured(const QByteArray& data)
{
    if (!m_running.load()) return;

    // Always compute level even when muted so the settings meter still works
    const double rms = computeRms(data.constData(), data.size());
    emit inputLevelChanged(static_cast<float>(qMin(rms / 5000.0, 1.0)));

    if (rms >= kVadRmsThreshold) {
        m_silenceFrames = 0;
        if (!m_speaking && !m_muted) {   // don't show ring while muted
            m_speaking = true;
            emit speakingChanged(true);
        }
    } else if (m_speaking) {
        if (++m_silenceFrames >= kSpeakHoldFrames) {
            m_speaking      = false;
            m_silenceFrames = 0;
            emit speakingChanged(false);
        }
    }

    if (!m_muted)
        emit audioFrame(data);   // only transmit when not muted
}
