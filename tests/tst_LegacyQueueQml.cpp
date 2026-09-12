#include <QGuiApplication>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QTest>

#include <memory>

class QueueModelDouble final : public QObject {
    Q_OBJECT
    Q_PROPERTY(int count READ count CONSTANT)
public:
    int count() const { return 1; }
};

class LegacyPlayerDouble final : public QObject {
    Q_OBJECT
public:
    int refreshCalls = 0;
    Q_INVOKABLE void refreshLegacyMusicPlay() { ++refreshCalls; }
};

class LegacyQueueQmlTest final : public QObject {
    Q_OBJECT
private slots:
    void copiesOnlyLegacyQueueFields();
    void dispatchesOnlyValidIndexes();
};

static std::unique_ptr<QObject> createController(QQmlEngine &engine,
                                                 QueueModelDouble *queue,
                                                 LegacyPlayerDouble *player)
{
    QQmlComponent component(&engine,
        QUrl(QStringLiteral("qrc:/QueMusic/components/LegacyQueueController.qml")));
    if (!component.isReady())
        qWarning().noquote() << component.errorString();
    return std::unique_ptr<QObject>(component.createWithInitialProperties({
        {QStringLiteral("queueModel"), QVariant::fromValue(queue)},
        {QStringLiteral("legacyPlayer"), QVariant::fromValue(player)}
    }));
}

void LegacyQueueQmlTest::copiesOnlyLegacyQueueFields()
{
    QQmlEngine engine;
    QueueModelDouble queue;
    LegacyPlayerDouble player;
    auto controller = createController(engine, &queue, &player);
    QVERIFY(controller);

    QVariant returned;
    const QVariantMap input{
        {QStringLiteral("name"), QStringLiteral("Song")},
        {QStringLiteral("path"), QStringLiteral("/music/song.flac")},
        {QStringLiteral("songer"), QStringLiteral("Artist")},
        {QStringLiteral("source"), -1},
        {QStringLiteral("mediaId"), QStringLiteral("must-not-leak")}
    };
    QVERIFY(QMetaObject::invokeMethod(controller.get(), "copyQueueEntry",
                                     Q_RETURN_ARG(QVariant, returned),
                                     Q_ARG(QVariant, input)));
    const QVariantMap copy = returned.toMap();
    QCOMPARE(copy.size(), 4);
    QCOMPARE(copy.value(QStringLiteral("name")), input.value(QStringLiteral("name")));
    QCOMPARE(copy.value(QStringLiteral("path")), input.value(QStringLiteral("path")));
    QCOMPARE(copy.value(QStringLiteral("songer")), input.value(QStringLiteral("songer")));
    QCOMPARE(copy.value(QStringLiteral("source")), input.value(QStringLiteral("source")));
}

void LegacyQueueQmlTest::dispatchesOnlyValidIndexes()
{
    QQmlEngine engine;
    QueueModelDouble queue;
    LegacyPlayerDouble player;
    auto controller = createController(engine, &queue, &player);
    QVERIFY(controller);

    QVERIFY(QMetaObject::invokeMethod(controller.get(), "playQueueEntry",
                                     Q_ARG(QVariant, -1)));
    QVERIFY(QMetaObject::invokeMethod(controller.get(), "playQueueEntry",
                                     Q_ARG(QVariant, 1)));
    QCOMPARE(player.refreshCalls, 0);
    QVERIFY(QMetaObject::invokeMethod(controller.get(), "playQueueEntry",
                                     Q_ARG(QVariant, 0)));
    QCOMPARE(player.refreshCalls, 1);
}

QTEST_MAIN(LegacyQueueQmlTest)
#include "tst_LegacyQueueQml.moc"
