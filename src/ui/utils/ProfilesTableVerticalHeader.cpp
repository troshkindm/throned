#include "include/ui/utils/ProfilesTableVerticalHeader.h"
#include "include/ui/utils/ProfilesFilterProxyModel.h"
#include "include/ui/utils/ProfilesTableModel.h"
#include "include/ui/widget/MaterialIcon.h"
#include <QPainter>
#include <QTableView>
#include <QFontMetrics>
#include <QItemSelectionModel>

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
    const int w = qMax(30, qMax(wNum, wCheck) + 14);
    setMinimumWidth(w);
    setMaximumWidth(w);
}

void ProfilesTableVerticalHeader::paintSection(QPainter *painter, const QRect &rect, int logicalIndex) const {
    painter->save();
    const auto *view = qobject_cast<const QTableView *>(parentWidget());
    const bool selected = view != nullptr && view->selectionModel() != nullptr
        && view->selectionModel()->isRowSelected(logicalIndex, QModelIndex());
    // logicalIndex counts visible rows; the model is indexed by source row.
    const int sourceRow = m_proxy ? m_proxy->toSourceRow(logicalIndex) : logicalIndex;
    const bool running = m_model != nullptr && sourceRow >= 0
        && m_model->index(sourceRow, 0).data(ProfilesTableModel::RowVisualRole)
               .value<ProfilesTableModel::RowVisual>().running;
    const QColor background = selected
        ? view->palette().color(QPalette::Highlight)
        : (running && m_sectionRunningBackground.isValid()
               ? m_sectionRunningBackground
               : (m_sectionBackground.isValid() ? m_sectionBackground : palette().color(QPalette::Base)));
    const QColor separator = m_sectionBorder.isValid() ? m_sectionBorder : palette().color(QPalette::Mid);
    const QColor stateBorder = m_sectionRunningBorder.isValid()
        ? m_sectionRunningBorder : (view != nullptr ? view->palette().color(QPalette::Link) : separator);
    painter->fillRect(rect, background);

    // The card already owns the outer edge. Painting another line at the gutter's
    // left edge lands one pixel beside it and reads as a double border.
    painter->setPen(separator);
    if (rect.width() > 1) painter->drawLine(rect.topRight(), rect.bottomRight());
    painter->setPen((selected || running) ? stateBorder : separator);
    painter->drawLine(rect.bottomLeft() + QPoint(1, 0), rect.bottomRight());
    if (selected || running)
        painter->drawLine(rect.topLeft() + QPoint(1, 0), rect.topRight());
    const QColor foreground = selected && view != nullptr
        ? view->palette().color(QPalette::HighlightedText)
        : (running && view != nullptr
               ? view->palette().color(QPalette::Link)
               : (m_sectionForeground.isValid() ? m_sectionForeground : palette().color(QPalette::Text)));
    if (running) {
        constexpr int iconSize = 18;
        const QPixmap check = MaterialIcon::pixmap(MaterialIcon::Glyph::Check, foreground, iconSize);
        painter->drawPixmap(QPoint(rect.center().x() - iconSize / 2,
                                   rect.center().y() - iconSize / 2), check);
    } else {
        const QString text = m_model
            ? m_model->rowLabel(sourceRow, logicalIndex)
            : QString::number(logicalIndex + 1);
        painter->setPen(foreground);
        painter->drawText(rect, Qt::AlignCenter, text);
    }
    painter->restore();
}
