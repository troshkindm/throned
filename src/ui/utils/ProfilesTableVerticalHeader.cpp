#include "include/ui/utils/ProfilesTableVerticalHeader.h"
#include "include/ui/utils/ProfilesFilterProxyModel.h"
#include "include/ui/utils/ProfilesTableModel.h"
#include "include/global/Configs.hpp"
#include "include/database/SettingsRepo.h"
#include "include/ui/setting/ThemeManager.hpp"
#include "include/ui/widget/MaterialIcon.h"
#include <QPainter>
#include <QTableView>
#include <QFontMetrics>
#include <QItemSelectionModel>

namespace {
int pinSide(const QFontMetrics &fm) {
    return qMax(8, fm.height() * 3 / 4);
}

// A push pin on a 12-unit grid.
void paintPin(QPainter *painter, const QRectF &box, const QColor &color) {
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->translate(box.topLeft());
    painter->scale(box.width() / 12.0, box.height() / 12.0);
    painter->setPen(Qt::NoPen);
    painter->setBrush(color);
    painter->drawRoundedRect(QRectF(3.4, 0.6, 5.2, 1.7), 0.6, 0.6);
    painter->drawPolygon(QPolygonF{{4.4, 2.3}, {7.6, 2.3}, {8.4, 6.4}, {3.6, 6.4}});
    painter->drawRoundedRect(QRectF(2.0, 6.0, 8.0, 1.6), 0.6, 0.6);
    painter->setPen(QPen(color, 1.3, Qt::SolidLine, Qt::RoundCap));
    painter->drawLine(QPointF(6.0, 7.6), QPointF(6.0, 11.3));
    painter->restore();
}
} // namespace

ProfilesTableVerticalHeader::ProfilesTableVerticalHeader(QWidget *parent)
    : QHeaderView(Qt::Vertical, parent) {
    setSectionsClickable(true);
}

void ProfilesTableVerticalHeader::setProfilesModel(ProfilesTableModel *model, ProfilesFilterProxyModel *proxy) {
    if (m_model == model && m_proxy == proxy) return;
    if (m_model) disconnect(m_model, nullptr, this, nullptr);
    if (m_proxy) disconnect(m_proxy, nullptr, this, nullptr);
    m_model = model;
    m_proxy = proxy;
    if (m_selectionModel) disconnect(m_selectionModel, nullptr, this, nullptr);
    if (auto *view = qobject_cast<QTableView *>(parentWidget())) {
        m_selectionModel = view->selectionModel();
        if (m_selectionModel) {
            connect(m_selectionModel, &QItemSelectionModel::selectionChanged,
                    this, [this] { viewport()->update(); });
            connect(m_selectionModel, &QItemSelectionModel::currentChanged,
                    this, [this] { viewport()->update(); });
        }
    }
    if (m_model) {
        connect(m_model, &ProfilesTableModel::dataChanged, this, [this](const QModelIndex &topLeft, const QModelIndex &bottomRight) {
            for (int r = topLeft.row(); r <= bottomRight.row(); ++r) {
                // dataChanged carries source rows; sections are numbered by the proxy.
                const int section = m_proxy ? m_proxy->toProxyRow(r) : r;
                if (section >= 0) updateSection(section);
            }
        });
        connect(m_model, &ProfilesTableModel::modelReset, this, [this]() {
            updateWidthFromRowCount();
            update();
        });
        connect(m_model, &ProfilesTableModel::pinnedCountChanged, this, [this]() {
            updateWidthFromRowCount();
            update();
        });
    }
    if (m_proxy) {
        connect(m_proxy, &QAbstractItemModel::rowsInserted, this, [this] { update(); });
        connect(m_proxy, &QAbstractItemModel::rowsRemoved, this, [this] { update(); });
    }
    updateWidthFromRowCount();
    update();
}

void ProfilesTableVerticalHeader::updateWidthFromRowCount() {
    const QFontMetrics fm(font());
    int rows = m_model ? m_model->rowCount() : 0;
    const QString maxNum = rows <= 0 ? QStringLiteral("1") : QString::number(rows);
    int wNum = fm.horizontalAdvance(maxNum);
    constexpr int wCheck = 16;
    // Keep a real, stable index column instead of manufacturing its width with
    // trailing spaces in the label. Three digits can still grow it naturally.
    const int w = qMax(30, qMax(wNum, wCheck) + 14) + pinRoom();
    setMinimumWidth(w);
    setMaximumWidth(w);
}

int ProfilesTableVerticalHeader::pinRoom() const {
    return m_model && m_model->pinnedCount() > 0 ? pinSide(QFontMetrics(font())) + 4 : 0;
}

void ProfilesTableVerticalHeader::paintSection(QPainter *painter, const QRect &rect, int logicalIndex) const {
    painter->save();
    const auto *view = qobject_cast<const QTableView *>(parentWidget());
    const bool selected = view != nullptr && view->selectionModel() != nullptr && view->selectionModel()->isRowSelected(logicalIndex, QModelIndex());
    // logicalIndex counts visible rows; the model is indexed by source row.
    const int sourceRow = m_proxy ? m_proxy->toSourceRow(logicalIndex) : logicalIndex;
    // The id, not the whole RowVisual: building one costs a group lookup and half
    // a dozen strings, and this runs for every visible row on every repaint.
    const bool running = m_model != nullptr && sourceRow >= 0 && m_model->index(sourceRow, 0).data(ProfilesTableModel::ProfileIdRole).toInt() == Configs::dataManager->settingsRepo->started_id;
    const auto colors = themeManager()->Colors();
    const QColor background = selected
                                  ? colors.selection
                                  : (m_sectionBackground.isValid() ? m_sectionBackground : palette().color(QPalette::Base));
    const QColor separator = m_sectionBorder.isValid() ? m_sectionBorder : palette().color(QPalette::Mid);
    painter->fillRect(rect, background);

    // The card already owns the outer edge. Painting another line at the gutter's
    // left edge lands one pixel beside it and reads as a double border.
    painter->setPen(separator);
    if (rect.width() > 1) painter->drawLine(rect.topRight(), rect.bottomRight());
    painter->setPen(selected ? colors.selectionBorder : separator);
    painter->drawLine(rect.bottomLeft() + QPoint(1, 0), rect.bottomRight());
    if (selected)
        painter->drawLine(rect.topLeft() + QPoint(1, 0), rect.topRight());
    const QColor foreground = selected && view != nullptr
                                  ? view->palette().color(QPalette::HighlightedText)
                                  : (running
                                         ? colors.success
                                         : (m_sectionForeground.isValid() ? m_sectionForeground : palette().color(QPalette::Text)));
    // Every row's label shifts right while the group has pins, so the numbers stay in one column.
    const int room = pinRoom();
    const QRect content = rect.adjusted(room, 0, 0, 0);
    if (m_model && m_model->isPinnedRow(sourceRow)) {
        const int side = pinSide(QFontMetrics(font()));
        paintPin(painter, QRectF(rect.left() + 3, rect.top() + (rect.height() - side) / 2.0, side, side),
                 selected ? foreground : colors.accent);
    }
    if (running) {
        constexpr int iconSize = 18;
        const QPixmap check = MaterialIcon::pixmap(MaterialIcon::Glyph::Check, foreground, iconSize);
        painter->drawPixmap(QPoint(content.center().x() - iconSize / 2,
                                   content.center().y() - iconSize / 2),
                            check);
    } else {
        const QString text = m_model
                                 ? m_model->rowLabel(sourceRow, logicalIndex)
                                 : QString::number(logicalIndex + 1);
        painter->setPen(foreground);
        painter->drawText(content, Qt::AlignCenter, text);
    }
    const QAbstractItemModel *rows = m_proxy ? static_cast<QAbstractItemModel *>(m_proxy) : m_model;
    if (rows != nullptr && ProfilesTableModel::endsPinnedBlock(rows->index(logicalIndex, 0))) {
        painter->fillRect(QRect(rect.left(), rect.bottom() - 1, rect.width(), 2), colors.textMuted);
    }
    painter->restore();
}
