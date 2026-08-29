#include "SourceTypes.h"

namespace {

constexpr quint64 allCapabilities =
    static_cast<quint64>(SourceCapability::Search) |
    static_cast<quint64>(SourceCapability::Browse) |
    static_cast<quint64>(SourceCapability::StreamAudio) |
    static_cast<quint64>(SourceCapability::StreamVideo) |
    static_cast<quint64>(SourceCapability::Artwork) |
    static_cast<quint64>(SourceCapability::Lyrics) |
    static_cast<quint64>(SourceCapability::PlaylistRead) |
    static_cast<quint64>(SourceCapability::PlaylistWrite) |
    static_cast<quint64>(SourceCapability::Favorites) |
    static_cast<quint64>(SourceCapability::Download) |
    static_cast<quint64>(SourceCapability::Scrobble);

}

SourceErrorKind sourceErrorKindFromString(const QString &value)
{
    if (value == QStringLiteral("network")) {
        return SourceErrorKind::Network;
    }
    if (value == QStringLiteral("authentication")) {
        return SourceErrorKind::Authentication;
    }
    if (value == QStringLiteral("authorization")) {
        return SourceErrorKind::Authorization;
    }
    if (value == QStringLiteral("not_found")) {
        return SourceErrorKind::NotFound;
    }
    if (value == QStringLiteral("rate_limited")) {
        return SourceErrorKind::RateLimited;
    }
    if (value == QStringLiteral("invalid_request")) {
        return SourceErrorKind::InvalidRequest;
    }
    if (value == QStringLiteral("unavailable")) {
        return SourceErrorKind::Unavailable;
    }
    if (value == QStringLiteral("unsupported")) {
        return SourceErrorKind::Unsupported;
    }
    return SourceErrorKind::Unknown;
}

QJsonObject sourceDescriptorToJson(const SourceDescriptor &descriptor)
{
    return {
        {QStringLiteral("id"), descriptor.id},
        {QStringLiteral("name"), descriptor.name},
        {QStringLiteral("version"), descriptor.version},
        {QStringLiteral("protocol"), descriptor.protocol},
        {QStringLiteral("sdkVersion"), descriptor.sdkVersion},
        {QStringLiteral("capabilities"),
         static_cast<qint64>(descriptor.capabilities.toInt())},
    };
}

SourceDescriptor sourceDescriptorFromJson(const QJsonObject &object)
{
    const quint64 capabilityBits = static_cast<quint64>(
        object.value(QStringLiteral("capabilities")).toVariant().toULongLong());
    return {
        object.value(QStringLiteral("id")).toString(),
        object.value(QStringLiteral("name")).toString(),
        object.value(QStringLiteral("version")).toString(),
        object.value(QStringLiteral("protocol")).toString(),
        object.value(QStringLiteral("sdkVersion")).toString(),
        SourceCapabilities(static_cast<SourceCapability>(capabilityBits & allCapabilities)),
    };
}
