#pragma once

#include <QObject>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

class PluginThemeAdapter : public QObject {
    Q_OBJECT
    QML_NAMED_ELEMENT(PluginThemeAdapter)
    QML_SINGLETON
public:
    explicit PluginThemeAdapter(QObject *parent = nullptr);
    Q_INVOKABLE bool apply(const QVariantMap &values);
};
