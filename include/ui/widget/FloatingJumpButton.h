#pragma once

#include <QAbstractButton>

class QAbstractScrollArea;
class QVariantAnimation;

// A round "scroll to newest" button that floats over a scroll area's bottom-right corner, with a count badge.
class FloatingJumpButton final : public QAbstractButton {
    Q_OBJECT

public:
    explicit FloatingJumpButton(QAbstractScrollArea *area);

    void setPending(int count);

    void setShown(bool shown);

    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent *) override;

    bool eventFilter(QObject *watched, QEvent *event) override;

    void enterEvent(QEnterEvent *) override;

    void leaveEvent(QEvent *) override;

private:
    void reposition();

    QAbstractScrollArea *area_;
    QVariantAnimation *fade_;
    int pending_ = 0;
    qreal progress_ = 0;
    bool shown_ = false;
    bool hovered_ = false;
};
