#include "include/ui/widget/SimpleSheet.h"

#include <QAbstractScrollArea>
#include <QButtonGroup>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QLabel>
#include <QListView>
#include <QPainter>
#include <QPainterPath>
#include <QProgressBar>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QStandardItemModel>
#include <QStyledItemDelegate>
#include <QVBoxLayout>

#include "include/ui/setting/ThemeManager.hpp"
#include "include/ui/utils/ProfileRowDelegate.h"
#include "include/ui/widget/MaterialIcon.h"

namespace {
constexpr int kAddressRole = Qt::UserRole + 1;
constexpr int kLatencyRole = Qt::UserRole + 2;
constexpr int kLatencyTextRole = Qt::UserRole + 3;
constexpr int kCurrentRole = Qt::UserRole + 4;
constexpr int kIdRole = Qt::UserRole + 5;
constexpr int kServerRowHeight = 56;

QColor blend(const QColor &a, const QColor &b, qreal t) {
    return QColor::fromRgbF(a.redF() + (b.redF() - a.redF()) * t, a.greenF() + (b.greenF() - a.greenF()) * t,
                            a.blueF() + (b.blueF() - a.blueF()) * t);
}

QFont sheetFont(const QFont &base, qreal factor, QFont::Weight weight = QFont::Normal) {
    QFont font = base;
    if (font.pixelSize() > 0)
        font.setPixelSize(qMax(9, qRound(font.pixelSize() * factor)));
    else
        font.setPointSizeF(qMax(7.0, font.pointSizeF() * factor));
    font.setWeight(weight);
    return font;
}

class SheetPanel : public QWidget {
public:
    using QWidget::QWidget;

protected:
    void paintEvent(QPaintEvent *) override {
        const auto colors = themeManager()->Colors();
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        QPainterPath shape;
        const QRectF r = QRectF(rect()).adjusted(.5, .5, -.5, 16);
        shape.addRoundedRect(r, 16, 16);
        painter.setPen(QPen(colors.borderStrong, 1));
        painter.setBrush(SimpleSheet::panelColor());
        painter.drawPath(shape);
        painter.setPen(Qt::NoPen);
        painter.setBrush(colors.borderStrong);
        painter.drawRoundedRect(QRectF(width() / 2.0 - 18, 8, 36, 4), 2, 2);
    }
};

class ServerDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    QSize sizeHint(const QStyleOptionViewItem &, const QModelIndex &) const override { return {200, kServerRowHeight}; }

    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override {
        const auto colors = themeManager()->Colors();
        const bool current = index.data(kCurrentRole).toBool();
        const bool hovered = option.state.testFlag(QStyle::State_MouseOver);
        const bool cursor = option.state.testFlag(QStyle::State_HasFocus) && option.state.testFlag(QStyle::State_Selected);
        const bool rtl = option.direction == Qt::RightToLeft;
        const auto visual = [&](const QRect &r) { return QStyle::visualRect(option.direction, option.rect, r); };
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        const QRectF row = QRectF(option.rect).adjusted(2, 2, -2, -2);
        if (current || hovered) {
            painter->setPen(Qt::NoPen);
            painter->setBrush(current ? colors.accentSoft : colors.surfaceHover);
            painter->drawRoundedRect(row, 10, 10);
        }
        if (cursor) {
            painter->setPen(QPen(colors.accent, 1));
            painter->setBrush(Qt::NoBrush);
            painter->drawRoundedRect(row.adjusted(.5, .5, -.5, -.5), 10, 10);
        }
        const QRect base = option.rect;
        const QRect tile = visual(QRect(base.left() + 10, base.center().y() - 15, 30, 30));
        painter->setPen(Qt::NoPen);
        painter->setBrush(current ? colors.accent : colors.surfaceRaised);
        painter->drawRoundedRect(tile, 9, 9);
        painter->drawPixmap(tile.center() - QPoint(8, 8),
                            MaterialIcon::pixmap(MaterialIcon::Glyph::Public, current ? QColor(Qt::white) : colors.textMuted, 16));

        const int checkWidth = 26;
        if (current)
            painter->drawPixmap(visual(QRect(base.right() - checkWidth, base.center().y() - 8, 16, 16)),
                                MaterialIcon::pixmap(MaterialIcon::Glyph::Check, colors.accent, 16));
        const QString latencyText = index.data(kLatencyTextRole).toString();
        const QFont latencyFont = sheetFont(option.font, 0.9, QFont::DemiBold);
        const int latencyWidth = QFontMetrics(latencyFont).horizontalAdvance(latencyText) + 8;
        painter->setFont(latencyFont);
        painter->setPen(ProfileRowDelegate::latencyColor(index.data(kLatencyRole).toInt(), colors));
        painter->drawText(visual(QRect(base.right() - checkWidth - 6 - latencyWidth, base.top(), latencyWidth, base.height())),
                          Qt::AlignVCenter | Qt::AlignTrailing, latencyText);

        const int textLeft = base.left() + 52;
        const int textWidth = qMax(0, base.right() - checkWidth - 12 - latencyWidth - textLeft);
        const QFont nameFont = sheetFont(option.font, 1.0, QFont::DemiBold);
        const QFont addressFont = sheetFont(option.font, 0.88);
        const QFontMetrics nameMetrics(nameFont), addressMetrics(addressFont);
        const int top = base.top() + (base.height() - nameMetrics.height() - addressMetrics.height() - 1) / 2;
        painter->setFont(nameFont);
        painter->setPen(colors.text);
        painter->drawText(visual(QRect(textLeft, top, textWidth, nameMetrics.height())), Qt::AlignVCenter | Qt::AlignLeading,
                          nameMetrics.elidedText(index.data(Qt::DisplayRole).toString(), Qt::ElideRight, textWidth));
        painter->setFont(addressFont);
        painter->setPen(colors.textSubtle);
        painter->drawText(visual(QRect(textLeft, top + nameMetrics.height() + 1, textWidth, addressMetrics.height())),
                          Qt::AlignVCenter | Qt::AlignLeading,
                          addressMetrics.elidedText(index.data(kAddressRole).toString(), rtl ? Qt::ElideLeft : Qt::ElideRight, textWidth));
        painter->restore();
    }
};
} // namespace

class SimpleModeOption : public QAbstractButton {
public:
    SimpleModeOption(MaterialIcon::Glyph glyph, const QString &title, const QString &badge, const QString &description,
                     const QString &note, QWidget *parent)
        : QAbstractButton(parent), glyph_(glyph) {
        setCursor(Qt::PointingHandCursor);
        setAttribute(Qt::WA_Hover);
        setFocusPolicy(Qt::StrongFocus);
        setAccessibleName(title);
        setAccessibleDescription(description);
        auto *row = new QHBoxLayout(this);
        row->setContentsMargins(64, 14, 14, 14);
        body_ = new QVBoxLayout;
        body_->setSpacing(3);
        auto *heading = new QHBoxLayout;
        heading->setSpacing(8);
        auto *titleLabel = new QLabel(title, this);
        titleLabel->setObjectName(QStringLiteral("simpleOptionTitle"));
        heading->addWidget(titleLabel);
        if (!badge.isEmpty()) {
            auto *badgeLabel = new QLabel(badge, this);
            badgeLabel->setObjectName(QStringLiteral("simpleOptionBadge"));
            heading->addWidget(badgeLabel);
        }
        heading->addStretch();
        body_->addLayout(heading);
        auto *descriptionLabel = new QLabel(description, this);
        descriptionLabel->setObjectName(QStringLiteral("simpleOptionText"));
        descriptionLabel->setWordWrap(true);
        body_->addWidget(descriptionLabel);
        if (!note.isEmpty()) {
            auto *noteLabel = new QLabel(note, this);
            noteLabel->setObjectName(QStringLiteral("simpleOptionNote"));
            noteLabel->setWordWrap(true);
            body_->addWidget(noteLabel);
        }
        row->addLayout(body_, 1);
        for (auto *label: findChildren<QLabel *>()) label->setAttribute(Qt::WA_TransparentForMouseEvents);
        connect(themeManager(), &ThemeManager::themeChanged, this, qOverload<>(&QWidget::update));
    }

    void setSelected(bool selected) {
        selected_ = selected;
        update();
    }
    void addBodyWidget(QWidget *widget) { body_->addWidget(widget); }
    QSize sizeHint() const override { return layout()->sizeHint(); }
    QSize minimumSizeHint() const override { return layout()->minimumSize(); }
    bool hasHeightForWidth() const override { return true; }
    int heightForWidth(int width) const override { return layout()->heightForWidth(width); }

protected:
    void paintEvent(QPaintEvent *) override {
        const auto colors = themeManager()->Colors();
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const bool hovered = underMouse() && isEnabled();
        painter.setPen(QPen(selected_ ? colors.accent : hasFocus() ? colors.borderStrong
                                                                   : colors.border,
                            selected_ ? 1.5 : 1));
        painter.setBrush(selected_ ? blend(colors.surfaceRaised, colors.accent, 0.09) : hovered ? colors.surfaceHover
                                                                                                : colors.surfaceRaised);
        painter.drawRoundedRect(QRectF(rect()).adjusted(.75, .75, -.75, -.75), 14, 14);
        const QRect tile = QStyle::visualRect(layoutDirection(), rect(), QRect(14, 14, 38, 38));
        painter.setPen(QPen(selected_ ? colors.accent : colors.border, 1));
        painter.setBrush(selected_ ? colors.accentSoft : colors.window);
        painter.drawRoundedRect(QRectF(tile).adjusted(.5, .5, -.5, -.5), 11, 11);
        painter.drawPixmap(tile.center() - QPoint(10, 10),
                           MaterialIcon::pixmap(glyph_, selected_ ? colors.accent : colors.textMuted, 20));
        if (!isEnabled()) {
            QColor veil = colors.window;
            veil.setAlpha(120);
            painter.setPen(Qt::NoPen);
            painter.setBrush(veil);
            painter.drawRoundedRect(QRectF(rect()), 14, 14);
        }
    }

private:
    MaterialIcon::Glyph glyph_;
    QVBoxLayout *body_;
    bool selected_ = false;
};

namespace {
class WarningBox : public QFrame {
public:
    using QFrame::QFrame;

protected:
    void paintEvent(QPaintEvent *) override {
        const auto colors = themeManager()->Colors();
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        QColor fill = colors.warning, edge = colors.warning;
        fill.setAlpha(28);
        edge.setAlpha(90);
        painter.setPen(QPen(edge, 1));
        painter.setBrush(fill);
        painter.drawRoundedRect(QRectF(rect()).adjusted(.5, .5, -.5, -.5), 10, 10);
    }
};

const QString kSheetStyle = QStringLiteral(R"(
QLabel#simpleSheetTitle { font-size: 16px; font-weight: 600; color: #F1F3F5; background: transparent; }
QLabel#simpleSheetEmpty { color: #747C86; background: transparent; }
QPushButton#simpleSheetLink { border: none; background: transparent; color: #A4ABB4; padding: 4px 2px; }
QPushButton#simpleSheetLink:hover { color: #F1F3F5; }
QPushButton#simpleSheetLink:disabled { color: #747C86; }
QPushButton#simpleSheetTab { border: 1px solid #2F3136; border-radius: 13px; background: transparent; color: #A4ABB4; padding: 3px 11px; }
QPushButton#simpleSheetTab:hover { color: #F1F3F5; }
QPushButton#simpleSheetTab:checked { background: #182530; border-color: #237AE9; color: #F1F3F5; }
QPushButton#simpleSheetAllowance { border: none; background: transparent; color: #A4ABB4; text-align: left; padding: 0; }
QPushButton#simpleSheetAllowance:hover { color: #F1F3F5; }
QProgressBar#simpleSheetAllowanceBar { border: none; border-radius: 1px; background: #2F3136; }
QProgressBar#simpleSheetAllowanceBar::chunk { border-radius: 1px; background: #237AE9; }
QPushButton#simpleSheetAction { border: 1px solid #2F3136; border-radius: 9px; background: transparent; color: #A4ABB4; padding: 7px 10px; }
QPushButton#simpleSheetAction:hover { background: #292D33; color: #F1F3F5; }
QListView#simpleSheetList { background: transparent; border: none; outline: none; }
QLabel#simpleOptionTitle { font-weight: 600; color: #F1F3F5; background: transparent; }
QLabel#simpleOptionBadge { color: #2EBC75; font-size: 11px; font-weight: 600; background: transparent; }
QLabel#simpleOptionText { color: #A4ABB4; background: transparent; }
QLabel#simpleOptionNote { color: #747C86; font-size: 12px; background: transparent; }
QLabel#simpleElevationText { color: #F1F3F5; background: transparent; }
QPushButton#simpleElevationButton { border: none; border-radius: 8px; background: #D9A441; color: #1B1E23; font-weight: 600; padding: 6px 12px; }
)");
} // namespace

ScrollEdgeFade::ScrollEdgeFade(QAbstractScrollArea *area, QColor (*background)())
    : QWidget(area), area_(area), background_(background) {
    setAttribute(Qt::WA_TransparentForMouseEvents);
    area->viewport()->installEventFilter(this);
    const auto refresh = [this] { update(); };
    connect(area->verticalScrollBar(), &QScrollBar::valueChanged, this, refresh);
    connect(area->verticalScrollBar(), &QScrollBar::rangeChanged, this, refresh);
    setGeometry(area->viewport()->geometry());
    raise();
}

bool ScrollEdgeFade::eventFilter(QObject *watched, QEvent *event) {
    if (watched == area_->viewport() && (event->type() == QEvent::Resize || event->type() == QEvent::Move)) {
        setGeometry(area_->viewport()->geometry());
        raise();
    }
    return false;
}

void ScrollEdgeFade::setTopInset(int inset) {
    if (topInset_ == inset) return;
    topInset_ = inset;
    update();
}

void ScrollEdgeFade::paintEvent(QPaintEvent *) {
    const QScrollBar *bar = area_->verticalScrollBar();
    const QColor solid = background_();
    QColor clear = solid;
    clear.setAlpha(0);
    QPainter painter(this);
    if (bar->value() > bar->minimum()) {
        QLinearGradient top(0, topInset_, 0, topInset_ + 22);
        top.setColorAt(0, solid);
        top.setColorAt(1, clear);
        painter.fillRect(QRect(0, topInset_, width(), 22), top);
    }
    if (bar->value() < bar->maximum()) {
        QLinearGradient bottom(0, height() - 28, 0, height());
        bottom.setColorAt(0, clear);
        bottom.setColorAt(1, solid);
        painter.fillRect(QRect(0, height() - 28, width(), 28), bottom);
    }
}

SimpleSheet::SimpleSheet(QWidget *host) : QWidget(host) {
    setObjectName(QStringLiteral("simpleSheet"));
    setFocusPolicy(Qt::StrongFocus);
    panel_ = new SheetPanel(this);
    panel_->setObjectName(QStringLiteral("simpleSheetPanel"));
    animation_ = new QPropertyAnimation(this, "progress", this);
    connect(animation_, &QPropertyAnimation::finished, this, [this] {
        if (open_) return;
        hide();
        emit dismissed();
    });
    host->installEventFilter(this);
    themeManager()->RegisterStyle(this, kSheetStyle);
    connect(themeManager(), &ThemeManager::themeChanged, this, qOverload<>(&QWidget::update));
    hide();
}

QColor SimpleSheet::panelColor() {
    const auto colors = themeManager()->Colors();
    return blend(colors.window, colors.surfaceRaised, 0.5);
}

void SimpleSheet::open() {
    if (open_) return;
    open_ = true;
    setGeometry(parentWidget()->rect());
    // Measured after the stylesheet fonts apply, or wrapped text comes out short.
    ensurePolished();
    if (panel_->layout() != nullptr) panel_->layout()->invalidate();
    relayout();
    show();
    raise();
    animation_->stop();
    animation_->setDuration(220);
    animation_->setEasingCurve(QEasingCurve::OutCubic);
    animation_->setStartValue(progress_);
    animation_->setEndValue(1.0);
    animation_->start();
    focusTarget()->setFocus(Qt::PopupFocusReason);
}

void SimpleSheet::dismiss() {
    if (!open_) return;
    open_ = false;
    animation_->stop();
    animation_->setDuration(160);
    animation_->setEasingCurve(QEasingCurve::InCubic);
    animation_->setStartValue(progress_);
    animation_->setEndValue(0.0);
    animation_->start();
    if (auto *host = parentWidget()) host->setFocus(Qt::OtherFocusReason);
}

void SimpleSheet::setProgress(qreal progress) {
    progress_ = progress;
    relayout();
    update();
}

int SimpleSheet::preferredPanelHeight() const {
    return panel_->layout() != nullptr ? panel_->layout()->totalHeightForWidth(width()) : 240;
}

void SimpleSheet::relayout() {
    const int height = qMin(preferredPanelHeight(), qRound(this->height() * 0.82));
    panel_->setGeometry(0, this->height() - qRound(height * progress_), width(), height);
}

void SimpleSheet::paintEvent(QPaintEvent *) {
    QPainter painter(this);
    painter.fillRect(rect(), QColor(8, 9, 11, qRound(140 * progress_)));
}

void SimpleSheet::mousePressEvent(QMouseEvent *event) {
    if (!panel_->geometry().contains(event->position().toPoint())) dismiss();
    event->accept();
}

void SimpleSheet::keyPressEvent(QKeyEvent *event) {
    if (event->key() == Qt::Key_Escape) {
        dismiss();
        event->accept();
        return;
    }
    QWidget::keyPressEvent(event);
}

bool SimpleSheet::event(QEvent *event) {
    // The main window binds Return and Escape to its own actions; inside the sheet they belong to the sheet.
    if (event->type() == QEvent::ShortcutOverride) {
        const int key = static_cast<QKeyEvent *>(event)->key();
        if (key == Qt::Key_Escape || key == Qt::Key_Return || key == Qt::Key_Enter) {
            event->accept();
            return true;
        }
    }
    return QWidget::event(event);
}

bool SimpleSheet::eventFilter(QObject *watched, QEvent *event) {
    if (watched == parentWidget() && event->type() == QEvent::Resize && isVisible()) {
        setGeometry(parentWidget()->rect());
        relayout();
    }
    return QWidget::eventFilter(watched, event);
}

SimpleServerSheet::SimpleServerSheet(QWidget *host) : SimpleSheet(host) {
    setObjectName(QStringLiteral("simpleServerSheet"));
    auto *layout = new QVBoxLayout(panel());
    layout->setContentsMargins(16, 20, 16, 14);
    layout->setSpacing(10);

    auto *head = new QHBoxLayout;
    auto *title = new QLabel(tr("Server"), panel());
    title->setObjectName(QStringLiteral("simpleSheetTitle"));
    head->addWidget(title);
    head->addStretch();
    test_ = new QPushButton(tr("Check ping"), panel());
    test_->setObjectName(QStringLiteral("simpleSheetLink"));
    test_->setCursor(Qt::PointingHandCursor);
    connect(test_, &QPushButton::clicked, this, &SimpleServerSheet::testRequested);
    head->addWidget(test_);
    layout->addLayout(head);

    tabsArea_ = new QScrollArea(panel());
    tabsArea_->setFrameShape(QFrame::NoFrame);
    tabsArea_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    tabsArea_->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    tabsArea_->setWidgetResizable(true);
    tabsArea_->setStyleSheet(QStringLiteral("QScrollArea, QScrollArea > QWidget > QWidget { background: transparent; }"));
    tabsArea_->viewport()->installEventFilter(this);
    tabs_ = new QWidget(tabsArea_);
    tabsLayout_ = new QHBoxLayout(tabs_);
    tabsLayout_->setContentsMargins(0, 0, 0, 0);
    tabsLayout_->setSpacing(6);
    tabsLayout_->addStretch();
    tabsArea_->setWidget(tabs_);
    tabGroup_ = new QButtonGroup(this);
    connect(tabGroup_, &QButtonGroup::idClicked, this, [this](int id) {
        if (id == shownGroup_) return;
        shownGroup_ = id;
        emit groupShown(id);
    });
    layout->addWidget(tabsArea_);

    allowance_ = new QPushButton(panel());
    allowance_->setObjectName(QStringLiteral("simpleSheetAllowance"));
    allowance_->setCursor(Qt::PointingHandCursor);
    connect(allowance_, &QPushButton::clicked, this,
            [this] { emit allowanceClicked(allowance_->mapToGlobal(QPoint(0, allowance_->height()))); });
    layout->addWidget(allowance_);
    allowanceBar_ = new QProgressBar(panel());
    allowanceBar_->setObjectName(QStringLiteral("simpleSheetAllowanceBar"));
    allowanceBar_->setRange(0, 1000);
    allowanceBar_->setTextVisible(false);
    allowanceBar_->setFixedHeight(3);
    layout->addWidget(allowanceBar_);

    list_ = new QListView(panel());
    list_->setObjectName(QStringLiteral("simpleSheetList"));
    list_->setMouseTracking(true);
    list_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    list_->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    list_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    list_->setUniformItemSizes(true);
    list_->setItemDelegate(new ServerDelegate(list_));
    model_ = new QStandardItemModel(this);
    list_->setModel(model_);
    list_->installEventFilter(this);
    new ScrollEdgeFade(list_, &SimpleSheet::panelColor);
    connect(list_, &QListView::clicked, this, [this](const QModelIndex &index) {
        if (index.isValid()) emit serverChosen(index.data(kIdRole).toInt());
    });
    layout->addWidget(list_, 1);
    empty_ = new QLabel(tr("This group has no profiles yet"), panel());
    empty_->setObjectName(QStringLiteral("simpleSheetEmpty"));
    empty_->setAlignment(Qt::AlignCenter);
    empty_->setMinimumHeight(72);
    layout->addWidget(empty_);

    auto *actions = new QHBoxLayout;
    actions->setSpacing(8);
    for (const auto &[text, signal]: {std::pair{tr("From clipboard"), &SimpleServerSheet::addFromClipboard},
                                      std::pair{tr("From a link"), &SimpleServerSheet::addFromLink}}) {
        auto *button = new QPushButton(text, panel());
        button->setObjectName(QStringLiteral("simpleSheetAction"));
        button->setCursor(Qt::PointingHandCursor);
        const auto tint = [button] { button->setIcon(MaterialIcon::icon(MaterialIcon::Glyph::Add, themeManager()->Colors().textMuted, 16)); };
        tint();
        connect(themeManager(), &ThemeManager::themeChanged, button, tint);
        connect(button, &QPushButton::clicked, this, signal);
        actions->addWidget(button, 1);
    }
    layout->addLayout(actions);
}

void SimpleServerSheet::setGroups(const QList<QPair<int, QString>> &groups, int shownGroup) {
    shownGroup_ = shownGroup;
    for (auto *button: tabGroup_->buttons()) {
        tabGroup_->removeButton(button);
        button->deleteLater();
    }
    for (const auto &[id, name]: groups) {
        auto *tab = new QPushButton(name, tabs_);
        tab->setObjectName(QStringLiteral("simpleSheetTab"));
        tab->setCheckable(true);
        tab->setChecked(id == shownGroup);
        tab->setCursor(Qt::PointingHandCursor);
        tabGroup_->addButton(tab, id);
        tabsLayout_->insertWidget(tabsLayout_->count() - 1, tab);
    }
    tabsArea_->setVisible(groups.size() > 1);
    tabsArea_->setFixedHeight(tabs_->sizeHint().height());
    if (auto *checked = tabGroup_->checkedButton())
        QMetaObject::invokeMethod(this, [this, checked] { tabsArea_->ensureWidgetVisible(checked, 24, 0); }, Qt::QueuedConnection);
}

void SimpleServerSheet::setServers(const QList<SimpleServerEntry> &servers) {
    model_->clear();
    QModelIndex current;
    for (const auto &server: servers) {
        auto *item = new QStandardItem(server.name);
        item->setData(server.address, kAddressRole);
        item->setData(server.latency, kLatencyRole);
        item->setData(server.latencyText, kLatencyTextRole);
        item->setData(server.current, kCurrentRole);
        item->setData(server.id, kIdRole);
        item->setToolTip(server.name.toHtmlEscaped());
        item->setAccessibleText(server.name + QStringLiteral(", ") + server.latencyText);
        model_->appendRow(item);
        if (server.current) current = item->index();
    }
    list_->setVisible(!servers.isEmpty());
    empty_->setVisible(servers.isEmpty());
    if (current.isValid()) {
        list_->setCurrentIndex(current);
        list_->scrollTo(current, QAbstractItemView::PositionAtCenter);
    } else if (model_->rowCount() > 0) {
        list_->setCurrentIndex(model_->index(0, 0));
    }
    if (isOpen()) relayout();
}

void SimpleServerSheet::setAllowance(const QString &text, qreal fraction) {
    allowance_->setText(text);
    allowance_->setVisible(!text.isEmpty());
    allowanceBar_->setVisible(!text.isEmpty() && fraction >= 0);
    allowanceBar_->setValue(qRound(qBound(0.0, fraction, 1.0) * 1000));
}

void SimpleServerSheet::setTesting(bool testing, bool available) {
    test_->setText(testing ? tr("Testing…") : tr("Check ping"));
    test_->setEnabled(!testing && available);
}

int SimpleServerSheet::profileAt(int row) const {
    return model_->index(row, 0).data(kIdRole).toInt();
}

int SimpleServerSheet::preferredPanelHeight() const {
    auto *layout = panel()->layout();
    const QMargins margins = layout->contentsMargins();
    int height = margins.top() + margins.bottom();
    int visible = 0;
    for (int i = 0; i < layout->count(); ++i) {
        auto *item = layout->itemAt(i);
        if (item->widget() == list_) {
            if (!list_->isHidden()) {
                height += qMax(1, model_->rowCount()) * kServerRowHeight + 4;
                ++visible;
            }
            continue;
        }
        if (item->widget() != nullptr && item->widget()->isHidden()) continue;
        height += item->sizeHint().height();
        ++visible;
    }
    return height + qMax(0, visible - 1) * layout->spacing();
}

QWidget *SimpleServerSheet::focusTarget() const {
    return list_->isHidden() ? static_cast<QWidget *>(panel()) : list_;
}

bool SimpleServerSheet::eventFilter(QObject *watched, QEvent *event) {
    if (watched == list_ && event->type() == QEvent::KeyPress) {
        const auto key = static_cast<QKeyEvent *>(event)->key();
        if ((key == Qt::Key_Return || key == Qt::Key_Enter) && list_->currentIndex().isValid()) {
            emit serverChosen(list_->currentIndex().data(kIdRole).toInt());
            return true;
        }
    }
    if (watched == tabsArea_->viewport() && event->type() == QEvent::Wheel) {
        auto *wheel = static_cast<QWheelEvent *>(event);
        auto *bar = tabsArea_->horizontalScrollBar();
        bar->setValue(bar->value() - (wheel->angleDelta().y() + wheel->angleDelta().x()) / 2);
        return true;
    }
    return SimpleSheet::eventFilter(watched, event);
}

SimpleModeSheet::SimpleModeSheet(QWidget *host) : SimpleSheet(host) {
    setObjectName(QStringLiteral("simpleModeSheet"));
    auto *layout = new QVBoxLayout(panel());
    layout->setContentsMargins(16, 22, 16, 18);
    layout->setSpacing(10);
    auto *title = new QLabel(tr("What goes through the VPN"), panel());
    title->setObjectName(QStringLiteral("simpleSheetTitle"));
    title->setWordWrap(true);
    layout->addWidget(title);
    layout->addSpacing(2);

    whole_ = new SimpleModeOption(MaterialIcon::Glyph::Desktop, tr("Whole computer"), tr("Recommended"),
                                  tr("Everything that goes online: games, messengers, calls and the browser."), {}, panel());
    whole_->setObjectName(QStringLiteral("simpleModeWhole"));
    elevation_ = new WarningBox(whole_);
    auto *elevationLayout = new QVBoxLayout(elevation_);
    elevationLayout->setContentsMargins(10, 9, 10, 10);
    elevationLayout->setSpacing(8);
    elevationText_ = new QLabel(elevation_);
    elevationText_->setObjectName(QStringLiteral("simpleElevationText"));
    elevationText_->setWordWrap(true);
    elevationLayout->addWidget(elevationText_);
    elevationButton_ = new QPushButton(tr("Continue"), elevation_);
    elevationButton_->setObjectName(QStringLiteral("simpleElevationButton"));
    elevationButton_->setCursor(Qt::PointingHandCursor);
    elevationLayout->addWidget(elevationButton_, 0, Qt::AlignLeading);
    elevation_->hide();
    whole_->addBodyWidget(elevation_);
    connect(elevationButton_, &QPushButton::clicked, this, [this] { emit modeChosen(SimpleMode::WholeComputer, true); });
    connect(whole_, &QAbstractButton::clicked, this, [this] { choose(SimpleMode::WholeComputer); });
    layout->addWidget(whole_);

    browser_ = new SimpleModeOption(MaterialIcon::Glyph::Public, tr("Browser only"), {},
                                    tr("Chrome, Edge, Firefox and apps that use the system proxy settings."),
                                    tr("Games, voice calls and most other apps bypass the VPN. No administrator rights needed."),
                                    panel());
    browser_->setObjectName(QStringLiteral("simpleModeBrowser"));
    connect(browser_, &QAbstractButton::clicked, this, [this] { choose(SimpleMode::BrowserOnly); });
    layout->addWidget(browser_);
}

void SimpleModeSheet::setState(SimpleMode current, bool browserAvailable, const QString &elevationNote) {
    current_ = current;
    elevationNote_ = elevationNote;
    whole_->setSelected(current == SimpleMode::WholeComputer);
    browser_->setSelected(current == SimpleMode::BrowserOnly);
    browser_->setEnabled(browserAvailable);
    elevation_->hide();
    if (isOpen()) relayout();
}

void SimpleModeSheet::choose(SimpleMode mode) {
    if (mode == current_) {
        dismiss();
        return;
    }
    if (mode == SimpleMode::WholeComputer && !elevationNote_.isEmpty()) {
        elevationText_->setText(elevationNote_);
        elevation_->show();
        whole_->updateGeometry();
        relayout();
        elevationButton_->setFocus(Qt::OtherFocusReason);
        return;
    }
    emit modeChosen(mode, false);
}

int SimpleModeSheet::preferredPanelHeight() const {
    // Wrapped labels under-report their height for width here; the size hint is the honest floor.
    auto *layout = panel()->layout();
    return qMax(layout->totalHeightForWidth(width()), layout->totalSizeHint().height());
}

QWidget *SimpleModeSheet::focusTarget() const {
    return current_ == SimpleMode::BrowserOnly ? static_cast<QWidget *>(browser_) : whole_;
}
