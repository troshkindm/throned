#pragma once

#include <QList>
#include <QWidget>

// The throughput plot for the stats panel. Replaces the vendored qBittorrent
// chart: that one drew from a hardcoded palette and a QGraphicsView, so it could
// not follow a theme and conflicted on every upstream merge.
//
// Four series only, because four are all the app ever fed the old widget.
class ThroughputChart : public QWidget {
    Q_OBJECT
public:
    enum Series { ProxyDown, ProxyUp, DirectDown, DirectUp, SeriesCount };

    explicit ThroughputChart(QWidget *parent = nullptr);

    // Bytes per second for the last tick.
    void addSample(qint64 proxyDown, qint64 proxyUp, qint64 directDown, qint64 directUp);
    void clear();

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    struct Sample { qint64 value[SeriesCount] = {0, 0, 0, 0}; };

    [[nodiscard]] qint64 peak() const;
    [[nodiscard]] QList<qint64> buckets(int series, int count) const;

    QList<Sample> m_samples;
};
