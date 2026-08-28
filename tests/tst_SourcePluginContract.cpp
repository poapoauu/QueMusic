#include "IMusicSourcePlugin.h"
#include "IMusicSourceArtworkSession.h"
#include "IMusicSourceSession.h"
#include "SourcePluginContext.h"

#include <QNetworkAccessManager>
#include <QSignalSpy>
#include <QTest>
#include <QTimer>

class FakeMusicSourceSession final : public IMusicSourceSession {
    Q_OBJECT

public:
    using IMusicSourceSession::IMusicSourceSession;

    QUuid search(const SearchQuery &query) override
    {
        return completeSuccess(QStringLiteral("search"), QJsonValue(query.query));
    }

    QUuid browse(const BrowseQuery &) override { return completeUnsupported(); }
    QUuid resolveStream(const TrackRef &track) override
    {
        return completeSuccess(QStringLiteral("resolveStream"),
                               QJsonObject{{QStringLiteral("url"),
                                            QStringLiteral("https://example.invalid/%1.mp3")
                                                .arg(track.nativeId)}});
    }
    QUuid fetchLyrics(const TrackRef &) override { return completeUnsupported(); }
    void cancel(const QUuid &) override { }

private:
    QUuid completeSuccess(const QString &operation, const QJsonValue &result)
    {
        const QUuid requestId = QUuid::createUuid();
        QTimer::singleShot(0, this, [this, requestId, operation, result] {
            emit requestSucceeded(requestId, operation, result);
        });
        return requestId;
    }

    QUuid completeUnsupported()
    {
        const QUuid requestId = QUuid::createUuid();
        QTimer::singleShot(0, this, [this, requestId] {
            emit requestFailed(requestId,
                               {SourceErrorKind::Unsupported,
                                QStringLiteral("Fake operation is unsupported"), std::nullopt});
        });
        return requestId;
    }
};

class FakeArtworkMusicSourceSession final : public IMusicSourceSession,
                                            public IMusicSourceArtworkSession {
    Q_OBJECT
    Q_INTERFACES(IMusicSourceArtworkSession)

public:
    using IMusicSourceSession::IMusicSourceSession;

    QUuid search(const SearchQuery &query) override
    {
        return completeSuccess(QStringLiteral("search"), QJsonValue(query.query));
    }

    QUuid browse(const BrowseQuery &) override { return completeUnsupported(); }
    QUuid resolveStream(const TrackRef &track) override
    {
        return completeSuccess(QStringLiteral("resolveStream"),
                               QJsonObject{{QStringLiteral("url"),
                                            QStringLiteral("https://example.invalid/%1.mp3")
                                                .arg(track.nativeId)}});
    }
    QUuid fetchArtwork(const TrackRef &track) override
    {
        return completeSuccess(QStringLiteral("fetchArtwork"), QJsonValue(track.nativeId));
    }
    QUuid fetchLyrics(const TrackRef &) override { return completeUnsupported(); }
    void cancel(const QUuid &) override { }

private:
    QUuid completeSuccess(const QString &operation, const QJsonValue &result)
    {
        const QUuid requestId = QUuid::createUuid();
        QTimer::singleShot(0, this, [this, requestId, operation, result] {
            emit requestSucceeded(requestId, operation, result);
        });
        return requestId;
    }

    QUuid completeUnsupported()
    {
        const QUuid requestId = QUuid::createUuid();
        QTimer::singleShot(0, this, [this, requestId] {
            emit requestFailed(requestId,
                               {SourceErrorKind::Unsupported,
                                QStringLiteral("Fake operation is unsupported"), std::nullopt});
        });
        return requestId;
    }
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
    void exposesOptionalArtworkInterfaceByRequestId();
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
    QSignalSpy succeeded(&session, &IMusicSourceSession::requestSucceeded);

    const QUuid requestId = session.search({QStringLiteral("ambient"), 10});

    QVERIFY(!requestId.isNull());
    QVERIFY(succeeded.wait(1000));
    QCOMPARE(succeeded.constFirst().at(0).toUuid(), requestId);
    QCOMPARE(succeeded.constFirst().at(1).toString(), QStringLiteral("search"));
}

void SourcePluginContractTest::exposesOptionalArtworkInterfaceByRequestId()
{
    FakeMusicSourceSession baseSession;
    FakeArtworkMusicSourceSession artworkSession;
    QSignalSpy succeeded(&artworkSession, &IMusicSourceSession::requestSucceeded);

    QVERIFY(qobject_cast<IMusicSourceArtworkSession *>(&baseSession) == nullptr);

    auto *artworkInterface = qobject_cast<IMusicSourceArtworkSession *>(&artworkSession);
    QVERIFY(artworkInterface != nullptr);

    const QUuid requestId = artworkInterface->fetchArtwork(
        {QStringLiteral("fake"), QStringLiteral("cover-1")});

    QVERIFY(!requestId.isNull());
    QVERIFY(succeeded.wait(1000));
    QCOMPARE(succeeded.constFirst().at(0).toUuid(), requestId);
    QCOMPARE(succeeded.constFirst().at(1).toString(), QStringLiteral("fetchArtwork"));
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
