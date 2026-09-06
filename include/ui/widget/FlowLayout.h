#pragma once

#include <QLayout>
#include <QList>
#include <QMargins>
#include <QRect>
#include <QSize>

#include <algorithm>

// A layout that fills a row and wraps to the next one, instead of squeezing its
// items to fit. Rows of buttons and chips need this: a fixed horizontal layout
// either stretches them edge to edge or clips their captions once the window
// gets narrow, and neither degrades gracefully.
class FlowLayout final : public QLayout {
public:
    explicit FlowLayout(QWidget *parent = nullptr, int horizontalSpacing = 8, int verticalSpacing = 7)
        : QLayout(parent), horizontalSpacing_(horizontalSpacing), verticalSpacing_(verticalSpacing) {}

    ~FlowLayout() override {
        while (QLayoutItem *item = takeAt(0)) delete item;
    }

    void setUniformColumns(bool enabled, int minimumWidth = 150, int maximumWidth = 260) {
        columnMinimum_ = minimumWidth;
        columnMaximum_ = maximumWidth;
        uniformColumns_ = enabled;
        invalidate();
    }

    void addItem(QLayoutItem *item) override { items_.append(item); }
    int count() const override { return items_.size(); }
    QLayoutItem *itemAt(int index) const override { return items_.value(index); }
    QLayoutItem *takeAt(int index) override {
        return index >= 0 && index < items_.size() ? items_.takeAt(index) : nullptr;
    }
    Qt::Orientations expandingDirections() const override { return {}; }
    bool hasHeightForWidth() const override { return true; }
    int heightForWidth(int width) const override { return arrange(QRect(0, 0, width, 0), true); }
    QSize sizeHint() const override { return minimumSize(); }
    QSize minimumSize() const override {
        QSize size;
        for (QLayoutItem *item: items_) size = size.expandedTo(item->minimumSize());
        const QMargins margins = contentsMargins();
        if (uniformColumns_) size.setWidth(std::min(size.width(), columnMinimum_));
        return size + QSize(margins.left() + margins.right(), margins.top() + margins.bottom());
    }
    void setGeometry(const QRect &rect) override {
        QLayout::setGeometry(rect);
        arrange(rect, false);
    }

private:
    int arrangeColumns(const QRect &rect, bool measureOnly) const {
        const QMargins margins = contentsMargins();
        const QRect area = rect.adjusted(margins.left(), margins.top(), -margins.right(), -margins.bottom());
        if (items_.isEmpty()) return margins.top() + margins.bottom();

        int widest = 0;
        int rowHeight = 0;
        for (QLayoutItem *item: items_) {
            widest = std::max(widest, item->sizeHint().width());
            rowHeight = std::max(rowHeight, item->sizeHint().height());
        }
        const int preferred = std::clamp(widest, columnMinimum_, columnMaximum_);
        const int available = std::max(area.width(), columnMinimum_);
        const int columns = std::max(1, (available + horizontalSpacing_) / (preferred + horizontalSpacing_));
        const int columnWidth = (available - (columns - 1) * horizontalSpacing_) / columns;
        const int rows = (items_.size() + columns - 1) / columns;

        if (!measureOnly) {
            for (int index = 0; index < items_.size(); ++index) {
                const int column = index / rows;
                const int row = index % rows;
                items_.at(index)->setGeometry(QRect(area.x() + column * (columnWidth + horizontalSpacing_),
                                                    area.y() + row * (rowHeight + verticalSpacing_),
                                                    columnWidth, rowHeight));
            }
        }
        return rows * rowHeight + (rows - 1) * verticalSpacing_ + margins.top() + margins.bottom();
    }

    int arrange(const QRect &rect, bool measureOnly) const {
        if (uniformColumns_) return arrangeColumns(rect, measureOnly);
        const QMargins margins = contentsMargins();
        const QRect area = rect.adjusted(margins.left(), margins.top(), -margins.right(), -margins.bottom());
        int x = area.x();
        int y = area.y();
        int rowHeight = 0;
        for (QLayoutItem *item: items_) {
            const QSize hint = item->sizeHint();
            if (x > area.x() && x + hint.width() > area.right() + 1) {
                x = area.x();
                y += rowHeight + verticalSpacing_;
                rowHeight = 0;
            }
            if (!measureOnly) item->setGeometry(QRect(QPoint(x, y), hint));
            x += hint.width() + horizontalSpacing_;
            rowHeight = std::max(rowHeight, hint.height());
        }
        return y + rowHeight - rect.y() + margins.bottom();
    }

    QList<QLayoutItem *> items_;
    int horizontalSpacing_;
    int verticalSpacing_;
    bool uniformColumns_ = false;
    int columnMinimum_ = 150;
    int columnMaximum_ = 260;
};
