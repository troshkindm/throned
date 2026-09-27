#pragma once

#include <QElapsedTimer>
#include <QLabel>
#include <QTimer>

class HoverMarqueeLabel : public QLabel {
public:
    explicit HoverMarqueeLabel(QWidget *parent = nullptr);

protected:
    void enterEvent(QEnterEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void hideEvent(QHideEvent *event) override;
    void paintEvent(QPaintEvent *event) override;

private:
    void resetScroll();
    QTimer timer;
    QElapsedTimer elapsed;
    QString scrollingText;
};
