#pragma once

#include <QList>
#include <QWidget>

#include <functional>

class RuntimeHistoryChart : public QWidget {
public:
    explicit RuntimeHistoryChart(QWidget *parent = nullptr);
    void setFormatter(std::function<QString(double)> formatter);
    void push(double application, double core);
    void clear();

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    struct Sample {
        double application;
        double core;
    };
    QList<Sample> samples_;
    std::function<QString(double)> formatter_;
};
