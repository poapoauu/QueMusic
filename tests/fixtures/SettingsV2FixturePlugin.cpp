#include "SettingsV2FixturePlugin.h"
#include "v2/IMusicSourcePluginV2.h"
#include "v2/ISourceProvidersV2.h"
#include <QTimer>

class SettingsFixtureSession : public IMusicSourceSessionV2 {
    Q_OBJECT
public:
    SettingsFixtureSession(SourceConfigurationV2 config, SettingsV2FixtureControl *control, QObject *parent)
        : IMusicSourceSessionV2(parent), config(std::move(config)), control(control) {}
    ~SettingsFixtureSession() override { ++control->destroyed; control->event("destroy", this); }
    SourceIdentityV2 identity() const override {
        const SourceIdentityV2 result{
            control->invalidIdentity ? QString("wrong") : config.sourceId,
            config.sourceInstanceId, config.accountId, config.displayName};
        // Capture every plugin-owned value before the callback. A test may
        // externally delete this session while identity() remains on stack.
        auto *hostControl = control;
        hostControl->event("identity", const_cast<SettingsFixtureSession *>(this));
        return result;
    }
    SourceSessionStateV2 state() const override { return currentState; }
    CapabilitySetV2 capabilities() const override { return control->musicCapabilities; }
    QUuid open() override {
        ++control->opened;
        auto id = QUuid::createUuid();
        if (control->openMode == SettingsV2FixtureControl::FailureBeforeStarted)
            emit requestFailed(id, {SourceErrorKindV2::Authentication, "PRIVATE", "PRIVATE"});
        if (control->openMode == SettingsV2FixtureControl::NestedForeign) {
            auto foreign = QUuid::createUuid(); emit requestStarted(foreign);
            emit requestFailed(foreign, {SourceErrorKindV2::Authentication, "PRIVATE", "PRIVATE"});
        }
        if (control->openMode != SettingsV2FixtureControl::Unstarted) emit requestStarted(id);
        control->event("open", this);
        auto done = [this, id] {
            if (control->openMode == SettingsV2FixtureControl::Failure) {
                emit requestFailed(id, {SourceErrorKindV2::Authentication, "PRIVATE", "PRIVATE"});
                return;
            }
            const bool revoke = control->openMode == SettingsV2FixtureControl::ReadyThenAuth;
            currentState = SourceSessionStateV2::Ready; emit stateChanged(currentState);
            if (revoke) {
                currentState = SourceSessionStateV2::AuthenticationRequired; emit stateChanged(currentState);
            }
        };
        if (control->openMode == SettingsV2FixtureControl::Delayed) QTimer::singleShot(80, this, done);
        else done();
        return id;
    }
    void close() override { ++control->closed; control->event("close", this); currentState = SourceSessionStateV2::Closed; }
    void cancel(const QUuid &) override { ++control->canceled; control->event("cancel", this); }
protected:
    SourceConfigurationV2 config;
    SettingsV2FixtureControl *control;
    SourceSessionStateV2 currentState = SourceSessionStateV2::Closed;
};

class SettingsActionFixtureSession final : public SettingsFixtureSession, public ISettingsActionProviderV2 {
    Q_OBJECT
    Q_INTERFACES(ISettingsActionProviderV2)
public:
    using SettingsFixtureSession::SettingsFixtureSession;
    SettingsActionCapabilitiesV2 settingsCapabilities() const override {
        if (control->revokeReadyInCapabilities)
            const_cast<SettingsActionFixtureSession *>(this)->currentState = SourceSessionStateV2::AuthenticationRequired;
        SettingsActionCapabilitiesV2 caps;
        caps.serverActions.insert("diagnose", {AvailabilityV2::Available, "PRIVATE", control->actionConstraints});
        if (!control->missingAccountGrant) caps.accountActions.insert("diagnose", {control->accountGrant, "PRIVATE", {}});
        return caps;
    }
    QUuid runSettingsAction(const QString &action) override {
        ++control->invoked;
        const auto id = QUuid::createUuid();
        if (control->actionMode == SettingsV2FixtureControl::TerminalBeforeStarted)
            emit settingsActionCompleted(id, action);
        if (control->actionMode != SettingsV2FixtureControl::Unstarted) emit requestStarted(id);
        control->event("action", this);
        auto done = [this, id, action] {
            if (control->actionMode == SettingsV2FixtureControl::TerminalBeforeStarted) return;
            if (control->actionMode == SettingsV2FixtureControl::Failure) {
                emit requestFailed(id, {SourceErrorKindV2::Authorization, "PRIVATE", "PRIVATE"}); return;
            }
            if (control->actionMode == SettingsV2FixtureControl::WrongThenCorrect
                || control->actionMode == SettingsV2FixtureControl::WrongOnly) {
                emit settingsActionCompleted(QUuid::createUuid(), action);
                emit settingsActionCompleted(id, "foreign");
                if (control->actionMode == SettingsV2FixtureControl::WrongOnly) return;
            }
            emit settingsActionCompleted(id, action);
            if (control->actionMode == SettingsV2FixtureControl::Duplicate) emit settingsActionCompleted(id, action);
        };
        if (control->actionMode == SettingsV2FixtureControl::Delayed) QTimer::singleShot(80, this, done);
        else done();
        return id;
    }
};

class SettingsV2FixturePlugin final : public QObject, public IMusicSourcePluginV2,
                                      public IPluginSettingsProviderV2 {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID QUEMUSIC_MUSIC_SOURCE_PLUGIN_V2_IID)
    Q_INTERFACES(IMusicSourcePluginV2 IPluginSettingsProviderV2)
    Q_PROPERTY(int sourceSdkAbi READ sourceSdkAbi CONSTANT)
public:
    int sourceSdkAbi() const { return 2; }
    SourceDescriptorV2 descriptor() const override {
        return {"org.quemusic.source.settings-fixture", "settings-fixture", "Settings fixture", "2.0", 2,
            {{SourceActionV2::Play, {AvailabilityV2::Available, "PRIVATE", {}}},
             {SourceActionV2::Favorite, {AvailabilityV2::Available, "PRIVATE", {}}}}};
    }
    SettingsV2FixtureControl *control() const {
        return static_cast<SettingsV2FixtureControl *>(property("control").value<void *>());
    }
    SettingsSchemaV2 settingsSchema() const override {
        SettingsFieldV2 folder{"folder", "settings.folder", SettingsFieldTypeV2::Directory};
        SettingsFieldV2 password{"password", "settings.password", SettingsFieldTypeV2::Secret};
        password.defaultValue = QStringLiteral("PRIVATE-default");
        SettingsFieldV2 active{"active", "settings.active", SettingsFieldTypeV2::Boolean};
        active.defaultValue = true;
        if (control() && control()->conditionalRequiredFolder) {
            folder.required = true;
            folder.visibleWhen = SettingsVisibilityConditionV2{
                "active", SettingsComparisonV2::Equal, true};
        }
        if (control() && control()->invalidSchema) folder.id = "../invalid";
        return {{"main", "settings.main", {folder, password, active},
                 {{"diagnose", "settings.diagnose", control() && control()->requiresConfirmation}}}};
    }
    IMusicSourceSessionV2 *createSession(const SourceConfigurationV2 &config, QObject *parent) override {
        auto *c = control();
        if (!c) return nullptr;
        ++c->created; c->lastConfiguration = config;
        if (c->forcedParent) parent = c->forcedParent;
        auto *s = c->noActionProvider ? new SettingsFixtureSession(config, c, parent)
                                     : new SettingsActionFixtureSession(config, c, parent);
        c->lastSession = s; c->event("create", s);
        return s;
    }
};
#include "SettingsV2FixturePlugin.moc"
