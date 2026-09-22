#include "PluginSettingsOperation.h"
#include "SettingsSchemaPresentation.h"
#include "v2/IMusicSourcePluginV2.h"
#include "v2/ISourceProvidersV2.h"
#include <QCoreApplication>
#include <QSet>
#include <QTimer>
#include <QThread>
#include <utility>

struct PluginSettingsOperation::State {
    PluginSettingsOperation *owner;
    QPointer<PluginManager> manager;
    SourceConfigurationV2 configuration;
    SettingsSchemaV2 schema;
    QString action;
    bool confirmed;
    int deadlineMs;
    QUuid id = QUuid::createUuid(), pending, lastStarted;
    PluginLease lease;
    QPointer<IMusicSourceSessionV2> session;
    QTimer deadline;
    bool started = false, terminal = false, canceled = false, cleaned = false, actionPhase = false;
    bool cleanupQueued = false;
    int invocationDepth = 0;
    QVariantMap result;
    QVariantList actions;
    QVariantList musicCapabilities;
    QHash<SourceActionV2, ActionAvailabilityV2> declaredMusic;

    struct Invocation {
        QSet<QUuid> started;
        QList<QPair<QUuid, SourceSessionStateV2>> states;
        QList<QPair<QUuid, SourceErrorKindV2>> failures;
        QList<QPair<QUuid, QString>> completions;
    };
    Invocation *invocation = nullptr;
    struct Frame {
        State *state;
        std::shared_ptr<PluginSettingsOperation> keep;
        explicit Frame(State *s) : state(s), keep(s->owner->shared_from_this()) { ++state->invocationDepth; }
        ~Frame() { --state->invocationDepth; if (state->terminal) state->scheduleCleanup(); }
    };

    void finish(bool success, AvailabilityV2 availability, const QString &reason,
                SourceSessionStateV2 connectionState = SourceSessionStateV2::Failed)
    {
        if (terminal) return;
        terminal = true;
        deadline.stop();
        result = {{"success", success}, {"state", action.isEmpty() ? int(connectionState) : int(availability)},
                  {"reasonKey", reason}};
        if (!action.isEmpty()) result.insert("actionId", action);
        scheduleCleanup();
    }
    void scheduleCleanup()
    {
        if (invocationDepth || cleaned || cleanupQueued) return;
        cleanupQueued = true;
        deferCleanup(QThread::currentThread()->loopLevel());
    }
    void deferCleanup(int postingLoopLevel)
    {
        // DeferredDelete respects the posting event-loop nesting level; a
        // zero-timer can run inside a later signal observer's nested loop while
        // an asynchronous provider is still emitting. Retain state/lease until
        // that outer dispatch unwinds, even if the controller has disappeared.
        auto keep = owner->shared_from_this();
        auto *boundary = new QObject;
        QObject::connect(boundary, &QObject::destroyed, QCoreApplication::instance(), [keep, postingLoopLevel] {
            auto *s = keep->d.get();
            if (s->cleaned) return;
            if (s->invocationDepth) { s->cleanupQueued = false; return; }
            // DeferredDelete posted before a main exec() may be delivered as
            // soon as a nested loop starts. Preserve the original loop level
            // across retries too; entering a loop is never an unwind proof.
            if (QThread::currentThread()->loopLevel() > postingLoopLevel) {
                QTimer::singleShot(0, QCoreApplication::instance(), [keep, postingLoopLevel] {
                    if (!keep->d->cleaned) keep->d->deferCleanup(postingLoopLevel);
                });
                return;
            }
            s->cleanup();
            if (!s->canceled) emit keep->finished(s->id, s->result, s->actions, s->musicCapabilities);
        });
        boundary->deleteLater();
    }
    void cleanup()
    {
        if (cleaned) return;
        cleaned = true;
        deadline.stop();
        auto live = session;
        session = nullptr;
        if (live) {
            QObject::disconnect(live, nullptr, owner, nullptr);
            if (!pending.isNull()) live->cancel(pending);
            if (live) live->close();
            if (live) delete live;
        }
        configuration.secret.clear();
        lease = {};
    }
    void failRequest(SourceErrorKindV2 kind)
    {
        const bool authentication = kind == SourceErrorKindV2::Authentication;
        auto availability = kind == SourceErrorKindV2::Authorization ? AvailabilityV2::Forbidden
            : kind == SourceErrorKindV2::Unsupported ? AvailabilityV2::Unsupported : AvailabilityV2::Unavailable;
        finish(false, availability, authentication ? QStringLiteral("source.settings.authenticationRequired")
            : settingsAvailabilityReasonV2(availability), authentication ? SourceSessionStateV2::AuthenticationRequired
                                                                        : SourceSessionStateV2::Failed);
    }
    void completeAction(const QUuid &request, const QString &name)
    {
        if (terminal || !actionPhase || request != pending || name != action) return;
        pending = {};
        finish(true, AvailabilityV2::Available, {});
    }
    void stateChanged(SourceSessionStateV2 state)
    {
        if (terminal || actionPhase || pending.isNull() || lastStarted != pending) return;
        if (state == SourceSessionStateV2::Ready) {
            pending = {};
            ready();
        } else if (state == SourceSessionStateV2::AuthenticationRequired) {
            pending = {}; failRequest(SourceErrorKindV2::Authentication);
        } else if (state == SourceSessionStateV2::Failed || state == SourceSessionStateV2::Closed) {
            pending = {}; failRequest(SourceErrorKindV2::Unavailable);
        }
    }
    template<class Callable> void callRequest(Callable callable)
    {
        Invocation local;
        invocation = &local;
        const QUuid returned = callable();
        invocation = nullptr;
        // Only IDs observed in this invocation may be owned or canceled.
        pending = !returned.isNull() && local.started.contains(returned) ? returned : QUuid();
        if (terminal) return;
        if (pending.isNull()) {
            finish(false, AvailabilityV2::Unavailable, QStringLiteral("source.settings.invalidRequest")); return;
        }
        for (const auto &failure : local.failures) if (failure.first == pending) {
            pending = {}; failRequest(failure.second); return;
        }
        for (const auto &completion : local.completions) completeAction(completion.first, completion.second);
        if (terminal) return;
        for (auto state = local.states.crbegin(); state != local.states.crend(); ++state) if (state->first == pending) {
            lastStarted = pending; stateChanged(state->second);
            break;
        }
    }
    bool isReady()
    {
        if (terminal || !session) return false;
        const auto state = session->state();
        if (terminal || !session) return false;
        if (state == SourceSessionStateV2::Ready) return true;
        failRequest(state == SourceSessionStateV2::AuthenticationRequired
            ? SourceErrorKindV2::Authentication : SourceErrorKindV2::Unavailable);
        return false;
    }
    void ready()
    {
        Frame frame(this);
        if (!isReady()) return;
        auto *provider = qobject_cast<ISettingsActionProviderV2 *>(session.data());
        const auto capabilities = provider ? std::optional(provider->settingsCapabilities()) : std::nullopt;
        if (!isReady()) return;
        actions = settingsActionsPresentationV2(schema, capabilities, provider != nullptr);
        if (action.isEmpty()) {
            const auto runtime = session->capabilities();
            if (!isReady()) return;
            musicCapabilities = sourceCapabilitiesPresentationV2(declaredMusic, runtime);
            finish(true, AvailabilityV2::Available, {}, SourceSessionStateV2::Ready); return;
        }
        QVariantMap permission;
        for (const auto &row : actions) if (row.toMap().value("id").toString() == action) permission = row.toMap();
        const auto availability = permission.isEmpty() ? AvailabilityV2::Unsupported
            : AvailabilityV2(permission.value("state").toInt());
        if (availability != AvailabilityV2::Available) {
            finish(false, availability, settingsAvailabilityReasonV2(availability)); return;
        }
        if (permission.value("requiresConfirmation").toBool() && !confirmed) {
            finish(false, AvailabilityV2::Unavailable, QStringLiteral("source.settings.confirmationRequired")); return;
        }
        actionPhase = true;
        callRequest([&] { return provider->runSettingsAction(action); });
    }
    void execute()
    {
        Frame frame(this);
        if (terminal || !manager) {
            finish(false, AvailabilityV2::Unavailable, QStringLiteral("source.settings.unavailable")); return;
        }
        lease = manager->acquire(configuration.pluginPackageId);
        if (terminal) return;
        if (!manager || !lease.isValid()) {
            finish(false, AvailabilityV2::Unavailable, QStringLiteral("source.settings.unavailable")); return;
        }
        auto *plugin = qobject_cast<IMusicSourcePluginV2 *>(manager->pluginInstance(configuration.pluginPackageId));
        if (!plugin) { finish(false, AvailabilityV2::Unsupported, QStringLiteral("source.settings.unsupported")); return; }
        const auto descriptor = plugin->descriptor();
        if (terminal) return;
        declaredMusic = detachSettingsDescriptorV2(descriptor).declaredActions;
        if (descriptor.sourceId != configuration.sourceId
            || (!descriptor.pluginPackageId.isEmpty() && descriptor.pluginPackageId != configuration.pluginPackageId)) {
            finish(false, AvailabilityV2::Unavailable, QStringLiteral("source.settings.invalidIdentity")); return;
        }
        session = plugin->createSession(configuration, nullptr);
        if (session && session->parent()) {
            // R7: no host invocation boundary proves this foreign destructor
            // unwound. Nested event loops defeat queued-release heuristics.
            // Permanently pin the offending package, without touching ownership.
            session = nullptr;
            lease.pinLoadedPackage();
            finish(false, AvailabilityV2::Unavailable, QStringLiteral("source.settings.invalidOwnership")); return;
        }
        if (terminal) return;
        if (!session) { finish(false, AvailabilityV2::Unavailable, QStringLiteral("source.settings.unavailable")); return; }
        // Observe ownership loss before the first session virtual call. The
        // lease is callable independently of the manager, so R7 pinning is
        // safe even when identity() reentrantly destroys both session/facade.
        QObject::connect(session, &QObject::destroyed, owner, [this] {
            Frame frame(this);
            session = nullptr;
            // Normal cleanup disconnects this observer before deleting. Reaching
            // it means external ownership was violated, so R7 applies here too.
            lease.pinLoadedPackage();
            finish(false, AvailabilityV2::Unavailable, QStringLiteral("source.settings.invalidOwnership"));
        });
        const auto identity = session->identity();
        if (terminal || !session) return;
        if (identity.sourcePluginId != configuration.sourceId || identity.accountId != configuration.accountId
            || identity.sourceInstanceId != configuration.sourceInstanceId) {
            finish(false, AvailabilityV2::Unavailable, QStringLiteral("source.settings.invalidIdentity")); return;
        }
        QObject::connect(session, &IMusicSourceSessionV2::requestStarted, owner, [this](QUuid request) {
            lastStarted = request;
            if (invocation && !request.isNull()) invocation->started.insert(request);
        });
        QObject::connect(session, &IMusicSourceSessionV2::stateChanged, owner, [this](SourceSessionStateV2 state) {
            if (invocation) invocation->states.append({lastStarted, state});
            else { Frame frame(this); stateChanged(state); }
        });
        QObject::connect(session, &IMusicSourceSessionV2::requestFailed, owner, [this](QUuid request, const SourceErrorV2 &error) {
            if (invocation) {
                if (invocation->started.contains(request)) invocation->failures.append({request, error.kind});
            }
            else if (!terminal && !pending.isNull() && pending == request) {
                Frame frame(this); pending = {}; failRequest(error.kind);
            }
        });
        QObject::connect(session, &IMusicSourceSessionV2::settingsActionCompleted, owner, [this](QUuid request, const QString &name) {
            if (invocation) {
                if (invocation->started.contains(request) && name == action)
                    invocation->completions.append({request, action});
            }
            else { Frame frame(this); completeAction(request, name); }
        });
        if (terminal || !session) return;
        callRequest([&] { return session->open(); });
    }
};

PluginSettingsOperation::PluginSettingsOperation(PluginManager *manager, SourceConfigurationV2 config,
    SettingsSchemaV2 schema, QString action, bool confirmed, int timeout)
    : d(std::make_unique<State>())
{
    d->owner = this; d->manager = manager; d->configuration = std::move(config);
    d->schema = std::move(schema); d->action = std::move(action); d->confirmed = confirmed; d->deadlineMs = timeout;
    d->deadline.setSingleShot(true);
    connect(&d->deadline, &QTimer::timeout, this, [this] {
        d->finish(false, AvailabilityV2::Unavailable, QStringLiteral("source.settings.timeout"));
    });
    if (manager) connect(manager, &QObject::destroyed, this, [this] { cancel(); });
}
PluginSettingsOperation::~PluginSettingsOperation() { d->canceled = true; d->cleanup(); }
QUuid PluginSettingsOperation::requestId() const { return d->id; }
void PluginSettingsOperation::start()
{
    if (d->started || d->terminal) return;
    d->started = true;
    auto keep = shared_from_this();
    d->deadline.start(d->deadlineMs);
    QTimer::singleShot(0, this, [keep] { keep->d->execute(); });
}
void PluginSettingsOperation::cancel()
{
    auto keep = shared_from_this();
    d->canceled = true;
    d->finish(false, AvailabilityV2::Unavailable, QStringLiteral("source.settings.canceled"));
    if (d->terminal) d->scheduleCleanup();
}
