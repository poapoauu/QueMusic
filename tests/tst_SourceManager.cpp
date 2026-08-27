#include "SourceManager.h"

#include <QDir>
#include <QSignalSpy>
#include <QTest>

class SourceManagerTest : public QObject {
    Q_OBJECT

private slots:
    void loadsValidSourcePlugin();
    void ignoresMissingSearchPath();
    void rejectsUnknownSourceId();
    void rejectsDuplicateSourceIdWithoutStoppingOtherSources();
    void rejectsInvalidPluginsWithoutStoppingOtherSources();
};

namespace {

QString fixtureDirectory(const QString &name)
{
    return QDir(QStringLiteral(QUEMUSIC_TEST_PLUGIN_ROOT)).filePath(name);
}

}

void SourceManagerTest::loadsValidSourcePlugin()
{
    SourceManager manager;
    QSignalSpy loaded(&manager, &SourceManager::sourceLoaded);
    const SourceAccount account{QStringLiteral("fixture.valid"), QStringLiteral("account-1"),
                                QStringLiteral("Fixture Account")};
    QObject parent;

    manager.addSearchPath(fixtureDirectory(QStringLiteral("valid")));

    QCOMPARE(manager.loadAll(), 1);
    QCOMPARE(loaded.count(), 1);
    const QVariantList sources = manager.availableSources();
    QCOMPARE(sources.size(), 1);
    const QVariantMap source = sources.constFirst().toMap();
    QCOMPARE(source.value(QStringLiteral("id")).toString(), QStringLiteral("fixture.valid"));
    QCOMPARE(source.value(QStringLiteral("name")).toString(), QStringLiteral("Fixture Source"));
    QCOMPARE(source.value(QStringLiteral("version")).toString(), QStringLiteral("1.0.0"));
    QCOMPARE(source.value(QStringLiteral("protocol")).toString(), QStringLiteral("test"));
    QCOMPARE(source.value(QStringLiteral("capabilities")).toULongLong(), qulonglong(5));

    IMusicSourceSession *session = manager.createSession(account.sourceId, account, &parent);
    QVERIFY(session != nullptr);
    QCOMPARE(session->parent(), &parent);
}

void SourceManagerTest::ignoresMissingSearchPath()
{
    SourceManager manager;
    QSignalSpy failed(&manager, &SourceManager::sourceLoadFailed);

    manager.addSearchPath(fixtureDirectory(QStringLiteral("does-not-exist")));

    QCOMPARE(manager.loadAll(), 0);
    QCOMPARE(failed.count(), 1);
    QCOMPARE(manager.sourceIds(), QStringList());

    manager.addSearchPath(fixtureDirectory(QStringLiteral("valid")));

    QCOMPARE(manager.loadAll(), 1);
    QCOMPARE(manager.sourceIds(), QStringList({QStringLiteral("fixture.valid")}));
}

void SourceManagerTest::rejectsUnknownSourceId()
{
    SourceManager manager;
    QObject parent;
    const SourceAccount account{QStringLiteral("unknown"), QStringLiteral("account-1"),
                                QStringLiteral("Unknown Account")};

    manager.addSearchPath(fixtureDirectory(QStringLiteral("valid")));
    QCOMPARE(manager.loadAll(), 1);

    QVERIFY(manager.createSession(account.sourceId, account, &parent) == nullptr);
}

void SourceManagerTest::rejectsDuplicateSourceIdWithoutStoppingOtherSources()
{
    SourceManager manager;
    QSignalSpy failed(&manager, &SourceManager::sourceLoadFailed);
    QObject parent;
    const SourceAccount account{QStringLiteral("fixture.duplicate"), QStringLiteral("account-1"),
                                QStringLiteral("Duplicate Account")};

    manager.addSearchPath(fixtureDirectory(QStringLiteral("duplicate")));

    QCOMPARE(manager.loadAll(), 1);
    QCOMPARE(failed.count(), 1);
    QCOMPARE(manager.sourceIds(), QStringList({QStringLiteral("fixture.duplicate")}));
    QVERIFY(manager.createSession(account.sourceId, account, &parent) != nullptr);
}

void SourceManagerTest::rejectsInvalidPluginsWithoutStoppingOtherSources()
{
    SourceManager manager;
    QSignalSpy failed(&manager, &SourceManager::sourceLoadFailed);

    manager.addSearchPath(fixtureDirectory(QStringLiteral("invalid")));

    QCOMPARE(manager.loadAll(), 1);
    QCOMPARE(failed.count(), 5);
    QCOMPARE(manager.sourceIds(), QStringList({QStringLiteral("fixture.valid")}));
}

QTEST_MAIN(SourceManagerTest)
#include "tst_SourceManager.moc"
