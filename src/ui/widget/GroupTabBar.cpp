#include "include/ui/widget/GroupTabBar.h"

#include "include/ui/setting/ThemeManager.hpp"

#include <QMouseEvent>
#include <QResizeEvent>
#include <QPainter>
#include <QStylePainter>
#include <QStyleOptionTab>
#include <QToolButton>
#include <QWheelEvent>

namespace {
constexpr int kLineHeight = 2;
// The group is now a detached pill. Keep the meter inside that pill rather
// than letting it fall into the gap above the profile table.
constexpr int kBottomInset = 8;

// Same three states the start button and the latency column use, from the theme.
QColor usageColor(GroupTabBar::Urgency urgency) {
    const auto colors = themeManager()->Colors();
    switch (urgency) {
        case GroupTabBar::Urgency::Critical:
            return colors.danger;
        case GroupTabBar::Urgency::Warning:
            return colors.warning;
        default:
            return colors.success;
    }
}
} // namespace

GroupTabBar::GroupTabBar(QWidget *parent) : QTabBar(parent) {
    // Without tracking, moves only arrive while a button is held.
    setMouseTracking(true);
}

void GroupTabBar::setUsage(int index, double fraction, Urgency urgency) {
    if (fraction < 0)
        usage_.remove(index);
    else
        usage_[index] = {qBound(0.0, fraction, 1.0), urgency};
    update();
}

void GroupTabBar::setSubscription(int index, bool subscription) {
    if (subscription)
        subscriptions_.insert(index);
    else
        subscriptions_.remove(index);
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
    if (subscriptionTab < 0)
        emit meterHoverLeft();
    else
        emit meterHovered(subscriptionTab);
}

void GroupTabBar::leaveEvent(QEvent *event) {
    QTabBar::leaveEvent(event);
    if (hoveredSubscription_ < 0) return;
    hoveredSubscription_ = -1;
    emit meterHoverLeft();
}

void GroupTabBar::wheelEvent(QWheelEvent *event) {
    // Scroll by driving Qt's own scroll buttons, which the groupsCard stylesheet
    // keeps zero-width: the strip slides without switching the current group and
    // without duplicating any of QTabBar's scroll bookkeeping here.
    constexpr int kWheelStep = 120; // one detent of a regular mouse wheel
    const QPoint delta = event->angleDelta();
    Qt::ArrowType direction;
    int magnitude;
    if (qAbs(delta.x()) > qAbs(delta.y())) {
        magnitude = delta.x();
        direction = delta.x() > 0 ? Qt::RightArrow : Qt::LeftArrow;
    } else {
        magnitude = delta.y();
        direction = delta.y() < 0 ? Qt::RightArrow : Qt::LeftArrow;
    }
    if (magnitude == 0) {
        event->ignore();
        return;
    }
    const int steps = qMax(1, qAbs(magnitude) / kWheelStep);
    const auto scrollers = findChildren<QToolButton *>();
    for (auto *scroller: scrollers) {
        if (scroller->arrowType() != direction || scroller->isHidden()) continue;
        for (int i = 0; i < steps; ++i) scroller->click();
        event->accept();
        return;
    }
    // No visible scroller means every tab already fits: nothing to scroll.
    event->ignore();
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

    if (!usage_.isEmpty()) {
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

    // The strip scrolls, the button beside it does not; without this the last pill
    // ends in a hard edge hard against that button and the two read as one control.
    // Drawn only on a side that actually has something beyond it.
    if (count() > 0) {
        const bool moreLeft = tabRect(0).left() < 0;
        const bool moreRight = tabRect(count() - 1).right() > width();
        if (moreLeft || moreRight) {
            constexpr int kFadeWidth = 130;
            const QColor ground = themeManager()->Colors().window;
            QColor clear = ground;
            clear.setAlpha(0);
            // Eased rather than linear: a straight ramp has a visible start line, which
            // is the very edge the fade exists to hide.
            const auto ease = [](QLinearGradient &gradient, bool towardsEdge, const QColor &ground, QColor clear) {
                QColor faint = ground;
                faint.setAlphaF(0.06f);
                QColor soft = ground;
                soft.setAlphaF(0.28f);
                QColor mid = ground;
                mid.setAlphaF(0.70f);
                if (towardsEdge) {
                    gradient.setColorAt(0.00, clear);
                    gradient.setColorAt(0.45, faint);
                    gradient.setColorAt(0.72, soft);
                    gradient.setColorAt(0.90, mid);
                    gradient.setColorAt(1.00, ground);
                } else {
                    gradient.setColorAt(0.00, ground);
                    gradient.setColorAt(0.10, mid);
                    gradient.setColorAt(0.28, soft);
                    gradient.setColorAt(0.55, faint);
                    gradient.setColorAt(1.00, clear);
                }
            };
            QPainter fade(this);
            if (moreLeft) {
                QLinearGradient gradient(0, 0, kFadeWidth, 0);
                ease(gradient, false, ground, clear);
                fade.fillRect(QRect(0, 0, kFadeWidth, height()), gradient);
            }
            if (moreRight) {
                QLinearGradient gradient(width() - kFadeWidth, 0, width(), 0);
                ease(gradient, true, ground, clear);
                fade.fillRect(QRect(width() - kFadeWidth, 0, kFadeWidth, height()), gradient);
            }
        }
    }
}

void GroupTabBar::resizeEvent(QResizeEvent *event) {
    QTabBar::resizeEvent(event);
    reportOverflow();
}

void GroupTabBar::tabLayoutChange() {
    QTabBar::tabLayoutChange();
    reportOverflow();
}

// Asked of the laid-out tabs rather than of their summed width: Qt has already
// applied elision and the strip's own margins by this point, and those decide
// whether anything is actually out of reach.
void GroupTabBar::reportOverflow() {
    const bool overflowing =
        count() > 0 && (tabRect(0).left() < 0 || tabRect(count() - 1).right() > width());
    if (overflowing == overflowing_) return;
    overflowing_ = overflowing;
    emit overflowChanged(overflowing_);
}

GroupTabWidget::GroupTabWidget(QWidget *parent) : QTabWidget(parent) {
    setTabBar(new GroupTabBar(this));
}

GroupTabBar *GroupTabWidget::groupTabBar() const {
    return qobject_cast<GroupTabBar *>(tabBar());
}
