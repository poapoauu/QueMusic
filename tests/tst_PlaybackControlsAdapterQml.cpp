#include <QMetaMethod>
#include <QMetaProperty>
#include <QSignalSpy>
#include <QPointF>
#include <QTest>
#include <QtQml/QQmlComponent>
#include <QtQml/QQmlEngine>

class FakePlaybackController final : public QObject {
    Q_OBJECT
    Q_PROPERTY(qint64 position MEMBER position NOTIFY positionChanged)
    Q_PROPERTY(qint64 duration MEMBER duration NOTIFY durationChanged)
    Q_PROPERTY(bool playing MEMBER playing NOTIFY playingChanged)
    Q_PROPERTY(bool seekable MEMBER seekable NOTIFY seekableChanged)
    Q_PROPERTY(qreal volume MEMBER volume NOTIFY volumeChanged)
    Q_PROPERTY(qreal playbackRate MEMBER playbackRate NOTIFY playbackRateChanged)
    Q_PROPERTY(bool muted MEMBER muted NOTIFY mutedChanged)
    Q_PROPERTY(int state MEMBER state NOTIFY stateChanged)
    Q_PROPERTY(QVariantList wavePath MEMBER wavePath NOTIFY wavePathChanged)
public:
    qint64 position = 1250;
    qint64 duration = 9000;
    bool playing = true;
    bool seekable = true;
    qreal volume = 0.75;
    qreal playbackRate = 1.25;
    bool muted = false;
    int state = 2;
    int plays = 0;
    int pauses = 0;
    int stops = 0;
    qint64 sought = -1;
    qreal requestedVolume = -1;
    qreal requestedRate = -1;
    bool requestedMuted = false;
    bool spectrumEnabled = false;
    QVariantList wavePath;

    Q_INVOKABLE void play() { ++plays; }
    Q_INVOKABLE void pause() { ++pauses; }
    Q_INVOKABLE void stop() { ++stops; }
    Q_INVOKABLE void seek(qint64 value) { sought = value; }
    Q_INVOKABLE void setVolume(qreal value) { requestedVolume = value; }
    Q_INVOKABLE void setPlaybackRate(qreal value) { requestedRate = value; }
    Q_INVOKABLE void setMuted(bool value) { requestedMuted = value; }
    Q_INVOKABLE void setSpectrumEnabled(bool value) { spectrumEnabled = value; }

signals:
    void positionChanged();
    void durationChanged();
    void playingChanged();
    void seekableChanged();
    void volumeChanged();
    void playbackRateChanged();
    void mutedChanged();
    void stateChanged();
    void playbackError(QString messageKey);
    void wavePathChanged();
};

namespace {
std::unique_ptr<QObject> createAdapter(QQmlEngine &engine, QObject *controller,
                                       QString *error = nullptr)
{
    QQmlComponent component(&engine,
        QUrl(QStringLiteral("qrc:/QueMusic/components/PlaybackControlsAdapter.qml")));
    if (component.status() != QQmlComponent::Ready) {
        if (error) *error = component.errorString();
        return {};
    }
    QObject *created = component.createWithInitialProperties({
        {QStringLiteral("controller"), QVariant::fromValue(controller)}
    });
    if (!created && error) *error = component.errorString();
    return std::unique_ptr<QObject>(created);
}
}

class PlaybackControlsAdapterQmlTest final : public QObject {
    Q_OBJECT
private slots:
    void spectrumProjectionAndControllerReplacementUseOnlyTypedControls()
    {
        QQmlEngine engine;
        FakePlaybackController first, second;
        auto adapter = createAdapter(engine, &first);
        QVERIFY(adapter);
        QVERIFY(!first.spectrumEnabled);
        QVERIFY(adapter->setProperty("spectrumEnabled", true));
        QVERIFY(first.spectrumEnabled);
        first.wavePath = {QPointF(0, 80), QPointF(256, 25), QPointF(512, 80)};
        emit first.wavePathChanged();
        QCOMPARE(adapter->property("wavePath").toList(), first.wavePath);
        QVERIFY(adapter->setProperty("controller", QVariant::fromValue<QObject *>(&second)));
        QVERIFY(!first.spectrumEnabled);
        QVERIFY(second.spectrumEnabled);
        QVERIFY(adapter->property("wavePath").toList().isEmpty());
        emit first.wavePathChanged();
        QVERIFY(adapter->property("wavePath").toList().isEmpty());
        QVERIFY(adapter->setProperty("controller", QVariant::fromValue<QObject *>(nullptr)));
        QVERIFY(!second.spectrumEnabled);
        QVERIFY(adapter->property("wavePath").toList().isEmpty());
        QVERIFY(adapter->setProperty("controller", QVariant::fromValue<QObject *>(&second)));
        QVERIFY(second.spectrumEnabled);
        adapter.reset();
        QVERIFY(!second.spectrumEnabled);
    }

    void mirrorsOnlySafePlaybackState()
    {
        QQmlEngine engine;
        FakePlaybackController controller;
        QString error;
        auto adapter = createAdapter(engine, &controller, &error);
        QVERIFY2(adapter, qPrintable(error));
        QCOMPARE(adapter->property("position").toLongLong(), 1250);
        QCOMPARE(adapter->property("duration").toLongLong(), 9000);
        QCOMPARE(adapter->property("playing").toBool(), true);
        QCOMPARE(adapter->property("seekable").toBool(), true);
        QCOMPARE(adapter->property("volume").toReal(), 0.75);
        QCOMPARE(adapter->property("playbackRate").toReal(), 1.25);
        QCOMPARE(adapter->property("muted").toBool(), false);
        QCOMPARE(adapter->property("state").toInt(), 2);

        controller.position = 2500;
        controller.playing = false;
        emit controller.positionChanged();
        emit controller.playingChanged();
        QCOMPARE(adapter->property("position").toLongLong(), 2500);
        QCOMPARE(adapter->property("playing").toBool(), false);

        const QSet<QByteArray> forbidden{"source", "url", "headers", "player",
                                         "mediaPlayer", "metaData", "errorString"};
        const QMetaObject *meta = adapter->metaObject();
        for (int i = 0; i < meta->propertyCount(); ++i)
            QVERIFY2(!forbidden.contains(meta->property(i).name()), meta->property(i).name());
        for (int i = 0; i < meta->methodCount(); ++i)
            QVERIFY2(!forbidden.contains(meta->method(i).name()), meta->method(i).name());
    }

    void forwardsTransportAndSettings()
    {
        QQmlEngine engine;
        FakePlaybackController controller;
        auto adapter = createAdapter(engine, &controller);
        QVERIFY(adapter);
        QVERIFY(QMetaObject::invokeMethod(adapter.get(), "play"));
        QVERIFY(QMetaObject::invokeMethod(adapter.get(), "pause"));
        QVERIFY(QMetaObject::invokeMethod(adapter.get(), "stop"));
        QVERIFY(QMetaObject::invokeMethod(adapter.get(), "seek", Q_ARG(QVariant, 4321)));
        QVERIFY(QMetaObject::invokeMethod(adapter.get(), "setVolume", Q_ARG(QVariant, 0.4)));
        QVERIFY(QMetaObject::invokeMethod(adapter.get(), "setPlaybackRate", Q_ARG(QVariant, 1.5)));
        QVERIFY(QMetaObject::invokeMethod(adapter.get(), "setMuted", Q_ARG(QVariant, true)));
        QCOMPARE(controller.plays, 1);
        QCOMPARE(controller.pauses, 1);
        QCOMPARE(controller.stops, 1);
        QCOMPARE(controller.sought, 4321);
        QCOMPARE(controller.requestedVolume, 0.4);
        QCOMPARE(controller.requestedRate, 1.5);
        QCOMPARE(controller.requestedMuted, true);
    }

    void preservesLongMillisecondValues()
    {
        QQmlEngine engine;
        FakePlaybackController controller;
        controller.position = 3000000000LL;
        controller.duration = 4000000000LL;
        auto adapter = createAdapter(engine, &controller);
        QVERIFY(adapter);
        QCOMPARE(adapter->property("position").toLongLong(), 3000000000LL);
        QCOMPARE(adapter->property("duration").toLongLong(), 4000000000LL);
    }

    void relaysOnlySafeErrorKey()
    {
        QQmlEngine engine;
        FakePlaybackController controller;
        auto adapter = createAdapter(engine, &controller);
        QVERIFY(adapter);
        QSignalSpy errors(adapter.get(), SIGNAL(playbackError(QString)));
        emit controller.playbackError(QStringLiteral("music.playbackBackendError"));
        QCOMPARE(errors.size(), 1);
        QCOMPARE(errors.at(0).at(0).toString(), QStringLiteral("music.playbackBackendError"));
    }

    void nullControllerUsesDefaultsAndTransportIsNoOp()
    {
        QQmlEngine engine;
        QString error;
        auto adapter = createAdapter(engine, nullptr, &error);
        QVERIFY2(adapter, qPrintable(error));
        QCOMPARE(adapter->property("position").toLongLong(), 0);
        QCOMPARE(adapter->property("duration").toLongLong(), 0);
        QCOMPARE(adapter->property("playing").toBool(), false);
        QCOMPARE(adapter->property("seekable").toBool(), false);
        QCOMPARE(adapter->property("volume").toReal(), 1.0);
        QCOMPARE(adapter->property("playbackRate").toReal(), 1.0);
        QCOMPARE(adapter->property("muted").toBool(), false);
        QCOMPARE(adapter->property("state").toInt(), 0);
        QVERIFY(QMetaObject::invokeMethod(adapter.get(), "play"));
        QVERIFY(QMetaObject::invokeMethod(adapter.get(), "pause"));
        QVERIFY(QMetaObject::invokeMethod(adapter.get(), "stop"));
        QVERIFY(QMetaObject::invokeMethod(adapter.get(), "seek", Q_ARG(QVariant, 10)));
        QVERIFY(QMetaObject::invokeMethod(adapter.get(), "setVolume", Q_ARG(QVariant, 0.5)));
        QVERIFY(QMetaObject::invokeMethod(adapter.get(), "setPlaybackRate", Q_ARG(QVariant, 1.5)));
        QVERIFY(QMetaObject::invokeMethod(adapter.get(), "setMuted", Q_ARG(QVariant, true)));
    }
};

QTEST_GUILESS_MAIN(PlaybackControlsAdapterQmlTest)
#include "tst_PlaybackControlsAdapterQml.moc"
