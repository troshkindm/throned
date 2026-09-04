#include "include/ui/widget/RoutingQuickMenu.hpp"

#include "include/global/Configs.hpp"
#include "include/database/RoutesRepo.h"
#include "include/database/SettingsRepo.h"
#include "include/database/entities/RouteProfile.h"
#include "include/ui/setting/ThemeManager.hpp"
#include "include/ui/widget/MaterialIcon.h"

#include <QCheckBox>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QPushButton>
#include <QScreen>
#include <QTimer>
#include <QVBoxLayout>

namespace {

std::shared_ptr<Configs::RouteProfile> activeProfile() {
    if (!Configs::dataManager || !Configs::dataManager->routesRepo) return nullptr;
    return Configs::dataManager->routesRepo->GetRouteProfile(
        Configs::dataManager->settingsRepo->current_route_id);
}

QString outboundLabel(int outboundID) {
    switch (outboundID) {
    case Configs::directID: return RoutingQuickMenu::tr("Direct");
    case Configs::proxyID: return RoutingQuickMenu::tr("Proxy");
    case Configs::blockID: return RoutingQuickMenu::tr("Block");
    case Configs::warpBypassID: return RoutingQuickMenu::tr("WARP bypass");
    default: return RoutingQuickMenu::tr("Custom");
    }
}

} // namespace

QString RoutingQuickMenu::statusSummary() {
    const auto profile = activeProfile();
    if (!profile) return RoutingQuickMenu::tr("No routing profile");
    QString summary = profile->name + QStringLiteral(" · ") + outboundLabel(profile->defaultOutboundID);
    if (!profile->isRaw && !profile->applyProfileRules) summary += RoutingQuickMenu::tr(" · rules off");
    return summary;
}

RoutingQuickMenu::RoutingQuickMenu(Callbacks cb, QWidget *parent) : QFrame(parent), m_cb(std::move(cb)) {
    setWindowFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    setAttribute(Qt::WA_DeleteOnClose);
    setAttribute(Qt::WA_TranslucentBackground);
    setFrameShape(QFrame::NoFrame);
    setFixedWidth(340);

    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    auto *card = new QFrame(this);
    card->setObjectName(QStringLiteral("routingQuickCard"));
    outer->addWidget(card);

    auto *root = new QVBoxLayout(card);
    root->setContentsMargins(0, 8, 0, 8);
    root->setSpacing(0);

    const auto sectionLabel = [card](const QString &text, const char *name) {
        auto *label = new QLabel(text, card);
        label->setObjectName(QString::fromLatin1(name));
        return label;
    };

    auto *heading = sectionLabel(tr("Routing profile"), "routingQuickHeading");
    heading->setContentsMargins(14, 0, 14, 4);
    root->addWidget(heading);

    auto *profileRow = new QFrame(card);
    profileRow->setObjectName(QStringLiteral("routingQuickProfile"));
    auto *profileLayout = new QHBoxLayout(profileRow);
    profileLayout->setContentsMargins(14, 7, 14, 7);
    m_profileName = new QLabel(profileRow);
    m_profileName->setObjectName(QStringLiteral("routingQuickProfileName"));
    profileLayout->addWidget(m_profileName, 1);
    auto *check = new QLabel(profileRow);
    check->setPixmap(MaterialIcon::pixmap(MaterialIcon::Glyph::Check, themeManager()->Colors().accent, 16));
    profileLayout->addWidget(check);
    root->addWidget(profileRow);

    auto *trafficLabel = sectionLabel(tr("Default traffic"), "routingQuickSection");
    trafficLabel->setContentsMargins(14, 8, 14, 4);
    root->addWidget(trafficLabel);

    auto *segment = new QFrame(card);
    segment->setObjectName(QStringLiteral("routingQuickSegment"));
    auto *segmentLayout = new QHBoxLayout(segment);
    segmentLayout->setContentsMargins(14, 0, 14, 0);
    segmentLayout->setSpacing(6);
    const auto makeSegment = [&](const QString &text, int outboundID) {
        auto *button = new QPushButton(text, segment);
        button->setObjectName(QStringLiteral("routingQuickSegmentButton"));
        button->setCheckable(true);
        button->setCursor(Qt::PointingHandCursor);
        connect(button, &QPushButton::clicked, this, [this, outboundID] {
            if (m_cb.setDefaultOutbound) m_cb.setDefaultOutbound(outboundID);
            rebuild();
        });
        segmentLayout->addWidget(button, 1);
        return button;
    };
    m_direct = makeSegment(tr("Direct"), Configs::directID);
    m_proxy = makeSegment(tr("Through proxy"), Configs::proxyID);
    root->addWidget(segment);

    auto *rulesRow = new QFrame(card);
    rulesRow->setObjectName(QStringLiteral("routingQuickRules"));
    auto *rulesLayout = new QVBoxLayout(rulesRow);
    rulesLayout->setContentsMargins(14, 10, 14, 4);
    rulesLayout->setSpacing(2);
    m_applyRules = new QCheckBox(tr("Apply profile rules"), rulesRow);
    m_applyRules->setObjectName(QStringLiteral("routingQuickCheck"));
    m_applyRules->setCursor(Qt::PointingHandCursor);
    connect(m_applyRules, &QCheckBox::toggled, this, [this](bool enabled) {
        if (m_cb.setApplyProfileRules) m_cb.setApplyProfileRules(enabled);
        rebuild();
    });
    rulesLayout->addWidget(m_applyRules);
    auto *rulesHint = new QLabel(tr("Rules are matched first, then whatever is left\ngoes to the default above."), rulesRow);
    rulesHint->setObjectName(QStringLiteral("routingQuickHint"));
    rulesLayout->addWidget(rulesHint);
    root->addWidget(rulesRow);

    auto *separator = new QFrame(card);
    separator->setObjectName(QStringLiteral("routingQuickSeparator"));
    separator->setFixedHeight(1);
    root->addSpacing(6);
    root->addWidget(separator);
    root->addSpacing(4);

    const auto makeAction = [&](const QString &text, MaterialIcon::Glyph glyph, const std::function<void()> &action) {
        auto *button = new QPushButton(text, card);
        button->setObjectName(QStringLiteral("routingQuickAction"));
        button->setIcon(MaterialIcon::icon(glyph, themeManager()->Colors().textMuted, 17));
        button->setCursor(Qt::PointingHandCursor);
        connect(button, &QPushButton::clicked, this, [this, action] {
            close();
            if (action) action();
        });
        root->addWidget(button);
    };
    makeAction(tr("Open routing profile"), MaterialIcon::Glyph::File, m_cb.openProfile);
    makeAction(tr("Manage profiles…"), MaterialIcon::Glyph::Settings, m_cb.manageProfiles);

    themeManager()->RegisterStyle(this, QStringLiteral(R"(
QFrame#routingQuickCard { background: #171B21; border: 1px solid #2F3136; border-radius: 10px; }
QLabel#routingQuickHeading { color: #F1F3F5; font-weight: 650; }
QLabel#routingQuickSection { color: #A4ABB4; font-size: 12px; }
QFrame#routingQuickProfile { background: transparent; border: none; }
QLabel#routingQuickProfileName { color: #F1F3F5; font-size: 15px; }
QPushButton#routingQuickSegmentButton {
    color: #DDE2E7; background: #222529; border: 1px solid #2F3136;
    border-radius: 6px; padding: 7px 10px;
}
QPushButton#routingQuickSegmentButton:hover { background: #292E35; border-color: #4A535E; }
QPushButton#routingQuickSegmentButton:checked {
    color: white; background: #237AE9; border-color: #3B8BF0; font-weight: 600;
}
QFrame#routingQuickRules { background: transparent; border: none; }
QCheckBox#routingQuickCheck { color: #F1F3F5; spacing: 8px; }
QCheckBox#routingQuickCheck::indicator {
    width: 17px; height: 17px; border: 1px solid #4A535E; border-radius: 5px; background: #22272E;
}
QCheckBox#routingQuickCheck::indicator:checked {
    background: #237AE9; border-color: #4193F4;
    image: url(:/icon/material/check.svg);
}
QLabel#routingQuickHint { color: #747C86; font-size: 12px; }
QFrame#routingQuickSeparator { background: #2F3136; border: none; }
QPushButton#routingQuickAction {
    color: #DDE2E7; background: transparent; border: none;
    padding: 9px 14px; text-align: left;
}
QPushButton#routingQuickAction:hover { background: #292D33; }
)"));

    rebuild();
}

void RoutingQuickMenu::rebuild() {
    const auto profile = activeProfile();
    m_profileName->setText(profile ? profile->name : tr("None"));

    const int defaultOutbound = profile ? profile->defaultOutboundID : Configs::proxyID;
    const QSignalBlocker directBlock(m_direct);
    const QSignalBlocker proxyBlock(m_proxy);
    m_direct->setChecked(defaultOutbound == Configs::directID);
    m_proxy->setChecked(defaultOutbound == Configs::proxyID);

    // A raw profile carries a verbatim sing-box route object, so neither the
    // segmented default nor the rules switch has anything to act on.
    const bool structured = profile && !profile->isRaw;
    m_direct->setEnabled(structured);
    m_proxy->setEnabled(structured);
    m_applyRules->setEnabled(structured);
    const QSignalBlocker rulesBlock(m_applyRules);
    m_applyRules->setChecked(structured ? profile->applyProfileRules : false);
}

void RoutingQuickMenu::popupAt(const QPoint &globalPos) {
    rebuild();
    adjustSize();

    QScreen *screen = QGuiApplication::screenAt(globalPos);
    if (!screen) screen = QGuiApplication::primaryScreen();
    const QRect available = screen ? screen->availableGeometry() : QRect(0, 0, 1024, 768);
    const QSize wanted = size();
    // Anchored above the status bar, centred on the clicked segment.
    int x = globalPos.x() - wanted.width() / 2;
    int y = globalPos.y() - wanted.height() - 8;
    if (y < available.top()) y = globalPos.y() + 8;
    x = qBound(available.left(), x, available.right() - wanted.width());
    y = qBound(available.top(), y, available.bottom() - wanted.height());
    move(x, y);

    show();
    raise();
    activateWindow();

    m_armed = false;
    QTimer::singleShot(150, this, [this] { m_armed = true; });
}

bool RoutingQuickMenu::event(QEvent *e) {
    if (e->type() == QEvent::WindowDeactivate && m_armed) close();
    return QFrame::event(e);
}

void RoutingQuickMenu::keyPressEvent(QKeyEvent *e) {
    if (e->key() == Qt::Key_Escape) {
        close();
        return;
    }
    QFrame::keyPressEvent(e);
}
