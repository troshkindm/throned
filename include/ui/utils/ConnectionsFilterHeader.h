#pragma once

#include <QHeaderView>
#include <QKeyEvent>
#include <QLineEdit>
#include <QToolButton>
#include <QVector>

// Filter row over the connections table; traffic/speed hold formatted byte counts, so they get no field.
class ConnectionsFilterHeader : public QHeaderView {
    Q_OBJECT
public:
    enum Column { ColDest = 0, ColProcess = 1, ColProtocol = 2, ColOutbound = 3, ColClose = 6, ColumnCount = 7 };

    explicit ConnectionsFilterHeader(QWidget *parent = nullptr)
        : QHeaderView(Qt::Horizontal, parent) {
        setSectionsClickable(true);
        setDefaultAlignment(Qt::AlignHCenter | Qt::AlignTop);

        dest_filter = makeEdit();
        process_filter = makeEdit();
        protocol_filter = makeEdit();
        outbound_filter = makeEdit();

        connect(this, &QHeaderView::sectionResized, this, &ConnectionsFilterHeader::adjustPositions);

        setFiltersVisible(false);
    }

    bool filtersVisible() const { return m_filtersVisible; }

    bool hasActiveFilter() const {
        for (QLineEdit *edit : filterEdits()) {
            if (!edit->text().isEmpty()) return true;
        }
        return false;
    }

    bool accepts(const QString &dest, const QString &process,
                 const QString &protocol, const QString &outbound) const {
        return matches(dest_filter, dest) && matches(process_filter, process)
            && matches(protocol_filter, protocol) && matches(outbound_filter, outbound);
    }

    QSize sizeHint() const override {
        QSize s = QHeaderView::sizeHint();
        if (m_filtersVisible) {
            s.setHeight(s.height() + 32);
        }
        return s;
    }

protected:
    // Protocol/Outbound are ResizeToContents, so without a floor their fields shrink to the header label's width.
    QSize sectionSizeFromContents(int logicalIndex) const override {
        QSize s = QHeaderView::sectionSizeFromContents(logicalIndex);
        if (m_filtersVisible && editForColumn(logicalIndex) != nullptr) {
            s.setWidth(qMax(s.width(), 120));
        }
        return s;
    }

    void updateGeometries() override {
        QHeaderView::updateGeometries();
        adjustPositions();
    }

    bool eventFilter(QObject *obj, QEvent *event) override {
        if (!qobject_cast<QLineEdit*>(obj)) return QHeaderView::eventFilter(obj, event);

        // Window shortcuts resolve before the key reaches the field, so bare Return/Del would fire menu actions.
        if (event->type() == QEvent::ShortcutOverride) {
            if (!isTextEditingKey(static_cast<QKeyEvent*>(event))) {
                return QHeaderView::eventFilter(obj, event);
            }
            event->accept();
            return true;
        }

        if (event->type() == QEvent::KeyPress
            && static_cast<QKeyEvent*>(event)->key() == Qt::Key_Escape) {
            emit closeRequested();
            return true;
        }
        return QHeaderView::eventFilter(obj, event);
    }

public slots:
    void setFiltersVisible(bool visible) {
        m_filtersVisible = visible;

        // Hiding must clear, or the list stays filtered with nothing explaining why.
        if (!visible) {
            for (QLineEdit *edit : filterEdits()) edit->clear();
        }

        if (auto btn = qobject_cast<QToolButton*>(sender())) {
            btn->setToolTip(visible ? tr("Disable Filter") : tr("Enable Filter"));
        }

        for (QLineEdit *edit : filterEdits()) edit->setVisible(visible);

        resizeSections();
        emit geometriesChanged();

        // Tab/Backtab/Shortcut focus reasons make QLineEdit select all; OtherFocusReason does not.
        if (visible) dest_filter->setFocus(Qt::OtherFocusReason);
    }

    void adjustPositions() {
        if (!m_filtersVisible || count() < ColumnCount) return;

        const int editHeight = 24;
        const int topPos = height() - editHeight - 4;

        auto place = [&](QLineEdit *edit, int section) {
            edit->setGeometry(sectionViewportPosition(section) + 2, topPos, sectionSize(section) - 4, editHeight);
        };
        place(dest_filter, ColDest);
        place(process_filter, ColProcess);
        place(protocol_filter, ColProtocol);
        place(outbound_filter, ColOutbound);
    }

signals:
    void filtersChanged();
    // The checkable toolbutton owns the visible state, so it has to be the one to untoggle us.
    void closeRequested();

private:
    QLineEdit *makeEdit() {
        auto *edit = new QLineEdit(this->viewport());
        edit->setPlaceholderText(tr("Filter..."));
        edit->setClearButtonEnabled(true);
        edit->installEventFilter(this);
        connect(edit, &QLineEdit::textChanged, this, [this] { emit filtersChanged(); });
        return edit;
    }

    QLineEdit *editForColumn(int column) const {
        switch (column) {
        case ColDest:     return dest_filter;
        case ColProcess:  return process_filter;
        case ColProtocol: return protocol_filter;
        case ColOutbound: return outbound_filter;
        default:          return nullptr;
        }
    }

    QVector<QLineEdit*> filterEdits() const {
        return {dest_filter, process_filter, protocol_filter, outbound_filter};
    }

    static bool matches(const QLineEdit *edit, const QString &value) {
        const QString needle = edit->text();
        return needle.isEmpty() || value.contains(needle, Qt::CaseInsensitive);
    }

    static bool isTextEditingKey(QKeyEvent *key) {
        if (!(key->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier))) {
            return key->key() < Qt::Key_F1 || key->key() > Qt::Key_F35;
        }
        for (auto standard : {QKeySequence::SelectAll, QKeySequence::Copy, QKeySequence::Cut,
                              QKeySequence::Paste, QKeySequence::Undo, QKeySequence::Redo,
                              QKeySequence::MoveToStartOfLine, QKeySequence::MoveToEndOfLine,
                              QKeySequence::SelectStartOfLine, QKeySequence::SelectEndOfLine,
                              QKeySequence::DeleteStartOfWord, QKeySequence::DeleteEndOfWord}) {
            if (key->matches(standard)) return true;
        }
        return false;
    }

    QLineEdit *dest_filter;
    QLineEdit *process_filter;
    QLineEdit *protocol_filter;
    QLineEdit *outbound_filter;
    bool m_filtersVisible = false;
};
