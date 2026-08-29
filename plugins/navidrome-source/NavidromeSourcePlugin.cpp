#include "NavidromeSourcePlugin.h"

#include "NavidromeSourceSession.h"

SourceDescriptor NavidromeSourcePlugin::descriptor() const
{
    return {QStringLiteral("navidrome"), QStringLiteral("Navidrome"),
            QStringLiteral("1.0.0"), QStringLiteral("subsonic"),
            QStringLiteral("1.0"),
            SourceCapability::Search | SourceCapability::Browse |
                SourceCapability::StreamAudio | SourceCapability::Artwork |
                SourceCapability::Lyrics};
}

bool NavidromeSourcePlugin::initialize(SourcePluginContext &context)
{
    m_network = context.network;
    return !m_network.isNull();
}

IMusicSourceSession *NavidromeSourcePlugin::createSession(const SourceAccount &account,
                                                           QObject *parent)
{
    if (m_network.isNull()) {
        return nullptr;
    }

    return new NavidromeSourceSession(account, m_network, parent);
}
