#pragma once

#include "plugin-ui/v1/PluginUiTypes.h"

#include <QObject>
#include <QPointer>
#include <QtQml/qqmlregistration.h>

class PluginUiContext : public QObject {
    Q_OBJECT
    QML_NAMED_ELEMENT(PluginUiContext)
    QML_UNCREATABLE("PluginUiContext is created by QueMusic")
    Q_PROPERTY(QString pluginPackageId READ pluginPackageId NOTIFY contextChanged)
    Q_PROPERTY(QString sourceId READ sourceId NOTIFY contextChanged)
    Q_PROPERTY(QString sourceInstanceId READ sourceInstanceId NOTIFY contextChanged)
    Q_PROPERTY(QString accountId READ accountId NOTIFY contextChanged)
    Q_PROPERTY(int mode READ mode NOTIFY contextChanged)
    Q_PROPERTY(QObject *backend READ backend NOTIFY contextChanged)
    Q_PROPERTY(QObject *settings READ settings NOTIFY contextChanged)
    Q_PROPERTY(QObject *capabilities READ capabilities NOTIFY contextChanged)
    Q_PROPERTY(QObject *host READ host NOTIFY contextChanged)
    Q_PROPERTY(bool valid READ isValid NOTIFY contextChanged)
public:
    explicit PluginUiContext(QObject *parent = nullptr);

    QString pluginPackageId() const { return m_data.pluginPackageId; }
    QString sourceId() const { return m_data.sourceId; }
    QString sourceInstanceId() const { return m_data.sourceInstanceId; }
    QString accountId() const { return m_data.accountId; }
    int mode() const { return static_cast<int>(m_data.mode); }
    QObject *backend() const { return m_backend; }
    QObject *settings() const { return m_settings; }
    QObject *capabilities() const { return m_capabilities; }
    QObject *host() const { return m_host; }
    bool isValid() const { return m_valid; }

    void setContext(const PluginUiContextData &data, QObject *backend,
                    QObject *settings, QObject *capabilities, QObject *host);
    void invalidate();

signals:
    void contextChanged();

private:
    PluginUiContextData m_data;
    QPointer<QObject> m_backend;
    QPointer<QObject> m_settings;
    QPointer<QObject> m_capabilities;
    QPointer<QObject> m_host;
    bool m_valid = false;
};
