#pragma once

#include <QWidget>
#include <QListWidgetItem>
#include <QDialog>
#include <QEvent>
#include <QShortcut>

#include <v2/ui/QvAutoCompleteTextEdit.hpp>
#include "ui_RouteItem.h"
#include "include/database/entities/RouteProfile.h"

class QListWidget;
class QAbstractButton;
class QFrame;
class QLabel;
class RouteProfileSimpleEditor;
class QStackedWidget;
class QVBoxLayout;

class RouteItem : public QDialog {
    Q_OBJECT

public:
    explicit RouteItem(QWidget* parent = nullptr, const std::shared_ptr<Configs::RouteProfile>& routeChain = nullptr);
    ~RouteItem() override;

    std::shared_ptr<Configs::RouteProfile> chain;
signals:
    void settingsChanged(std::shared_ptr<Configs::RouteProfile> routingChain);

private:
    Ui::RouteItem* ui;
    int currentIndex = -1;

    int lastNum = 0;

    QStringList geo_items;

    QShortcut* deleteShortcut;

    QStringList outbounds;

    std::map<int, int> outboundMap;

    AutoCompleteTextEdit* simpleDirect;

    AutoCompleteTextEdit* simpleBlock;

    AutoCompleteTextEdit* simpleProxy;

    AutoCompleteTextEdit* simpleWarpBypass;

    // One via-profile bucket per chosen profile, id -> its rule lines. Buckets are
    // created on demand, so this holds only the ones the user asked for.
    QMap<int, QString> viaBucketRules_;
    QList<QPair<int, QString>> viaCatalog_;
    void reloadViaBuckets();
    void pushViaBuckets();
    QString saveViaBuckets();

    RouteProfileSimpleEditor* simpleEditor = nullptr;

    QStackedWidget* advancedStack = nullptr;
    QVBoxLayout* advancedRulesLayout = nullptr;
    QFrame* advancedSidebar = nullptr;
    QLabel* advancedTotalLabel = nullptr;
    std::map<int, QAbstractButton*> advancedActionButtons;
    int advancedActionFilter = 2;
    bool advancedShowAllActions = false;

    void rebuildAdvancedSummary();

    void showAdvancedDetail(int ruleIndex);

    void syncSimpleEditorsToRouteProfile();

    void syncRouteProfileToSimpleEditors();

    QListWidget* ruleAttrPlusList = nullptr;

    QWidget* lastTabPage = nullptr;

    void setupRemoteSection();

    void fetchRemote(bool applyToChain);

    QList<QPair<QString, int>> endpointCandidates;

    void setupEndpointsSection();

    void refreshEndpointCandidates() const;

    void addEndpointRow(int profileId, bool innerHops);

    void removeEndpointRow(int profileId);

    [[nodiscard]] QList<int> listedEndpointIDs() const;

    [[nodiscard]] QList<int> listedInnerHopEndpointIDs() const;

    // The listed endpoint profileId is an opened-up inner hop of, or -1.
    [[nodiscard]] int innerHopOwner(int profileId) const;

    void setEndpointRowInnerHops(int profileId, bool innerHops);

    // One endpointPreferredBy rule per listed endpoint and opened-up inner hop, keeping existing rules where they sit.
    void syncEndpointRules();

    [[nodiscard]] bool currentRuleIsEndpoint() const;

    void applyRuleEditLock();

    void reloadRuleViewsFromChain();

    void ensurePlusTabBuiltOnce();

    void removeAllAttributeTabsExceptPlus();

    void syncPlusListCheckStatesFromRule();

    void persistCurrentRuleAttrTabLabel();

    void applyStoredRuleAttrTabSelection();

    void syncRuleActionCombo();

    void rebuildRuleAttributeTabs();

    [[nodiscard]] QWidget* makeAttributeEditorPage(const QString& attr);

    void updateRuleSection();

    void updateRulePreview();

    void updateRouteItemsView();

    void applyAttributeVisibilityChange(const QString& attr, bool visible);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private slots:
    void accept() override;

    void on_new_route_item_clicked();
    void on_moveup_route_item_clicked();
    void on_movedown_route_item_clicked();
    void on_delete_route_item_clicked();
};
