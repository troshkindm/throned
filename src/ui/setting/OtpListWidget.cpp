#include "include/ui/setting/OtpListWidget.h"

#include <QApplication>
#include <QDrag>
#include <QDragEnterEvent>
#include <QDragLeaveEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>

namespace {
constexpr auto ROW_MIME_TYPE = "application/otp-row-number";
constexpr int INDICATOR_THICKNESS = 2;
constexpr int INDICATOR_INSET = 4;
} // namespace

OtpListWidget::OtpListWidget(QWidget *parent) : QListWidget(parent) {
    setSelectionMode(NoSelection);
    setFocusPolicy(Qt::NoFocus);
    setDragEnabled(false);
    setAcceptDrops(true);
    viewport()->setAcceptDrops(true);
}

void OtpListWidget::mousePressEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton) {
        pressPos = event->position().toPoint();
        pressedRow = indexAt(pressPos).row();
    }
    QListWidget::mousePressEvent(event);
}

void OtpListWidget::mouseMoveEvent(QMouseEvent *event) {
    if (!(event->buttons() & Qt::LeftButton) || pressedRow < 0 || count() < 2) {
        QListWidget::mouseMoveEvent(event);
        return;
    }
    if ((event->position().toPoint() - pressPos).manhattanLength() < QApplication::startDragDistance()) {
        QListWidget::mouseMoveEvent(event);
        return;
    }

    auto *mime = new QMimeData;
    mime->setData(ROW_MIME_TYPE, QByteArray::number(pressedRow));

    auto *drag = new QDrag(this);
    drag->setMimeData(mime);
    if (auto *rowWidget = itemWidget(item(pressedRow))) {
        drag->setPixmap(rowWidget->grab());
        drag->setHotSpot(pressPos - rowWidget->pos());
    }
    // Never MoveAction: Qt deletes the source row itself when exec() returns it.
    drag->exec(Qt::CopyAction);
    pressedRow = -1;
    setDropRow(-1);
}

int OtpListWidget::insertionRowAt(const QPoint &pos) const {
    if (count() == 0) return 0;

    const auto target = indexAt(pos);
    if (!target.isValid()) return pos.y() <= visualItemRect(item(0)).top() ? 0 : count();

    const int row = target.row();
    return pos.y() > visualItemRect(item(row)).center().y() ? row + 1 : row;
}

void OtpListWidget::setDropRow(const int row) {
    if (dropRow == row) return;
    dropRow = row;
    viewport()->update();
}

void OtpListWidget::dragEnterEvent(QDragEnterEvent *event) {
    if (event->source() == this && event->mimeData()->hasFormat(ROW_MIME_TYPE)) {
        setDropRow(insertionRowAt(event->position().toPoint()));
        event->acceptProposedAction();
    } else {
        event->ignore();
    }
}

void OtpListWidget::dragMoveEvent(QDragMoveEvent *event) {
    if (event->source() == this && event->mimeData()->hasFormat(ROW_MIME_TYPE)) {
        setDropRow(insertionRowAt(event->position().toPoint()));
        event->acceptProposedAction();
    } else {
        event->ignore();
    }
}

void OtpListWidget::dragLeaveEvent(QDragLeaveEvent *event) {
    setDropRow(-1);
    QListWidget::dragLeaveEvent(event);
}

void OtpListWidget::dropEvent(QDropEvent *event) {
    setDropRow(-1);
    if (event->source() != this || !event->mimeData()->hasFormat(ROW_MIME_TYPE)) {
        event->ignore();
        return;
    }

    const int from = event->mimeData()->data(ROW_MIME_TYPE).toInt();
    int to = insertionRowAt(event->position().toPoint());
    // Lifting the row out shifts everything below it up by one.
    if (from < to) --to;
    if (to >= count()) to = count() - 1;

    event->acceptProposedAction();
    if (from >= 0 && to >= 0 && from != to) emit reorderRequested(from, to);
}

void OtpListWidget::paintEvent(QPaintEvent *event) {
    QListWidget::paintEvent(event);
    if (dropRow < 0 || count() == 0) return;

    const int y = dropRow >= count() ? visualItemRect(item(count() - 1)).bottom()
                                     : visualItemRect(item(dropRow)).top();

    QPainter painter(viewport());
    painter.setRenderHint(QPainter::Antialiasing, false);
    painter.setPen(QPen(palette().color(QPalette::Highlight), INDICATOR_THICKNESS));
    painter.drawLine(INDICATOR_INSET, y, viewport()->width() - INDICATOR_INSET, y);
}
