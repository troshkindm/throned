#include "include/ui/widget/ThronedToggle.h"
#include "include/ui/setting/ThemeManager.hpp"

#include <QPainter>
#include <QVariant>

ThronedToggle::ThronedToggle(bool checked, QWidget *parent) : QAbstractButton(parent) {
    setCheckable(true);
    setChecked(checked);
    setCursor(Qt::PointingHandCursor);
    setFixedSize(38, 20);
}

void ThronedToggle::bindTo(QAbstractButton *source) {
    if (!source) return;
    setChecked(source->isChecked());
    setEnabled(source->isEnabled());
    connect(this, &QAbstractButton::toggled, source, &QAbstractButton::setChecked);
    connect(source, &QAbstractButton::toggled, this, &QAbstractButton::setChecked);
}

void ThronedToggle::paintEvent(QPaintEvent *) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    const auto colors = themeManager()->Colors();
    const QColor trackInk = colors.dark ? QColor(Qt::white) : QColor(Qt::black);
    const bool materialTrack = !isChecked() && window()->property("custom-style").toBool();
    painter.setBrush(isChecked() ? colors.success : materialTrack ? QColor(trackInk.red(), trackInk.green(), trackInk.blue(), 15)
                                                                  : colors.controlInactive);
    if (materialTrack) painter.setPen(QPen(QColor(trackInk.red(), trackInk.green(), trackInk.blue(), 90), 1));
    painter.drawRoundedRect(materialTrack ? QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5) : QRectF(rect()), height() / 2.0, height() / 2.0);
    painter.setPen(Qt::NoPen);
    painter.setBrush(!isChecked() && materialTrack && !colors.dark ? QColor(QStringLiteral("#5D5D5D")) : QColor(Qt::white));
    const qreal diameter = height() - 4;
    const qreal x = isChecked() ? width() - diameter - 2 : 2;
    painter.drawEllipse(QRectF(x, 2, diameter, diameter));
}
