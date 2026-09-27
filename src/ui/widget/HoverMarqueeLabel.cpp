#include "include/ui/widget/HoverMarqueeLabel.h"

#include <QPainter>
#include <QTextLayout>

HoverMarqueeLabel::HoverMarqueeLabel(QWidget *parent) : QLabel(parent) {
    timer.setInterval(30);
    connect(&timer, &QTimer::timeout, this, qOverload<>(&QWidget::update));
}

void HoverMarqueeLabel::enterEvent(QEnterEvent *event) {
    QLabel::enterEvent(event);
    const QString full = property("statusFullText").toString();
    if (fontMetrics().horizontalAdvance(full) <= contentsRect().width()) return;
    scrollingText = full;
    elapsed.start();
    timer.start();
}

void HoverMarqueeLabel::resetScroll() {
    timer.stop();
    scrollingText.clear();
    update();
}

void HoverMarqueeLabel::leaveEvent(QEvent *event) {
    resetScroll();
    QLabel::leaveEvent(event);
}

void HoverMarqueeLabel::hideEvent(QHideEvent *event) {
    resetScroll();
    QLabel::hideEvent(event);
}

void HoverMarqueeLabel::paintEvent(QPaintEvent *event) {
    if (!scrollingText.isEmpty() && scrollingText != property("statusFullText").toString()) resetScroll();
    if (scrollingText.isEmpty() || elapsed.elapsed() < 800) {
        QLabel::paintEvent(event);
        return;
    }
    QTextLayout layout(scrollingText, font());
    layout.beginLayout();
    QTextLine line = layout.createLine();
    line.setLineWidth(100000);
    layout.endLayout();
    const QRect area = contentsRect();
    const qreal overflow = qMax(0.0, line.naturalTextWidth() - area.width());
    const qreal offset = qMin(overflow, (elapsed.elapsed() - 800) * 0.035);
    if (offset >= overflow) timer.stop();
    QPainter painter(this);
    painter.setClipRect(area);
    painter.setPen(palette().color(foregroundRole()));
    const qreal x = scrollingText.isRightToLeft() ? area.right() + 1 - line.naturalTextWidth() + offset
                                                  : area.left() - offset;
    line.draw(&painter, QPointF(x, area.top() + (area.height() - line.height()) / 2));
}
