#include "include/ui/widget/FloatingJumpButton.h"

#include "include/ui/setting/ThemeManager.hpp"
#include "include/ui/widget/MaterialIcon.h"

#include <QAbstractScrollArea>
#include <QEnterEvent>
#include <QEvent>
#include <QFontMetricsF>
#include <QPainter>
#include <QPainterPath>
#include <QScrollBar>
#include <QVariantAnimation>

#include <algorithm>

namespace {
constexpr int kDiameter = 40;
// Room above and right of the circle for the badge to overhang.
constexpr int kOverhang = 8;
constexpr int kMargin = 14;
constexpr int kSlide = 8;

QColor readableOn(const QColor &fill) {
    return fill.lightnessF() > 0.6 ? QColor(0x16, 0x18, 0x1C) : QColor(Qt::white);
}
} // namespace

FloatingJumpButton::FloatingJumpButton(QAbstractScrollArea *area)
    : QAbstractButton(area), area_(area), fade_(new QVariantAnimation(this)) {
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::NoFocus);
    setAttribute(Qt::WA_Hover);
    resize(sizeHint());
    hide();

    fade_->setDuration(160);
    fade_->setEasingCurve(QEasingCurve::OutCubic);
    connect(fade_, &QVariantAnimation::valueChanged, this, [this](const QVariant &value) {
        progress_ = value.toReal();
        reposition();
        update();
    });
    connect(fade_, &QVariantAnimation::finished, this, [this] {
        if (!shown_) hide();
    });

    area_->installEventFilter(this);
    area_->viewport()->installEventFilter(this);
    connect(themeManager(), &ThemeManager::themeChanged, this, qOverload<>(&QWidget::update));
}

QSize FloatingJumpButton::sizeHint() const {
    return {kDiameter + kOverhang, kDiameter + kOverhang};
}

void FloatingJumpButton::setPending(int count) {
    if (pending_ == count) return;
    pending_ = count;
    update();
}

void FloatingJumpButton::setShown(bool shown) {
    if (shown_ == shown) return;
    shown_ = shown;
    if (shown) {
        reposition();
        show();
        raise();
    }
    fade_->stop();
    fade_->setStartValue(progress_);
    fade_->setEndValue(shown ? 1.0 : 0.0);
    fade_->start();
}

void FloatingJumpButton::reposition() {
    // The viewport excludes the scroll bars, so the button never covers them.
    const QRect view = area_->viewport()->geometry();
    const int slide = qRound((1.0 - progress_) * kSlide);
    move(view.right() - kMargin - kDiameter - kOverhang + 1, view.bottom() - kMargin - height() + 1 + slide);
}

bool FloatingJumpButton::eventFilter(QObject *watched, QEvent *event) {
    if (event->type() == QEvent::Resize || event->type() == QEvent::Move || event->type() == QEvent::LayoutRequest) reposition();
    return QAbstractButton::eventFilter(watched, event);
}

void FloatingJumpButton::enterEvent(QEnterEvent *event) {
    hovered_ = true;
    update();
    QAbstractButton::enterEvent(event);
}

void FloatingJumpButton::leaveEvent(QEvent *event) {
    hovered_ = false;
    update();
    QAbstractButton::leaveEvent(event);
}

void FloatingJumpButton::paintEvent(QPaintEvent *) {
    const auto &colors = themeManager()->Colors();
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setOpacity(progress_);

    const QRectF circle(0.5, kOverhang + 0.5, kDiameter - 1, kDiameter - 1);
    // A soft drop shadow lifts it off busy log text.
    for (int i = 3; i >= 1; --i) {
        QColor shadow(0, 0, 0, 22);
        painter.setPen(Qt::NoPen);
        painter.setBrush(shadow);
        painter.drawEllipse(circle.adjusted(-i * 0.5, i, i * 0.5, i * 1.2));
    }
    painter.setPen(QPen(hovered_ ? colors.accent : colors.borderStrong, 1));
    painter.setBrush(isDown() ? colors.surfaceHover : hovered_ ? colors.surfaceHover
                                                               : colors.surfaceRaised);
    painter.drawEllipse(circle);

    const int glyph = 20;
    painter.drawPixmap(QPointF(circle.center().x() - glyph / 2.0, circle.center().y() - glyph / 2.0),
                       MaterialIcon::pixmap(MaterialIcon::Glyph::ArrowDown, colors.accent, glyph));

    if (pending_ <= 0) return;
    const QString label = pending_ > 999 ? QStringLiteral("999+") : QString::number(pending_);
    QFont badgeFont = font();
    badgeFont.setPixelSize(10);
    badgeFont.setWeight(QFont::DemiBold);
    painter.setFont(badgeFont);
    const QFontMetricsF metrics(badgeFont);
    const qreal badgeHeight = 16;
    const qreal badgeWidth = std::max(badgeHeight, metrics.horizontalAdvance(label) + 9);
    const QRectF badge(width() - badgeWidth - 0.5, 0.5, badgeWidth, badgeHeight);
    painter.setPen(QPen(colors.window, 1.5));
    painter.setBrush(colors.accent);
    painter.drawRoundedRect(badge, badgeHeight / 2, badgeHeight / 2);
    painter.setPen(readableOn(colors.accent));
    painter.drawText(badge, Qt::AlignCenter, label);
}
