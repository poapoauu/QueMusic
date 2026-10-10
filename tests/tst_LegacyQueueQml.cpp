#include <QGuiApplication>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QTest>

#include <memory>
#include <limits>

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
class QueueCoordinatorDouble final : public QObject {
    Q_OBJECT
public:
    int calls = 0, lastIndex = -1;
    Q_INVOKABLE void playQueueEntry(int index) { ++calls; lastIndex = index; }
};

class LegacyQueueQmlTest final : public QObject {
    Q_OBJECT
private slots:
    void copiesOnlyLegacyQueueFields();
    void dispatchesOnlyValidIndexes();
    void sourceDependenciesNeverFallBackAndIndicesStayTyped();
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

void LegacyQueueQmlTest::sourceDependenciesNeverFallBackAndIndicesStayTyped()
{
    QQmlEngine engine; QueueModelDouble queue; LegacyPlayerDouble player;
    QueueCoordinatorDouble coordinator;
    auto controller = createController(engine, &queue, &player);
    QVERIFY(controller);
    const auto play = [&](const QVariant &index) {
        return QMetaObject::invokeMethod(controller.get(), "playQueueEntry", Q_ARG(QVariant, index));
    };
    const QVariantList invalid{0.5, -1, "0", false, QVariant{},
        std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity(), 2};
    for (const auto &index : invalid) QVERIFY(play(index));
    QCOMPARE(player.refreshCalls, 0);
    QVERIFY(controller->setProperty("useCoordinator", true));
    QVERIFY(play(0)); // No Coordinator: no Legacy playback.
    QVERIFY(controller->setProperty("playbackCoordinator", QVariant::fromValue<QObject *>(&coordinator)));
    QVERIFY(play(0)); // No dedicated queue: do not borrow the one-row Legacy model.
    QVERIFY(controller->setProperty("secureQueueModel", QVariantList{}));
    QVERIFY(play(0)); // An empty Source queue is terminal.
    QVERIFY(controller->setProperty("secureQueueModel", QVariantMap{{"count", 1.5}}));
    QVERIFY(play(0)); // A malformed count must not be coerced either.
    QCOMPARE(player.refreshCalls, 0); QCOMPARE(coordinator.calls, 0);
    QVERIFY(controller->setProperty("secureQueueModel", QVariantList{QString("A"), QString("B")}));
    for (const auto &index : invalid) QVERIFY(play(index));
    QCOMPARE(coordinator.calls, 0);
    QVERIFY(play(1)); // Source array length, not the unrelated Legacy count.
    QCOMPARE(coordinator.calls, 1); QCOMPARE(coordinator.lastIndex, 1);
    QVERIFY(controller->setProperty("playbackCoordinator", QVariant::fromValue<QObject *>(nullptr)));
    QVERIFY(play(0));
    QCOMPARE(player.refreshCalls, 0); QCOMPARE(coordinator.calls, 1);
    QVERIFY(controller->setProperty("useCoordinator", false));
    QVERIFY(play(0)); // Explicit Legacy selection still works without Coordinator.
    QCOMPARE(player.refreshCalls, 1);
    QVERIFY(controller->setProperty("legacyPlayer", QVariant::fromValue<QObject *>(nullptr)));
    QVERIFY(play(0)); QCOMPARE(player.refreshCalls, 1);
}

QTEST_MAIN(LegacyQueueQmlTest)
#include "tst_LegacyQueueQml.moc"
