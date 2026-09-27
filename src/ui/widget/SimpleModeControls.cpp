#include "include/ui/widget/SimpleModeControls.h"

#include <QPainter>
#include <QPainterPath>
#include <QStyle>

#include "include/global/Utils.hpp"
#include "include/ui/setting/ThemeManager.hpp"
#include "include/ui/widget/MaterialIcon.h"

namespace {
QFont controlFont(const QFont &base, qreal factor, QFont::Weight weight = QFont::Normal) {
    QFont font = base;
    if (font.pixelSize() > 0)
        font.setPixelSize(qMax(9, qRound(font.pixelSize() * factor)));
    else
        font.setPointSizeF(qMax(7.0, font.pointSizeF() * factor));
    font.setWeight(weight);
    return font;
}

QRect visualFor(const QWidget *widget, const QRect &rect) {
    return QStyle::visualRect(widget->layoutDirection(), widget->rect(), rect);
}

QString perSecond(int bytes) {
    return bytes <= 0 ? QStringLiteral("0 B/s") : ReadableSize(bytes) + QStringLiteral("/s");
}
} // namespace

SimpleServerCard::SimpleServerCard(QWidget *parent) : QAbstractButton(parent) {
    setCursor(Qt::PointingHandCursor);
    setAttribute(Qt::WA_Hover);
    setFocusPolicy(Qt::StrongFocus);
    connect(themeManager(), &ThemeManager::themeChanged, this, qOverload<>(&QWidget::update));
}

void SimpleServerCard::setServer(const QString &name, const QString &subtitle, const QString &badge, const QColor &badgeColor) {
    name_ = name;
    subtitle_ = subtitle;
    badge_ = badge;
    badgeColor_ = badgeColor;
    setAccessibleName(name);
    setAccessibleDescription(subtitle);
    update();
}

QSize SimpleServerCard::sizeHint() const { return {280, 64}; }
QSize SimpleServerCard::minimumSizeHint() const { return {200, 64}; }

void SimpleServerCard::paintEvent(QPaintEvent *) {
    const auto colors = themeManager()->Colors();
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const bool hovered = underMouse() && isEnabled();
    painter.setPen(QPen(hasFocus() ? colors.accent : colors.border, 1));
    painter.setBrush(isDown() ? colors.surfaceHover : hovered ? colors.surfaceHover
                                                              : colors.surfaceRaised);
    painter.drawRoundedRect(QRectF(rect()).adjusted(.5, .5, -.5, -.5), 14, 14);

    const QRect tile = visualFor(this, QRect(14, (height() - 34) / 2, 34, 34));
    painter.setPen(Qt::NoPen);
    painter.setBrush(colors.accentSoft);
    painter.drawRoundedRect(tile, 10, 10);
    painter.drawPixmap(tile.center() - QPoint(9, 9), MaterialIcon::pixmap(MaterialIcon::Glyph::Public, colors.accent, 18));

    const QRect chevron = visualFor(this, QRect(width() - 30, (height() - 16) / 2, 16, 16));
    painter.drawPixmap(chevron, MaterialIcon::pixmap(MaterialIcon::Glyph::ChevronDown, colors.textSubtle, 16));

    const QFont badgeFont = controlFont(font(), 0.92, QFont::DemiBold);
    const int badgeWidth = badge_.isEmpty() ? 0 : QFontMetrics(badgeFont).horizontalAdvance(badge_) + 10;
    if (badgeWidth > 0) {
        painter.setFont(badgeFont);
        painter.setPen(badgeColor_.isValid() ? badgeColor_ : colors.textMuted);
        painter.drawText(visualFor(this, QRect(width() - 36 - badgeWidth, 0, badgeWidth, height())),
                         Qt::AlignVCenter | Qt::AlignTrailing, badge_);
    }

    const int textLeft = 60;
    const int textWidth = qMax(0, width() - textLeft - 42 - badgeWidth);
    const QFont nameFont = controlFont(font(), 1.08, QFont::DemiBold);
    const QFont subFont = controlFont(font(), 0.9);
    const QFontMetrics nameMetrics(nameFont), subMetrics(subFont);
    const int block = nameMetrics.height() + 2 + subMetrics.height();
    const int top = (height() - block) / 2;
    painter.setFont(nameFont);
    painter.setPen(isEnabled() ? colors.text : colors.textSubtle);
    painter.drawText(visualFor(this, QRect(textLeft, top, textWidth, nameMetrics.height())), Qt::AlignLeading | Qt::AlignVCenter,
                     nameMetrics.elidedText(name_, Qt::ElideRight, textWidth));
    painter.setFont(subFont);
    painter.setPen(colors.textMuted);
    painter.drawText(visualFor(this, QRect(textLeft, top + nameMetrics.height() + 2, textWidth, subMetrics.height())),
                     Qt::AlignLeading | Qt::AlignVCenter, subMetrics.elidedText(subtitle_, Qt::ElideRight, textWidth));
}

SimpleListRow::SimpleListRow(QWidget *parent) : QAbstractButton(parent) {
    setCursor(Qt::PointingHandCursor);
    setAttribute(Qt::WA_Hover);
    setFocusPolicy(Qt::StrongFocus);
    connect(themeManager(), &ThemeManager::themeChanged, this, qOverload<>(&QWidget::update));
}

void SimpleListRow::setLabel(const QString &label) {
    label_ = label;
    setAccessibleName(label);
    update();
}

void SimpleListRow::setHint(const QString &hint, bool warning) {
    hint_ = hint;
    warning_ = warning;
    setAccessibleDescription(hint);
    update();
}

void SimpleListRow::setValue(const QString &value) {
    value_ = value;
    updateGeometry();
    update();
}

QSize SimpleListRow::sizeHint() const { return {280, 52}; }
QSize SimpleListRow::minimumSizeHint() const { return {160, 52}; }

void SimpleListRow::paintEvent(QPaintEvent *) {
    const auto colors = themeManager()->Colors();
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    if (!first_) {
        painter.setPen(QPen(colors.border, 1));
        painter.drawLine(QPointF(14, .5), QPointF(width() - 14, .5));
    }
    if ((underMouse() || isDown()) && isEnabled()) {
        painter.setPen(Qt::NoPen);
        painter.setBrush(colors.surfaceHover);
        painter.drawRoundedRect(QRectF(rect()).adjusted(4, 4, -4, -4), 9, 9);
    }
    if (hasFocus()) {
        painter.setPen(QPen(colors.accent, 1));
        painter.setBrush(Qt::NoBrush);
        painter.drawRoundedRect(QRectF(rect()).adjusted(4.5, 4.5, -4.5, -4.5), 9, 9);
    }

    const QRect chevron = visualFor(this, QRect(width() - 28, (height() - 16) / 2, 16, 16));
    painter.drawPixmap(chevron, MaterialIcon::pixmap(MaterialIcon::Glyph::ChevronRight, colors.textSubtle, 16));

    const QFont valueFont = controlFont(font(), 0.96, QFont::DemiBold);
    const QFontMetrics valueMetrics(valueFont);
    const int valueWidth = value_.isEmpty() ? 0 : qMin(width() / 2, valueMetrics.horizontalAdvance(value_) + 8);
    if (valueWidth > 0) {
        painter.setFont(valueFont);
        painter.setPen(isEnabled() ? colors.text : colors.textSubtle);
        painter.drawText(visualFor(this, QRect(width() - 32 - valueWidth, 0, valueWidth, height())),
                         Qt::AlignVCenter | Qt::AlignTrailing, valueMetrics.elidedText(value_, Qt::ElideRight, valueWidth));
    }

    const int textLeft = 16;
    const int textWidth = qMax(0, width() - textLeft - 40 - valueWidth);
    const QFont labelFont = font();
    const QFont hintFont = controlFont(font(), 0.88);
    const QFontMetrics labelMetrics(labelFont), hintMetrics(hintFont);
    const bool hasHint = !hint_.isEmpty();
    const int block = labelMetrics.height() + (hasHint ? 1 + hintMetrics.height() : 0);
    const int top = (height() - block) / 2;
    painter.setFont(labelFont);
    painter.setPen(isEnabled() ? colors.text : colors.textSubtle);
    painter.drawText(visualFor(this, QRect(textLeft, top, textWidth, labelMetrics.height())), Qt::AlignLeading | Qt::AlignVCenter,
                     labelMetrics.elidedText(label_, Qt::ElideRight, textWidth));
    if (hasHint) {
        painter.setFont(hintFont);
        painter.setPen(warning_ ? colors.warning : colors.textSubtle);
        painter.drawText(visualFor(this, QRect(textLeft, top + labelMetrics.height() + 1, textWidth, hintMetrics.height())),
                         Qt::AlignLeading | Qt::AlignVCenter, hintMetrics.elidedText(hint_, Qt::ElideRight, textWidth));
    }
}

SimpleTrafficLine::SimpleTrafficLine(QWidget *parent) : QWidget(parent) {
    connect(themeManager(), &ThemeManager::themeChanged, this, qOverload<>(&QWidget::update));
}

void SimpleTrafficLine::setIdle(const QString &caption, const QString &totals) {
    caption_ = caption;
    totals_ = totals;
    update();
}

void SimpleTrafficLine::addSample(int down, int up) {
    live_ = true;
    down_.append(qMax(0, down));
    // Forty one-second samples fill the trace without it scrolling faster than it can be read.
    while (down_.size() > 40) down_.removeFirst();
    up_ = qMax(0, up);
    update();
}

void SimpleTrafficLine::clearSamples() {
    live_ = false;
    down_.clear();
    up_ = 0;
    update();
}

QSize SimpleTrafficLine::sizeHint() const { return {280, qMax(30, fontMetrics().height() + 12)}; }

void SimpleTrafficLine::paintEvent(QPaintEvent *) {
    const auto colors = themeManager()->Colors();
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const Qt::LayoutDirection direction = layoutDirection();
    const QFont small = controlFont(font(), 0.92);
    const QFont strong = controlFont(font(), 0.92, QFont::DemiBold);
    const QFontMetrics smallMetrics(small), strongMetrics(strong);
    const QRect area = rect().adjusted(4, 0, -4, 0);
    if (!live_) {
        const int totalsWidth = strongMetrics.horizontalAdvance(totals_);
        painter.setFont(small);
        painter.setPen(colors.textSubtle);
        const QRect captionRect = visualFor(this, QRect(area.left(), 0, qMax(0, area.width() - totalsWidth - 12), height()));
        painter.drawText(captionRect, Qt::AlignVCenter | Qt::AlignLeading,
                         smallMetrics.elidedText(caption_, Qt::ElideRight, captionRect.width()));
        painter.setFont(strong);
        painter.setPen(colors.textMuted);
        // Units and arrows read left to right in every language; a right-to-left run reorders them.
        painter.setLayoutDirection(Qt::LeftToRight);
        painter.drawText(visualFor(this, QRect(area.right() - totalsWidth, 0, totalsWidth + 1, height())),
                         Qt::AlignVCenter | (direction == Qt::RightToLeft ? Qt::AlignLeft : Qt::AlignRight), totals_);
        return;
    }
    const QString down = QStringLiteral("↓ ") + perSecond(down_.isEmpty() ? 0 : down_.last());
    const QString up = QStringLiteral("↑ ") + perSecond(up_);
    const int downWidth = strongMetrics.horizontalAdvance(down);
    const int upWidth = strongMetrics.horizontalAdvance(up);
    painter.setFont(strong);
    painter.setPen(colors.text);
    painter.setLayoutDirection(Qt::LeftToRight);
    painter.drawText(visualFor(this, QRect(area.left(), 0, downWidth + 1, height())), Qt::AlignCenter, down);
    painter.drawText(visualFor(this, QRect(area.right() - upWidth, 0, upWidth + 1, height())), Qt::AlignCenter, up);

    const QRect trace = visualFor(this, QRect(area.left() + downWidth + 14, 4, qMax(0, area.width() - downWidth - upWidth - 28), height() - 8));
    if (trace.width() < 24 || down_.size() < 2) return;
    int peak = 1;
    for (int value: down_) peak = qMax(peak, value);
    QPainterPath line;
    const bool rtl = direction == Qt::RightToLeft;
    for (int i = 0; i < down_.size(); ++i) {
        const qreal step = qreal(i) / (down_.size() - 1);
        const qreal x = rtl ? trace.right() - step * trace.width() : trace.left() + step * trace.width();
        const qreal y = trace.bottom() - qreal(down_.at(i)) / peak * trace.height();
        i == 0 ? line.moveTo(x, y) : line.lineTo(x, y);
    }
    QPainterPath fill = line;
    fill.lineTo(rtl ? trace.left() : trace.right(), trace.bottom());
    fill.lineTo(rtl ? trace.right() : trace.left(), trace.bottom());
    fill.closeSubpath();
    QColor wash = colors.success;
    wash.setAlpha(36);
    painter.setPen(Qt::NoPen);
    painter.setBrush(wash);
    painter.drawPath(fill);
    painter.setPen(QPen(colors.success, 1.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.setBrush(Qt::NoBrush);
    painter.drawPath(line);
}

void SimpleConnectButton::paintEvent(QPaintEvent *) {
    const auto colors = themeManager()->Colors();
    const bool busy = state() == State::Connecting || state() == State::Disconnecting;
    const QColor ink = state() == State::Disabled  ? colors.controlInactive
                       : state() == State::Running ? colors.success
                       : busy                      ? colors.warning
                                                   : colors.accent;
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.translate(rect().center());
    // Drawn at the 152 px design size and scaled, so the short-window variant keeps its proportions.
    const qreal scale = qMin(width(), height()) / 152.0 * (1.0 - press() * 0.035);
    painter.scale(scale, scale);
    QColor halo = ink;
    halo.setAlpha(underMouse() ? 28 : 15);
    painter.setPen(Qt::NoPen);
    painter.setBrush(halo);
    painter.drawEllipse(QPointF(), 73, 73);
    halo.setAlpha(35);
    painter.setPen(QPen(halo, 1));
    painter.setBrush(colors.surfaceRaised);
    painter.drawEllipse(QPointF(), 61, 61);
    QColor fill = ink;
    fill.setAlpha(22);
    painter.setBrush(fill);
    painter.setPen(Qt::NoPen);
    painter.drawEllipse(QPointF(), 60, 60);
    if (hasFocus()) {
        painter.setPen(QPen(colors.accent, 1.5));
        painter.setBrush(Qt::NoBrush);
        painter.drawEllipse(QPointF(), 74, 74);
    }
    painter.setPen(QPen(ink, 3.5, Qt::SolidLine, Qt::RoundCap));
    painter.setBrush(Qt::NoBrush);
    if (busy)
        painter.drawArc(QRectF(-22, -22, 44, 44), static_cast<int>(-spin() * 16), 270 * 16);
    else
        painter.drawPixmap(-28, -28, MaterialIcon::pixmap(MaterialIcon::Glyph::Power, ink, 56));
}
