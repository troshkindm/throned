#include "include/ui/widget/SubscriptionPopover.hpp"

#include "include/configs/sub/SubInfo.h"
#include "include/database/GroupsRepo.h"
#include "include/database/entities/Group.h"
#include "include/global/Configs.hpp"
#include "include/global/Utils.hpp"
#include "include/ui/setting/ThemeManager.hpp"
#include "include/ui/widget/MaterialIcon.h"

#include <QDateTime>
#include <QDesktopServices>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QPushButton>
#include <QScreen>
#include <QSignalBlocker>
#include <QToolButton>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

namespace {
    constexpr int kCardWidth = 340;
    constexpr double kQuotaWarnFrom = 0.75;
    constexpr double kQuotaCriticalFrom = 0.90;

    // Same reading the tab meter uses, so the popover and the hairline never disagree.
    QColor stateColor(double usedFraction, int days) {
        const auto colors = themeManager()->Colors();
        if (days >= 0 && days <= 2) return colors.danger;
        if (usedFraction >= kQuotaCriticalFrom) return colors.danger;
        if (days >= 0 && days <= 7) return colors.warning;
        if (usedFraction >= kQuotaWarnFrom) return colors.warning;
        return colors.success;
    }



    // QLabel breaks a wrapped line only where the text already allows it, so an
    // announcement sent as one long run is silently clipped to a single line.
    // Zero-width spaces give the layout somewhere to break without changing what is drawn.
    QString withBreakOpportunities(const QString &text) {
        constexpr int kLongestRun = 24;
        QString out;
        out.reserve(text.size() + text.size() / kLongestRun);
        int run = 0;
        for (const QChar ch : text) {
            // Qt already breaks after these, so a run only counts what it cannot.
            if (ch.isSpace() || ch == u'/' || ch == u'-' || ch == u'.' || ch == u',') {
                run = 0;
            } else if (++run > kLongestRun) {
                out += QChar(0x200B);
                run = 1;
            }
            out += ch;
        }
        return out;
    }
    // Time of day says nothing about a plan that is measured in days.
    QString shortDate(qint64 seconds) {
        return QLocale().toString(QDateTime::fromSecsSinceEpoch(seconds).date(), QLocale::ShortFormat);
    }
    void openProviderLink(const QString &url) {
        const QUrl target(url);
        const auto scheme = target.scheme().toLower();
        if (!target.isValid() || (scheme != "http" && scheme != "https")) return;
        QDesktopServices::openUrl(target);
    }
}

SubscriptionPopover::SubscriptionPopover(std::function<void(int)> updateNow, QWidget *parent)
    : QFrame(parent), m_updateNow(std::move(updateNow)) {
    setWindowFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    setAttribute(Qt::WA_TranslucentBackground);
    setFrameShape(QFrame::NoFrame);
    setFixedWidth(kCardWidth);

    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    auto *card = new QFrame(this);
    card->setObjectName(QStringLiteral("subPopoverCard"));
    outer->addWidget(card);

    auto *root = new QVBoxLayout(card);
    root->setContentsMargins(14, 12, 14, 12);
    root->setSpacing(9);

    const auto linkButton = [card](const QString &text) {
        auto *button = new QPushButton(text, card);
        button->setObjectName(QStringLiteral("subPopoverLink"));
        button->setCursor(Qt::PointingHandCursor);
        button->setFocusPolicy(Qt::NoFocus);
        button->hide();
        return button;
    };

    auto *header = new QHBoxLayout;
    header->setContentsMargins(0, 0, 0, 0);
    header->setSpacing(6);
    m_name = new QLabel(card);
    m_name->setObjectName(QStringLiteral("subPopoverName"));
    m_name->setTextFormat(Qt::PlainText);
    header->addWidget(m_name, 1);
    // Icon-only and in the header with the other per-subscription actions: a whole
    // labelled row for something you touch once would outweigh what it does.
    m_mute = new QToolButton(card);
    m_mute->setObjectName(QStringLiteral("subPopoverMute"));
    m_mute->setCheckable(true);
    m_mute->setCursor(Qt::PointingHandCursor);
    m_mute->setFocusPolicy(Qt::NoFocus);
    m_mute->setFixedSize(24, 24);
    m_mute->setIconSize(QSize(15, 15));
    header->addWidget(m_mute);
    m_support = linkButton(tr("Support"));
    m_website = linkButton(tr("Website"));
    header->addWidget(m_support);
    header->addWidget(m_website);
    root->addLayout(header);

    m_announceBox = new QFrame(card);
    m_announceBox->setObjectName(QStringLiteral("subPopoverAnnounce"));
    auto *announceLayout = new QVBoxLayout(m_announceBox);
    announceLayout->setContentsMargins(10, 7, 10, 7);
    m_announce = new QLabel(m_announceBox);
    m_announce->setObjectName(QStringLiteral("subPopoverAnnounceText"));
    // The provider wrote this, so it is never read as markup.
    m_announce->setTextFormat(Qt::PlainText);
    m_announce->setWordWrap(true);
    announceLayout->addWidget(m_announce);
    m_announceBox->hide();
    root->addWidget(m_announceBox);

    // Two frames sharing a row: the split is the stretch, so no painting of our own.
    auto *meter = new QWidget(card);
    meter->setFixedHeight(4);
    auto *meterLayout = new QHBoxLayout(meter);
    meterLayout->setContentsMargins(0, 0, 0, 0);
    meterLayout->setSpacing(0);
    m_meterSpent = new QFrame(meter);
    m_meterRest = new QFrame(meter);
    meterLayout->addWidget(m_meterSpent);
    meterLayout->addWidget(m_meterRest);
    root->addWidget(meter);

    m_traffic = new QLabel(card);
    m_traffic->setObjectName(QStringLiteral("subPopoverMuted"));
    root->addWidget(m_traffic);

    auto *expiryRow = new QHBoxLayout;
    expiryRow->setContentsMargins(0, 0, 0, 0);
    expiryRow->setSpacing(6);
    m_expiry = new QLabel(card);
    m_expiry->setObjectName(QStringLiteral("subPopoverExpiry"));
    m_expiryDate = new QLabel(card);
    m_expiryDate->setObjectName(QStringLiteral("subPopoverMuted"));
    expiryRow->addWidget(m_expiry, 1);
    expiryRow->addWidget(m_expiryDate);
    root->addLayout(expiryRow);

    auto *footer = new QHBoxLayout;
    footer->setContentsMargins(0, 0, 0, 0);
    footer->setSpacing(6);
    m_updated = new QLabel(card);
    m_updated->setObjectName(QStringLiteral("subPopoverMuted"));
    m_refresh = new QPushButton(tr("Update now"), card);
    m_refresh->setObjectName(QStringLiteral("subPopoverLink"));
    m_refresh->setCursor(Qt::PointingHandCursor);
    m_refresh->setFocusPolicy(Qt::NoFocus);
    footer->addWidget(m_updated, 1);
    footer->addWidget(m_refresh);
    root->addLayout(footer);


    connect(m_mute, &QToolButton::toggled, this, [this](bool muted) {
        applyMuteIcon(!muted);
        const auto group = m_gid < 0 ? nullptr : Configs::dataManager->groupsRepo->GetGroup(m_gid);
        if (group == nullptr) return;
        group->provider.notifyExpiry = !muted;
        Configs::dataManager->groupsRepo->Save(group);
    });
    connect(m_refresh, &QPushButton::clicked, this, [this] {
        const int gid = m_gid;
        close();
        if (m_updateNow && gid >= 0) m_updateNow(gid);
    });

    themeManager()->RegisterStyle(this, QStringLiteral(R"(
QFrame#subPopoverCard { background: #171B21; border: 1px solid #2F3136; border-radius: 10px; }
QLabel#subPopoverName { color: #F1F3F5; font-size: 15px; }
QLabel#subPopoverMuted { color: #A4ABB4; font-size: 12px; background: transparent; }
QLabel#subPopoverExpiry { font-size: 12px; background: transparent; }
QFrame#subPopoverAnnounce { background: #193452; border: none; border-radius: 6px; }
QLabel#subPopoverAnnounceText { color: #F1F3F5; font-size: 12px; background: transparent; }
QPushButton#subPopoverLink {
    background: transparent; border: 1px solid #2F3136; border-radius: 5px;
    color: #A4ABB4; font-size: 12px; padding: 3px 9px;
}
QPushButton#subPopoverLink:hover { background: #222529; color: #F1F3F5; border-color: #4A4F57; }
QToolButton#subPopoverMute { background: transparent; border: 1px solid #2F3136; border-radius: 5px; }
QToolButton#subPopoverMute:hover { background: #222529; border-color: #4A4F57; }
QToolButton#subPopoverMute:checked { background: #222529; }
)"));
}


void SubscriptionPopover::applyMuteIcon(bool notify) {
    const auto colors = themeManager()->Colors();
    m_mute->setIcon(MaterialIcon::icon(notify ? MaterialIcon::Glyph::Bell : MaterialIcon::Glyph::BellOff,
                                       notify ? colors.textMuted : colors.textSubtle, 15));
    m_mute->setToolTip(notify ? tr("Warn me before this subscription runs out")
                              : tr("No warnings for this subscription"));
}
void SubscriptionPopover::fill(const std::shared_ptr<Configs::Group> &group) {
    m_gid = group->id;
    m_name->setText(group->name);

    const QSignalBlocker muteBlock(m_mute);
    m_mute->setChecked(!group->provider.notifyExpiry);
    applyMuteIcon(group->provider.notifyExpiry);

    const auto &provider = group->provider;
    const auto announce = provider.announce.trimmed();
    m_announceBox->setVisible(!announce.isEmpty());
    m_announce->setText(withBreakOpportunities(announce));

    const auto setLink = [](QPushButton *button, const QString &url) {
        button->setVisible(!url.isEmpty());
        button->setToolTip(url);
        button->disconnect();
        if (url.isEmpty()) return;
        connect(button, &QPushButton::clicked, button, [url] { openProviderLink(url); });
    };
    setLink(m_support, provider.supportUrl);
    setLink(m_website, provider.webPageUrl);

    const auto sub = Configs::ParseSubInfo(group->info);
    const int days = sub.valid ? sub.daysLeft(QDateTime::currentSecsSinceEpoch()) : -1;
    const double used = sub.valid ? qMax(0.0, sub.usedFraction()) : 0.0;
    const QColor state = stateColor(used, days);

    QColor rest = state;
    rest.setAlpha(45);
    m_meterSpent->setStyleSheet(QStringLiteral("background: %1; border: none;").arg(state.name()));
    m_meterRest->setStyleSheet(QStringLiteral("background: rgba(%1,%2,%3,45); border: none;")
                                   .arg(state.red()).arg(state.green()).arg(state.blue()));
    // Stretch carries the split, so the bar follows the card width without a repaint of ours.
    auto *meterLayout = qobject_cast<QHBoxLayout *>(m_meterSpent->parentWidget()->layout());
    meterLayout->setStretch(0, qRound(used * 1000));
    meterLayout->setStretch(1, 1000 - qRound(used * 1000));
    m_meterSpent->parentWidget()->setVisible(sub.valid && sub.total > 0);

    if (!sub.valid) {
        m_traffic->setText(tr("The provider sends no allowance for this subscription."));
    } else if (sub.total == 0) {
        m_traffic->setText(tr("%1 used of an unlimited plan").arg(ReadableSize(sub.used())));
    } else {
        m_traffic->setText(tr("%1 of %2 used · %3 left")
                               .arg(ReadableSize(sub.used()), ReadableSize(sub.total),
                                    ReadableSize(qMax<qint64>(0, sub.total - sub.used()))));
    }

    m_expiry->setVisible(days >= 0);
    m_expiryDate->setVisible(days >= 0);
    if (days >= 0) {
        m_expiry->setText(days == 0 ? tr("Expires today") : tr("%n day(s) left", "", days));
        m_expiry->setStyleSheet(QStringLiteral("color: %1;").arg(state.name()));
        m_expiryDate->setText(shortDate(sub.expire));
    }

    QStringList footer;
    if (group->sub_last_update > 0)
        footer << tr("Updated %1").arg(shortDate(group->sub_last_update));
    if (const int minutes = provider.updateIntervalMinutes; minutes > 0) {
        const QString cycle = minutes % 60 == 0 ? tr("every %n h", "", minutes / 60)
                                                : tr("every %n min", "", minutes);
        footer << cycle;
    }
    const QString line = footer.join(QStringLiteral(" · "));
    m_updated->setToolTip(provider.intervalFromProvider ? tr("The provider sets this cycle.") : QString());
    // Budget from the card, not a constant: a translated cycle is wider than the English one.
    const int budget = kCardWidth - 14 * 2 - m_refresh->sizeHint().width() - 6;
    m_updated->setText(m_updated->fontMetrics().elidedText(line, Qt::ElideRight, budget));
    m_refresh->setVisible(!group->url.isEmpty());
}

void SubscriptionPopover::place(const std::shared_ptr<Configs::Group> &group, const QPoint &globalPos) {
    fill(group);
    adjustSize();

    QScreen *screen = QGuiApplication::screenAt(globalPos);
    if (!screen) screen = QGuiApplication::primaryScreen();
    const QRect available = screen ? screen->availableGeometry() : QRect(0, 0, 1024, 768);
    const QSize wanted = size();
    // Hangs below the tab it belongs to, left-aligned with it unless the screen ends first.
    int x = globalPos.x();
    int y = globalPos.y() + 6;
    if (y + wanted.height() > available.bottom()) y = globalPos.y() - wanted.height() - 6;
    x = qBound(available.left(), x, available.right() - wanted.width());
    y = qBound(available.top(), y, available.bottom() - wanted.height());
    move(x, y);
}

void SubscriptionPopover::popupFor(const std::shared_ptr<Configs::Group> &group, const QPoint &globalPos) {
    if (group == nullptr) {
        close();
        return;
    }
    // A click promotes a hovered card into a real popup: it stops closing itself and
    // takes focus, so Escape and click-away work the way they do everywhere else.
    m_hoverCard = false;
    if (m_hoverClose != nullptr) m_hoverClose->stop();
    setAttribute(Qt::WA_ShowWithoutActivating, false);
    place(group, globalPos);

    show();
    raise();
    activateWindow();

    m_armed = false;
    QTimer::singleShot(150, this, [this] { m_armed = true; });
}

void SubscriptionPopover::popupHovered(const std::shared_ptr<Configs::Group> &group, const QPoint &globalPos) {
    m_hoverCard = true;
    // Shown without activating: taking focus every time the pointer crosses a tab
    // would pull it off whatever the user was typing in.
    setAttribute(Qt::WA_ShowWithoutActivating, true);
    if (m_hoverClose == nullptr) {
        m_hoverClose = new QTimer(this);
        m_hoverClose->setSingleShot(true);
        m_hoverClose->setInterval(220);
        connect(m_hoverClose, &QTimer::timeout, this, &SubscriptionPopover::close);
    }
    m_hoverClose->stop();
    place(group, globalPos);
    show();
    raise();
}

void SubscriptionPopover::scheduleHoverClose() {
    // Only a hovered card closes itself: one the user clicked open stays until dismissed.
    if (!m_hoverCard || m_hoverClose == nullptr) return;
    m_hoverClose->start();
}

void SubscriptionPopover::enterEvent(QEnterEvent *e) {
    // The pointer crossed from the tab into the card, so the card is now the target.
    if (m_hoverClose != nullptr) m_hoverClose->stop();
    QFrame::enterEvent(e);
}

void SubscriptionPopover::leaveEvent(QEvent *e) {
    scheduleHoverClose();
    QFrame::leaveEvent(e);
}
bool SubscriptionPopover::event(QEvent *e) {
    if (e->type() == QEvent::WindowDeactivate && m_armed) close();
    return QFrame::event(e);
}

void SubscriptionPopover::keyPressEvent(QKeyEvent *e) {
    if (e->key() == Qt::Key_Escape) {
        close();
        return;
    }
    QFrame::keyPressEvent(e);
}
