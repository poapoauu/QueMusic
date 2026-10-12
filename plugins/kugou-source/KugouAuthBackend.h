#pragma once
#include "KugouQrController.h"
#include "PluginUiContext.h"
#include "PluginUiSettingsBridge.h"
#include "plugin-ui/v1/IPluginUiBackendSecretsV1.h"
#include <QHash>

// Private management backend. Stored means durable candidate binding, never
// authenticated/Source Ready. Credentials/device identities remain C++ only.
class KugouAuthBackend final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString state READ state NOTIFY changed)
    Q_PROPERTY(QUrl qrUrl READ qrUrl NOTIFY changed)
    Q_PROPERTY(QString errorKey READ errorKey NOTIFY changed)
    Q_PROPERTY(bool credentialStored READ credentialStored NOTIFY changed)
public:
    explicit KugouAuthBackend(PluginUiContext *context, QObject *parent = nullptr);
    KugouAuthBackend(PluginUiContext *context, QNetworkAccessManager *network,
                     KugouQrController::Timing timing, QObject *parent = nullptr);
    QString state() const { return m_state; }
    QUrl qrUrl() const { return m_running ? m_controller->qrUrl() : QUrl(); }
    QString errorKey() const { return m_error; }
    bool credentialStored() const { return m_stored; }
    Q_INVOKABLE bool startQr();
    Q_INVOKABLE void cancelQr();
    Q_INVOKABLE bool clearAccount();
signals:
    void changed();
private:
    void initialize();
    bool valid() const;
    bool refreshBinding();
    bool notify(const QString &state, const QString &error = {});
    void invalidate();
    void confirmed(QUuid attempt);
    void completed(QUuid request, bool success);
    IPluginUiBackendSecretsV1 *service() const;
    QPointer<PluginUiContext> m_context;
    QPointer<PluginUiSettingsBridge> m_settings;
    KugouQrController *m_controller;
    QString m_instance, m_account, m_user, m_state = "idle", m_error;
    QByteArray m_pendingRecord;
    std::optional<QByteArray> m_bindingSnapshot;
    QUuid m_request;
    QHash<QUuid, bool> m_earlyCompletions;
    quint64 m_generation = 0;
    bool m_invalidated = false, m_running = false, m_pending = false, m_submitting = false,
         m_clearing = false, m_stored = false;
};
