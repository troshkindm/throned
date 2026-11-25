#pragma once

#include <QPixmap>
#include <QWidget>

class QStackedWidget;
class QVariantAnimation;

// Changes a stacked widget's page at once, then fades a picture of the old page out over the live new one.
// Clicks pass through, so the new page answers from the first frame.
class StackTransition : public QWidget {
public:
    explicit StackTransition(QStackedWidget *stack);
    void setAnimated(bool animated) { animated_ = animated; }
    // Forward slides the old page out to the leading side, back to the trailing one.
    void switchTo(QWidget *page, bool forward);

protected:
    void paintEvent(QPaintEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void finish();

    QStackedWidget *stack_;
    QVariantAnimation *animation_;
    QPixmap old_;
    qreal progress_ = 1;
    bool forward_ = true;
    bool animated_ = false;
};
