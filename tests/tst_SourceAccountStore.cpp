#include "SourceAccountStore.h"

#include <QDir>
#include <QFile>
#include <QHash>
#include <QSettings>
#include <QSet>
#include <QTemporaryDir>
#include <QTest>

namespace {

class MemorySecretStore final : public ISecretStore {
public:
    bool write(const QString &reference, const QByteArray &secret, QString *error) override
    {
        if (reference.isEmpty()) {
            if (error) {
                *error = QStringLiteral("Secret reference is required");
            }
            return false;
        }
        m_values.insert(reference, secret);
        return true;
    }

    std::optional<QByteArray> read(const QString &reference, QString *error) const override
    {
        const auto value = m_values.constFind(reference);
        if (value == m_values.cend()) {
            if (error) {
                *error = QStringLiteral("Secret is unavailable");
            }
            return std::nullopt;
        }
        return *value;
    }

    bool remove(const QString &reference, QString *error) override
    {
        if (m_removeFailures.contains(reference)) {
            if (error) {
                *error = QStringLiteral("Secret removal failed");
            }
            return false;
        }
        m_values.remove(reference);
        return true;
    }

    QByteArray value(const QString &reference) const
    {
        return m_values.value(reference);
    }

    bool isEmpty() const
    {
        return m_values.isEmpty();
    }

    int size() const
    {
        return m_values.size();
    }

    void failRemovalFor(const QString &reference)
    {
        m_removeFailures.insert(reference);
    }

private:
    QHash<QString, QByteArray> m_values;
    QSet<QString> m_removeFailures;
};

SourceAccount accountWithSecret()
{
    return {QStringLiteral("navidrome"),
            QStringLiteral("home"),
            QStringLiteral("Home server"),
            {{QStringLiteral("serverUrl"), QStringLiteral("https://music.example.invalid")},
             {QStringLiteral("username"), QStringLiteral("unit-test-user")}},
            QByteArrayLiteral("unit-test-password")};
}

}

class SourceAccountStoreTest : public QObject {
    Q_OBJECT

private slots:
    void persistsOnlyMetadataAndReconstructsAccount();
    void removesMetadataAndSecret();
    void removesFreshSecretWhenMetadataWriteFails();
    void keepsAccountRecoverableWhenMetadataRemovalFails();
    void refusesToReconstructAccountWhenSecretIsMissing();
    void rejectsSensitiveParameterNames();
    void rejectsUntrustedParameterNames_data();
    void rejectsUntrustedParameterNames();
    void keepsSlashContainingAccountIdentitiesDistinct();
    void preservesPreviousAccountWhenOldSecretCleanupFails();
    void unavailableSecretStoreNeverPersistsSecrets();
};

void SourceAccountStoreTest::persistsOnlyMetadataAndReconstructsAccount()
{
    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());
    const QString settingsPath = temporaryDirectory.filePath(QStringLiteral("accounts.ini"));
    QSettings settings(settingsPath, QSettings::IniFormat);
    MemorySecretStore secretStore;
    SourceAccountStore store(&settings, &secretStore);
    const SourceAccount account = accountWithSecret();

    QVERIFY(store.upsert(account, false));

    const QString reference = store.secretReference(account.sourceId, account.accountId);
    QVERIFY(!reference.isEmpty());
    QCOMPARE(secretStore.value(reference), QByteArrayLiteral("unit-test-password"));
    QCOMPARE(settings.value(QStringLiteral("sources/navidrome/home/version")).toInt(), 1);
    QCOMPARE(settings.value(QStringLiteral("sources/navidrome/home/sourceId")).toString(),
             QStringLiteral("navidrome"));
    QCOMPARE(settings.value(QStringLiteral("sources/navidrome/home/accountId")).toString(),
             QStringLiteral("home"));
    QCOMPARE(settings.value(QStringLiteral("sources/navidrome/home/displayName")).toString(),
             QStringLiteral("Home server"));
    QCOMPARE(settings.value(QStringLiteral("sources/navidrome/home/enabled")).toBool(), false);
    QCOMPARE(settings.value(QStringLiteral("sources/navidrome/home/parameters/serverUrl")).toString(),
             QStringLiteral("https://music.example.invalid"));
    QCOMPARE(settings.value(QStringLiteral("sources/navidrome/home/secretReference")).toString(), reference);

    QFile settingsFile(settingsPath);
    QVERIFY(settingsFile.open(QIODevice::ReadOnly));
    const QByteArray rawSettings = settingsFile.readAll();
    QVERIFY(rawSettings.contains("navidrome"));
    QVERIFY(rawSettings.contains("Home server"));
    QVERIFY(rawSettings.contains("music.example.invalid"));
    QVERIFY(rawSettings.contains(reference.toUtf8()));
    QVERIFY(!rawSettings.contains("unit-test-password"));

    const std::optional<StoredSourceAccount> stored =
        store.storedAccount(account.sourceId, account.accountId);
    QVERIFY(stored.has_value());
    QCOMPARE(stored->enabled, false);
    QCOMPARE(stored->parameters.value(QStringLiteral("username")).toString(),
             QStringLiteral("unit-test-user"));
    QCOMPARE(store.accounts().size(), 1);

    const std::optional<SourceAccount> restored =
        store.sourceAccount(account.sourceId, account.accountId);
    QVERIFY(restored.has_value());
    QCOMPARE(restored->sourceId, account.sourceId);
    QCOMPARE(restored->accountId, account.accountId);
    QCOMPARE(restored->displayName, account.displayName);
    QCOMPARE(restored->parameters, account.parameters);
    QCOMPARE(restored->secret, account.secret);
}

void SourceAccountStoreTest::removesMetadataAndSecret()
{
    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());
    QSettings settings(temporaryDirectory.filePath(QStringLiteral("accounts.ini")),
                       QSettings::IniFormat);
    MemorySecretStore secretStore;
    SourceAccountStore store(&settings, &secretStore);
    const SourceAccount account = accountWithSecret();

    QVERIFY(store.upsert(account));
    const QString reference = store.secretReference(account.sourceId, account.accountId);

    QVERIFY(store.remove(account.sourceId, account.accountId));
    QVERIFY(!settings.contains(QStringLiteral("sources/navidrome/home/secretReference")));
    QVERIFY(!store.storedAccount(account.sourceId, account.accountId).has_value());
    QCOMPARE(secretStore.value(reference), QByteArray());
}

void SourceAccountStoreTest::removesFreshSecretWhenMetadataWriteFails()
{
    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());
    QFile blockingParent(temporaryDirectory.filePath(QStringLiteral("not-a-directory")));
    QVERIFY(blockingParent.open(QIODevice::WriteOnly));
    blockingParent.close();
    const QString settingsPath = temporaryDirectory.filePath(
        QStringLiteral("not-a-directory/accounts.ini"));
    QSettings settings(settingsPath, QSettings::IniFormat);
    MemorySecretStore secretStore;
    SourceAccountStore store(&settings, &secretStore);

    QVERIFY(!store.upsert(accountWithSecret()));
    QVERIFY(secretStore.isEmpty());
}

void SourceAccountStoreTest::keepsAccountRecoverableWhenMetadataRemovalFails()
{
    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());
    const QString settingsPath = temporaryDirectory.filePath(QStringLiteral("accounts.ini"));
    QSettings settings(settingsPath, QSettings::IniFormat);
    MemorySecretStore secretStore;
    SourceAccountStore store(&settings, &secretStore);
    const SourceAccount account = accountWithSecret();

    QVERIFY(store.upsert(account));
    const QString reference = store.secretReference(account.sourceId, account.accountId);
    QVERIFY(QFile::remove(settingsPath));
    QVERIFY(QDir().mkdir(settingsPath));

    QString error;
    QVERIFY(!store.remove(account.sourceId, account.accountId, &error));
    QVERIFY(!error.isEmpty());
    QCOMPARE(secretStore.value(reference), account.secret);
    const std::optional<SourceAccount> restored =
        store.sourceAccount(account.sourceId, account.accountId);
    QVERIFY(restored.has_value());
    QCOMPARE(restored->secret, account.secret);
}

void SourceAccountStoreTest::refusesToReconstructAccountWhenSecretIsMissing()
{
    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());
    QSettings settings(temporaryDirectory.filePath(QStringLiteral("accounts.ini")),
                       QSettings::IniFormat);
    MemorySecretStore secretStore;
    SourceAccountStore store(&settings, &secretStore);
    const SourceAccount account = accountWithSecret();

    QVERIFY(store.upsert(account));
    QVERIFY(secretStore.remove(store.secretReference(account.sourceId, account.accountId), nullptr));

    QString error;
    QVERIFY(!store.sourceAccount(account.sourceId, account.accountId, &error).has_value());
    QVERIFY(!error.isEmpty());
}

void SourceAccountStoreTest::rejectsSensitiveParameterNames()
{
    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());
    QSettings settings(temporaryDirectory.filePath(QStringLiteral("accounts.ini")),
                       QSettings::IniFormat);
    MemorySecretStore secretStore;
    SourceAccountStore store(&settings, &secretStore);
    SourceAccount account = accountWithSecret();
    account.parameters.insert(QStringLiteral("password"), QByteArrayLiteral("must-not-persist"));

    QString error;
    QVERIFY(!store.upsert(account, true, &error));
    QVERIFY(error.contains(QStringLiteral("secret"), Qt::CaseInsensitive));
    QVERIFY(secretStore.isEmpty());
    QVERIFY(!settings.contains(QStringLiteral("sources/navidrome/home/secretReference")));
}

void SourceAccountStoreTest::rejectsUntrustedParameterNames_data()
{
    QTest::addColumn<QVariantMap>("parameters");
    QTest::newRow("auth") << QVariantMap{{QStringLiteral("auth"), QStringLiteral("opaque")}};
    QTest::newRow("access-key")
        << QVariantMap{{QStringLiteral("accessKey"), QStringLiteral("opaque")}};
    QTest::newRow("nested-authorization")
        << QVariantMap{{QStringLiteral("headers"),
                        QVariantMap{{QStringLiteral("Authorization"), QStringLiteral("Bearer opaque")}}}};
}

void SourceAccountStoreTest::rejectsUntrustedParameterNames()
{
    QFETCH(QVariantMap, parameters);
    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());
    QSettings settings(temporaryDirectory.filePath(QStringLiteral("accounts.ini")),
                       QSettings::IniFormat);
    MemorySecretStore secretStore;
    SourceAccountStore store(&settings, &secretStore);
    SourceAccount account = accountWithSecret();
    for (auto parameter = parameters.cbegin(); parameter != parameters.cend(); ++parameter) {
        account.parameters.insert(parameter.key(), parameter.value());
    }

    QString error;
    QVERIFY(!store.upsert(account, true, &error));
    QVERIFY(error.contains(QStringLiteral("metadata"), Qt::CaseInsensitive));
    QVERIFY(secretStore.isEmpty());
    QVERIFY(!settings.contains(QStringLiteral("sources/navidrome/home/secretReference")));
}

void SourceAccountStoreTest::keepsSlashContainingAccountIdentitiesDistinct()
{
    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());
    QSettings settings(temporaryDirectory.filePath(QStringLiteral("accounts.ini")),
                       QSettings::IniFormat);
    MemorySecretStore secretStore;
    SourceAccountStore store(&settings, &secretStore);
    SourceAccount first = accountWithSecret();
    first.sourceId = QStringLiteral("source/one");
    first.accountId = QStringLiteral("two");
    first.secret = QByteArrayLiteral("first-secret");
    SourceAccount second = accountWithSecret();
    second.sourceId = QStringLiteral("source");
    second.accountId = QStringLiteral("one/two");
    second.secret = QByteArrayLiteral("second-secret");

    QVERIFY(store.upsert(first));
    QVERIFY(store.upsert(second));

    const std::optional<SourceAccount> restoredFirst =
        store.sourceAccount(first.sourceId, first.accountId);
    const std::optional<SourceAccount> restoredSecond =
        store.sourceAccount(second.sourceId, second.accountId);
    QVERIFY(restoredFirst.has_value());
    QVERIFY(restoredSecond.has_value());
    QCOMPARE(restoredFirst->secret, QByteArrayLiteral("first-secret"));
    QCOMPARE(restoredSecond->secret, QByteArrayLiteral("second-secret"));
    QCOMPARE(store.accounts().size(), 2);
}

void SourceAccountStoreTest::preservesPreviousAccountWhenOldSecretCleanupFails()
{
    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());
    QSettings settings(temporaryDirectory.filePath(QStringLiteral("accounts.ini")),
                       QSettings::IniFormat);
    MemorySecretStore secretStore;
    SourceAccountStore store(&settings, &secretStore);
    const SourceAccount original = accountWithSecret();
    QVERIFY(store.upsert(original));
    const QString originalReference = store.secretReference(original.sourceId, original.accountId);
    secretStore.failRemovalFor(originalReference);
    SourceAccount updated = original;
    updated.secret = QByteArrayLiteral("replacement-secret");

    QString error;
    QVERIFY(!store.upsert(updated, true, &error));
    QVERIFY(!error.isEmpty());
    QCOMPARE(store.secretReference(original.sourceId, original.accountId), originalReference);
    QCOMPARE(secretStore.value(originalReference), original.secret);
    const std::optional<SourceAccount> restored =
        store.sourceAccount(original.sourceId, original.accountId);
    QVERIFY(restored.has_value());
    QCOMPARE(restored->secret, original.secret);
    QCOMPARE(secretStore.size(), 1);
}

void SourceAccountStoreTest::unavailableSecretStoreNeverPersistsSecrets()
{
    UnavailableSecretStore secretStore;
    QString error;

    QVERIFY(!secretStore.write(QStringLiteral("reference"), QByteArrayLiteral("unit-test-password"),
                               &error));
    QVERIFY(error.contains(QStringLiteral("unavailable"), Qt::CaseInsensitive));
    QVERIFY(!secretStore.read(QStringLiteral("reference"), &error).has_value());
    QVERIFY(!secretStore.remove(QStringLiteral("reference"), &error));
}

QTEST_MAIN(SourceAccountStoreTest)
#include "tst_SourceAccountStore.moc"
