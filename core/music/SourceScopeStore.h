#pragma once

#include <QObject>
#include <QString>

class QSettings;

// Share one store between the MusicHub pages. Settings must outlive the store.
class SourceScopeStore final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString selectedSourceInstanceId READ selectedSourceInstanceId
               WRITE setSelectedSourceInstanceId NOTIFY selectedSourceInstanceIdChanged)

public:
    explicit SourceScopeStore(QSettings *settings, QObject *parent = nullptr);

    QString selectedSourceInstanceId() const;
    void setSelectedSourceInstanceId(const QString &value);

signals:
    void selectedSourceInstanceIdChanged();

private:
    QSettings *m_settings;
    QString m_selected;
};
