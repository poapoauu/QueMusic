#include "QtPlaybackController.h"
#include "PlaybackCoordinator.h"

#include <QMetaMethod>
#include <QMetaProperty>
#include <QPointer>
#include <QSignalSpy>
#include <QTest>

#include <cmath>
#include <memory>

class QtPlaybackControllerTestAccess {
public:
    using BackendMediaStatus = QtPlaybackController::BackendMediaStatus;
    using BackendPlaybackState = QtPlaybackController::BackendPlaybackState;
    class FakeBackend final : public QtPlaybackController::Backend {
    public:
        QtPlaybackController::BackendCallbacks callbacks;
        QUrl source;
        int plays = 0;
        int pauses = 0;
        int stops = 0;
        qint64 sought = -1;
        qreal volume = -1;
        qreal rate = -1;
        bool muted = false;

        void setCallbacks(QtPlaybackController::BackendCallbacks value) override
        {
            callbacks = std::move(value);
        }
        void clearCallbacks() override { callbacks = {}; }
        void setSource(const QUrl &value) override { source = value; }
        void play() override { ++plays; }
        void pause() override { ++pauses; }
        void stop() override { ++stops; }
        void setPosition(qint64 value) override { sought = value; }
        void setVolume(qreal value) override { volume = value; }
        void setPlaybackRate(qreal value) override { rate = value; }
        void setMuted(bool value) override { muted = value; }
    };

    struct Harness {
        QList<FakeBackend *> backends;
        QList<QPair<QUuid, qint64>> positions;
        QList<QUuid> stopped;
        std::unique_ptr<QtPlaybackController> controller;

        Harness()
        {
            QtPlaybackController::BackendFactory factory = [this] {
                auto backend = std::make_unique<FakeBackend>();
                backends.append(backend.get());
                return backend;
            };
            QtPlaybackController::Reporter reporter;
            reporter.position = [this](QUuid generation, qint64 position) {
                positions.append({generation, position});
            };
            reporter.stopped = [this](QUuid generation) { stopped.append(generation); };
            controller.reset(new QtPlaybackController(std::move(factory), std::move(reporter)));
        }
    };
};

namespace {
StreamDescriptorV2 stream(const QString &url = QStringLiteral("https://example.test/audio.flac"))
{
    StreamDescriptorV2 value;
    value.url = QUrl(url);
    value.seekable = true;
    return value;
}

QSet<QByteArray> metaNames(const QMetaObject &metaObject)
{
    QSet<QByteArray> names;
    for (int i = 0; i < metaObject.propertyCount(); ++i)
        names.insert(metaObject.property(i).name());
    for (int i = 0; i < metaObject.methodCount(); ++i)
        names.insert(metaObject.method(i).name());
    return names;
}
}

class QtPlaybackControllerTest final : public QObject {
    Q_OBJECT
private slots:
    void facadeDoesNotExposePlaybackSecrets()
    {
        QtPlaybackController controller;
        const auto names = metaNames(QtPlaybackController::staticMetaObject);
        for (const auto forbidden : {"source", "url", "headers", "metaData", "error",
                                     "errorString", "player", "mediaPlayer", "stream"})
            QVERIFY2(!names.contains(forbidden), forbidden);
        QVERIFY(controller.prepare(stream(QStringLiteral("file:///tmp/qt-playback-private.wav")),
                                   QUuid::createUuid()));
        QCOMPARE(controller.children().size(), 0);
        QVERIFY(!controller.findChild<QObject *>());
    }

    void rejectsInvalidStreamsWithoutReplacingPreparedOccurrence()
    {
        QtPlaybackControllerTestAccess::Harness h;
        const auto generation = QUuid::createUuid();
        QVERIFY(h.controller->prepare(stream(), generation));
        QCOMPARE(h.backends.size(), 1);

        const QList<StreamDescriptorV2> invalid = [] {
            QList<StreamDescriptorV2> values;
            auto header = stream(); header.headers.insert(QStringLiteral("Authorization"), QStringLiteral("secret")); values << header;
            values << stream(QStringLiteral("relative/file.mp3"));
            values << stream(QStringLiteral("ftp://example.test/file.mp3"));
            values << stream(QStringLiteral("file:relative.wav"));
            auto expired = stream(); expired.expiresAt = QDateTime::currentDateTimeUtc().addSecs(-1); values << expired;
            values << stream(QString());
            return values;
        }();
        for (const auto &value : invalid)
            QVERIFY(!h.controller->prepare(value, QUuid::createUuid()));
        QVERIFY(!h.controller->prepare(stream(), {}));
        QCOMPARE(h.backends.size(), 1);
    }

    void acceptsAbsoluteLocalHttpAndHttpsUrls()
    {
        QtPlaybackControllerTestAccess::Harness h;
        QVERIFY(h.controller->prepare(stream(QStringLiteral("file:///tmp/qt-playback.wav")), QUuid::createUuid()));
        QVERIFY(h.controller->prepare(stream(QStringLiteral("http://example.test/a")), QUuid::createUuid()));
        QVERIFY(h.controller->prepare(stream(QStringLiteral("https://example.test/b")), QUuid::createUuid()));
        QCOMPARE(h.backends.size(), 3);
    }

    void exactGenerationControlsAndStaleGenerationIsIgnored()
    {
        QtPlaybackControllerTestAccess::Harness h;
        const auto current = QUuid::createUuid();
        const auto stale = QUuid::createUuid();
        QVERIFY(h.controller->prepare(stream(), current));
        auto *backend = h.backends.constLast();

        h.controller->play(stale);
        h.controller->stop(stale);
        QCOMPARE(backend->plays, 0);
        QCOMPARE(backend->stops, 0);

        h.controller->play(current);
        QCOMPARE(backend->plays, 1);
        h.controller->stop(current);
        QCOMPARE(backend->stops, 1);
        QCOMPARE(h.stopped, QList<QUuid>{current});
    }

    void switchingOccurrenceDropsOldCallbacks()
    {
        QtPlaybackControllerTestAccess::Harness h;
        const auto oldGeneration = QUuid::createUuid();
        const auto currentGeneration = QUuid::createUuid();
        QVERIFY(h.controller->prepare(stream(), oldGeneration));
        auto oldCallbacks = h.backends.constLast()->callbacks;
        QVERIFY(h.controller->prepare(stream(QStringLiteral("https://example.test/new")), currentGeneration));
        QCOMPARE(h.backends.size(), 2);

        oldCallbacks.position(55);
        oldCallbacks.mediaStatus(QtPlaybackControllerTestAccess::BackendMediaStatus::EndOfMedia);
        oldCallbacks.error();
        QVERIFY(h.positions.isEmpty());
        QVERIFY(h.stopped.isEmpty());
    }

    void pauseIsNotStopAndPositionUsesBoundGeneration()
    {
        QtPlaybackControllerTestAccess::Harness h;
        const auto generation = QUuid::createUuid();
        QVERIFY(h.controller->prepare(stream(), generation));
        auto *backend = h.backends.constLast();
        backend->callbacks.position(1234);
        QCOMPARE(h.controller->position(), 1234);
        QCOMPARE(h.positions, (QList<QPair<QUuid, qint64>>{{generation, 1234}}));

        h.controller->pause();
        QCOMPARE(backend->pauses, 1);
        QVERIFY(h.stopped.isEmpty());
        QCOMPARE(h.controller->state(), QtPlaybackController::Paused);
    }

    void endAndErrorReportStoppedOnlyOnceWithSafeState()
    {
        QtPlaybackControllerTestAccess::Harness h;
        QSignalSpy safeErrors(h.controller.get(), &QtPlaybackController::playbackError);
        const auto first = QUuid::createUuid();
        QVERIFY(h.controller->prepare(stream(), first));
        auto firstCallbacks = h.backends.constLast()->callbacks;
        firstCallbacks.mediaStatus(QtPlaybackControllerTestAccess::BackendMediaStatus::EndOfMedia);
        firstCallbacks.playbackState(QtPlaybackControllerTestAccess::BackendPlaybackState::Stopped);
        QCOMPARE(h.stopped, QList<QUuid>{first});
        QCOMPARE(h.controller->state(), QtPlaybackController::Stopped);

        const auto second = QUuid::createUuid();
        QVERIFY(h.controller->prepare(stream(), second));
        auto secondCallbacks = h.backends.constLast()->callbacks;
        secondCallbacks.error();
        secondCallbacks.mediaStatus(QtPlaybackControllerTestAccess::BackendMediaStatus::InvalidMedia);
        QCOMPARE(h.stopped, (QList<QUuid>{first, second}));
        QCOMPARE(h.controller->state(), QtPlaybackController::Error);
        QCOMPARE(safeErrors.size(), 1);
        QCOMPARE(safeErrors.at(0).at(0).toString(), QStringLiteral("music.playbackBackendError"));
    }

    void terminalOccurrenceRejectsCallbacksAndTransport()
    {
        QtPlaybackControllerTestAccess::Harness h;
        const auto generation = QUuid::createUuid();
        QVERIFY(h.controller->prepare(stream(), generation));
        auto *backend = h.backends.constLast();
        auto callbacks = backend->callbacks;
        callbacks.duration(1000);
        callbacks.seekable(true);
        callbacks.position(250);
        callbacks.mediaStatus(QtPlaybackControllerTestAccess::BackendMediaStatus::EndOfMedia);

        callbacks.position(750);
        callbacks.playbackState(QtPlaybackControllerTestAccess::BackendPlaybackState::Playing);
        callbacks.mediaStatus(QtPlaybackControllerTestAccess::BackendMediaStatus::Loading);
        callbacks.error();
        h.controller->play();
        h.controller->pause();
        h.controller->seek(500);

        QCOMPARE(h.controller->position(), 250);
        QCOMPARE(h.controller->state(), QtPlaybackController::Stopped);
        QVERIFY(!h.controller->playing());
        QCOMPARE(backend->plays, 0);
        QCOMPARE(backend->pauses, 0);
        QCOMPARE(backend->sought, -1);
        QCOMPARE(h.positions, (QList<QPair<QUuid, qint64>>{{generation, 250}}));
        QCOMPARE(h.stopped, QList<QUuid>{generation});
    }

    void loadedStatusDoesNotOverwritePausedState()
    {
        QtPlaybackControllerTestAccess::Harness h;
        QVERIFY(h.controller->prepare(stream(), QUuid::createUuid()));
        auto callbacks = h.backends.constLast()->callbacks;
        h.controller->pause();
        callbacks.mediaStatus(QtPlaybackControllerTestAccess::BackendMediaStatus::Loaded);
        QCOMPARE(h.controller->state(), QtPlaybackController::Paused);
    }

    void validatesVolumeMuteRateAndSeek()
    {
        QtPlaybackControllerTestAccess::Harness h;
        const auto generation = QUuid::createUuid();
        QVERIFY(h.controller->prepare(stream(), generation));
        auto *backend = h.backends.constLast();
        backend->callbacks.duration(1000);
        backend->callbacks.seekable(true);

        h.controller->setVolume(2.0);
        QCOMPARE(h.controller->volume(), 1.0);
        QCOMPARE(backend->volume, 1.0);
        h.controller->setVolume(-1.0);
        QCOMPARE(h.controller->volume(), 0.0);
        h.controller->setVolume(std::numeric_limits<qreal>::quiet_NaN());
        QCOMPARE(h.controller->volume(), 0.0);

        h.controller->setMuted(true);
        QVERIFY(h.controller->muted());
        QVERIFY(backend->muted);
        h.controller->setPlaybackRate(1.5);
        QCOMPARE(h.controller->playbackRate(), 1.5);
        QCOMPARE(backend->rate, 1.5);
        h.controller->setPlaybackRate(0);
        h.controller->setPlaybackRate(std::numeric_limits<qreal>::infinity());
        QCOMPARE(h.controller->playbackRate(), 1.5);

        h.controller->seek(750);
        QCOMPARE(backend->sought, 750);
        h.controller->seek(-1);
        h.controller->seek(1001);
        QCOMPARE(backend->sought, 750);
        backend->callbacks.seekable(false);
        h.controller->seek(500);
        QCOMPARE(backend->sought, 750);
    }

    void coordinatorAndSinkMayBeDestroyedInEitherOrder()
    {
        auto *controller = new QtPlaybackController;
        auto *coordinator = new PlaybackCoordinator(nullptr, controller);
        controller->setCoordinator(coordinator);
        delete coordinator;
        controller->play();
        delete controller;

        controller = new QtPlaybackController;
        coordinator = new PlaybackCoordinator(nullptr, controller);
        controller->setCoordinator(coordinator);
        delete controller;
        delete coordinator;
    }
};

QTEST_GUILESS_MAIN(QtPlaybackControllerTest)
#include "tst_QtPlaybackController.moc"
