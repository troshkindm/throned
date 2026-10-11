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
            if (!tabs || !mode) {
                qApp->exit(2);
                return;
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
                routing->reject();
                if (ok)
                    CaptureUpstreamFeatures(parent, prefix);
                else
                    qApp->exit(2);
            });
        });
    });
}
} // namespace UiPreview
