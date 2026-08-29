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

    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override;
    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override;
};
