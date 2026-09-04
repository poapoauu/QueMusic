#pragma once

#include "SourceTypes.h"
#include "v2/SourceV2Types.h"

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
    int recordVersion = 1;
    QString pluginPackageId;
    QString sourceInstanceId;
    int configurationVersion = 0;
    QStringList configuredSecretFieldIds;
    QString secretFormat;
};

struct SourceAccountSaveV2 {
    QString pluginPackageId;
    QString sourceId;
    QString accountId;
    QString displayName;
    bool enabled = true;
    int configurationVersion = 1;
    SettingsSchemaV2 schema;
    QVariantMap draft;
};

class SourceAccountStore {
public:
    SourceAccountStore(QSettings *settings, ISecretStore *secretStore);

    bool upsert(const SourceAccount &account, bool enabled = true, QString *error = nullptr);
    // Errors are host-owned keys; never contain draft values or backend diagnostics.
    bool saveValidatedV2(const SourceAccountSaveV2 &request, QString *error = nullptr);
    std::optional<SourceConfigurationV2> configurationForDraftV2(
        const SourceAccountSaveV2 &request, QString *error = nullptr);
    bool setEnabled(const QString &sourceId, const QString &accountId, bool enabled,
                    QString *error = nullptr);
    bool remove(const QString &sourceId, const QString &accountId, QString *error = nullptr);

    std::optional<StoredSourceAccount> storedAccount(const QString &sourceId,
                                                      const QString &accountId) const;
    QList<StoredSourceAccount> accounts() const;
    std::optional<SourceAccount> sourceAccount(const QString &sourceId, const QString &accountId,
                                               QString *error = nullptr) const;
    QString secretReference(const QString &sourceId, const QString &accountId) const;

private:
    struct PreparedV2 {
        QString group;
        QVariantMap previousValues;
        QString oldReference;
        QString format;
        QStringList configured;
        QVariantMap parameters;
        QByteArray secret;
        bool rotating = false;
    };
    std::optional<PreparedV2> prepareV2(const SourceAccountSaveV2 &request,
                                      bool resolveUnchanged, QString *error);
    QString groupFor(const QString &sourceId, const QString &accountId) const;
    QString legacyEncodedGroupFor(const QString &sourceId, const QString &accountId) const;
    QString legacyRawGroupFor(const QString &sourceId, const QString &accountId) const;
    QString matchingGroupFor(const QString &sourceId, const QString &accountId) const;
    QVariantMap recordValues(const QString &group) const;
    std::optional<StoredSourceAccount> storedAccountForGroup(const QString &group) const;
    QList<StoredSourceAccount> accountsForRoot(const QString &root) const;
    bool restoreRecord(const QString &group, const QVariantMap &values);
    bool writeRecord(const QString &group, const SourceAccount &account,
                     const QVariantMap &parameters, bool enabled,
                     const QString &secretReference);

    QSettings *m_settings = nullptr;
    ISecretStore *m_secretStore = nullptr;
};
