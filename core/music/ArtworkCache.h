#pragma once
#include <QByteArray>
#include <QString>
#include <QUrl>
#include <optional>

class ArtworkCache final {
public:
    explicit ArtworkCache(QString directory = {});
    std::optional<QUrl> lookup(const QString &sourceInstanceId,const QString &artworkId) const;
    QUrl store(const QString &sourceInstanceId,const QString &artworkId,
               const QByteArray &bytes,const QString &mimeType);
    void invalidateSource(const QString &sourceInstanceId);
private:
    QString sourceDirectory(const QString &sourceInstanceId) const;
    QString m_directory;
};
