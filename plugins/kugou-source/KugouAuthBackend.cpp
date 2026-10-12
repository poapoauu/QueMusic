#include "KugouAuthBackend.h"

namespace { const QString fieldId = QStringLiteral("credentialBundle"); }

KugouAuthBackend::KugouAuthBackend(PluginUiContext *context, QObject *parent)
    : QObject(parent), m_context(context),
      m_controller(new KugouQrController(context ? context->sourceInstanceId() : QString(),
          context ? context->accountId() : QString(), this))
{ initialize(); }

KugouAuthBackend::KugouAuthBackend(PluginUiContext *context, QNetworkAccessManager *network,
    KugouQrController::Timing timing, QObject *parent)
    : QObject(parent), m_context(context),
      m_controller(new KugouQrController(context ? context->sourceInstanceId() : QString(),
          context ? context->accountId() : QString(), network, timing, this))
{ initialize(); }

void KugouAuthBackend::initialize()
{
    if (m_context) {
        m_instance = m_context->sourceInstanceId(); m_account = m_context->accountId();
        m_settings = qobject_cast<PluginUiSettingsBridge *>(m_context->settings());
        connect(m_context, &PluginUiContext::contextChanged, this, [this] { if (!valid()) invalidate(); });
        connect(m_context, &QObject::destroyed, this, &KugouAuthBackend::invalidate);
    }
    if (m_settings) {
        connect(m_settings, &PluginUiSettingsBridge::operationFinished, this,
            [this](QUuid id, bool success, const QString &) { completed(id, success); });
        connect(m_settings, &QObject::destroyed, this, &KugouAuthBackend::invalidate);
    }
    connect(m_controller, &KugouQrController::confirmationReady, this, &KugouAuthBackend::confirmed);
    connect(m_controller, &KugouQrController::stateChanged, this, [this](KugouQrController::State state) {
        if (!m_running || !valid()) return;
        using State = KugouQrController::State;
        switch (state) {
        case State::Fetching: notify("fetching"); break;
        case State::Waiting: notify("waiting"); break;
        case State::Scanned: notify("scanned"); break;
        case State::Expired: m_running = false; notify("expired"); break;
        case State::Failed: m_running = false; notify("failed", m_controller->errorKey()); break;
        default: break; // Confirmed is a candidate, not a public login grant.
        }
    });
    if (!valid()) invalidate();
    else if (!refreshBinding()) notify("failed", "kugou.authBindingUnavailable");
}

IPluginUiBackendSecretsV1 *KugouAuthBackend::service() const
{ return m_settings ? qobject_cast<IPluginUiBackendSecretsV1 *>(m_settings.data()) : nullptr; }

bool KugouAuthBackend::valid() const
{
    return !m_invalidated && m_context && m_context->isValid() && m_settings && service()
        && m_context->settings() == m_settings && m_context->pluginPackageId() == "org.quemusic.source.kugou"
        && m_context->sourceId() == "kugou" && m_context->sourceInstanceId() == m_instance
        && m_context->accountId() == m_account && KugouQrProtocol::validScope(m_instance, m_account);
}

bool KugouAuthBackend::refreshBinding()
{
    m_stored = false; m_user.clear(); m_bindingSnapshot.reset();
    if (!valid()) return false;
    if (!service()->backendSecretConfigured(fieldId)) return true;
    const auto bytes = service()->readBackendSecret(fieldId);
    if (!bytes) return false;
    const auto record = KugouCredentialRecord::decode(*bytes, m_instance, m_account);
    if (!record) return false;
    m_stored = true; m_user = record->credentials.userId; m_bindingSnapshot = *bytes;
    return true;
}

bool KugouAuthBackend::notify(const QString &state, const QString &error)
{
    m_state = state; m_error = error;
    QPointer<KugouAuthBackend> guard(this);
    emit changed();
    return !guard.isNull();
}

void KugouAuthBackend::invalidate()
{
    if (m_invalidated) return;
    m_invalidated = true; ++m_generation; m_running = m_pending = m_stored = m_submitting = false;
    m_earlyCompletions.clear();
    m_request = {}; m_pendingRecord.clear(); m_user.clear(); m_bindingSnapshot.reset();
    m_controller->cancel(); notify("invalidated");
}

bool KugouAuthBackend::startQr()
{
    if (!valid() || m_pending) return false;
    ++m_generation; m_running = false; m_controller->cancel();
    if (!refreshBinding()) { notify("failed", "kugou.authBindingUnavailable"); return false; }
    m_running = true;
    m_controller->start(); // Observers may destroy/restart us; no access afterwards.
    return true;
}

void KugouAuthBackend::cancelQr()
{
    // A synchronous storage transaction cannot be undone by cancelling its
    // queued notification. Context invalidation still fences late completion.
    if (!valid() || m_pending) return;
    ++m_generation; m_running = false; m_controller->cancel(); notify("cancelled");
}

void KugouAuthBackend::confirmed(QUuid attempt)
{
    if (!valid() || !m_running || m_controller->attemptId() != attempt) return;
    const auto account = m_controller->takeAccount(attempt);
    m_running = false;
    const auto expectedBinding = m_bindingSnapshot;
    if (!refreshBinding() || m_bindingSnapshot != expectedBinding) {
        notify("failed", "kugou.authBindingChanged"); return;
    }
    if (!account || (!m_user.isEmpty() && account->credentials.userId != m_user)) {
        notify("failed", "kugou.authUserConflict"); return;
    }
    const auto bytes = KugouCredentialRecord::encode(*account);
    if (!bytes) { notify("failed", "kugou.authInvalidBinding"); return; }
    m_pendingRecord = *bytes;
    m_pending = true; m_clearing = false;
    const auto generation = m_generation;
    if (!notify("storing") || !valid() || generation != m_generation) return;
    // State observers may synchronously commit another binding. Recheck after
    // notification too; do not overwrite a concurrent same-user token rotation.
    if (!refreshBinding() || m_bindingSnapshot != expectedBinding) {
        m_pending = false; m_pendingRecord.clear();
        notify("failed", "kugou.authBindingChanged"); return;
    }
    QPointer<KugouAuthBackend> guard(this);
    m_submitting = true;
    const auto id = service()->saveBackendSecret(fieldId, *bytes);
    if (guard && valid() && generation == m_generation) {
        m_submitting = false; m_request = id;
        const bool early = m_earlyCompletions.contains(id);
        const bool success = m_earlyCompletions.value(id); m_earlyCompletions.clear();
        if (early) completed(id, success);
    }
}

bool KugouAuthBackend::clearAccount()
{
    if (!valid() || m_pending) return false;
    ++m_generation; m_running = false; m_controller->cancel();
    if (!service()->backendSecretConfigured(fieldId)) {
        // Create-mode has no persisted account yet. Clearing an empty binding
        // is idempotent and must not ask Host storage to manufacture an account.
        m_stored = false; m_user.clear(); m_bindingSnapshot.reset();
        return notify("cleared");
    }
    m_pending = true; m_clearing = true; m_pendingRecord.clear();
    const auto generation = m_generation;
    if (!notify("clearing") || !valid() || generation != m_generation) return false;
    QPointer<KugouAuthBackend> guard(this);
    m_submitting = true;
    const auto id = service()->clearBackendSecret(fieldId);
    if (guard && valid() && generation == m_generation) {
        m_submitting = false; m_request = id;
        const bool early = m_earlyCompletions.contains(id);
        const bool success = m_earlyCompletions.value(id); m_earlyCompletions.clear();
        if (early) completed(id, success);
    }
    return true;
}

void KugouAuthBackend::completed(QUuid request, bool success)
{
    // A Host changed observer may enter a nested event loop and deliver the
    // queued result before save/clear returns its request ID. Match afterwards.
    if (valid() && m_pending && m_submitting && !request.isNull()) {
        m_earlyCompletions.insert(request, success); return;
    }
    if (!valid() || !m_pending || request.isNull() || request != m_request) return;
    m_request = {}; m_pending = false;
    const auto expected = m_pendingRecord; m_pendingRecord.clear();
    const bool bindingReadable = refreshBinding();
    if (success && bindingReadable) {
        if (m_clearing && !m_stored) { notify("cleared"); return; }
        const auto stored = service()->readBackendSecret(fieldId);
        if (!m_clearing && stored && *stored == expected) { notify("stored"); return; }
    }
    notify("failed", "kugou.authStorageFailed");
}
