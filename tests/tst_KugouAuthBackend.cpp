#include "KugouAuthBackend.h"
#include "HostPluginUiSettingsBridge.h"
#include <QNetworkReply>
#include <QMetaProperty>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <cstring>

namespace {
class Reply final : public QNetworkReply {
public:
    Reply(const QNetworkRequest &request, QObject *parent) : QNetworkReply(parent)
    { setRequest(request); setUrl(request.url()); open(QIODevice::ReadOnly); }
    void deliver(const QByteArray &bytes)
    { body = bytes; setAttribute(QNetworkRequest::HttpStatusCodeAttribute, 200); setFinished(true); emit readyRead(); emit finished(); }
    void abort() override { aborted = true; setFinished(true); emit finished(); }
    qint64 bytesAvailable() const override { return body.size() - pos + QNetworkReply::bytesAvailable(); }
    bool aborted = false;
protected:
    qint64 readData(char *data, qint64 max) override
    { const auto n = qMin(max, qint64(body.size()) - pos); if (n <= 0) return -1; std::memcpy(data, body.constData() + pos, size_t(n)); pos += n; return n; }
private:
    QByteArray body; qint64 pos = 0;
};
class Network final : public QNetworkAccessManager {
public:
    QList<QPointer<Reply>> replies;
    QList<QNetworkRequest> requests;
protected:
    QNetworkReply *createRequest(Operation, const QNetworkRequest &request, QIODevice *) override
    { requests.append(request); auto *reply = new Reply(request, this); replies.append(reply); return reply; }
};
class Secrets final : public ISecretStore {
public:
    QHash<QString, QByteArray> values;
    bool failWrite = false, failRemove = false;
    int writes = 0;
    bool write(const QString &id, const QByteArray &bytes, QString *error) override
    { ++writes; if (failWrite) { if (error) *error = "PRIVATE_ERROR"; return false; } values[id] = bytes; return true; }
    std::optional<QByteArray> read(const QString &id, QString *) const override
    { return values.contains(id) ? std::optional<QByteArray>(values.value(id)) : std::nullopt; }
    bool remove(const QString &id, QString *) override
    { if (failRemove) return false; values.remove(id); return true; }
};
SourceAccountSaveV2 request(const QString &account)
{
    SettingsFieldV2 field; field.id = "credentialBundle"; field.type = SettingsFieldTypeV2::Secret;
    return {"org.quemusic.source.kugou", "kugou", account, "Fixture", true, 1, {{"auth", "fixture", {field}}}, {}};
}
struct Fixture {
    QTemporaryDir dir;
    QSettings settings{dir.filePath("accounts.ini"), QSettings::IniFormat};
    Secrets secrets;
    SourceAccountStore store{&settings, &secrets};
    HostPluginUiSettingsBridge bridge;
    PluginUiContext context;
    Network network;
    KugouAuthBackend *backend;
    Fixture(const QString &account = "one") : bridge(&store, nullptr, request(account), QStringList{"credentialBundle"})
    {
        context.setContext({"org.quemusic.source.kugou", "kugou", "kugou/" + account, account, PluginUiMode::Edit}, nullptr, &bridge, nullptr, nullptr);
        backend = new KugouAuthBackend(&context, &network, {1, 3000}, &context);
    }
    QByteArray record(const QString &user, const QString &token = "PRIVATE-old")
    {
        const auto device = KugouQrProtocol::createDevice(context.sourceInstanceId(), context.accountId());
        return *KugouCredentialRecord::encode({*device, {user, token}});
    }
};
QByteArray confirmed(const QString &user = "123", const QString &token = "PRIVATE-new")
{ return QJsonDocument(QJsonObject{{"status", 1}, {"data", QJsonObject{{"status", 4}, {"user_id", user}, {"token", token}}}}).toJson(QJsonDocument::Compact); }
}

class KugouAuthBackendTest final : public QObject {
    Q_OBJECT
private slots:
    void candidateIsStoredAndVerifiedWithoutAuthenticationGrant()
    {
        Fixture f; QVERIFY(f.backend->startQr()); QCOMPARE(f.backend->state(), "fetching");
        f.network.replies[0]->deliver("{\"status\":1,\"data\":{\"qrcode\":\"KEY\"}}");
        QCOMPARE(f.backend->state(), "waiting"); QVERIFY(!f.backend->qrUrl().isEmpty());
        QTRY_COMPARE(f.network.replies.size(), 2); f.network.replies[1]->deliver(confirmed());
        QCOMPARE(f.backend->state(), "storing"); QVERIFY(!f.backend->credentialStored());
        QTRY_COMPARE(f.backend->state(), "stored"); QVERIFY(f.backend->credentialStored()); QVERIFY(f.backend->qrUrl().isEmpty());
        const auto bytes = f.bridge.readBackendSecret("credentialBundle"); QVERIFY(bytes);
        const auto account = KugouCredentialRecord::decode(*bytes, "kugou/one", "one", "123"); QVERIFY(account);
        QCOMPARE(account->device.requestIdentity.mid.toUtf8(), f.network.requests[0].rawHeader("mid"));
        QVERIFY(f.bridge.sections().isEmpty()); QVERIFY(f.bridge.publicValues().isEmpty());
        for (int i = f.backend->metaObject()->propertyOffset(); i < f.backend->metaObject()->propertyCount(); ++i) {
            const auto property = f.backend->metaObject()->property(i);
            QVERIFY(!property.read(f.backend).toString().contains("PRIVATE"));
        }
        QVERIFY(f.backend->clearAccount()); QTRY_COMPARE(f.backend->state(), "cleared");
        QVERIFY(!f.backend->credentialStored()); QVERIFY(f.secrets.values.isEmpty());
    }
    void storageFailureAndUserConflictPreserveOldBinding()
    {
        Fixture f;
        const auto old = f.record("123"); f.bridge.saveBackendSecret("credentialBundle", old); QCoreApplication::processEvents();
        QVERIFY(f.backend->startQr()); QVERIFY(f.backend->credentialStored());
        f.network.replies[0]->deliver("{\"status\":1,\"data\":{\"qrcode\":\"KEY\"}}");
        QTRY_COMPARE(f.network.replies.size(), 2); f.secrets.failWrite = true;
        f.network.replies[1]->deliver(confirmed()); QTRY_COMPARE(f.backend->state(), "failed");
        QCOMPARE(f.backend->errorKey(), "kugou.authStorageFailed"); QVERIFY(f.backend->credentialStored());
        QCOMPARE(*f.bridge.readBackendSecret("credentialBundle"), old);
        f.secrets.failWrite = false; const auto writes = f.secrets.writes;
        QVERIFY(f.backend->startQr()); f.network.replies[2]->deliver("{\"status\":1,\"data\":{\"qrcode\":\"NEW\"}}");
        QTRY_COMPARE(f.network.replies.size(), 4); f.network.replies[3]->deliver(confirmed("456"));
        QCOMPARE(f.backend->state(), "failed"); QCOMPARE(f.backend->errorKey(), "kugou.authUserConflict");
        QCOMPARE(f.secrets.writes, writes); QCOMPARE(*f.bridge.readBackendSecret("credentialBundle"), old);
        f.secrets.failRemove = true; QVERIFY(f.backend->clearAccount()); QTRY_COMPARE(f.backend->state(), "failed");
        QVERIFY(f.backend->credentialStored()); QCOMPARE(*f.bridge.readBackendSecret("credentialBundle"), old);
    }
    void malformedOrChangedBindingFailsClosed()
    {
        Fixture f; f.bridge.saveBackendSecret("credentialBundle", "PRIVATE-corrupt"); QCoreApplication::processEvents();
        QVERIFY(!f.backend->startQr()); QCOMPARE(f.backend->errorKey(), "kugou.authBindingUnavailable");
        QCOMPARE(f.network.replies.size(), 0);
        f.bridge.clearBackendSecret("credentialBundle"); QCoreApplication::processEvents();
        QVERIFY(f.backend->startQr()); f.network.replies[0]->deliver("{\"status\":1,\"data\":{\"qrcode\":\"KEY\"}}");
        QTRY_COMPARE(f.network.replies.size(), 2);
        const auto external = f.record("123"); f.bridge.saveBackendSecret("credentialBundle", external); QCoreApplication::processEvents();
        const auto writes = f.secrets.writes;
        f.network.replies[1]->deliver(confirmed()); QCOMPARE(f.backend->errorKey(), "kugou.authBindingChanged");
        QCOMPARE(f.secrets.writes, writes); QCOMPARE(*f.bridge.readBackendSecret("credentialBundle"), external);
    }
    void invalidationAndRetryFenceLateRepliesAndInstances()
    {
        Fixture one, two("two"); QVERIFY(one.backend->startQr()); QVERIFY(two.backend->startQr());
        const auto old = one.network.replies[0]; one.backend->cancelQr(); QVERIFY(old->aborted);
        QVERIFY(one.backend->startQr()); old->deliver(confirmed()); QCOMPARE(one.backend->state(), "fetching");
        QCOMPARE(one.secrets.writes, 0); QCOMPARE(two.backend->state(), "fetching");
        const auto pending = one.network.replies[1]; one.context.invalidate(); QCOMPARE(one.backend->state(), "invalidated");
        QVERIFY(pending->aborted); pending->deliver(confirmed()); QCOMPARE(one.secrets.writes, 0);
        QVERIFY(!one.backend->startQr()); QVERIFY(!one.backend->clearAccount()); QVERIFY(one.backend->qrUrl().isEmpty());
        two.network.replies[0]->deliver("{\"status\":1,\"data\":{\"qrcode\":\"TWO\"}}");
        QTRY_COMPARE(two.network.replies.size(), 2); two.network.replies[1]->deliver(confirmed());
        QTRY_COMPARE(two.backend->state(), "stored");
        const auto bytes = two.bridge.readBackendSecret("credentialBundle"); QVERIFY(bytes);
        QVERIFY(KugouCredentialRecord::decode(*bytes, "kugou/two", "two"));
        QVERIFY(!KugouCredentialRecord::decode(*bytes, "kugou/one", "one"));
    }
    void notificationReentrancyCanInvalidateOrDestroyBackend()
    {
        Fixture f;
        connect(f.backend, &KugouAuthBackend::changed, &f.context, [&] {
            if (f.backend->state() == "storing") f.context.invalidate();
        });
        QVERIFY(f.backend->startQr()); f.network.replies[0]->deliver("{\"status\":1,\"data\":{\"qrcode\":\"KEY\"}}");
        QTRY_COMPARE(f.network.replies.size(), 2); f.network.replies[1]->deliver(confirmed());
        QCOMPARE(f.backend->state(), "invalidated"); QCOMPARE(f.secrets.writes, 0);
        Fixture destroyed;
        QPointer<KugouAuthBackend> guard(destroyed.backend);
        connect(destroyed.backend, &KugouAuthBackend::changed, &destroyed.context, [&] {
            if (guard && guard->state() == "fetching") delete guard.data();
        });
        QVERIFY(destroyed.backend->startQr()); QVERIFY(guard.isNull()); QCOMPARE(destroyed.network.replies.size(), 0);
    }
    void rejectsForeignContextAndUnavailableService()
    {
        Fixture f; f.context.setContext({"org.example.foreign", "kugou", "kugou/one", "one", PluginUiMode::Edit}, f.backend, &f.bridge, nullptr, nullptr);
        QCOMPARE(f.backend->state(), "invalidated"); QVERIFY(!f.backend->startQr()); QCOMPARE(f.network.replies.size(), 0);
        PluginUiContext context; Network network;
        context.setContext({"org.quemusic.source.kugou", "kugou", "kugou/one", "one", PluginUiMode::Create}, nullptr, nullptr, nullptr, nullptr);
        KugouAuthBackend missing(&context, &network, {1, 1000}); QCOMPARE(missing.state(), "invalidated");
        QVERIFY(!missing.startQr()); QCOMPARE(network.replies.size(), 0);
    }
    void nestedHostNotificationDoesNotLoseCompletion()
    {
        Fixture f;
        connect(&f.bridge, &PluginUiSettingsBridge::changed, &f.context, [] { QCoreApplication::processEvents(); });
        QVERIFY(f.backend->startQr()); f.network.replies[0]->deliver("{\"status\":1,\"data\":{\"qrcode\":\"KEY\"}}");
        QTRY_COMPARE(f.network.replies.size(), 2); f.network.replies[1]->deliver(confirmed());
        QTRY_COMPARE(f.backend->state(), "stored"); QVERIFY(f.backend->credentialStored());
        QVERIFY(f.backend->clearAccount()); QTRY_COMPARE(f.backend->state(), "cleared");
        QVERIFY(!f.backend->credentialStored());
    }
    void storingNotificationCannotOverwriteNewBinding()
    {
        Fixture f;
        const auto external = f.record("123");
        connect(f.backend, &KugouAuthBackend::changed, &f.context, [&] {
            if (f.backend->state() == "storing") f.bridge.saveBackendSecret("credentialBundle", external);
        });
        QVERIFY(f.backend->startQr()); f.network.replies[0]->deliver("{\"status\":1,\"data\":{\"qrcode\":\"KEY\"}}");
        QTRY_COMPARE(f.network.replies.size(), 2); f.network.replies[1]->deliver(confirmed());
        QCOMPARE(f.backend->state(), "failed"); QCOMPARE(f.backend->errorKey(), "kugou.authBindingChanged");
        QCOMPARE(f.secrets.writes, 1); QCOMPARE(*f.bridge.readBackendSecret("credentialBundle"), external);
    }
};
QTEST_MAIN(KugouAuthBackendTest)
#include "tst_KugouAuthBackend.moc"
