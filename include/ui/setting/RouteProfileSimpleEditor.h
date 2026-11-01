#pragma once

#include <QList>
#include <QMap>
#include <QPair>
#include <QStringList>
#include <QWidget>

#include <functional>

class QAbstractButton;
class QComboBox;
class QLabel;
class QMenu;
class QVBoxLayout;

class RouteProfileSimpleEditor final : public QWidget {
    Q_OBJECT

public:
    explicit RouteProfileSimpleEditor(QWidget *parent = nullptr);

    [[nodiscard]] static QString dialogStyleSheet();

    void setRules(int action, const QString &rules);
    [[nodiscard]] QString rules(int action) const;
    void setAdvancedRuleCount(int count);
    void setAdvancedRules(const QStringList &names);
    void setRuleSetCatalog(const QStringList &names);
    void setLocalProxyTrafficEnabled(bool enabled);

    // A via-profile bucket is one more action, keyed by the profile it aims at:
    // viaAction() maps a profile id into the action space the rest of the widget
    // already speaks, so buckets need no parallel storage of their own.
    static constexpr int viaActionBase = 1000000;
    static int viaAction(int profileID) { return viaActionBase + profileID; }
    static bool isViaAction(int action) { return action >= viaActionBase; }
    static int viaProfileOf(int action) { return action - viaActionBase; }

    // Buckets currently on the sidebar, and every profile one could be made from.
    void setViaBuckets(const QList<QPair<int, QString>> &buckets);
    void setViaCatalog(const QList<QPair<int, QString>> &profiles);

signals:
    void rulesChanged(int action, const QString &rules);
    void localProxyTrafficChanged(bool enabled);
    void advancedEditorRequested();
    void viaBucketAdded(int profileID);
    void viaBucketRemoved(int profileID);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    [[nodiscard]] QString viaLabelFor(int action) const;
    // Every action on the sidebar except `exclude`, as menu entries that call pick(action).
    void addMoveTargets(QMenu *menu, int exclude, const std::function<void(int)> &pick);
    void moveRules(int from, const QStringList &moved, int to);
    void showRuleMenu(const QString &rule, const QPoint &globalPos);
    void showActionMenu(int action, const QPoint &globalPos, QWidget *anchor = nullptr);
    void changeViaProfile(int profileID);
    void selectAction(int action);
    void rebuildSidebar();
    void addViaBucket();
    // Asks for a profile and adds its bucket; the new action, or -1 when cancelled.
    int createViaBucket();
    void rebuild();
    void bulkEdit();
    void addApplicationRules();
    void addRule(const QString &section);
    void removeRule(const QString &rule);
    void updateActionButtons();
    void updateTotalCount();

    int selectedAction_ = 2;
    int advancedRuleCount_ = 0;
    QStringList advancedRules_;
    QStringList ruleSetCatalog_;
    QMap<int, QStringList> rules_;
    QMap<int, QAbstractButton *> actionButtons_;
    QLabel *heading_ = nullptr;
    QLabel *description_ = nullptr;
    QLabel *totalLabel_ = nullptr;
    QVBoxLayout *cardsLayout_ = nullptr;
    QWidget *quickOptionsCard_ = nullptr;
    QAbstractButton *localProxyToggle_ = nullptr;
    bool localProxyTrafficEnabled_ = false;
    QList<QPair<int, QString>> viaBuckets_;
    QList<QPair<int, QString>> viaCatalog_;
    QVBoxLayout *sideLayout_ = nullptr;
    QWidget *sidebar_ = nullptr;
    QAbstractButton *addViaButton_ = nullptr;
};
