#if defined(QUEMUSIC_SOURCE_REGISTRY_V2_FIXTURE)

#include "v2/IMusicSourcePluginV2.h"
#include "v2/ISourceProvidersV2.h"

#include <QObject>
#include <QStringList>
#include <QVariantMap>

class RegistryV2FixturePlugin final : public QObject, public IMusicSourcePluginV2 {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID QUEMUSIC_MUSIC_SOURCE_PLUGIN_V2_IID)
    Q_INTERFACES(IMusicSourcePluginV2)
    Q_PROPERTY(int sourceSdkAbi READ sourceSdkAbi CONSTANT)
    Q_PROPERTY(int createCount READ createCount NOTIFY fixtureChanged)
    Q_PROPERTY(QStringList events READ events NOTIFY fixtureChanged)
    Q_PROPERTY(QVariantMap lastConfiguration READ lastConfiguration NOTIFY fixtureChanged)

public:
    int sourceSdkAbi() const { return QUEMUSIC_MUSIC_SOURCE_SDK_V2_ABI; }
    int createCount() const { return m_createCount; }
    QStringList events() const { return m_events; }
    QVariantMap lastConfiguration() const { return m_lastConfiguration; }

    SourceDescriptorV2 descriptor() const override;
    IMusicSourceSessionV2 *createSession(const SourceConfigurationV2 &configuration,
                                         QObject *parent) override;

    void record(const QString &event)
    {
        m_events.append(event);
        emit fixtureChanged();
    }

    Q_INVOKABLE void clearEvents()
    {
        m_events.clear();
        emit fixtureChanged();
    }

signals:
    void fixtureChanged();
    void sessionCreated(QObject *session);

private:
    int m_createCount = 0;
    QStringList m_events;
    QVariantMap m_lastConfiguration;
};

class RegistryV2FixtureSession final : public IMusicSourceSessionV2,
                                       public IPageProviderV2 {
    Q_OBJECT
    Q_INTERFACES(IPageProviderV2)

public:
    RegistryV2FixtureSession(const SourceConfigurationV2 &configuration,
                             RegistryV2FixturePlugin *plugin, QObject *parent)
        : IMusicSourceSessionV2(parent)
        , m_configuration(configuration)
        , m_plugin(plugin)
    {
    }

    ~RegistryV2FixtureSession() override
    {
        m_plugin->record(event(QStringLiteral("destroy")));
    }

    SourceIdentityV2 identity() const override
    {
        return {m_configuration.sourceId, m_configuration.sourceInstanceId,
                m_configuration.accountId, m_configuration.displayName};
    }

    SourceSessionStateV2 state() const override { return m_state; }
    CapabilitySetV2 capabilities() const override { return {}; }

    QUuid open() override
    {
        m_state = SourceSessionStateV2::Connecting;
        const QUuid requestId = QUuid::createUuid();
        m_plugin->record(event(QStringLiteral("open"), requestId));
        emit requestStarted(requestId);
        if (m_configuration.accountId == QStringLiteral("sync-open")) {
            m_plugin->record(event(QStringLiteral("finish"), requestId));
            emit requestFailed(requestId, {});
        }
        emit stateChanged(m_state);
        return requestId;
    }

    void close() override
    {
        m_plugin->record(event(QStringLiteral("close")));
        emit stateChanged(SourceSessionStateV2::Closing);
        m_state = SourceSessionStateV2::Closed;
    }

    void cancel(const QUuid &requestId) override
    {
        m_plugin->record(event(QStringLiteral("cancel"), requestId));
        emit stateChanged(SourceSessionStateV2::Closing);
    }

    QUuid fetchPage(const PageQueryV2 &query) override
    {
        Q_UNUSED(query)
        const QUuid requestId = QUuid::createUuid();
        m_plugin->record(event(QStringLiteral("request"), requestId));
        emit requestStarted(requestId);
        return requestId;
    }

    Q_INVOKABLE QString beginObservableRequest()
    {
        const QUuid requestId = QUuid::createUuid();
        m_plugin->record(event(QStringLiteral("request"), requestId));
        emit requestStarted(requestId);
        return requestId.toString(QUuid::WithoutBraces);
    }

    Q_INVOKABLE void finishRequest(const QString &request, int terminalSignal)
    {
        const QUuid requestId(request);
        m_plugin->record(event(QStringLiteral("finish"), requestId));
        switch (terminalSignal) {
        case 0:
            emit pageReady(requestId, {});
            break;
        case 1:
            emit streamReady(requestId, {});
            break;
        case 2:
            emit actionCompleted(requestId, {});
            break;
        default:
            emit requestFailed(requestId, {});
            break;
        }
    }

private:
    QString event(const QString &kind, const QUuid &requestId = {}) const
    {
        QString value = kind + QLatin1Char(':') + m_configuration.sourceInstanceId;
        if (!requestId.isNull()) {
            value += QLatin1Char(':') + requestId.toString(QUuid::WithoutBraces);
        }
        return value;
    }

    SourceConfigurationV2 m_configuration;
    RegistryV2FixturePlugin *m_plugin = nullptr;
    SourceSessionStateV2 m_state = SourceSessionStateV2::Closed;
};

SourceDescriptorV2 RegistryV2FixturePlugin::descriptor() const
{
    SourceDescriptorV2 descriptor;
    descriptor.pluginPackageId = QStringLiteral(QUEMUSIC_SOURCE_REGISTRY_FIXTURE_PACKAGE_ID);
    descriptor.sourceId = QStringLiteral(QUEMUSIC_SOURCE_REGISTRY_FIXTURE_SOURCE_ID);
    descriptor.name = QStringLiteral("Registry V2 Fixture");
    descriptor.version = QStringLiteral("2.0.0");
    descriptor.sdkAbi = QUEMUSIC_MUSIC_SOURCE_SDK_V2_ABI;
    return descriptor;
}

IMusicSourceSessionV2 *RegistryV2FixturePlugin::createSession(
    const SourceConfigurationV2 &configuration, QObject *parent)
{
    ++m_createCount;
    m_lastConfiguration = {
        {QStringLiteral("pluginPackageId"), configuration.pluginPackageId},
        {QStringLiteral("sourceId"), configuration.sourceId},
        {QStringLiteral("sourceInstanceId"), configuration.sourceInstanceId},
        {QStringLiteral("accountId"), configuration.accountId},
        {QStringLiteral("displayName"), configuration.displayName},
        {QStringLiteral("parameters"), configuration.parameters},
        {QStringLiteral("secretSize"), configuration.secret.size()},
    };
    record(QStringLiteral("create:") + configuration.sourceInstanceId);
    QObject *sessionParent = configuration.accountId == QStringLiteral("ignored-parent")
        ? nullptr : parent;
    auto *session = new RegistryV2FixtureSession(configuration, this, sessionParent);
    emit sessionCreated(session);
    return session;
}

#include "tst_SourceRegistryV2.moc"

#else

#include "SourceRegistry.h"

#include "PluginManager.h"
#include "SourceAccountStore.h"
#include "v2/ISourceProvidersV2.h"

#include <QEvent>
#include <QHash>
#include <QMetaObject>
#include <QPointer>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <utility>

namespace {

const QString kPackageId = QStringLiteral("org.quemusic.source.registry-v2");
const QString kSourceId = QStringLiteral("registry-v2");
const QString kMismatchPackageId =
    QStringLiteral("org.quemusic.source.registry-v2-mismatch");
const QString kSecondPackageId =
    QStringLiteral("org.quemusic.source.registry-v2-secondary");
const QString kSecondSourceId = QStringLiteral("registry-secondary");

class TestSecretStore final : public ISecretStore {
public:
    bool write(const QString &reference, const QByteArray &secret, QString *error) override
    {
        if (reference.isEmpty()) {
            if (error) {
                *error = QStringLiteral("Secret reference is required");
            }
            return false;
        }
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

SourceAccount sourceAccount(const QString &sourceId, const QString &accountId,
                            const QString &displayName)
{
    return {sourceId,
            accountId,
            displayName,
            {{QStringLiteral("serverUrl"), QStringLiteral("https://fixture.example.invalid")},
             {QStringLiteral("username"), QStringLiteral("fixture-user")}},
            QByteArrayLiteral("fixture-secret")};
}

class RegistryHarness {
public:
    RegistryHarness()
        : settings(temporaryDirectory.filePath(QStringLiteral("accounts.ini")),
                   QSettings::IniFormat)
        , accountStore(&settings, &secretStore)
        , registry(&pluginManager, &accountStore)
    {
    }

    bool loadValidPlugin()
    {
        pluginManager.addSearchPath(QStringLiteral(QUEMUSIC_TEST_REGISTRY_V2_PLUGIN_PACKAGE_DIR));
        return pluginManager.discover() == 1 && pluginManager.load(kPackageId);
    }

    bool loadMismatchPlugin()
    {
        pluginManager.addSearchPath(
            QStringLiteral(QUEMUSIC_TEST_REGISTRY_V2_MISMATCH_PLUGIN_PACKAGE_DIR));
        return pluginManager.discover() == 1 && pluginManager.load(kMismatchPackageId);
    }

    bool loadTwoValidPlugins()
    {
        pluginManager.addSearchPath(QStringLiteral(QUEMUSIC_TEST_REGISTRY_V2_PLUGIN_PACKAGE_DIR));
        pluginManager.addSearchPath(
            QStringLiteral(QUEMUSIC_TEST_REGISTRY_V2_SECOND_PLUGIN_PACKAGE_DIR));
        return pluginManager.discover() == 2 && pluginManager.load(kPackageId)
            && pluginManager.load(kSecondPackageId);
    }

    bool loadV1Plugin()
    {
        pluginManager.addSearchPath(QStringLiteral(QUEMUSIC_TEST_PLUGIN_PACKAGE_DIR));
        return pluginManager.discover() == 1
            && pluginManager.load(QStringLiteral("org.quemusic.source.fixture"));
    }

    bool saveAccount(const QString &accountId, const QString &displayName,
                     bool enabled = true, const QString &sourceId = kSourceId)
    {
        return accountStore.upsert(sourceAccount(sourceId, accountId, displayName), enabled);
    }

    QObject *validPluginObject() const
    {
        return pluginManager.pluginInstance(kPackageId);
    }

    QTemporaryDir temporaryDirectory;
    QSettings settings;
    TestSecretStore secretStore;
    SourceAccountStore accountStore;
    PluginManager pluginManager;
    SourceRegistry registry;
};

class SessionLeaseObserver final : public QObject {
    Q_OBJECT

public:
    SessionLeaseObserver(PluginManager *plugins, QString packageId)
        : m_plugins(plugins)
        , m_packageId(std::move(packageId))
    {
    }

    int captureCount = 0;
    int leasesAtDestruction = -1;
    QPointer<QObject> capturedSession;

public slots:
    void capture(QObject *session)
    {
        ++captureCount;
        capturedSession = session;
        connect(session, &QObject::destroyed, this, [this] {
            leasesAtDestruction = m_plugins->plugin(m_packageId).activeLeases;
        });
    }

private:
    PluginManager *m_plugins = nullptr;
    QString m_packageId;
};

class CreationCallbackObserver final : public QObject {
    Q_OBJECT

public:
    CreationCallbackObserver(SourceRegistry *registry, PluginManager *plugins,
                             QString packageId)
        : m_registry(registry)
        , m_plugins(plugins)
        , m_packageId(std::move(packageId))
    {
    }

    int callbackCount = 0;
    int leasesAtDestruction = -1;
    QPointer<QObject> capturedSession;

public slots:
    void closeAllOnCreation(QObject *session)
    {
        ++callbackCount;
        capturedSession = session;
        connect(session, &QObject::destroyed, this, [this] {
            leasesAtDestruction = m_plugins->plugin(m_packageId).activeLeases;
        });
        m_registry->closeAll();
    }

private:
    SourceRegistry *m_registry = nullptr;
    PluginManager *m_plugins = nullptr;
    QString m_packageId;
};

SourceInstanceDescriptorV2 descriptorFor(const QList<SourceInstanceDescriptorV2> &descriptors,
                                         const QString &sourceInstanceId)
{
    for (const SourceInstanceDescriptorV2 &descriptor : descriptors) {
        if (descriptor.sourceInstanceId == sourceInstanceId) {
            return descriptor;
        }
    }
    return {};
}

QString startObservableRequest(QObject *session)
{
    QString requestId;
    const bool invoked = QMetaObject::invokeMethod(
        session, "beginObservableRequest", Q_RETURN_ARG(QString, requestId));
    return invoked ? requestId : QString();
}

bool finishRequest(QObject *session, const QString &requestId, int terminalSignal)
{
    return QMetaObject::invokeMethod(session, "finishRequest", Q_ARG(QString, requestId),
                                     Q_ARG(int, terminalSignal));
}

QString cancelEvent(const QString &sourceInstanceId, const QString &requestId)
{
    return QStringLiteral("cancel:") + sourceInstanceId + QLatin1Char(':') + requestId;
}

}

class SourceRegistryV2Test : public QObject {
    Q_OBJECT

private slots:
    void twoAccountsCreateDistinctStableInstances();
    void slashContainingAccountIdHasUnambiguousIdentity();
    void twoLoadedSourcePackagesMapToTheirOwnAccounts();
    void disabledInstanceRemainsDescribableAndCannotCreateSession();
    void missingSecretPreventsSessionAndLeaseCreation();
    void pluginDescriptorSourceMismatchPreventsSession();
    void v1PluginIsNeverReinterpretedAsV2();
    void sessionCreationIsLazyAndReusesTheLiveSession();
    void creationReservationPreventsReentrantAcquireRecursion();
    void closeInstanceDuringAcquireInvalidatesCreation();
    void closeAllDuringAcquireInvalidatesCreation();
    void disableDuringAcquireInvalidatesCreation();
    void registryDestructionDuringAcquireInvalidatesCreation();
    void closeAllDuringCreateSessionInvalidatesBeforeOpen();
    void terminalSignalsRemoveRequestsAndCloseCancelsFirst();
    void synchronousOpenTerminalDoesNotRemainActive();
    void closeRejectsReentrantCreationFromCancelCloseAndDestruction();
    void closeAllBlocksRecreationWhileDraining();
    void rejectsSessionThatIgnoresRegistryParent();
    void reparentedSessionIsRejectedAndClosed();
    void externalDestructionDefersLeaseReleaseAndRecreation();
    void registryDestructionTransfersDeferredLeaseToPluginManagerQueue();
    void disableClosesSessionBeforeReleasingLease();
    void destructionClosesSessionBeforeReleasingLease();
    void closeAllIsIdempotent();
};

void SourceRegistryV2Test::twoAccountsCreateDistinctStableInstances()
{
    RegistryHarness harness;
    QVERIFY(harness.loadValidPlugin());
    QVERIFY(harness.saveAccount(QStringLiteral("office"), QStringLiteral("Office")));
    QVERIFY(harness.saveAccount(QStringLiteral("home"), QStringLiteral("Home")));

    const QList<SourceInstanceDescriptorV2> instances = harness.registry.enabledInstances();
    QCOMPARE(instances.size(), 2);
    QCOMPARE(instances.at(0).sourceInstanceId, QStringLiteral("registry-v2/home"));
    QCOMPARE(instances.at(1).sourceInstanceId, QStringLiteral("registry-v2/office"));
    QCOMPARE(instances.at(0).accountId, QStringLiteral("home"));
    QCOMPARE(instances.at(1).accountId, QStringLiteral("office"));
    QCOMPARE(instances.at(0).pluginPackageId, kPackageId);
    QCOMPARE(instances.at(1).pluginPackageId, kPackageId);
}

void SourceRegistryV2Test::slashContainingAccountIdHasUnambiguousIdentity()
{
    RegistryHarness harness;
    QVERIFY(harness.loadValidPlugin());
    QVERIFY(harness.saveAccount(QStringLiteral("team/home"), QStringLiteral("Team Home")));

    const QList<SourceInstanceDescriptorV2> instances = harness.registry.enabledInstances();
    QCOMPARE(instances.size(), 1);
    QCOMPARE(instances.constFirst().sourceId, kSourceId);
    QCOMPARE(instances.constFirst().accountId, QStringLiteral("team/home"));
    QCOMPARE(instances.constFirst().sourceInstanceId,
             QStringLiteral("registry-v2/team/home"));
    QVERIFY(harness.registry.sessionFor(QStringLiteral("registry-v2/team/home")) != nullptr);
}

void SourceRegistryV2Test::twoLoadedSourcePackagesMapToTheirOwnAccounts()
{
    RegistryHarness harness;
    QVERIFY(harness.loadTwoValidPlugins());
    QVERIFY(harness.saveAccount(QStringLiteral("home"), QStringLiteral("Primary")));
    QVERIFY(harness.saveAccount(QStringLiteral("home"), QStringLiteral("Secondary"), true,
                                kSecondSourceId));

    const SourceInstanceDescriptorV2 primary = descriptorFor(
        harness.registry.enabledInstances(), QStringLiteral("registry-v2/home"));
    const SourceInstanceDescriptorV2 secondary = descriptorFor(
        harness.registry.enabledInstances(), QStringLiteral("registry-secondary/home"));
    QCOMPARE(primary.pluginPackageId, kPackageId);
    QCOMPARE(primary.sourceId, kSourceId);
    QCOMPARE(secondary.pluginPackageId, kSecondPackageId);
    QCOMPARE(secondary.sourceId, kSecondSourceId);
    QVERIFY(harness.registry.sessionFor(primary.sourceInstanceId) != nullptr);
    QVERIFY(harness.registry.sessionFor(secondary.sourceInstanceId) != nullptr);
    QCOMPARE(harness.pluginManager.plugin(kPackageId).activeLeases, 1);
    QCOMPARE(harness.pluginManager.plugin(kSecondPackageId).activeLeases, 1);
}

void SourceRegistryV2Test::disabledInstanceRemainsDescribableAndCannotCreateSession()
{
    RegistryHarness harness;
    QVERIFY(harness.loadValidPlugin());
    QVERIFY(harness.saveAccount(QStringLiteral("home"), QStringLiteral("Home"), false));
    QSignalSpy changed(&harness.registry, &SourceRegistry::instanceChanged);

    const SourceInstanceDescriptorV2 disabled =
        descriptorFor(harness.registry.enabledInstances(), QStringLiteral("registry-v2/home"));
    QCOMPARE(disabled.sourceInstanceId, QStringLiteral("registry-v2/home"));
    QVERIFY(!disabled.enabled);
    QVERIFY(harness.registry.sessionFor(disabled.sourceInstanceId) == nullptr);
    QCOMPARE(harness.pluginManager.plugin(kPackageId).activeLeases, 0);

    QVERIFY(harness.registry.enableInstance(disabled.sourceInstanceId));
    QCOMPARE(changed.count(), 1);
    IMusicSourceSessionV2 *session = harness.registry.sessionFor(disabled.sourceInstanceId);
    QVERIFY(session != nullptr);
    QPointer<IMusicSourceSessionV2> guardedSession(session);
    int leasesAtDestruction = -1;
    connect(session, &QObject::destroyed, this, [&] {
        leasesAtDestruction = harness.pluginManager.plugin(kPackageId).activeLeases;
    });
    changed.clear();

    QVERIFY(harness.registry.disableInstance(disabled.sourceInstanceId));
    QCOMPARE(changed.count(), 1);
    QVERIFY(guardedSession.isNull());
    QCOMPARE(leasesAtDestruction, 1);
    QCOMPARE(harness.pluginManager.plugin(kPackageId).activeLeases, 0);
    QVERIFY(!descriptorFor(harness.registry.enabledInstances(), disabled.sourceInstanceId).enabled);
    QVERIFY(harness.registry.sessionFor(disabled.sourceInstanceId) == nullptr);

    QVERIFY(harness.registry.disableInstance(disabled.sourceInstanceId));
    QCOMPARE(changed.count(), 1);
}

void SourceRegistryV2Test::missingSecretPreventsSessionAndLeaseCreation()
{
    RegistryHarness harness;
    QVERIFY(harness.loadValidPlugin());
    QVERIFY(harness.saveAccount(QStringLiteral("home"), QStringLiteral("Home")));
    const QString reference = harness.accountStore.secretReference(kSourceId,
                                                                    QStringLiteral("home"));
    QVERIFY(harness.secretStore.remove(reference, nullptr));

    QVERIFY(harness.registry.sessionFor(QStringLiteral("registry-v2/home")) == nullptr);
    QCOMPARE(harness.pluginManager.plugin(kPackageId).activeLeases, 0);
    QCOMPARE(harness.validPluginObject()->property("createCount").toInt(), 0);
}

void SourceRegistryV2Test::pluginDescriptorSourceMismatchPreventsSession()
{
    RegistryHarness harness;
    QVERIFY(harness.loadMismatchPlugin());
    QVERIFY(harness.saveAccount(QStringLiteral("home"), QStringLiteral("Home"), true,
                                QStringLiteral("registry-v2-mismatch")));

    QVERIFY(harness.registry.sessionFor(QStringLiteral("registry-v2-mismatch/home")) == nullptr);
    QCOMPARE(harness.pluginManager.plugin(kMismatchPackageId).activeLeases, 0);
    QCOMPARE(harness.pluginManager.pluginInstance(kMismatchPackageId)
                 ->property("createCount").toInt(), 0);
}

void SourceRegistryV2Test::v1PluginIsNeverReinterpretedAsV2()
{
    RegistryHarness harness;
    QVERIFY(harness.loadV1Plugin());
    QVERIFY(harness.saveAccount(QStringLiteral("home"), QStringLiteral("Home"), true,
                                QStringLiteral("test-source")));

    QVERIFY(harness.registry.sessionFor(QStringLiteral("test-source/home")) == nullptr);
    QCOMPARE(harness.pluginManager.plugin(QStringLiteral("org.quemusic.source.fixture"))
                 .activeLeases, 0);
}

void SourceRegistryV2Test::sessionCreationIsLazyAndReusesTheLiveSession()
{
    RegistryHarness harness;
    QVERIFY(harness.loadValidPlugin());
    QVERIFY(harness.saveAccount(QStringLiteral("home"), QStringLiteral("Home")));
    QCOMPARE(harness.validPluginObject()->property("createCount").toInt(), 0);

    IMusicSourceSessionV2 *first =
        harness.registry.sessionFor(QStringLiteral("registry-v2/home"));
    QVERIFY(first != nullptr);
    QCOMPARE(harness.registry.sessionFor(QStringLiteral("registry-v2/home")), first);
    QCOMPARE(harness.validPluginObject()->property("createCount").toInt(), 1);
    QCOMPARE(harness.pluginManager.plugin(kPackageId).activeLeases, 1);
    QCOMPARE(first->identity().sourceInstanceId, QStringLiteral("registry-v2/home"));
    QCOMPARE(first->identity().accountId, QStringLiteral("home"));

    const QVariantMap configuration =
        harness.validPluginObject()->property("lastConfiguration").toMap();
    QCOMPARE(configuration.value(QStringLiteral("pluginPackageId")).toString(), kPackageId);
    QCOMPARE(configuration.value(QStringLiteral("sourceId")).toString(), kSourceId);
    QCOMPARE(configuration.value(QStringLiteral("sourceInstanceId")).toString(),
             QStringLiteral("registry-v2/home"));
    QCOMPARE(configuration.value(QStringLiteral("accountId")).toString(),
             QStringLiteral("home"));
    QCOMPARE(configuration.value(QStringLiteral("secretSize")).toInt(),
             QByteArrayLiteral("fixture-secret").size());
    QVERIFY(!configuration.contains(QStringLiteral("secret")));
}

void SourceRegistryV2Test::creationReservationPreventsReentrantAcquireRecursion()
{
    RegistryHarness harness;
    QVERIFY(harness.loadValidPlugin());
    QVERIFY(harness.saveAccount(QStringLiteral("home"), QStringLiteral("Home")));
    const QString instanceId = QStringLiteral("registry-v2/home");
    IMusicSourceSessionV2 *reentrantSession = nullptr;
    bool handledAcquire = false;
    connect(&harness.pluginManager, &PluginManager::pluginChanged, this,
            [&](const QString &packageId) {
                if (handledAcquire || packageId != kPackageId) {
                    return;
                }
                handledAcquire = true;
                reentrantSession = harness.registry.sessionFor(instanceId);
            });

    IMusicSourceSessionV2 *session = harness.registry.sessionFor(instanceId);

    QVERIFY(handledAcquire);
    QVERIFY(reentrantSession == nullptr);
    QVERIFY(session != nullptr);
    QCOMPARE(harness.validPluginObject()->property("createCount").toInt(), 1);
    QCOMPARE(session->parent(), &harness.registry);
    QCOMPARE(harness.registry.children().count(session), 1);
    QCOMPARE(harness.pluginManager.plugin(kPackageId).activeLeases, 1);
}

void SourceRegistryV2Test::closeInstanceDuringAcquireInvalidatesCreation()
{
    RegistryHarness harness;
    QVERIFY(harness.loadValidPlugin());
    QVERIFY(harness.saveAccount(QStringLiteral("home"), QStringLiteral("Home")));
    const QString instanceId = QStringLiteral("registry-v2/home");
    bool handledAcquire = false;
    connect(&harness.pluginManager, &PluginManager::pluginChanged, this,
            [&](const QString &packageId) {
                if (handledAcquire || packageId != kPackageId) {
                    return;
                }
                handledAcquire = true;
                QVERIFY(harness.registry.closeInstance(instanceId));
            });

    QVERIFY(harness.registry.sessionFor(instanceId) == nullptr);
    QVERIFY(handledAcquire);
    QCOMPARE(harness.validPluginObject()->property("createCount").toInt(), 0);
    QVERIFY(harness.registry.children().isEmpty());
    QCOMPARE(harness.pluginManager.plugin(kPackageId).activeLeases, 0);
}

void SourceRegistryV2Test::closeAllDuringAcquireInvalidatesCreation()
{
    RegistryHarness harness;
    QVERIFY(harness.loadValidPlugin());
    QVERIFY(harness.saveAccount(QStringLiteral("home"), QStringLiteral("Home")));
    const QString instanceId = QStringLiteral("registry-v2/home");
    bool handledAcquire = false;
    connect(&harness.pluginManager, &PluginManager::pluginChanged, this,
            [&](const QString &packageId) {
                if (handledAcquire || packageId != kPackageId) {
                    return;
                }
                handledAcquire = true;
                harness.registry.closeAll();
            });

    QVERIFY(harness.registry.sessionFor(instanceId) == nullptr);
    QVERIFY(handledAcquire);
    QCOMPARE(harness.validPluginObject()->property("createCount").toInt(), 0);
    QVERIFY(harness.registry.children().isEmpty());
    QCOMPARE(harness.pluginManager.plugin(kPackageId).activeLeases, 0);
}

void SourceRegistryV2Test::disableDuringAcquireInvalidatesCreation()
{
    RegistryHarness harness;
    QVERIFY(harness.loadValidPlugin());
    QVERIFY(harness.saveAccount(QStringLiteral("home"), QStringLiteral("Home")));
    const QString instanceId = QStringLiteral("registry-v2/home");
    bool handledAcquire = false;
    connect(&harness.pluginManager, &PluginManager::pluginChanged, this,
            [&](const QString &packageId) {
                if (handledAcquire || packageId != kPackageId) {
                    return;
                }
                handledAcquire = true;
                QVERIFY(harness.registry.disableInstance(instanceId));
            });

    QVERIFY(harness.registry.sessionFor(instanceId) == nullptr);
    QVERIFY(handledAcquire);
    QCOMPARE(harness.validPluginObject()->property("createCount").toInt(), 0);
    QVERIFY(harness.registry.children().isEmpty());
    QCOMPARE(harness.pluginManager.plugin(kPackageId).activeLeases, 0);
    QVERIFY(!descriptorFor(harness.registry.enabledInstances(), instanceId).enabled);
}

void SourceRegistryV2Test::registryDestructionDuringAcquireInvalidatesCreation()
{
    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());
    QSettings settings(temporaryDirectory.filePath(QStringLiteral("accounts.ini")),
                       QSettings::IniFormat);
    TestSecretStore secretStore;
    SourceAccountStore accountStore(&settings, &secretStore);
    PluginManager pluginManager;
    pluginManager.addSearchPath(QStringLiteral(QUEMUSIC_TEST_REGISTRY_V2_PLUGIN_PACKAGE_DIR));
    QCOMPARE(pluginManager.discover(), 1);
    QVERIFY(pluginManager.load(kPackageId));
    QVERIFY(accountStore.upsert(sourceAccount(kSourceId, QStringLiteral("home"),
                                              QStringLiteral("Home"))));

    auto *registry = new SourceRegistry(&pluginManager, &accountStore);
    QPointer<SourceRegistry> guardedRegistry(registry);
    bool handledAcquire = false;
    connect(&pluginManager, &PluginManager::pluginChanged, this,
            [&](const QString &packageId) {
                if (handledAcquire || packageId != kPackageId) {
                    return;
                }
                handledAcquire = true;
                delete registry;
            });

    IMusicSourceSessionV2 *session =
        registry->sessionFor(QStringLiteral("registry-v2/home"));
    QVERIFY(handledAcquire);
    QVERIFY(guardedRegistry.isNull());
    QVERIFY(session == nullptr);
    QCOMPARE(pluginManager.pluginInstance(kPackageId)->property("createCount").toInt(), 0);
    QCOMPARE(pluginManager.plugin(kPackageId).activeLeases, 0);
}

void SourceRegistryV2Test::closeAllDuringCreateSessionInvalidatesBeforeOpen()
{
    RegistryHarness harness;
    QVERIFY(harness.loadValidPlugin());
    QVERIFY(harness.saveAccount(QStringLiteral("home"), QStringLiteral("Home")));
    CreationCallbackObserver observer(&harness.registry, &harness.pluginManager, kPackageId);
    QVERIFY(QObject::connect(harness.validPluginObject(), SIGNAL(sessionCreated(QObject*)),
                             &observer, SLOT(closeAllOnCreation(QObject*))));

    QVERIFY(harness.registry.sessionFor(QStringLiteral("registry-v2/home")) == nullptr);
    QCOMPARE(observer.callbackCount, 1);
    QVERIFY(observer.capturedSession.isNull());
    QCOMPARE(observer.leasesAtDestruction, 1);
    QCOMPARE(harness.pluginManager.plugin(kPackageId).activeLeases, 0);
    QVERIFY(harness.registry.children().isEmpty());
    const QStringList events = harness.validPluginObject()->property("events").toStringList();
    for (const QString &event : events) {
        QVERIFY(!event.startsWith(QStringLiteral("open:registry-v2/home:")));
    }
}

void SourceRegistryV2Test::terminalSignalsRemoveRequestsAndCloseCancelsFirst()
{
    RegistryHarness harness;
    QVERIFY(harness.loadValidPlugin());
    QVERIFY(harness.saveAccount(QStringLiteral("home"), QStringLiteral("Home")));
    const QString instanceId = QStringLiteral("registry-v2/home");
    IMusicSourceSessionV2 *session = harness.registry.sessionFor(instanceId);
    QVERIFY(session != nullptr);
    auto *pages = qobject_cast<IPageProviderV2 *>(session);
    QVERIFY(pages != nullptr);
    QCOMPARE(session->metaObject()->indexOfSignal("requestStarted(QUuid)"),
             IMusicSourceSessionV2::staticMetaObject.indexOfSignal("requestStarted(QUuid)"));

    QStringList completedRequests;
    for (int terminalSignal = 0; terminalSignal < 4; ++terminalSignal) {
        const QString requestId = startObservableRequest(session);
        QVERIFY(!requestId.isEmpty());
        completedRequests.append(requestId);
        QVERIFY(finishRequest(session, requestId, terminalSignal));
    }
    const QUuid pendingRequest = pages->fetchPage({});
    QVERIFY(!pendingRequest.isNull());
    const QString pending = pendingRequest.toString(QUuid::WithoutBraces);

    int leasesAtDestruction = -1;
    connect(session, &QObject::destroyed, this, [&] {
        leasesAtDestruction = harness.pluginManager.plugin(kPackageId).activeLeases;
    });
    QSignalSpy changed(&harness.registry, &SourceRegistry::instanceChanged);
    changed.clear();
    QVERIFY(harness.registry.closeInstance(instanceId));
    QCOMPARE(changed.count(), 1);
    QCOMPARE(leasesAtDestruction, 1);
    QCOMPARE(harness.pluginManager.plugin(kPackageId).activeLeases, 0);

    const QStringList events = harness.validPluginObject()->property("events").toStringList();
    const int closeIndex = events.indexOf(QStringLiteral("close:") + instanceId);
    const int destroyIndex = events.indexOf(QStringLiteral("destroy:") + instanceId);
    const int pendingCancelIndex = events.indexOf(cancelEvent(instanceId, pending));
    QVERIFY(pendingCancelIndex >= 0);
    QVERIFY(closeIndex > pendingCancelIndex);
    QVERIFY(destroyIndex > closeIndex);
    for (const QString &completed : completedRequests) {
        QVERIFY2(!events.contains(cancelEvent(instanceId, completed)),
                 qPrintable(QStringLiteral("Completed request was cancelled: ") + completed));
    }

    QVERIFY(harness.registry.closeInstance(instanceId));
    QCOMPARE(changed.count(), 1);
}

void SourceRegistryV2Test::synchronousOpenTerminalDoesNotRemainActive()
{
    RegistryHarness harness;
    QVERIFY(harness.loadValidPlugin());
    QVERIFY(harness.saveAccount(QStringLiteral("sync-open"), QStringLiteral("Sync Open")));
    const QString instanceId = QStringLiteral("registry-v2/sync-open");

    QVERIFY(harness.registry.sessionFor(instanceId) != nullptr);
    QVERIFY(harness.registry.closeInstance(instanceId));

    const QStringList events = harness.validPluginObject()->property("events").toStringList();
    QString openRequest;
    for (const QString &event : events) {
        if (event.startsWith(QStringLiteral("open:") + instanceId + QLatin1Char(':'))) {
            openRequest = event.section(QLatin1Char(':'), -1);
            break;
        }
    }
    QVERIFY(!openRequest.isEmpty());
    QVERIFY(!events.contains(cancelEvent(instanceId, openRequest)));
}

void SourceRegistryV2Test::closeRejectsReentrantCreationFromCancelCloseAndDestruction()
{
    RegistryHarness harness;
    QVERIFY(harness.loadValidPlugin());
    QVERIFY(harness.saveAccount(QStringLiteral("home"), QStringLiteral("Home")));
    const QString instanceId = QStringLiteral("registry-v2/home");
    IMusicSourceSessionV2 *session = harness.registry.sessionFor(instanceId);
    QVERIFY(session != nullptr);
    auto *pages = qobject_cast<IPageProviderV2 *>(session);
    QVERIFY(pages != nullptr);
    QVERIFY(!pages->fetchPage({}).isNull());

    QList<IMusicSourceSessionV2 *> reentrantResults;
    QStringList reentrantPhases;
    connect(session, &IMusicSourceSessionV2::stateChanged, this, [&] {
        const QStringList events = harness.validPluginObject()->property("events").toStringList();
        reentrantPhases.append(events.constLast().section(QLatin1Char(':'), 0, 0));
        reentrantResults.append(harness.registry.sessionFor(instanceId));
    });
    connect(session, &QObject::destroyed, this, [&] {
        reentrantPhases.append(QStringLiteral("destroy"));
        reentrantResults.append(harness.registry.sessionFor(instanceId));
    });

    QVERIFY(harness.registry.closeInstance(instanceId));
    QVERIFY(reentrantPhases.contains(QStringLiteral("cancel")));
    QVERIFY(reentrantPhases.contains(QStringLiteral("close")));
    QVERIFY(reentrantPhases.contains(QStringLiteral("destroy")));
    for (IMusicSourceSessionV2 *result : std::as_const(reentrantResults)) {
        QVERIFY(result == nullptr);
    }
    QCOMPARE(harness.validPluginObject()->property("createCount").toInt(), 1);
    QCOMPARE(harness.pluginManager.plugin(kPackageId).activeLeases, 0);
}

void SourceRegistryV2Test::closeAllBlocksRecreationWhileDraining()
{
    RegistryHarness harness;
    QVERIFY(harness.loadValidPlugin());
    QVERIFY(harness.saveAccount(QStringLiteral("home"), QStringLiteral("Home")));
    QVERIFY(harness.saveAccount(QStringLiteral("office"), QStringLiteral("Office")));
    const QString homeId = QStringLiteral("registry-v2/home");
    const QString officeId = QStringLiteral("registry-v2/office");
    IMusicSourceSessionV2 *home = harness.registry.sessionFor(homeId);
    QVERIFY(home != nullptr);
    QVERIFY(harness.registry.sessionFor(officeId) != nullptr);

    QList<IMusicSourceSessionV2 *> reentrantResults;
    connect(home, &IMusicSourceSessionV2::stateChanged, this, [&] {
        reentrantResults.append(harness.registry.sessionFor(homeId));
        reentrantResults.append(harness.registry.sessionFor(officeId));
    });

    harness.registry.closeAll();
    QVERIFY(!reentrantResults.isEmpty());
    for (IMusicSourceSessionV2 *result : std::as_const(reentrantResults)) {
        QVERIFY(result == nullptr);
    }
    QCOMPARE(harness.validPluginObject()->property("createCount").toInt(), 2);
    QCOMPARE(harness.pluginManager.plugin(kPackageId).activeLeases, 0);
}

void SourceRegistryV2Test::rejectsSessionThatIgnoresRegistryParent()
{
    RegistryHarness harness;
    QVERIFY(harness.loadValidPlugin());
    QVERIFY(harness.saveAccount(QStringLiteral("ignored-parent"),
                                QStringLiteral("Ignored Parent")));
    SessionLeaseObserver observer(&harness.pluginManager, kPackageId);
    QVERIFY(QObject::connect(harness.validPluginObject(), SIGNAL(sessionCreated(QObject*)),
                             &observer, SLOT(capture(QObject*))));

    QVERIFY(harness.registry.sessionFor(QStringLiteral("registry-v2/ignored-parent")) == nullptr);
    QCOMPARE(observer.captureCount, 1);
    QVERIFY(observer.capturedSession.isNull());
    QCOMPARE(observer.leasesAtDestruction, 1);
    QCOMPARE(harness.pluginManager.plugin(kPackageId).activeLeases, 0);
}

void SourceRegistryV2Test::reparentedSessionIsRejectedAndClosed()
{
    RegistryHarness harness;
    QVERIFY(harness.loadValidPlugin());
    QVERIFY(harness.saveAccount(QStringLiteral("home"), QStringLiteral("Home")));
    const QString instanceId = QStringLiteral("registry-v2/home");
    QPointer<IMusicSourceSessionV2> session = harness.registry.sessionFor(instanceId);
    QVERIFY(session != nullptr);
    session->setParent(nullptr);

    QVERIFY(harness.registry.sessionFor(instanceId) == nullptr);
    QVERIFY(session.isNull());
    QCOMPARE(harness.pluginManager.plugin(kPackageId).activeLeases, 0);
}

void SourceRegistryV2Test::externalDestructionDefersLeaseReleaseAndRecreation()
{
    RegistryHarness harness;
    QVERIFY(harness.loadValidPlugin());
    QVERIFY(harness.saveAccount(QStringLiteral("home"), QStringLiteral("Home")));
    const QString instanceId = QStringLiteral("registry-v2/home");
    IMusicSourceSessionV2 *session = harness.registry.sessionFor(instanceId);
    QVERIFY(session != nullptr);
    int leasesSeenFromDestroyed = -1;
    connect(session, &QObject::destroyed, this, [&] {
        leasesSeenFromDestroyed = harness.pluginManager.plugin(kPackageId).activeLeases;
    });
    QSignalSpy changed(&harness.registry, &SourceRegistry::instanceChanged);
    changed.clear();

    session->setParent(nullptr);
    delete session;

    QCOMPARE(leasesSeenFromDestroyed, 1);
    QCOMPARE(harness.pluginManager.plugin(kPackageId).activeLeases, 1);
    QVERIFY(harness.registry.sessionFor(instanceId) == nullptr);
    QCOMPARE(changed.count(), 0);
    QTRY_COMPARE(harness.pluginManager.plugin(kPackageId).activeLeases, 0);
    QCOMPARE(changed.count(), 1);

    QVERIFY(harness.registry.sessionFor(instanceId) != nullptr);
    QCOMPARE(harness.validPluginObject()->property("createCount").toInt(), 2);
}

void SourceRegistryV2Test::registryDestructionTransfersDeferredLeaseToPluginManagerQueue()
{
    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());
    QSettings settings(temporaryDirectory.filePath(QStringLiteral("accounts.ini")),
                       QSettings::IniFormat);
    TestSecretStore secretStore;
    SourceAccountStore accountStore(&settings, &secretStore);
    PluginManager pluginManager;
    pluginManager.addSearchPath(QStringLiteral(QUEMUSIC_TEST_REGISTRY_V2_PLUGIN_PACKAGE_DIR));
    QCOMPARE(pluginManager.discover(), 1);
    QVERIFY(pluginManager.load(kPackageId));
    QVERIFY(accountStore.upsert(sourceAccount(kSourceId, QStringLiteral("home"),
                                              QStringLiteral("Home"))));

    auto *registry = new SourceRegistry(&pluginManager, &accountStore);
    const QString instanceId = QStringLiteral("registry-v2/home");
    IMusicSourceSessionV2 *session = registry->sessionFor(instanceId);
    QVERIFY(session != nullptr);
    session->setParent(nullptr);
    delete session;

    QCOMPARE(pluginManager.plugin(kPackageId).activeLeases, 1);
    registry->closeAll();
    QVERIFY(registry->sessionFor(instanceId) == nullptr);
    delete registry;
    QCOMPARE(pluginManager.plugin(kPackageId).activeLeases, 1);

    QCoreApplication::sendPostedEvents(&pluginManager, QEvent::MetaCall);
    QCOMPARE(pluginManager.plugin(kPackageId).activeLeases, 0);
}

void SourceRegistryV2Test::disableClosesSessionBeforeReleasingLease()
{
    RegistryHarness harness;
    QVERIFY(harness.loadValidPlugin());
    QVERIFY(harness.saveAccount(QStringLiteral("home"), QStringLiteral("Home")));
    IMusicSourceSessionV2 *session =
        harness.registry.sessionFor(QStringLiteral("registry-v2/home"));
    QVERIFY(session != nullptr);
    int leasesAtDestruction = -1;
    connect(session, &QObject::destroyed, this, [&] {
        leasesAtDestruction = harness.pluginManager.plugin(kPackageId).activeLeases;
    });

    QVERIFY(harness.registry.disableInstance(QStringLiteral("registry-v2/home")));
    QCOMPARE(leasesAtDestruction, 1);
    QCOMPARE(harness.pluginManager.plugin(kPackageId).activeLeases, 0);
}

void SourceRegistryV2Test::destructionClosesSessionBeforeReleasingLease()
{
    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());
    QSettings settings(temporaryDirectory.filePath(QStringLiteral("accounts.ini")),
                       QSettings::IniFormat);
    TestSecretStore secretStore;
    SourceAccountStore accountStore(&settings, &secretStore);
    PluginManager pluginManager;
    pluginManager.addSearchPath(QStringLiteral(QUEMUSIC_TEST_REGISTRY_V2_PLUGIN_PACKAGE_DIR));
    QCOMPARE(pluginManager.discover(), 1);
    QVERIFY(pluginManager.load(kPackageId));
    QVERIFY(accountStore.upsert(sourceAccount(kSourceId, QStringLiteral("home"),
                                              QStringLiteral("Home"))));

    auto *registry = new SourceRegistry(&pluginManager, &accountStore);
    IMusicSourceSessionV2 *session = registry->sessionFor(QStringLiteral("registry-v2/home"));
    QVERIFY(session != nullptr);
    int leasesAtDestruction = -1;
    connect(session, &QObject::destroyed, this, [&] {
        leasesAtDestruction = pluginManager.plugin(kPackageId).activeLeases;
    });

    delete registry;
    QCOMPARE(leasesAtDestruction, 1);
    QCOMPARE(pluginManager.plugin(kPackageId).activeLeases, 0);
}

void SourceRegistryV2Test::closeAllIsIdempotent()
{
    RegistryHarness harness;
    QVERIFY(harness.loadValidPlugin());
    QVERIFY(harness.saveAccount(QStringLiteral("home"), QStringLiteral("Home")));
    QVERIFY(harness.saveAccount(QStringLiteral("office"), QStringLiteral("Office")));
    QPointer<IMusicSourceSessionV2> home =
        harness.registry.sessionFor(QStringLiteral("registry-v2/home"));
    QPointer<IMusicSourceSessionV2> office =
        harness.registry.sessionFor(QStringLiteral("registry-v2/office"));
    QVERIFY(home != nullptr);
    QVERIFY(office != nullptr);
    QCOMPARE(harness.pluginManager.plugin(kPackageId).activeLeases, 2);
    QSignalSpy changed(&harness.registry, &SourceRegistry::instanceChanged);
    changed.clear();

    harness.registry.closeAll();
    QVERIFY(home.isNull());
    QVERIFY(office.isNull());
    QCOMPARE(harness.pluginManager.plugin(kPackageId).activeLeases, 0);
    QCOMPARE(changed.count(), 2);

    harness.registry.closeAll();
    QCOMPARE(harness.pluginManager.plugin(kPackageId).activeLeases, 0);
    QCOMPARE(changed.count(), 2);
}

QTEST_MAIN(SourceRegistryV2Test)
#include "tst_SourceRegistryV2.moc"

#endif
