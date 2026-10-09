#include "core/playback/AudioSpectrumAnalyzer.h"
#include "cpp/GetWave.h"
#include <QPromise>
#include <QSignalSpy>
#include <QTest>
#include <QThread>
#include <algorithm>
#include <cmath>
#include <limits>

class AudioSpectrumAnalyzerTestAccess {
public:
    struct Ticket {
        AudioSpectrumAnalyzer::Input input;
        std::shared_ptr<QPromise<AudioSpectrumAnalyzer::Frame>> promise;
    };
    struct Harness {
        QList<Ticket> tickets;
        std::unique_ptr<AudioSpectrumAnalyzer> analyzer;
        Harness() {
            analyzer.reset(new AudioSpectrumAnalyzer([this](AudioSpectrumAnalyzer::Input input) {
                auto promise = std::make_shared<QPromise<AudioSpectrumAnalyzer::Frame>>();
                promise->start(); tickets.append({std::move(input), promise}); return promise->future();
            }));
            analyzer->setEnabled(true);
        }
        void finish(int index) {
            const auto &ticket = tickets.at(index);
            ticket.promise->addResult(AudioSpectrumAnalyzer::calculate(ticket.input)); ticket.promise->finish();
        }
        int retained() const { return analyzer->m_samples.size(); }
        int pending() const { return analyzer->m_pending ? analyzer->m_pending->samples.size() : 0; }
    };
};

static QAudioBuffer tone(QAudioFormat::SampleFormat type = QAudioFormat::Float, int rate = 48000, int channels = 2, int frames = 4096)
{
    QAudioFormat format; format.setSampleRate(rate); format.setChannelCount(channels); format.setSampleFormat(type);
    QByteArray bytes(frames * channels * format.bytesPerSample(), '\0');
    for (int f = 0; f < frames; ++f) for (int c = 0; c < channels; ++c) {
        const float value = c == 0 ? 0.8f * std::sin(2 * 3.141592653589793 * 1000 * f / rate) : 0;
        const int i = f * channels + c;
        switch (type) {
        case QAudioFormat::Float: reinterpret_cast<float *>(bytes.data())[i] = value; break;
        case QAudioFormat::Int16: reinterpret_cast<qint16 *>(bytes.data())[i] = qint16(value * 32767); break;
        case QAudioFormat::Int32: reinterpret_cast<qint32 *>(bytes.data())[i] = qint32(value * 2147483647.0); break;
        case QAudioFormat::UInt8: reinterpret_cast<quint8 *>(bytes.data())[i] = quint8(128 + value * 127); break;
        default: break;
        }
    }
    return QAudioBuffer(bytes, format);
}
static bool empty(const AudioSpectrumAnalyzer::Frame &frame)
{
    for (auto value : frame.spectrum) if (value != 0) return false;
    return frame.path.isEmpty();
}
class AudioSpectrumTest final : public QObject {
    Q_OBJECT
private slots:
    void pcmFormatsProduceFiniteMirroredDisplayFrames()
    {
        for (auto type : {QAudioFormat::Float, QAudioFormat::Int16, QAudioFormat::Int32, QAudioFormat::UInt8}) {
            AudioSpectrumAnalyzer analyzer; analyzer.setEnabled(true);
            QSignalSpy changed(&analyzer, &AudioSpectrumAnalyzer::frameChanged);
            bool ownerThread = true;
            connect(&analyzer, &AudioSpectrumAnalyzer::frameChanged, &analyzer, [&] {
                ownerThread &= QThread::currentThread() == analyzer.thread();
            });
            analyzer.consume(tone(type)); QTRY_VERIFY(!analyzer.frame().path.isEmpty());
            const auto frame = analyzer.frame(); QCOMPARE(frame.spectrum.size(), 128); QCOMPARE(frame.path.size(), 130);
            qreal maximum = 0;
            for (int i = 0; i < 128; ++i) {
                QVERIFY(std::isfinite(frame.spectrum[i])); QVERIFY(frame.spectrum[i] >= 0 && frame.spectrum[i] <= 1);
                QCOMPARE(frame.spectrum[i], frame.spectrum[127 - i]); maximum = std::max(maximum, frame.spectrum[i]);
            }
            QVERIFY(maximum > 0); QVERIFY(ownerThread); QVERIFY(changed.size() >= 1);
            for (const auto &point : frame.path) {
                QVERIFY(std::isfinite(point.x()) && std::isfinite(point.y()));
                QVERIFY(point.x() >= 0 && point.x() <= 512 && point.y() >= 0 && point.y() <= 80);
            }
            analyzer.setEnabled(false); QVERIFY(empty(analyzer.frame()));
        }
    }
    void oneWorkerAndOnlyNewestBoundedPendingWindow()
    {
        AudioSpectrumAnalyzerTestAccess::Harness h;
        h.analyzer->consume(tone()); QCOMPARE(h.tickets.size(), 1);
        for (int n = 0; n < 50; ++n) h.analyzer->consume(tone(QAudioFormat::Float, 48000, 2, 10000));
        QCOMPARE(h.tickets.size(), 1); QVERIFY(h.retained() <= 4096); QVERIFY(h.pending() <= 4096);
        h.finish(0); QTRY_COMPARE(h.tickets.size(), 2); h.finish(1);
        QTRY_VERIFY(!h.analyzer->frame().path.isEmpty()); QCoreApplication::processEvents(); QCOMPARE(h.tickets.size(), 2);
    }
    void resetDisableAndBandChangesFenceOldWork()
    {
        for (int kind : {0, 1, 2}) {
            AudioSpectrumAnalyzerTestAccess::Harness h; h.analyzer->consume(tone());
            if (kind == 0) h.analyzer->reset();
            if (kind == 1) { h.analyzer->setEnabled(false); h.analyzer->setEnabled(true); }
            if (kind == 2) h.analyzer->setBands(7);
            h.analyzer->consume(tone()); QCOMPARE(h.tickets.size(), 1);
            h.finish(0); QTRY_COMPARE(h.tickets.size(), 2); QVERIFY(empty(h.analyzer->frame()));
            h.finish(1); QTRY_VERIFY(!h.analyzer->frame().path.isEmpty());
            QCOMPARE(h.analyzer->frame().spectrum.size(), kind == 2 ? 8 : 128);
        }
    }
    void destroyAnalyzerBeforeCompletionAndObserverMayDestroyIt()
    {
        AudioSpectrumAnalyzerTestAccess::Harness h; h.analyzer->consume(tone()); h.analyzer.reset();
        h.finish(0); QCoreApplication::processEvents(); // Worker owns no dead analyzer pointer.
        AudioSpectrumAnalyzerTestAccess::Harness observed; observed.analyzer->consume(tone());
        connect(observed.analyzer.get(), &AudioSpectrumAnalyzer::frameChanged, this, [&] { observed.analyzer.reset(); });
        observed.finish(0); QTRY_VERIFY(!observed.analyzer);
    }
    void invalidPcmAndExtremeBandCountsCannotRetainOldFrames()
    {
        AudioSpectrumAnalyzer analyzer; analyzer.setEnabled(true); analyzer.consume(tone());
        QTRY_VERIFY(!analyzer.frame().path.isEmpty()); analyzer.consume({}); QVERIFY(empty(analyzer.frame()));
        analyzer.setBands(std::numeric_limits<int>::max()); QCOMPARE(analyzer.bands(), 512);
        analyzer.setBands(-1); QCOMPARE(analyzer.bands(), 4);
        auto buffer = tone(); auto *data = buffer.data<float>();
        for (int i = 0; i < buffer.sampleCount(); ++i) data[i] = std::numeric_limits<float>::quiet_NaN();
        analyzer.consume(buffer); QTRY_VERIFY(!analyzer.frame().path.isEmpty());
        for (auto value : analyzer.frame().spectrum) QCOMPARE(value, 0.0);
    }
    void legacyAttachmentDetachesAndClearsWithoutDanglingPlayer()
    {
        QMediaPlayer a, b; GetWave wave; wave.setBands(128); wave.setMediaPlayer(&a);
        QVERIFY(a.audioBufferOutput()); emit a.audioBufferOutput()->audioBufferReceived(tone());
        QTRY_VERIFY(!wave.wavePath().isEmpty()); wave.setMediaPlayer(&b);
        QVERIFY(!a.audioBufferOutput()); QVERIFY(b.audioBufferOutput()); QVERIFY(wave.wavePath().isEmpty());
        wave.setMediaPlayer(nullptr); QVERIFY(!b.audioBufferOutput()); QVERIFY(!wave.mediaPlayer());
        auto *player = new QMediaPlayer; wave.setMediaPlayer(player); delete player;
        QVERIFY(!wave.mediaPlayer()); QVERIFY(wave.wavePath().isEmpty());
        { GetWave scoped; scoped.setMediaPlayer(&a); QVERIFY(a.audioBufferOutput()); }
        QVERIFY(!a.audioBufferOutput());
        bool changed = false;
        connect(&wave, &GetWave::wavePathChanged, &wave, [&] {
            if (!changed) { changed = true; wave.setMediaPlayer(&b); }
        });
        wave.setMediaPlayer(&a); QCOMPARE(wave.mediaPlayer(), &b); QVERIFY(!a.audioBufferOutput());
    }
};
QTEST_GUILESS_MAIN(AudioSpectrumTest)
#include "tst_AudioSpectrum.moc"
