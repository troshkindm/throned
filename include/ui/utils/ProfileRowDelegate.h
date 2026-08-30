#pragma once

#include <QStyledItemDelegate>

// Paints the comfortable two-line profile row. The model stays a plain table
// model: everything here reads ProfilesTableModel::RowVisualRole.
class ProfileRowDelegate : public QStyledItemDelegate {
    Q_OBJECT
public:
    explicit ProfileRowDelegate(QObject *parent = nullptr) : QStyledItemDelegate(parent) {}

    static constexpr int RowHeight = 52;

    // Width the metric columns are pinned to: measured from the same fonts paint()
    // uses, because ResizeToContents does not agree with them.
    static int metricColumnWidth(int column, const QFont &font);

    // A short tint over one row, so a jump from the status bar lands somewhere
    // the eye can follow. strength 0 clears it.
    void setFlash(int row, qreal strength);

    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override;
    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override;

private:
    int m_flashRow = -1;
    qreal m_flashStrength = 0.0;
};
