#include "include/ui/widget/ThronedToggle.h"
#include "include/ui/setting/ThemeManager.hpp"

#include <QPainter>

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
    painter.setBrush(isChecked() ? colors.success : colors.controlInactive);
    painter.drawRoundedRect(rect(), height() / 2.0, height() / 2.0);
    painter.setBrush(QColor(QStringLiteral("#FFFFFF")));
    const qreal diameter = height() - 4;
    const qreal x = isChecked() ? width() - diameter - 2 : 2;
    painter.drawEllipse(QRectF(x, 2, diameter, diameter));
}
