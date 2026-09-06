#include "include/ui/widget/GroupOverflowMenu.h"

#include <QFont>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QPushButton>
#include <QVBoxLayout>

#include "include/database/GroupsRepo.h"
#include "include/database/entities/Group.h"
#include "include/global/Configs.hpp"
#include "include/ui/setting/ThemeManager.hpp"
#include "include/ui/widget/MaterialIcon.h"

namespace {
constexpr int kGroupIdRole = Qt::UserRole + 1;
}

GroupOverflowMenu::GroupOverflowMenu(std::function<void(int)> chooseGroup, QWidget *parent)
    : TrayPopupFrame(parent), chooseGroup_(std::move(chooseGroup)) {
    auto *list = new QListWidget(m_card);
    list->setUniformItemSizes(true);
    list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    list->setObjectName(QStringLiteral("groupOverflowList"));
    list->setMinimumHeight(220);
    m_cardLayout->addWidget(list, 1);
    setListWidget(list);

    m_search->setPlaceholderText(tr("Find a group"));
    connect(m_search, &QLineEdit::textChanged, this, [this] { rebuild(); });
    connect(m_search, &QLineEdit::returnPressed, this, [this] {
        QListWidgetItem *item = m_list->currentItem();
        if (item == nullptr && m_list->count() > 0) item = m_list->item(0);
        activateItem(item);
    });
    connect(m_list, &QListWidget::itemClicked, this, [this](QListWidgetItem *item) { activateItem(item); });

    // Beside the themed pills, a palette-coloured card reads as a different application.
    // The base paints from the platform palette; clear that so the template below wins.
    m_card->setStyleSheet({});
    m_search->setStyleSheet({});
    m_card->setObjectName(QStringLiteral("groupOverflowCard"));
    themeManager()->RegisterStyle(this, QStringLiteral(R"(
QFrame#groupOverflowCard { background: #171B21; border: 1px solid #2F3136; border-radius: 10px; }
QLineEdit#traySearch {
    color: #F1F3F5; background: #22272E; border: 1px solid #2F3136;
    border-radius: 7px; padding: 6px 9px;
}
QLineEdit#traySearch:focus { border-color: #237AE9; }
QPushButton { color: #DDE2E7; background: #222529; border: 1px solid #2F3136; border-radius: 7px; }
QPushButton:hover { background: #292E35; border-color: #4A535E; }
QListWidget#groupOverflowList {
    color: #DDE2E7; background: transparent; border: none; outline: none;
}
QListWidget#groupOverflowList::item { padding: 6px 8px; border-radius: 6px; }
QListWidget#groupOverflowList::item:hover { background: #22272E; }
QListWidget#groupOverflowList::item:selected { color: #F1F3F5; background: #182530; }
QListWidget#groupOverflowList QScrollBar:vertical {
    background: transparent; width: 8px; margin: 0px;
}
QListWidget#groupOverflowList QScrollBar::handle:vertical {
    background: #3E454F; border-radius: 4px; min-height: 24px;
}
QListWidget#groupOverflowList QScrollBar::handle:vertical:hover { background: #4A535E; }
QListWidget#groupOverflowList QScrollBar::add-line:vertical,
QListWidget#groupOverflowList QScrollBar::sub-line:vertical { height: 0px; }
QListWidget#groupOverflowList QScrollBar::add-page:vertical,
QListWidget#groupOverflowList QScrollBar::sub-page:vertical { background: transparent; }
)"));
    if (auto *close = m_card->findChild<QPushButton *>()) {
        close->setObjectName(QStringLiteral("groupOverflowClose"));
        close->setText({});
        close->setIcon(MaterialIcon::icon(MaterialIcon::Glyph::Close, themeManager()->Colors().textMuted, 15));
        close->setIconSize(QSize(15, 15));
        close->setFixedSize(32, 32);
        close->setCursor(Qt::PointingHandCursor);
    }
    m_search->setFixedHeight(32);
}

void GroupOverflowMenu::preparePopup() {
    clearSearch();
    rebuild();
}

void GroupOverflowMenu::rebuild() {
    m_list->clear();
    if (Configs::dataManager == nullptr || Configs::dataManager->groupsRepo == nullptr) return;

    const QString query = m_search->text().trimmed();
    const auto current = Configs::dataManager->groupsRepo->CurrentGroup();
    const int currentId = current ? current->id : -1;

    for (const int id: Configs::dataManager->groupsRepo->GetGroupsTabOrder()) {
        const auto group = Configs::dataManager->groupsRepo->GetGroup(id);
        if (!group || group->archive) continue;
        if (!query.isEmpty() && !group->name.contains(query, Qt::CaseInsensitive)) continue;

        // The count is what tells two similarly named subscriptions apart.
        const auto label = QStringLiteral("%1  ·  %2").arg(group->name).arg(group->Profiles().size());
        auto *item = new QListWidgetItem(label, m_list);
        item->setData(kGroupIdRole, id);
        if (id == currentId) {
            QFont font = item->font();
            font.setBold(true);
            item->setFont(font);
            m_list->setCurrentItem(item);
        }
    }
    if (m_list->currentItem() == nullptr && m_list->count() > 0) m_list->setCurrentRow(0);
}

void GroupOverflowMenu::activateItem(QListWidgetItem *item) {
    if (item == nullptr) return;
    const int id = item->data(kGroupIdRole).toInt();
    close();
    if (chooseGroup_) chooseGroup_(id);
}
