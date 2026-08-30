#include "SourceAccountStore.h"
#include "SourceManager.h"
#include "SourceSessionRegistry.h"

#include "PluginManager.h"

#include <QHash>
#include <QPointer>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

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
    RegistryHarness harness;
    QVERIFY(harness.initialize());
    IMusicSourceSession *session = harness.registry.sessionFor(
        {QStringLiteral("test-source"), QStringLiteral("home")});
    QVERIFY(session != nullptr);
    QPointer<IMusicSourceSession> guardedSession(session);
    QSignalSpy destroyed(session, &QObject::destroyed);
    QSignalSpy succeeded(session, &IMusicSourceSession::requestSucceeded);
    QSignalSpy invalidated(&harness.registry, &SourceSessionRegistry::sessionInvalidated);

    const QUuid requestId = session->search({QStringLiteral("anything"), 1});
    harness.registry.trackRequest({QStringLiteral("test-source"), QStringLiteral("home")}, requestId);
    harness.registry.disable(QStringLiteral("test-source"), QStringLiteral("home"));

    QCOMPARE(destroyed.count(), 1);
    QVERIFY(guardedSession.isNull());
    QCOMPARE(invalidated.count(), 1);
    QCOMPARE(invalidated.constFirst().at(0).toString(), QStringLiteral("test-source"));
    QCOMPARE(invalidated.constFirst().at(1).toString(), QStringLiteral("home"));
    QTest::qWait(50);
    QCOMPARE(succeeded.count(), 0);
    QVERIFY(harness.registry.sessionFor({QStringLiteral("test-source"),
                                         QStringLiteral("home")}) == nullptr);
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
    IMusicSourceSession *session = harness.registry.sessionFor(
        {QStringLiteral("test-source"), QStringLiteral("home")});
    QVERIFY(session != nullptr);
    QPointer<IMusicSourceSession> guardedSession(session);
    QSignalSpy destroyed(session, &QObject::destroyed);
    QSignalSpy invalidated(&harness.registry, &SourceSessionRegistry::sessionInvalidated);

    const QUuid requestId = session->search({QStringLiteral("anything"), 1});
    harness.registry.trackRequest({QStringLiteral("test-source"), QStringLiteral("home")}, requestId);
    harness.registry.remove(QStringLiteral("test-source"), QStringLiteral("home"));

    QCOMPARE(destroyed.count(), 1);
    QVERIFY(guardedSession.isNull());
    QCOMPARE(invalidated.count(), 1);
    QCOMPARE(harness.pluginManager.unload(QStringLiteral("org.quemusic.source.fixture")),
             PluginOperationResult::Success);
    QCOMPARE(harness.sourceManager.sourceIds(), QStringList());
    QVERIFY(harness.registry.sessionFor({QStringLiteral("test-source"),
                                         QStringLiteral("home")}) == nullptr);
    QVERIFY(guardedSession.isNull());
}

void SourceSessionRegistryTest::destructionCancelsTrackedRequestBeforeDestroyingSession()
{
    RegistryHarness harness;
    QVERIFY(harness.initialize());
    auto *registry = new SourceSessionRegistry(&harness.sourceManager, &harness.accountStore);
    IMusicSourceSession *session = registry->sessionFor(
        {QStringLiteral("test-source"), QStringLiteral("home")});
    QVERIFY(session != nullptr);
    QPointer<IMusicSourceSession> guardedSession(session);
    QSignalSpy destroyed(session, &QObject::destroyed);
    QSignalSpy succeeded(session, &IMusicSourceSession::requestSucceeded);

    const QUuid requestId = session->search({QStringLiteral("anything"), 1});
    registry->trackRequest({QStringLiteral("test-source"), QStringLiteral("home")}, requestId);
    delete registry;

    QCOMPARE(destroyed.count(), 1);
    QVERIFY(guardedSession.isNull());
    QTest::qWait(50);
    QCOMPARE(succeeded.count(), 0);
}

QTEST_MAIN(SourceSessionRegistryTest)
#include "tst_SourceSessionRegistry.moc"
