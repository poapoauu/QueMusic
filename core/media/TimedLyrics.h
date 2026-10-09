#pragma once

#include <QRegularExpression>
#include <QVariantList>
#include <QVariantMap>
#include <algorithm>

// Pure presentation parser shared by plugins and Host. No file/network access.
namespace TimedLyrics {
inline QVariantList parseLrc(const QString &contents)
{
    static const QRegularExpression timestamp(QStringLiteral(R"(\[(\d{1,3}):(\d{2})(?:\.(\d{1,3}))?\])"));
    QVariantList lyrics;
    const auto normalized = contents.startsWith(QChar(0xFEFF)) ? contents.mid(1) : contents;
    for (const auto &line : normalized.split(QRegularExpression(QStringLiteral("[\\r\\n]")), Qt::KeepEmptyParts)) {
        auto matches = timestamp.globalMatch(line);
        QList<qint64> times;
        while (matches.hasNext()) {
            const auto match = matches.next();
            QString fraction = match.captured(3);
            while (fraction.size() < 3) fraction.append(QLatin1Char('0'));
            times.append((match.captured(1).toLongLong() * 60 + match.captured(2).toLongLong()) * 1000
                         + fraction.left(3).toLongLong());
        }
        QString text = line;
        text.remove(timestamp); text = text.trimmed();
        if (text.isEmpty()) continue;
        for (const auto time : times)
            lyrics.append(QVariantMap{{QStringLiteral("time"), time}, {QStringLiteral("text"), text}});
    }
    std::stable_sort(lyrics.begin(), lyrics.end(), [](const QVariant &a, const QVariant &b) {
        return a.toMap().value(QStringLiteral("time")).toLongLong() < b.toMap().value(QStringLiteral("time")).toLongLong();
    });
    return lyrics;
}
}
