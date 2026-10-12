// Test-only native factory: real Kugou Session/client/mapper, no sockets.
#include "KugouSourceDefinition.h"
#include "KugouSourceSession.h"
#include "plugin-ui/v1/IPluginUiBackendSecretProviderV1.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkReply>
#include <QTimer>
#include <cstring>
#include <utility>

namespace {
const QString hash = "0123456789ABCDEF0123456789ABCDEF";
class Reply final : public QNetworkReply {
public:
    Reply(const QNetworkRequest &request, QJsonObject response, QObject *owner)
        : QNetworkReply(owner), m_response(std::move(response))
    {
        setRequest(request); setUrl(request.url()); setOperation(QNetworkAccessManager::GetOperation);
        open(QIODevice::ReadOnly | QIODevice::Unbuffered);
    }
    void finish()
    {
        // Deliberately allow late signals after abort; the real client must fence them.
        m_body = QJsonDocument(m_response).toJson(QJsonDocument::Compact);
        setAttribute(QNetworkRequest::HttpStatusCodeAttribute, 200); setFinished(true);
        QPointer<Reply> guard(this);
        emit readyRead(); if (guard) emit finished();
    }
    void abort() override { setFinished(true); emit finished(); }
protected:
    qint64 readData(char *data, qint64 maximum) override
    {
        const auto count = qMin(maximum, qint64(m_body.size()) - m_offset);
        if (count <= 0) return -1;
        std::memcpy(data, m_body.constData() + m_offset, size_t(count)); m_offset += count; return count;
    }
private:
    QJsonObject m_response;
    QByteArray m_body;
    qint64 m_offset = 0;
};
class Network final : public QNetworkAccessManager {
public:
    explicit Network(QObject *control) : m_control(control) {}
    QList<QPointer<Reply>> pending;
    void complete()
    {
        const auto replies = std::exchange(pending, {});
        for (const auto &reply : replies) if (reply) reply->finish();
    }
protected:
    QNetworkReply *createRequest(Operation, const QNetworkRequest &request, QIODevice * = nullptr) override
    {
        const bool resource = request.url().path().endsWith("getSongInfo.php");
        const char *counter = resource ? "resourceCount" : "searchCount";
        m_control->setProperty(counter, m_control->property(counter).toInt() + 1);
        const int pay = m_control->property("pay").toInt();
        QJsonObject row{{"hash", hash}, {"songname", "Fixture Song"}, {"singername", "Fixture Artist"},
                        {"duration", 61}, {"pay_type", pay}, {"token", "PRIVATE"}};
        QJsonObject data = resource ? QJsonObject{{"hash", hash}, {"songName", "Verified Fixture Song"},
            {"author_name", "Fixture Artist"}, {"timeLength", 61}, {"pay_type", pay},
            {"url", "https://cdn.kugou.com/fixture?key=PRIVATE"}, {"token", "PRIVATE"}}
            : QJsonObject{{"info", QJsonArray{row}}, {"total", 1}};
        if (resource && m_control->property("missingRights").toBool()) data.remove("pay_type");
        if (resource && m_control->property("unsafeUrl").toBool()) data["url"] = "file:///private";
        if (resource && m_control->property("wrongHash").toBool()) data["hash"] = QString(32, 'F');
        auto *reply = new Reply(request, {{"status", 1}, {"data", data}}, this);
        if (resource && m_control->property("holdResources").toBool()) pending.append(reply);
        else QTimer::singleShot(0, reply, [reply] { reply->finish(); });
        return reply;
    }
private:
    QPointer<QObject> m_control;
};
}
class KugouSourceFixture final : public QObject, public IMusicSourcePluginV2,
                                 public IPluginSettingsProviderV2,
                                 public IPluginUiBackendSecretProviderV1 {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID QUEMUSIC_MUSIC_SOURCE_PLUGIN_V2_IID FILE "../../plugins/kugou-source/plugin.json")
    Q_INTERFACES(IMusicSourcePluginV2 IPluginSettingsProviderV2 IPluginUiBackendSecretProviderV1)
    Q_PROPERTY(int sourceSdkAbi READ sourceSdkAbi CONSTANT)
public:
    int sourceSdkAbi() const { return QUEMUSIC_MUSIC_SOURCE_SDK_V2_ABI; }
    SourceDescriptorV2 descriptor() const override { return KugouSourceDefinition::descriptor(); }
    SettingsSchemaV2 settingsSchema() const override { return KugouSourceDefinition::settingsSchema(); }
    QStringList backendSecretFieldIds() const override { return {QStringLiteral("credentialBundle")}; }
    IMusicSourceSessionV2 *createSession(const SourceConfigurationV2 &configuration, QObject *parent) override
    {
        auto *network = new Network(this);
        auto *session = new KugouSourceSession(configuration, network, parent);
        network->setParent(session); m_networks.append(network);
        return session;
    }
    Q_INVOKABLE void completePending()
    {
        const auto networks = m_networks;
        for (const auto &network : networks) if (network) network->complete();
    }
private:
    QList<QPointer<Network>> m_networks;
};
#include "KugouSourceFixture.moc"
