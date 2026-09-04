#include "include/ui/setting/RouteItem.h"

#include "include/configs/generate.h"
#include "include/database/ProfilesRepo.h"
#include "include/database/GroupsRepo.h"
#include "include/global/Configs.hpp"
#include "include/configs/sub/RouteUpdater.hpp"
#include "include/ui/setting/RouteProfileSimpleEditor.h"
#include "include/ui/setting/ThemeManager.hpp"
#include "include/ui/widget/FlowLayout.h"
#include "include/ui/widget/MaterialIcon.h"
#include "include/ui/widget/ThronedTitleBar.h"
#include "include/ui/widget/ThronedWindowChrome.h"
#include "include/ui/widget/ThronedToggle.h"

#include <srslist.h>

#include <algorithm>

#include <QComboBox>
#include <QAbstractButton>
#include <QAction>
#include <QDialogButtonBox>
#include <QFont>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScreen>
#include <QSet>
#include <QSizePolicy>
#include <QStackedWidget>
#include <QGridLayout>
#include <QFrame>
#include <QJsonDocument>
#include <QMouseEvent>
#include <QMenu>
#include <QPainter>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QTabBar>
#include <QTextEdit>
#include <QToolButton>

#include <algorithm>

namespace {

constexpr auto RouteBlue = "#237AE9";
constexpr auto RouteGreen = "#2EBC75";
constexpr auto RouteRed = "#FF4D56";
constexpr auto RoutePurple = "#A66CFF";

class RouteActionFilterButton final : public QAbstractButton {
public:
    RouteActionFilterButton(MaterialIcon::Glyph glyph, const QString &title, const QColor &tone,
                            QWidget *parent = nullptr)
        : QAbstractButton(parent), glyph_(glyph), title_(title), tone_(tone) {
        setCheckable(true);
        setCursor(Qt::PointingHandCursor);
        setFixedHeight(44);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    }

    void setCount(int count) {
        count_ = count;
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const QRectF bounds = rect().adjusted(.5, .5, -.5, -.5);
        painter.setPen(isChecked() ? tone_ : QColor(QStringLiteral("#2F3136")));
        painter.setBrush(isChecked() ? QColor(QStringLiteral("#193452")) : QColor(QStringLiteral("#222529")));
        painter.drawRoundedRect(bounds, 6, 6);

        painter.drawPixmap(13, (height() - 18) / 2, MaterialIcon::pixmap(glyph_, tone_, 18));
        QFont titleFont = font();
        titleFont.setWeight(QFont::DemiBold);
        painter.setFont(titleFont);
        painter.setPen(QColor(QStringLiteral("#F1F3F5")));
        painter.drawText(QRect(43, 0, width() - 85, height()), Qt::AlignVCenter | Qt::AlignLeft, title_);

        const QString count = QString::number(count_);
        const int pillWidth = std::max(26, QFontMetrics(font()).horizontalAdvance(count) + 14);
        const QRectF pill(width() - pillWidth - 12, (height() - 24) / 2.0, pillWidth, 24);
        painter.setPen(Qt::NoPen);
        painter.setBrush(isChecked() ? tone_ : QColor(QStringLiteral("#2B3037")));
        painter.drawRoundedRect(pill, 5, 5);
        painter.setPen(QColor(QStringLiteral("#F7F9FA")));
        painter.drawText(pill, Qt::AlignCenter, count);
    }

private:
    MaterialIcon::Glyph glyph_;
    QString title_;
    QColor tone_;
    int count_ = 0;
};

QString compactJsonValue(const QJsonValue &value) {
    if (value.isString()) return value.toString();
    if (value.isDouble()) return QString::number(value.toDouble());
    if (value.isBool()) return value.toBool() ? QStringLiteral("true") : QStringLiteral("false");
    if (value.isArray()) {
        QStringList parts;
        const QJsonArray array = value.toArray();
        for (int index = 0; index < array.size() && index < 2; ++index)
            parts.append(compactJsonValue(array[index]));
        QString text = parts.join(QStringLiteral(", "));
        if (array.size() > 2) text += QStringLiteral(" +%1").arg(array.size() - 2);
        return text;
    }
    if (value.isObject()) return QStringLiteral("{…}");
    return QStringLiteral("null");
}

QString actionTone(const QString &action, int outboundId) {
    if (action == QStringLiteral("reject")) return QStringLiteral("red");
    if (action == QStringLiteral("hijack-dns") || action == QStringLiteral("sniff"))
        return QStringLiteral("cyan");
    if (outboundId == Configs::directID) return QStringLiteral("green");
    if (outboundId == Configs::warpBypassID) return QStringLiteral("purple");
    return QStringLiteral("blue");
}

int actionBucket(const std::shared_ptr<Configs::RouteRule> &rule) {
    if (!rule) return -1;
    if (rule->action == QStringLiteral("reject") || rule->outboundID == Configs::blockID) return 1;
    if (rule->action != QStringLiteral("route") && rule->action != QStringLiteral("bypass")) return -1;
    if (rule->outboundID == Configs::directID) return 0;
    if (rule->outboundID == Configs::warpBypassID) return 3;
    return 2;
}

QString actionFilterTitle(int action) {
    switch (action) {
    case 0: return RouteItem::tr("Direct");
    case 1: return RouteItem::tr("Block");
    case 3: return RouteItem::tr("WARP bypass");
    default: return RouteItem::tr("Proxy");
    }
}

} // namespace

void adjustComboBoxWidth(const QComboBox *comboBox) {
    int maxWidth = 0;

    for (int i = 0; i < comboBox->count(); ++i) {
        QFontMetrics fontMetrics(comboBox->font());
        int itemWidth = fontMetrics.horizontalAdvance(comboBox->itemText(i));
        maxWidth = qMax(maxWidth, itemWidth);
    }

    maxWidth += 30;
    comboBox->view()->setMinimumWidth(maxWidth);
}

QString get_outbound_name(int id) {
    if (id == -1) return "proxy";
    if (id == -2) return "direct";
    if (id == Configs::warpBypassID) return "warp-bypass";
    if (auto profile = Configs::dataManager->profilesRepo->GetProfile(id)) return profile->name;
    return "INVALID OUTBOUND";
}

RouteItem::RouteItem(QWidget *parent, const std::shared_ptr<Configs::RouteProfile>& routeChain)
    : QDialog(parent), ui(new Ui::RouteItem) {
    ui->setupUi(this);

    chain = std::make_shared<Configs::RouteProfile>(*routeChain);

    if (chain->IsEmpty()) {
        auto routeItem = std::make_shared<Configs::RouteRule>();
        routeItem->name = "dns-hijack";
        routeItem->protocol = "dns";
        routeItem->action = "hijack-dns";
        chain->Rules << routeItem;
    }

    std::map<QString, int> valueMap;
    for (auto &item: chain->Rules) {
        auto baseName = item->name;
        int randPart;
        if (baseName == "") {
            randPart = int(GetRandomUint64() % 1000);
            baseName = "rule_" + Int2String(randPart);
            lastNum = std::max(lastNum, randPart);
        }
        while (true) {
            valueMap[baseName]++;
            if (valueMap[baseName] > 1) {
                valueMap[baseName]--;
                randPart = int(GetRandomUint64() % 1000);
                baseName = "rule_" + Int2String(randPart);
                lastNum = std::max(lastNum, randPart);
                continue;
            }
            item->name = baseName;
            break;
        }
        ui->route_items->addItem(item->name);
    }

    outbounds = {"proxy", "direct", "warp-bypass"};
    outboundMap[0] = -1;
    outboundMap[1] = -2;
    outboundMap[2] = Configs::warpBypassID;
    auto proxyListRaw = Configs::dataManager->profilesRepo->GetAllProfileIDNameMapped();
    QMap<int, QString> idToName;
    for (const auto& [id, name] : proxyListRaw) idToName.insert(id, name);
    auto groupIDs = Configs::dataManager->groupsRepo->GetGroupsTabOrder();
    for (auto groupID : groupIDs) {
        auto group = Configs::dataManager->groupsRepo->GetGroup(groupID);
        if (!group) continue;
        for (int profileID : group->profiles) {
            if (!idToName.contains(profileID)) continue;
            outboundMap[static_cast<int>(outboundMap.size())] = profileID;
            outbounds << QString("[" + group->name + "] ") + idToName[profileID];
        }
    }

    for (const auto& item : ruleSetList) {
        geo_items.append(QString::fromUtf8(item.first.data(), item.first.size()));
    }

    ui->route_name->setText(chain->name);
    ui->rule_preview->setReadOnly(true);
    ui->rule_attr_tabs->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    ui->rule_attr_tabs->setTabsClosable(true);
    connect(ui->rule_attr_tabs, &QTabWidget::tabCloseRequested, this, [this](int index) {
        if (currentIndex < 0) return;
        const QString tabText = ui->rule_attr_tabs->tabText(index);
        if (tabText == QStringLiteral("+")) return;
        applyAttributeVisibilityChange(tabText, false);
    });

    connect(ui->rule_attr_tabs, &QTabWidget::currentChanged, this, [this](int index) {
        if (currentIndex >= 0 && !currentRuleIsEndpoint() && index >= 0 && index < ui->rule_attr_tabs->count())
            chain->Rules[currentIndex]->uiActiveAttributeTabLabel = ui->rule_attr_tabs->tabText(index);
        if (QWidget* w = ui->rule_attr_tabs->currentWidget()) {
            w->updateGeometry();
            w->adjustSize();
        }
        ui->rule_attr_tabs->updateGeometry();
    });

    ensurePlusTabBuiltOnce();

    ui->def_out->setCurrentText(Configs::outboundIDToString(chain->defaultOutboundID));

    QStringList ruleItems = {"domain:", "suffix:", "regex:", "keyword:", "ip:", "processName:", "processPath:", "ruleset:"};
    for (const auto& item : ruleSetList) {
        ruleItems.append("ruleset:" + QString::fromUtf8(item.first.data(), item.first.size()));
    }
    simpleDirect = new AutoCompleteTextEdit("", ruleItems, this);
    simpleBlock = new AutoCompleteTextEdit("", ruleItems, this);
    simpleProxy = new AutoCompleteTextEdit("", ruleItems, this);
    simpleWarpBypass = new AutoCompleteTextEdit("", ruleItems, this);

    ui->simple_direct_box->layout()->replaceWidget(ui->simple_direct, simpleDirect);
    ui->simple_block_box->layout()->replaceWidget(ui->simple_block, simpleBlock);
    ui->simple_proxy_box->layout()->replaceWidget(ui->simple_proxy, simpleProxy);
    ui->simple_warpbypass_box->layout()->replaceWidget(ui->simple_warpbypass, simpleWarpBypass);
    ui->simple_direct->hide();
    ui->simple_block->hide();
    ui->simple_proxy->hide();
    ui->simple_warpbypass->hide();

    simpleDirect->setPlainText(chain->GetSimpleRules(Configs::bypass));
    simpleBlock->setPlainText(chain->GetSimpleRules(Configs::block));
    simpleProxy->setPlainText(chain->GetSimpleRules(Configs::proxy));
    simpleWarpBypass->setPlainText(chain->GetSimpleRules(Configs::warpBypass));
    simpleEditor = new RouteProfileSimpleEditor(ui->tab_2);
    ui->simple_direct_box->hide();
    ui->simple_block_box->hide();
    ui->simple_proxy_box->hide();
    ui->simple_warpbypass_box->hide();
    ui->howtouse_button->hide();
    ui->verticalLayout_5->removeItem(ui->simpleGrid);
    ui->verticalLayout_5->addWidget(simpleEditor, 1);
    simpleEditor->setRules(Configs::bypass, simpleDirect->toPlainText());
    simpleEditor->setRules(Configs::block, simpleBlock->toPlainText());
    simpleEditor->setRules(Configs::proxy, simpleProxy->toPlainText());
    simpleEditor->setRules(Configs::warpBypass, simpleWarpBypass->toPlainText());
    // outbounds/outboundMap already hold every profile keyed by combo index; the
    // first three entries are the proxy/direct/warp roles, which are not profiles.
    for (int i = 3; i < outbounds.size(); i++) viaCatalog_ << qMakePair(outboundMap[i], outbounds[i]);
    simpleEditor->setViaCatalog(viaCatalog_);
    reloadViaBuckets();
    simpleEditor->setRuleSetCatalog(geo_items);
    int advancedRules = 0;
    QStringList advancedRuleNames;
    bool localProxyTraffic = false;
    for (const auto& rule : chain->Rules) {
        if (rule->type == Configs::custom && !Configs::IsLocalProxyTrafficRule(rule)) {
            ++advancedRules;
            advancedRuleNames.append(rule->name);
        }
        if (Configs::IsLocalProxyTrafficRule(rule)) localProxyTraffic = true;
    }
    simpleEditor->setAdvancedRules(advancedRuleNames);
    simpleEditor->setLocalProxyTrafficEnabled(localProxyTraffic);
    connect(simpleEditor, &RouteProfileSimpleEditor::rulesChanged, this, [this](int action, const QString& rules) {
        if (RouteProfileSimpleEditor::isViaAction(action)) {
            viaBucketRules_[RouteProfileSimpleEditor::viaProfileOf(action)] = rules;
            return;
        }
        switch (static_cast<Configs::simpleAction>(action)) {
        case Configs::bypass: simpleDirect->setPlainText(rules); break;
        case Configs::block: simpleBlock->setPlainText(rules); break;
        case Configs::proxy: simpleProxy->setPlainText(rules); break;
        case Configs::warpBypass: simpleWarpBypass->setPlainText(rules); break;
        case Configs::viaProfile: break;
        }
    });
    connect(simpleEditor, &RouteProfileSimpleEditor::viaBucketAdded, this, [this](int profileID) {
        if (!viaBucketRules_.contains(profileID)) viaBucketRules_[profileID] = "";
        pushViaBuckets();
    });
    connect(simpleEditor, &RouteProfileSimpleEditor::viaBucketRemoved, this, [this](int profileID) {
        viaBucketRules_.remove(profileID);
        chain->RemoveSimpleViaProfile(profileID);
        pushViaBuckets();
    });
    connect(simpleEditor, &RouteProfileSimpleEditor::localProxyTrafficChanged, this, [this](bool enabled) {
        const auto it = std::find_if(chain->Rules.begin(), chain->Rules.end(), Configs::IsLocalProxyTrafficRule);
        if (enabled && it == chain->Rules.end()) {
            auto rule = std::make_shared<Configs::RouteRule>();
            rule->name = Configs::LocalProxyRuleName;
            rule->action = "route";
            rule->outboundID = Configs::proxyID;
            rule->inbound = {"mixed-in", "socks-in"};
            chain->Rules.append(rule);
        } else if (!enabled && it != chain->Rules.end()) {
            chain->Rules.erase(it);
        }
    });
    connect(simpleEditor, &RouteProfileSimpleEditor::advancedEditorRequested, this, [this] {
        ui->tabWidget->setCurrentIndex(1);
        on_new_route_item_clicked();
        showAdvancedDetail(currentIndex);
    });

    // Replace the legacy advanced page with the ordered-card view used by
    // tools/ui-demo/RoutesPreview.  The old detailed editor is retained as the
    // second page of the stack, so every existing rule field remains editable
    // and serialization stays lossless.
    QWidget *legacyAdvancedPage = ui->tab;
    ui->tabWidget->removeTab(1);
    auto *advancedHost = new QWidget(ui->tabWidget);
    advancedHost->setObjectName(QStringLiteral("routeAdvancedHost"));
    auto *advancedHostLayout = new QVBoxLayout(advancedHost);
    advancedHostLayout->setContentsMargins(0, 0, 0, 0);
    advancedHostLayout->setSpacing(0);
    advancedStack = new QStackedWidget(advancedHost);
    advancedStack->setObjectName(QStringLiteral("routeAdvancedStack"));

    auto *summaryHost = new QWidget(advancedStack);
    summaryHost->setObjectName(QStringLiteral("routeAdvancedSummaryHost"));
    auto *summaryHostLayout = new QHBoxLayout(summaryHost);
    summaryHostLayout->setContentsMargins(0, 0, 0, 0);
    summaryHostLayout->setSpacing(10);

    advancedSidebar = new QFrame(summaryHost);
    advancedSidebar->setObjectName(QStringLiteral("routeAdvancedSidebar"));
    advancedSidebar->setFixedWidth(228);
    auto *sidebarLayout = new QVBoxLayout(advancedSidebar);
    sidebarLayout->setContentsMargins(10, 12, 10, 10);
    sidebarLayout->setSpacing(6);
    auto *sidebarTitle = new QLabel(tr("Routing actions"), advancedSidebar);
    sidebarTitle->setObjectName(QStringLiteral("routeSideTitle"));
    sidebarLayout->addWidget(sidebarTitle);
    sidebarLayout->addSpacing(8);
    const struct {
        int action;
        const char *title;
        MaterialIcon::Glyph glyph;
        const char *tone;
    } sidebarActions[] = {
        {0, QT_TR_NOOP("Direct"), MaterialIcon::Glyph::Direct, RouteGreen},
        {2, QT_TR_NOOP("Proxy"), MaterialIcon::Glyph::Shield, RouteBlue},
        {1, QT_TR_NOOP("Block"), MaterialIcon::Glyph::Block, RouteRed},
        {3, QT_TR_NOOP("WARP bypass"), MaterialIcon::Glyph::SwapVertical, RoutePurple},
    };
    for (const auto &item : sidebarActions) {
        auto *button = new RouteActionFilterButton(item.glyph, tr(item.title), QColor(item.tone), advancedSidebar);
        advancedActionButtons[item.action] = button;
        connect(button, &QAbstractButton::clicked, this, [this, action = item.action] {
            advancedActionFilter = action;
            advancedShowAllActions = false;
            rebuildAdvancedSummary();
        });
        sidebarLayout->addWidget(button);
    }
    sidebarLayout->addStretch(1);
    auto *stats = new QFrame(advancedSidebar);
    stats->setObjectName(QStringLiteral("routeAdvancedStats"));
    auto *statsLayout = new QGridLayout(stats);
    statsLayout->setContentsMargins(12, 10, 12, 10);
    auto *statsTitle = new QLabel(tr("Profile statistics"), stats);
    statsTitle->setObjectName(QStringLiteral("routeSideTitle"));
    statsLayout->addWidget(statsTitle, 0, 0, 1, 2);
    auto *totalText = new QLabel(tr("Total rules"), stats);
    totalText->setObjectName(QStringLiteral("routeMuted"));
    statsLayout->addWidget(totalText, 1, 0);
    advancedTotalLabel = new QLabel(QStringLiteral("0"), stats);
    advancedTotalLabel->setObjectName(QStringLiteral("routeStatsValue"));
    statsLayout->addWidget(advancedTotalLabel, 1, 1, Qt::AlignRight);
    sidebarLayout->addWidget(stats);
    summaryHostLayout->addWidget(advancedSidebar);

    auto *summaryScroll = new QScrollArea(summaryHost);
    summaryScroll->setObjectName(QStringLiteral("routeAdvancedScroll"));
    summaryScroll->setWidgetResizable(true);
    summaryScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    summaryScroll->setFrameShape(QFrame::NoFrame);
    auto *summaryPage = new QWidget(summaryScroll);
    summaryPage->setObjectName(QStringLiteral("routeAdvancedSummary"));
    advancedRulesLayout = new QVBoxLayout(summaryPage);
    advancedRulesLayout->setContentsMargins(12, 10, 12, 10);
    advancedRulesLayout->setSpacing(9);
    summaryScroll->setWidget(summaryPage);
    summaryHostLayout->addWidget(summaryScroll, 1);
    advancedStack->addWidget(summaryHost);

    auto *detailPage = new QWidget(advancedStack);
    detailPage->setObjectName(QStringLiteral("routeAdvancedDetail"));
    auto *detailLayout = new QVBoxLayout(detailPage);
    detailLayout->setContentsMargins(10, 8, 10, 8);
    detailLayout->setSpacing(8);
    auto *detailBar = new QHBoxLayout;
    auto *backButton = new QPushButton(tr("Back to rule list"), detailPage);
    backButton->setObjectName(QStringLiteral("routeSecondaryButton"));
    backButton->setIcon(MaterialIcon::icon(MaterialIcon::Glyph::List, QColor(QStringLiteral("#DDE2E7")), 17));
    detailBar->addWidget(backButton);
    auto *detailHint = new QLabel(tr("Edit the selected rule without losing its original JSON fields."), detailPage);
    detailHint->setObjectName(QStringLiteral("routeMuted"));
    detailBar->addWidget(detailHint);
    detailBar->addStretch(1);
    detailLayout->addLayout(detailBar);
    legacyAdvancedPage->setParent(detailPage);
    legacyAdvancedPage->setVisible(true);
    detailLayout->addWidget(legacyAdvancedPage, 1);
    advancedStack->addWidget(detailPage);
    connect(backButton, &QPushButton::clicked, this, [this] {
        persistCurrentRuleAttrTabLabel();
        rebuildAdvancedSummary();
        advancedStack->setCurrentIndex(0);
    });

    advancedHostLayout->addWidget(advancedStack);
    ui->tabWidget->insertTab(1, advancedHost, tr("Advanced"));
    rebuildAdvancedSummary();

    QFont editorFont = font();
    editorFont.setStyleStrategy(QFont::PreferAntialias);
    editorFont.setHintingPreference(QFont::PreferDefaultHinting);
    simpleEditor->setFont(editorFont);
    setObjectName("routeProfileEditor");
    ui->tabWidget->setTabText(0, tr("Simple"));
    ui->tabWidget->setStyleSheet({});
    auto *modeTabs = new QTabBar(this);
    modeTabs->setObjectName("routeModeTabs");
    modeTabs->addTab(tr("Simple"));
    modeTabs->addTab(tr("Advanced"));
    modeTabs->addTab(tr("Endpoints"));
    modeTabs->setUsesScrollButtons(false);
    modeTabs->setExpanding(true);
    modeTabs->setFixedSize(318, 38);
    ui->tabWidget->tabBar()->hide();
    connect(modeTabs, &QTabBar::currentChanged, ui->tabWidget, &QTabWidget::setCurrentIndex);
    connect(ui->tabWidget, &QTabWidget::currentChanged, modeTabs, &QTabBar::setCurrentIndex);
    ui->rule_attr_tabs->setStyleSheet({});
    ui->rule_attr_tabs->tabBar()->setObjectName(QStringLiteral("ruleAttrTabBar"));
    ui->route_items->setMinimumHeight(172);
    ui->rule_action_combo->setMinimumWidth(220);
    ui->rule_action_combo->setMaximumWidth(320);
    ui->horizontalLayout_action->addStretch(1);

    // Use the exact structural shell from tools/ui-demo/RoutesPreview.  The
    // controls below are the real editor controls; only their old Designer
    // containers are discarded.
    auto *dialogLayout = ui->verticalLayout_3;
    while (QLayoutItem *item = dialogLayout->takeAt(0)) delete item;
    dialogLayout->setContentsMargins(1, 1, 1, 1);
    dialogLayout->setSpacing(0);
    dialogLayout->addWidget(ThronedChrome::install(this, tr("Route profile")));

    auto *body = new QWidget(this);
    body->setObjectName(QStringLiteral("routeBody"));
    auto *bodyLayout = new QVBoxLayout(body);
    bodyLayout->setContentsMargins(12, 8, 12, 10);
    bodyLayout->setSpacing(9);

    const auto makeField = [body](QLabel *label, QWidget *control, int minimumWidth) {
        auto *box = new QFrame(body);
        box->setObjectName(QStringLiteral("routeFieldBox"));
        auto *layout = new QVBoxLayout(box);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(6);
        label->setParent(box);
        label->setObjectName(QStringLiteral("routeFieldLabel"));
        control->setParent(box);
        control->setMinimumWidth(minimumWidth);
        control->setMinimumHeight(38);
        layout->addWidget(label);
        layout->addWidget(control);
        return box;
    };

    ui->gridLayout->removeWidget(ui->route_name_l);
    ui->gridLayout->removeWidget(ui->route_name);
    ui->gridLayout->removeWidget(ui->def_out_l);
    ui->gridLayout->removeWidget(ui->def_out);
    ui->generalBox->hide();
    auto *top = new QHBoxLayout;
    top->setSpacing(14);
    top->addWidget(makeField(ui->route_name_l, ui->route_name, 250), 2);
    top->addWidget(makeField(ui->def_out_l, ui->def_out, 250), 2);
    auto *modeBox = new QFrame(body);
    modeBox->setObjectName(QStringLiteral("routeFieldBox"));
    auto *modeLayout = new QVBoxLayout(modeBox);
    modeLayout->setContentsMargins(0, 0, 0, 0);
    modeLayout->setSpacing(6);
    auto *modeLabel = new QLabel(tr("Mode"), modeBox);
    modeLabel->setObjectName(QStringLiteral("routeFieldLabel"));
    modeTabs->setParent(modeBox);
    modeLayout->addWidget(modeLabel);
    modeLayout->addWidget(modeTabs);
    top->addWidget(modeBox);
    top->addStretch(1);
    ui->howtouse_button->setParent(body);
    ui->howtouse_button->setVisible(true);
    ui->howtouse_button->setText(tr("?  Help"));
    ui->howtouse_button->setObjectName(QStringLiteral("routeSecondaryButton"));
    ui->howtouse_button->setFixedHeight(42);
    top->addWidget(ui->howtouse_button, 0, Qt::AlignBottom);
    bodyLayout->addLayout(top);

    ui->remoteBox->setParent(body);
    bodyLayout->addWidget(ui->remoteBox);
    ui->tabWidget->setParent(body);
    ui->tabWidget->setObjectName(QStringLiteral("routeModeStack"));
    bodyLayout->addWidget(ui->tabWidget, 1);

    ui->buttonBox->hide();
    auto *footer = new QHBoxLayout;
    auto *saveHint = new QLabel(tr("Changes are saved only after validation."), body);
    saveHint->setObjectName(QStringLiteral("routeMuted"));
    footer->addWidget(saveHint);
    footer->addStretch(1);
    auto *cancelButton = new QPushButton(tr("Cancel"), body);
    cancelButton->setObjectName(QStringLiteral("routeSecondaryButton"));
    cancelButton->setMinimumWidth(138);
    auto *saveButton = new QPushButton(tr("Save profile"), body);
    saveButton->setObjectName(QStringLiteral("routeSaveButton"));
    saveButton->setMinimumWidth(150);
    connect(cancelButton, &QPushButton::clicked, this, &QDialog::reject);
    connect(saveButton, &QPushButton::clicked, this, &RouteItem::accept);
    footer->addWidget(cancelButton);
    footer->addWidget(saveButton);
    bodyLayout->addLayout(footer);
    dialogLayout->addWidget(body, 1);

    themeManager()->RegisterStyle(this, RouteProfileSimpleEditor::dialogStyleSheet());
    setMinimumSize(960, 680);
    FitWindowToScreen(this, QSize(1085, 761));

    // Dispatch on page identity: literal tab indices misroute as soon as a page is inserted.
    lastTabPage = ui->tabWidget->currentWidget();
    connect(ui->tabWidget, &QTabWidget::currentChanged, this, [=, this]() {
        QWidget* from = lastTabPage;
        lastTabPage = ui->tabWidget->currentWidget();

        if (from == ui->tab_2) {
            QString res;
            res += chain->UpdateSimpleRules(simpleDirect->toPlainText(), Configs::bypass);
            res += chain->UpdateSimpleRules(simpleBlock->toPlainText(), Configs::block);
            res += chain->UpdateSimpleRules(simpleProxy->toPlainText(), Configs::proxy);
            res += chain->UpdateSimpleRules(simpleWarpBypass->toPlainText(), Configs::warpBypass);
            res += saveViaBuckets();
            if (!res.isEmpty()) {
                runOnUiThread([=] {
                    MessageBoxWarning(tr("Invalid rules"), tr("Some rules could not be added:\n") + res);
                });
            }
        }
        if (currentIndex >= 0) persistCurrentRuleAttrTabLabel();

        // UpdateSimpleRules rebuilds and filters chain->Rules, so the selection is stale.
        if (from == ui->tab_2) currentIndex = -1;

        syncEndpointRules();

        if (lastTabPage == ui->tab_2) {
            simpleDirect->setPlainText(chain->GetSimpleRules(Configs::bypass));
            simpleBlock->setPlainText(chain->GetSimpleRules(Configs::block));
            simpleProxy->setPlainText(chain->GetSimpleRules(Configs::proxy));
            simpleWarpBypass->setPlainText(chain->GetSimpleRules(Configs::warpBypass));
            reloadViaBuckets();
            syncRouteProfileToSimpleEditors();
        } else if (lastTabPage == advancedHost) {
            if (currentIndex < 0 && !chain->Rules.isEmpty()) currentIndex = 0;
            updateRouteItemsView();
            updateRuleSection();
            rebuildAdvancedSummary();
            advancedStack->setCurrentIndex(0);
        }
    });

    connect(ui->howtouse_button, &QPushButton::clicked, this, [=]() {
        runOnUiThread([=] {
            MessageBoxInfo(tr("Simple rule manual"), Configs::Information::SimpleRuleInfo);
        });
    });

    connect(ui->rule_name, &QLineEdit::textChanged, this, [=, this](const QString& text) {
        if (currentIndex == -1 || currentRuleIsEndpoint()) return;
        chain->Rules[currentIndex]->name = QString(text);
        auto ruleNameCursorPosition = ui->rule_name->cursorPosition();
        updateRouteItemsView();
        ui->rule_name->setCursorPosition(ruleNameCursorPosition);
    });

    connect(ui->route_items, &QListWidget::currentRowChanged, this, [=, this](const int idx) {
        if (idx == -1) {
            if (currentIndex >= 0)
                persistCurrentRuleAttrTabLabel();
            currentIndex = -1;
            updateRuleSection();
            return;
        }
        if (currentIndex >= 0)
            persistCurrentRuleAttrTabLabel();
        currentIndex = idx;
        updateRuleSection();
    });

    connect(ui->rule_action_combo, &QComboBox::currentTextChanged, this, [=, this](const QString& text) {
        if (currentIndex < 0 || currentRuleIsEndpoint()) return;
        chain->Rules[currentIndex]->set_field_value(QStringLiteral("action"), {text});
        updateRulePreview();
    });

    connect(ui->buttonBox, &QDialogButtonBox::accepted, this, [=, this] {
        accept();
    });
    connect(ui->buttonBox, &QDialogButtonBox::rejected, this, [=, this] {
       QDialog::reject();
    });

    deleteShortcut = new QShortcut(QKeySequence(Qt::Key_Delete), this);

    connect(deleteShortcut, &QShortcut::activated, this, [=, this] {
        // The shortcut is dialog-wide, so on the Endpoints page it must not reach the rule list.
        if (ui->tabWidget->currentWidget() == ui->endpointsTab) {
            const int row = ui->endpointList->currentRow();
            if (row >= 0) removeEndpointRow(ui->endpointList->item(row)->data(Qt::UserRole).toInt());
            return;
        }
        on_delete_route_item_clicked();
    });

    setupRemoteSection();
    setupEndpointsSection();
    const int endpointsIndex = ui->tabWidget->indexOf(ui->endpointsTab);
    modeTabs->setTabVisible(2, endpointsIndex >= 0 && ui->tabWidget->isTabVisible(endpointsIndex));

    updateRuleSection();
}

RouteItem::~RouteItem() {
    delete ui;
}

void RouteItem::showAdvancedDetail(int ruleIndex) {
    if (!advancedStack || ruleIndex < 0 || ruleIndex >= chain->Rules.size()) return;
    if (currentIndex >= 0) persistCurrentRuleAttrTabLabel();
    currentIndex = ruleIndex;
    updateRouteItemsView();
    updateRuleSection();
    advancedStack->setCurrentIndex(1);
}

void RouteItem::rebuildAdvancedSummary() {
    if (!advancedRulesLayout) return;
    while (QLayoutItem *item = advancedRulesLayout->takeAt(0)) {
        if (QWidget *widget = item->widget()) delete widget;
        if (QLayout *layout = item->layout()) delete layout;
        delete item;
    }

    std::map<int, int> actionCounts{{0, 0}, {1, 0}, {2, 0}, {3, 0}};
    for (const auto &rule : chain->Rules) {
        const int bucket = actionBucket(rule);
        if (actionCounts.contains(bucket)) ++actionCounts[bucket];
    }
    for (const auto &[action, button] : advancedActionButtons) {
        auto *filterButton = static_cast<RouteActionFilterButton *>(button);
        filterButton->setCount(actionCounts[action]);
        filterButton->setChecked(action == advancedActionFilter);
    }
    if (advancedTotalLabel) advancedTotalLabel->setText(QString::number(chain->Rules.size()));

    QWidget *summaryParent = advancedRulesLayout->parentWidget();
    auto *hero = new QWidget(summaryParent);
    hero->setObjectName(QStringLiteral("routeTransparent"));
    auto *heroLayout = new QHBoxLayout(hero);
    heroLayout->setContentsMargins(0, 0, 0, 0);
    heroLayout->setSpacing(9);
    auto *heroIcon = new QLabel(hero);
    heroIcon->setPixmap(MaterialIcon::pixmap(MaterialIcon::Glyph::List, QColor(QStringLiteral("#35C2F1")), 23));
    heroLayout->addWidget(heroIcon);
    auto *heroCopy = new QVBoxLayout;
    heroCopy->setSpacing(2);
    auto *heroTitle = new QLabel(tr("Ordered rule list"), hero);
    heroTitle->setObjectName(QStringLiteral("routeAdvancedHero"));
    heroCopy->addWidget(heroTitle);
    auto *heroSubtitle = new QLabel(
        tr("First match wins. Reordering here changes the actual sing-box priority."), hero);
    heroSubtitle->setObjectName(QStringLiteral("routeMuted"));
    heroSubtitle->setWordWrap(true);
    heroSubtitle->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    heroCopy->addWidget(heroSubtitle);
    heroLayout->addLayout(heroCopy, 1);

    auto *sourceButton = new QPushButton(tr("Full source"), hero);
    sourceButton->setObjectName(QStringLiteral("routeSecondaryButton"));
    sourceButton->setIcon(MaterialIcon::icon(MaterialIcon::Glyph::Code, QColor(QStringLiteral("#DDE2E7")), 17));
    sourceButton->setMinimumWidth(118);
    connect(sourceButton, &QPushButton::clicked, this, [this] {
        QDialog dialog(this);
        dialog.setWindowTitle(tr("Full routing source"));
        FitWindowToScreen(&dialog, QSize(760, 560));
        themeManager()->RegisterStyle(&dialog, RouteProfileSimpleEditor::dialogStyleSheet());
        auto *layout = new QVBoxLayout(&dialog);
        auto *source = new QPlainTextEdit(&dialog);
        source->setReadOnly(true);
        source->setPlainText(QString::fromUtf8(QJsonDocument(chain->get_route_rules(true)).toJson(QJsonDocument::Indented)));
        layout->addWidget(source, 1);
        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
        connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        layout->addWidget(buttons);
        dialog.exec();
    });
    heroLayout->addWidget(sourceButton);

    auto *validateButton = new QPushButton(tr("Validate"), hero);
    validateButton->setObjectName(QStringLiteral("routeSecondaryButton"));
    validateButton->setIcon(MaterialIcon::icon(MaterialIcon::Glyph::Shield, QColor(QStringLiteral("#2EBC75")), 17));
    validateButton->setMinimumWidth(108);
    connect(validateButton, &QPushButton::clicked, this, [this] {
        QStringList invalid;
        for (int index = 0; index < chain->Rules.size(); ++index) {
            if (chain->Rules[index]->get_rule_json(true).isEmpty())
                invalid.append(chain->Rules[index]->name.isEmpty() ? tr("Rule %1").arg(index + 1) : chain->Rules[index]->name);
        }
        if (invalid.isEmpty())
            MessageBoxInfo(tr("Routing profile is valid"), tr("All %1 rules can be serialized.").arg(chain->Rules.size()));
        else
            MessageBoxWarning(tr("Invalid rules"), invalid.join(QStringLiteral("\n")));
    });
    heroLayout->addWidget(validateButton);

    advancedRulesLayout->addWidget(hero);

    auto *notice = new QFrame(summaryParent);
    notice->setObjectName(QStringLiteral("routeAdvancedNotice"));
    auto *noticeLayout = new QHBoxLayout(notice);
    noticeLayout->setContentsMargins(12, 9, 12, 9);
    auto *noticeIcon = new QLabel(notice);
    noticeIcon->setPixmap(MaterialIcon::pixmap(MaterialIcon::Glyph::Shield, QColor(QStringLiteral("#237AE9")), 17));
    noticeLayout->addWidget(noticeIcon);
    auto *noticeText = new QLabel(advancedShowAllActions
        ? tr("All actions are shown in their original global order; unknown JSON fields are preserved.")
        : tr("Showing %1 rules · original global positions are preserved").arg(actionFilterTitle(advancedActionFilter)),
        notice);
    noticeText->setObjectName(QStringLiteral("routeMuted"));
    noticeText->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    noticeLayout->addWidget(noticeText, 1);
    auto *showAllButton = new QPushButton(advancedShowAllActions
        ? tr("Show %1 only").arg(actionFilterTitle(advancedActionFilter))
        : tr("Show all actions"), notice);
    showAllButton->setObjectName(QStringLiteral("routeLinkButton"));
    showAllButton->setMinimumWidth(132);
    showAllButton->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);
    connect(showAllButton, &QPushButton::clicked, this, [this] {
        advancedShowAllActions = !advancedShowAllActions;
        rebuildAdvancedSummary();
    });
    noticeLayout->addWidget(showAllButton);
    advancedRulesLayout->addWidget(notice);

    int shownRules = 0;
    for (int index = 0; index < chain->Rules.size(); ++index) {
        const auto rule = chain->Rules[index];
        if (!advancedShowAllActions && actionBucket(rule) != advancedActionFilter) continue;
        ++shownRules;
        const QJsonObject json = rule->get_rule_json(true);
        const QString tone = actionTone(rule->action, rule->outboundID);
        auto *card = new QFrame(summaryParent);
        card->setObjectName(QStringLiteral("routeOrderedRule"));
        card->setProperty("tone", tone);
        auto *cardLayout = new QHBoxLayout(card);
        cardLayout->setContentsMargins(13, 11, 11, 11);
        cardLayout->setSpacing(10);
        auto *drag = new QLabel(QStringLiteral("⋮⋮"), card);
        drag->setObjectName(QStringLiteral("routeDragHandle"));
        cardLayout->addWidget(drag);
        auto *priority = new QLabel(QString::number(index + 1), card);
        priority->setObjectName(QStringLiteral("routePriorityPill"));
        priority->setAlignment(Qt::AlignCenter);
        priority->setFixedSize(32, 28);
        cardLayout->addWidget(priority);
        auto *active = new ThronedToggle(true, card);
        active->setToolTip(tr("This rule is active"));
        active->setAttribute(Qt::WA_TransparentForMouseEvents);
        cardLayout->addWidget(active);

        auto *body = new QVBoxLayout;
        body->setSpacing(7);
        auto *titleRow = new QHBoxLayout;
        const QString ruleName = rule->name.isEmpty() ? tr("Rule %1").arg(index + 1) : rule->name;
        auto *name = new QLabel(card);
        name->setObjectName(QStringLiteral("routeOrderedTitle"));
        name->setText(name->fontMetrics().elidedText(ruleName, Qt::ElideRight, 260));
        name->setToolTip(ruleName);
        name->setMaximumWidth(272);
        titleRow->addWidget(name);
        QString actionText = rule->action;
        if (rule->action == QStringLiteral("route")) actionText = get_outbound_name(rule->outboundID);
        auto *action = new QLabel(actionText, card);
        action->setObjectName(QStringLiteral("routeActionPill"));
        action->setProperty("tone", tone);
        titleRow->addWidget(action);
        titleRow->addStretch(1);
        body->addLayout(titleRow);

        auto *conditionHost = new QWidget(card);
        conditionHost->setObjectName(QStringLiteral("routeTransparent"));
        auto *conditions = new FlowLayout(conditionHost, 6, 6);
        conditions->setContentsMargins(0, 0, 0, 0);
        int conditionCount = 0;
        int hiddenConditions = 0;
        for (auto it = json.begin(); it != json.end(); ++it) {
            if (it.key() == QStringLiteral("action") || it.key() == QStringLiteral("outbound")) continue;
            if (conditionCount >= 4) {
                ++hiddenConditions;
                continue;
            }
            const QString conditionText = QStringLiteral("%1  %2").arg(it.key(), compactJsonValue(it.value()));
            auto *condition = new QLabel(conditionHost);
            condition->setObjectName(QStringLiteral("routeConditionPill"));
            condition->setProperty("tone", tone);
            condition->setText(condition->fontMetrics().elidedText(conditionText, Qt::ElideRight, 170));
            condition->setToolTip(conditionText);
            condition->setMaximumWidth(190);
            conditions->addWidget(condition);
            ++conditionCount;
        }
        if (hiddenConditions > 0) {
            auto *more = new QLabel(tr("+%1 more").arg(hiddenConditions), conditionHost);
            more->setObjectName(QStringLiteral("routeMuted"));
            conditions->addWidget(more);
        }
        if (conditionCount == 0) {
            auto *empty = new QLabel(tr("No match conditions"), conditionHost);
            empty->setObjectName(QStringLiteral("routeMuted"));
            conditions->addWidget(empty);
        }
        body->addWidget(conditionHost);
        cardLayout->addLayout(body, 1);

        auto *jsonButton = new QPushButton(QStringLiteral("{ }  JSON"), card);
        jsonButton->setObjectName(QStringLiteral("routeCardJsonButton"));
        connect(jsonButton, &QPushButton::clicked, this, [this, index] { showAdvancedDetail(index); });
        cardLayout->addWidget(jsonButton);

        auto *more = new QToolButton(card);
        more->setObjectName(QStringLiteral("routeAdvancedMoreButton"));
        more->setIcon(MaterialIcon::icon(MaterialIcon::Glyph::More, QColor(QStringLiteral("#AEB7C2")), 18));
        more->setPopupMode(QToolButton::InstantPopup);
        more->setCursor(Qt::PointingHandCursor);
        auto *menu = new QMenu(more);
        auto *editAction = menu->addAction(MaterialIcon::icon(MaterialIcon::Glyph::Settings, QColor(QStringLiteral("#AEB7C2")), 17), tr("Edit rule"));
        auto *moveUpAction = menu->addAction(MaterialIcon::icon(MaterialIcon::Glyph::ArrowUp, QColor(QStringLiteral("#AEB7C2")), 17), tr("Move up"));
        auto *moveDownAction = menu->addAction(MaterialIcon::icon(MaterialIcon::Glyph::ArrowDown, QColor(QStringLiteral("#AEB7C2")), 17), tr("Move down"));
        menu->addSeparator();
        auto *deleteAction = menu->addAction(MaterialIcon::icon(MaterialIcon::Glyph::Block, QColor(QStringLiteral("#FF7B82")), 17), tr("Delete rule"));
        moveUpAction->setEnabled(index > 0);
        moveDownAction->setEnabled(index + 1 < chain->Rules.size());
        connect(editAction, &QAction::triggered, this, [this, index] { showAdvancedDetail(index); });
        connect(moveUpAction, &QAction::triggered, this, [this, index] {
            currentIndex = index;
            on_moveup_route_item_clicked();
            rebuildAdvancedSummary();
        });
        connect(moveDownAction, &QAction::triggered, this, [this, index] {
            currentIndex = index;
            on_movedown_route_item_clicked();
            rebuildAdvancedSummary();
        });
        connect(deleteAction, &QAction::triggered, this, [this, index] {
            currentIndex = index;
            on_delete_route_item_clicked();
            rebuildAdvancedSummary();
        });
        more->setMenu(menu);
        cardLayout->addWidget(more);
        advancedRulesLayout->addWidget(card);
    }

    if (shownRules == 0) {
        auto *empty = new QFrame(summaryParent);
        empty->setObjectName(QStringLiteral("routeAdvancedNotice"));
        auto *emptyLayout = new QHBoxLayout(empty);
        emptyLayout->setContentsMargins(14, 14, 14, 14);
        auto *emptyText = new QLabel(tr("No %1 rules yet.").arg(actionFilterTitle(advancedActionFilter)), empty);
        emptyText->setObjectName(QStringLiteral("routeMuted"));
        emptyLayout->addWidget(emptyText);
        emptyLayout->addStretch(1);
        auto *add = new QPushButton(tr("Add rule"), empty);
        add->setObjectName(QStringLiteral("routeSecondaryButton"));
        connect(add, &QPushButton::clicked, this, [this] {
            on_new_route_item_clicked();
            showAdvancedDetail(currentIndex);
        });
        emptyLayout->addWidget(add);
        advancedRulesLayout->addWidget(empty);
    }

    auto *fallback = new QFrame(summaryParent);
    fallback->setObjectName(QStringLiteral("routeFallbackCard"));
    auto *fallbackLayout = new QHBoxLayout(fallback);
    fallbackLayout->setContentsMargins(14, 10, 14, 10);
    auto *fallbackIcon = new QLabel(fallback);
    fallbackIcon->setPixmap(MaterialIcon::pixmap(MaterialIcon::Glyph::Direct, QColor(QStringLiteral("#2EBC75")), 18));
    fallbackLayout->addWidget(fallbackIcon);
    auto *fallbackText = new QLabel(tr("Unmatched traffic"), fallback);
    fallbackText->setObjectName(QStringLiteral("routeOrderedTitle"));
    fallbackLayout->addWidget(fallbackText);
    fallbackLayout->addStretch(1);
    auto *fallbackValue = new QLabel(get_outbound_name(chain->defaultOutboundID), fallback);
    fallbackValue->setObjectName(QStringLiteral("routeActionPill"));
    fallbackValue->setProperty("tone", actionTone(QStringLiteral("route"), chain->defaultOutboundID));
    fallbackLayout->addWidget(fallbackValue);
    advancedRulesLayout->addWidget(fallback);
    advancedRulesLayout->addStretch(1);
}

void RouteItem::setupRemoteSection() {
    if (!chain->isRemote) {
        ui->remoteBox->hide();
        return;
    }
    ui->remoteUrlEdit->setText(chain->remoteURL);
    ui->autoUpdateCheck->setChecked(chain->autoUpdate);
    connect(ui->remotePreviewBtn, &QPushButton::clicked, this, [this] { fetchRemote(false); });
    connect(ui->remoteFetchBtn, &QPushButton::clicked, this, [this] { fetchRemote(true); });
}

void RouteItem::fetchRemote(bool applyToChain) {
    const QString url = ui->remoteUrlEdit->text().trimmed();
    if (!url.startsWith("http://", Qt::CaseInsensitive) && !url.startsWith("https://", Qt::CaseInsensitive)) {
        MessageBoxWarning(tr("Invalid URL"), tr("Enter a valid http(s) URL first."));
        return;
    }
    if (applyToChain && !chain->IsEmpty()) {
        if (QMessageBox::question(this, tr("Fetch from remote"),
                                  tr("This will replace the current rules with the ones fetched from the URL. Continue?"))
            != QMessageBox::StandardButton::Yes) {
            return;
        }
    }

    ui->remotePreviewBtn->setEnabled(false);
    ui->remoteFetchBtn->setEnabled(false);
    const QString origFetch = ui->remoteFetchBtn->text();
    const QString origPreview = ui->remotePreviewBtn->text();
    (applyToChain ? ui->remoteFetchBtn : ui->remotePreviewBtn)->setText(tr("Fetching..."));

    // UpdateProfile fills an empty name from the remote one; this stops it overwriting what the user typed.
    const QString currentName = ui->route_name->text();

    runOnNewThread([=, this] {
        auto target = applyToChain ? chain : std::make_shared<Configs::RouteProfile>(*chain);
        target->isRemote = true;
        target->remoteURL = url;
        target->name = currentName;
        QString warnings;
        const QString err = RouteUpdate::UpdateProfile(target, &warnings);
        runOnUiThread([=, this] {
            ui->remotePreviewBtn->setEnabled(true);
            ui->remoteFetchBtn->setEnabled(true);
            ui->remoteFetchBtn->setText(origFetch);
            ui->remotePreviewBtn->setText(origPreview);
            if (!err.isEmpty()) {
                MessageBoxWarning(tr("Could not fetch routing profile"), err);
                return;
            }
            if (applyToChain) {
                reloadRuleViewsFromChain();
                const QString msg = tr("Loaded %1 rule(s) from the remote URL.").arg(target->Rules.size());
                if (warnings.isEmpty()) MessageBoxInfo(tr("Fetched"), msg);
                else MessageBoxInfo(tr("Fetched with warnings"), msg + "\n\n" + warnings);
            } else {
                auto* dlg = new QDialog(this);
                dlg->setAttribute(Qt::WA_DeleteOnClose);
                dlg->setWindowTitle(tr("Remote routing profile preview"));
                auto* lay = new QVBoxLayout(dlg);
                auto* header = new QLabel(tr("%1 — %2 rule(s)").arg(target->name.isEmpty() ? tr("(unnamed)") : target->name)
                                              .arg(target->Rules.size()), dlg);
                lay->addWidget(header);
                if (!warnings.isEmpty()) {
                    auto* warn = new QLabel(warnings, dlg);
                    warn->setStyleSheet(QStringLiteral("color: %1;").arg(themeManager()->Colors().danger.name()));
                    warn->setWordWrap(true);
                    lay->addWidget(warn);
                }
                auto* view = new QPlainTextEdit(dlg);
                view->setReadOnly(true);
                view->setPlainText(QJsonObject2QString(target->ToShareObject(), false));
                view->setLineWrapMode(QPlainTextEdit::NoWrap);
                lay->addWidget(view, 1);
                auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, Qt::Horizontal, dlg);
                connect(buttons, &QDialogButtonBox::rejected, dlg, &QDialog::reject);
                connect(buttons, &QDialogButtonBox::accepted, dlg, &QDialog::accept);
                lay->addWidget(buttons);
                FitWindowToScreen(dlg, QSize(560, 460));
                dlg->show();
            }
        });
    });
}

static QList<QPair<QString, int>> routeItemEndpointCandidates() {
    QList<QPair<QString, int>> candidates;
    QList<int> ids = Configs::dataManager->profilesRepo->GetProfileIdsByType("openvpn");
    ids += Configs::dataManager->profilesRepo->GetProfileIdsByType("openconnect");
    ids += Configs::dataManager->profilesRepo->GetProfileIdsByType("chain");
    for (const int id : ids) {
        const auto ent = Configs::dataManager->profilesRepo->GetProfile(id);
        if (ent == nullptr || ent->outbound == nullptr) continue;
        if (!Configs::CanBeAuxEndpoint(ent)) continue;
        candidates.append({ent->outbound->DisplayTypeAndName(), id});
    }
    std::sort(candidates.begin(), candidates.end(), [](const auto& a, const auto& b) {
        return QString::localeAwareCompare(a.first, b.first) < 0;
    });
    return candidates;
}

static bool routeItemIsEndpointRule(const std::shared_ptr<Configs::RouteRule>& rule) {
    return rule != nullptr && rule->type == Configs::endpointPreferredBy;
}

static QString routeItemEndpointName(int profileId) {
    const auto ent = Configs::dataManager->profilesRepo->GetProfile(profileId);
    if (ent == nullptr || ent->outbound == nullptr) return QString::number(profileId);
    return ent->outbound->DisplayName();
}

static QString routeItemUniqueRuleName(const QString& base, const QSet<QString>& taken) {
    QString name = base;
    for (int n = 2; taken.contains(name); n++) name = base + " (" + Int2String(n) + ")";
    return name;
}

static std::shared_ptr<Configs::RouteRule> routeItemMakeEndpointRule(int profileId) {
    auto rule = std::make_shared<Configs::RouteRule>();
    rule->type = Configs::endpointPreferredBy;
    rule->outboundID = profileId;
    // Nothing on it is user-editable; seeding attribute tabs from it would only offer edits.
    rule->uiAttributeTabsSeeded = true;
    return rule;
}

void RouteItem::setupEndpointsSection() {
    endpointCandidates = routeItemEndpointCandidates();
    QSet<int> listed;
    for (const int profileId : chain->endpointProfileIDs) {
        if (listed.contains(profileId)) continue;
        listed.insert(profileId);
        addEndpointRow(profileId);
    }

    syncEndpointRules();

    if (endpointCandidates.isEmpty() && ui->endpointList->count() == 0) {
        ui->tabWidget->setTabVisible(ui->tabWidget->indexOf(ui->endpointsTab), false);
        return;
    }

    connect(ui->endpointAddBtn, &QPushButton::clicked, this, [this] {
        const int idx = ui->endpointPicker->currentIndex();
        if (idx < 0) return;
        addEndpointRow(ui->endpointPicker->itemData(idx).toInt());
        refreshEndpointCandidates();
        syncEndpointRules();
    });
    connect(ui->endpointRemoveBtn, &QPushButton::clicked, this, [this] {
        const int row = ui->endpointList->currentRow();
        if (row < 0) return;
        removeEndpointRow(ui->endpointList->item(row)->data(Qt::UserRole).toInt());
    });
    connect(ui->endpointList, &QListWidget::currentRowChanged, this, [this](const int row) {
        ui->endpointRemoveBtn->setEnabled(row >= 0);
    });

    refreshEndpointCandidates();
}

void RouteItem::refreshEndpointCandidates() const {
    QSet<int> used;
    for (int i = 0; i < ui->endpointList->count(); i++)
        used.insert(ui->endpointList->item(i)->data(Qt::UserRole).toInt());

    ui->endpointPicker->clear();
    for (const auto& [label, id] : endpointCandidates) {
        if (used.contains(id)) continue;
        ui->endpointPicker->addItem(label, id);
    }

    const bool canAdd = ui->endpointPicker->count() > 0;
    ui->endpointPicker->setEnabled(canAdd);
    ui->endpointAddBtn->setEnabled(canAdd);
    ui->endpointRemoveBtn->setEnabled(ui->endpointList->currentRow() >= 0);
}

void RouteItem::addEndpointRow(int profileId) const {
    auto* row = new QListWidgetItem(ui->endpointList);
    row->setData(Qt::UserRole, profileId);
    const auto ent = Configs::dataManager->profilesRepo->GetProfile(profileId);
    if (ent != nullptr && ent->outbound != nullptr) {
        row->setText(ent->outbound->DisplayTypeAndName());
        return;
    }
    row->setText(tr("Profile #%1 — deleted, dropped when you save").arg(profileId));
    row->setForeground(QColor(0xc6, 0x28, 0x28));
}

void RouteItem::removeEndpointRow(int profileId) {
    for (int i = 0; i < ui->endpointList->count(); i++) {
        if (ui->endpointList->item(i)->data(Qt::UserRole).toInt() != profileId) continue;
        delete ui->endpointList->takeItem(i);
        break;
    }
    refreshEndpointCandidates();
    syncEndpointRules();
}

QList<int> RouteItem::listedEndpointIDs() const {
    QList<int> ids;
    for (int i = 0; i < ui->endpointList->count(); i++) {
        const int id = ui->endpointList->item(i)->data(Qt::UserRole).toInt();
        if (ids.contains(id)) continue;
        const auto ent = Configs::dataManager->profilesRepo->GetProfile(id);
        if (ent == nullptr || ent->outbound == nullptr) continue;
        ids << id;
    }
    return ids;
}

void RouteItem::syncEndpointRules() {
    const QList<int> listed = listedEndpointIDs();
    const auto selected = currentIndex >= 0 && currentIndex < chain->Rules.size()
                              ? chain->Rules[currentIndex]
                              : nullptr;

    QSet<int> paired;
    QSet<QString> names;
    QList<std::shared_ptr<Configs::RouteRule>> kept;
    for (const auto& rule : chain->Rules) {
        if (routeItemIsEndpointRule(rule)) {
            if (!listed.contains(rule->outboundID) || paired.contains(rule->outboundID)) continue;
            paired.insert(rule->outboundID);
        } else {
            names.insert(rule->name);
        }
        kept << rule;
    }
    chain->Rules = kept;

    for (const int id : listed) {
        if (!paired.contains(id)) chain->Rules << routeItemMakeEndpointRule(id);
    }

    for (const auto& rule : chain->Rules) {
        if (!routeItemIsEndpointRule(rule)) continue;
        rule->name = routeItemUniqueRuleName(
            RouteItem::tr("%1 route prefer").arg(routeItemEndpointName(rule->outboundID)), names);
        names.insert(rule->name);
    }

    currentIndex = selected == nullptr ? -1 : static_cast<int>(chain->Rules.indexOf(selected));
    updateRouteItemsView();
    updateRuleSection();
}

bool RouteItem::currentRuleIsEndpoint() const {
    return currentIndex >= 0 && currentIndex < chain->Rules.size()
        && routeItemIsEndpointRule(chain->Rules[currentIndex]);
}

void RouteItem::applyRuleEditLock() {
    const bool managed = currentRuleIsEndpoint();
    ui->rule_managed_note->setVisible(managed);
    ui->rule_name->setEnabled(!managed);
    ui->rule_attr_tabs->setEnabled(!managed);
    if (managed) ui->rule_action_combo->setEnabled(false);
}

void RouteItem::reloadRuleViewsFromChain() {
    currentIndex = -1;
    ui->route_name->setText(chain->name);
    syncEndpointRules();
    simpleDirect->setPlainText(chain->GetSimpleRules(Configs::bypass));
    simpleBlock->setPlainText(chain->GetSimpleRules(Configs::block));
    simpleProxy->setPlainText(chain->GetSimpleRules(Configs::proxy));
    simpleWarpBypass->setPlainText(chain->GetSimpleRules(Configs::warpBypass));
    reloadViaBuckets();
    syncRouteProfileToSimpleEditors();
    ui->def_out->setCurrentText(Configs::outboundIDToString(chain->defaultOutboundID));
}

void RouteItem::accept() {
    syncSimpleEditorsToRouteProfile();
    chain->name = ui->route_name->text().trimmed();

    if (chain->name == "") {
        MessageBoxWarning(tr("Invalid operation"), tr("Cannot create Route Profile with empty name"));
        return;
    }

    if (chain->isRemote) {
        const QString url = ui->remoteUrlEdit->text().trimmed();
        if (url.isEmpty()) {
            MessageBoxWarning(tr("Invalid operation"), tr("Remote routing profiles need a URL."));
            return;
        }
        chain->remoteURL = url;
        chain->autoUpdate = ui->autoUpdateCheck->isChecked();
    }

    QString res;
    res += chain->UpdateSimpleRules(simpleDirect->toPlainText(), Configs::bypass);
    res += chain->UpdateSimpleRules(simpleBlock->toPlainText(), Configs::block);
    res += chain->UpdateSimpleRules(simpleProxy->toPlainText(), Configs::proxy);
    res += chain->UpdateSimpleRules(simpleWarpBypass->toPlainText(), Configs::warpBypass);
    res += saveViaBuckets();

    if (!res.isEmpty()) {
        runOnUiThread([=] {
            MessageBoxWarning(tr("Invalid rules"), tr("Some rules could not be added, fix them before saving:\n") + res);
        });
        return;
    }
    chain->FilterEmptyRules();

    // Endpoints whose profile is gone drop out here, and syncEndpointRules() drops their rules with them.
    const QList<int> endpointIDs = listedEndpointIDs();
    const int missingEndpoints = static_cast<int>(ui->endpointList->count() - endpointIDs.size());
    chain->endpointProfileIDs = endpointIDs;
    syncEndpointRules();

    // A remote profile may be saved before its first fetch; only plain profiles must be non-empty.
    if (!chain->isRemote && chain->IsEmpty()) {
        MessageBoxInfo(tr("Empty Route Profile"), tr("No valid rules are in the profile"));
        return;
    }

    chain->defaultOutboundID = Configs::stringToOutboundID(ui->def_out->currentText());

    if (missingEndpoints > 0) {
        MessageBoxInfo(tr("Endpoints"),
                       tr("%1 endpoint profile(s) no longer exist and were removed from this routing profile.")
                           .arg(missingEndpoints));
    }

    emit settingsChanged(chain);

    QDialog::accept();
}

void RouteItem::syncSimpleEditorsToRouteProfile() {
    if (!simpleEditor) return;
    simpleDirect->setPlainText(simpleEditor->rules(Configs::bypass));
    simpleBlock->setPlainText(simpleEditor->rules(Configs::block));
    simpleProxy->setPlainText(simpleEditor->rules(Configs::proxy));
    simpleWarpBypass->setPlainText(simpleEditor->rules(Configs::warpBypass));
    for (auto it = viaBucketRules_.begin(); it != viaBucketRules_.end(); ++it) it.value() = simpleEditor->rules(RouteProfileSimpleEditor::viaAction(it.key()));
}

void RouteItem::syncRouteProfileToSimpleEditors() {
    if (!simpleEditor) return;
    simpleEditor->setRules(Configs::bypass, simpleDirect->toPlainText());
    simpleEditor->setRules(Configs::block, simpleBlock->toPlainText());
    simpleEditor->setRules(Configs::proxy, simpleProxy->toPlainText());
    simpleEditor->setRules(Configs::warpBypass, simpleWarpBypass->toPlainText());
    pushViaBuckets();
    QStringList advancedRuleNames;
    bool localProxyTraffic = false;
    for (const auto& rule : chain->Rules) {
        if (rule->type == Configs::custom && !Configs::IsLocalProxyTrafficRule(rule)) advancedRuleNames.append(rule->name);
        if (Configs::IsLocalProxyTrafficRule(rule)) localProxyTraffic = true;
    }
    simpleEditor->setAdvancedRules(advancedRuleNames);
    simpleEditor->setLocalProxyTrafficEnabled(localProxyTraffic);
}

void RouteItem::updateRouteItemsView() {
    const QSignalBlocker listBlocker(ui->route_items);
    ui->route_items->clear();
    if (chain->IsEmpty()) return;

    for (const auto& item: chain->Rules) {
        ui->route_items->addItem(item->name);
        if (!routeItemIsEndpointRule(item)) continue;
        auto* row = ui->route_items->item(ui->route_items->count() - 1);
        QFont font = row->font();
        font.setItalic(true);
        row->setFont(font);
        row->setToolTip(tr("Endpoint rule: move it to choose where the endpoint claims traffic. Managed by the Endpoints tab."));
    }
    if (currentIndex != -1) ui->route_items->setCurrentRow(currentIndex);
}

void RouteItem::syncRuleActionCombo() {
    if (currentIndex < 0) return;
    ui->rule_action_combo->blockSignals(true);
    ui->rule_action_combo->clear();
    ui->rule_action_combo->addItems(Configs::RouteRule::get_values_for_field(QStringLiteral("action")));
    ui->rule_action_combo->setCurrentText(chain->Rules[currentIndex]->action);
    adjustComboBoxWidth(ui->rule_action_combo);
    auto rule = chain->Rules[currentIndex];
    if (rule->canEditAttr("action")) {
        ui->rule_action_combo->setEnabled(true);
    } else {
        ui->rule_action_combo->setEnabled(false);
    }
    ui->rule_action_combo->blockSignals(false);
}

QWidget* RouteItem::makeAttributeEditorPage(const QString& attr) {
    auto* container = new QWidget(ui->rule_attr_tabs);
    auto* lay = new QVBoxLayout(container);
    lay->setContentsMargins(8, 8, 8, 8);
    const auto rule = chain->Rules[currentIndex];
    const bool editable = rule->canEditAttr(attr);

    const auto addDisabled = [editable](QWidget* w) { w->setEnabled(editable); };

    switch (Configs::RouteRule::get_input_type(attr)) {
        case Configs::trufalse: {
            auto* cb = new QComboBox(container);
            cb->addItems({QStringLiteral("false"), QStringLiteral("true")});
            cb->setCurrentText(rule->get_current_value_bool(attr));
            connect(cb, &QComboBox::currentTextChanged, this, [this, attr](const QString& t) {
                if (currentIndex < 0) return;
                chain->Rules[currentIndex]->set_field_value(attr, {t});
                updateRulePreview();
            });
            addDisabled(cb);
            lay->addWidget(cb);
            break;
        }
        case Configs::select: {
            auto* cb = new QComboBox(container);
            if (attr == QStringLiteral("outbound")) {
                cb->addItems(outbounds);
                cb->setCurrentText(get_outbound_name(rule->outboundID));
                connect(cb, &QComboBox::currentTextChanged, this, [this, cb] {
                    if (currentIndex < 0) return;
                    chain->Rules[currentIndex]->set_field_value(QStringLiteral("outbound"),
                        {QString::number(outboundMap[cb->currentIndex()])});
                    updateRulePreview();
                });
            } else {
                cb->addItems(Configs::RouteRule::get_values_for_field(attr));
                const auto cur = rule->get_current_value_string(attr);
                cb->setCurrentText(cur.isEmpty() ? QString() : cur[0]);
                connect(cb, &QComboBox::currentTextChanged, this, [this, attr](const QString& t) {
                    if (currentIndex < 0) return;
                    chain->Rules[currentIndex]->set_field_value(attr, {t});
                    updateRulePreview();
                });
            }
            addDisabled(cb);
            adjustComboBoxWidth(cb);
            lay->addWidget(cb);
            break;
        }
        case Configs::text: {
            if (attr == QStringLiteral("rule_set")) {
                auto* ed = new AutoCompleteTextEdit("", geo_items, container);
                ed->setPlainText(rule->get_current_value_string(attr).join('\n'));
                ed->setMinimumHeight(100);
                ed->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
                connect(ed, &QPlainTextEdit::textChanged, this, [this, attr, ed] {
                    if (currentIndex < 0) return;
                    chain->Rules[currentIndex]->set_field_value(attr, ed->toPlainText().split('\n'));
                    updateRulePreview();
                });
                addDisabled(ed);
                lay->addWidget(ed, 1);
            } else {
                auto* te = new QPlainTextEdit(container);
                te->setPlainText(rule->get_current_value_string(attr).join('\n'));
                te->setMinimumHeight(100);
                te->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
                connect(te, &QPlainTextEdit::textChanged, this, [this, attr, te] {
                    if (currentIndex < 0) return;
                    chain->Rules[currentIndex]->set_field_value(attr, te->toPlainText().split('\n'));
                    updateRulePreview();
                });
                addDisabled(te);
                lay->addWidget(te, 1);
            }
            break;
        }
    }
    container->setMinimumHeight(100);
    container->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    return container;
}

void RouteItem::ensurePlusTabBuiltOnce() {
    if (ruleAttrPlusList) return;

    auto* container = new QWidget(ui->rule_attr_tabs);
    auto* lay = new QVBoxLayout(container);
    lay->setContentsMargins(8, 8, 8, 8);

    auto* hint = new QLabel(tr("Check attributes to show as tabs; unchecking clears their values."), container);
    hint->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    ruleAttrPlusList = new QListWidget(container);
    ruleAttrPlusList->setMinimumHeight(100);
    ruleAttrPlusList->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    ruleAttrPlusList->setObjectName(QStringLiteral("route_rule_attr_plus_list"));
    ruleAttrPlusList->viewport()->installEventFilter(this);
    for (const QString& attr : Configs::RouteRule::tab_attributes()) {
        auto* it = new QListWidgetItem(attr);
        it->setFlags(it->flags() | Qt::ItemIsUserCheckable);
        it->setCheckState(Qt::Unchecked);
        ruleAttrPlusList->addItem(it);
    }
    lay->addWidget(hint);
    lay->addWidget(ruleAttrPlusList);
    container->setMinimumHeight(100);
    container->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    ui->rule_attr_tabs->addTab(container, QStringLiteral("+"));
    ui->rule_attr_tabs->tabBar()->setTabButton(ui->rule_attr_tabs->count() - 1, QTabBar::RightSide, nullptr);
}

void RouteItem::removeAllAttributeTabsExceptPlus() {
    ensurePlusTabBuiltOnce();
    while (ui->rule_attr_tabs->count() > 1)
        ui->rule_attr_tabs->removeTab(0);
}

void RouteItem::syncPlusListCheckStatesFromRule() {
    if (!ruleAttrPlusList) return;
    const QSignalBlocker b(ruleAttrPlusList);
    if (currentIndex < 0) {
        ruleAttrPlusList->setEnabled(false);
        for (int i = 0; i < ruleAttrPlusList->count(); ++i)
            ruleAttrPlusList->item(i)->setCheckState(Qt::Unchecked);
        return;
    }
    ruleAttrPlusList->setEnabled(true);
    const auto rule = chain->Rules[currentIndex];
    for (int i = 0; i < ruleAttrPlusList->count(); ++i) {
        auto* it = ruleAttrPlusList->item(i);
        it->setCheckState(rule->uiVisibleAttributes.contains(it->text()) ? Qt::Checked : Qt::Unchecked);
        if (!rule->canEditAttr(it->text())) {
            it->setHidden(true);
        } else {
            it->setHidden(false);
        }
    }
}

void RouteItem::persistCurrentRuleAttrTabLabel() {
    if (currentIndex < 0 || currentRuleIsEndpoint()) return;
    const int idx = ui->rule_attr_tabs->currentIndex();
    if (idx < 0 || idx >= ui->rule_attr_tabs->count()) return;
    chain->Rules[currentIndex]->uiActiveAttributeTabLabel = ui->rule_attr_tabs->tabText(idx);
}

void RouteItem::applyStoredRuleAttrTabSelection() {
    int sel = 0;
    if (currentIndex >= 0) {
        const QString& pref = chain->Rules[currentIndex]->uiActiveAttributeTabLabel;
        if (!pref.isEmpty()) {
            for (int i = 0; i < ui->rule_attr_tabs->count(); ++i) {
                if (ui->rule_attr_tabs->tabText(i) == pref) {
                    sel = i;
                    break;
                }
            }
        }
    }
    ui->rule_attr_tabs->setCurrentIndex(sel);
}

void RouteItem::applyAttributeVisibilityChange(const QString& attr, bool visible) {
    if (currentIndex < 0 || currentRuleIsEndpoint()) return;
    persistCurrentRuleAttrTabLabel();
    auto r = chain->Rules[currentIndex];
    r->uiAttributeTabsSeeded = true;
    if (visible)
        r->uiVisibleAttributes.insert(attr);
    else {
        r->uiVisibleAttributes.remove(attr);
        r->clear_attribute_value(attr);
    }
    rebuildRuleAttributeTabs();
    updateRulePreview();
}

bool RouteItem::eventFilter(QObject* watched, QEvent* event) {
    if (event->type() == QEvent::MouseButtonRelease) {
        auto* vp = qobject_cast<QWidget*>(watched);
        if (vp && vp->parentWidget()) {
            auto* lw = qobject_cast<QListWidget*>(vp->parentWidget());
            if (lw && lw->objectName() == QLatin1String("route_rule_attr_plus_list")) {
                auto* me = static_cast<QMouseEvent*>(event);
                if (me->button() == Qt::LeftButton) {
                    if (QListWidgetItem* item = lw->itemAt(me->pos())) {
                        lw->setCurrentItem(item);
                        const bool toChecked = (item->checkState() != Qt::Checked);
                        {
                            const QSignalBlocker b(lw);
                            item->setCheckState(toChecked ? Qt::Checked : Qt::Unchecked);
                        }
                        applyAttributeVisibilityChange(item->text(), toChecked);
                        return true;
                    }
                }
            }
        }
    }
    return QDialog::eventFilter(watched, event);
}

void RouteItem::rebuildRuleAttributeTabs() {
    ensurePlusTabBuiltOnce();

    ui->rule_attr_tabs->blockSignals(true);
    removeAllAttributeTabsExceptPlus();

    if (currentIndex < 0) {
        syncPlusListCheckStatesFromRule();
        applyStoredRuleAttrTabSelection();
        ui->rule_attr_tabs->blockSignals(false);
        return;
    }

    const auto rule = chain->Rules[currentIndex];
    if (!routeItemIsEndpointRule(rule)) {
        for (const QString& attr : Configs::RouteRule::tab_attributes()) {
            if (!rule->uiVisibleAttributes.contains(attr)) continue;
            const int beforePlus = ui->rule_attr_tabs->count() - 1;
            ui->rule_attr_tabs->insertTab(beforePlus, makeAttributeEditorPage(attr), attr);
        }
    }

    syncPlusListCheckStatesFromRule();
    applyStoredRuleAttrTabSelection();

    ui->rule_attr_tabs->blockSignals(false);
}

void RouteItem::updateRuleSection() {
    if (currentIndex < 0) {
        {
            const QSignalBlocker nameBlocker(ui->rule_name);
            ui->rule_name->clear();
        }
        ui->rule_preview->clear();
        ui->rule_action_combo->blockSignals(true);
        ui->rule_action_combo->clear();
        ui->rule_action_combo->blockSignals(false);
        rebuildRuleAttributeTabs();
        applyRuleEditLock();
        return;
    }

    auto rule = chain->Rules[currentIndex];
    {
        const QSignalBlocker nameBlocker(ui->rule_name);
        ui->rule_name->setText(rule->name);
    }
    if (routeItemIsEndpointRule(rule)) {
        updateRulePreview();
        const QSignalBlocker actionBlocker(ui->rule_action_combo);
        ui->rule_action_combo->clear();
        rebuildRuleAttributeTabs();
        applyRuleEditLock();
        return;
    }
    rule->ensure_ui_visible_attribute_tabs_seeded();
    syncRuleActionCombo();
    rebuildRuleAttributeTabs();
    applyRuleEditLock();
    updateRulePreview();
}

void RouteItem::updateRulePreview() {
    if (currentIndex == -1) return;
    // The endpoint rule is a position marker; its gate is generated at build time.
    if (currentRuleIsEndpoint()) {
        ui->rule_preview->setPlainText(
            tr("This rule installs a 'preferred by' rule so that the networks advertised by the "
               "endpoint %1 get routed into the endpoint tunnel.")
                .arg(routeItemEndpointName(chain->Rules[currentIndex]->outboundID)));
        return;
    }

    ui->rule_preview->setPlainText(QJsonObject2QString(chain->Rules[currentIndex]->get_rule_json(true), false));
}

void RouteItem::on_new_route_item_clicked() {
    auto routeItem = std::make_shared<Configs::RouteRule>();
    routeItem->name = "rule_" + Int2String(++lastNum);
    chain->Rules << routeItem;
    currentIndex = chain->Rules.size() - 1;

    updateRouteItemsView();
    updateRuleSection();
}

void RouteItem::on_moveup_route_item_clicked() {
    if (currentIndex == -1 || currentIndex == 0) return;
    auto curr = chain->Rules[currentIndex];
    chain->Rules[currentIndex] = chain->Rules[currentIndex - 1];
    chain->Rules[currentIndex - 1] = curr;
    currentIndex--;
    updateRouteItemsView();
}

void RouteItem::on_movedown_route_item_clicked() {
    if (currentIndex == -1 || currentIndex == chain->Rules.size() - 1) return;
    auto curr = chain->Rules[currentIndex];
    chain->Rules[currentIndex] = chain->Rules[currentIndex + 1];
    chain->Rules[currentIndex + 1] = curr;
    currentIndex++;
    updateRouteItemsView();
}

void RouteItem::on_delete_route_item_clicked() {
    if (currentIndex == -1) return;
    if (currentRuleIsEndpoint()) {
        const int endpointID = chain->Rules[currentIndex]->outboundID;
        if (QMessageBox::question(this, tr("Endpoint rule"),
                                  tr("This rule belongs to the endpoint \"%1\" and cannot be deleted on its own.\n\n"
                                     "Remove that endpoint from this routing profile as well?")
                                      .arg(routeItemEndpointName(endpointID)))
            != QMessageBox::StandardButton::Yes) {
            return;
        }
        removeEndpointRow(endpointID);
        return;
    }
    chain->Rules.removeAt(currentIndex);
    if (chain->Rules.empty()) currentIndex = -1;
    else {
        currentIndex--;
        if (currentIndex == -1) currentIndex = 0;
    }
    updateRouteItemsView();
    updateRuleSection();
}

// Buckets come from the rules themselves: whichever profiles the via-profile
// rules aim at are exactly the buckets that should be on the sidebar.
void RouteItem::reloadViaBuckets() {
    viaBucketRules_.clear();
    for (int profileID : chain->GetSimpleViaProfileIDs())
        viaBucketRules_[profileID] = chain->GetSimpleRules(Configs::viaProfile, profileID);
    pushViaBuckets();
}

void RouteItem::pushViaBuckets() {
    QList<QPair<int, QString>> buckets;
    for (auto it = viaBucketRules_.begin(); it != viaBucketRules_.end(); ++it) {
        QString label = QString::number(it.key());
        for (const auto &entry : viaCatalog_) {
            if (entry.first != it.key()) continue;
            label = entry.second;
            break;
        }
        buckets << qMakePair(it.key(), label);
        simpleEditor->setRules(RouteProfileSimpleEditor::viaAction(it.key()), it.value());
    }
    simpleEditor->setViaBuckets(buckets);
}

QString RouteItem::saveViaBuckets() {
    QString res;
    // Profiles dropped from a bucket keep no rules behind: the ones still listed
    // are rewritten, and anything else was removed with its bucket already.
    for (auto it = viaBucketRules_.begin(); it != viaBucketRules_.end(); ++it)
        res += chain->UpdateSimpleRules(it.value(), Configs::viaProfile, it.key());
    return res;
}
