#pragma once

#include <QListWidget>

class ReorderListWidget : public QListWidget {
    Q_OBJECT

public:
    explicit ReorderListWidget(QWidget *parent = nullptr);

    // The row under the cursor, -1 when none; follows scrolling, row rebuilds and drags.
    [[nodiscard]] int HoveredRow() const { return hoveredRow; }

    // Recompute after rows were rebuilt or a row's widget changed size.
    void RefreshHover();

signals:
    void reorderRequested(int from, int to);

    void hoveredRowChanged(int row);

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dragLeaveEvent(QDragLeaveEvent *event) override;
    void dropEvent(QDropEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    [[nodiscard]] int dropRow(const QPoint &pos) const;

    void setHoveredRow(int row);

    QPoint pressPos;
    int pressRow = -1;
    int indicatorRow = -1;
    int hoveredRow = -1;
    bool dragging = false;
};
