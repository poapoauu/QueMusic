#pragma once

#include "SourceTypes.h"

#include <QList>
#include <QString>
#include <QVariantMap>

#include <optional>

class QSettings;

class ISecretStore {
public:
    virtual ~ISecretStore() = default;

    virtual bool write(const QString &reference, const QByteArray &secret,
                       QString *error = nullptr) = 0;
    virtual std::optional<QByteArray> read(const QString &reference,
                                            QString *error = nullptr) const = 0;
    virtual bool remove(const QString &reference, QString *error = nullptr) = 0;
};

class UnavailableSecretStore final : public ISecretStore {
public:
    bool write(const QString &reference, const QByteArray &secret,
               QString *error = nullptr) override;
    std::optional<QByteArray> read(const QString &reference,
                                   QString *error = nullptr) const override;
    bool remove(const QString &reference, QString *error = nullptr) override;
};

struct StoredSourceAccount {
    QString sourceId;
    QString accountId;
    QString displayName;
    bool enabled = true;
    QVariantMap parameters;
    QString secretReference;
};

class SourceAccountStore {
public:
    SourceAccountStore(QSettings *settings, ISecretStore *secretStore);

    bool upsert(const SourceAccount &account, bool enabled = true, QString *error = nullptr);
    bool remove(const QString &sourceId, const QString &accountId, QString *error = nullptr);

    std::optional<StoredSourceAccount> storedAccount(const QString &sourceId,
                                                      const QString &accountId) const;
    QList<StoredSourceAccount> accounts() const;
    std::optional<SourceAccount> sourceAccount(const QString &sourceId, const QString &accountId,
                                               QString *error = nullptr) const;
    QString secretReference(const QString &sourceId, const QString &accountId) const;

private:
    QString groupFor(const QString &sourceId, const QString &accountId) const;
    QVariantMap recordValues(const QString &group) const;
    void restoreRecord(const QString &group, const QVariantMap &values);
    bool writeRecord(const QString &group, const SourceAccount &account, bool enabled,
                     const QString &secretReference);

    QSettings *m_settings = nullptr;
    ISecretStore *m_secretStore = nullptr;
};
