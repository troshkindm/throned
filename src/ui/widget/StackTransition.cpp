#include "include/ui/widget/StackTransition.h"

#include <QEvent>
#include <QPainter>
#include <QStackedWidget>
#include <QVariantAnimation>

namespace {
constexpr int kDurationMs = 170;
constexpr int kSlidePx = 28;
} // namespace

StackTransition::StackTransition(QStackedWidget *stack) : QWidget(stack), stack_(stack), animation_(new QVariantAnimation(this)) {
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setAttribute(Qt::WA_NoSystemBackground);
    animation_->setStartValue(0.0);
    animation_->setEndValue(1.0);
    animation_->setDuration(kDurationMs);
    animation_->setEasingCurve(QEasingCurve::OutCubic);
    connect(animation_, &QVariantAnimation::valueChanged, this, [this](const QVariant &value) {
        progress_ = value.toReal();
        update();
    });
    connect(animation_, &QVariantAnimation::finished, this, &StackTransition::finish);
    stack->installEventFilter(this);
    hide();
}

void StackTransition::switchTo(QWidget *page, bool forward) {
    QWidget *current = stack_->currentWidget();
    if (page == nullptr || page == current) return;
    animation_->stop();
    const bool animate = animated_ && current != nullptr && stack_->isVisible() && !stack_->size().isEmpty();
    // Grabbed before the switch: afterwards the old page is hidden and paints nothing.
    if (animate) old_ = current->grab();
    stack_->setCurrentWidget(page);
    if (!animate) {
        finish();
        return;
    }
    forward_ = forward;
    progress_ = 0;
    setGeometry(stack_->rect());
    raise();
    QWidget::show();
    animation_->start();
}

void StackTransition::finish() {
    animation_->stop();
    old_ = QPixmap();
    progress_ = 1;
    hide();
}

void StackTransition::paintEvent(QPaintEvent *) {
    if (old_.isNull()) return;
    QPainter painter(this);
    const int direction = (forward_ != (layoutDirection() == Qt::RightToLeft)) ? -1 : 1;
    painter.setOpacity(1.0 - progress_);
    painter.drawPixmap(QPointF(direction * kSlidePx * progress_, 0), old_);
}

bool StackTransition::eventFilter(QObject *watched, QEvent *event) {
    // A picture of the old size would be wrong the moment the stack resizes.
    if (watched == stack_ && (event->type() == QEvent::Resize || event->type() == QEvent::Hide) && animation_->state() == QAbstractAnimation::Running) finish();
    return QWidget::eventFilter(watched, event);
}
