#pragma once

#include <QByteArray>
#include <QMap>
#include <QString>
#include <optional>

// Encoding only, not encryption. Keep these bytes in secure storage or C++ sessions.
using SourceNamedSecretsV2 = QMap<QString, QByteArray>;
enum class SourceSecretInputKindV2 { LegacyRaw, NamedEnvelope, MalformedEnvelope };
bool isSafeSourceSettingsIdV2(const QString &id);
std::optional<QByteArray> encodeSourceSecretsV2(const SourceNamedSecretsV2 &secrets);
std::optional<SourceNamedSecretsV2> decodeSourceSecretsV2(const QByteArray &envelope);
SourceSecretInputKindV2 sourceSecretInputKindV2(const QByteArray &bytes);
