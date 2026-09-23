#include "PluginTheme.h"
#include "PluginThemeAdapter.h"
#include "PluginUiContext.h"

#include <QSignalSpy>
#include <QTest>

class PluginUiThemeTest final : public QObject {
    Q_OBJECT
private slots:
    void appliesCompleteThemeAtomically();
    void rejectsPartialTheme();
    void contextIsReadOnlyAndInvalidates();
};

static QVariantMap palette(const QString &background, bool dark)
{
    return {{"background", background}, {"surface", "#eeeeee"},
            {"surfaceHover", "#dddddd"}, {"primary", "#3481fa"},
            {"textPrimary", dark ? "#f6f6f8" : "#101014"},
            {"textSecondary", "#777777"}, {"border", "#bbbbbb"},
            {"success", "#168447"}, {"warning", "#9a6500"},
            {"danger", dark ? "#ff6b6b" : "#b42318"},
            {"spacingSmall", 6}, {"spacingMedium", 12}, {"spacingLarge", 20},
            {"radiusSmall", 6}, {"radiusMedium", 10}, {"radiusLarge", 16},
            {"fontCaptionPixelSize", 11}, {"fontBodyPixelSize", 13},
            {"fontTitlePixelSize", 17}, {"fontFamily", "Sans Serif"},
            {"scaleFactor", dark ? 1.25 : 1.0}, {"dark", dark},
            {"reducedMotion", false}};
}

void PluginUiThemeTest::appliesCompleteThemeAtomically()
{
    auto *theme = PluginTheme::instance();
    PluginThemeAdapter adapter;
    QVERIFY(adapter.apply(palette("#ffffff", false)));
    QCOMPARE(theme->background(), QColor("#ffffff"));
    QCOMPARE(theme->textPrimary(), QColor("#101014"));
    QCOMPARE(theme->dark(), false);

    QSignalSpy changed(theme, &PluginTheme::themeChanged);
    QVERIFY(adapter.apply(palette("#101014", true)));
    QCOMPARE(changed.count(), 1);
    QCOMPARE(theme->background(), QColor("#101014"));
    QCOMPARE(theme->danger(), QColor("#ff6b6b"));
    QCOMPARE(theme->scaleFactor(), 1.25);
    QCOMPARE(theme->dark(), true);
}

void PluginUiThemeTest::rejectsPartialTheme()
{
    PluginThemeAdapter adapter;
    QSignalSpy changed(PluginTheme::instance(), &PluginTheme::themeChanged);
    QVERIFY(!adapter.apply({{"background", "#123456"}}));
    QCOMPARE(changed.count(), 0);
}

void PluginUiThemeTest::contextIsReadOnlyAndInvalidates()
{
    PluginUiContext context;
    QObject backend;
    const PluginUiContextData data{QStringLiteral("package"), QStringLiteral("source"),
                                   QStringLiteral("source/account"), QStringLiteral("account"),
                                   PluginUiMode::Edit};
    QSignalSpy changed(&context, &PluginUiContext::contextChanged);
    context.setContext(data, &backend, nullptr, nullptr, nullptr);
    QVERIFY(context.isValid());
    QCOMPARE(context.pluginPackageId(), QStringLiteral("package"));
    QCOMPARE(context.backend(), &backend);
    QVERIFY(context.metaObject()->property(context.metaObject()->indexOfProperty("backend"))
                .isWritable() == false);
    context.invalidate();
    QVERIFY(!context.isValid());
    QCOMPARE(context.backend(), nullptr);
    QCOMPARE(changed.count(), 2);
}

QTEST_MAIN(PluginUiThemeTest)
#include "tst_PluginUiTheme.moc"
