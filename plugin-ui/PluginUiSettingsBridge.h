#pragma once

#include <QObject>
#include <QUuid>
#include <QVariantList>
#include <QVariantMap>

// Stable Plugin UI service contract. Host implementation and storage types remain private.
class PluginUiSettingsBridge : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList sections READ sections NOTIFY changed)
    Q_PROPERTY(QVariantMap publicValues READ publicValues NOTIFY changed)
public:
    using QObject::QObject;
    virtual QVariantList sections() const = 0;
    virtual QVariantMap publicValues() const = 0;
    Q_INVOKABLE virtual bool secretConfigured(const QString &fieldId) const = 0;
    Q_INVOKABLE virtual QUuid savePublicValues(const QVariantMap &values) = 0;
    Q_INVOKABLE virtual QUuid saveSecret(const QString &fieldId, const QString &value) = 0;
    Q_INVOKABLE virtual QUuid clearSecret(const QString &fieldId) = 0;
signals:
    void changed();
    void operationFinished(QUuid requestId, bool success, QString reasonKey);
};
