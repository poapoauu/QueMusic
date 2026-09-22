#include "SourceSecretsV2.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace {
constexpr qsizetype maximumEnvelopeSize = 1024 * 1024;
const QByteArray prefix = "QueMusic.SourceSecrets/2\n";

// Parse the flat object before constructing a map. QJsonObject alone silently
// accepts duplicate keys, including aliases spelled with JSON unicode escapes.
class EnvelopeReader {
public:
    explicit EnvelopeReader(const QByteArray &input) : bytes(input), pos(prefix.size()) {}
    bool take(char c)
    {
        whitespace();
        if (pos >= bytes.size() || bytes[pos] != c) return false;
        ++pos;
        return true;
    }
    void whitespace()
    {
        while (pos < bytes.size() && (bytes[pos] == ' ' || bytes[pos] == '\n'
               || bytes[pos] == '\r' || bytes[pos] == '\t')) ++pos;
    }
    std::optional<QString> string()
    {
        whitespace();
        const qsizetype start = pos;
        if (!take('"')) return std::nullopt;
        while (pos < bytes.size()) {
            const char c = bytes[pos++];
            if (c == '\\') {
                if (pos == bytes.size()) return std::nullopt;
                ++pos;
            } else if (c == '"') {
                const auto doc = QJsonDocument::fromJson("[" + bytes.mid(start, pos - start) + "]");
                if (!doc.isArray() || doc.array().size() != 1 || !doc.array()[0].isString())
                    return std::nullopt;
                return doc.array()[0].toString();
            }
        }
        return std::nullopt;
    }
    bool end() { whitespace(); return pos == bytes.size(); }
private:
    const QByteArray &bytes;
    qsizetype pos;
};
}

bool isSafeSourceSettingsIdV2(const QString &id)
{
    if (id.isEmpty()) return false;
    for (QChar c : id) {
        const auto u = c.unicode();
        if (!((u >= 'a' && u <= 'z') || (u >= 'A' && u <= 'Z')
              || (u >= '0' && u <= '9') || u == '.' || u == '_' || u == '-')) return false;
    }
    return true;
}

std::optional<QByteArray> encodeSourceSecretsV2(const SourceNamedSecretsV2 &secrets)
{
    QJsonObject object;
    qsizetype size = prefix.size() + 2;
    for (auto it = secrets.cbegin(); it != secrets.cend(); ++it) {
        if (!isSafeSourceSettingsIdV2(it.key()) || it->size() > maximumEnvelopeSize) return std::nullopt;
        size += it.key().size() + ((it->size() + 2) / 3) * 4 + 6;
        if (size - 1 > maximumEnvelopeSize) return std::nullopt;
        object.insert(it.key(), QString::fromLatin1(it->toBase64()));
    }
    auto bytes = prefix + QJsonDocument(object).toJson(QJsonDocument::Compact);
    if (bytes.size() > maximumEnvelopeSize) return std::nullopt;
    return bytes;
}

std::optional<SourceNamedSecretsV2> decodeSourceSecretsV2(const QByteArray &envelope)
{
    if (envelope.size() > maximumEnvelopeSize || !envelope.startsWith(prefix)) return std::nullopt;
    EnvelopeReader reader(envelope);
    if (!reader.take('{')) return std::nullopt;
    SourceNamedSecretsV2 result;
    if (reader.take('}')) return reader.end() ? std::optional(result) : std::nullopt;
    do {
        auto key = reader.string();
        if (!key || !isSafeSourceSettingsIdV2(*key) || result.contains(*key) || !reader.take(':'))
            return std::nullopt;
        auto value = reader.string();
        if (!value) return std::nullopt;
        const auto base64 = value->toLatin1();
        if (QString::fromLatin1(base64) != *value) return std::nullopt;
        const auto decoded = QByteArray::fromBase64(base64, QByteArray::AbortOnBase64DecodingErrors);
        if (decoded.toBase64() != base64) return std::nullopt;
        result.insert(*key, decoded);
        if (reader.take('}')) return reader.end() ? std::optional(result) : std::nullopt;
    } while (reader.take(','));
    return std::nullopt;
}

SourceSecretInputKindV2 sourceSecretInputKindV2(const QByteArray &bytes)
{
    if (!bytes.startsWith("QueMusic.SourceSecrets")) return SourceSecretInputKindV2::LegacyRaw;
    return decodeSourceSecretsV2(bytes) ? SourceSecretInputKindV2::NamedEnvelope
                                      : SourceSecretInputKindV2::MalformedEnvelope;
}
