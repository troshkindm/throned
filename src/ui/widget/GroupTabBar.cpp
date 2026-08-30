#include "include/ui/widget/GroupTabBar.h"

#include <QPainter>
#include <QStyleOptionTab>

namespace {
    // The app has no shared warning/danger tokens yet, so these match the hexes
    // the routing editor already uses for the same three states.
    constexpr auto kHealthy = "#2EBC75";
    constexpr auto kLow = "#E8A33D";
    constexpr auto kCritical = "#FF4D56";

    constexpr double kLowFrom = 0.75;
    constexpr double kCriticalFrom = 0.90;
    constexpr int kLineHeight = 2;
    // The group is now a detached pill. Keep the meter inside that pill rather
    // than letting it fall into the gap above the profile table.
    constexpr int kBottomInset = 8;

    QColor usageColor(double fraction) {
        if (fraction >= kCriticalFrom) return QColor(kCritical);
        if (fraction >= kLowFrom) return QColor(kLow);
        return QColor(kHealthy);
    }
}

GroupTabBar::GroupTabBar(QWidget *parent) : QTabBar(parent) {}

void GroupTabBar::setUsage(int index, double fraction) {
    if (fraction < 0) usage_.remove(index);
    else usage_[index] = qBound(0.0, fraction, 1.0);
    update();
}

void GroupTabBar::clearUsage() {
    usage_.clear();
    update();
}

void GroupTabBar::paintEvent(QPaintEvent *event) {
    QTabBar::paintEvent(event);
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

        QColor spent = usageColor(*it);
        QColor rest = spent;
        rest.setAlpha(45);
        painter.setBrush(rest);
        painter.drawRect(track);

        QRect filled = track;
        filled.setWidth(qRound(track.width() * *it));
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
