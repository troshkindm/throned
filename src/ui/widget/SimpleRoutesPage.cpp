#include "include/ui/widget/SimpleRoutesPage.h"

#include <QAbstractButton>
#include <QApplication>
#include <QEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPainter>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QStyle>
#include <QTimer>
#include <QToolButton>
#include <QVariantAnimation>
#include <QStylePainter>
#include <QStyleOptionButton>
#include <QStyleOptionToolButton>
#include <QVBoxLayout>
#include <QtMath>

#include <functional>
#include <utility>

#include "include/database/entities/RouteRule.h"
#include "include/ui/setting/ThemeManager.hpp"
#include "include/ui/utils/ProfileRowDelegate.h"
#include "include/ui/widget/FlowLayout.h"
#include "include/ui/widget/MaterialIcon.h"
#include "include/ui/widget/SimpleSheet.h"
#include "include/ui/widget/ThronedToggle.h"

using Configs::AppRoutes::CatalogEntry;
using Configs::AppRoutes::Kind;
using Configs::AppRoutes::Route;

namespace {
const QString kCustomId = QString::fromLatin1(Configs::AppRoutes::CustomId);

QFont routeFont(const QFont &base, qreal factor, QFont::Weight weight = QFont::Normal) {
    QFont font = base;
    if (font.pixelSize() > 0)
        font.setPixelSize(qMax(9, qRound(font.pixelSize() * factor)));
    else
        font.setPointSizeF(qMax(7.0, font.pointSizeF() * factor));
    font.setWeight(weight);
    return font;
}

QColor windowColor() { return themeManager()->Colors().window; }

constexpr int kRuleSetKind = 100;

Route defaultsOf(const CatalogEntry *entry) {
    if (entry == nullptr) return {};
    return {.id = entry->id, .processes = entry->processes, .ruleSets = entry->ruleSets, .domains = entry->domains, .cidrs = entry->cidrs};
}

enum class Pill { None,
                  Vpn,
                  Profile,
                  Direct };

Pill pillFor(int outbound) {
    if (outbound == Configs::proxyID) return Pill::Vpn;
    if (outbound == Configs::directID) return Pill::Direct;
    return Pill::Profile;
}

void paintPill(QPainter &painter, const QRect &rect, Pill pill, const QString &text, const QFont &font) {
    const auto colors = themeManager()->Colors();
    painter.setPen(pill == Pill::Profile ? QPen(colors.accent, 1) : Qt::NoPen);
    painter.setBrush(pill == Pill::Vpn ? colors.accentSoft : pill == Pill::Direct ? colors.surfaceHover
                                                                                  : Qt::transparent);
    painter.drawRoundedRect(QRectF(rect).adjusted(.5, .5, -.5, -.5), 6, 6);
    painter.setFont(font);
    painter.setPen(pill == Pill::Direct ? colors.textMuted : colors.accent);
    painter.drawText(rect, Qt::AlignCenter, QFontMetrics(font).elidedText(text, Qt::ElideRight, rect.width() - 10));
}

// Divider above every row but the first, the way the main screen's grouped list draws it.
class RouteRow : public QWidget {
public:
    RouteRow(bool first, QWidget *parent) : QWidget(parent), first_(first) {}

protected:
    void paintEvent(QPaintEvent *) override {
        if (first_) return;
        QPainter painter(this);
        painter.setPen(QPen(themeManager()->Colors().border, 1));
        painter.drawLine(QPointF(0, .5), QPointF(width(), .5));
    }

private:
    bool first_;
};

class RouteHeader : public QAbstractButton {
public:
    struct Look {
        QString name;
        QString meta;
        QColor tile;
        QString letter;
        bool running = false;
        bool off = false;
        bool ghost = false;
        QIcon icon;
        Pill pill = Pill::None;
        QString pillText;
    };

    RouteHeader(Look look, QWidget *parent) : QAbstractButton(parent), look_(std::move(look)) {
        setCursor(Qt::PointingHandCursor);
        setAttribute(Qt::WA_Hover);
        setFocusPolicy(Qt::StrongFocus);
        setAccessibleName(look_.name);
        setAccessibleDescription(look_.meta);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    }
    QSize sizeHint() const override { return {240, 52}; }
    QSize minimumSizeHint() const override { return {160, 52}; }
    void setLook(Look look) {
        look_ = std::move(look);
        setAccessibleName(look_.name);
        setAccessibleDescription(look_.meta);
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override {
        const auto colors = themeManager()->Colors();
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const auto visual = [this](const QRect &r) { return QStyle::visualRect(layoutDirection(), rect(), r); };
        if ((underMouse() || hasFocus()) && isEnabled()) {
            painter.setPen(hasFocus() ? QPen(colors.borderStrong, 1) : Qt::NoPen);
            painter.setBrush(colors.surfaceHover);
            painter.drawRoundedRect(QRectF(rect()).adjusted(4.5, 4.5, -.5, -4.5), 9, 9);
        }
        const QRect tile = visual(QRect(12, (height() - 28) / 2, 28, 28));
        if (!look_.icon.isNull()) {
            // The program's own icon says more than a letter; a switched-off entry shows it greyed.
            look_.icon.paint(&painter, tile, Qt::AlignCenter, look_.off ? QIcon::Disabled : QIcon::Normal);
        } else {
            QColor tileColor = look_.ghost ? colors.window : look_.tile;
            if (look_.off && !look_.ghost) tileColor = colors.controlInactive;
            painter.setPen(look_.ghost ? QPen(colors.border, 1) : Qt::NoPen);
            painter.setBrush(tileColor);
            painter.drawRoundedRect(QRectF(tile).adjusted(.5, .5, -.5, -.5), 8, 8);
            if (look_.ghost) {
                painter.drawPixmap(tile.center() - QPoint(8, 8), MaterialIcon::pixmap(MaterialIcon::Glyph::Public, colors.textMuted, 16));
            } else {
                painter.setFont(routeFont(font(), 0.95, QFont::Bold));
                painter.setPen(look_.off ? colors.textSubtle : QColor(Qt::white));
                painter.drawText(tile, Qt::AlignCenter, look_.letter);
            }
        }

        const QFont pillFont = routeFont(font(), 0.85, QFont::DemiBold);
        const int pillWidth = look_.pill == Pill::None ? 0 : qMin(110, QFontMetrics(pillFont).horizontalAdvance(look_.pillText) + 16);
        if (pillWidth > 0) paintPill(painter, visual(QRect(width() - pillWidth - 4, (height() - 20) / 2, pillWidth, 20)), look_.pill, look_.pillText, pillFont);

        const int left = 50;
        const int textWidth = qMax(0, width() - left - pillWidth - 12);
        const QFont nameFont = routeFont(font(), 0.98, QFont::DemiBold);
        const QFont metaFont = routeFont(font(), 0.86);
        const QFontMetrics nameMetrics(nameFont), metaMetrics(metaFont);
        const int top = (height() - nameMetrics.height() - metaMetrics.height()) / 2;
        const QString name = nameMetrics.elidedText(look_.name, Qt::ElideRight, textWidth - (look_.running ? 12 : 0));
        painter.setFont(nameFont);
        painter.setPen(look_.off ? colors.textMuted : colors.text);
        painter.drawText(visual(QRect(left, top, textWidth, nameMetrics.height())), Qt::AlignLeading | Qt::AlignVCenter, name);
        if (look_.running) {
            const int x = left + nameMetrics.horizontalAdvance(name) + 7;
            const QRect dot = visual(QRect(x, top + nameMetrics.height() / 2 - 3, 6, 6));
            painter.setPen(Qt::NoPen);
            painter.setBrush(colors.success);
            painter.drawEllipse(dot);
        }
        painter.setFont(metaFont);
        painter.setPen(colors.textSubtle);
        painter.drawText(visual(QRect(left, top + nameMetrics.height(), textWidth, metaMetrics.height())), Qt::AlignLeading | Qt::AlignVCenter,
                         metaMetrics.elidedText(look_.meta, Qt::ElideRight, textWidth));
    }

private:
    Look look_;
};

class RouteActionButton : public QPushButton {
public:
    RouteActionButton(const QString &text, QWidget *parent) : QPushButton(text, parent) {
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        setToolTip(text);
    }
    QSize minimumSizeHint() const override {
        QSize size = QPushButton::minimumSizeHint();
        size.setWidth(qMin(80, size.width()));
        return size;
    }

protected:
    void paintEvent(QPaintEvent *) override {
        QStyleOptionButton option;
        initStyleOption(&option);
        const int width = style()->subElementRect(QStyle::SE_PushButtonContents, &option, this).width();
        QStringList lines = option.text.split(QLatin1Char('\n'));
        for (QString &line: lines) line = fontMetrics().elidedText(line, Qt::ElideRight, qMax(1, width));
        option.text = lines.join(QLatin1Char('\n'));
        QStylePainter painter(this);
        painter.drawControl(QStyle::CE_PushButton, option);
    }
};

class TargetPill : public QToolButton {
public:
    explicit TargetPill(QWidget *parent) : QToolButton(parent) { setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed); }
    QSize sizeHint() const override {
        QSize size = QToolButton::sizeHint();
        size.setWidth(qMin(132, size.width() + 12));
        return size;
    }
    QSize minimumSizeHint() const override {
        QSize size = sizeHint();
        size.setWidth(qMin(72, size.width()));
        return size;
    }

protected:
    void paintEvent(QPaintEvent *) override {
        QStyleOptionToolButton option;
        initStyleOption(&option);
        option.text = fontMetrics().elidedText(text(), Qt::ElideRight, qMax(1, width() - 28)) + QStringLiteral(" ▾");
        QStylePainter painter(this);
        painter.drawComplexControl(QStyle::CC_ToolButton, option);
    }
};

class TargetOption : public QAbstractButton {
public:
    TargetOption(const QString &text, const QString &latency, int latencyMs, bool selected, QWidget *parent)
        : QAbstractButton(parent), latency_(latency), latencyMs_(latencyMs), selected_(selected) {
        setText(text);
        setCursor(Qt::PointingHandCursor);
        setAttribute(Qt::WA_Hover);
        setFocusPolicy(Qt::StrongFocus);
        setAccessibleName(text);
    }
    QSize sizeHint() const override { return {200, fontMetrics().height() + 12}; }

protected:
    void paintEvent(QPaintEvent *) override {
        const auto colors = themeManager()->Colors();
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const auto visual = [this](const QRect &r) { return QStyle::visualRect(layoutDirection(), rect(), r); };
        if (selected_ || underMouse() || hasFocus()) {
            painter.setPen(hasFocus() ? QPen(colors.borderStrong, 1) : Qt::NoPen);
            painter.setBrush(selected_ ? colors.accentSoft : colors.surfaceHover);
            painter.drawRoundedRect(QRectF(rect()).adjusted(.5, .5, -.5, -.5), 8, 8);
        }
        const QRect radio = visual(QRect(8, (height() - 14) / 2, 14, 14));
        painter.setBrush(Qt::NoBrush);
        painter.setPen(QPen(selected_ ? colors.accent : colors.borderStrong, selected_ ? 4 : 1.5));
        painter.drawEllipse(QRectF(radio).adjusted(selected_ ? 2 : .75, selected_ ? 2 : .75, selected_ ? -2 : -.75, selected_ ? -2 : -.75));
        const QFont small = routeFont(font(), 0.9);
        const int latencyWidth = latency_.isEmpty() ? 0 : QFontMetrics(small).horizontalAdvance(latency_) + 8;
        painter.setFont(small);
        if (latencyWidth > 0) {
            painter.setPen(ProfileRowDelegate::latencyColor(latencyMs_, colors));
            painter.drawText(visual(QRect(width() - latencyWidth - 8, 0, latencyWidth, height())), Qt::AlignVCenter | Qt::AlignTrailing, latency_);
        }
        painter.setFont(routeFont(font(), 0.94));
        painter.setPen(selected_ ? colors.text : colors.textMuted);
        const int width = this->width() - 30 - latencyWidth - 12;
        painter.drawText(visual(QRect(30, 0, width, height())), Qt::AlignVCenter | Qt::AlignLeading,
                         QFontMetrics(painter.font()).elidedText(text(), Qt::ElideRight, width));
    }

private:
    QString latency_;
    int latencyMs_;
    bool selected_;
};

class WarningFrame : public QFrame {
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
        painter.drawRoundedRect(QRectF(rect()).adjusted(.5, .5, -.5, -.5), 12, 12);
    }
};

} // namespace

// The current group's title pinned above the list while its rows scroll by; the next group pushes it out.
class StickyHeader : public QWidget {
public:
    explicit StickyHeader(QWidget *parent) : QWidget(parent) {
        setAttribute(Qt::WA_TransparentForMouseEvents);
        auto *layout = new QHBoxLayout(this);
        layout->setContentsMargins(2, 0, 2, 6);
        title_ = new QLabel(this);
        title_->setObjectName(QStringLiteral("simpleRouteGroup"));
        layout->addWidget(title_, 1);
        count_ = new QLabel(this);
        count_->setObjectName(QStringLiteral("simpleRouteGroup"));
        layout->addWidget(count_);
        hide();
    }
    void setTexts(const QString &title, const QString &count) {
        title_->setText(title);
        count_->setText(count);
    }

protected:
    void paintEvent(QPaintEvent *) override {
        QPainter painter(this);
        painter.fillRect(rect(), themeManager()->Colors().window);
    }

private:
    QLabel *title_;
    QLabel *count_;
};

namespace {
constexpr int kRevealMs = 180;

// Hidden and orphaned first: a deferred delete would still be found, laid out and focused otherwise.
void retire(QWidget *widget) {
    widget->hide();
    widget->setParent(nullptr);
    widget->deleteLater();
}
} // namespace

// A detail card's clip: it grows from nothing to the card's height, so the rows below move instead of jumping.
class RouteReveal : public QWidget {
public:
    explicit RouteReveal(QWidget *parent) : QWidget(parent), animation_(new QVariantAnimation(this)) {
        animation_->setEasingCurve(QEasingCurve::OutCubic);
        connect(animation_, &QVariantAnimation::valueChanged, this, [this](const QVariant &value) { setReveal(value.toReal()); });
        connect(animation_, &QVariantAnimation::finished, this, [this] {
            if (const auto done = std::exchange(done_, {})) done();
        });
    }
    [[nodiscard]] QWidget *content() const { return content_; }
    void setContent(QWidget *content) {
        if (content_ != nullptr) retire(content_);
        content_ = content;
        content->setParent(this);
        content->setGeometry(0, 0, width(), fullHeight(width()));
        content->show();
        updateGeometry();
    }
    // Reverses from wherever it is, so a second click mid-way turns it around instead of restarting.
    void animateTo(qreal target, bool animated, std::function<void()> done = {}) {
        animation_->stop();
        done_ = std::move(done);
        if (!animated || qAbs(reveal_ - target) < 0.001) {
            setReveal(target);
            if (const auto finished = std::exchange(done_, {})) finished();
            return;
        }
        animation_->setStartValue(reveal_);
        animation_->setEndValue(target);
        animation_->setDuration(qMax(1, qRound(kRevealMs * qAbs(target - reveal_))));
        animation_->start();
    }
    bool hasHeightForWidth() const override { return true; }
    int heightForWidth(int width) const override { return qCeil(fullHeight(width) * reveal_); }
    QSize sizeHint() const override {
        const int hint = content_ != nullptr ? content_->sizeHint().width() : 0;
        return {hint, heightForWidth(width() > 0 ? width() : hint)};
    }
    QSize minimumSizeHint() const override { return {content_ != nullptr ? content_->minimumSizeHint().width() : 0, sizeHint().height()}; }

protected:
    void resizeEvent(QResizeEvent *) override { placeContent(); }
    bool event(QEvent *event) override {
        // The card is not in a layout of ours, so its size changes arrive here as a request.
        if (event->type() == QEvent::LayoutRequest) {
            updateGeometry();
            placeContent();
        }
        return QWidget::event(event);
    }

private:
    [[nodiscard]] int fullHeight(int width) const {
        if (content_ == nullptr) return 0;
        return content_->hasHeightForWidth() ? content_->heightForWidth(width) : content_->sizeHint().height();
    }
    void placeContent() {
        if (content_ != nullptr) content_->setGeometry(0, 0, width(), fullHeight(width()));
    }
    void setReveal(qreal value) {
        reveal_ = value;
        updateGeometry();
    }

    QVariantAnimation *animation_;
    QWidget *content_ = nullptr;
    qreal reveal_ = 0;
    std::function<void()> done_;
};

namespace {
const QString kRoutesStyle = QStringLiteral(R"(
QLabel#simpleRoutesTitle { font-size: 16px; font-weight: 600; color: #F1F3F5; background: transparent; }
QLabel#simpleRoutesProfile { color: #A4ABB4; font-size: 12px; background: transparent; }
QToolButton#simpleRoutesHeaderButton, QToolButton#simpleRoutesBack { border: none; border-radius: 8px; background: transparent; }
QToolButton#simpleRoutesHeaderButton:hover, QToolButton#simpleRoutesBack:hover { background: #292D33; }
QLineEdit#simpleRoutesSearch { background: #171B21; border: 1px solid #2F3136; border-radius: 10px; padding: 6px 10px; color: #F1F3F5; }
QLineEdit#simpleRoutesSearch:focus { border-color: #237AE9; }
QScrollArea#simpleRoutesScroll, QWidget#simpleRoutesContent { background: transparent; border: none; }
QScrollArea#simpleRoutesScroll QScrollBar:vertical { width: 6px; background: transparent; margin: 0; }
QScrollArea#simpleRoutesScroll QScrollBar::handle:vertical { background: #3A424D; border-radius: 3px; min-height: 28px; }
QScrollArea#simpleRoutesScroll QScrollBar::handle:vertical:hover { background: #4B5663; }
QScrollArea#simpleRoutesScroll QScrollBar::add-line:vertical, QScrollArea#simpleRoutesScroll QScrollBar::sub-line:vertical { height: 0; }
QScrollArea#simpleRoutesScroll QScrollBar::add-page:vertical, QScrollArea#simpleRoutesScroll QScrollBar::sub-page:vertical { background: transparent; }
QFrame#simpleRouteCard { background: #222529; border: none; border-radius: 12px; }
QLabel#simpleRouteGroup, QLabel#simpleRouteCaption { color: #747C86; font-size: 11px; font-weight: 600; background: transparent; }
QFrame#simpleRouteTargets { background: #171B21; border: none; border-radius: 10px; }
QFrame#simpleRouteChip { background: #171B21; border: none; border-radius: 8px; }
QLabel#simpleRouteChipText { color: #A4ABB4; font-size: 12px; background: transparent; }
QToolButton#simpleRouteChipRemove { border: none; background: transparent; color: #747C86; padding: 0 3px; }
QToolButton#simpleRouteChipRemove:hover { color: #F1F3F5; }
QLineEdit#simpleRouteAdd { background: transparent; border: 1px dashed #3E454F; border-radius: 7px; padding: 5px 8px; color: #F1F3F5; font-size: 12px; }
QLineEdit#simpleRouteAdd:focus { border: 1px solid #237AE9; }
QPushButton#simpleRouteLink { border: none; background: transparent; color: #A4ABB4; padding: 0; font-size: 12px; }
QPushButton#simpleRouteLink:hover { color: #F1F3F5; }
QPushButton#simpleRouteMore { border: 1px dashed #3E454F; border-radius: 12px; background: transparent; color: #A4ABB4; padding: 10px 14px; text-align: left; }
QPushButton#simpleRouteMore:hover { border-style: solid; color: #F1F3F5; background: #222529; }
QPushButton#simpleRouteAddRow { border: 1px solid #237AE9; border-radius: 12px; background: #182530; color: #F1F3F5; padding: 10px 12px; text-align: left; }
QLabel#simpleRouteNote, QLabel#simpleRouteSentence, QLabel#simpleRouteDownload { color: #A4ABB4; background: transparent; }
QLabel#simpleRouteDownload[failed="true"] { color: #D9A441; }
QProgressBar#simpleRouteDownloadBar { border: none; border-radius: 1px; background: #2F3136; }
QProgressBar#simpleRouteDownloadBar::chunk { border-radius: 1px; background: #237AE9; }
QLabel#simpleRouteBannerText { color: #F1F3F5; background: transparent; }
QPushButton#simpleRouteBannerButton { border: none; border-radius: 7px; background: #D9A441; color: #1B1E23; font-weight: 600; padding: 5px 10px; }
QToolButton#simpleRoutePill { border: none; border-radius: 6px; padding: 2px 8px; font-weight: 600; font-size: 12px; }
QToolButton#simpleRoutePill[route="vpn"] { background: #182530; color: #237AE9; }
QToolButton#simpleRoutePill[route="direct"] { background: #292D33; color: #A4ABB4; }
QToolButton#simpleRoutePill::menu-indicator { image: none; width: 0; }
QPushButton#simpleRouteApply { border: none; border-radius: 10px; background: #237AE9; color: white; font-weight: 600; padding: 9px; }
QPushButton#simpleRouteApply:disabled { background: transparent; border: 1px solid #2F3136; color: #747C86; }
QFrame#simpleRoutesFooter { background: transparent; border: none; border-top: 1px solid #2F3136; }
)");
} // namespace

SimpleRoutesPage::SimpleRoutesPage(QWidget *parent) : QWidget(parent), catalog_(Configs::AppRoutes::Catalog()) {
    setObjectName(QStringLiteral("simpleRoutes"));
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(10);

    auto *head = new QHBoxLayout;
    head->setSpacing(8);
    const auto headerButton = [this](MaterialIcon::Glyph glyph, const QString &tip, bool mirror) {
        auto *button = new QToolButton(this);
        button->setObjectName(QStringLiteral("simpleRoutesHeaderButton"));
        button->setFixedSize(32, 32);
        button->setToolTip(tip);
        button->setAccessibleName(tip);
        button->setCursor(Qt::PointingHandCursor);
        const auto tint = [button, glyph, mirror] {
            QPixmap pixmap = MaterialIcon::pixmap(glyph, themeManager()->Colors().textMuted, 18);
            // The only chevron glyph points forward; back is its mirror, which a right-to-left layout mirrors again.
            if (mirror != (button->layoutDirection() == Qt::RightToLeft)) pixmap = pixmap.transformed(QTransform().scale(-1, 1));
            button->setIcon(QIcon(pixmap));
        };
        tint();
        connect(themeManager(), &ThemeManager::themeChanged, button, tint);
        return button;
    };
    backButton_ = headerButton(MaterialIcon::Glyph::ChevronRight, tr("Back"), true);
    backButton_->setObjectName(QStringLiteral("simpleRoutesBack"));
    connect(backButton_, &QToolButton::clicked, this, &SimpleRoutesPage::back);
    head->addWidget(backButton_);
    auto *titles = new QVBoxLayout;
    titles->setSpacing(1);
    title_ = new QLabel(tr("Routing"), this);
    title_->setObjectName(QStringLiteral("simpleRoutesTitle"));
    titles->addWidget(title_);
    profileName_ = new QLabel(this);
    profileName_->setObjectName(QStringLiteral("simpleRoutesProfile"));
    profileName_->setTextFormat(Qt::PlainText);
    profileName_->hide();
    titles->addWidget(profileName_);
    head->addLayout(titles, 1);
    auto *advanced = headerButton(MaterialIcon::Glyph::Tune, tr("Advanced editor"), false);
    connect(advanced, &QToolButton::clicked, this, &SimpleRoutesPage::openAdvanced);
    head->addWidget(advanced, 0, Qt::AlignVCenter);
    layout->addLayout(head);

    search_ = new QLineEdit(this);
    search_->setObjectName(QStringLiteral("simpleRoutesSearch"));
    search_->setPlaceholderText(tr("App, site, .exe or IP"));
    search_->setClearButtonEnabled(true);
    const auto tintSearch = [this] {
        search_->removeAction(search_->actions().value(0));
        search_->addAction(MaterialIcon::icon(MaterialIcon::Glyph::Search, themeManager()->Colors().textSubtle, 16), QLineEdit::LeadingPosition);
    };
    tintSearch();
    connect(themeManager(), &ThemeManager::themeChanged, search_, tintSearch);
    connect(search_, &QLineEdit::textChanged, this, [this] {
        scroll_->verticalScrollBar()->setValue(0);
        refresh();
    });
    connect(search_, &QLineEdit::returnPressed, this, [this] {
        if (Configs::AppRoutes::Classify(search_->text()).first == Kind::Invalid) return;
        if (addValue(kCustomId, search_->text())) search_->clear();
    });
    layout->addWidget(search_);

    scroll_ = new QScrollArea(this);
    scroll_->setObjectName(QStringLiteral("simpleRoutesScroll"));
    scroll_->setWidgetResizable(true);
    scroll_->setFrameShape(QFrame::NoFrame);
    scroll_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    // One content widget for the page's life: replacing it reset the scroll range and flashed an empty frame.
    content_ = new QWidget;
    content_->setObjectName(QStringLiteral("simpleRoutesContent"));
    auto *contentLayout = new QVBoxLayout(content_);
    contentLayout->setContentsMargins(0, 0, 8, 12);
    contentLayout->setSpacing(14);
    scroll_->setWidget(content_);
    fade_ = new ScrollEdgeFade(scroll_, &windowColor);
    sticky_ = new StickyHeader(scroll_);
    connect(scroll_->verticalScrollBar(), &QScrollBar::valueChanged, this, &SimpleRoutesPage::updateSticky);
    connect(scroll_->verticalScrollBar(), &QScrollBar::rangeChanged, this, &SimpleRoutesPage::updateSticky);
    layout->addWidget(scroll_, 1);

    auto *footer = new QFrame(this);
    footer->setObjectName(QStringLiteral("simpleRoutesFooter"));
    auto *footerLayout = new QVBoxLayout(footer);
    footerLayout->setContentsMargins(0, 10, 0, 0);
    footerLayout->setSpacing(8);
    sentence_ = new QLabel(footer);
    sentence_->setObjectName(QStringLiteral("simpleRouteSentence"));
    sentence_->setWordWrap(true);
    sentence_->setTextFormat(Qt::RichText);
    // Two lines at most, so the footer never pushes the list off a short window.
    sentence_->setFixedHeight(sentence_->fontMetrics().lineSpacing() * 2 + 2);
    footerLayout->addWidget(sentence_);
    download_ = new QWidget(footer);
    auto *downloadLayout = new QVBoxLayout(download_);
    downloadLayout->setContentsMargins(0, 0, 0, 0);
    downloadLayout->setSpacing(5);
    auto *downloadLine = new QHBoxLayout;
    downloadText_ = new QLabel(download_);
    downloadText_->setObjectName(QStringLiteral("simpleRouteDownload"));
    downloadText_->setWordWrap(true);
    downloadLine->addWidget(downloadText_, 1);
    downloadRetry_ = new QPushButton(tr("Retry"), download_);
    downloadRetry_->setObjectName(QStringLiteral("simpleRouteLink"));
    downloadRetry_->setCursor(Qt::PointingHandCursor);
    connect(downloadRetry_, &QPushButton::clicked, this, &SimpleRoutesPage::retryDownload);
    downloadLine->addWidget(downloadRetry_, 0, Qt::AlignTop);
    downloadLayout->addLayout(downloadLine);
    downloadBar_ = new QProgressBar(download_);
    downloadBar_->setObjectName(QStringLiteral("simpleRouteDownloadBar"));
    downloadBar_->setTextVisible(false);
    downloadBar_->setFixedHeight(3);
    downloadLayout->addWidget(downloadBar_);
    download_->hide();
    footerLayout->addWidget(download_);
    apply_ = new QPushButton(footer);
    apply_->setObjectName(QStringLiteral("simpleRouteApply"));
    apply_->setCursor(Qt::PointingHandCursor);
    connect(apply_, &QPushButton::clicked, this, [this] { emit applyRequested(routes_, rest_); });
    footerLayout->addWidget(apply_);
    layout->addWidget(footer);

    themeManager()->RegisterStyle(this, kRoutesStyle);
    connect(themeManager(), &ThemeManager::themeChanged, this, [this] {
        built_ = false;
        refresh();
    });
    rest_ = loadedRest_ = handedRest_ = Configs::directID;
    refresh();
}

QWidget *SimpleRoutesPage::scrollArea() const { return scroll_; }

void SimpleRoutesPage::load(const QList<Route> &routes, int restOutbound, int foreignRules, bool editable, bool rulesEnabled, bool resetView) {
    if (!resetView && !isDirty() && loaded_ == routes && loadedRest_ == restOutbound && foreign_ == foreignRules &&
        editable_ == editable && rulesEnabled_ == rulesEnabled) return;
    loaded_ = routes_ = handed_ = routes;
    loadedRest_ = rest_ = handedRest_ = restOutbound;
    foreign_ = foreignRules;
    editable_ = editable;
    rulesEnabled_ = rulesEnabled;
    open_.clear();
    // Another profile, or the same one changed elsewhere: nothing on screen is worth animating away.
    built_ = false;
    refresh(false);
}

void SimpleRoutesPage::setDraft(const QList<Route> &routes, int restOutbound) {
    routes_ = routes;
    rest_ = restOutbound;
    refresh();
}

void SimpleRoutesPage::markApplied(const QList<Route> &routes, int restOutbound) {
    loaded_ = routes;
    loadedRest_ = restOutbound;
    rulesEnabled_ = true;
    renderFooter();
}

void SimpleRoutesPage::setTargets(const SimpleRouteTarget &main, const QList<SimpleRouteTarget> &others) {
    // Called on every status refresh; an unchanged list must not rebuild the open card under the cursor.
    if (main_ == main && others_ == others) return;
    main_ = main;
    others_ = others;
    refresh();
}

void SimpleRoutesPage::setDetected(const QHash<QString, bool> &runningById) {
    if (detected_ == runningById) return;
    detected_ = runningById;
    refresh();
}

void SimpleRoutesPage::setIcon(const QString &id, const QIcon &icon) {
    icons_.insert(id, icon);
    // Icons arrive one by one while the list is on screen; each repaints its own row only.
    if (rows_.contains(id)) updateHeader(id);
}

void SimpleRoutesPage::setWholeComputer(bool wholeComputer) {
    if (wholeComputer_ == wholeComputer) return;
    wholeComputer_ = wholeComputer;
    refresh();
}

void SimpleRoutesPage::setConnected(bool connected) {
    if (connected_ == connected) return;
    connected_ = connected;
    renderFooter();
}

void SimpleRoutesPage::setEmbedded(bool embedded) {
    embedded_ = embedded;
    backButton_->setVisible(!embedded);
    apply_->setVisible(!embedded);
    title_->setText(embedded ? tr("Apps and sites") : tr("Routing"));
    profileName_->setVisible(embedded && !profileName_->text().isEmpty());
    built_ = false;
    refresh();
}

void SimpleRoutesPage::setProfileName(const QString &name) {
    profileName_->setText(name.isEmpty() ? QString() : tr("Profile: %1").arg(name));
    profileName_->setVisible(embedded_ && !name.isEmpty());
}

void SimpleRoutesPage::addEntries(const QStringList &lines) {
    for (const QString &line: lines) addValue(kCustomId, line);
}

bool SimpleRoutesPage::isDirty() const { return editable_ && (!rulesEnabled_ || routes_ != loaded_ || rest_ != loadedRest_); }

QStringList SimpleRoutesPage::neededRuleSets() const {
    QStringList names;
    for (const auto &item: routes_)
        for (const QString &name: item.ruleSets)
            if (!names.contains(name)) names.append(name);
    return names;
}

void SimpleRoutesPage::setDownload(int done, int total, const QString &error) {
    downloading_ = total > 0 && done < total && error.isEmpty();
    download_->setVisible(total > 0 || !error.isEmpty());
    downloadRetry_->setVisible(!error.isEmpty());
    downloadBar_->setVisible(error.isEmpty());
    downloadBar_->setRange(0, qMax(1, total));
    downloadBar_->setValue(done);
    downloadText_->setProperty("failed", !error.isEmpty());
    downloadText_->style()->unpolish(downloadText_);
    downloadText_->style()->polish(downloadText_);
    downloadText_->setText(!error.isEmpty() ? tr("Could not download the site lists: %1").arg(error)
                           : done < total   ? tr("Downloading site lists: %1 of %2").arg(done).arg(total)
                                            : tr("Site lists are ready"));
    renderFooter();
}

void SimpleRoutesPage::updateSticky() {
    const int value = scroll_->verticalScrollBar()->value();
    const QRect viewport = scroll_->viewport()->geometry();
    const int height = sticky_->sizeHint().height();
    int index = -1;
    for (int i = 0; i < sections_.size(); ++i)
        if (sections_.at(i).widget->y() < value) index = i;
    // Pinned only while its own title has scrolled away and some of its rows are still in view.
    if (index < 0 || sections_.at(index).widget->y() + sections_.at(index).widget->height() <= value) {
        sticky_->hide();
        fade_->setTopInset(0);
        return;
    }
    int offset = 0;
    if (index + 1 < sections_.size()) offset = qMin(0, sections_.at(index + 1).widget->y() - value - height);
    sticky_->setTexts(sections_.at(index).title, sections_.at(index).count);
    sticky_->setGeometry(viewport.x(), viewport.y() + offset, viewport.width() - 8, height);
    sticky_->show();
    sticky_->raise();
    fade_->setTopInset(qMax(0, height + offset));
}

QString SimpleRoutesPage::categoryName(const QString &category) const {
    if (category == QStringLiteral("messaging")) return tr("Messengers");
    if (category == QStringLiteral("social")) return tr("Social networks");
    if (category == QStringLiteral("ai")) return tr("AI");
    if (category == QStringLiteral("dev")) return tr("Development");
    if (category == QStringLiteral("video")) return tr("Video");
    if (category == QStringLiteral("music")) return tr("Music");
    if (category == QStringLiteral("gaming")) return tr("Games");
    if (category == QStringLiteral("cloud")) return tr("Cloud storage");
    if (category == QStringLiteral("work")) return tr("Work");
    return tr("Other");
}

Route *SimpleRoutesPage::route(const QString &id) {
    for (auto &item: routes_)
        if (item.id == id) return &item;
    return nullptr;
}

const Route *SimpleRoutesPage::routeOf(const QString &id) const {
    for (const auto &item: routes_)
        if (item.id == id) return &item;
    return nullptr;
}

const CatalogEntry *SimpleRoutesPage::entry(const QString &id) const {
    for (const auto &item: catalog_)
        if (item.id == id) return &item;
    return nullptr;
}

int SimpleRoutesPage::defaultOutbound() const {
    return rest_ == Configs::directID ? Configs::proxyID : Configs::directID;
}

QString SimpleRoutesPage::targetName(int outbound) const {
    if (outbound == Configs::proxyID) return tr("VPN");
    if (outbound == Configs::directID) return tr("Direct");
    if (outbound == Configs::blockID) return tr("Block");
    if (outbound == Configs::warpBypassID) return tr("WARP bypass");
    for (const auto &target: others_)
        if (target.outbound == outbound) return target.name;
    return tr("Missing server");
}

QString SimpleRoutesPage::detailKey(const QString &id) const {
    QStringList key;
    if (const Route *current = routeOf(id)) {
        key << QString::number(current->outbound) << current->processes.join(QLatin1Char('\n')) << current->processPaths.join(QLatin1Char('\n'))
            << current->ruleSets.join(QLatin1Char('\n')) << current->domains.join(QLatin1Char('\n')) << current->cidrs.join(QLatin1Char('\n'));
    } else {
        key << QStringLiteral("off");
    }
    key << QString::number(rest_) << main_.name << main_.latencyText << (isModified(id) ? QStringLiteral("edited") : QString());
    for (const auto &target: others_) key << QString::number(target.outbound) + target.name + target.latencyText;
    return key.join(QChar(0x1f));
}

void SimpleRoutesPage::setRouteEnabled(const QString &id, bool enabled) {
    if (!enabled) {
        routes_.removeIf([&id](const Route &item) { return item.id == id; });
    } else if (route(id) == nullptr) {
        if (const auto *catalogEntry = entry(id)) {
            Route added = defaultsOf(catalogEntry);
            added.outbound = defaultOutbound();
            routes_.append(added);
        }
        open_ = id;
    }
    refresh();
}

void SimpleRoutesPage::setRest(int outbound) {
    const int before = defaultOutbound();
    rest_ = outbound;
    // Entries that followed the default flip with it; one sent to a named server keeps that server.
    for (auto &item: routes_)
        if (item.outbound == before) item.outbound = defaultOutbound();
    refresh();
}

bool SimpleRoutesPage::addValue(const QString &id, const QString &text) {
    const auto [kind, value] = Configs::AppRoutes::Classify(text);
    if (kind == Kind::Invalid) return false;
    if (route(id) == nullptr) {
        if (id == kCustomId)
            routes_.append({.id = kCustomId, .outbound = defaultOutbound()});
        else
            setRouteEnabled(id, true);
    }
    Route *target = route(id);
    if (target == nullptr) return false;
    QStringList &list = kind == Kind::Process ? target->processes : kind == Kind::ProcessPath ? target->processPaths
                                                                : kind == Kind::Domain        ? target->domains
                                                                                              : target->cidrs;
    if (!list.contains(value, Qt::CaseInsensitive)) list.append(value);
    open_ = id;
    focusAdd_ = id;
    refresh();
    return true;
}

void SimpleRoutesPage::removeValue(const QString &id, int kind, const QString &value) {
    Route *target = route(id);
    if (target == nullptr) return;
    (kind == kRuleSetKind ? target->ruleSets : kind == int(Kind::Process)   ? target->processes
                                           : kind == int(Kind::ProcessPath) ? target->processPaths
                                           : kind == int(Kind::Domain)      ? target->domains
                                                                            : target->cidrs)
        .removeAll(value);
    if (target->isEmpty()) routes_.removeIf([&id](const Route &item) { return item.id == id; });
    refresh();
}

bool SimpleRoutesPage::isModified(const QString &id) const {
    const auto *catalogEntry = entry(id);
    const Route *current = routeOf(id);
    if (catalogEntry == nullptr || current == nullptr) return false;
    Route defaults = defaultsOf(catalogEntry);
    defaults.outbound = current->outbound;
    return !(defaults == *current);
}

QWidget *SimpleRoutesPage::makeSection(const QString &title, const QString &count) {
    auto *section = new QWidget(content_);
    auto *layout = new QVBoxLayout(section);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(6);
    auto *head = new QHBoxLayout;
    head->setContentsMargins(2, 0, 2, 0);
    sections_.append({section, title.toUpper(), count});
    auto *label = new QLabel(title.toUpper(), section);
    label->setObjectName(QStringLiteral("simpleRouteGroup"));
    head->addWidget(label, 1);
    auto *countLabel = new QLabel(count, section);
    countLabel->setObjectName(QStringLiteral("simpleRouteGroup"));
    head->addWidget(countLabel);
    layout->addLayout(head);
    auto *card = new QFrame(section);
    card->setObjectName(QStringLiteral("simpleRouteCard"));
    auto *cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(0, 0, 0, 0);
    cardLayout->setSpacing(0);
    layout->addWidget(card);
    currentCard_ = card;
    return section;
}

void SimpleRoutesPage::updateHeader(const QString &id) {
    auto *header = static_cast<RouteHeader *>(rows_.value(id).header);
    if (header == nullptr) return;
    const bool custom = id == kCustomId;
    const auto *catalogEntry = entry(id);
    const Route *current = routeOf(id);
    RouteHeader::Look look;
    look.name = custom ? tr("My sites and apps") : catalogEntry->name;
    look.tile = custom ? themeManager()->Colors().borderStrong : QColor(catalogEntry->color);
    look.letter = custom ? QStringLiteral("+") : look.name.left(1);
    look.running = detected_.value(id, false);
    look.icon = icons_.value(id);
    look.off = current == nullptr;
    const Route contents = current != nullptr ? *current : defaultsOf(catalogEntry);
    QStringList meta;
    if (isModified(id)) meta << tr("edited");
    if (!contents.processes.isEmpty() || !contents.processPaths.isEmpty())
        meta << tr("%n program(s)", nullptr, contents.processes.size() + contents.processPaths.size());
    if (std::any_of(contents.ruleSets.begin(), contents.ruleSets.end(), [](const QString &n) { return n.startsWith(QStringLiteral("geosite-")); }))
        meta << tr("sites list");
    if (std::any_of(contents.ruleSets.begin(), contents.ruleSets.end(), [](const QString &n) { return n.startsWith(QStringLiteral("geoip-")); }))
        meta << tr("IP networks");
    if (!contents.domains.isEmpty()) meta << tr("%n site(s)", nullptr, contents.domains.size());
    if (!contents.cidrs.isEmpty()) meta << tr("%n IP range(s)", nullptr, contents.cidrs.size());
    look.meta = meta.isEmpty() ? tr("Nothing yet") : meta.join(QStringLiteral(" · "));
    if (current != nullptr) {
        look.pill = pillFor(current->outbound);
        look.pillText = targetName(current->outbound);
    }
    header->setLook(std::move(look));
}

void SimpleRoutesPage::makeRow(const QString &id, bool animateOpen) {
    const bool custom = id == kCustomId;
    auto *card = currentCard_;
    auto *row = new RouteRow(card->layout()->count() == 0, card);
    auto *rowLayout = new QVBoxLayout(row);
    rowLayout->setContentsMargins(0, 0, 0, 0);
    rowLayout->setSpacing(0);
    auto *line = new QHBoxLayout;
    line->setContentsMargins(0, 0, custom ? 4 : 12, 0);
    line->setSpacing(8);
    auto *header = new RouteHeader({}, row);
    header->setObjectName(QStringLiteral("simpleRouteRow_") + (custom ? kCustomId : id));
    connect(header, &QAbstractButton::clicked, this, [this, id] {
        open_ = open_ == id ? QString() : id;
        refresh();
    });
    line->addWidget(header, 1);
    RowParts &parts = rows_[id];
    parts.row = row;
    parts.header = header;
    if (!custom) {
        auto *toggle = new ThronedToggle(routeOf(id) != nullptr, row);
        toggle->setObjectName(QStringLiteral("simpleRouteToggle_") + id);
        toggle->setAccessibleName(entry(id)->name);
        connect(toggle, &QAbstractButton::toggled, this, [this, id](bool on) {
            // Deferred: switching an entry off can drop its row, and a widget must not delete itself mid-signal.
            QTimer::singleShot(0, this, [this, id, on] { setRouteEnabled(id, on); });
        });
        line->addWidget(toggle, 0, Qt::AlignVCenter);
        parts.toggle = toggle;
    }
    rowLayout->addLayout(line);
    updateHeader(id);
    if (open_ == id) openDetail(id, animateOpen);
    card->layout()->addWidget(row);
}

void SimpleRoutesPage::openDetail(const QString &id, bool animate) {
    RowParts &parts = rows_[id];
    auto *reveal = new RouteReveal(parts.row);
    reveal->setContent(makeDetail(id, reveal));
    parts.row->layout()->addWidget(reveal);
    reveal->show();
    parts.reveal = reveal;
    parts.detailKey = detailKey(id);
    parts.closing = false;
    reveal->animateTo(1, animate && animated_);
}

QWidget *SimpleRoutesPage::makeDetail(const QString &id, QWidget *parent) {
    const Route *current = routeOf(id);
    const auto *catalogEntry = entry(id);
    // Parented at once: the chips measure their text in the page's style sheet font.
    auto *detail = new QWidget(parent);
    auto *layout = new QVBoxLayout(detail);
    layout->setContentsMargins(50, 0, 12, 12);
    layout->setSpacing(8);

    if (current != nullptr) {
        auto *caption = new QLabel(tr("Send through").toUpper(), detail);
        caption->setObjectName(QStringLiteral("simpleRouteCaption"));
        layout->addWidget(caption);
        auto *targets = new QFrame(detail);
        targets->setObjectName(QStringLiteral("simpleRouteTargets"));
        auto *targetsLayout = new QVBoxLayout(targets);
        targetsLayout->setContentsMargins(3, 3, 3, 3);
        targetsLayout->setSpacing(2);
        const int selected = current->outbound;
        const auto option = [this, targets, targetsLayout, selected, id](int outbound, const QString &text, const QString &latency, int latencyMs) {
            auto *button = new TargetOption(text, latency, latencyMs, selected == outbound, targets);
            connect(button, &QAbstractButton::clicked, this, [this, id, outbound] {
                QTimer::singleShot(0, this, [this, id, outbound] {
                    if (Route *target = route(id)) target->outbound = outbound;
                    refresh();
                });
            });
            targetsLayout->addWidget(button);
        };
        if (rest_ == Configs::directID)
            option(Configs::proxyID, tr("Main VPN · %1").arg(main_.name), main_.latencyText, main_.latency);
        else
            option(Configs::directID, tr("Directly, around the VPN"), {}, 0);
        for (const auto &target: others_) option(target.outbound, tr("Through %1").arg(target.name), target.latencyText, target.latency);
        if (rest_ != Configs::directID) option(Configs::proxyID, tr("Main VPN · %1").arg(main_.name), main_.latencyText, main_.latency);
        if (selected >= 0 && std::none_of(others_.begin(), others_.end(), [selected](const SimpleRouteTarget &t) { return t.outbound == selected; }))
            option(selected, tr("Missing server"), {}, 0);
        layout->addWidget(targets);
    }

    auto *caption = new QLabel(tr("What it covers").toUpper(), detail);
    caption->setObjectName(QStringLiteral("simpleRouteCaption"));
    layout->addWidget(caption);
    auto *chips = new QWidget(detail);
    auto *flow = new FlowLayout(chips, 5, 5);
    flow->setContentsMargins(0, 0, 0, 0);
    const Route contents = current != nullptr ? *current : defaultsOf(catalogEntry);
    const QString owner = catalogEntry != nullptr ? catalogEntry->name : tr("My sites and apps");
    // A maintained list reads as what it is for; its file name means nothing to the person choosing.
    const auto label = [this, &owner](int kind, const QString &value) {
        if (kind != kRuleSetKind) return value;
        if (value.startsWith(QStringLiteral("geosite-"))) return tr("%1 sites").arg(owner);
        if (value.startsWith(QStringLiteral("geoip-"))) return tr("%1 IP networks").arg(owner);
        return value;
    };
    const QList<std::pair<int, QStringList>> groups{{int(Kind::Process), contents.processes},
                                                    {int(Kind::ProcessPath), contents.processPaths},
                                                    {kRuleSetKind, contents.ruleSets},
                                                    {int(Kind::Domain), contents.domains},
                                                    {int(Kind::Cidr), contents.cidrs}};
    for (const auto &[kind, values]: groups) {
        for (const QString &value: values) {
            auto *chip = new QFrame(chips);
            chip->setObjectName(QStringLiteral("simpleRouteChip"));
            auto *chipLayout = new QHBoxLayout(chip);
            chipLayout->setContentsMargins(7, 2, 7, 2);
            chipLayout->setSpacing(2);
            const QString fullText = label(kind, value);
            auto *text = new QLabel(chip);
            text->setObjectName(QStringLiteral("simpleRouteChipText"));
            text->setTextFormat(Qt::PlainText);
            text->ensurePolished();
            text->setText(text->fontMetrics().elidedText(fullText, Qt::ElideMiddle, 160));
            text->setToolTip(value);
            text->setAccessibleName(fullText);
            chipLayout->addWidget(text);
            if (current != nullptr) {
                auto *remove = new QToolButton(chip);
                remove->setObjectName(QStringLiteral("simpleRouteChipRemove"));
                remove->setText(QStringLiteral("×"));
                remove->setToolTip(tr("Remove %1").arg(label(kind, value)));
                remove->setAccessibleName(tr("Remove %1").arg(label(kind, value)));
                remove->setCursor(Qt::PointingHandCursor);
                connect(remove, &QToolButton::clicked, this, [this, id, kind, value] {
                    QTimer::singleShot(0, this, [this, id, kind, value] { removeValue(id, kind, value); });
                });
                chipLayout->addWidget(remove);
            }
            flow->addWidget(chip);
        }
    }
    if (flow->count() == 0) {
        auto *none = new QLabel(tr("Nothing yet: type below"), chips);
        none->setObjectName(QStringLiteral("simpleRouteNote"));
        flow->addWidget(none);
    }
    layout->addWidget(chips);

    auto *add = new QLineEdit(detail);
    add->setObjectName(QStringLiteral("simpleRouteAdd"));
    add->setProperty("routeId", id);
    add->setPlaceholderText(tr("+ site, .exe or IP, then Enter"));
    add->setAccessibleName(tr("Add to %1").arg(id == kCustomId ? tr("My sites and apps") : catalogEntry->name));
    connect(add, &QLineEdit::returnPressed, this, [this, id, add] {
        const QString text = add->text();
        QTimer::singleShot(0, this, [this, id, text] { addValue(id, text); });
    });
    layout->addWidget(add);
    rows_[id].add = add;

    auto *links = new QHBoxLayout;
    if (id == kCustomId) {
        auto *pick = new QPushButton(tr("Choose a program…"), detail);
        pick->setObjectName(QStringLiteral("simpleRouteLink"));
        pick->setCursor(Qt::PointingHandCursor);
        connect(pick, &QPushButton::clicked, this, &SimpleRoutesPage::pickApplication);
        links->addWidget(pick);
    } else if (isModified(id)) {
        auto *reset = new QPushButton(tr("Reset to the catalog"), detail);
        reset->setObjectName(QStringLiteral("simpleRouteLink"));
        reset->setCursor(Qt::PointingHandCursor);
        connect(reset, &QPushButton::clicked, this, [this, id] {
            QTimer::singleShot(0, this, [this, id] {
                Route *target = route(id);
                const auto *catalogEntry = entry(id);
                if (target == nullptr || catalogEntry == nullptr) return;
                const int outbound = target->outbound;
                *target = defaultsOf(catalogEntry);
                target->outbound = outbound;
                refresh();
            });
        });
        links->addWidget(reset);
    }
    links->addStretch();
    if (links->count() > 1)
        layout->addLayout(links);
    else
        delete links;
    return detail;
}

QWidget *SimpleRoutesPage::makeRestRow() {
    auto *section = makeSection(tr("Default"), {});
    auto *card = currentCard_;
    auto *row = new RouteRow(true, card);
    auto *line = new QHBoxLayout(row);
    line->setContentsMargins(0, 0, 12, 0);
    RouteHeader::Look look;
    look.name = tr("Everything else");
    look.meta = tr("Whatever is not switched on above");
    look.ghost = true;
    auto *header = new RouteHeader(look, row);
    header->setFocusPolicy(Qt::NoFocus);
    header->setCursor(Qt::ArrowCursor);
    line->addWidget(header, 1);
    auto *pill = new TargetPill(row);
    pill->setObjectName(QStringLiteral("simpleRoutePill"));
    pill->setCursor(Qt::PointingHandCursor);
    pill->setPopupMode(QToolButton::InstantPopup);
    pill->setMenu(new QMenu(pill));
    restPill_ = pill;
    updateRestPill();
    line->addWidget(pill, 0, Qt::AlignVCenter);
    card->layout()->addWidget(row);
    return section;
}

void SimpleRoutesPage::updateRestPill() {
    if (restPill_ == nullptr) return;
    const QString route = rest_ == Configs::directID ? QStringLiteral("direct") : QStringLiteral("vpn");
    if (restPill_->property("route").toString() != route) {
        restPill_->setProperty("route", route);
        restPill_->style()->unpolish(restPill_);
        restPill_->style()->polish(restPill_);
    }
    restPill_->setText(targetName(rest_));
    restPill_->setToolTip(targetName(rest_));
    restPill_->setAccessibleName(tr("Everything else: %1").arg(targetName(rest_)));
    QMenu *menu = restPill_->menu();
    menu->clear();
    QList<std::pair<int, QString>> options{{Configs::directID, tr("Directly")}, {Configs::proxyID, tr("Through the VPN")}};
    for (const auto &target: others_) options.append({target.outbound, tr("Through %1").arg(target.name)});
    if (std::none_of(options.begin(), options.end(), [this](const auto &option) { return option.first == loadedRest_; }))
        options.append({loadedRest_, targetName(loadedRest_)});
    for (const auto &[outbound, text]: options) {
        auto *action = menu->addAction(text);
        action->setCheckable(true);
        action->setChecked(rest_ == outbound);
        connect(action, &QAction::triggered, this, [this, outbound] { QTimer::singleShot(0, this, [this, outbound] { setRest(outbound); }); });
    }
}

SimpleRoutesPage::Plan SimpleRoutesPage::plan() const {
    Plan next;
    next.editable = editable_;
    if (!editable_) return next;
    const QString query = search_->text().trimmed();
    const auto matches = [&query](const QString &name, const QStringList &values) {
        if (query.isEmpty() || name.contains(query, Qt::CaseInsensitive)) return true;
        return std::any_of(values.begin(), values.end(), [&query](const QString &v) { return v.contains(query, Qt::CaseInsensitive); });
    };

    QStringList withPrograms;
    for (const auto &item: routes_)
        if ((!item.processes.isEmpty() || !item.processPaths.isEmpty()) && item.outbound != Configs::directID)
            withPrograms << (item.id == kCustomId ? tr("Your programs") : entry(item.id) ? entry(item.id)->name
                                                                                         : item.id);
    if (!wholeComputer_ && !withPrograms.isEmpty() && query.isEmpty())
        next.banner = (embedded_ ? tr("%1: programs go through the VPN only in TUN mode.") : tr("%1: programs go through the VPN only in Whole computer mode."))
                          .arg(withPrograms.join(QStringLiteral(", ")));

    if (const auto [kind, value] = Configs::AppRoutes::Classify(query); kind != Kind::Invalid) {
        const QString what = (kind == Kind::Process || kind == Kind::ProcessPath) ? tr("Add the program %1") : kind == Kind::Domain ? tr("Add the site %1")
                                                                                                                                    : tr("Add the addresses %1");
        next.addRow = what.arg(value) + QStringLiteral("\n") + tr("to My sites and apps");
    }

    QStringList found, rest;
    for (const auto &item: catalog_) {
        if (!matches(item.name, item.processes + item.domains)) continue;
        (detected_.contains(item.id) ? found : rest).append(item.id);
    }
    if (!found.isEmpty()) next.groups.append({tr("Found on this computer"), QString::number(found.size()), found});
    QStringList shownRest;
    for (const QString &id: rest)
        if (showMore_ || !query.isEmpty() || routeOf(id) != nullptr) shownRest.append(id);
    if (!shownRest.isEmpty() && !showMore_ && query.isEmpty()) {
        next.groups.append({tr("Switched on from the catalog"), QString::number(shownRest.size()), shownRest});
    } else if (!shownRest.isEmpty()) {
        // A hundred entries in one card is a wall; the categories are how people look for them.
        QStringList categories;
        for (const QString &id: shownRest)
            if (!categories.contains(entry(id)->category)) categories.append(entry(id)->category);
        for (const QString &category: categories) {
            QStringList members;
            for (const QString &id: shownRest)
                if (entry(id)->category == category) members.append(id);
            next.groups.append({categoryName(category), QString::number(members.size()), members});
        }
    }
    if (query.isEmpty()) {
        const int hidden = int(rest.size() - shownRest.size());
        if (showMore_ || hidden > 0) next.more = showMore_ ? tr("Hide the catalog") : tr("%n more app(s) and site(s)", nullptr, hidden);
    }
    const Route *custom = routeOf(kCustomId);
    next.custom = query.isEmpty() || (custom != nullptr && matches(tr("My sites and apps"), custom->processes + custom->processPaths + custom->domains + custom->cidrs));
    next.restRow = query.isEmpty();
    next.foreign = query.isEmpty() ? foreign_ : 0;
    next.nothing = next.banner.isEmpty() && next.addRow.isEmpty() && next.groups.isEmpty() && next.more.isEmpty() && !next.custom && !next.restRow;
    return next;
}

void SimpleRoutesPage::releaseFocus() {
    // A focused widget that disappears hands focus on, and the scroll area scrolls to wherever that lands.
    if (QWidget *focus = QApplication::focusWidget(); focus != nullptr && content_->isAncestorOf(focus)) scroll_->setFocus(Qt::OtherFocusReason);
}

void SimpleRoutesPage::clearContent() {
    releaseFocus();
    auto *layout = content_->layout();
    while (QLayoutItem *item = layout->takeAt(0)) {
        if (QWidget *widget = item->widget()) {
            retire(widget);
        } else if (QLayout *inner = item->layout()) {
            while (QLayoutItem *child = inner->takeAt(0)) {
                if (QWidget *widget = child->widget()) retire(widget);
                delete child;
            }
        }
        delete item;
    }
    rows_.clear();
    sections_.clear();
    restPill_ = nullptr;
    currentCard_ = nullptr;
    sticky_->hide();
}

void SimpleRoutesPage::rebuild(const Plan &next, const QSet<QString> &wasOpen) {
    const quint64 generation = ++renderGeneration_;
    const int keep = scroll_->verticalScrollBar()->value();
    // Held at its old height while the rows are swapped, so the scroll range cannot shrink and drop the position.
    content_->setUpdatesEnabled(false);
    content_->setMinimumHeight(content_->height());
    clearContent();
    auto *layout = static_cast<QVBoxLayout *>(content_->layout());
    search_->setEnabled(editable_);

    if (!next.editable) {
        auto *note = new QLabel(tr("This routing profile is written by hand, so it can only be changed in the advanced editor."), content_);
        note->setObjectName(QStringLiteral("simpleRouteNote"));
        note->setWordWrap(true);
        layout->addWidget(note);
        auto *open = new QPushButton(tr("Open the advanced editor"), content_);
        open->setObjectName(QStringLiteral("simpleRouteMore"));
        connect(open, &QPushButton::clicked, this, &SimpleRoutesPage::openAdvanced);
        layout->addWidget(open);
    } else {
        if (!next.banner.isEmpty()) {
            auto *banner = new WarningFrame(content_);
            auto *bannerLayout = new QHBoxLayout(banner);
            bannerLayout->setContentsMargins(12, 8, 8, 8);
            auto *text = new QLabel(next.banner, banner);
            text->setObjectName(QStringLiteral("simpleRouteBannerText"));
            text->setWordWrap(true);
            bannerLayout->addWidget(text, 1);
            // Inside a settings window the switch would change the connection behind its Save.
            if (!embedded_) {
                auto *enable = new QPushButton(tr("Switch"), banner);
                enable->setObjectName(QStringLiteral("simpleRouteBannerButton"));
                enable->setCursor(Qt::PointingHandCursor);
                connect(enable, &QPushButton::clicked, this, &SimpleRoutesPage::wholeComputerRequested);
                bannerLayout->addWidget(enable, 0, Qt::AlignVCenter);
            }
            layout->addWidget(banner);
        }
        if (!next.addRow.isEmpty()) {
            auto *addRow = new RouteActionButton(next.addRow, content_);
            addRow->setObjectName(QStringLiteral("simpleRouteAddRow"));
            addRow->setCursor(Qt::PointingHandCursor);
            connect(addRow, &QPushButton::clicked, this, [this] {
                const QString text = search_->text();
                QTimer::singleShot(0, this, [this, text] {
                    if (addValue(kCustomId, text)) search_->clear();
                });
            });
            layout->addWidget(addRow);
        }
        for (const auto &group: next.groups) {
            layout->addWidget(makeSection(group.title, group.count));
            for (const QString &id: group.ids) makeRow(id, !wasOpen.contains(id));
        }
        if (!next.more.isEmpty()) {
            auto *more = new RouteActionButton(next.more, content_);
            more->setObjectName(QStringLiteral("simpleRouteMore"));
            more->setCursor(Qt::PointingHandCursor);
            connect(more, &QPushButton::clicked, this, [this] {
                QTimer::singleShot(0, this, [this] {
                    showMore_ = !showMore_;
                    refresh();
                });
            });
            layout->addWidget(more);
        }
        if (next.custom) {
            layout->addWidget(makeSection(tr("Your own"), {}));
            makeRow(kCustomId, !wasOpen.contains(kCustomId));
        }
        if (next.restRow) layout->addWidget(makeRestRow());
        if (next.foreign > 0) {
            auto *other = new QHBoxLayout;
            auto *label = new QLabel(tr("Other rules in this profile: %1").arg(next.foreign), content_);
            label->setObjectName(QStringLiteral("simpleRouteNote"));
            label->setWordWrap(true);
            other->addWidget(label, 1);
            auto *open = new QPushButton(tr("Open"), content_);
            open->setObjectName(QStringLiteral("simpleRouteLink"));
            open->setCursor(Qt::PointingHandCursor);
            connect(open, &QPushButton::clicked, this, &SimpleRoutesPage::openAdvanced);
            other->addWidget(open);
            layout->addLayout(other);
        }
        if (next.nothing) {
            auto *none = new QLabel(tr("Nothing found. Type a site or an .exe to add your own."), content_);
            none->setObjectName(QStringLiteral("simpleRouteNote"));
            none->setWordWrap(true);
            none->setAlignment(Qt::AlignCenter);
            layout->addWidget(none);
        }
    }
    layout->addStretch();
    plan_ = next;
    built_ = true;
    // Section positions exist only after the new content is laid out.
    QTimer::singleShot(0, this, [this, generation, keep] {
        if (generation != renderGeneration_) return;
        content_->setMinimumHeight(0);
        content_->setUpdatesEnabled(true);
        scroll_->verticalScrollBar()->setValue(keep);
        updateSticky();
    });
}

void SimpleRoutesPage::updateRows() {
    for (auto it = rows_.begin(); it != rows_.end(); ++it) {
        const QString &id = it.key();
        RowParts &parts = it.value();
        updateHeader(id);
        if (parts.toggle != nullptr && parts.toggle->isChecked() != (routeOf(id) != nullptr)) {
            const QSignalBlocker blocker(parts.toggle);
            parts.toggle->setChecked(routeOf(id) != nullptr);
        }
        if (open_ == id) {
            if (parts.reveal == nullptr) {
                openDetail(id, true);
                continue;
            }
            if (parts.closing) {
                parts.closing = false;
                parts.reveal->animateTo(1, animated_);
            }
            if (const QString key = detailKey(id); key != parts.detailKey) {
                if (QWidget *focus = QApplication::focusWidget(); focus != nullptr && parts.reveal->isAncestorOf(focus)) releaseFocus();
                parts.detailKey = key;
                parts.reveal->setContent(makeDetail(id, parts.reveal));
            }
        } else if (parts.reveal != nullptr && !parts.closing) {
            parts.closing = true;
            if (QWidget *focus = QApplication::focusWidget(); focus != nullptr && parts.reveal->isAncestorOf(focus)) releaseFocus();
            RouteReveal *reveal = parts.reveal;
            reveal->animateTo(0, animated_, [this, id, reveal] {
                auto found = rows_.find(id);
                if (found == rows_.end() || found->reveal != reveal) return;
                found->reveal = nullptr;
                found->add = nullptr;
                found->closing = false;
                found->detailKey.clear();
                retire(reveal);
            });
        }
    }
    updateRestPill();
}

SimpleRoutesPage::Drafts SimpleRoutesPage::captureDrafts() const {
    Drafts drafts;
    for (auto it = rows_.cbegin(); it != rows_.cend(); ++it) {
        QLineEdit *input = it->add;
        if (input == nullptr) continue;
        drafts.text.insert(it.key(), input->text());
        if (input->hasFocus()) {
            drafts.focused = it.key();
            drafts.cursor = input->selectionStart() >= 0 ? input->selectionStart() : input->cursorPosition();
            drafts.selection = int(input->selectedText().size());
        }
    }
    return drafts;
}

void SimpleRoutesPage::restoreDrafts(const Drafts &drafts) {
    for (auto it = rows_.begin(); it != rows_.end(); ++it) {
        QLineEdit *input = it->add;
        if (input == nullptr || it.key() == focusAdd_) continue;
        // A field that survived kept its own text; only a rebuilt one takes the draft back.
        if (input->text().isEmpty() && !drafts.text.value(it.key()).isEmpty()) input->setText(drafts.text.value(it.key()));
        if (it.key() == drafts.focused && !input->hasFocus()) {
            input->setFocus();
            if (drafts.selection > 0)
                input->setSelection(drafts.cursor, drafts.selection);
            else
                input->setCursorPosition(drafts.cursor);
        }
    }
    if (!focusAdd_.isEmpty()) {
        if (QLineEdit *input = rows_.value(focusAdd_).add) {
            input->clear();
            input->setFocus();
        }
        focusAdd_.clear();
    }
}

void SimpleRoutesPage::handOver() {
    if (!embedded_ || !editable_ || (routes_ == handed_ && rest_ == handedRest_)) return;
    handed_ = routes_;
    handedRest_ = rest_;
    // The owner turns the profile's rules on with the first change, as Apply does.
    rulesEnabled_ = true;
    emit stagedChanged(routes_, rest_);
}

void SimpleRoutesPage::refresh(bool keepDrafts) {
    const Drafts drafts = keepDrafts ? captureDrafts() : Drafts{};
    const Plan next = plan();
    if (!built_ || !(next == plan_)) {
        QSet<QString> wasOpen;
        // A rebuild caused by something else must not replay the opening of a card that was already open.
        if (built_)
            for (auto it = rows_.cbegin(); it != rows_.cend(); ++it)
                if (it->reveal != nullptr && !it->closing) wasOpen.insert(it.key());
        if (!built_) wasOpen.insert(open_);
        rebuild(next, wasOpen);
    } else {
        updateRows();
    }
    restoreDrafts(drafts);
    if (const QStringList needed = neededRuleSets(); needed != lastNeeded_) {
        lastNeeded_ = needed;
        emit ruleSetsNeeded(needed);
    }
    handOver();
    renderFooter();
}

void SimpleRoutesPage::renderFooter() {
    QList<std::pair<int, QStringList>> groups;
    for (const auto &item: routes_) {
        const QString name = item.id == kCustomId ? tr("your own") : entry(item.id) ? entry(item.id)->name
                                                                                    : item.id;
        auto it = std::find_if(groups.begin(), groups.end(), [&item](const auto &group) { return group.first == item.outbound; });
        if (it == groups.end())
            groups.append({item.outbound, {name}});
        else
            it->second.append(name);
    }
    QStringList parts;
    for (const auto &[outbound, names]: groups) {
        QStringList shown = names.mid(0, 3);
        if (names.size() > 3) shown << tr("%n more", nullptr, int(names.size() - 3));
        const QString where = outbound == Configs::proxyID ? tr("through the VPN") : outbound == Configs::directID ? tr("directly")
                                                                                 : outbound == Configs::blockID    ? tr("blocked")
                                                                                                                   : tr("through %1").arg(targetName(outbound));
        parts << QStringLiteral("<b>%1</b> %2").arg(shown.join(QStringLiteral(", ")).toHtmlEscaped(), where.toHtmlEscaped());
    }
    parts << (rest_ == Configs::directID  ? tr("everything else directly")
              : rest_ == Configs::blockID ? tr("everything else blocked")
              : rest_ == Configs::proxyID ? tr("everything else through the VPN")
                                          : tr("everything else through %1").arg(targetName(rest_)))
                 .toHtmlEscaped();
    QString sentence = parts.join(QStringLiteral(" · "));
    if (!sentence.isEmpty() && !sentence.startsWith(QLatin1Char('<'))) sentence[0] = sentence[0].toUpper();
    const bool rulesOff = !rulesEnabled_ && editable_;
    if (rulesOff)
        sentence = (embedded_ ? tr("Profile rules are disabled. Any change here turns them on.") : tr("Profile rules are disabled. Apply to enable them.")).toHtmlEscaped() +
                   QStringLiteral("<br>") + sentence;
    sentence_->setFixedHeight(sentence_->fontMetrics().lineSpacing() * (rulesOff ? 3 : 2) + 2);
    sentence_->setText(sentence);
    const bool dirty = isDirty();
    apply_->setEnabled(dirty && editable_ && !downloading_);
    apply_->setText(!dirty ? tr("Everything is applied") : connected_ ? tr("Apply and reconnect")
                                                                      : tr("Apply"));
}
