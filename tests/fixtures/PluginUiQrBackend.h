#pragma once
#include "PluginUiContext.h"
#include "PluginUiSettingsBridge.h"
#include <QPointer>
#include <QTimer>

// Offline fixture: only native C++ owns the credential; QML observes state.
class PluginUiQrBackend final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString loginState READ loginState NOTIFY loginStateChanged)
public:
    explicit PluginUiQrBackend(QObject *parent) : QObject(parent), m_context(qobject_cast<PluginUiContext *>(parent))
    {
        if (m_context) {
            if (auto *settings = qobject_cast<PluginUiSettingsBridge *>(m_context->settings()))
                connect(settings, &PluginUiSettingsBridge::operationFinished, this,
                    [this](QUuid id, bool success, const QString &) {
                        if (!m_context || !m_context->isValid() || id != m_request) return;
                        m_state = success ? QStringLiteral("success") : QStringLiteral("error");
                        emit loginStateChanged();
                    });
        }
    }
    QString loginState() const { return m_state; }
    Q_INVOKABLE void requestLogin() {
        const auto token = ++m_generation;
        m_state = QStringLiteral("pending"); emit loginStateChanged();
        QTimer::singleShot(30, this, [this, token] {
            if (token != m_generation || !m_context || !m_context->isValid()) return;
            auto *settings = qobject_cast<PluginUiSettingsBridge *>(m_context->settings());
            if (!settings || m_failNext) {
                m_failNext = false; m_state = QStringLiteral("error"); emit loginStateChanged(); return;
            }
            m_request = settings->saveSecret(QStringLiteral("token"), QStringLiteral("fixture-native-secret"));
        });
    }
    Q_INVOKABLE void cancelLogin() {
        ++m_generation; m_request = {}; m_state = QStringLiteral("idle"); emit loginStateChanged();
    }
    Q_INVOKABLE void failNextLogin() { m_failNext = true; }
signals:
    void loginStateChanged();
private:
    QPointer<PluginUiContext> m_context;
    QString m_state = QStringLiteral("idle");
    QUuid m_request;
    quint64 m_generation = 0;
    bool m_failNext = false;
};
