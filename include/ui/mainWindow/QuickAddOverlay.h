#pragma once

#include <QList>
#include <QPair>
#include <QWidget>

#include <functional>

class QComboBox;
class QAction;
class QFrame;
class QLabel;
class QLineEdit;
class QPushButton;
class QStackedWidget;
class QToolButton;
class QMouseEvent;

// One entry point for both the group-strip plus button and the empty-group
// action.  It deliberately stays presentation-only: MainWindow supplies the
// database/import callbacks, while this widget owns validation and theming.
class QuickAddOverlay final : public QWidget {
public:
    enum class LinkKind { Profile, Subscription };

    struct Callbacks {
        std::function<void(LinkKind, const QString &)> addLink;
        std::function<void(const QString &type, int groupId, const QString &name,
                           const QString &address, const QString &port)> addProfile;
        std::function<void(const QString &name, const QString &subscriptionUrl)> addGroup;
    };

    explicit QuickAddOverlay(QWidget *parent, Callbacks callbacks);
    ~QuickAddOverlay() override;

    void open(const QList<QPair<int, QString>> &groups, int currentGroupId);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;

private:
    enum class ManualPage { Profile, Group };

    Callbacks callbacks;
    LinkKind detectedKind = LinkKind::Profile;
    bool detectedLinkValid = false;
    bool manualMode = false;
    ManualPage manualPage = ManualPage::Profile;

    QFrame *card = nullptr;
    QLabel *headingIcon = nullptr;
    QLabel *title = nullptr;
    QLabel *subtitle = nullptr;
    QToolButton *closeButton = nullptr;
    QWidget *quickPage = nullptr;
    QLineEdit *linkInput = nullptr;
    QAction *linkClearAction = nullptr;
    QFrame *detectedFrame = nullptr;
    QLabel *detectedIcon = nullptr;
    QLabel *detectedTitle = nullptr;
    QLabel *detectedMeta = nullptr;
    QLabel *detectedKindLabel = nullptr;
    QLabel *validation = nullptr;

    QWidget *manualContainer = nullptr;
    QToolButton *profileTab = nullptr;
    QToolButton *groupTab = nullptr;
    QStackedWidget *manualStack = nullptr;
    QComboBox *profileType = nullptr;
    QComboBox *profileGroup = nullptr;
    QLineEdit *profileName = nullptr;
    QLineEdit *profileAddress = nullptr;
    QLineEdit *profilePort = nullptr;
    QLineEdit *groupName = nullptr;
    QComboBox *groupType = nullptr;
    QLineEdit *groupUrl = nullptr;

    QPushButton *manualButton = nullptr;
    QLabel *enterHint = nullptr;
    QPushButton *primaryButton = nullptr;

    void reset(const QList<QPair<int, QString>> &groups, int currentGroupId);
    void setManualMode(bool manual);
    void setManualPage(ManualPage page, bool focusFirst = true);
    void updateLinkValidation();
    void updateManualValidation();
    void updateGroupUrlState();
    void submit();
    void closeOverlay();
    void retintIcons();
};
