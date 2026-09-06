#include "include/ui/stats/MiniChartWidget.h"

#include <QPainter>
#include <QPaintEvent>
#include <QPainterPath>
#include <QFontMetricsF>
#include <QPolygonF>

#include <algorithm>
#include <cmath>
#include <vector>

namespace {
double niceCeil(double v) {
    if (v <= 0) return 1.0;
    const double mag = std::pow(10.0, std::floor(std::log10(v)));
    const double n = v / mag;
    double nice;
    if (n <= 1.0)
        nice = 1.0;
    else if (n <= 2.0)
        nice = 2.0;
    else if (n <= 5.0)
        nice = 5.0;
    else
        nice = 10.0;
    return nice * mag;
}
} // namespace

MiniChartWidget::MiniChartWidget(QWidget *parent) : QWidget(parent) {
    setMinimumHeight(46);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

void MiniChartWidget::setCapacity(int n) {
    cap_ = n > 1 ? n : 1;
    for (auto &series: series_)
        while (series.values.size() > static_cast<std::size_t>(cap_)) series.values.pop_front();
    update();
}

void MiniChartWidget::setColors(const QColor &primary, const QColor &secondary) {
    setSeriesStyles({{primary, Qt::SolidLine, true}, {secondary, Qt::SolidLine, false}});
}

void MiniChartWidget::setSeriesStyles(const QList<MiniChartSeriesStyle> &styles) {
    series_.clear();
    series_.reserve(styles.size());
    for (const auto &style: styles) series_.push_back({{}, style});
    update();
}

void MiniChartWidget::setMaxValue(double m) {
    fixedMax_ = m;
    update();
}

void MiniChartWidget::setFormatter(std::function<QString(double)> formatter) {
    formatter_ = std::move(formatter);
    update();
}

void MiniChartWidget::setCaption(const QString &caption) {
    caption_ = caption;
    update();
}

void MiniChartWidget::push(double primary, double secondary) {
    if (series_.size() != 2)
        setSeriesStyles({{QColor{}, Qt::SolidLine, true}, {QColor{}, Qt::SolidLine, false}});
    pushValues({primary, secondary});
}

void MiniChartWidget::pushValues(const QList<double> &values) {
    if (values.size() != static_cast<qsizetype>(series_.size())) return;
    for (qsizetype i = 0; i < values.size(); ++i) {
        auto &samples = series_[static_cast<std::size_t>(i)].values;
        samples.push_back(values.at(i));
        while (samples.size() > static_cast<std::size_t>(cap_)) samples.pop_front();
    }
    update();
}

void MiniChartWidget::clear() {
    for (auto &series: series_) series.values.clear();
    update();
}

void MiniChartWidget::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const QColor textColor = palette().color(QPalette::WindowText);
    QColor mutedColor = textColor;
    mutedColor.setAlpha(150);
    QColor gridColor(128, 128, 128, 128);

    // Scale against the normal range in the most recent minute, not its single
    // highest value. Brief lag spikes therefore stay obvious as clipped triangles
    // without flattening 20-50 ms traffic against a 2000 ms ceiling. A sustained
    // slowdown still moves the 90th percentile and expands the scale naturally.
    // Missing probes are ignored here and rendered separately at the upper edge.
    double maxV = fixedMax_;
    if (maxV <= 0) {
        constexpr int scaleWindow = 30;
        std::vector<double> recent;
        for (const auto &series: series_) {
            const int first = qMax(0, static_cast<int>(series.values.size()) - scaleWindow);
            for (int i = first; i < static_cast<int>(series.values.size()); ++i) {
                const double value = series.values[static_cast<std::size_t>(i)];
                if (std::isfinite(value) && value >= 0) recent.push_back(value);
            }
        }
        std::sort(recent.begin(), recent.end());
        const double normalPeak = recent.empty()
                                      ? 0.0
                                  : recent.size() < 5
                                      ? recent.back()
                                      : recent[static_cast<std::size_t>(std::floor((recent.size() - 1) * 0.90))];
        maxV = niceCeil(qMax(50.0, normalPeak * 1.20));
    }
    if (maxV <= 0) maxV = 1.0;

    const QRectF panel = QRectF(rect()).adjusted(4, 4, 0, -4);
    if (panel.width() <= 2 || panel.height() <= 2) return;

    QFont labelFont = font();
    labelFont.setPointSizeF(qMax(7.0, labelFont.pointSizeF() - 1.0));
    const QFontMetricsF fm(labelFont);
    p.setFont(labelFont);

    constexpr int gridIntervals = 4;
    QStringList scaleLabels;
    double yAxisWidth = 0;
    for (int i = gridIntervals; i >= 0; --i) {
        const double value = maxV * i / gridIntervals;
        const QString label = formatter_ ? formatter_(value) : QString::number(value);
        scaleLabels << label;
        yAxisWidth = qMax(yAxisWidth, fm.horizontalAdvance(label));
    }

    const QRectF plot(panel.left() + yAxisWidth + 8, panel.top() + fm.height() / 2.0,
                      panel.width() - yAxisWidth - 8, panel.height() - fm.height());
    if (plot.width() <= 1 || plot.height() <= 1) return;

    // Use the same five dashed horizontal levels and six time divisions as the
    // throughput graph beside this widget.
    QPen gridPen(gridColor, 1.0, Qt::DashLine);
    for (int i = 0; i <= gridIntervals; ++i) {
        const double y = plot.top() + i * plot.height() / gridIntervals;
        p.setPen(gridPen);
        p.drawLine(QPointF(panel.left(), y), QPointF(plot.right(), y));
        p.setPen(mutedColor);
        p.drawText(QRectF(panel.left(), y - fm.height() / 2.0, yAxisWidth, fm.height()),
                   Qt::AlignRight | Qt::AlignVCenter, scaleLabels.at(i));
    }
    constexpr int timeDivisions = 6;
    p.setPen(gridPen);
    for (int i = 0; i < timeDivisions; ++i) {
        const double x = plot.left() + i * plot.width() / timeDivisions;
        p.drawLine(QPointF(x, panel.top()), QPointF(x, panel.bottom()));
    }

    const double stepX = plot.width() / static_cast<double>(cap_ - 1 > 0 ? cap_ - 1 : 1);

    auto drawSeries = [&](const SeriesData &series, const int index) {
        const auto &s = series.values;
        if (s.empty()) return;
        QColor color = series.style.color;
        if (!color.isValid()) {
            color = index == 0 ? palette().color(QPalette::Highlight) : textColor;
            if (index != 0) color.setAlpha(120);
        }
        const int n = static_cast<int>(s.size());
        // Newest sample hugs the right edge; older samples extend left.
        const auto xAt = [&](int i) {
            return plot.right() - stepX * (n - 1 - i);
        };
        const auto pointAt = [&](int i) {
            const double x = xAt(i);
            const double y = plot.bottom() - qBound(0.0, s[i] / maxV, 1.0) * plot.height();
            return QPointF(x, y);
        };
        if (n >= 2) {
            QPainterPath line;
            bool segmentOpen = false;
            for (int i = 0; i < n; ++i) {
                if (!std::isfinite(s[i]) || s[i] < 0) {
                    segmentOpen = false;
                    continue;
                }
                if (segmentOpen)
                    line.lineTo(pointAt(i));
                else
                    line.moveTo(pointAt(i));
                segmentOpen = true;
            }
            QPen pen(color, 1.6);
            pen.setStyle(series.style.penStyle);
            pen.setJoinStyle(Qt::RoundJoin);
            pen.setCapStyle(Qt::RoundCap);
            p.setPen(pen);
            p.setBrush(Qt::NoBrush);
            p.drawPath(line);
        }

        // Missing probes and clipped lag spikes are distinct: a cross means no
        // reply, while a triangle means a real value above the current scale.
        for (int i = 0; i < n; ++i) {
            const double x = xAt(i);
            if (!std::isfinite(s[i]) || s[i] < 0) {
                QPen markerPen(color, 1.8);
                markerPen.setCapStyle(Qt::RoundCap);
                p.setPen(markerPen);
                p.drawLine(QPointF(x - 3.0, plot.top() + 2.0), QPointF(x + 3.0, plot.top() + 8.0));
                p.drawLine(QPointF(x + 3.0, plot.top() + 2.0), QPointF(x - 3.0, plot.top() + 8.0));
            } else if (s[i] > maxV) {
                p.setPen(Qt::NoPen);
                p.setBrush(color);
                p.drawPolygon(QPolygonF{QPointF(x, plot.top() + 1.0),
                                        QPointF(x - 4.0, plot.top() + 7.0),
                                        QPointF(x + 4.0, plot.top() + 7.0)});
            }
        }

        // A larger, haloed latest point stays legible even with only one sample.
        if (std::isfinite(s.back()) && s.back() >= 0) {
            const QPointF latest = pointAt(n - 1);
            QColor halo = color;
            halo.setAlpha(70);
            p.setPen(Qt::NoPen);
            p.setBrush(halo);
            p.drawEllipse(latest, 5.0, 5.0);
            p.setBrush(color);
            p.drawEllipse(latest, 2.8, 2.8);
        }
    };

    // Later series are usually baselines; paint them first so the primary target
    // remains on top when paths overlap.
    for (int i = static_cast<int>(series_.size()) - 1; i >= 0; --i)
        drawSeries(series_[static_cast<std::size_t>(i)], i);

    if (!caption_.isEmpty()) {
        QColor capColor = textColor;
        capColor.setAlpha(190);
        p.setPen(capColor);
        QFont capFont = labelFont;
        capFont.setBold(true);
        p.setFont(capFont);
        p.drawText(QRectF(plot.left(), plot.bottom() - fm.height(), plot.width() - 4, fm.height()),
                   Qt::AlignRight | Qt::AlignVCenter, caption_);
    }
}
