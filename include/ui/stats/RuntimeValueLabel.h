#pragma once

#include <QLabel>
#include <QPainter>

class RuntimeValueLabel : public QLabel {
public:
    explicit RuntimeValueLabel(QWidget *parent = nullptr) : QLabel(parent) {
        setTextFormat(Qt::PlainText);
        setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    }

    void setText(const QString &value) {
        QLabel::setText(value);
        setToolTip(value);
    }

    QSize minimumSizeHint() const override { return {0, QLabel::minimumSizeHint().height()}; }

protected:
    void paintEvent(QPaintEvent *) override {
        QPainter painter(this);
        painter.setPen(palette().color(QPalette::WindowText));
        painter.drawText(contentsRect(), alignment(),
                         fontMetrics().elidedText(text(), Qt::ElideRight, contentsRect().width()));
    }
};
