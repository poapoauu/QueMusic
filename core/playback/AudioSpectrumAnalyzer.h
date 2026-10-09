#pragma once
#include <QAudioBuffer>
#include <QFutureWatcher>
#include <QObject>
#include <QPointF>
#include <functional>
#include <optional>

// Host-private, owner-thread pipeline. Workers own value snapshots, never QObject pointers.
class AudioSpectrumAnalyzer final : public QObject {
    Q_OBJECT
public:
    struct Frame { QList<qreal> spectrum; QVector<QPointF> path; };
    explicit AudioSpectrumAnalyzer(QObject *parent = nullptr);
    Frame frame() const { return m_frame; }
    int bands() const { return m_bands; }
    bool enabled() const { return m_enabled; }
    void setBands(int bands);
    void setEnabled(bool enabled);
    void reset();
    void consume(const QAudioBuffer &buffer);
signals:
    void frameChanged();
private:
    struct Input { QVector<float> samples; int sampleRate; int bands; QList<qreal> previous; };
    using Dispatcher = std::function<QFuture<Frame>(Input)>;
    AudioSpectrumAnalyzer(Dispatcher dispatcher, QObject *parent = nullptr);
    static Frame calculate(const Input &input);
    void pump();
    bool ownerThread() const;
    QFutureWatcher<Frame> m_watcher;
    Dispatcher m_dispatch;
    std::optional<Input> m_pending;
    QVector<float> m_samples;
    Frame m_frame;
    quint64 m_epoch = 0, m_jobEpoch = 0;
    int m_bands = 128, m_sampleRate = 0;
    bool m_enabled = false, m_busy = false;
    friend class AudioSpectrumAnalyzerTestAccess;
};
