#include "include/ui/widget/ActionButton.h"

#include <QPainter>
#include <QTextLayout>
#include <QTimer>

#include <algorithm>
#include <cmath>
#include <limits>

ActionButton::ActionButton(MaterialIcon::Glyph glyph, const QString &title, const QColor &tone, QWidget *parent)
    : QAbstractButton(parent), glyph_(glyph), title_(title), tone_(tone) {
    setCheckable(true);
    setCursor(Qt::PointingHandCursor);
    setFixedHeight(44);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

ActionButton::~ActionButton() = default;

void ActionButton::setCount(int count) {
    count_ = count;
    update();
}

void ActionButton::setDropHighlight(bool on) {
    if (dropHighlight_ == on) return;
    dropHighlight_ = on;
    update();
}

void ActionButton::setScrollsWhenTooLong(bool enabled) {
    if (!enabled) return;
    // Start inside the dwell so the first pass shows the head as long as every later one.
    offset_ = -kMarqueePause;
    marquee_ = new QTimer(this);
    marquee_->setInterval(40);
    connect(marquee_, &QTimer::timeout, this, [this] {
        offset_ += 1;
        // Dwell at both ends, then start over from the left.
        if (offset_ > overflow_ + kMarqueePause) offset_ = -kMarqueePause;
        update();
    });
}

void ActionButton::ensureLayout(const QFont &font) {
    if (layout_ != nullptr && layoutFont_ == font) return;
    layout_ = std::make_unique<QTextLayout>(title_, font);
    layout_->beginLayout();
    QTextLine line = layout_->createLine();
    line.setLineWidth(std::numeric_limits<qreal>::max());
    layout_->endLayout();
    naturalWidth_ = static_cast<int>(std::ceil(line.naturalTextWidth()));
    layoutFont_ = font;
}

void ActionButton::paintEvent(QPaintEvent *) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const QRectF bounds = rect().adjusted(.5, .5, -.5, -.5);
    painter.setPen(isChecked() ? tone_ : QColor("#2F3136"));
    painter.setBrush(isChecked() ? QColor("#193452") : QColor("#222529"));
    if (dropHighlight_) {
        QColor wash = tone_;
        wash.setAlpha(46);
        painter.setPen(QPen(tone_, 1.5, Qt::DashLine));
        painter.setBrush(wash);
    }
    painter.drawRoundedRect(bounds, 6, 6);

    const auto icon = MaterialIcon::pixmap(glyph_, tone_, 18);
    painter.drawPixmap(13, (height() - 18) / 2, icon);
    QFont titleFont = font();
    titleFont.setWeight(QFont::DemiBold);
    painter.setFont(titleFont);
    painter.setPen(QColor("#F1F3F5"));
    const QRect titleRect(43, 0, width() - 85, height());

    ensureLayout(titleFont);
    overflow_ = std::max(0, naturalWidth_ - titleRect.width());
    if (marquee_ != nullptr) {
        if (overflow_ > 0 && !marquee_->isActive()) marquee_->start();
        if (overflow_ <= 0 && marquee_->isActive()) marquee_->stop();
    }

    const QTextLine line = layout_->lineAt(0);
    const qreal top = titleRect.top() + (titleRect.height() - line.height()) / 2.0;
    if (marquee_ != nullptr && marquee_->isActive()) {
        // The clip is the column; the line itself is drawn whole, so the tail is not
        // cut away as it travels. Drawing into a translated rect would clip it twice.
        painter.setClipRect(titleRect);
        line.draw(&painter, QPointF(titleRect.left() - std::clamp(offset_, 0, overflow_), top));
        painter.setClipping(false);
    } else if (overflow_ > 0) {
        painter.drawText(titleRect, Qt::AlignVCenter | Qt::AlignLeft,
                         QFontMetrics(titleFont).elidedText(title_, Qt::ElideRight, titleRect.width()));
    } else {
        line.draw(&painter, QPointF(titleRect.left(), top));
    }

    const QString count = QString::number(count_);
    const int pillWidth = std::max(26, QFontMetrics(font()).horizontalAdvance(count) + 14);
    const QRectF pill(width() - pillWidth - 12, (height() - 24) / 2.0, pillWidth, 24);
    painter.setPen(Qt::NoPen);
    painter.setBrush(isChecked() ? tone_ : QColor("#2B3037"));
    painter.drawRoundedRect(pill, 5, 5);
    painter.setPen(QColor("#F7F9FA"));
    painter.drawText(pill, Qt::AlignCenter, count);
}

void ActionButton::hideEvent(QHideEvent *) {
    if (marquee_ != nullptr) marquee_->stop();
}
