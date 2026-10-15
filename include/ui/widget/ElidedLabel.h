#pragma once

#include <QLabel>
#include <QPainter>
#include <QStyle>

class ElidedLabel : public QLabel {
public:
    using QLabel::QLabel;

    QSize minimumSizeHint() const override { return {0, QLabel::minimumSizeHint().height()}; }

protected:
    void paintEvent(QPaintEvent *) override {
        QPainter painter(this);
        const QRect area = contentsRect();
        const QString shown = fontMetrics().elidedText(text(), Qt::ElideRight, area.width());
        style()->drawItemText(&painter, area, alignment(), palette(), isEnabled(), shown, foregroundRole());
    }
};
