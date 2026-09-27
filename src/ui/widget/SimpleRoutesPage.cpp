#include "include/ui/widget/SimpleRoutesPage.h"

#include <QAbstractButton>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPainter>
#include <QProgressBar>
#include <QPushButton>
#include <QStyle>
#include <QScrollArea>
#include <QScrollBar>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

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
    }
    QSize sizeHint() const override { return {240, 52}; }
    QSize minimumSizeHint() const override { return {160, 52}; }

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
            painter.setBrush(colors.surfaceHover);
            painter.drawRoundedRect(QRectF(rect()).adjusted(.5, .5, -.5, -.5), 7, 7);
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
const QString kRoutesStyle = QStringLiteral(R"(
QLabel#simpleRoutesTitle { font-size: 16px; font-weight: 600; color: #F1F3F5; background: transparent; }
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
QFrame#simpleRouteCard { background: #222529; border: 1px solid #2F3136; border-radius: 12px; }
QLabel#simpleRouteGroup, QLabel#simpleRouteCaption { color: #747C86; font-size: 11px; font-weight: 600; background: transparent; }
QFrame#simpleRouteTargets { background: #171B21; border: 1px solid #2F3136; border-radius: 10px; }
QFrame#simpleRouteChip { background: #171B21; border: 1px solid #2F3136; border-radius: 6px; }
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
    auto *backButton = headerButton(MaterialIcon::Glyph::ChevronRight, tr("Back"), true);
    backButton->setObjectName(QStringLiteral("simpleRoutesBack"));
    connect(backButton, &QToolButton::clicked, this, &SimpleRoutesPage::back);
    head->addWidget(backButton);
    auto *title = new QLabel(tr("Routing"), this);
    title->setObjectName(QStringLiteral("simpleRoutesTitle"));
    head->addWidget(title, 1);
    auto *advanced = headerButton(MaterialIcon::Glyph::Tune, tr("Advanced editor"), false);
    connect(advanced, &QToolButton::clicked, this, &SimpleRoutesPage::openAdvanced);
    head->addWidget(advanced);
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
    connect(search_, &QLineEdit::textChanged, this, &SimpleRoutesPage::render);
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
    connect(themeManager(), &ThemeManager::themeChanged, this, &SimpleRoutesPage::render);
    rest_ = loadedRest_ = Configs::directID;
    render();
}

QWidget *SimpleRoutesPage::scrollArea() const { return scroll_; }

void SimpleRoutesPage::load(const QList<Route> &routes, int restOutbound, int foreignRules, bool editable) {
    loaded_ = routes_ = routes;
    loadedRest_ = rest_ = restOutbound == Configs::directID ? Configs::directID : Configs::proxyID;
    foreign_ = foreignRules;
    editable_ = editable;
    open_.clear();
    render();
}

void SimpleRoutesPage::setTargets(const SimpleRouteTarget &main, const QList<SimpleRouteTarget> &others) {
    main_ = main;
    others_ = others;
    render();
}

void SimpleRoutesPage::setDetected(const QHash<QString, bool> &runningById) {
    detected_ = runningById;
    render();
}

void SimpleRoutesPage::setIcon(const QString &id, const QIcon &icon) {
    icons_.insert(id, icon);
    if (renderQueued_) return;
    // Icons arrive one by one; the list is rebuilt once for the batch.
    renderQueued_ = true;
    QTimer::singleShot(0, this, [this] {
        renderQueued_ = false;
        render();
    });
}

void SimpleRoutesPage::setWholeComputer(bool wholeComputer) {
    // Called on every status refresh; rebuilding the list would drop what is being typed.
    if (wholeComputer_ == wholeComputer) return;
    wholeComputer_ = wholeComputer;
    render();
}

void SimpleRoutesPage::setConnected(bool connected) {
    if (connected_ == connected) return;
    connected_ = connected;
    renderFooter();
}

void SimpleRoutesPage::addEntries(const QStringList &lines) {
    for (const QString &line: lines) addValue(kCustomId, line);
}

bool SimpleRoutesPage::isDirty() const { return routes_ != loaded_ || rest_ != loadedRest_; }

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
    for (const auto &target: others_)
        if (target.outbound == outbound) return target.name;
    return tr("Missing server");
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
    render();
}

void SimpleRoutesPage::setRest(int outbound) {
    const int before = defaultOutbound();
    rest_ = outbound;
    // Entries that followed the default flip with it; one sent to a named server keeps that server.
    for (auto &item: routes_)
        if (item.outbound == before) item.outbound = defaultOutbound();
    render();
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
    QStringList &list = kind == Kind::Process ? target->processes : kind == Kind::Domain ? target->domains
                                                                                         : target->cidrs;
    if (!list.contains(value, Qt::CaseInsensitive)) list.append(value);
    open_ = id;
    focusAdd_ = id;
    render();
    return true;
}

void SimpleRoutesPage::removeValue(const QString &id, int kind, const QString &value) {
    Route *target = route(id);
    if (target == nullptr) return;
    (kind == kRuleSetKind ? target->ruleSets : kind == int(Kind::Process) ? target->processes
                                           : kind == int(Kind::Domain)    ? target->domains
                                                                          : target->cidrs)
        .removeAll(value);
    if (target->isEmpty()) routes_.removeIf([&id](const Route &item) { return item.id == id; });
    render();
}

bool SimpleRoutesPage::isModified(const QString &id) const {
    const auto *catalogEntry = entry(id);
    const Route *current = nullptr;
    for (const auto &item: routes_)
        if (item.id == id) current = &item;
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

QWidget *SimpleRoutesPage::makeRow(const QString &id) {
    const bool custom = id == kCustomId;
    const auto *catalogEntry = entry(id);
    const Route *current = route(id);
    auto *card = currentCard_;
    auto *row = new RouteRow(card->layout()->count() == 0, card);
    auto *rowLayout = new QVBoxLayout(row);
    rowLayout->setContentsMargins(0, 0, 0, 0);
    rowLayout->setSpacing(0);
    auto *line = new QHBoxLayout;
    line->setContentsMargins(0, 0, custom ? 4 : 12, 0);
    line->setSpacing(8);

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
    if (!contents.processes.isEmpty()) meta << tr("%n program(s)", nullptr, contents.processes.size());
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
    auto *header = new RouteHeader(look, row);
    header->setObjectName(QStringLiteral("simpleRouteRow_") + (custom ? kCustomId : id));
    connect(header, &QAbstractButton::clicked, this, [this, id] {
        open_ = open_ == id ? QString() : id;
        render();
    });
    line->addWidget(header, 1);
    if (!custom) {
        auto *toggle = new ThronedToggle(current != nullptr, row);
        toggle->setObjectName(QStringLiteral("simpleRouteToggle_") + id);
        toggle->setAccessibleName(look.name);
        connect(toggle, &QAbstractButton::toggled, this, [this, id](bool on) {
            // Deferred: the toggle is rebuilt with the list and must not delete itself mid-signal.
            QTimer::singleShot(0, this, [this, id, on] { setRouteEnabled(id, on); });
        });
        line->addWidget(toggle, 0, Qt::AlignVCenter);
    }
    rowLayout->addLayout(line);
    if (open_ == id) rowLayout->addWidget(makeDetail(id));
    card->layout()->addWidget(row);
    return row;
}

QWidget *SimpleRoutesPage::makeDetail(const QString &id) {
    const Route *current = route(id);
    const auto *catalogEntry = entry(id);
    auto *detail = new QWidget(content_);
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
        const auto option = [this, targets, targetsLayout, current, id](int outbound, const QString &text, const QString &latency, int latencyMs) {
            auto *button = new TargetOption(text, latency, latencyMs, current->outbound == outbound, targets);
            connect(button, &QAbstractButton::clicked, this, [this, id, outbound] {
                QTimer::singleShot(0, this, [this, id, outbound] {
                    if (Route *target = route(id)) target->outbound = outbound;
                    render();
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
        if (current->outbound >= 0 && std::none_of(others_.begin(), others_.end(), [current](const SimpleRouteTarget &t) { return t.outbound == current->outbound; }))
            option(current->outbound, tr("Missing server"), {}, 0);
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
                                                    {kRuleSetKind, contents.ruleSets},
                                                    {int(Kind::Domain), contents.domains},
                                                    {int(Kind::Cidr), contents.cidrs}};
    for (const auto &[kind, values]: groups) {
        for (const QString &value: values) {
            auto *chip = new QFrame(chips);
            chip->setObjectName(QStringLiteral("simpleRouteChip"));
            auto *chipLayout = new QHBoxLayout(chip);
            chipLayout->setContentsMargins(7, 2, 2, 2);
            chipLayout->setSpacing(2);
            auto *text = new QLabel(label(kind, value), chip);
            text->setObjectName(QStringLiteral("simpleRouteChipText"));
            text->setTextFormat(Qt::PlainText);
            if (kind == kRuleSetKind) text->setToolTip(value);
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
                render();
            });
        });
        links->addWidget(reset);
    }
    links->addStretch();
    if (links->count() > 1) layout->addLayout(links);
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
    auto *pill = new QToolButton(row);
    pill->setObjectName(QStringLiteral("simpleRoutePill"));
    pill->setProperty("route", rest_ == Configs::directID ? QStringLiteral("direct") : QStringLiteral("vpn"));
    pill->setText(targetName(rest_) + QStringLiteral("  ▾"));
    pill->setAccessibleName(tr("Everything else: %1").arg(targetName(rest_)));
    pill->setCursor(Qt::PointingHandCursor);
    pill->setPopupMode(QToolButton::InstantPopup);
    auto *menu = new QMenu(pill);
    for (const auto &[outbound, text]: {std::pair{int(Configs::directID), tr("Directly")}, std::pair{int(Configs::proxyID), tr("Through the VPN")}}) {
        auto *action = menu->addAction(text);
        action->setCheckable(true);
        action->setChecked(rest_ == outbound);
        connect(action, &QAction::triggered, this, [this, outbound] { QTimer::singleShot(0, this, [this, outbound] { setRest(outbound); }); });
    }
    pill->setMenu(menu);
    line->addWidget(pill, 0, Qt::AlignVCenter);
    card->layout()->addWidget(row);
    return section;
}

void SimpleRoutesPage::render() {
    const int keep = scroll_->verticalScrollBar()->value();
    auto *old = scroll_->takeWidget();
    if (old != nullptr) old->deleteLater();
    content_ = new QWidget;
    content_->setObjectName(QStringLiteral("simpleRoutesContent"));
    sections_.clear();
    sticky_->hide();
    auto *layout = new QVBoxLayout(content_);
    layout->setContentsMargins(0, 0, 8, 12);
    layout->setSpacing(14);
    search_->setEnabled(editable_);

    if (!editable_) {
        auto *note = new QLabel(tr("This routing profile is written by hand, so it can only be changed in the advanced editor."), content_);
        note->setObjectName(QStringLiteral("simpleRouteNote"));
        note->setWordWrap(true);
        layout->addWidget(note);
        auto *open = new QPushButton(tr("Open the advanced editor"), content_);
        open->setObjectName(QStringLiteral("simpleRouteMore"));
        connect(open, &QPushButton::clicked, this, &SimpleRoutesPage::openAdvanced);
        layout->addWidget(open);
        layout->addStretch();
        scroll_->setWidget(content_);
        renderFooter();
        return;
    }

    const QString query = search_->text().trimmed();
    const auto matches = [&query](const QString &name, const QStringList &values) {
        if (query.isEmpty() || name.contains(query, Qt::CaseInsensitive)) return true;
        return std::any_of(values.begin(), values.end(), [&query](const QString &v) { return v.contains(query, Qt::CaseInsensitive); });
    };

    QStringList withPrograms;
    for (const auto &item: routes_)
        if (!item.processes.isEmpty() && item.outbound != Configs::directID)
            withPrograms << (item.id == kCustomId ? tr("Your programs") : entry(item.id) ? entry(item.id)->name
                                                                                         : item.id);
    if (!wholeComputer_ && !withPrograms.isEmpty() && query.isEmpty()) {
        auto *banner = new WarningFrame(content_);
        auto *bannerLayout = new QHBoxLayout(banner);
        bannerLayout->setContentsMargins(12, 8, 8, 8);
        auto *text = new QLabel(tr("%1: programs go through the VPN only in Whole computer mode.").arg(withPrograms.join(QStringLiteral(", "))), banner);
        text->setObjectName(QStringLiteral("simpleRouteBannerText"));
        text->setWordWrap(true);
        bannerLayout->addWidget(text, 1);
        auto *enable = new QPushButton(tr("Switch"), banner);
        enable->setObjectName(QStringLiteral("simpleRouteBannerButton"));
        enable->setCursor(Qt::PointingHandCursor);
        connect(enable, &QPushButton::clicked, this, &SimpleRoutesPage::wholeComputerRequested);
        bannerLayout->addWidget(enable, 0, Qt::AlignVCenter);
        layout->addWidget(banner);
    }

    if (const auto [kind, value] = Configs::AppRoutes::Classify(query); kind != Kind::Invalid) {
        const QString what = kind == Kind::Process ? tr("Add the program %1") : kind == Kind::Domain ? tr("Add the site %1")
                                                                                                     : tr("Add the addresses %1");
        auto *addRow = new QPushButton(what.arg(value) + QStringLiteral("\n") + tr("to My sites and apps"), content_);
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

    QStringList found, rest;
    for (const auto &item: catalog_) {
        if (!matches(item.name, item.processes + item.domains)) continue;
        (detected_.contains(item.id) ? found : rest).append(item.id);
    }
    if (!found.isEmpty()) {
        layout->addWidget(makeSection(tr("Found on this computer"), QString::number(found.size())));
        for (const QString &id: found) makeRow(id);
    }
    QStringList shownRest;
    for (const QString &id: rest)
        if (showMore_ || !query.isEmpty() || route(id) != nullptr) shownRest.append(id);
    if (!shownRest.isEmpty() && !showMore_ && query.isEmpty()) {
        layout->addWidget(makeSection(tr("Switched on from the catalog"), QString::number(shownRest.size())));
        for (const QString &id: shownRest) makeRow(id);
    } else if (!shownRest.isEmpty()) {
        // A hundred entries in one card is a wall; the categories are how people look for them.
        QStringList categories;
        for (const QString &id: shownRest)
            if (!categories.contains(entry(id)->category)) categories.append(entry(id)->category);
        for (const QString &category: categories) {
            QStringList members;
            for (const QString &id: shownRest)
                if (entry(id)->category == category) members.append(id);
            layout->addWidget(makeSection(categoryName(category), QString::number(members.size())));
            for (const QString &id: members) makeRow(id);
        }
    }
    if (query.isEmpty()) {
        const int hidden = rest.size() - shownRest.size();
        if (showMore_ || hidden > 0) {
            auto *more = new QPushButton(showMore_ ? tr("Hide the catalog") : tr("%n more app(s) and site(s)", nullptr, hidden), content_);
            more->setObjectName(QStringLiteral("simpleRouteMore"));
            more->setCursor(Qt::PointingHandCursor);
            connect(more, &QPushButton::clicked, this, [this] {
                QTimer::singleShot(0, this, [this] {
                    showMore_ = !showMore_;
                    render();
                });
            });
            layout->addWidget(more);
        }
    }
    const Route *custom = route(kCustomId);
    if (query.isEmpty() || (custom != nullptr && matches(tr("My sites and apps"), custom->processes + custom->domains + custom->cidrs))) {
        layout->addWidget(makeSection(tr("Your own"), {}));
        makeRow(kCustomId);
    }
    if (query.isEmpty()) {
        layout->addWidget(makeRestRow());
        if (foreign_ > 0) {
            auto *other = new QHBoxLayout;
            auto *label = new QLabel(tr("Other rules in this profile: %1").arg(foreign_), content_);
            label->setObjectName(QStringLiteral("simpleRouteNote"));
            other->addWidget(label, 1);
            auto *open = new QPushButton(tr("Open"), content_);
            open->setObjectName(QStringLiteral("simpleRouteLink"));
            open->setCursor(Qt::PointingHandCursor);
            connect(open, &QPushButton::clicked, this, &SimpleRoutesPage::openAdvanced);
            other->addWidget(open);
            layout->addLayout(other);
        }
    }
    if (layout->count() == 0) {
        auto *none = new QLabel(tr("Nothing found. Type a site or an .exe to add your own."), content_);
        none->setObjectName(QStringLiteral("simpleRouteNote"));
        none->setWordWrap(true);
        none->setAlignment(Qt::AlignCenter);
        layout->addWidget(none);
    }
    layout->addStretch();
    scroll_->setWidget(content_);
    scroll_->verticalScrollBar()->setValue(keep);
    // Section positions exist only after the new content is laid out.
    QTimer::singleShot(0, this, &SimpleRoutesPage::updateSticky);
    if (const QStringList needed = neededRuleSets(); needed != lastNeeded_) {
        lastNeeded_ = needed;
        emit ruleSetsNeeded(needed);
    }
    if (!focusAdd_.isEmpty()) {
        for (auto *add: content_->findChildren<QLineEdit *>(QStringLiteral("simpleRouteAdd")))
            if (add->property("routeId").toString() == focusAdd_) add->setFocus();
        focusAdd_.clear();
    }
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
                                                                                                                   : tr("through %1").arg(targetName(outbound));
        parts << QStringLiteral("<b>%1</b> %2").arg(shown.join(QStringLiteral(", ")).toHtmlEscaped(), where.toHtmlEscaped());
    }
    parts << (rest_ == Configs::directID ? tr("everything else directly") : tr("everything else through the VPN")).toHtmlEscaped();
    QString sentence = parts.join(QStringLiteral(" · "));
    if (!sentence.isEmpty() && !sentence.startsWith(QLatin1Char('<'))) sentence[0] = sentence[0].toUpper();
    sentence_->setText(sentence);
    const bool dirty = isDirty();
    apply_->setEnabled(dirty && editable_ && !downloading_);
    apply_->setText(!dirty ? tr("Everything is applied") : connected_ ? tr("Apply and reconnect")
                                                                      : tr("Apply"));
}
