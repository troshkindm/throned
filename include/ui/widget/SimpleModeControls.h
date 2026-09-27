#pragma once

#include <QAbstractButton>
#include <QColor>
#include <QList>

#include "include/ui/widget/StartStopButton.hpp"

class QLabel;

// The server the simple screen connects to; opens the server sheet.
class SimpleServerCard : public QAbstractButton {
    Q_OBJECT

public:
    explicit SimpleServerCard(QWidget *parent);
    void setServer(const QString &name, const QString &subtitle, const QString &badge, const QColor &badgeColor);
    [[nodiscard]] QString badge() const { return badge_; }
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QString name_;
    QString subtitle_;
    QString badge_;
    QColor badgeColor_;
};

// One tappable line of a grouped list: label, hint underneath, value and chevron.
class SimpleListRow : public QAbstractButton {
    Q_OBJECT

public:
    explicit SimpleListRow(QWidget *parent);
    void setLabel(const QString &label);
    void setHint(const QString &hint, bool warning = false);
    void setValue(const QString &value);
    void setFirst(bool first) {
        first_ = first;
        update();
    }
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QString label_;
    QString hint_;
    QString value_;
    bool warning_ = false;
    bool first_ = false;
};

// Totals while idle, live rates and a short throughput trace while connected.
class SimpleTrafficLine : public QWidget {
    Q_OBJECT

public:
    explicit SimpleTrafficLine(QWidget *parent);
    void setIdle(const QString &caption, const QString &totals);
    void addSample(int down, int up);
    void clearSamples();
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override { return sizeHint(); }

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QString caption_;
    QString totals_;
    QList<int> down_;
    int up_ = 0;
    bool live_ = false;
};

class SimpleConnectButton : public StartStopButton {
public:
    using StartStopButton::StartStopButton;

protected:
    void paintEvent(QPaintEvent *event) override;
};
