#include "include/ui/widget/ThroughputChart.h"

#include "include/global/Utils.hpp"
#include "include/ui/setting/ThemeManager.hpp"

#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>

namespace {
    // Two minutes at one sample a second, the window the old chart also kept.
    constexpr int kWindow = 120;
    constexpr int kPadLeft = 10;
    constexpr int kPadRight = 66;   // room for the scale labels
    constexpr int kPadTop = 10;
    constexpr int kLegendHeight = 26;
    constexpr int kBarSlot = 6;     // bar plus gap, in the retro style

    QString seriesLabel(int series) {
        switch (series) {
        case ThroughputChart::ProxyDown:  return ThroughputChart::tr("Proxy ↓");
        case ThroughputChart::ProxyUp:    return ThroughputChart::tr("Proxy ↑");
        case ThroughputChart::DirectDown: return ThroughputChart::tr("Direct ↓");
        case ThroughputChart::DirectUp:   return ThroughputChart::tr("Direct ↑");
        default: return {};
        }
    }

    // Short, because the gutter is narrow, but the quarter marks are halves of a
    // power of two: rounding to whole units printed the top two grid lines the same.
    QString scaleLabel(qint64 bytes) {
        static const char *units[] = {"B", "KiB", "MiB", "GiB", "TiB"};
        int unit = 0;
        qreal value = static_cast<qreal>(bytes);
        while (value >= 1024.0 && unit < 4) { value /= 1024.0; ++unit; }
        QString number = QString::number(value, 'f', 1);
        if (number.endsWith(QLatin1String(".0"))) number.chop(2);
        return number + QLatin1Char(' ') + QLatin1String(units[unit]);
    }

    // Proxy is the loud pair, direct the quiet one: the eye should find the
    // tunnelled traffic first, which is what the panel is open to watch.
    QColor seriesColor(int series, const ThronedThemeColors &colors) {
        switch (series) {
        case ThroughputChart::ProxyDown:  return colors.accent;
        case ThroughputChart::ProxyUp:    return colors.accentHover;
        case ThroughputChart::DirectDown: return colors.textMuted;
        case ThroughputChart::DirectUp:   return colors.textSubtle;
        default: return colors.textSubtle;
        }
    }
}

ThroughputChart::ThroughputChart(QWidget *parent) : QWidget(parent) {
    setMinimumHeight(120);
    connect(themeManager, &ThemeManager::themeChanged, this, [this] { update(); });
}

void ThroughputChart::addSample(qint64 proxyDown, qint64 proxyUp, qint64 directDown, qint64 directUp) {
    Sample sample;
    sample.value[ProxyDown] = qMax<qint64>(0, proxyDown);
    sample.value[ProxyUp] = qMax<qint64>(0, proxyUp);
    sample.value[DirectDown] = qMax<qint64>(0, directDown);
    sample.value[DirectUp] = qMax<qint64>(0, directUp);
    m_samples.append(sample);
    while (m_samples.size() > kWindow) m_samples.removeFirst();
    update();
}

void ThroughputChart::clear() {
    m_samples.clear();
    update();
}

qint64 ThroughputChart::peak() const {
    qint64 top = 0;
    for (const Sample &sample : m_samples)
        for (const qint64 value : sample.value) top = qMax(top, value);
    return top;
}

QList<qint64> ThroughputChart::buckets(int series, int count) const {
    QList<qint64> result(count, 0);
    if (count <= 0 || m_samples.isEmpty()) return result;
    // Right-aligned: the newest sample owns the last bucket, so the plot scrolls
    // in from the right instead of stretching while it fills up.
    for (int i = 0; i < m_samples.size(); ++i) {
        const int fromEnd = m_samples.size() - 1 - i;
        const int bucket = count - 1 - fromEnd * count / kWindow;
        if (bucket < 0 || bucket >= count) continue;
        result[bucket] = qMax(result[bucket], m_samples[i].value[series]);
    }
    return result;
}

void ThroughputChart::paintEvent(QPaintEvent *) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const auto colors = themeManager->Colors();
    const QRectF plot(qreal(kPadLeft), qreal(kPadTop),
                      qMax(10.0, qreal(width() - kPadLeft - kPadRight)),
                      qMax(10.0, qreal(height() - kPadTop - kLegendHeight)));

    // A rounded scale keeps the grid labels stable instead of twitching every tick.
    const qint64 top = peak();
    qint64 scale = 1024;
    while (scale < top) scale *= 2;

    QPen grid(colors.border);
    grid.setWidthF(1.0);
    painter.setPen(grid);
    painter.setFont(font());
    const QFontMetrics metrics(font());
    for (int step = 0; step <= 4; ++step) {
        const qreal y = plot.top() + plot.height() * step / 4.0;
        painter.setPen(grid);
        painter.drawLine(QPointF(plot.left(), y), QPointF(plot.right(), y));
        painter.setPen(colors.textSubtle);
        painter.drawText(QRectF(plot.right() + 6, y - metrics.height() / 2.0,
                                kPadRight - 10, metrics.height()),
                         Qt::AlignLeft | Qt::AlignVCenter,
                         scaleLabel(scale * (4 - step) / 4));
    }

    if (m_samples.isEmpty()) {
        painter.setPen(colors.textSubtle);
        painter.drawText(plot, Qt::AlignCenter, tr("Waiting for traffic"));
        return;
    }

    // A skin asks for the retro look; the neutral themes stay on the smooth area.
    const bool bars = colors.chartBars;
    const int columns = bars ? qMax(1, static_cast<int>(plot.width()) / kBarSlot) : kWindow;

    for (int series = SeriesCount - 1; series >= 0; --series) {
        const QList<qint64> values = buckets(series, columns);
        const QColor ink = seriesColor(series, colors);
        const auto heightFor = [&](qint64 value) {
            return plot.height() * qMin<qreal>(1.0, static_cast<qreal>(value) / scale);
        };

        if (bars) {
            painter.setPen(Qt::NoPen);
            QColor fill = ink;
            fill.setAlphaF(series < 2 ? 0.85 : 0.45);
            painter.setBrush(fill);
            const qreal slot = plot.width() / columns;
            for (int i = 0; i < columns; ++i) {
                const qreal barHeight = heightFor(values[i]);
                if (barHeight < 1.0) continue;
                painter.drawRect(QRectF(plot.left() + i * slot + 1, plot.bottom() - barHeight,
                                        qMax(1.0, slot - 2), barHeight));
            }
            continue;
        }

        QPainterPath line;
        for (int i = 0; i < columns; ++i) {
            const QPointF point(plot.left() + plot.width() * i / qreal(columns - 1),
                                plot.bottom() - heightFor(values[i]));
            if (i == 0) line.moveTo(point);
            else line.lineTo(point);
        }
        QPainterPath area = line;
        area.lineTo(plot.right(), plot.bottom());
        area.lineTo(plot.left(), plot.bottom());
        area.closeSubpath();

        QLinearGradient fade(plot.topLeft(), plot.bottomLeft());
        QColor stop = ink;
        stop.setAlphaF(series < 2 ? 0.28 : 0.14);
        fade.setColorAt(0.0, stop);
        stop.setAlphaF(0.0);
        fade.setColorAt(1.0, stop);
        painter.setPen(Qt::NoPen);
        painter.setBrush(fade);
        painter.drawPath(area);

        QPen stroke(ink);
        stroke.setWidthF(series < 2 ? 1.6 : 1.1);
        painter.setPen(stroke);
        painter.setBrush(Qt::NoBrush);
        painter.drawPath(line);
    }

    // Legend doubles as the live readout, so the panel answers "how fast right
    // now" without a second row of labels. It sits in the smaller face because the
    // panel is often only half the window wide.
    QFont legendFont = font();
    if (legendFont.pixelSize() > 0) legendFont.setPixelSize(qMax(9, legendFont.pixelSize() - 2));
    else legendFont.setPointSizeF(qMax(7.0, legendFont.pointSizeF() - 1.5));
    painter.setFont(legendFont);
    const QFontMetrics legendMetrics(legendFont);
    qreal x = plot.left();
    const Sample &latest = m_samples.constLast();
    for (int series = 0; series < SeriesCount; ++series) {
        const QColor ink = seriesColor(series, colors);
        const QString text = seriesLabel(series) + QStringLiteral("  ")
            + ReadableSize(latest.value[series]) + QStringLiteral("/s");
        const qreal entryWidth = 11 + legendMetrics.horizontalAdvance(text) + 14;
        if (x + entryWidth > width() - 4) break;
        const qreal y = height() - kLegendHeight / 2.0;
        painter.setPen(Qt::NoPen);
        painter.setBrush(ink);
        painter.drawEllipse(QRectF(x, y - 3, 6, 6));
        painter.setPen(colors.textMuted);
        painter.drawText(QRectF(x + 11, y - legendMetrics.height() / 2.0,
                                legendMetrics.horizontalAdvance(text) + 4, legendMetrics.height()),
                         Qt::AlignLeft | Qt::AlignVCenter, text);
        x += entryWidth;
    }
}
