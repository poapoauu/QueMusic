#include "TestSourcePlugin.h"

#include "TestSourceSession.h"

SourceDescriptor TestSourcePlugin::descriptor() const
{
    return {QStringLiteral("test-source"), QStringLiteral("Test Source"), QStringLiteral("1.0.0"),
            QStringLiteral("test"), QStringLiteral("1.0"),
            SourceCapability::Search | SourceCapability::StreamAudio | SourceCapability::Artwork};
}

bool TestSourcePlugin::initialize(SourcePluginContext &context)
{
    Q_UNUSED(context);
    return true;
}

IMusicSourceSession *TestSourcePlugin::createSession(const SourceAccount &account, QObject *parent)
{
    Q_UNUSED(account);
    return new TestSourceSession(parent);
}
