#pragma once

#include <QObject>
#include <QPointer>
#include <QVariantList>

class SourceAccountStore;
class SourceManager;
class SourceSessionRegistry;

// QML-facing account boundary. It intentionally exposes only account metadata;
// credentials are accepted transiently by the invokables and remain in C++.
class SourceAccountController : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList accounts READ accounts NOTIFY accountsChanged)
    Q_PROPERTY(QVariantList availableSources READ availableSources NOTIFY availableSourcesChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)

public:
    explicit SourceAccountController(SourceAccountStore *accountStore,
                                     SourceSessionRegistry *registry,
                                     SourceManager *sourceManager,
                                     QObject *parent = nullptr);

    QVariantList accounts() const;
    QVariantList availableSources() const;
    QString lastError() const;

    Q_INVOKABLE bool createNavidromeAccount(const QString &displayName, const QString &serverUrl,
                                            const QString &username, const QString &password);
    Q_INVOKABLE bool updateNavidromeAccount(const QString &accountId, const QString &displayName,
                                            const QString &serverUrl, const QString &username,
                                            const QString &password);
    Q_INVOKABLE bool setAccountEnabled(const QString &accountId, bool enabled);
    Q_INVOKABLE bool removeAccount(const QString &accountId);

signals:
    void accountsChanged();
    void availableSourcesChanged();
    void lastErrorChanged();

private:
    bool storeNavidromeAccount(const QString &accountId, const QString &displayName,
                               const QString &serverUrl, const QString &username,
                               const QByteArray &secret);
    bool validateNavidromeInput(const QString &serverUrl, const QString &username,
                                const QByteArray &secret, bool requireSecret);
    void setLastError(const QString &error);

    SourceAccountStore *m_accountStore = nullptr;
    SourceSessionRegistry *m_registry = nullptr;
    QPointer<SourceManager> m_sourceManager;
    QString m_lastError;
};
