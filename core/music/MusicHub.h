#pragma once

#include "MusicPageModel.h"
#include "MediaActionRouter.h"
#include "SourceScopeStore.h"
#include <memory>

class QSettings;
class DirectoryLibraryController;

// Owner-thread QML boundary. Registry/scope/settings are borrowed and must
// outlive synchronous calls. Owns models, router, repositories and caches;
// teardown cancels/disconnects work before releasing composer/cache storage.
// Registry sessions are borrowed and are never reparented or deleted here.
class MusicHub final : public QObject {
    Q_OBJECT
    Q_PROPERTY(MusicPageModel *recommendation READ recommendation CONSTANT)
    Q_PROPERTY(MusicPageModel *category READ category CONSTANT)
    Q_PROPERTY(MusicPageModel *favorites READ favorites CONSTANT)
    Q_PROPERTY(MusicPageModel *searchResults READ searchResults CONSTANT)
    Q_PROPERTY(QVariantList sourceOptions READ sourceOptions NOTIFY sourceOptionsChanged)
    Q_PROPERTY(QString selectedSourceInstanceId READ selectedSourceInstanceId WRITE setSelectedSourceInstanceId NOTIFY selectedSourceInstanceIdChanged)
    Q_PROPERTY(MediaActionRouter *actions READ actions CONSTANT)
    Q_PROPERTY(QVariantMap categoryContext READ categoryContext NOTIFY categoryContextChanged)
    Q_PROPERTY(bool canNavigateBack READ canNavigateBack NOTIFY categoryContextChanged)
public:
    // Reads optional MusicHub/cacheDirectory (absolute local path) once; absent
    // uses existing repository defaults. Does not write settings or store secrets.
    explicit MusicHub(SourceRegistry *sources, SourceScopeStore *scope,
                      QSettings *cacheSettings, QObject *parent = nullptr);
    ~MusicHub() override;
    MusicPageModel *recommendation() const;
    MusicPageModel *category() const;
    MusicPageModel *favorites() const;
    MusicPageModel *searchResults() const;
    // Host-private discovery indices: 0 PersonalRadio, 1 PersonalRadar. These
    // are independent model/request generations, NOT new MusicPageKindV2 values.
    MusicPageModel *discovery(int kind) const;
    Q_INVOKABLE void refreshDiscovery(int kind);
    Q_INVOKABLE void loadMoreDiscovery(int kind, const QString &sectionId);
    Q_INVOKABLE void retryDiscovery(int kind, const QString &sectionId);
    Q_INVOKABLE void closeDiscovery(int kind);
    MediaActionRouter *actions() const;
    DirectoryLibraryController *directoryLibrary() const;
    bool sourcePluginLoaded(const QString &packageId) const;
    QVariantList sourceOptions() const;
    QString selectedSourceInstanceId() const;
    void setSelectedSourceInstanceId(const QString &id);
    QVariantMap categoryContext() const;
    bool canNavigateBack() const;
    // int(MusicPageKindV2): 0 recommendation, 1 category, 2 favorites, 3 search.
    // activate is idempotent; refresh alone does not activate automatic refresh.
    // Models remain owned by the hub; consumers must not call their write APIs.
    Q_INVOKABLE void activatePage(int pageKind);
    Q_INVOKABLE void refresh(int pageKind);
    Q_INVOKABLE void loadMore(int pageKind, const QString &sectionId);
    // searchTab follows the original UI order: songs, playlists, albums, lyrics.
    Q_INVOKABLE void search(const QString &text, int searchTab = 0);
    Q_INVOKABLE void cancel(int pageKind);
    Q_INVOKABLE void retrySection(int pageKind, const QString &sectionId);
    // Full item maps. Specified shared scope rejects foreign instances; aggregate
    // details bind their own instance. Back history is in-memory, bounded at 32.
    Q_INVOKABLE bool browse(const QVariantMap &item);
    Q_INVOKABLE bool navigateBack();
    Q_INVOKABLE void resetCategoryNavigation();
    // Asset input is item.ref (five fields); browse/router input is the full item.
    // IDs correlate independent delegates, including deferred invalid-input errors.
    Q_INVOKABLE QUuid loadArtwork(const QVariantMap &media);
    Q_INVOKABLE QUuid loadLyrics(const QVariantMap &media);
    Q_INVOKABLE void cancelAsset(const QUuid &requestId);
signals:
    // A background refresh warning, not a failed page request. The reason is
    // host-owned; plugin diagnostics never enter the presentation signal.
    void sourceRefreshFailed(QString sourceInstanceId, QString reasonKey);
    void sourceOptionsChanged();
    void selectedSourceInstanceIdChanged();
    void categoryContextChanged();
    void artworkReady(QUuid requestId, QVariantMap media, QUrl localUrl);
    void lyricsReady(QUuid requestId, QVariantMap media, QString lyrics);
    void assetFailed(QUuid requestId, QVariantMap error);
private:
    struct Impl;
    std::unique_ptr<Impl> d;
};
