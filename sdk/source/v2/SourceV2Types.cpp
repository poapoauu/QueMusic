#include "SourceV2Types.h"

QVariantMap mediaRefV2ToVariantMap(const MediaRefV2 &media)
{
    return {
        {QStringLiteral("sourcePluginId"), media.sourcePluginId},
        {QStringLiteral("sourceInstanceId"), media.sourceInstanceId},
        {QStringLiteral("accountId"), media.accountId},
        {QStringLiteral("entityType"), static_cast<int>(media.entityType)},
        {QStringLiteral("entityId"), media.entityId},
    };
}

MediaRefV2 mediaRefV2FromVariantMap(const QVariantMap &map)
{
    return {
        map.value(QStringLiteral("sourcePluginId")).toString(),
        map.value(QStringLiteral("sourceInstanceId")).toString(),
        map.value(QStringLiteral("accountId")).toString(),
        static_cast<MediaEntityTypeV2>(map.value(QStringLiteral("entityType")).toInt()),
        map.value(QStringLiteral("entityId")).toString(),
    };
}

QJsonObject actionAvailabilityV2ToJson(const ActionAvailabilityV2 &availability)
{
    return {
        {QStringLiteral("state"), static_cast<int>(availability.state)},
        {QStringLiteral("reasonKey"), availability.reasonKey},
        {QStringLiteral("constraints"), QJsonObject::fromVariantMap(availability.constraints)},
    };
}

ActionAvailabilityV2 actionAvailabilityV2FromJson(const QJsonObject &object)
{
    return {
        static_cast<AvailabilityV2>(object.value(QStringLiteral("state")).toInt()),
        object.value(QStringLiteral("reasonKey")).toString(),
        object.value(QStringLiteral("constraints")).toObject().toVariantMap(),
    };
}
