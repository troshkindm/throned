#include "include/ui/stats/RuntimeHistoryChart.h"

#include "include/ui/setting/ThemeManager.hpp"

#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>

#include <algorithm>
#include <cmath>
#include <utility>

RuntimeHistoryChart::RuntimeHistoryChart(QWidget *parent) : QWidget(parent) {
    setMinimumHeight(52);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    connect(themeManager(), &ThemeManager::themeChanged, this, [this] { update(); });
}

void RuntimeHistoryChart::setFormatter(std::function<QString(double)> formatter) {
    formatter_ = std::move(formatter);
}

void RuntimeHistoryChart::push(double application, double core) {
    samples_.append({std::max(0.0, application), std::max(0.0, core)});
    while (samples_.size() > 60) samples_.removeFirst();
    update();
}

void RuntimeHistoryChart::clear() {
    samples_.clear();
    update();
}

void RuntimeHistoryChart::paintEvent(QPaintEvent *) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const auto colors = themeManager()->Colors();
    double peak = 1.0;
    for (const auto &sample: samples_) peak = std::max({peak, sample.application, sample.core});
    const double magnitude = std::pow(10.0, std::floor(std::log10(peak)));
    const double normalized = peak / magnitude;
    const double scale = (normalized <= 1 ? 1 : normalized <= 2 ? 2
                                            : normalized <= 5   ? 5
                                                                : 10) *
                         magnitude;
    const QString topLabel = formatter_ ? formatter_(scale) : QString::number(scale);
    const QFontMetrics metrics(font());
    const int gutter = metrics.horizontalAdvance(topLabel) + 12;
    const QRectF plot(0, 8, qMax(1, width() - gutter), qMax(1, height() - 20));
    for (int step = 0; step <= 2; ++step) {
        const qreal y = plot.top() + plot.height() * step / 2.0;
        painter.setPen(colors.border);
        painter.drawLine(QPointF(plot.left(), y), QPointF(plot.right(), y));
        painter.setPen(colors.textSubtle);
        const double value = scale * (2 - step) / 2;
        painter.drawText(QRectF(plot.right() + 8, y - metrics.height() / 2.0, gutter - 8, metrics.height()),
                         Qt::AlignLeft | Qt::AlignVCenter, formatter_ ? formatter_(value) : QString::number(value));
    }
    if (samples_.isEmpty()) return;
    for (int series = 1; series >= 0; --series) {
        const QColor ink = series == 0 ? colors.accent : colors.textMuted;
        QPainterPath line;
        const auto point = [&](int i) {
            const double value = series == 0 ? samples_[i].application : samples_[i].core;
            return QPointF(plot.right() - plot.width() * (samples_.size() - 1 - i) / 59.0,
                           plot.bottom() - value / scale * plot.height());
        };
        if (colors.chartBars) {
            painter.setPen(Qt::NoPen);
            QColor fill = ink;
            fill.setAlphaF(series == 0 ? 0.8 : 0.35);
            painter.setBrush(fill);
            for (int i = 0; i < samples_.size(); ++i) {
                const auto p = point(i);
                painter.drawRect(QRectF(p.x(), p.y(), qMax(1.0, plot.width() / 60 - 1), plot.bottom() - p.y()));
            }
            continue;
        }
        line.moveTo(point(0));
        for (int i = 1; i < samples_.size(); ++i) line.lineTo(point(i));
        QPainterPath area = line;
        area.lineTo(plot.right(), plot.bottom());
        area.lineTo(point(0).x(), plot.bottom());
        area.closeSubpath();
        QLinearGradient fade(plot.topLeft(), plot.bottomLeft());
        QColor fill = ink;
        fill.setAlphaF(series == 0 ? 0.24 : 0.1);
        fade.setColorAt(0, fill);
        fill.setAlpha(0);
        fade.setColorAt(1, fill);
        painter.fillPath(area, fade);
        painter.setPen(QPen(ink, series == 0 ? 1.6 : 1.1));
        painter.setBrush(Qt::NoBrush);
        painter.drawPath(line);
    }
}
