#include "include/ui/preview/MainWindowCapture.h"
#include "include/ui/preview/UpstreamPreview.h"
#include "include/ui/preview/GeometryReport.h"
#include "include/database/GroupsRepo.h"
#include "include/ui/profile/dialog_edit_profile.h"
#include "include/ui/setting/dialog_manage_routes.h"

#include <QApplication>
#include <QComboBox>
#include <QGroupBox>
#include <QLineEdit>
#include <QPixmap>
#include <QPushButton>
#include <QSet>
#include <QTabWidget>
#include <QTimer>

namespace UiPreview {
namespace {
bool capture(QWidget *widget, const QString &path) {
    SaveGeometryReport(widget, path);
    return widget->grab().save(path, "PNG");
}
} // namespace

void CaptureProtocolEditors(QWidget *parent, const QString &prefix) {
    const auto group = Configs::dataManager->groupsRepo->CurrentGroup();
    if (!group) {
        qApp->exit(2);
        return;
    }
    auto *editor = new DialogEditProfile(QStringLiteral("masque"), group->id, parent);
    editor->setInitialCommonFields(QStringLiteral("Example MASQUE"), QStringLiteral("proxy.example"), QStringLiteral("443"));
    editor->show();
    QTimer::singleShot(400, editor, [editor, parent, prefix] {
        auto *type = editor->findChild<QComboBox *>(QStringLiteral("type"));
        auto *version = editor->findChild<QComboBox *>(QStringLiteral("http_version"));
        auto *key = editor->findChild<QLineEdit *>(QStringLiteral("private_key"));
        auto *generate = editor->findChild<QPushButton *>(QStringLiteral("warp_autogen"));
        if (!type || type->currentData() != "masque" || !version || !version->isVisible() ||
            !key || !key->isVisible() || !generate || !generate->isVisible() ||
            !capture(editor, prefix + QStringLiteral("-masque.png"))) {
            qApp->exit(2);
            return;
        }
        type->setCurrentIndex(type->findData(QStringLiteral("naive")));
        QTimer::singleShot(400, editor, [editor, parent, prefix] {
            auto *headers = editor->findChild<QLineEdit *>(QStringLiteral("extra_headers"));
            auto *utls = editor->findChild<QComboBox *>(QStringLiteral("utlsFingerprint"));
            if (!headers || !headers->isVisible() || !utls || utls->isEnabled() ||
                !capture(editor, prefix + QStringLiteral("-naive.png"))) {
                qApp->exit(2);
                return;
            }
            editor->reject();
            auto *routing = new DialogManageRoutes(parent);
            auto *tabs = routing->findChild<QTabWidget *>(QStringLiteral("routeSettingsPages"));
            auto *mode = routing->findChild<QComboBox *>(QStringLiteral("warp_mode"));
            const auto navigation = routing->findChildren<QPushButton *>(QStringLiteral("routeSettingsNav"));
            auto *strategy = routing->findChild<QComboBox *>(QStringLiteral("domainStrategyCombo"));
            auto *defaultStrategy = routing->findChild<QComboBox *>(QStringLiteral("default_domain_strategy"));
            if (!tabs || !mode || navigation.size() != tabs->count() || !strategy || !defaultStrategy ||
                tabs->currentWidget()->objectName() != QStringLiteral("tab_2")) {
                qWarning("Routing settings: navigation must match the real pages and open on profiles");
                qApp->exit(2);
                return;
            }
            for (auto *button: navigation) {
                if (button->text().trimmed().isEmpty()) {
                    qWarning("Routing settings: a navigation label is empty");
                    qApp->exit(2);
                    return;
                }
            }
            QSet<int> visited;
            for (auto *button: navigation) {
                button->click();
                visited.insert(tabs->currentIndex());
            }
            if (visited.size() != tabs->count()) {
                qWarning("Routing settings: navigation buttons must open distinct pages");
                qApp->exit(2);
                return;
            }
            for (auto *picker: {strategy, defaultStrategy}) {
                const int inherited = picker->findData(QString());
                if (inherited < 0 || picker->itemText(inherited).trimmed().isEmpty()) {
                    qWarning("Routing settings: the default strategy needs a label and an empty stored value");
                    qApp->exit(2);
                    return;
                }
            }
            for (int i = 0; i < tabs->count(); ++i) {
                if (tabs->widget(i)->isAncestorOf(mode)) tabs->setCurrentIndex(i);
            }
            mode->setCurrentIndex(1);
            routing->show();
            QTimer::singleShot(400, routing, [routing, parent, prefix] {
                auto *masque = routing->findChild<QGroupBox *>(QStringLiteral("warp_masque_box"));
                auto *wireguard = routing->findChild<QGroupBox *>(QStringLiteral("warp_wg_box"));
                const bool ok = masque && masque->isVisible() && wireguard && !wireguard->isVisible() &&
                                capture(routing, prefix + QStringLiteral("-warp.png"));
                auto *settings = Configs::dataManager->settingsRepo.get();
                const auto originalResolve = settings->resolve_domain_strategy;
                const auto originalDefault = settings->default_domain_strategy;
                auto *resolve = routing->findChild<QComboBox *>(QStringLiteral("domainStrategyCombo"));
                auto *defaultDomain = routing->findChild<QComboBox *>(QStringLiteral("default_domain_strategy"));
                resolve->setCurrentIndex(resolve->findData(QStringLiteral("prefer_ipv6")));
                defaultDomain->setCurrentIndex(defaultDomain->findData(QString()));
                routing->accept();
                settings->Save();
                const Configs::SettingsRepo reloaded(Configs::dataManager->getDatabase());
                const bool strategiesSaved = reloaded.resolve_domain_strategy == QStringLiteral("prefer_ipv6") &&
                                             reloaded.default_domain_strategy.isEmpty();
                settings->resolve_domain_strategy = originalResolve;
                settings->default_domain_strategy = originalDefault;
                settings->Save();
                if (!strategiesSaved) {
                    qWarning("Routing settings: translated labels must not become stored strategy values");
                    qApp->exit(2);
                    return;
                }
                if (ok)
                    CaptureUpstreamFeatures(parent, prefix);
                else
                    qApp->exit(2);
            });
        });
    });
}
} // namespace UiPreview
