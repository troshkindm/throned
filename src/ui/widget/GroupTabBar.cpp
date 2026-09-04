#include "include/ui/widget/GroupTabBar.h"

#include "include/ui/setting/ThemeManager.hpp"

#include <QMouseEvent>
#include <QPainter>
#include <QStylePainter>
#include <QStyleOptionTab>

namespace {
    constexpr int kLineHeight = 2;
    // The group is now a detached pill. Keep the meter inside that pill rather
    // than letting it fall into the gap above the profile table.
    constexpr int kBottomInset = 8;

    // Same three states the start button and the latency column use, from the theme.
    QColor usageColor(GroupTabBar::Urgency urgency) {
        const auto colors = themeManager()->Colors();
        switch (urgency) {
        case GroupTabBar::Urgency::Critical: return colors.danger;
        case GroupTabBar::Urgency::Warning: return colors.warning;
        default: return colors.success;
        }
    }
}

GroupTabBar::GroupTabBar(QWidget *parent) : QTabBar(parent) {
    // Without tracking, moves only arrive while a button is held.
    setMouseTracking(true);
}

void GroupTabBar::setUsage(int index, double fraction, Urgency urgency) {
    if (fraction < 0) usage_.remove(index);
    else usage_[index] = {qBound(0.0, fraction, 1.0), urgency};
    update();
}

void GroupTabBar::setSubscription(int index, bool subscription) {
    if (subscription) subscriptions_.insert(index);
    else subscriptions_.remove(index);
}

void GroupTabBar::clearUsage() {
    usage_.clear();
    subscriptions_.clear();
    update();
}

void GroupTabBar::setSelectionVisible(bool visible) {
    if (selectionVisible_ == visible) return;
    selectionVisible_ = visible;
    update();
}


void GroupTabBar::mousePressEvent(QMouseEvent *event) {
    QTabBar::mousePressEvent(event);
    if (event->button() != Qt::LeftButton) return;
    for (auto it = usage_.constBegin(); it != usage_.constEnd(); ++it) {
        const QRect rect = tabRect(it.key());
        if (rect.isEmpty()) continue;
        // Generous vertically: the meter is 2px tall, and nobody aims at 2px.
        const QRect hot(rect.left(), rect.bottom() - kBottomInset - 4, rect.width(), kLineHeight + 8);
        if (hot.contains(event->position().toPoint())) {
            emit meterClicked(it.key());
            return;
        }
    }
}

void GroupTabBar::mouseMoveEvent(QMouseEvent *event) {
    QTabBar::mouseMoveEvent(event);
    const int index = tabAt(event->position().toPoint());
    const int subscriptionTab = subscriptions_.contains(index) ? index : -1;
    if (subscriptionTab == hoveredSubscription_) return;
    hoveredSubscription_ = subscriptionTab;
    if (subscriptionTab < 0) emit meterHoverLeft();
    else emit meterHovered(subscriptionTab);
}

void GroupTabBar::leaveEvent(QEvent *event) {
    QTabBar::leaveEvent(event);
    if (hoveredSubscription_ < 0) return;
    hoveredSubscription_ = -1;
    emit meterHoverLeft();
}
void GroupTabBar::paintEvent(QPaintEvent *event) {
    QTabBar::paintEvent(event);

    // QTabWidget requires a current page, but Favourites is a view layered over
    // that page rather than another tab. Repaint only the current tab with its
    // ordinary state so every theme (including skin-provided gradients) supplies
    // the right inactive appearance without duplicating its colours here.
    if (!selectionVisible_ && currentIndex() >= 0) {
        QStyleOptionTab option;
        initStyleOption(&option, currentIndex());
        option.state &= ~(QStyle::State_Selected | QStyle::State_HasFocus);
        QStylePainter painter(this);
        painter.drawControl(QStyle::CE_TabBarTab, option);
    }

    if (usage_.isEmpty()) return;

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, false);
    painter.setPen(Qt::NoPen);
    for (int i = 0; i < count(); ++i) {
        const auto it = usage_.constFind(i);
        if (it == usage_.constEnd()) continue;
        const QRect rect = tabRect(i);
        if (rect.isEmpty()) continue;

        // Inset so the line reads as part of the tab rather than the strip's own edge.
        const QRect track(rect.left() + 7, rect.bottom() - kBottomInset, rect.width() - 14, kLineHeight);
        if (track.width() <= 0) continue;

        QColor spent = usageColor(it->urgency);
        QColor rest = spent;
        rest.setAlpha(45);
        painter.setBrush(rest);
        painter.drawRect(track);

        QRect filled = track;
        filled.setWidth(qRound(track.width() * it->fraction));
        if (filled.width() > 0) {
            painter.setBrush(spent);
            painter.drawRect(filled);
        }
    }
}

GroupTabWidget::GroupTabWidget(QWidget *parent) : QTabWidget(parent) {
    setTabBar(new GroupTabBar(this));
}

GroupTabBar *GroupTabWidget::groupTabBar() const {
    return qobject_cast<GroupTabBar *>(tabBar());
}
