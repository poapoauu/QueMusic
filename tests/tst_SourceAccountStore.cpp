#include "SourceAccountStore.h"

#include <QFile>
#include <QHash>
#include <QSettings>
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
        Q_UNUSED(error)
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

private:
    QHash<QString, QByteArray> m_values;
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
    void refusesToReconstructAccountWhenSecretIsMissing();
    void rejectsSensitiveParameterNames();
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
