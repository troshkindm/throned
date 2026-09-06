#include "include/ui/stats/TrafficChartWidget.h"

#include "include/global/Utils.hpp"

#include <QPainter>
#include <QPaintEvent>
#include <QMouseEvent>
#include <QToolTip>
#include <QDateTime>
#include <QFontMetrics>

namespace {
// Prefixed: the unity build merges anonymous-namespace symbols from batched translation units.
const QColor kChartDownColor(0x4F, 0x8A, 0xF7); // blue
const QColor kChartUpColor(0x34, 0xC9, 0x8A);   // green
constexpr int kChartMarginLeft = 66;
constexpr int kChartMarginRight = 14;
constexpr int kChartMarginTop = 24;
constexpr int kChartMarginBottom = 30;
constexpr int kChartGridLines = 4; // intervals between 0 and the top
} // namespace

TrafficChartWidget::TrafficChartWidget(QWidget* parent) : QWidget(parent) {
    setMinimumHeight(190);
    setMouseTracking(true);
}

void TrafficChartWidget::setData(const QList<Bar>& bars, int labelStride, long long bucketSecs) {
    bars_ = bars;
    labelStride_ = labelStride < 1 ? 1 : labelStride;
    bucketSecs_ = bucketSecs > 0 ? bucketSecs : 3600;
    hovered_ = -1;
    update();
}

QString TrafficChartWidget::bucketRangeText(long long bucketStart) const {
    // fromSecsSinceEpoch renders in the system timezone, matching how the buckets were aligned.
    const QDateTime start = QDateTime::fromSecsSinceEpoch(bucketStart);
    if (bucketSecs_ >= 86400LL) {
        return start.toString("ddd, yyyy-MM-dd");
    }
    const QDateTime end = QDateTime::fromSecsSinceEpoch(bucketStart + bucketSecs_);
    return QString("%1  %2 – %3").arg(start.toString("yyyy-MM-dd"), start.toString("HH:mm"), end.toString("HH:mm"));
}

void TrafficChartWidget::paintEvent(QPaintEvent*) {
    QPainter p(this);

    const QColor textColor = palette().color(QPalette::WindowText);
    QColor mutedColor = textColor;
    mutedColor.setAlpha(150);
    QColor gridColor = textColor;
    gridColor.setAlpha(38);

    const QRectF plot(kChartMarginLeft, kChartMarginTop,
                      width() - kChartMarginLeft - kChartMarginRight,
                      height() - kChartMarginTop - kChartMarginBottom);

    barRects_.clear();

    long long maxTotal = 0;
    for (const auto& b: bars_) maxTotal = qMax(maxTotal, b.down + b.up);
    if (bars_.isEmpty() || maxTotal <= 0 || plot.width() <= 0 || plot.height() <= 0) {
        p.setPen(mutedColor);
        p.drawText(rect(), Qt::AlignCenter, tr("No traffic recorded for this period"));
        return;
    }

    const QFontMetrics fm(font());
    for (int i = 0; i <= kChartGridLines; ++i) {
        const double frac = static_cast<double>(i) / kChartGridLines;
        const double y = plot.bottom() - frac * plot.height();
        p.setPen(gridColor);
        p.drawLine(QPointF(plot.left(), y), QPointF(plot.right(), y));
        p.setPen(mutedColor);
        p.drawText(QRectF(0, y - fm.height() / 2.0, kChartMarginLeft - 6, fm.height()),
                   Qt::AlignRight | Qt::AlignVCenter, ReadableSize(static_cast<qint64>(maxTotal * frac)));
    }

    const int n = bars_.size();
    const double slotW = plot.width() / n;
    const double barW = qMin(slotW * 0.66, 44.0);
    for (int i = 0; i < n; ++i) {
        const auto& b = bars_[i];
        const double slotLeft = plot.left() + i * slotW;
        // Hit-test against the full-height slot, not the bar, which can be a few pixels tall.
        barRects_.append(QRectF(slotLeft, plot.top(), slotW, plot.height()));

        if (hovered_ == i) {
            QColor hl = palette().color(QPalette::Highlight);
            hl.setAlpha(28);
            p.fillRect(QRectF(slotLeft, plot.top(), slotW, plot.height()), hl);
        }

        if (b.down + b.up <= 0) continue;
        const double x = slotLeft + (slotW - barW) / 2.0;
        const double downH = b.down / static_cast<double>(maxTotal) * plot.height();
        const double upH = b.up / static_cast<double>(maxTotal) * plot.height();
        if (downH > 0) p.fillRect(QRectF(x, plot.bottom() - downH, barW, downH), kChartDownColor);
        if (upH > 0) p.fillRect(QRectF(x, plot.bottom() - downH - upH, barW, upH), kChartUpColor);
    }

    p.setPen(mutedColor);
    for (int i = 0; i < n; ++i) {
        if (i % labelStride_ != 0) continue;
        const double slotLeft = plot.left() + i * slotW;
        p.drawText(QRectF(slotLeft - slotW / 2.0, plot.bottom() + 4, slotW * 2.0, kChartMarginBottom - 6),
                   Qt::AlignHCenter | Qt::AlignTop, bars_[i].label);
    }

    const int sw = fm.height() - 4; // swatch size
    const QString downLbl = tr("Download");
    const QString upLbl = tr("Upload");
    const int downTextW = fm.horizontalAdvance(downLbl);
    const int upTextW = fm.horizontalAdvance(upLbl);
    const int itemGap = 16;
    const double ly = (kChartMarginTop - sw) / 2.0;
    double lx = plot.right() - (sw + 4 + downTextW + itemGap + sw + 4 + upTextW);
    p.fillRect(QRectF(lx, ly, sw, sw), kChartDownColor);
    lx += sw + 4;
    p.setPen(textColor);
    p.drawText(QRectF(lx, ly - 2, downTextW + 2, sw + 4), Qt::AlignLeft | Qt::AlignVCenter, downLbl);
    lx += downTextW + itemGap;
    p.fillRect(QRectF(lx, ly, sw, sw), kChartUpColor);
    lx += sw + 4;
    p.drawText(QRectF(lx, ly - 2, upTextW + 2, sw + 4), Qt::AlignLeft | Qt::AlignVCenter, upLbl);
}

void TrafficChartWidget::mouseMoveEvent(QMouseEvent* event) {
    const QPointF pos = event->position();
    int hit = -1;
    for (int i = 0; i < barRects_.size(); ++i) {
        if (barRects_[i].contains(pos)) {
            hit = i;
            break;
        }
    }
    if (hit != hovered_) {
        hovered_ = hit;
        update();
    }
    if (hit >= 0 && hit < bars_.size()) {
        const auto& b = bars_[hit];
        const auto when = bucketRangeText(b.bucketStart);
        QToolTip::showText(event->globalPosition().toPoint(),
                           QString("%1\n↓ %2   ↑ %3\nΣ %4")
                               .arg(when, ReadableSize(b.down), ReadableSize(b.up),
                                    ReadableSize(b.down + b.up)),
                           this);
    } else {
        QToolTip::hideText();
    }
    QWidget::mouseMoveEvent(event);
}

void TrafficChartWidget::leaveEvent(QEvent* event) {
    if (hovered_ != -1) {
        hovered_ = -1;
        update();
    }
    QWidget::leaveEvent(event);
}
