#include "AudioSpectrumAnalyzer.h"
#include <QPointer>
#include <QThread>
#include <QtConcurrent/QtConcurrentRun>
#include <algorithm>
#include <cmath>
#include <complex>

namespace {
constexpr int fftSize = 4096;
constexpr float pi = 3.14159265358979323846f;
}
AudioSpectrumAnalyzer::AudioSpectrumAnalyzer(QObject *parent)
    : AudioSpectrumAnalyzer([](Input input) {
        return QtConcurrent::run([input = std::move(input)] { return calculate(input); });
    }, parent) {}
AudioSpectrumAnalyzer::AudioSpectrumAnalyzer(Dispatcher dispatcher, QObject *parent)
    : QObject(parent), m_dispatch(std::move(dispatcher))
{
    m_frame.spectrum.fill(0, m_bands);
    connect(&m_watcher, &QFutureWatcher<Frame>::finished, this, [this] {
        Frame result;
        try {
            if (!m_watcher.isCanceled() && m_watcher.future().resultCount() == 1) result = m_watcher.result();
        } catch (...) { /* Visualization failure must not affect playback. */ }
        const bool current = m_jobEpoch == m_epoch && m_enabled;
        m_busy = false;
        const QPointer<AudioSpectrumAnalyzer> guard(this);
        if (current) {
            m_frame = std::move(result);
            emit frameChanged();
        }
        if (guard) pump();
    });
}
bool AudioSpectrumAnalyzer::ownerThread() const { return thread() == QThread::currentThread(); }
void AudioSpectrumAnalyzer::reset()
{
    if (!ownerThread()) return;
    ++m_epoch;
    m_samples.clear(); m_pending.reset(); m_sampleRate = 0;
    m_frame = {}; m_frame.spectrum.fill(0, m_bands);
    emit frameChanged();
}
void AudioSpectrumAnalyzer::setBands(int value)
{
    if (!ownerThread()) return;
    value = std::clamp(value, 4, 512); value += value % 2;
    if (m_bands == value) return;
    m_bands = value; reset();
}
void AudioSpectrumAnalyzer::setEnabled(bool value)
{
    if (!ownerThread() || m_enabled == value) return;
    m_enabled = value; reset();
}
void AudioSpectrumAnalyzer::consume(const QAudioBuffer &buffer)
{
    if (!ownerThread() || !m_enabled) return;
    const auto format = buffer.format();
    const int rate = format.sampleRate(), channels = format.channelCount();
    if (!buffer.isValid() || rate < 100 || rate > 384000 || channels < 1 || channels > 32
        || format.sampleFormat() == QAudioFormat::Unknown) { reset(); return; }
    const QPointer<AudioSpectrumAnalyzer> guard(this);
    if (m_sampleRate != rate) {
        const auto epoch = m_epoch + 1;
        reset(); if (!guard || !m_enabled || m_epoch != epoch) return;
        m_sampleRate = rate;
    }
    const int frames = buffer.frameCount();
    // Read only the newest bounded window and first channel (original visual behavior).
    for (int frame = std::max(0, frames - fftSize); frame < frames; ++frame) {
        const int i = frame * channels;
        float sample = 0;
        switch (format.sampleFormat()) {
        case QAudioFormat::Float: sample = buffer.constData<float>()[i]; break;
        case QAudioFormat::Int16: sample = buffer.constData<qint16>()[i] / 32768.0f; break;
        case QAudioFormat::Int32: sample = buffer.constData<qint32>()[i] / 2147483648.0f; break;
        case QAudioFormat::UInt8: sample = (int(buffer.constData<quint8>()[i]) - 128) / 128.0f; break;
        default: reset(); return;
        }
        m_samples.append(std::isfinite(sample) ? std::clamp(sample, -1.0f, 1.0f) : 0);
    }
    const int limit = std::min(fftSize, rate / 10);
    if (m_samples.size() > limit) m_samples.remove(0, m_samples.size() - limit);
    if (m_samples.size() < 64) return;
    m_pending = Input{m_samples, rate, m_bands, {}};
    pump();
}
void AudioSpectrumAnalyzer::pump()
{
    if (m_busy || !m_enabled || !m_pending) return;
    auto input = std::move(*m_pending); m_pending.reset();
    input.previous = m_frame.spectrum;
    m_busy = true; m_jobEpoch = m_epoch;
    m_watcher.setFuture(m_dispatch(std::move(input)));
}
AudioSpectrumAnalyzer::Frame AudioSpectrumAnalyzer::calculate(const Input &input)
{
    using Complex = std::complex<float>;
    QVector<Complex> fft(fftSize);
    const int n = std::min(int(input.samples.size()), fftSize);
    float windowSum = 0;
    for (int i = 0; i < n; ++i) {
        const float window = 0.5f * (1 - std::cos(2 * pi * i / (n - 1)));
        fft[i] = Complex(input.samples[i] * window, 0); windowSum += window;
    }
    for (int i = 1, j = 0; i < fftSize; ++i) {
        int bit = fftSize >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(fft[i], fft[j]);
    }
    for (int len = 2; len <= fftSize; len <<= 1) {
        const Complex step(std::cos(-2 * pi / len), std::sin(-2 * pi / len));
        for (int i = 0; i < fftSize; i += len) {
            Complex w(1, 0);
            for (int j = 0; j < len / 2; ++j) {
                const auto u = fft[i + j], v = fft[i + j + len / 2] * w;
                fft[i + j] = u + v; fft[i + j + len / 2] = u - v; w *= step;
            }
        }
    }
    Frame frame; frame.spectrum.fill(0, input.bands);
    const int half = input.bands / 2;
    const float low = std::log(30.0f), high = std::log(input.sampleRate * 0.48f);
    for (int band = 0; band < half; ++band) {
        const int first = std::clamp(int(std::exp(low + (high - low) * band / half) * fftSize / input.sampleRate), 1, fftSize / 2 - 1);
        const int last = std::clamp(int(std::exp(low + (high - low) * (band + 1) / half) * fftSize / input.sampleRate), first + 1, fftSize / 2);
        float sum = 0;
        for (int bin = first; bin < last; ++bin) sum += std::abs(fft[bin]) / (windowSum + 1e-9f);
        const qreal value = std::clamp((20 * std::log10(sum / (last - first) + 1e-6f) + 50) / 45, 0.0f, 1.0f);
        for (int index : {half - 1 - band, half + band}) {
            const qreal previous = input.previous.size() == input.bands ? input.previous[index] : 0;
            frame.spectrum[index] = previous * 0.4 + value * 0.6;
        }
    }
    frame.path.append(QPointF(0, 80));
    for (int i = 0; i < input.bands - 1; ++i)
        frame.path.append(QPointF((i + 0.5) * 512 / (input.bands - 1),
            80 * (1 - (frame.spectrum[i] + frame.spectrum[i + 1]) / 2)));
    frame.path.append(QPointF(512, 80)); frame.path.append(QPointF(0, 80));
    return frame;
}
