#include "SourceAccountStore.h"
#include "SourceSettingsValidation.h"
#include "v2/SourceSecretsV2.h"

#include <QDir>
#include <QFile>
#include <QSettings>
#include <QTemporaryDir>
#include <QTest>
#include <limits>

namespace {
// Only the external secure backend is replaced; metadata uses real temporary INI files.
class Secrets final : public ISecretStore {
public:
    QMap<QString, QByteArray> values;
    mutable int reads = 0;
    int writes = 0, removes = 0;
    bool failWrite = false, failRead = false;
    QString failRemove;
    bool write(const QString &ref, const QByteArray &bytes, QString *error) override
    {
        ++writes;
        if (failWrite) { if (error) *error = "PRIVATE backend detail"; return false; }
        values[ref] = bytes;
        return true;
    }
    std::optional<QByteArray> read(const QString &ref, QString *error) const override
    {
        ++reads;
        if (failRead || !values.contains(ref)) {
            if (error) *error = "PRIVATE backend detail";
            return std::nullopt;
        }
        return values.value(ref);
    }
    bool remove(const QString &ref, QString *error) override
    {
        ++removes;
        if (ref == failRemove) { if (error) *error = "PRIVATE backend detail"; return false; }
        values.remove(ref);
        return true;
    }
    void resetCalls() { reads = writes = removes = 0; }
    int calls() const { return reads + writes + removes; }
};
SettingsFieldV2 field(QString id, SettingsFieldTypeV2 type, bool required = false)
{
    SettingsFieldV2 f;
    f.id = id; f.type = type; f.required = required;
    return f;
}
SettingsSchemaV2 schema(QList<SettingsFieldV2> fields)
{
    return {{"main", "settings.main", fields}};
}
SourceAccountSaveV2 request(SettingsSchemaV2 fields, QVariantMap draft = {})
{
    return {"org.example.source", "example", "home/office", "Home", true, 7, fields, draft};
}
struct Fixture {
    QTemporaryDir dir;
    QSettings settings{dir.filePath("accounts.ini"), QSettings::IniFormat};
    Secrets secrets;
    SourceAccountStore store{&settings, &secrets};
    QString reference() const { return store.secretReference("example", "home/office"); }
    std::optional<StoredSourceAccount> account() const { return store.storedAccount("example", "home/office"); }
};
SettingsSchemaV2 credentials()
{
    return schema({field("password", SettingsFieldTypeV2::Secret, true),
                   field("token", SettingsFieldTypeV2::Secret, true),
                   field("folder", SettingsFieldTypeV2::Directory)});
}
}

class SourceSettingsStorageTest : public QObject {
    Q_OBJECT
private slots:
    void envelopeRoundtripAndExplicitLegacy();
    void malformedEnvelope_data();
    void malformedEnvelope();
    void genericRoundtripWithoutBackend();
    void oversizeSecretFailsBeforeStorageAccess();
    void invalidDraft_data();
    void invalidDraft();
    void invalidSchema_data();
    void invalidSchema();
    void defaultsAndSecretClassification();
    void requiredAndConstraintBoundaries();
    void twoSecretsPartialEditAndZeroIoPreservation();
    void legacyPreservationAndAmbiguity();
    void incompatibleSecretSchema();
    void failuresRollBack();
    void metadataFailureCleansNewSecret();
    void identityAndLegacyRestrictions();
};

void SourceSettingsStorageTest::envelopeRoundtripAndExplicitLegacy()
{
    SourceNamedSecretsV2 map{{"a", QByteArray("\0\xff", 2)}, {"b", ""}};
    const auto encoded = encodeSourceSecretsV2(map);
    QVERIFY(encoded);
    QCOMPARE(*encoded, QByteArray("QueMusic.SourceSecrets/2\n{\"a\":\"AP8=\",\"b\":\"\"}"));
    const auto decoded = decodeSourceSecretsV2(*encoded);
    QVERIFY(decoded); QCOMPARE(*decoded, map);
    QVERIFY(decodeSourceSecretsV2("QueMusic.SourceSecrets/2\n{}"));
    QVERIFY(encodeSourceSecretsV2({}));
    QVERIFY(!encodeSourceSecretsV2({{"bad/id", "x"}}));
    QVERIFY(!encodeSourceSecretsV2({{"a", QByteArray(1024 * 1024, 'a')}}));
    QCOMPARE(sourceSecretInputKindV2("old-password"), SourceSecretInputKindV2::LegacyRaw);
    QCOMPARE(sourceSecretInputKindV2(*encoded), SourceSecretInputKindV2::NamedEnvelope);
    QCOMPARE(sourceSecretInputKindV2("QueMusic.SourceSecrets/2 broken"), SourceSecretInputKindV2::MalformedEnvelope);
}
void SourceSettingsStorageTest::malformedEnvelope_data()
{
    QTest::addColumn<QByteArray>("bytes");
    for (const auto &payload : {QByteArray(""), QByteArray("[]"), QByteArray("{\"a\":1}"),
          QByteArray("{\"a/b\":\"eA==\"}"), QByteArray("{\"\":\"\"}"),
          QByteArray("{\"a\":\"x\"}"), QByteArray("{\"a\":\"AB==\"}"),
          QByteArray("{\"a\":\"eA== \"}"), QByteArray("{\"a\":\"\",\"a\":\"eA==\"}"),
          QByteArray("{\"a\":\"\",\"\\u0061\":\"eA==\"}"), QByteArray("{} trailing"),
          QByteArray("{\"a\":{}}"), QByteArray("{\"é\":\"\"}")})
        QTest::newRow(payload.constData()) << QByteArray("QueMusic.SourceSecrets/2\n") + payload;
    QTest::newRow("wrong-prefix") << QByteArray("QueMusic.SourceSecrets/1\n{}");
    QTest::newRow("oversize") << QByteArray("QueMusic.SourceSecrets/2\n") + QByteArray(1024 * 1024, ' ');
}
void SourceSettingsStorageTest::malformedEnvelope()
{
    QFETCH(QByteArray, bytes);
    QVERIFY(!decodeSourceSecretsV2(bytes));
    QCOMPARE(sourceSecretInputKindV2(bytes), SourceSecretInputKindV2::MalformedEnvelope);
}
void SourceSettingsStorageTest::genericRoundtripWithoutBackend()
{
    Fixture f;
    auto choice = field("mode", SettingsFieldTypeV2::Choice, true);
    choice.choices = {QString("local"), true, 4};
    auto count = field("count", SettingsFieldTypeV2::Integer, true);
    count.constraints = {{"min", 1}, {"max", 10}};
    auto s = schema({field("folder", SettingsFieldTypeV2::Directory, true),
                     field("active", SettingsFieldTypeV2::Boolean, true), count, choice});
    auto r = request(s, {{"folder", "/not/a/real/path"}, {"active", false}, {"count", 4.0}, {"mode", "local"}});
    SourceAccountStore store(&f.settings, nullptr);
    QString error;
    QVERIFY2(store.saveValidatedV2(r, &error), qPrintable(error));
    QSettings reopened(f.settings.fileName(), QSettings::IniFormat);
    SourceAccountStore reload(&reopened, nullptr);
    auto a = reload.storedAccount(r.sourceId, r.accountId);
    QVERIFY(a); QCOMPARE(a->recordVersion, 2); QCOMPARE(a->configurationVersion, 7);
    QCOMPARE(a->pluginPackageId, r.pluginPackageId);
    QCOMPARE(a->sourceInstanceId, QString("example/home/office"));
    QCOMPARE(a->parameters.value("active").metaType().id(), QMetaType::Bool);
    QCOMPARE(a->parameters.value("count").toLongLong(), 4);
    QCOMPARE(a->parameters.value("mode").toString(), QString("local"));
    QCOMPARE(a->parameters.value("folder").toString(), QString("/not/a/real/path"));
    QVERIFY(a->secretReference.isEmpty());
    QVERIFY(reload.sourceAccount(r.sourceId, r.accountId));
    r.draft.clear(); r.displayName = "Edited";
    QVERIFY(reload.saveValidatedV2(r));
    QCOMPARE(reload.storedAccount(r.sourceId, r.accountId)->parameters, a->parameters);
    QVERIFY(reload.setEnabled(r.sourceId, r.accountId, false));
    QCOMPARE(reload.accounts().size(), 1);
    QVERIFY(reload.remove(r.sourceId, r.accountId));
    QVERIFY(reload.accounts().isEmpty());
    UnavailableSecretStore unavailable;
    SourceAccountStore offline(&f.settings, &unavailable);
    r.draft = {{"folder", "/tmp"}, {"active", true}, {"count", 2}, {"mode", true}};
    QVERIFY(offline.saveValidatedV2(r));
    QVERIFY(offline.sourceAccount(r.sourceId, r.accountId));
    QVERIFY(offline.remove(r.sourceId, r.accountId));
}
void SourceSettingsStorageTest::oversizeSecretFailsBeforeStorageAccess()
{
    SourceAccountStore store(nullptr, nullptr);
    auto r = request(schema({field("password", SettingsFieldTypeV2::Secret, true)}),
                     {{"password", QString(1024 * 1024, 'x')}});
    QString error;
    QVERIFY(!store.saveValidatedV2(r, &error));
    QCOMPARE(error, QString("source.settings.invalidSecretEnvelope"));
}
void SourceSettingsStorageTest::invalidDraft_data()
{
    QTest::addColumn<int>("type"); QTest::addColumn<QVariant>("value");
    auto row = [](const char *name, SettingsFieldTypeV2 type, QVariant value) {
        QTest::newRow(name) << int(type) << value;
    };
    row("integer-bool", SettingsFieldTypeV2::Integer, true);
    row("integer-string", SettingsFieldTypeV2::Integer, "4");
    row("fraction", SettingsFieldTypeV2::Integer, 1.2);
    row("nan", SettingsFieldTypeV2::Integer, std::numeric_limits<double>::quiet_NaN());
    row("infinity", SettingsFieldTypeV2::Integer, std::numeric_limits<double>::infinity());
    row("unsafe", SettingsFieldTypeV2::Integer, qlonglong(9007199254740992LL));
    row("bool-string", SettingsFieldTypeV2::Boolean, "true");
    row("text-map", SettingsFieldTypeV2::Text, QVariantMap{{"token", "PRIVATE"}});
    row("text-bytes", SettingsFieldTypeV2::Text, QByteArray("PRIVATE"));
    row("directory-list", SettingsFieldTypeV2::Directory, QStringList{"a"});
    row("url-relative", SettingsFieldTypeV2::Url, "/a");
    row("url-ftp", SettingsFieldTypeV2::Url, "ftp://example.test");
    row("url-userinfo", SettingsFieldTypeV2::Url, "https://user:PRIVATE@example.test");
    row("url-empty-userinfo", SettingsFieldTypeV2::Url, "https://@example.test");
    row("url-no-host", SettingsFieldTypeV2::Url, "https:/abc");
    row("choice-type", SettingsFieldTypeV2::Choice, true);
    row("choice-unknown", SettingsFieldTypeV2::Choice, "other");
}
void SourceSettingsStorageTest::invalidDraft()
{
    QFETCH(int, type); QFETCH(QVariant, value);
    Fixture f;
    auto item = field("value", SettingsFieldTypeV2(type), true);
    if (item.type == SettingsFieldTypeV2::Choice) item.choices = {1, QString("one")};
    QString error;
    QVERIFY(!f.store.saveValidatedV2(request(schema({item}), {{"value", value}}), &error));
    QVERIFY(error.startsWith("source.settings.")); QVERIFY(!error.contains("PRIVATE"));
    QCOMPARE(f.secrets.calls(), 0); QVERIFY(f.settings.allKeys().isEmpty());
}
void SourceSettingsStorageTest::invalidSchema_data()
{
    QTest::addColumn<SettingsSchemaV2>("s");
    auto base = field("value", SettingsFieldTypeV2::Text);
    QTest::newRow("duplicate-field") << schema({base, base});
    QTest::newRow("duplicate-section") << SettingsSchemaV2{{"main", {}, {}}, {"main", {}, {}}};
    QTest::newRow("unsafe-section") << SettingsSchemaV2{{"../bad", {}, {base}}};
    for (const QString &id : {QString(), QString("../x"), QString("é"), QString("x y")}) {
        auto b = base; b.id = id; QTest::newRow(qPrintable("id-" + id)) << schema({b});
    }
    base.type = SettingsFieldTypeV2(999); QTest::newRow("unknown-enum") << schema({base});
    base = field("value", SettingsFieldTypeV2::Boolean); base.secret = true;
    QTest::newRow("secret-bool") << schema({base});
    base = field("value", SettingsFieldTypeV2::Choice); base.secret = true; base.choices = {"a"};
    QTest::newRow("secret-choice") << schema({base});
    base.secret = false; base.choices.clear(); QTest::newRow("empty-choices") << schema({base});
    base.choices = {QVariantMap{}}; QTest::newRow("nested-choice") << schema({base});
    base.choices = {1.5}; QTest::newRow("fraction-choice") << schema({base});
    base = field("value", SettingsFieldTypeV2::Text);
    int invalidConstraintIndex = 0;
    for (auto constraints : {QVariantMap{{"mystery", 1}}, QVariantMap{{"min", 1}},
           QVariantMap{{"minLength", -1}}, QVariantMap{{"maxLength", "2"}},
           QVariantMap{{"minLength", 4}, {"maxLength", 2}}, QVariantMap{{"pattern", "["}},
           QVariantMap{{"pattern", true}}}) {
        base.constraints = constraints;
        QTest::newRow(qPrintable(QString("constraint-%1").arg(invalidConstraintIndex++))) << schema({base});
    }
    base = field("value", SettingsFieldTypeV2::Integer);
    base.constraints = {{"min", 5}, {"max", 1}}; QTest::newRow("inverted-range") << schema({base});
    base.constraints = {{"min", 0.5}}; QTest::newRow("fraction-bound") << schema({base});
    base.constraints.clear(); base.defaultValue = "PRIVATE"; QTest::newRow("invalid-default") << schema({base});
}
void SourceSettingsStorageTest::invalidSchema()
{
    QFETCH(SettingsSchemaV2, s);
    Fixture f; QString error;
    QVERIFY(!f.store.saveValidatedV2(request(s), &error));
    QVERIFY(error.startsWith("source.settings.")); QVERIFY(!error.contains("PRIVATE"));
    QCOMPARE(f.secrets.calls(), 0); QVERIFY(f.settings.allKeys().isEmpty());
}
void SourceSettingsStorageTest::defaultsAndSecretClassification()
{
    Fixture f;
    auto text = field("name", SettingsFieldTypeV2::Text, true);
    text.defaultValue = "alpha"; text.constraints = {{"minLength", 3}, {"maxLength", 6}, {"pattern", "^[a-z]+$"}};
    auto secret = field("password", SettingsFieldTypeV2::Secret, true); // flag remains false
    secret.defaultValue = "PRIVATE-default";
    auto flagged = field("endpoint", SettingsFieldTypeV2::Url); flagged.secret = true;
    auto s = schema({text, secret, flagged}); QString error;
    QVERIFY(!f.store.saveValidatedV2(request(s), &error));
    QCOMPARE(f.secrets.calls(), 0);
    QVERIFY(!f.store.saveValidatedV2(request(s, {{"password", "PRIVATE"}, {"auth", "PRIVATE"}})));
    QVERIFY(!f.store.saveValidatedV2(request(s, {{"password", "PRIVATE"}, {"name", "TOOLONG"}})));
    QVERIFY(f.store.saveValidatedV2(request(s, {{"password", "PRIVATE"}, {"endpoint", "https://example.test"}})));
    QCOMPARE(f.account()->parameters, QVariantMap({{"name", "alpha"}}));
    auto split = validateSourceSettingsV2(s, {{"password", "new"}}, {{"name", "bravo"}}, {"password"}, true);
    QVERIFY(split.errorKey.isEmpty()); QCOMPARE(split.parameters.value("name").toString(), QString("bravo"));
    QCOMPARE(split.secretUpdates, SourceNamedSecretsV2({{"password", "new"}}));
    QVERIFY(split.preserveOmittedSecrets);
    for (const auto &key : f.settings.allKeys()) QVERIFY(!f.settings.value(key).toString().contains("PRIVATE"));
    SourceAccountStore noBackend(&f.settings, nullptr);
    auto r = request(s, {{"password", "new"}}); r.accountId = "other";
    QVERIFY(!noBackend.saveValidatedV2(r, &error));
    QVERIFY(error.startsWith("source.settings."));
}
void SourceSettingsStorageTest::requiredAndConstraintBoundaries()
{
    auto number = field("number", SettingsFieldTypeV2::Integer, true);
    number.constraints = {{"min", -2}, {"max", 2}};
    auto name = field("name", SettingsFieldTypeV2::Text, true);
    name.constraints = {{"minLength", 2}, {"maxLength", 4}, {"pattern", "^[a-z]+$"}};
    auto s = schema({number, name});
    QCOMPARE(validateSourceSettingsV2(s, {}).errorKey, QString("source.settings.requiredField"));
    for (const QVariant &invalid : {QVariant(-3), QVariant(3)})
        QVERIFY(!validateSourceSettingsV2(s, {{"number", invalid}, {"name", "ok"}}).errorKey.isEmpty());
    for (const QString &invalid : {QString("a"), QString("abcde"), QString("AB"), QString()})
        QVERIFY(!validateSourceSettingsV2(s, {{"number", 0}, {"name", invalid}}).errorKey.isEmpty());
    QVERIFY(validateSourceSettingsV2(s, {{"number", -2}, {"name", "ok"}}).errorKey.isEmpty());
    QVERIFY(validateSourceSettingsV2(s, {{"number", 2}, {"name", "abcd"}}).errorKey.isEmpty());
    auto safe = schema({field("n", SettingsFieldTypeV2::Integer, true)});
    for (qlonglong value : {-9007199254740991LL, 9007199254740991LL}) {
        auto result = validateSourceSettingsV2(safe, {{"n", value}});
        QVERIFY(result.errorKey.isEmpty()); QCOMPARE(result.parameters.value("n").toLongLong(), value);
    }
    name.defaultValue = "okay";
    auto addedOnEdit = validateSourceSettingsV2(schema({name}), {}, {}, {}, true);
    QCOMPARE(addedOnEdit.errorKey, QString("source.settings.requiredField"));
}
void SourceSettingsStorageTest::twoSecretsPartialEditAndZeroIoPreservation()
{
    Fixture f; auto r = request(credentials(), {{"password", "first"}, {"token", "second"}, {"folder", "/a"}});
    QVERIFY(f.store.saveValidatedV2(r)); const QString old = f.reference();
    QCOMPARE(f.account()->configuredSecretFieldIds, QStringList({"password", "token"}));
    f.secrets.resetCalls(); r.draft = {{"password", ""}};
    QVERIFY(f.store.saveValidatedV2(r)); QCOMPARE(f.reference(), old); QCOMPARE(f.secrets.calls(), 0);
    SourceAccountStore offline(&f.settings, nullptr);
    QVERIFY(offline.saveValidatedV2(r)); QCOMPARE(f.reference(), old);
    QCOMPARE(f.secrets.calls(), 0);
    r.draft = {{"password", "replacement"}};
    QVERIFY(f.store.saveValidatedV2(r)); QCOMPARE(f.secrets.reads, 1);
    QVERIFY(f.reference() != old); QVERIFY(!f.secrets.values.contains(old));
    QCOMPARE(*decodeSourceSecretsV2(f.secrets.values.value(f.reference())), SourceNamedSecretsV2({{"password", "replacement"}, {"token", "second"}}));
    f.secrets.resetCalls(); f.secrets.failRead = true;
    r.draft = {{"password", "all-new"}, {"token", "all-new-too"}};
    QVERIFY(f.store.saveValidatedV2(r)); QCOMPARE(f.secrets.reads, 0);
}
void SourceSettingsStorageTest::legacyPreservationAndAmbiguity()
{
    Fixture f;
    // Seed a pre-encoded legacy group that must be updated in place.
    const QString group = "sources/example/home%2Foffice";
    f.settings.setValue(group + "/version", 1); f.settings.setValue(group + "/sourceId", "example");
    f.settings.setValue(group + "/accountId", "home/office"); f.settings.setValue(group + "/secretReference", "legacy");
    f.secrets.values["legacy"] = "raw-password";
    auto single = schema({field("password", SettingsFieldTypeV2::Secret, true)});
    QVERIFY(f.store.saveValidatedV2(request(single)));
    QCOMPARE(f.reference(), QString("legacy")); QCOMPARE(f.secrets.calls(), 0);
    QCOMPARE(f.settings.value(group + "/secretFormat").toString(), QString("legacyRaw"));
    QCOMPARE(f.secrets.values.value("legacy"), QByteArray("raw-password"));
    QVERIFY(!f.settings.contains("sourceAccountsV2/example/home%2Foffice/version"));
    QString error;
    QVERIFY(!f.store.saveValidatedV2(request(credentials()), &error));
    QCOMPARE(error, QString("source.settings.credentialsReentryRequired")); QCOMPARE(f.secrets.calls(), 0);
    QVERIFY(!f.store.saveValidatedV2(request(credentials(), {{"password", "new"}}), &error));
    QVERIFY(f.store.saveValidatedV2(request(credentials(), {{"password", "new"}, {"token", "new-token"}})));
    QCOMPARE(f.secrets.reads, 0);
}
void SourceSettingsStorageTest::incompatibleSecretSchema()
{
    Fixture f; QVERIFY(f.store.saveValidatedV2(request(credentials(), {{"password", "a"}, {"token", "b"}})));
    const auto old = f.reference(); f.secrets.resetCalls(); QString error;
    auto changed = schema({field("password", SettingsFieldTypeV2::Text), field("token", SettingsFieldTypeV2::Secret, true)});
    QVERIFY(!f.store.saveValidatedV2(request(changed), &error));
    QCOMPARE(error, QString("source.settings.credentialsReentryRequired"));
    QCOMPARE(f.reference(), old); QCOMPARE(f.secrets.calls(), 0);
    // Corrupt envelope input must never be reinterpreted as legacy raw.
    f.secrets.values[old] = "QueMusic.SourceSecrets/2\n{bad}";
    QVERIFY(!f.store.saveValidatedV2(request(credentials(), {{"password", "new"}}), &error));
    QCOMPARE(error, QString("source.settings.credentialsReentryRequired"));
    QCOMPARE(f.reference(), old); QCOMPARE(f.secrets.writes, 0);
}
void SourceSettingsStorageTest::failuresRollBack()
{
    Fixture f; auto r = request(credentials(), {{"password", "a"}, {"token", "b"}});
    QVERIFY(f.store.saveValidatedV2(r)); auto old = f.reference(); auto oldBytes = f.secrets.values.value(old);
    r.draft = {{"password", "PRIVATE-new"}}; r.displayName = "Changed"; QString error;
    f.secrets.failWrite = true;
    QVERIFY(!f.store.saveValidatedV2(r, &error)); QVERIFY(!error.contains("PRIVATE"));
    QCOMPARE(f.reference(), old); QCOMPARE(f.account()->displayName, QString("Home"));
    f.secrets.failWrite = false; f.secrets.failRemove = old;
    QVERIFY(!f.store.saveValidatedV2(r, &error)); QVERIFY(!error.contains("PRIVATE"));
    QCOMPARE(f.reference(), old); QCOMPARE(f.secrets.values.size(), 1); QCOMPARE(f.secrets.values.value(old), oldBytes);
    QVERIFY(!f.store.remove(r.sourceId, r.accountId, &error)); QVERIFY(!error.contains("PRIVATE"));
    QCOMPARE(f.reference(), old); QCOMPARE(f.account()->displayName, QString("Home"));
    f.secrets.failRead = true;
    QVERIFY(!f.store.sourceAccount(r.sourceId, r.accountId, &error)); QVERIFY(!error.contains("PRIVATE"));
    QVERIFY(!f.store.saveValidatedV2(r, &error)); QVERIFY(!error.contains("PRIVATE"));
}
void SourceSettingsStorageTest::metadataFailureCleansNewSecret()
{
    Fixture f; auto r = request(credentials(), {{"password", "a"}, {"token", "b"}});
    QVERIFY(f.store.saveValidatedV2(r)); const auto old = f.reference();
    QVERIFY(QFile::remove(f.settings.fileName())); QVERIFY(QDir().mkdir(f.settings.fileName()));
    r.draft = {{"password", "new"}}; QString error;
    QVERIFY(!f.store.saveValidatedV2(r, &error)); QVERIFY(error.startsWith("source.settings."));
    QCOMPARE(f.reference(), old); QCOMPARE(f.secrets.values.size(), 1);
    QVERIFY(f.secrets.values.contains(old));
}
void SourceSettingsStorageTest::identityAndLegacyRestrictions()
{
    Fixture f; auto r = request({}); QString error;
    for (int i = 0; i < 5; ++i) {
        auto bad = r;
        if (i == 0) bad.sourceId.clear(); if (i == 1) bad.accountId.clear();
        if (i == 2) bad.pluginPackageId.clear(); if (i == 3) bad.sourceId = "a/b";
        if (i == 4) bad.configurationVersion = 0;
        QVERIFY(!f.store.saveValidatedV2(bad, &error));
    }
    QCOMPARE(f.secrets.calls(), 0); QVERIFY(f.settings.allKeys().isEmpty());
    QVERIFY(f.store.saveValidatedV2(r)); r.accountId = "home%2Foffice";
    QVERIFY(f.store.saveValidatedV2(r)); QCOMPARE(f.store.accounts().size(), 2);
    SourceAccount legacy{"example", "legacy", "Legacy", {{"folder", "/tmp"}}, "raw"};
    QVERIFY(!f.store.upsert(legacy));
}

QTEST_GUILESS_MAIN(SourceSettingsStorageTest)
#include "tst_SourceSettingsStorage.moc"
