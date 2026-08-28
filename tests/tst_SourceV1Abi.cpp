#include "SourceManager.h"

#include <QSignalSpy>
#include <QTest>

class SourceV1AbiTest : public QObject {
    Q_OBJECT

private slots:
    void preservesTrailingVirtualDispatchAcrossPluginDso();
};

void SourceV1AbiTest::preservesTrailingVirtualDispatchAcrossPluginDso()
{
    SourceManager manager;
    QObject parent;
    const SourceAccount account{QStringLiteral("fixture.frozen-v1"),
                                QStringLiteral("account-1"),
                                QStringLiteral("Frozen v1 Account")};

    manager.addSearchPath(QStringLiteral(QUEMUSIC_TEST_FROZEN_V1_PLUGIN_DIR));
    QCOMPARE(manager.loadAll(), 1);

    IMusicSourceSession *session = manager.createSession(account.sourceId, account, &parent);
    QVERIFY(session != nullptr);
    QSignalSpy succeeded(session, &IMusicSourceSession::requestSucceeded);
    const TrackRef track{account.sourceId, QStringLiteral("lyrics-1")};

    const QUuid completedRequest = session->fetchLyrics(track);
    QVERIFY(succeeded.wait(1000));
    QCOMPARE(succeeded.constFirst().at(0).toUuid(), completedRequest);
    QCOMPARE(succeeded.constFirst().at(1).toString(), QStringLiteral("fetchLyrics"));
    QCOMPARE(succeeded.constFirst().at(2).toJsonValue().toObject()
                 .value(QStringLiteral("text")).toString(),
             QStringLiteral("frozen-v1"));

    succeeded.clear();
    const QUuid cancelledRequest = session->fetchLyrics(track);
    session->cancel(cancelledRequest);
    QTest::qWait(100);
    QCOMPARE(succeeded.count(), 0);
}

QTEST_MAIN(SourceV1AbiTest)
#include "tst_SourceV1Abi.moc"
