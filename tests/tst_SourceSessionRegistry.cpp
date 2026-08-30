#include "SourceAccountStore.h"
#include "SourceManager.h"

#include "PluginManager.h"

#include <QHash>
#include <QPointer>
#include <QSettings>
#include <QSignalSpy>
#include <QSet>
#include <QStringList>
#include <QTemporaryDir>
#include <QTest>

#define private public
#include "SourceSessionRegistry.h"
#undef private

namespace {

class MemorySecretStore final : public ISecretStore {
public:
    bool write(const QString &reference, const QByteArray &secret, QString *error) override
    {
        Q_UNUSED(error)
        m_values.insert(reference, secret);
        return true;
    }

    std::optional<QByteArray> read(const QString &reference, QString *error) const override
    {
        const auto value = m_values.constFind(reference);
        if (value == m_values.cend()) {
            if (error) {
                *error = QStringLiteral("Secret is unavailable");
            }
            return std::nullopt;
        }
        return *value;
    }

    bool remove(const QString &reference, QString *error) override
    {
        Q_UNUSED(error)
        m_values.remove(reference);
        return true;
    }

private:
    QHash<QString, QByteArray> m_values;
};

SourceAccount testAccount(const QString &accountId = QStringLiteral("home"))
{
    return {QStringLiteral("test-source"),
            accountId,
            QStringLiteral("Test account"),
            {{QStringLiteral("serverUrl"), QStringLiteral("https://music.example.invalid")},
             {QStringLiteral("username"), QStringLiteral("test-user")}},
            QByteArrayLiteral("test-secret")};
}

class ControllableSession final : public IMusicSourceSession {
public:
    ControllableSession(QStringList *events, QObject *parent)
        : IMusicSourceSession(parent)
        , m_events(events)
    {
    }

    ~ControllableSession() override
    {
        m_events->append(QStringLiteral("destroyed"));
    }

    QUuid search(const SearchQuery &) override { return {}; }
    QUuid browse(const BrowseQuery &) override { return {}; }
    QUuid resolveStream(const TrackRef &) override { return {}; }
    QUuid fetchArtwork(const TrackRef &) override { return {}; }
    QUuid fetchLyrics(const TrackRef &) override { return {}; }

    void cancel(const QUuid &requestId) override
    {
        m_events->append(QStringLiteral("cancel:%1").arg(requestId.toString()));
    }

private:
    QStringList *m_events = nullptr;
};

ControllableSession *installControllableSession(SourceSessionRegistry *registry, const MediaId &id,
                                                QStringList *events)
{
    auto *session = new ControllableSession(events, registry);
    registry->m_sessions.insert(registry->keyFor(id), {session, {}});
    return session;
}

void assertCancellationPrecedesDestruction(const QStringList &events, const QUuid &requestId)
{
    const QString cancellation = QStringLiteral("cancel:%1").arg(requestId.toString());
    QCOMPARE(events, QStringList({cancellation, QStringLiteral("destroyed")}));
}

class RegistryHarness {
public:
    RegistryHarness()
        : settings(temporaryDirectory.filePath(QStringLiteral("accounts.ini")), QSettings::IniFormat)
        , accountStore(&settings, &secretStore)
        , sourceManager(&pluginManager)
        , registry(&sourceManager, &accountStore)
    {
        pluginManager.addSearchPath(QStringLiteral(QUEMUSIC_TEST_PLUGIN_PACKAGE_DIR));
    }

    bool initialize()
    {
        return temporaryDirectory.isValid() && sourceManager.loadAll() == 1
            && accountStore.upsert(testAccount());
    }

    QTemporaryDir temporaryDirectory;
    QSettings settings;
    MemorySecretStore secretStore;
    SourceAccountStore accountStore;
    PluginManager pluginManager;
    SourceManager sourceManager;
    SourceSessionRegistry registry;
};

}

class SourceSessionRegistryTest : public QObject {
    Q_OBJECT

private slots:
    void reusesSessionForEnabledAccount();
    void enabledAccountsExcludesDisabledStoredAccounts();
    void disableCancelsTrackedRequestBeforeDestroyingSession();
    void removeDoesNotReenableDisabledAccount();
    void removeReleasesSessionBeforeSourcePackageUnload();
    void destructionCancelsTrackedRequestBeforeDestroyingSession();
};

void SourceSessionRegistryTest::reusesSessionForEnabledAccount()
{
    RegistryHarness harness;
    QVERIFY(harness.initialize());

    IMusicSourceSession *first = harness.registry.sessionFor({QStringLiteral("test-source"),
                                                               QStringLiteral("home")});

    QVERIFY(first != nullptr);
    QCOMPARE(harness.registry.sessionFor({QStringLiteral("test-source"),
                                          QStringLiteral("home")}),
             first);
}

void SourceSessionRegistryTest::enabledAccountsExcludesDisabledStoredAccounts()
{
    RegistryHarness harness;
    QVERIFY(harness.initialize());
    QVERIFY(harness.accountStore.upsert(testAccount(QStringLiteral("disabled")), false));

    const QList<StoredSourceAccount> accounts = harness.registry.enabledAccounts();

    QCOMPARE(accounts.size(), 1);
    QCOMPARE(accounts.constFirst().sourceId, QStringLiteral("test-source"));
    QCOMPARE(accounts.constFirst().accountId, QStringLiteral("home"));
}

void SourceSessionRegistryTest::disableCancelsTrackedRequestBeforeDestroyingSession()
{
    SourceSessionRegistry registry(nullptr, nullptr);
    const MediaId id{QStringLiteral("test-source"), QStringLiteral("home")};
    QStringList events;
    auto *session = installControllableSession(&registry, id, &events);
    QPointer<IMusicSourceSession> guardedSession(session);
    QSignalSpy destroyed(session, &QObject::destroyed);
    const QUuid requestId = QUuid::createUuid();

    registry.trackRequest(id, requestId);
    registry.disable(id.sourceId, id.accountId);

    QCOMPARE(destroyed.count(), 1);
    QVERIFY(guardedSession.isNull());
    assertCancellationPrecedesDestruction(events, requestId);
    QVERIFY(registry.sessionFor(id) == nullptr);
}

void SourceSessionRegistryTest::removeDoesNotReenableDisabledAccount()
{
    RegistryHarness harness;
    QVERIFY(harness.initialize());
    QVERIFY(harness.registry.sessionFor({QStringLiteral("test-source"),
                                         QStringLiteral("home")}) != nullptr);

    harness.registry.disable(QStringLiteral("test-source"), QStringLiteral("home"));
    harness.registry.remove(QStringLiteral("test-source"), QStringLiteral("home"));

    QVERIFY(harness.registry.sessionFor({QStringLiteral("test-source"),
                                         QStringLiteral("home")}) == nullptr);
}

void SourceSessionRegistryTest::removeReleasesSessionBeforeSourcePackageUnload()
{
    RegistryHarness harness;
    QVERIFY(harness.initialize());
    QVERIFY(harness.accountStore.upsert(testAccount(QStringLiteral("lease"))));
    IMusicSourceSession *leasedSession = harness.registry.sessionFor(
        {QStringLiteral("test-source"), QStringLiteral("lease")});
    QVERIFY(leasedSession != nullptr);
    QPointer<IMusicSourceSession> guardedLeasedSession(leasedSession);
    harness.registry.remove(QStringLiteral("test-source"), QStringLiteral("lease"));
    QVERIFY(guardedLeasedSession.isNull());

    const MediaId id{QStringLiteral("test-source"), QStringLiteral("home")};
    QStringList events;
    auto *session = installControllableSession(&harness.registry, id, &events);
    QPointer<IMusicSourceSession> guardedSession(session);
    QSignalSpy destroyed(session, &QObject::destroyed);
    const QUuid requestId = QUuid::createUuid();

    harness.registry.trackRequest(id, requestId);
    harness.registry.remove(id.sourceId, id.accountId);

    QCOMPARE(destroyed.count(), 1);
    QVERIFY(guardedSession.isNull());
    assertCancellationPrecedesDestruction(events, requestId);
    QCOMPARE(harness.pluginManager.unload(QStringLiteral("org.quemusic.source.fixture")),
             PluginOperationResult::Success);
    QCOMPARE(harness.sourceManager.sourceIds(), QStringList());
    QVERIFY(harness.registry.sessionFor({QStringLiteral("test-source"),
                                         QStringLiteral("home")}) == nullptr);
    QVERIFY(guardedSession.isNull());
}

void SourceSessionRegistryTest::destructionCancelsTrackedRequestBeforeDestroyingSession()
{
    auto *registry = new SourceSessionRegistry(nullptr, nullptr);
    const MediaId id{QStringLiteral("test-source"), QStringLiteral("home")};
    QStringList events;
    auto *session = installControllableSession(registry, id, &events);
    QPointer<IMusicSourceSession> guardedSession(session);
    QSignalSpy destroyed(session, &QObject::destroyed);
    const QUuid requestId = QUuid::createUuid();

    registry->trackRequest(id, requestId);
    delete registry;

    QCOMPARE(destroyed.count(), 1);
    QVERIFY(guardedSession.isNull());
    assertCancellationPrecedesDestruction(events, requestId);
}

QTEST_MAIN(SourceSessionRegistryTest)
#include "tst_SourceSessionRegistry.moc"
