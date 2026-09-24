#pragma once

#include <QObject>
#include <QUuid>
#include <QUrl>

class PluginUiHostServices : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;
    Q_INVOKABLE virtual QUuid requestDirectory() = 0;
    Q_INVOKABLE virtual void notify(const QString &message, bool isError) = 0;
signals:
    void directoryRequested(QUuid requestId);
    void directorySelected(QUuid requestId, QUrl localDirectory);
    void notificationRequested(QString message, bool isError);
};
