#include "IMusicSourcePlugin.h"
#include "IMusicSourceSession.h"
#include "SourcePluginContext.h"

#include <QNetworkAccessManager>
#include <QTest>

class FakeMusicSourceSession final : public IMusicSourceSession {
    Q_OBJECT

public:
    using IMusicSourceSession::IMusicSourceSession;

    QUuid search(const SearchQuery &query) override
    {
        const QUuid requestId = QUuid::createUuid();
        emit requestSucceeded(requestId, QStringLiteral("search"), QJsonValue(query.query));
        return requestId;
    }

    QUuid browse(const BrowseQuery &) override { return QUuid::createUuid(); }
    QUuid resolveStream(const TrackRef &) override { return QUuid::createUuid(); }
    QUuid fetchLyrics(const TrackRef &) override { return QUuid::createUuid(); }
    void cancel(const QUuid &) override { }
};

class FakeMusicSourcePlugin final : public QObject, public IMusicSourcePlugin {
    Q_OBJECT
    Q_INTERFACES(IMusicSourcePlugin)

public:
    SourceDescriptor descriptor() const override
    {
        return {QStringLiteral("fake"), QStringLiteral("Fake Source"),
                QStringLiteral("1.0.0"), QStringLiteral("test"),
                QStringLiteral("1.0"), SourceCapability::Search | SourceCapability::StreamAudio};
    }

    bool initialize(SourcePluginContext &context) override
    {
        initialized = context.network != nullptr;
        return initialized;
    }

    IMusicSourceSession *createSession(const SourceAccount &, QObject *parent) override
    {
        return new FakeMusicSourceSession(parent);
    }

    bool initialized = false;
};

class SourcePluginContractTest : public QObject {
    Q_OBJECT

private slots:
    void createsSessionThroughStableContract();
    void reportsAsyncOperationByRequestId();
    void exposesCapabilitiesWithoutProviderBranching();
};

void SourcePluginContractTest::createsSessionThroughStableContract()
{
    FakeMusicSourcePlugin plugin;
    QNetworkAccessManager network;
    SourcePluginContext context{&network, QStringLiteral("/tmp/quemusic-contract")};
    const SourceAccount account{QStringLiteral("fake"), QStringLiteral("account-1"),
                                QStringLiteral("Test Account")};

    QVERIFY(plugin.initialize(context));
    IMusicSourceSession *session = plugin.createSession(account, &plugin);

    QVERIFY(session != nullptr);
    QCOMPARE(session->parent(), &plugin);
}

void SourcePluginContractTest::reportsAsyncOperationByRequestId()
{
    FakeMusicSourceSession session;
    QUuid completedRequestId;
    QString operation;
    QObject::connect(&session, &IMusicSourceSession::requestSucceeded, &session,
                     [&completedRequestId, &operation](const QUuid &requestId,
                                                        const QString &operationName,
                                                        const QJsonValue &) {
                         completedRequestId = requestId;
                         operation = operationName;
                     });

    const QUuid requestId = session.search({QStringLiteral("ambient"), 10});

    QVERIFY(!requestId.isNull());
    QCOMPARE(completedRequestId, requestId);
    QCOMPARE(operation, QStringLiteral("search"));
}

void SourcePluginContractTest::exposesCapabilitiesWithoutProviderBranching()
{
    const SourceDescriptor descriptor = FakeMusicSourcePlugin().descriptor();

    QVERIFY(descriptor.capabilities.testFlag(SourceCapability::Search));
    QVERIFY(descriptor.capabilities.testFlag(SourceCapability::StreamAudio));
    QVERIFY(!descriptor.capabilities.testFlag(SourceCapability::Browse));
}

QTEST_MAIN(SourcePluginContractTest)
#include "tst_SourcePluginContract.moc"
