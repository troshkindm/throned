#include "include/ui/preview/UiPreview.h"

#include "include/ui/preview/GeometryReport.h"

#include <memory>

#include <QApplication>
#include <QComboBox>
#include <QCursor>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScreen>
#include <QTabBar>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTimer>
#include <QVBoxLayout>

#include "include/global/Configs.hpp"
#include "include/ui/setting/RouteItem.h"
#include "include/ui/setting/RouteProfileSimpleEditor.h"
#include "include/ui/setting/ThemeManager.hpp"
#include "include/ui/widget/ActionButton.h"

namespace UiPreview {
namespace {
QSize PreviewSize(const QStringList &arguments, const QSize &fallback) {
    const int sizeAt = arguments.indexOf(QStringLiteral("-ui-preview-size"));
    if (sizeAt < 0 || sizeAt + 1 >= arguments.size()) return fallback;

    const QStringList parts = arguments.at(sizeAt + 1).toLower().split(QLatin1Char('x'));
    if (parts.size() != 2) return fallback;

    bool widthOk = false;
    bool heightOk = false;
    const int width = parts.at(0).toInt(&widthOk);
    const int height = parts.at(1).toInt(&heightOk);
    if (!widthOk || !heightOk || width < 900 || height < 620) return fallback;
    return {width, height};
}

int RunAdvancedRouteEditorPreview(QApplication &app) {
    QTemporaryDir workdir;
    if (!workdir.isValid()) return 2;
    QDir::setCurrent(workdir.path());
    Configs::initDB(QDir(workdir.path()).absoluteFilePath("preview.db").toStdString());

    auto profile = std::make_shared<Configs::RouteProfile>();
    profile->name = QStringLiteral("Default");
    profile->defaultOutboundID = Configs::directID;
    const auto rule = [](const QString &name, const QJsonObject &fields) {
        QString error;
        auto parsed = Configs::RouteProfile::parseJsonArray(QJsonArray{fields}, &error);
        if (!parsed.isEmpty()) parsed.first()->name = name;
        return parsed;
    };
    profile->Rules += rule(QStringLiteral("Simple Address Proxy"), QJsonObject{
                                                                       {"domain", QJsonArray{"assistant.example", "gateway.example", "voice.example"}},
                                                                       {"domain_suffix", QJsonArray{"example.org", "example.net", "apis.example.com"}},
                                                                       {"rule_set", QJsonArray{"geosite-example-ai", "geosite-example-chat", "geosite-example-code"}},
                                                                       {"outbound", QStringLiteral("proxy")},
                                                                   });
    profile->Rules += rule(QStringLiteral("Simple Process Name Proxy"), QJsonObject{
                                                                            {"process_name", QJsonArray{"DemoChat.exe", "EditorDemo.exe", "ExampleBrowser.exe"}},
                                                                            {"outbound", QStringLiteral("proxy")},
                                                                        });
    profile->Rules += rule(QString::fromLatin1(Configs::LocalProxyRuleName), QJsonObject{
                                                                                 {"inbound", QJsonArray{"mixed-in", "socks-in"}},
                                                                                 {"outbound", QStringLiteral("proxy")},
                                                                             });

    auto *dialog = new RouteItem(nullptr, profile);
    dialog->resize(PreviewSize(app.arguments(), {1120, 720}));
    dialog->show();
    if (auto *modeTabs = dialog->findChild<QTabBar *>(QStringLiteral("routeModeTabs")))
        modeTabs->setCurrentIndex(1);
    if (app.arguments().contains(QStringLiteral("--detail")))
        if (auto *json = dialog->findChild<QPushButton *>(QStringLiteral("routeCardJsonButton")))
            json->click();
    const QStringList args = app.arguments();
    if (const int outputAt = args.indexOf(QStringLiteral("--output"));
        outputAt >= 0 && outputAt + 1 < args.size()) {
        const QString output = args.at(outputAt + 1);
        QTimer::singleShot(900, dialog, [dialog, output, &app] {
            SaveGeometryReport(dialog, output);
            app.exit(dialog->grab().save(output, "PNG") ? 0 : 2);
        });
    }
    return app.exec();
}

} // namespace

QString RequestedFont(const QApplication &app) {
    const QStringList arguments = app.arguments();
    const int at = arguments.indexOf(QStringLiteral("-ui-preview-font"));
    return at >= 0 && at + 1 < arguments.size() ? arguments.at(at + 1) : QString();
}

void ApplyTheme(const QApplication &app) {
    if (QScreen *screen = app.primaryScreen()) QCursor::setPos(screen->geometry().bottomLeft());
    QString requested = QStringLiteral("Throned Graphite");
    if (const int themeAt = app.arguments().indexOf(QStringLiteral("-theme"));
        themeAt >= 0 && themeAt + 1 < app.arguments().size()) {
        const QString name = app.arguments().at(themeAt + 1);
        bool matched = false;
        for (const QString &theme: themeManager()->ThronedThemes())
            if (theme.compare(name, Qt::CaseInsensitive) == 0 || theme.compare(QStringLiteral("Throned ") + name, Qt::CaseInsensitive) == 0) {
                requested = theme;
                matched = true;
            }
        // "System", a skin or a Qt style name reaches ApplyTheme untouched: the light
        // themes are exactly where a hardcoded dark colour shows up, so they need
        // rendering as much as the Throned ones do.
        if (!matched) requested = name;
    }
    themeManager()->ApplyTheme(requested);
}

int RunRouteEditor(QApplication &app) {
    ApplyTheme(app);
    if (app.arguments().contains(QStringLiteral("--advanced"))) return RunAdvancedRouteEditorPreview(app);
    QDialog dialog;
    dialog.setObjectName("routeProfileEditor");
    dialog.setWindowTitle(QObject::tr("Throned — Route profile preview"));
    dialog.resize(PreviewSize(app.arguments(), {1120, 700}));

    QFont font = app.font();
    if (const QString pinned = RequestedFont(app); !pinned.isEmpty()) {
        font.setFamily(pinned);
    }
#ifdef Q_OS_WIN
    else {
        font.setFamily(QStringLiteral("Segoe UI Variable Text"));
    }
#endif
    font.setPointSize(10);
    font.setStyleStrategy(QFont::PreferAntialias);
    font.setHintingPreference(QFont::PreferDefaultHinting);
    dialog.setFont(font);
    themeManager()->RegisterStyle(&dialog, RouteProfileSimpleEditor::dialogStyleSheet());

    auto *root = new QVBoxLayout(&dialog);
    root->setContentsMargins(12, 12, 12, 12);
    root->setSpacing(10);
    auto *header = new QFrame;
    header->setObjectName("routeProfileHeader");
    auto *headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(12, 10, 12, 10);
    auto *nameLayout = new QVBoxLayout;
    auto *nameLabel = new QLabel(QCoreApplication::translate("RouteItem", "Name"));
    nameLabel->setObjectName("routeFieldLabel");
    auto *name = new QLineEdit(QObject::tr("Development"));
    nameLayout->addWidget(nameLabel);
    nameLayout->addWidget(name);
    headerLayout->addLayout(nameLayout, 1);
    auto *outboundLayout = new QVBoxLayout;
    auto *outboundLabel = new QLabel(QCoreApplication::translate("RouteItem", "Default outbound"));
    outboundLabel->setObjectName("routeFieldLabel");
    auto *outbound = new QComboBox;
    outbound->setObjectName("def_out");
    outbound->addItems({"direct", "proxy", "block", "warp-bypass"});
    outbound->setMaximumWidth(250);
    outboundLayout->addWidget(outboundLabel);
    outboundLayout->addWidget(outbound);
    headerLayout->addLayout(outboundLayout, 1);
    auto *modeLayout = new QVBoxLayout;
    auto *modeLabel = new QLabel(QCoreApplication::translate("RouteItem", "Mode"));
    modeLabel->setObjectName("routeFieldLabel");
    auto *mode = new QTabBar;
    mode->setObjectName("routeModeTabs");
    mode->addTab(QCoreApplication::translate("RouteItem", "Simple"));
    mode->addTab(QCoreApplication::translate("RouteItem", "Advanced"));
    mode->setUsesScrollButtons(false);
    mode->setExpanding(true);
    mode->setMinimumWidth(220);
    mode->setMaximumWidth(280);
    modeLayout->addWidget(modeLabel);
    modeLayout->addWidget(mode);
    headerLayout->addLayout(modeLayout, 1);
    root->addWidget(header);

    auto *tabs = new QTabWidget;
    auto *editor = new RouteProfileSimpleEditor;
    editor->setRules(0, "domain:updates.example.com\n");
    editor->setRules(1, "domain:ads.example\nip:198.51.100.0/24\n");
    editor->setRules(2,
                     "processPath:C:\\Program Files\\Example Browser\\ExampleBrowser.exe\n"
                     "processName:DemoChat.exe\nprocessName:EditorDemo.exe\n"
                     "domain:assistant.example\ndomain:gateway.example\ndomain:voice.example\n"
                     "domain:audio.example\ndomain:*.datasets.example\ndomain:daily-api.example.com\n"
                     "domain:code-api.example.com\ndomain:oauth.example.com\ndomain:accounts.example.com\n"
                     "domain:apis.example.com\ndomain:example.com\ndomain:usercontent.example.com\n"
                     "domain:static.example.com\ndomain:assets.example\ndomain:packages.example\n"
                     "domain:usercontent.packages.example\nsuffix:example.org\nsuffix:example.net\n"
                     "keyword:telemetry\nregex:^cdn[0-9]+\\.example\\.com$\n"
                     "ruleset:geosite-example-chat\nruleset:geosite-example-ai\nruleset:geosite-example-search\n"
                     "ruleset:geosite-example-code\nruleset:geosite-example-containers\nruleset:geosite-example-ide\n"
                     "ruleset:geosite-example-models\nruleset:geosite-example-games\nruleset:geosite-example-media\n"
                     "ruleset:geosite-example-docs\nruleset:geosite-example-cdn\nruleset:geosite-example-updates\n"
                     "ip:192.0.2.0/24\nip:198.51.100.0/24\nruleset:geoip-example-cdn\n");
    editor->setRules(3, {});
    editor->setAdvancedRules({QObject::tr("regional-routing"), QObject::tr("fallback-policy")});
    editor->setRuleSetCatalog({
        QStringLiteral("geosite-example-ai"),
        QStringLiteral("geosite-example-chat"),
        QStringLiteral("geosite-example-code"),
        QStringLiteral("geosite-example-search"),
        QStringLiteral("geoip-example-cdn"),
        QStringLiteral("geoip-example-service"),
        QStringLiteral("geoip-private"),
        QStringLiteral("geoip-example-chat"),
    });
    editor->setLocalProxyTrafficEnabled(true);
    editor->setViaCatalog({});
    editor->setViaBuckets({});
    tabs->addTab(editor, QCoreApplication::translate("RouteItem", "Simple"));
    auto *advanced = new QLabel(QObject::tr("The existing lossless advanced editor remains available here."));
    advanced->setAlignment(Qt::AlignCenter);
    tabs->addTab(advanced, QCoreApplication::translate("RouteItem", "Advanced"));
    tabs->tabBar()->hide();
    QObject::connect(mode, &QTabBar::currentChanged, tabs, &QTabWidget::setCurrentIndex);
    QObject::connect(tabs, &QTabWidget::currentChanged, mode, &QTabBar::setCurrentIndex);
    root->addWidget(tabs, 1);

    auto *buttons = new QDialogButtonBox;
    auto *cancel = buttons->addButton(QDialogButtonBox::Cancel);
    cancel->setObjectName("routeSecondaryButton");
    auto *save = buttons->addButton(QCoreApplication::translate("RouteItem", "Save profile"), QDialogButtonBox::AcceptRole);
    save->setObjectName("routeSaveButton");
    QObject::connect(cancel, &QPushButton::clicked, &dialog, &QDialog::reject);
    QObject::connect(save, &QPushButton::clicked, &dialog, &QDialog::accept);
    root->addWidget(buttons);
    dialog.show();
    const QStringList args = app.arguments();
    if (args.contains(QStringLiteral("--paste")))
        if (auto *paste = dialog.findChild<QPushButton *>(QStringLiteral("routeBulkEditButton")))
            QTimer::singleShot(200, paste, [paste] { paste->click(); });
    if (args.contains(QStringLiteral("--paste-sample")))
        QTimer::singleShot(400, &dialog, [] {
            if (auto *modal = QApplication::activeModalWidget())
                if (auto *editor = modal->findChild<QPlainTextEdit *>())
                    editor->setPlainText(QStringLiteral(
                        "# sample rule list\n"
                        "assistant.example\n"
                        ".example.org\n"
                        "  \"cdn.example.net\",\n"
                        "- geosite-example-ai\n"
                        "domain_suffix: example.io\n"
                        "process_name: DemoChat.exe\n"
                        "C:\\DemoApps\\OverlayDemo\\OverlayDemo.exe\n"
                        "198.51.100.0/24\n"
                        "2001:db8::/32\n"
                        "rule_set:geoip-example-cdn\n"
                        "regexp:^cdn[0-9]+\\.example\\.com$\n"
                        "telemetry\n"
                        "?? total nonsense here\n"));
        });
    if (const int outputAt = args.indexOf(QStringLiteral("--output"));
        outputAt >= 0 && outputAt + 1 < args.size()) {
        const QString output = args.at(outputAt + 1);
        QTimer::singleShot(700, &dialog, [&dialog, editor, output, &app] {
            int expected = 0;
            for (int action: {0, 1, 2, 3}) expected += editor->rules(action).split('\n', Qt::SkipEmptyParts).size();
            int count = 0;
            int checked = 0;
            for (auto *button: editor->findChildren<ActionButton *>()) {
                count += button->count();
                checked += button->isChecked();
            }
            if (expected == 0 || count != expected || checked != 1) {
                qWarning() << "Routing sidebar lost its initial counts or selection" << expected << count << checked;
                app.exit(2);
                return;
            }
            QWidget *target = QApplication::activeModalWidget();
            SaveGeometryReport(target ? target : &dialog, output);
            const bool ok = (target ? target : &dialog)->grab().save(output, "PNG");
            if (target != nullptr) target->close();
            app.exit(ok ? 0 : 2);
        });
    }
    return app.exec();
}

} // namespace UiPreview
