#include "include/global/Utils.hpp"
#include "include/database/entities/RouteProfile.h"
#include "include/ui/setting/RouteProfileSimpleEditor.h"

#include "include/ui/setting/ApplicationPickerDialog.h"
#include "include/ui/setting/ThemeManager.hpp"
#include "include/ui/widget/FlowLayout.h"
#include "include/ui/widget/ActionButton.h"
#include "include/ui/widget/MaterialIcon.h"
#include "include/ui/widget/ThronedToggle.h"

#include <QAbstractButton>
#include <QComboBox>
#include <QDir>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileIconProvider>
#include <QFileInfo>
#include <QFontMetrics>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QScrollArea>
#include <QTextLayout>
#include <QTimer>

#include <limits>
#include <QInputDialog>
#include <QMessageBox>
#include <QHostAddress>
#include <QMap>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QTextLayout>
#include <QSignalBlocker>
#include <QStyleOptionButton>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <functional>

namespace {

constexpr auto Blue = "#237AE9";
constexpr auto Cyan = "#29B6F6";
constexpr auto Green = "#2EBC75";
constexpr auto Red = "#FF4D56";
constexpr auto Purple = "#A66CFF";
constexpr auto Muted = "#A4ABB4";


// Same footprint as an action, drawn as an outline so it reads as "make one"
// rather than as another action sitting in the list.
class AddActionButton final : public QAbstractButton {
public:
    AddActionButton(const QString &title, const QColor &tone, QWidget *parent = nullptr)
        : QAbstractButton(parent), tone_(tone) {
        setText(title);
        setCursor(Qt::PointingHandCursor);
        setFixedHeight(38);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    }

protected:
    void paintEvent(QPaintEvent *) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const QRectF bounds = rect().adjusted(.5, .5, -.5, -.5);
        QPen pen(underMouse() ? tone_ : QColor("#3A4048"));
        pen.setStyle(Qt::DashLine);
        painter.setPen(pen);
        painter.setBrush(Qt::NoBrush);
        painter.drawRoundedRect(bounds, 6, 6);

        const auto icon = MaterialIcon::pixmap(MaterialIcon::Glyph::Add, tone_, 16);
        const int textWidth = QFontMetrics(font()).horizontalAdvance(text());
        const int left = std::max(12, (width() - textWidth - 22) / 2);
        painter.drawPixmap(left, (height() - 16) / 2, icon);
        painter.setPen(QColor(underMouse() ? "#E8ECF1" : "#A4ABB4"));
        painter.drawText(QRect(left + 22, 0, width() - left - 26, height()),
                         Qt::AlignVCenter | Qt::AlignLeft, text());
    }

    void enterEvent(QEnterEvent *) override { update(); }
    void leaveEvent(QEvent *) override { update(); }

private:
    QColor tone_;
};

struct ActionPresentation {
    QString title;
    QString description;
    QColor tone;
    MaterialIcon::Glyph glyph;
};

ActionPresentation actionPresentation(int action, const QString &viaLabel = {}) {
    if (RouteProfileSimpleEditor::isViaAction(action)) {
        // The name is already the heading; repeating it here only overflows the row.
        return {viaLabel,
                RouteProfileSimpleEditor::tr("Traffic that should leave through this profile instead of the active one."),
                QColor(Cyan), MaterialIcon::Glyph::Public};
    }
    switch (action) {
    case 0: return {RouteProfileSimpleEditor::tr("Direct rules"), RouteProfileSimpleEditor::tr("Traffic that should bypass the proxy."), QColor(Green), MaterialIcon::Glyph::Direct};
    case 1: return {RouteProfileSimpleEditor::tr("Block rules"), RouteProfileSimpleEditor::tr("Traffic that should be rejected."), QColor(Red), MaterialIcon::Glyph::Block};
    case 3: return {RouteProfileSimpleEditor::tr("WARP bypass rules"), RouteProfileSimpleEditor::tr("Traffic that should bypass the WARP outbound."), QColor(Purple), MaterialIcon::Glyph::SwapVertical};
    default: return {RouteProfileSimpleEditor::tr("Proxy rules"), RouteProfileSimpleEditor::tr("Traffic that should be routed through a proxy outbound."), QColor(Blue), MaterialIcon::Glyph::Shield};
    }
}

QString rulePrefix(const QString &rule) {
    return rule.left(rule.indexOf(':'));
}

QString ruleValue(const QString &rule) {
    return rule.mid(rule.indexOf(':') + 1);
}

QString ruleDisplayValue(const QString &rule) {
    const QString value = ruleValue(rule);
    if (rule.startsWith("processPath:")) return QFileInfo(value).fileName();
    if (rule.startsWith("ruleset:")) {
        for (const auto &prefix : {QStringLiteral("geosite-"), QStringLiteral("geoip-")})
            if (value.startsWith(prefix)) return value.mid(prefix.size());
    }
    return value;
}

QIcon iconForRule(const QString &rule, MaterialIcon::Glyph fallback, const QColor &tone) {
    if (rule.startsWith("processPath:")) {
        const QFileInfo info(ruleValue(rule));
        if (info.exists()) {
            QFileIconProvider provider;
            const QIcon icon = provider.icon(info);
            if (!icon.isNull()) return icon;
        }
    }
    return MaterialIcon::icon(fallback, tone, 16);
}

class RuleChip final : public QAbstractButton {
public:
    RuleChip(const QString &rule, const QIcon &icon, bool removable, QWidget *parent = nullptr)
        : QAbstractButton(parent), rule_(rule), icon_(icon), removable_(removable) {
        setCursor(removable ? Qt::PointingHandCursor : Qt::ArrowCursor);
        setToolTip(rule);
        // Fixed would clamp the chip back to its own hint and break the columns.
        setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
        setFixedHeight(32);
    }

    QSize sizeHint() const override {
        return {QFontMetrics(font()).horizontalAdvance(ruleDisplayValue(rule_)) + (removable_ ? 66 : 46), 32};
    }

    void setIcon(const QIcon &icon) {
        icon_ = icon;
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        QColor border("#343941");
        QColor fill("#222529");
        if (rule_.startsWith("processName:") || rule_.startsWith("processPath:")) {
            border = QColor("#263C55");
            fill = QColor("#1B2634");
        } else if (rule_.startsWith("ruleset:")) {
            border = QColor(rule_.contains("geoip-", Qt::CaseInsensitive) ? "#46402E" : "#3C3657");
            fill = QColor(rule_.contains("geoip-", Qt::CaseInsensitive) ? "#25231D" : "#222034");
        } else if (rule_.startsWith("ip:")) {
            border = QColor("#46402E");
            fill = QColor("#25231D");
        }
        else if (rule_.startsWith("suffix:") || rule_.startsWith("keyword:") || rule_.startsWith("regex:")) {
            border = QColor("#2B3F4A");
            fill = QColor("#1A242A");
        }
        if (underMouse() && removable_) border = QColor("#2F91FF");
        painter.setPen(border);
        painter.setBrush(fill);
        painter.drawRoundedRect(rect().adjusted(0, 0, -1, -1), 5, 5);
        painter.drawPixmap(10, (height() - 17) / 2, icon_.pixmap(17, 17));
        painter.setPen(QColor("#E5E8EB"));
        const QRect textArea(35, 0, width() - (removable_ ? 56 : 45), height());
        painter.drawText(textArea, Qt::AlignVCenter | Qt::AlignLeft,
                         painter.fontMetrics().elidedText(ruleDisplayValue(rule_), Qt::ElideRight, textArea.width()));
        if (removable_) {
            painter.setPen(QColor("#A4ABB4"));
            painter.drawText(QRect(width() - 28, 0, 18, height()), Qt::AlignCenter, QStringLiteral("×"));
        }
    }

private:
    QString rule_;
    QIcon icon_;
    bool removable_;
};

constexpr int kChipPreviewLimit = 24;
constexpr int kChipFilterLimit = 12;

class RuleCard final : public QFrame {
public:
    RuleCard(const QString &title, const QString &subtitle, MaterialIcon::Glyph glyph, const QColor &tone,
             const QStringList &rules, bool expanded, const std::function<void(const QString &)> &remove,
             const std::function<void()> &add,
             QWidget *parent = nullptr)
        : QFrame(parent), glyph_(glyph), tone_(tone), rules_(rules), remove_(remove) {
        setObjectName("routeRuleCard");
        auto *root = new QVBoxLayout(this);
        root->setContentsMargins(14, 12, 14, 12);
        root->setSpacing(8);

        auto *heading = new QHBoxLayout;
        auto *icon = new QLabel(this);
        icon->setPixmap(MaterialIcon::pixmap(glyph, tone, 18));
        icon->setFixedSize(22, 22);
        heading->addWidget(icon);
        auto *titles = new QVBoxLayout;
        titles->setSpacing(1);
        auto *titleRow = new QHBoxLayout;
        auto *titleLabel = new QLabel(title, this);
        titleLabel->setObjectName("routeSectionTitle");
        titleRow->addWidget(titleLabel);
        auto *count = new QLabel(QString::number(rules.size()), this);
        count->setObjectName("routeCountPill");
        titleRow->addWidget(count);
        titleRow->addStretch();
        titles->addLayout(titleRow);
        auto *subtitleLabel = new QLabel(subtitle, this);
        subtitleLabel->setObjectName("routeMuted");
        titles->addWidget(subtitleLabel);
        heading->addLayout(titles, 1);
        if (rules_.size() > kChipFilterLimit) {
            filter_ = new QLineEdit(this);
            filter_->setObjectName("routeCardFilter");
            filter_->setClearButtonEnabled(true);
            filter_->setPlaceholderText(RouteProfileSimpleEditor::tr("Filter…"));
            filter_->addAction(MaterialIcon::icon(MaterialIcon::Glyph::Search, QColor(Muted), 16),
                               QLineEdit::LeadingPosition);
            filter_->setFixedWidth(168);
            QObject::connect(filter_, &QLineEdit::textChanged, this, [this] { rebuildChips(); });
            heading->addWidget(filter_);
        }
        if (add) {
            auto *addButton = new QPushButton(RouteProfileSimpleEditor::tr("Add"), this);
            addButton->setObjectName("routeCardAddButton");
            addButton->setIcon(MaterialIcon::icon(MaterialIcon::Glyph::Add, QColor("#DDE2E7"), 16));
            QObject::connect(addButton, &QPushButton::clicked, this, [add] { add(); });
            heading->addWidget(addButton);
        }
        auto *expand = new QToolButton(this);
        expand->setObjectName("routeIconButton");
        expand->setCheckable(true);
        expand->setChecked(expanded);
        expand->setIcon(MaterialIcon::icon(expanded ? MaterialIcon::Glyph::ChevronDown : MaterialIcon::Glyph::ChevronRight,
                                           QColor(Muted), 18));
        heading->addWidget(expand);
        root->addLayout(heading);

        details_ = new QWidget(this);
        details_->setObjectName("routeTransparent");
        chips_ = new FlowLayout(details_, 8, 6);
        chips_->setContentsMargins(32, 2, 0, 0);
        rebuildChips();
        details_->setVisible(expanded);
        subtitleLabel->setVisible(expanded);
        if (filter_) filter_->setVisible(expanded);
        root->addWidget(details_);
        QObject::connect(expand, &QToolButton::toggled, this, [this, subtitleLabel, expand](bool show) {
            details_->setVisible(show);
            subtitleLabel->setVisible(show);
            if (filter_) filter_->setVisible(show);
            expand->setIcon(MaterialIcon::icon(show ? MaterialIcon::Glyph::ChevronDown : MaterialIcon::Glyph::ChevronRight,
                                               QColor(Muted), 18));
        });
    }

private:
    void rebuildChips() {
        while (QLayoutItem *item = chips_->takeAt(0)) {
            if (QWidget *widget = item->widget()) widget->deleteLater();
            delete item;
        }
        const QString needle = filter_ ? filter_->text().trimmed() : QString();
        QStringList matching;
        for (const QString &rule : rules_)
            if (needle.isEmpty() || rule.contains(needle, Qt::CaseInsensitive)) matching.append(rule);
        std::sort(matching.begin(), matching.end(), [](const QString &left, const QString &right) {
            const QString leftKind = rulePrefix(left);
            const QString rightKind = rulePrefix(right);
            if (leftKind != rightKind) return leftKind < rightKind;
            return ruleDisplayValue(left).compare(ruleDisplayValue(right), Qt::CaseInsensitive) < 0;
        });

        if (matching.isEmpty()) {
            auto *empty = new QLabel(needle.isEmpty() ? RouteProfileSimpleEditor::tr("No rules added yet.")
                                                      : RouteProfileSimpleEditor::tr("Nothing matches this filter."),
                                     details_);
            empty->setObjectName("routeEmpty");
            chips_->addWidget(empty);
            return;
        }

        chips_->setUniformColumns(matching.size() > 8, 178, 268);
        const bool folded = !showAll_ && matching.size() > kChipPreviewLimit;
        const int shown = folded ? kChipPreviewLimit : matching.size();
        for (int index = 0; index < shown; ++index) {
            const QString rule = matching.at(index);
            auto *chip = new RuleChip(rule, iconForRule(rule, glyph_, tone_), static_cast<bool>(remove_), details_);
            if (rule.startsWith("processName:")) {
                ApplicationIcons::resolve(ruleValue(rule), chip, [chip](const QIcon &icon) { chip->setIcon(icon); });
            }
            if (remove_) {
                const auto remove = remove_;
                QObject::connect(chip, &QAbstractButton::clicked, chip, [remove, rule] { remove(rule); });
            }
            chips_->addWidget(chip);
        }
        if (folded || showAll_) {
            auto *toggle = new QPushButton(folded ? RouteProfileSimpleEditor::tr("+%1 more").arg(matching.size() - shown)
                                                  : RouteProfileSimpleEditor::tr("Show less"),
                                           details_);
            toggle->setObjectName("routeChipMore");
            toggle->setCursor(Qt::PointingHandCursor);
            QObject::connect(toggle, &QPushButton::clicked, this, [this] {
                showAll_ = !showAll_;
                rebuildChips();
            });
            chips_->addWidget(toggle);
        }
    }

    MaterialIcon::Glyph glyph_;
    QColor tone_;
    QStringList rules_;
    std::function<void(const QString &)> remove_;
    QWidget *details_ = nullptr;
    FlowLayout *chips_ = nullptr;
    QLineEdit *filter_ = nullptr;
    bool showAll_ = false;
};


QString ruleKindLabel(const QString &kind) {
    if (kind == QStringLiteral("domain")) return RouteProfileSimpleEditor::tr("domains");
    if (kind == QStringLiteral("suffix")) return RouteProfileSimpleEditor::tr("suffixes");
    if (kind == QStringLiteral("keyword")) return RouteProfileSimpleEditor::tr("keywords");
    if (kind == QStringLiteral("regex")) return RouteProfileSimpleEditor::tr("regexes");
    if (kind == QStringLiteral("ruleset")) return RouteProfileSimpleEditor::tr("rule sets");
    if (kind == QStringLiteral("ip")) return RouteProfileSimpleEditor::tr("addresses");
    return RouteProfileSimpleEditor::tr("processes");
}

QStringList cleanedRules(const QString &rules) {
    QStringList result;
    for (const QString &line : rules.split('\n')) {
        const QString clean = line.trimmed();
        if (!clean.isEmpty() && !result.contains(clean)) result.append(clean);
    }
    return result;
}

} // namespace

RouteProfileSimpleEditor::RouteProfileSimpleEditor(QWidget *parent) : QWidget(parent) {
    setObjectName("routeSimpleEditor");
    auto *root = new QHBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(10);

    auto *sidebar = new QFrame(this);
    sidebar->setObjectName("routeSidebar");
    sidebar->setFixedWidth(228);
    auto *side = new QVBoxLayout(sidebar);
    side->setContentsMargins(10, 12, 10, 10);
    side->setSpacing(6);
    auto *sideTitle = new QLabel(tr("Routing actions"), sidebar);
    sideTitle->setObjectName("routeSideTitle");
    side->addWidget(sideTitle);
    side->addSpacing(8);

    sidebar_ = sidebar;
    // A subscription can add a lot of actions, so the list scrolls instead of
    // pushing the statistics off the bottom of the column.
    auto *buttonsHost = new QWidget(sidebar);
    sideLayout_ = new QVBoxLayout(buttonsHost);
    sideLayout_->setContentsMargins(0, 0, 0, 0);
    sideLayout_->setSpacing(6);
    sideLayout_->addStretch();
    auto *sideScroll = new QScrollArea(sidebar);
    sideScroll->setObjectName("routeSideScroll");
    sideScroll->setWidget(buttonsHost);
    sideScroll->setWidgetResizable(true);
    sideScroll->setFrameShape(QFrame::NoFrame);
    sideScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    sideScroll->viewport()->setAutoFillBackground(false);
    buttonsHost->setAutoFillBackground(false);
    side->addWidget(sideScroll, 1);
    rebuildSidebar();

    addViaButton_ = new AddActionButton(tr("Add profile"), QColor(Cyan), sidebar);
    connect(addViaButton_, &QAbstractButton::clicked, this, &RouteProfileSimpleEditor::addViaBucket);
    side->addWidget(addViaButton_);
    side->addSpacing(4);

    auto *stats = new QFrame(sidebar);
    stats->setObjectName("routeStats");
    auto *statsLayout = new QGridLayout(stats);
    statsLayout->setContentsMargins(12, 10, 12, 10);
    auto *statsTitle = new QLabel(tr("Profile statistics"), stats);
    statsTitle->setObjectName("routeSideTitle");
    statsLayout->addWidget(statsTitle, 0, 0, 1, 2);
    auto *totalText = new QLabel(tr("Total rules"), stats);
    totalText->setObjectName("routeMuted");
    statsLayout->addWidget(totalText, 1, 0);
    totalLabel_ = new QLabel("0", stats);
    totalLabel_->setObjectName("routeStatsValue");
    statsLayout->addWidget(totalLabel_, 1, 1, Qt::AlignRight);
    side->addWidget(stats);
    root->addWidget(sidebar);

    auto *content = new QWidget(this);
    content->setObjectName("routeTransparent");
    auto *contentLayout = new QVBoxLayout(content);
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->setSpacing(8);
    auto *top = new QHBoxLayout;
    auto *headerIcon = new QLabel(content);
    headerIcon->setObjectName("routeHeaderIcon");
    headerIcon->setProperty("routeHeaderIcon", true);
    headerIcon->setFixedSize(26, 26);
    top->addWidget(headerIcon);
    auto *titles = new QVBoxLayout;
    titles->setSpacing(1);
    heading_ = new QLabel(content);
    heading_->setObjectName("routeHeading");
    description_ = new QLabel(content);
    description_->setObjectName("routeMuted");
    titles->addWidget(heading_);
    titles->addWidget(description_);
    top->addLayout(titles, 1);
    auto *bulkEditButton = new QPushButton(tr("Paste list"), content);
    bulkEditButton->setObjectName("routeBulkEditButton");
    bulkEditButton->setIcon(MaterialIcon::icon(MaterialIcon::Glyph::List, QColor("#DDE2E7"), 16));
    bulkEditButton->setToolTip(tr("Edit every rule of this action as plain text."));
    connect(bulkEditButton, &QPushButton::clicked, this, &RouteProfileSimpleEditor::bulkEdit);
    top->addWidget(bulkEditButton);
    auto *ruleOrderButton = new QPushButton(tr("Rule order"), content);
    ruleOrderButton->setObjectName("routeSecondaryButton");
    ruleOrderButton->setIcon(MaterialIcon::icon(MaterialIcon::Glyph::SwapVertical, QColor("#DDE2E7"), 16));
    connect(ruleOrderButton, &QPushButton::clicked, this, &RouteProfileSimpleEditor::advancedEditorRequested);
    top->addWidget(ruleOrderButton);
    contentLayout->addLayout(top);

    auto *quick = new QFrame(content);
    quick->setObjectName("routeRuleCard");
    auto *quickRoot = new QVBoxLayout(quick);
    quickRoot->setContentsMargins(14, 12, 14, 12);
    quickRoot->setSpacing(8);
    auto *quickHeading = new QHBoxLayout;
    auto *quickIcon = new QLabel(quick);
    quickIcon->setPixmap(MaterialIcon::pixmap(MaterialIcon::Glyph::Bolt, QColor(Blue), 18));
    quickIcon->setFixedSize(22, 22);
    quickHeading->addWidget(quickIcon);
    auto *quickTitles = new QVBoxLayout;
    quickTitles->setSpacing(1);
    auto *quickHeadingTitle = new QLabel(tr("Quick options"), quick);
    quickHeadingTitle->setObjectName("routeSectionTitle");
    auto *quickHeadingSubtitle = new QLabel(tr("Common routing behavior."), quick);
    quickHeadingSubtitle->setObjectName("routeMuted");
    quickTitles->addWidget(quickHeadingTitle);
    quickTitles->addWidget(quickHeadingSubtitle);
    quickHeading->addLayout(quickTitles, 1);
    auto *quickExpander = new QToolButton(quick);
    quickExpander->setObjectName("routeIconButton");
    quickExpander->setCheckable(true);
    quickExpander->setChecked(true);
    quickExpander->setIcon(MaterialIcon::icon(MaterialIcon::Glyph::ChevronDown, QColor(Muted), 18));
    quickHeading->addWidget(quickExpander);
    quickRoot->addLayout(quickHeading);

    auto *quickDetails = new QWidget(quick);
    quickDetails->setObjectName("routeTransparent");
    auto *quickLayout = new QHBoxLayout(quickDetails);
    quickLayout->setContentsMargins(32, 2, 0, 0);
    quickLayout->setSpacing(10);
    auto *quickToggle = new ThronedToggle(false, quickDetails);
    localProxyToggle_ = quickToggle;
    quickLayout->addWidget(quickToggle, 0, Qt::AlignVCenter);
    auto *quickCopy = new QVBoxLayout;
    quickCopy->setSpacing(1);
    auto *quickTitle = new QLabel(tr("Route local proxy traffic through proxy"), quickDetails);
    quickTitle->setObjectName("routeSectionTitle");
    auto *quickSubtitle = new QLabel(tr("Also route mixed-in and socks-in traffic through this outbound."), quickDetails);
    quickSubtitle->setObjectName("routeMuted");
    quickCopy->addWidget(quickTitle);
    quickCopy->addWidget(quickSubtitle);
    quickLayout->addLayout(quickCopy, 1);
    connect(quickToggle, &QAbstractButton::toggled, this, [this](bool enabled) {
        localProxyTrafficEnabled_ = enabled;
        emit localProxyTrafficChanged(enabled);
    });
    quickRoot->addWidget(quickDetails);
    connect(quickExpander, &QToolButton::toggled, quick, [quickDetails, quickHeadingSubtitle, quickExpander](bool show) {
        quickDetails->setVisible(show);
        quickHeadingSubtitle->setVisible(show);
        quickExpander->setIcon(MaterialIcon::icon(show ? MaterialIcon::Glyph::ChevronDown : MaterialIcon::Glyph::ChevronRight,
                                                   QColor(Muted), 18));
    });
    quickOptionsCard_ = quick;

    auto *scroll = new QScrollArea(content);
    scroll->setObjectName("routeCardsScroll");
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto *cards = new QWidget(scroll);
    cards->setObjectName("routeTransparent");
    cardsLayout_ = new QVBoxLayout(cards);
    cardsLayout_->setContentsMargins(0, 0, 0, 0);
    cardsLayout_->setSpacing(9);
    cardsLayout_->addWidget(quickOptionsCard_);
    cardsLayout_->addStretch();
    scroll->setWidget(cards);
    contentLayout->addWidget(scroll, 1);
    root->addWidget(content, 1);

    themeManager()->RegisterStyle(this, QStringLiteral(R"(
#routeSimpleEditor, #routeTransparent, QScrollArea#routeCardsScroll, QScrollArea#routeCardsScroll > QWidget > QWidget {
    background: transparent;
}
QFrame#routeSidebar, QFrame#routeStats, QFrame#routeRuleCard {
    background: #171B21;
    border: 1px solid #2F3136;
    border-radius: 7px;
}
QLabel#routeSideTitle, QLabel#routeSectionTitle { color: #F1F3F5; font-weight: 600; }
QLabel#routeHeading { color: #F1F3F5; font-size: 17px; font-weight: 500; min-height: 26px; }
QScrollArea#routeSideScroll, QScrollArea#routeSideScroll > QWidget > QWidget { background: transparent; }
QLabel#routeMuted { color: #A4ABB4; font-size: 13px; }
QLabel#routeEmpty { color: #747C86; font-size: 13px; font-style: italic; }
QLabel#routeWarning { color: #E8B455; font-size: 13px; }
QLabel#routeStatsValue { color: #F1F3F5; font-weight: 700; }
QLabel#routeCountPill {
    color: #DDE2E7; background: #272C33; border: none; border-radius: 5px;
    padding: 2px 8px; margin-left: 4px;
}
QToolButton#routeIconButton { background: transparent; border: none; padding: 3px; }
QToolButton#routeIconButton:hover { background: #22272E; border-radius: 4px; }
QToolButton#routeChip {
    color: #E5E8EB; background: #222529; border: 1px solid #343941;
    border-radius: 5px; padding: 6px 9px;
}
QToolButton#routeChip:hover { background: #292E35; border-color: #4A535E; }
QPushButton#routeCardAddButton {
    color: #DDE2E7; background: #222529; border: 1px solid #343941;
    border-radius: 5px; padding: 6px 10px;
}
QPushButton#routeCardAddButton:hover { background: #292E35; border-color: #4A535E; }
QLineEdit#routeCardFilter {
    color: #E5E8EB; background: #171B21; border: 1px solid #2F3136;
    border-radius: 5px; padding: 5px 8px 5px 4px; font-size: 13px;
}
QLineEdit#routeCardFilter:focus { border-color: #237AE9; }
QPushButton#routeChipMore {
    color: #9CC7FF; background: #1B2634; border: 1px dashed #34506F;
    border-radius: 5px; padding: 7px 12px; min-height: 18px;
}
QPushButton#routeChipMore:hover { color: #C3DEFF; border-color: #4B7CAB; }
QPushButton#routePrimaryButton {
    color: white; background: #237AE9; border: 1px solid #3187F3;
    border-radius: 6px; padding: 8px 14px; font-weight: 600;
}
QPushButton#routePrimaryButton:hover { background: #2F86F1; }
QPushButton#routeSecondaryButton, QPushButton#routeBulkEditButton {
    color: #E1E4E8; background: #222529; border: 1px solid #2F3136;
    border-radius: 5px; padding: 7px 11px;
}
QPushButton#routeSecondaryButton:hover,
QPushButton#routeBulkEditButton:hover { background: #292E35; border-color: #4A535E; }
)"));

    selectAction(2);
}

QString RouteProfileSimpleEditor::dialogStyleSheet() {
    return QStringLiteral(R"(
QDialog#routeProfileEditor {
    background: #1B1E23; color: #F1F3F5;
    font-size: %BASE_FONT_PX%px;
}
QDialog#routeProfileEditor QWidget#routeBody { background: #1B1E23; }
/* Unscoped so every dialog that installs this sheet gets the same chrome. */
QFrame#titleBar {
    background: #1B1E23; border: none; border-bottom: 1px solid #2F3136;
}
QLabel#titleBrand { font-size: 18px; font-weight: 700; }
QLabel#titleContext { color: #D8DCE1; font-size: 14px; font-weight: 650; }
QFrame#vSeparator { background: #2F3136; border: none; }
QFrame#titleBar QToolButton {
    color: #F1F3F5; background: transparent; border: none;
}
QFrame#titleBar QToolButton:hover { background: #292D33; }
QFrame#titleBar QToolButton#titleClose:hover { background: #C42B35; }
QDialog#routeProfileEditor QFrame#routeFieldBox { background: transparent; border: none; }
QDialog#routeProfileEditor QGroupBox#generalBox, QFrame#routeProfileHeader {
    background: #171B21; border: 1px solid #2F3136; border-radius: 7px;
}
QDialog#routeProfileEditor QGroupBox#generalBox { margin: 0; }
QDialog#routeProfileEditor QGroupBox#generalBox QLabel,
QDialog#routeProfileEditor QLabel#routeFieldLabel { color: #A4ABB4; font-size: 13px; }
QDialog#routeProfileEditor QLineEdit, QDialog#routeProfileEditor QComboBox,
QDialog#routeProfileEditor QPlainTextEdit, QDialog#routeProfileEditor QTextEdit,
QDialog#routeProfileEditor QTextBrowser, QDialog#routeProfileEditor QListWidget,
QDialog#routeProfileEditor QTableView {
    color: #F1F3F5; background: #171B21; border: 1px solid #2F3136;
    border-radius: 6px; padding: 6px 9px; selection-background-color: #237AE9;
}
QDialog#routeProfileEditor QComboBox { padding-right: 26px; }
QDialog#routeProfileEditor QComboBox#def_out { padding-right: 34px; }
/* Styling the box takes the arrow away from the platform style, so it has to be
   supplied here or Qt falls back to a bevelled Windows 95 drop-down button. */
QDialog#routeProfileEditor QComboBox::drop-down {
    subcontrol-origin: padding; subcontrol-position: center right;
    width: 24px; border: none; background: transparent;
}
QDialog#routeProfileEditor QComboBox#def_out::drop-down { width: 30px; }
QDialog#routeProfileEditor QComboBox::down-arrow {
    image: url("%CHEVRON_DOWN%"); width: 14px; height: 14px;
}
QDialog#routeProfileEditor QLineEdit:focus, QDialog#routeProfileEditor QComboBox:focus,
QDialog#routeProfileEditor QPlainTextEdit:focus, QDialog#routeProfileEditor QTextEdit:focus,
QDialog#routeProfileEditor QTableView:focus {
    border-color: #237AE9;
}
QDialog#routeProfileEditor QTabWidget::pane { border: none; background: transparent; }
QDialog#routeProfileEditor QTabWidget#routeModeStack,
QDialog#routeProfileEditor QTabWidget#routeModeStack > QWidget > QWidget { background: transparent; }
QDialog#routeProfileEditor QTabBar { background: transparent; qproperty-drawBase: 0; }
QDialog#routeProfileEditor QTabBar::tab {
    color: #A4ABB4; background: #171B21; border: 1px solid #2F3136;
    padding: 7px 24px; min-width: 78px;
}
QDialog#routeProfileEditor QTabBar::tab:first { border-top-left-radius: 6px; border-bottom-left-radius: 6px; }
QDialog#routeProfileEditor QTabBar::tab:last { border-top-right-radius: 6px; border-bottom-right-radius: 6px; }
QDialog#routeProfileEditor QTabBar::tab:selected { color: white; background: #237AE9; border-color: #3187F3; }
QDialog#routeProfileEditor QTabBar#routeModeTabs {
    background: #171B21; border: 1px solid #2F3136; border-radius: 6px;
}
QDialog#routeProfileEditor QTabBar#routeModeTabs::tab {
    background: transparent; border: none; color: #A4ABB4;
    min-width: 92px; min-height: 28px; margin: 3px 1px; padding: 0 4px;
}
QDialog#routeProfileEditor QTabBar#routeModeTabs::tab:first { margin-left: 3px; }
QDialog#routeProfileEditor QTabBar#routeModeTabs::tab:last { margin-right: 3px; }
QDialog#routeProfileEditor QTabBar#routeModeTabs::tab:selected {
    background: #237AE9; border: 1px solid #4193F4; border-radius: 5px; color: white;
}
QDialog#routeProfileEditor QTabBar#applicationPickerTabs::tab {
    border-radius: 6px; margin-right: 6px; padding: 7px 16px; min-width: 118px;
}
QDialog#routeProfileEditor QHeaderView::section {
    color: #A4ABB4; background: #171B21; border: none; border-bottom: 1px solid #2F3136;
    padding: 7px 9px; font-weight: 600;
}
QDialog#routeProfileEditor QTableView::item { border: none; padding: 4px 7px; }
QDialog#routeProfileEditor QTableView::item:selected { color: white; background: #193E69; }
QDialog#routeProfileEditor QHeaderView::down-arrow,
QDialog#routeProfileEditor QHeaderView::up-arrow { margin-top: 4px; }
QDialog#routeProfileEditor QPushButton#applicationPickerReload { padding: 5px 12px; min-width: 78px; }
QDialog#routeProfileEditor QDialogButtonBox QPushButton,
QDialog#routeProfileEditor QPushButton#routeSecondaryButton {
    color: #F1F3F5; background: #222529; border: 1px solid #2F3136;
    border-radius: 6px; padding: 8px 24px; min-width: 92px;
}
QDialog#routeProfileEditor QDialogButtonBox QPushButton:hover,
QDialog#routeProfileEditor QPushButton#routeSecondaryButton:hover { background: #292E35; }
QDialog#routeProfileEditor QPushButton#routeSaveButton {
    color: white; background: #237AE9; border: 1px solid #3187F3;
    border-radius: 6px; padding: 8px 24px; min-width: 112px; font-weight: 600;
}
QDialog#routeProfileEditor QPushButton#routeSaveButton:hover { background: #2F86F1; }
QDialog#routeProfileEditor QScrollBar:vertical {
    background: transparent; width: 11px; margin: 2px 1px;
}
QDialog#routeProfileEditor QScrollBar::handle:vertical {
    background: #3A424D; min-height: 34px; border-radius: 4px; margin: 2px 1px;
}
QDialog#routeProfileEditor QScrollBar::handle:vertical:hover { background: #4B5663; }
QDialog#routeProfileEditor QScrollBar::add-line:vertical,
QDialog#routeProfileEditor QScrollBar::sub-line:vertical { height: 0; background: transparent; }
QDialog#routeProfileEditor QScrollBar::add-page:vertical,
QDialog#routeProfileEditor QScrollBar::sub-page:vertical { background: transparent; }
QDialog#routeProfileEditor QScrollBar:horizontal {
    background: transparent; height: 11px; margin: 1px 2px;
}
QDialog#routeProfileEditor QScrollBar::handle:horizontal {
    background: #3A424D; min-width: 34px; border-radius: 4px; margin: 1px 2px;
}
QDialog#routeProfileEditor QScrollBar::handle:horizontal:hover { background: #4B5663; }
QDialog#routeProfileEditor QScrollBar::add-line:horizontal,
QDialog#routeProfileEditor QScrollBar::sub-line:horizontal { width: 0; background: transparent; }
QDialog#routeProfileEditor QScrollBar::add-page:horizontal,
QDialog#routeProfileEditor QScrollBar::sub-page:horizontal { background: transparent; }
QDialog#routeProfileEditor QWidget#routeAdvancedHost,
QDialog#routeProfileEditor QWidget#routeAdvancedSummaryHost,
QDialog#routeProfileEditor QWidget#routeAdvancedSummary,
QDialog#routeProfileEditor QWidget#routeAdvancedDetail,
QDialog#routeProfileEditor QStackedWidget#routeAdvancedStack,
QDialog#routeProfileEditor QScrollArea#routeAdvancedScroll { background: transparent; border: none; }
QDialog#routeProfileEditor QFrame#routeAdvancedSidebar,
QDialog#routeProfileEditor QFrame#routeAdvancedStats {
    background: #171B21; border: 1px solid #2F3136; border-radius: 7px;
}
QDialog#routeProfileEditor QLabel#routeSideTitle { color: #F1F3F5; font-weight: 650; }
QDialog#routeProfileEditor QLabel#routeStatsValue { color: #F1F3F5; font-weight: 700; }
QDialog#routeProfileEditor QLabel#routeAdvancedHero {
    color: #F1F3F5; font-size: 18px; font-weight: 700;
}
QDialog#routeProfileEditor QFrame#routeAdvancedNotice,
QDialog#routeProfileEditor QFrame#routeFallbackCard,
QDialog#routeProfileEditor QFrame#routeOrderedRule {
    background: #171B21; border: 1px solid #2F3136; border-radius: 7px;
}
QDialog#routeProfileEditor QFrame#routeOrderedRule:hover { border-color: #45505C; }
QDialog#routeProfileEditor QLabel#routeDragHandle { color: #617181; font-size: 16px; }
QDialog#routeProfileEditor QLabel#routePriorityPill {
    color: #DDE7F0; background: #252B33; border: 1px solid #343C46; border-radius: 6px;
    font-weight: 700;
}
QDialog#routeProfileEditor QLabel#routeOrderedTitle { color: #F1F3F5; font-weight: 700; }
QDialog#routeProfileEditor QLabel#routeActionPill,
QDialog#routeProfileEditor QLabel#routeConditionPill {
    color: #CFE6FF; background: #193452; border: 1px solid #285C8F;
    border-radius: 5px; padding: 4px 8px;
}
QDialog#routeProfileEditor QLabel#routeActionPill { font-weight: 700; }
QDialog#routeProfileEditor QLabel#routeActionPill[tone="green"],
QDialog#routeProfileEditor QLabel#routeConditionPill[tone="green"] {
    color: #BDF7D6; background: #17392A; border-color: #276744;
}
QDialog#routeProfileEditor QLabel#routeActionPill[tone="red"],
QDialog#routeProfileEditor QLabel#routeConditionPill[tone="red"] {
    color: #FFD0D4; background: #402126; border-color: #7F343C;
}
QDialog#routeProfileEditor QLabel#routeActionPill[tone="purple"],
QDialog#routeProfileEditor QLabel#routeConditionPill[tone="purple"] {
    color: #E2D1FF; background: #2B2142; border-color: #5B4089;
}
QDialog#routeProfileEditor QLabel#routeActionPill[tone="cyan"],
QDialog#routeProfileEditor QLabel#routeConditionPill[tone="cyan"] {
    color: #C9F4FF; background: #173744; border-color: #276275;
}
QDialog#routeProfileEditor QToolButton#routeAdvancedIconButton {
    background: #222529; border: 1px solid #2F3136; border-radius: 5px; padding: 6px;
}
QDialog#routeProfileEditor QToolButton#routeAdvancedIconButton:hover {
    background: #292E35; border-color: #4A535E;
}
QDialog#routeProfileEditor QToolButton#routeAdvancedIconButton:disabled {
    background: transparent; border-color: transparent;
}
QDialog#routeProfileEditor QToolButton#routeAdvancedMoreButton {
    background: transparent; border: 1px solid transparent; border-radius: 5px; padding: 6px;
}
QDialog#routeProfileEditor QToolButton#routeAdvancedMoreButton:hover {
    background: #292E35; border-color: #4A535E;
}
/* InstantPopup draws its own arrow next to the glyph, which reads as a second
   icon stuck to the first. */
QDialog#routeProfileEditor QToolButton#routeAdvancedMoreButton::menu-indicator {
    image: none; width: 0; height: 0;
}
/* The per-rule JSON button sits inside a card, so it must not claim the wide
   min-width the dialog's footer buttons use. */
QDialog#routeProfileEditor QPushButton#routeCardJsonButton {
    color: #C6CCD4; background: #222529; border: 1px solid #2F3136;
    border-radius: 5px; padding: 6px 11px; min-width: 0;
}
QDialog#routeProfileEditor QPushButton#routeCardJsonButton:hover {
    background: #292E35; border-color: #4A535E;
}
/* The per-rule detail page is still the original Qt Designer layout; card
   chrome brings its group boxes, list and buttons in line with the rest. */
QDialog#routeProfileEditor QGroupBox {
    background: #171B21; border: 1px solid #2F3136; border-radius: 7px;
    margin-top: 9px; padding: 12px 10px 10px 10px; font-weight: 650;
}
QDialog#routeProfileEditor QGroupBox::title {
    subcontrol-origin: margin; subcontrol-position: top left;
    left: 12px; padding: 0 5px; color: #F1F3F5; background: #171B21;
}
QDialog#routeProfileEditor QWidget#routeAdvancedDetail QPushButton {
    color: #E1E4E8; background: #222529; border: 1px solid #2F3136;
    border-radius: 6px; padding: 7px 14px; min-width: 82px;
}
QDialog#routeProfileEditor QWidget#routeAdvancedDetail QPushButton:hover {
    background: #292E35; border-color: #4A535E;
}
QDialog#routeProfileEditor QListWidget#route_items { padding: 4px; }
QDialog#routeProfileEditor QListWidget#route_items::item {
    padding: 7px 9px; border-radius: 5px; border: none;
}
QDialog#routeProfileEditor QListWidget#route_items::item:hover { background: #22272E; }
QDialog#routeProfileEditor QListWidget#route_items::item:selected {
    color: white; background: #237AE9;
}
QDialog#routeProfileEditor QTabWidget#rule_attr_tabs::pane {
    background: transparent; border: 1px solid #2F3136; border-radius: 6px; top: -1px;
}
QDialog#routeProfileEditor QTabBar#ruleAttrTabBar::tab {
    color: #A4ABB4; background: #171B21; border: 1px solid #2F3136;
    border-radius: 5px; padding: 5px 12px; margin-right: 4px; min-width: 0;
}
QDialog#routeProfileEditor QTabBar#ruleAttrTabBar::tab:selected {
    color: white; background: #237AE9; border-color: #3187F3;
}
QDialog#routeProfileEditor QPushButton#routeLinkButton {
    color: #4DA3FF; background: transparent; border: none; padding: 4px 6px;
}
QDialog#routeProfileEditor QPushButton#routeLinkButton:hover { color: #7BBAFF; text-decoration: underline; }
QDialog#routeProfileEditor QMenu {
    color: #F1F3F5; background: #1B1E23; border: 1px solid #3A4048; border-radius: 6px;
    padding: 5px;
}
QDialog#routeProfileEditor QMenu::item { padding: 7px 26px 7px 10px; border-radius: 4px; }
QDialog#routeProfileEditor QMenu::item:selected { background: #263B55; }
QDialog#routeProfileEditor QMenu::separator { height: 1px; background: #2F3136; margin: 4px 7px; }
)");
}

void RouteProfileSimpleEditor::setRules(int action, const QString &rules) {
    rules_[action] = cleanedRules(rules);
    updateTotalCount();
    updateActionButtons();
    if (selectedAction_ == action) rebuild();
}

QString RouteProfileSimpleEditor::rules(int action) const {
    const QStringList values = rules_.value(action);
    return values.isEmpty() ? QString() : values.join('\n') + '\n';
}

void RouteProfileSimpleEditor::setAdvancedRuleCount(int count) {
    advancedRuleCount_ = count;
    updateTotalCount();
    rebuild();
}

void RouteProfileSimpleEditor::setAdvancedRules(const QStringList &names) {
    advancedRules_.clear();
    for (const QString &name : names) advancedRules_.append(QStringLiteral("raw:") + name);
    advancedRuleCount_ = advancedRules_.size();
    updateTotalCount();
    rebuild();
}

void RouteProfileSimpleEditor::setRuleSetCatalog(const QStringList &names) {
    ruleSetCatalog_ = names;
    ruleSetCatalog_.removeDuplicates();
    ruleSetCatalog_.sort(Qt::CaseInsensitive);
}

void RouteProfileSimpleEditor::setLocalProxyTrafficEnabled(bool enabled) {
    localProxyTrafficEnabled_ = enabled;
    if (localProxyToggle_) {
        const QSignalBlocker blocker(localProxyToggle_);
        localProxyToggle_->setChecked(enabled);
    }
}

QString RouteProfileSimpleEditor::viaLabelFor(int action) const {
    if (!isViaAction(action)) return {};
    const int profileID = viaProfileOf(action);
    for (const auto &[id, label] : viaBuckets_) {
        if (id == profileID) return label;
    }
    return {};
}

void RouteProfileSimpleEditor::selectAction(int action) {
    selectedAction_ = action;
    updateActionButtons();
    rebuild();
}

void RouteProfileSimpleEditor::setViaBuckets(const QList<QPair<int, QString>> &buckets) {
    viaBuckets_ = buckets;
    rebuildSidebar();
    // The selected bucket may have just been removed from under the selection.
    if (isViaAction(selectedAction_) && !actionButtons_.contains(selectedAction_)) selectAction(2);
    else rebuild();
}

void RouteProfileSimpleEditor::setViaCatalog(const QList<QPair<int, QString>> &profiles) {
    viaCatalog_ = profiles;
    rebuildSidebar();
}

void RouteProfileSimpleEditor::rebuildSidebar() {
    if (sideLayout_ == nullptr) return;
    while (QLayoutItem *item = sideLayout_->takeAt(0)) {
        if (item->widget()) item->widget()->deleteLater();
        delete item;
    }
    actionButtons_.clear();

    const struct { int action; const char *title; MaterialIcon::Glyph glyph; const char *tone; } actions[] = {
        {0, QT_TR_NOOP("Direct"), MaterialIcon::Glyph::Direct, Green},
        {2, QT_TR_NOOP("Proxy"), MaterialIcon::Glyph::Shield, Blue},
        {1, QT_TR_NOOP("Block"), MaterialIcon::Glyph::Block, Red},
        {3, QT_TR_NOOP("WARP bypass"), MaterialIcon::Glyph::SwapVertical, Purple},
    };
    for (const auto &item : actions) {
        auto *button = new ActionButton(item.glyph, tr(item.title), QColor(item.tone), sidebar_);
        actionButtons_[item.action] = button;
        connect(button, &QAbstractButton::clicked, this, [this, action = item.action] { selectAction(action); });
        sideLayout_->addWidget(button);
    }

    if (!viaBuckets_.isEmpty()) {
        auto *divider = new QFrame(sidebar_);
        divider->setObjectName("routeSidebarDivider");
        divider->setFixedHeight(1);
        sideLayout_->addWidget(divider);
    }
    for (const auto &[profileID, label] : viaBuckets_) {
        const int action = viaAction(profileID);
        auto *button = new ActionButton(MaterialIcon::Glyph::Public, label, QColor(Cyan), sidebar_);
        button->setScrollsWhenTooLong(true);
        button->setToolTip(tr("Rules here leave through %1. Right-click to remove the action.").arg(label));
        button->setContextMenuPolicy(Qt::CustomContextMenu);
        actionButtons_[action] = button;
        connect(button, &QAbstractButton::clicked, this, [this, action] { selectAction(action); });
        connect(button, &QWidget::customContextMenuRequested, this, [this, profileID, label](const QPoint &) {
            if (QMessageBox::question(this, tr("Remove action"),
                                      tr("Remove the action for %1 and every rule in it?").arg(label))
                != QMessageBox::Yes)
                return;
            emit viaBucketRemoved(profileID);
        });
        sideLayout_->addWidget(button);
    }

    sideLayout_->addStretch();
}

void RouteProfileSimpleEditor::addViaBucket() {
    QList<QPair<int, QString>> available;
    for (const auto &entry : viaCatalog_) {
        const bool taken = std::any_of(viaBuckets_.begin(), viaBuckets_.end(),
                                       [&entry](const auto &b) { return b.first == entry.first; });
        if (!taken) available << entry;
    }
    if (available.isEmpty()) {
        QMessageBox::information(this, tr("Add profile"),
                                 tr("Every profile already has an action here."));
        return;
    }

    QStringList labels;
    labels.reserve(available.size());
    for (const auto &[id, label] : available) labels << label;
    bool picked = false;
    const auto choice = QInputDialog::getItem(this, tr("Add profile"),
                                              tr("Rules in the new action leave through this profile:"),
                                              labels, 0, false, &picked);
    if (!picked) return;
    const int index = static_cast<int>(labels.indexOf(QStringView(choice)));
    if (index < 0) return;
    emit viaBucketAdded(available[index].first);
}

void RouteProfileSimpleEditor::updateActionButtons() {
    for (auto it = actionButtons_.begin(); it != actionButtons_.end(); ++it) {
        it.value()->setChecked(it.key() == selectedAction_);
        if (auto *button = dynamic_cast<ActionButton *>(it.value())) button->setCount(rules_.value(it.key()).size());
    }
}

void RouteProfileSimpleEditor::rebuild() {
    while (cardsLayout_->count() > 2) {
        QLayoutItem *item = cardsLayout_->takeAt(1);
        if (item->widget()) item->widget()->deleteLater();
        delete item;
    }
    const auto presentation = actionPresentation(selectedAction_, viaLabelFor(selectedAction_));
    heading_->setText(presentation.title);
    description_->setText(presentation.description);
    if (auto *icon = findChild<QLabel *>("routeHeaderIcon"))
        icon->setPixmap(MaterialIcon::pixmap(presentation.glyph, presentation.tone, 24));

    quickOptionsCard_->setVisible(selectedAction_ == 2);

    QStringList applications;
    QStringList domains;
    QStringList ruleSets;
    QStringList network;
    for (const QString &rule : rules_.value(selectedAction_)) {
        const QString prefix = rulePrefix(rule);
        if (prefix == "processName" || prefix == "processPath") applications.append(rule);
        else if (prefix == "ip" || (prefix == "ruleset" && ruleValue(rule).startsWith("geoip-"))) network.append(rule);
        else if (prefix == "ruleset") ruleSets.append(rule);
        else domains.append(rule);
    }
    const auto remove = [this](const QString &rule) { removeRule(rule); };
    cardsLayout_->insertWidget(cardsLayout_->count() - 1,
        new RuleCard(tr("Applications"), tr("Match by installed app, running process, or executable."), MaterialIcon::Glyph::Apps,
                      QColor(Blue), applications, true, remove, [this] { addApplicationRules(); }, this));
    cardsLayout_->insertWidget(cardsLayout_->count() - 1,
        new RuleCard(tr("Domains"), tr("Match domain names, suffixes, keywords, and regexes."), MaterialIcon::Glyph::Public,
                      QColor(Cyan), domains, true, remove, [this] { addRule(QStringLiteral("domain")); }, this));
    cardsLayout_->insertWidget(cardsLayout_->count() - 1,
        new RuleCard(tr("Rule sets"), tr("Remote geosite lists, matched as a whole."), MaterialIcon::Glyph::List,
                      QColor(Purple), ruleSets, true, remove, [this] { addRule(QStringLiteral("ruleset")); }, this));
    cardsLayout_->insertWidget(cardsLayout_->count() - 1,
        new RuleCard(tr("IP addresses & ranges"), tr("Match destination IP addresses, CIDR ranges, and geoip lists."), MaterialIcon::Glyph::Process,
                      QColor(Green), network, false, remove, [this] { addRule(QStringLiteral("network")); }, this));
    cardsLayout_->insertWidget(cardsLayout_->count() - 1,
        new RuleCard(tr("Advanced / raw rules"), tr("Ordered conditions, exact priority, and lossless JSON."), MaterialIcon::Glyph::List,
                      QColor(Cyan), advancedRules_, false, {}, [this] { emit advancedEditorRequested(); }, this));
}

void RouteProfileSimpleEditor::bulkEdit() {
    const auto presentation = actionPresentation(selectedAction_, viaLabelFor(selectedAction_));
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Paste rule list"));
    dialog.setObjectName("routeAddDialog");
    auto *layout = new QVBoxLayout(&dialog);
    layout->setSpacing(9);

    auto *hint = new QLabel(tr("Every rule of “%1”, one per line. Editing here replaces the whole list, "
                               "and each entry lands in its own card automatically.").arg(presentation.title), &dialog);
    hint->setObjectName("routeMuted");
    hint->setWordWrap(true);
    layout->addWidget(hint);
    auto *legend = new QLabel(tr("Prefixes: domain:  suffix:  keyword:  regex:  ruleset:  ip:  processName:  processPath:\n"
                                 "A line without a prefix is recognised on its own — sing-box spellings "
                                 "(domain_suffix, process_name, rule_set…) are accepted too."), &dialog);
    legend->setObjectName("routeEmpty");
    legend->setWordWrap(true);
    layout->addWidget(legend);

    auto *editor = new QPlainTextEdit(&dialog);
    editor->setPlaceholderText(QStringLiteral("domain:example.com\nsuffix:example.org\nprocessName:Discord.exe\n"
                                              "ruleset:geosite-openai\nip:198.51.100.0/24"));
    QStringList sorted = rules_.value(selectedAction_);
    std::sort(sorted.begin(), sorted.end(), [](const QString &left, const QString &right) {
        const QString leftKind = rulePrefix(left);
        const QString rightKind = rulePrefix(right);
        if (leftKind != rightKind) return leftKind < rightKind;
        return ruleValue(left).compare(ruleValue(right), Qt::CaseInsensitive) < 0;
    });
    editor->setPlainText(sorted.join('\n'));
    editor->setMinimumSize(560, 300);
    layout->addWidget(editor, 1);

    auto *summary = new QLabel(&dialog);
    summary->setObjectName("routeMuted");
    summary->setWordWrap(true);
    layout->addWidget(summary);
    auto *warning = new QLabel(&dialog);
    warning->setObjectName("routeWarning");
    warning->setWordWrap(true);
    warning->hide();
    layout->addWidget(warning);

    const auto parse = [editor] {
        QStringList parsed;
        QStringList rejected;
        for (const QString &line : editor->toPlainText().split('\n')) {
            const QString clean = line.trimmed();
            if (clean.isEmpty() || clean.startsWith(QLatin1Char('#')) || clean.startsWith(QStringLiteral("//")))
                continue;
            const QString rule = Configs::NormalizeRuleLine(clean);
            if (rule.isEmpty()) rejected.append(clean);
            else if (!parsed.contains(rule)) parsed.append(rule);
        }
        return std::pair{parsed, rejected};
    };
    const auto refreshSummary = [parse, summary, warning] {
        const auto [parsed, rejected] = parse();
        QMap<QString, int> counts;
        for (const QString &rule : parsed) {
            QString kind = rulePrefix(rule);
            if (kind == QStringLiteral("processPath")) kind = QStringLiteral("processName");
            ++counts[kind];
        }
        QStringList parts;
        for (auto it = counts.cbegin(); it != counts.cend(); ++it)
            parts.append(QStringLiteral("%1: %2").arg(ruleKindLabel(it.key())).arg(it.value()));
        summary->setText(parts.isEmpty() ? RouteProfileSimpleEditor::tr("Nothing to add yet.")
                                         : parts.join(QStringLiteral(" · ")));
        warning->setVisible(!rejected.isEmpty());
        if (!rejected.isEmpty())
            warning->setText(RouteProfileSimpleEditor::tr("%1 line(s) will be dropped — add a prefix to keep them: %2")
                                 .arg(rejected.size()).arg(rejected.mid(0, 3).join(QStringLiteral(", "))));
    };
    connect(editor, &QPlainTextEdit::textChanged, &dialog, refreshSummary);
    refreshSummary();

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Cancel | QDialogButtonBox::Ok);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("Apply list"));
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);
    dialog.setStyleSheet(styleSheet());
    FitWindowToScreen(&dialog, QSize(660, 560));
    editor->setFocus();
    if (dialog.exec() != QDialog::Accepted) return;

    const auto [parsed, rejected] = parse();
    if (parsed == rules_.value(selectedAction_)) return;
    rules_[selectedAction_] = parsed;
    updateTotalCount();
    emit rulesChanged(selectedAction_, rules(selectedAction_));
    updateActionButtons();
    rebuild();
}

void RouteProfileSimpleEditor::addApplicationRules() {
    ApplicationPickerDialog picker(this);
    themeManager()->RegisterStyle(&picker, RouteProfileSimpleEditor::dialogStyleSheet());
    if (picker.exec() != QDialog::Accepted) return;
    bool changed = false;
    for (const QString &rule : picker.selectedRules()) {
        if (!rules_[selectedAction_].contains(rule)) {
            rules_[selectedAction_].append(rule);
            changed = true;
        }
    }
    if (!changed) return;
    emit rulesChanged(selectedAction_, rules(selectedAction_));
    updateActionButtons();
    rebuild();
}

void RouteProfileSimpleEditor::addRule(const QString &section) {
    QDialog dialog(this);
    dialog.setWindowTitle(section == QStringLiteral("ruleset") ? tr("Add rule sets") : tr("Add routing rule"));
    dialog.setObjectName("routeAddDialog");
    auto *layout = new QVBoxLayout(&dialog);
    auto *hint = new QLabel(section == QStringLiteral("network")
        ? tr("Paste one or more destination IP addresses or CIDR ranges, one per line.")
        : section == QStringLiteral("ruleset")
        ? tr("Pick one or more geosite lists. Each is matched as a whole.")
        : tr("Paste one or more values, one per line, or choose several rule sets."), &dialog);
    hint->setObjectName("routeMuted");
    layout->addWidget(hint);
    auto *type = new QComboBox(&dialog);
    if (section == QStringLiteral("network")) {
        type->addItem(tr("IP / CIDR"), "ip");
        type->addItem(tr("GeoIP rule set"), "geoip");
    } else if (section == QStringLiteral("ruleset")) {
        // This card holds nothing else: a geoip set would be sorted into the address
        // card instead, so offering the choice here would only misplace the entry.
        type->addItem(tr("Geosite rule set"), "geosite");
        type->hide();
    } else {
        type->addItem(tr("Domain"), "domain");
        type->addItem(tr("Domain suffix"), "suffix");
        type->addItem(tr("Domain keyword"), "keyword");
        type->addItem(tr("Domain regex"), "regex");
        type->addItem(tr("Geosite rule set"), "geosite");
    }
    layout->addWidget(type);
    auto *valueRow = new QHBoxLayout;
    auto *value = new QPlainTextEdit(&dialog);
    value->setPlaceholderText(tr("One value per line…"));
    value->setMinimumHeight(112);
    valueRow->addWidget(value, 1);
    layout->addLayout(valueRow);

    auto *catalog = new QWidget(&dialog);
    auto *catalogLayout = new QVBoxLayout(catalog);
    catalogLayout->setContentsMargins(0, 0, 0, 0);
    catalogLayout->setSpacing(8);
    auto *catalogSearch = new QLineEdit(catalog);
    catalogSearch->setPlaceholderText(tr("Search rule sets…"));
    catalogSearch->addAction(MaterialIcon::icon(MaterialIcon::Glyph::Search, QColor(Muted), 17), QLineEdit::LeadingPosition);
    catalogLayout->addWidget(catalogSearch);
    auto *catalogList = new QListWidget(catalog);
    catalogList->setSelectionMode(QAbstractItemView::MultiSelection);
    catalogList->setMinimumHeight(220);
    catalogLayout->addWidget(catalogList);
    layout->addWidget(catalog);

    const auto updateCatalog = [this, type, valueRow, catalog, catalogList, catalogSearch] {
        const QString kind = type->currentData().toString();
        const bool showCatalog = kind == QStringLiteral("geosite") || kind == QStringLiteral("geoip");
        catalog->setVisible(showCatalog);
        for (int index = 0; index < valueRow->count(); ++index)
            if (auto *widget = valueRow->itemAt(index)->widget()) widget->setVisible(!showCatalog);
        catalogList->clear();
        if (!showCatalog) return;
        const QString prefix = kind + '-';
        for (const QString &name : ruleSetCatalog_) {
            if (!name.startsWith(prefix, Qt::CaseInsensitive)) continue;
            auto *item = new QListWidgetItem(name.mid(prefix.size()), catalogList);
            item->setData(Qt::UserRole, name);
        }
        catalogSearch->clear();
        catalogSearch->setFocus();
    };
    connect(type, &QComboBox::currentIndexChanged, &dialog, [updateCatalog](int) { updateCatalog(); });
    connect(catalogSearch, &QLineEdit::textChanged, &dialog, [catalogList](const QString &needle) {
        for (int row = 0; row < catalogList->count(); ++row) {
            auto *item = catalogList->item(row);
            const bool visible = item->text().contains(needle, Qt::CaseInsensitive)
                || item->data(Qt::UserRole).toString().contains(needle, Qt::CaseInsensitive);
            item->setHidden(!visible);
        }
    });
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Cancel | QDialogButtonBox::Ok);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("Add selected"));
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);
    dialog.setStyleSheet(styleSheet());
    dialog.setMinimumWidth(460);
    updateCatalog();
    if (section == QStringLiteral("ruleset")) catalogSearch->setFocus();
    else value->setFocus();
    if (dialog.exec() != QDialog::Accepted) return;
    const QString kind = type->currentData().toString();
    const bool selectedRuleSet = kind == QStringLiteral("geosite") || kind == QStringLiteral("geoip");
    QStringList values;
    if (selectedRuleSet) {
        for (QListWidgetItem *item : catalogList->selectedItems())
            values.append(item->data(Qt::UserRole).toString());
    } else {
        values = cleanedRules(value->toPlainText());
    }
    if (values.isEmpty()) return;
    const QString prefix = selectedRuleSet ? QStringLiteral("ruleset") : kind;
    bool changed = false;
    for (const QString &entry : values) {
        const QString rule = prefix + ':' + entry;
        if (rules_[selectedAction_].contains(rule)) continue;
        rules_[selectedAction_].append(rule);
        changed = true;
    }
    if (!changed) return;
    updateTotalCount();
    emit rulesChanged(selectedAction_, rules(selectedAction_));
    updateActionButtons();
    rebuild();
}

void RouteProfileSimpleEditor::removeRule(const QString &rule) {
    rules_[selectedAction_].removeAll(rule);
    updateTotalCount();
    emit rulesChanged(selectedAction_, rules(selectedAction_));
    updateActionButtons();
    rebuild();
}

void RouteProfileSimpleEditor::updateTotalCount() {
    int total = advancedRuleCount_;
    for (auto it = rules_.cbegin(); it != rules_.cend(); ++it) total += it.value().size();
    totalLabel_->setText(QString::number(total));
}
