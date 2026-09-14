#include "RuntimeLoggingPolicy.h"

#include <QLoggingCategory>

void RuntimeLoggingPolicy::install()
{
    const bool graphicsDebug = qEnvironmentVariableIsSet("QUEMUSIC_GRAPHICS_DEBUG")
        && qEnvironmentVariable("QUEMUSIC_GRAPHICS_DEBUG") != QStringLiteral("0");
    const QByteArray rules = QByteArrayLiteral(
        "qt.multimedia.ffmpeg.debug=false\n"
        "qt.multimedia.plugins.ffmpeg.debug=false\n")
        + (graphicsDebug
               ? QByteArrayLiteral("qt.scenegraph.general.debug=true\nqt.rhi.general.debug=true\n")
               : QByteArrayLiteral("qt.scenegraph.general.debug=false\nqt.rhi.general.debug=false\n"));
    QLoggingCategory::setFilterRules(QString::fromUtf8(rules));
}
