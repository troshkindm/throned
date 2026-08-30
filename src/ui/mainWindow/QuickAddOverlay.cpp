#include "include/ui/mainWindow/QuickAddOverlay.h"

#include "include/ui/setting/ThemeManager.hpp"
#include "include/ui/widget/MaterialIcon.h"

#include <QAction>
#include <QApplication>
#include <QButtonGroup>
#include <QComboBox>
#include <QCoreApplication>
#include <QEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPushButton>
#include <QStackedWidget>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

#include <utility>

namespace {
QString qaTr(const char *source) {
    return QCoreApplication::translate("QuickAddOverlay", source);
}

QWidget *makeField(const QString &caption, QWidget *control, QWidget *parent) {
    auto *field = new QWidget(parent);
    field->setObjectName(QStringLiteral("quickAddField"));
    auto *layout = new QVBoxLayout(field);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(5);
    auto *label = new QLabel(caption, field);
    label->setObjectName(QStringLiteral("quickAddFieldLabel"));
    layout->addWidget(label);
    layout->addWidget(control);
    return field;
}

bool isSubscriptionScheme(const QString &scheme) {
    return scheme == QStringLiteral("http") || scheme == QStringLiteral("https");
}

bool isProfileScheme(const QString &scheme) {
    static const QStringList schemes{
        QStringLiteral("ss"), QStringLiteral("vmess"), QStringLiteral("vless"),
        QStringLiteral("trojan"), QStringLiteral("anytls"), QStringLiteral("mieru"),
        QStringLiteral("mierus"), QStringLiteral("snell"), QStringLiteral("hysteria"),
        QStringLiteral("hysteria2"), QStringLiteral("hy2"), QStringLiteral("tuic"),
        QStringLiteral("juicity"), QStringLiteral("tt"), QStringLiteral("shadowtls"),
        QStringLiteral("wg"), QStringLiteral("ssh"), QStringLiteral("naive+https"),
        QStringLiteral("naive+quic"), QStringLiteral("socks"), QStringLiteral("socks4"),
        QStringLiteral("socks4a"), QStringLiteral("socks5"), QStringLiteral("throne"),
        QStringLiteral("json"),
    };
    return schemes.contains(scheme);
}
} // namespace

QuickAddOverlay::QuickAddOverlay(QWidget *parent, Callbacks callbacks)
    : QWidget(parent), callbacks(std::move(callbacks)) {
    setObjectName(QStringLiteral("quickAddOverlay"));
    setAttribute(Qt::WA_StyledBackground, true);
    setFocusPolicy(Qt::StrongFocus);
    hide();

    if (parent != nullptr) parent->installEventFilter(this);
    if (qApp != nullptr) qApp->installEventFilter(this);

    auto *overlayLayout = new QVBoxLayout(this);
    overlayLayout->setContentsMargins(24, 116, 24, 24);
    overlayLayout->setSpacing(0);

    card = new QFrame(this);
    card->setObjectName(QStringLiteral("quickAddCard"));
    card->setAttribute(Qt::WA_StyledBackground, true);
    card->setFixedWidth(620);
    auto *cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(0, 0, 0, 0);
    cardLayout->setSpacing(0);

    auto *heading = new QWidget(card);
    heading->setObjectName(QStringLiteral("quickAddHeading"));
    auto *headingLayout = new QHBoxLayout(heading);
    headingLayout->setContentsMargins(18, 17, 18, 10);
    headingLayout->setSpacing(11);
    headingIcon = new QLabel(heading);
    headingIcon->setObjectName(QStringLiteral("quickAddHeadingIcon"));
    headingIcon->setAlignment(Qt::AlignCenter);
    headingIcon->setFixedSize(34, 34);
    headingLayout->addWidget(headingIcon, 0, Qt::AlignTop);

    auto *headingCopy = new QWidget(heading);
    auto *headingCopyLayout = new QVBoxLayout(headingCopy);
    headingCopyLayout->setContentsMargins(0, 0, 0, 0);
    headingCopyLayout->setSpacing(3);
    title = new QLabel(headingCopy);
    title->setObjectName(QStringLiteral("quickAddTitle"));
    subtitle = new QLabel(headingCopy);
    subtitle->setObjectName(QStringLiteral("quickAddSubtitle"));
    headingCopyLayout->addWidget(title);
    headingCopyLayout->addWidget(subtitle);
    headingLayout->addWidget(headingCopy, 1, Qt::AlignTop);

    auto *escape = new QLabel(QStringLiteral("Esc"), heading);
    escape->setObjectName(QStringLiteral("quickAddEscape"));
    escape->setAlignment(Qt::AlignCenter);
    headingLayout->addWidget(escape, 0, Qt::AlignTop);
    cardLayout->addWidget(heading);

    auto *body = new QWidget(card);
    body->setObjectName(QStringLiteral("quickAddBody"));
    auto *bodyLayout = new QVBoxLayout(body);
    bodyLayout->setContentsMargins(18, 7, 18, 16);
    bodyLayout->setSpacing(0);

    quickPage = new QWidget(body);
    auto *quickLayout = new QVBoxLayout(quickPage);
    quickLayout->setContentsMargins(0, 0, 0, 0);
    quickLayout->setSpacing(0);
    linkInput = new QLineEdit(quickPage);
    linkInput->setObjectName(QStringLiteral("quickAddLinkInput"));
    linkInput->setPlaceholderText(qaTr("Paste a subscription or profile link…"));
    linkInput->setClearButtonEnabled(true);
    linkInput->setFixedHeight(54);
    linkAction = linkInput->addAction(QIcon(), QLineEdit::LeadingPosition);
    quickLayout->addWidget(linkInput);

    detectedFrame = new QFrame(quickPage);
    detectedFrame->setObjectName(QStringLiteral("quickAddDetected"));
    detectedFrame->setAttribute(Qt::WA_StyledBackground, true);
    auto *detectedLayout = new QHBoxLayout(detectedFrame);
    detectedLayout->setContentsMargins(11, 9, 11, 9);
    detectedLayout->setSpacing(10);
    detectedIcon = new QLabel(detectedFrame);
    detectedIcon->setFixedSize(20, 20);
    detectedLayout->addWidget(detectedIcon);
    auto *detectedCopy = new QWidget(detectedFrame);
    auto *detectedCopyLayout = new QVBoxLayout(detectedCopy);
    detectedCopyLayout->setContentsMargins(0, 0, 0, 0);
    detectedCopyLayout->setSpacing(3);
    detectedTitle = new QLabel(qaTr("Link recognized"), detectedCopy);
    detectedTitle->setObjectName(QStringLiteral("quickAddDetectedTitle"));
    detectedMeta = new QLabel(detectedCopy);
    detectedMeta->setObjectName(QStringLiteral("quickAddDetectedMeta"));
    detectedCopyLayout->addWidget(detectedTitle);
    detectedCopyLayout->addWidget(detectedMeta);
    detectedLayout->addWidget(detectedCopy, 1);
    detectedKindLabel = new QLabel(detectedFrame);
    detectedKindLabel->setObjectName(QStringLiteral("quickAddDetectedKind"));
    detectedLayout->addWidget(detectedKindLabel);
    detectedFrame->hide();
    quickLayout->addSpacing(11);
    quickLayout->addWidget(detectedFrame);

    validation = new QLabel(quickPage);
    validation->setObjectName(QStringLiteral("quickAddValidation"));
    validation->setMinimumHeight(18);
    quickLayout->addSpacing(7);
    quickLayout->addWidget(validation);
    bodyLayout->addWidget(quickPage);

    manualContainer = new QWidget(body);
    auto *manualLayout = new QVBoxLayout(manualContainer);
    manualLayout->setContentsMargins(0, 0, 0, 0);
    manualLayout->setSpacing(12);
    auto *manualTabs = new QFrame(manualContainer);
    manualTabs->setObjectName(QStringLiteral("quickAddManualTabs"));
    auto *tabsLayout = new QHBoxLayout(manualTabs);
    tabsLayout->setContentsMargins(0, 0, 0, 0);
    tabsLayout->setSpacing(16);
    profileTab = new QToolButton(manualTabs);
    profileTab->setObjectName(QStringLiteral("quickAddManualTab"));
    profileTab->setText(qaTr("New profile"));
    profileTab->setCheckable(true);
    profileTab->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    profileTab->setCursor(Qt::PointingHandCursor);
    groupTab = new QToolButton(manualTabs);
    groupTab->setObjectName(QStringLiteral("quickAddManualTab"));
    groupTab->setText(qaTr("New group"));
    groupTab->setCheckable(true);
    groupTab->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    groupTab->setCursor(Qt::PointingHandCursor);
    auto *tabGroup = new QButtonGroup(this);
    tabGroup->setExclusive(true);
    tabGroup->addButton(profileTab);
    tabGroup->addButton(groupTab);
    tabsLayout->addWidget(profileTab);
    tabsLayout->addWidget(groupTab);
    tabsLayout->addStretch(1);
    manualLayout->addWidget(manualTabs);

    manualStack = new QStackedWidget(manualContainer);
    manualStack->setObjectName(QStringLiteral("quickAddManualStack"));

    auto *profilePage = new QWidget(manualStack);
    auto *profilePageLayout = new QVBoxLayout(profilePage);
    profilePageLayout->setContentsMargins(0, 0, 0, 0);
    profilePageLayout->setSpacing(10);
    auto *profileFirstRow = new QWidget(profilePage);
    auto *profileFirstLayout = new QHBoxLayout(profileFirstRow);
    profileFirstLayout->setContentsMargins(0, 0, 0, 0);
    profileFirstLayout->setSpacing(10);
    profileType = new QComboBox(profileFirstRow);
    profileType->setObjectName(QStringLiteral("quickAddInput"));
    profileType->addItem(QStringLiteral("VLESS"), QStringLiteral("vless"));
    profileType->addItem(QStringLiteral("Trojan"), QStringLiteral("trojan"));
    profileType->addItem(QStringLiteral("Hysteria 2"), QStringLiteral("hysteria"));
    profileType->addItem(QStringLiteral("Shadowsocks"), QStringLiteral("shadowsocks"));
    profileType->addItem(QStringLiteral("WireGuard"), QStringLiteral("wireguard"));
    profileType->addItem(QStringLiteral("SOCKS"), QStringLiteral("socks"));
    profileType->addItem(QStringLiteral("HTTP"), QStringLiteral("http"));
    profileType->addItem(qaTr("Custom"), QStringLiteral("outbound"));
    profileGroup = new QComboBox(profileFirstRow);
    profileGroup->setObjectName(QStringLiteral("quickAddInput"));
    profileFirstLayout->addWidget(makeField(qaTr("Type"), profileType, profileFirstRow), 1);
    profileFirstLayout->addWidget(makeField(qaTr("Add to group"), profileGroup, profileFirstRow), 1);
    profilePageLayout->addWidget(profileFirstRow);
    profileName = new QLineEdit(profilePage);
    profileName->setObjectName(QStringLiteral("quickAddInput"));
    profileName->setPlaceholderText(qaTr("For example, Frankfurt · VLESS"));
    profilePageLayout->addWidget(makeField(qaTr("Profile name"), profileName, profilePage));
    auto *addressRow = new QWidget(profilePage);
    auto *addressLayout = new QHBoxLayout(addressRow);
    addressLayout->setContentsMargins(0, 0, 0, 0);
    addressLayout->setSpacing(10);
    profileAddress = new QLineEdit(addressRow);
    profileAddress->setObjectName(QStringLiteral("quickAddInput"));
    profileAddress->setPlaceholderText(QStringLiteral("server.example"));
    profilePort = new QLineEdit(addressRow);
    profilePort->setObjectName(QStringLiteral("quickAddInput"));
    profilePort->setPlaceholderText(QStringLiteral("443"));
    profilePort->setInputMethodHints(Qt::ImhDigitsOnly);
    addressLayout->addWidget(makeField(qaTr("Address"), profileAddress, addressRow), 2);
    addressLayout->addWidget(makeField(qaTr("Port"), profilePort, addressRow), 1);
    profilePageLayout->addWidget(addressRow);
    auto *profileNote = new QLabel(qaTr("→  The regular settings for the selected protocol open next"), profilePage);
    profileNote->setObjectName(QStringLiteral("quickAddNote"));
    profilePageLayout->addWidget(profileNote);
    manualStack->addWidget(profilePage);

    auto *groupPage = new QWidget(manualStack);
    auto *groupPageLayout = new QVBoxLayout(groupPage);
    groupPageLayout->setContentsMargins(0, 0, 0, 0);
    groupPageLayout->setSpacing(10);
    auto *groupFirstRow = new QWidget(groupPage);
    auto *groupFirstLayout = new QHBoxLayout(groupFirstRow);
    groupFirstLayout->setContentsMargins(0, 0, 0, 0);
    groupFirstLayout->setSpacing(10);
    groupName = new QLineEdit(groupFirstRow);
    groupName->setObjectName(QStringLiteral("quickAddInput"));
    groupName->setPlaceholderText(qaTr("For example, Work"));
    groupType = new QComboBox(groupFirstRow);
    groupType->setObjectName(QStringLiteral("quickAddInput"));
    groupType->addItem(qaTr("Local"), QStringLiteral("local"));
    groupType->addItem(qaTr("Subscription"), QStringLiteral("subscription"));
    groupFirstLayout->addWidget(makeField(qaTr("Group name"), groupName, groupFirstRow), 1);
    groupFirstLayout->addWidget(makeField(qaTr("Group type"), groupType, groupFirstRow), 1);
    groupPageLayout->addWidget(groupFirstRow);
    groupUrl = new QLineEdit(groupPage);
    groupUrl->setObjectName(QStringLiteral("quickAddInput"));
    groupUrl->setPlaceholderText(QStringLiteral("https://…"));
    groupPageLayout->addWidget(makeField(qaTr("Subscription link"), groupUrl, groupPage));
    auto *groupNote = new QLabel(qaTr("Profiles can be added to a local group immediately after it is created"), groupPage);
    groupNote->setObjectName(QStringLiteral("quickAddNote"));
    groupNote->setWordWrap(true);
    groupPageLayout->addWidget(groupNote);
    manualStack->addWidget(groupPage);
    manualLayout->addWidget(manualStack);
    manualContainer->hide();
    bodyLayout->addWidget(manualContainer);
    cardLayout->addWidget(body);

    auto *footer = new QFrame(card);
    footer->setObjectName(QStringLiteral("quickAddFooter"));
    auto *footerLayout = new QHBoxLayout(footer);
    footerLayout->setContentsMargins(11, 9, 11, 9);
    footerLayout->setSpacing(10);
    manualButton = new QPushButton(qaTr("Add manually"), footer);
    manualButton->setObjectName(QStringLiteral("quickAddManualButton"));
    manualButton->setCursor(Qt::PointingHandCursor);
    footerLayout->addWidget(manualButton);
    footerLayout->addStretch(1);
    enterHint = new QLabel(qaTr("Enter"), footer);
    enterHint->setObjectName(QStringLiteral("quickAddEnterHint"));
    footerLayout->addWidget(enterHint);
    primaryButton = new QPushButton(qaTr("Add"), footer);
    primaryButton->setObjectName(QStringLiteral("quickAddPrimaryButton"));
    primaryButton->setCursor(Qt::PointingHandCursor);
    primaryButton->setDefault(true);
    primaryButton->setEnabled(false);
    footerLayout->addWidget(primaryButton);
    cardLayout->addWidget(footer);

    overlayLayout->addWidget(card, 0, Qt::AlignHCenter | Qt::AlignTop);
    overlayLayout->addStretch(1);

    const QString styleTemplate = QStringLiteral(R"(
QWidget#quickAddOverlay { background: rgba(4, 5, 8, 174); color: #F1F3F5; }
QFrame#quickAddCard {
    background: #1B1E23; border: 1px solid #4A4F57; border-radius: 13px;
}
QWidget#quickAddHeading, QWidget#quickAddBody, QWidget#quickAddField,
QWidget#quickAddManualStack, QWidget#quickAddManualStack > QWidget {
    background: transparent; border: none;
}
QLabel#quickAddHeadingIcon { background: #182530; border: none; border-radius: 8px; }
QLabel#quickAddTitle { color: #F1F3F5; font-size: 16px; font-weight: 600; background: transparent; }
QLabel#quickAddSubtitle { color: #747C86; font-size: 12px; background: transparent; }
QLabel#quickAddEscape {
    color: #747C86; background: #171B21; border: 1px solid #2F3136;
    border-radius: 5px; padding: 3px 6px; font-size: 11px;
}
QLineEdit#quickAddLinkInput {
    min-height: 34px; color: #F1F3F5; background: #171B21;
    border: 1px solid #4A4F57; border-radius: 9px; padding: 9px 12px 9px 7px;
    font-size: 15px;
}
QLineEdit#quickAddLinkInput:focus { border: 2px solid #237AE9; padding: 8px 11px 8px 6px; }
QFrame#quickAddDetected { background: #182530; border: none; border-radius: 8px; }
QLabel#quickAddDetectedTitle { color: #F1F3F5; font-weight: 600; background: transparent; }
QLabel#quickAddDetectedMeta { color: #A4ABB4; font-size: 12px; background: transparent; }
QLabel#quickAddDetectedKind {
    color: #A4ABB4; background: transparent; border: 1px solid #237AE9;
    border-radius: 5px; padding: 3px 7px; font-size: 11px;
}
QLabel#quickAddValidation { color: #C42B35; font-size: 12px; background: transparent; }
QFrame#quickAddManualTabs { background: transparent; border: none; border-bottom: 1px solid #2F3136; }
QToolButton#quickAddManualTab {
    color: #747C86; background: transparent; border: none; border-bottom: 2px solid transparent;
    padding: 8px 2px 9px 2px; font-weight: 500;
}
QToolButton#quickAddManualTab:hover { color: #F1F3F5; }
QToolButton#quickAddManualTab:checked { color: #F1F3F5; border-bottom-color: #237AE9; }
QLabel#quickAddFieldLabel { color: #A4ABB4; font-size: 12px; background: transparent; }
QLineEdit#quickAddInput, QComboBox#quickAddInput {
    min-height: 27px; color: #F1F3F5; background: #171B21;
    border: 1px solid #4A4F57; border-radius: 8px; padding: 7px 10px;
}
QLineEdit#quickAddInput:focus, QComboBox#quickAddInput:focus { border-color: #237AE9; }
QLineEdit#quickAddInput:disabled { color: #747C86; background: #222529; border-color: #2F3136; }
QComboBox#quickAddInput::drop-down { border: none; width: 26px; }
QLabel#quickAddNote { color: #747C86; font-size: 12px; background: transparent; }
QFrame#quickAddFooter { background: transparent; border: none; border-top: 1px solid #2F3136; }
QPushButton#quickAddManualButton {
    color: #A4ABB4; background: transparent; border: 1px solid transparent;
    border-radius: 7px; padding: 8px 9px;
}
QPushButton#quickAddManualButton:hover { color: #F1F3F5; background: #222529; }
QLabel#quickAddEnterHint { color: #747C86; font-size: 11px; background: transparent; }
QPushButton#quickAddPrimaryButton {
    color: #F1F3F5; background: #237AE9; border: 1px solid #237AE9;
    border-radius: 7px; padding: 8px 15px; font-weight: 600;
}
QPushButton#quickAddPrimaryButton:hover { background: #3B8BF0; border-color: #3B8BF0; }
QPushButton#quickAddPrimaryButton:disabled {
    color: #747C86; background: #222529; border-color: #2F3136;
}
)");
    themeManager->RegisterStyle(this, styleTemplate);

    connect(linkInput, &QLineEdit::textChanged, this, [this] { updateLinkValidation(); });
    connect(linkInput, &QLineEdit::returnPressed, this, [this] { submit(); });
    connect(manualButton, &QPushButton::clicked, this, [this] { setManualMode(!manualMode); });
    connect(primaryButton, &QPushButton::clicked, this, [this] { submit(); });
    connect(profileTab, &QToolButton::clicked, this, [this] { setManualPage(ManualPage::Profile); });
    connect(groupTab, &QToolButton::clicked, this, [this] { setManualPage(ManualPage::Group); });
    connect(groupType, &QComboBox::currentIndexChanged, this, [this] { updateGroupUrlState(); });

    const QList<QLineEdit *> manualEdits{profileName, profileAddress, profilePort, groupName, groupUrl};
    for (auto *edit : manualEdits) {
        connect(edit, &QLineEdit::textChanged, this, [this] { updateManualValidation(); });
        connect(edit, &QLineEdit::returnPressed, this, [this] { submit(); });
    }

    connect(themeManager, &ThemeManager::themeChanged, this, [this] { retintIcons(); });
    retintIcons();
}

QuickAddOverlay::~QuickAddOverlay() {
    if (parentWidget() != nullptr) parentWidget()->removeEventFilter(this);
    if (qApp != nullptr) qApp->removeEventFilter(this);
}

void QuickAddOverlay::open(const QList<QPair<int, QString>> &groups, int currentGroupId) {
    reset(groups, currentGroupId);
    if (parentWidget() != nullptr) setGeometry(parentWidget()->rect());
    show();
    raise();
    linkInput->setFocus(Qt::PopupFocusReason);
}

bool QuickAddOverlay::eventFilter(QObject *watched, QEvent *event) {
    if (watched == parentWidget() && event->type() == QEvent::Resize)
        setGeometry(parentWidget()->rect());

    // MainWindow owns Return as the global start shortcut.  While this overlay
    // is up, Enter and Escape belong to it; accepting ShortcutOverride keeps
    // those keys from leaking through to start/stop or another window action.
    if (isVisible() && event->type() == QEvent::ShortcutOverride) {
        const auto *key = static_cast<QKeyEvent *>(event);
        if (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter || key->key() == Qt::Key_Escape) {
            event->accept();
            return true;
        }
    }
    if (isVisible() && event->type() == QEvent::KeyPress) {
        const auto *key = static_cast<QKeyEvent *>(event);
        if (key->key() == Qt::Key_Escape) {
            closeOverlay();
            return true;
        }
        if (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) {
            submit();
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

void QuickAddOverlay::mousePressEvent(QMouseEvent *event) {
    if (card != nullptr && !card->geometry().contains(event->position().toPoint())) {
        closeOverlay();
        event->accept();
        return;
    }
    QWidget::mousePressEvent(event);
}

void QuickAddOverlay::reset(const QList<QPair<int, QString>> &groups, int currentGroupId) {
    linkInput->clear();
    validation->clear();
    detectedFrame->hide();
    detectedLinkValid = false;

    profileGroup->clear();
    int currentIndex = -1;
    for (const auto &[id, name] : groups) {
        profileGroup->addItem(name, id);
        if (id == currentGroupId) currentIndex = profileGroup->count() - 1;
    }
    if (currentIndex >= 0) profileGroup->setCurrentIndex(currentIndex);

    profileType->setCurrentIndex(0);
    profileName->clear();
    profileAddress->clear();
    profilePort->clear();
    groupName->clear();
    groupType->setCurrentIndex(0);
    groupUrl->clear();
    groupUrl->setEnabled(false);
    setManualMode(false);
    primaryButton->setEnabled(false);
}

void QuickAddOverlay::setManualMode(bool manual) {
    manualMode = manual;
    quickPage->setVisible(!manual);
    manualContainer->setVisible(manual);
    if (manual) {
        title->setText(qaTr("Add manually"));
        subtitle->setText(qaTr("Create a profile or a separate group"));
        manualButton->setText(qaTr("Back to link"));
        setManualPage(ManualPage::Profile, isVisible());
    } else {
        title->setText(qaTr("Add a group or profile"));
        subtitle->setText(qaTr("Throned will detect the link format"));
        manualButton->setText(qaTr("Add manually"));
        primaryButton->setText(qaTr("Add"));
        updateLinkValidation();
        if (isVisible()) linkInput->setFocus(Qt::TabFocusReason);
    }
    card->adjustSize();
}

void QuickAddOverlay::setManualPage(ManualPage page, bool focusFirst) {
    manualPage = page;
    const bool profile = page == ManualPage::Profile;
    profileTab->setChecked(profile);
    groupTab->setChecked(!profile);
    manualStack->setCurrentIndex(profile ? 0 : 1);
    updateManualValidation();
    if (focusFirst && isVisible())
        (profile ? profileName : groupName)->setFocus(Qt::TabFocusReason);
}

void QuickAddOverlay::updateLinkValidation() {
    if (manualMode) return;
    const QString value = linkInput->text().trimmed();
    detectedLinkValid = false;
    detectedFrame->hide();
    validation->clear();

    if (value.isEmpty()) {
        primaryButton->setEnabled(false);
        return;
    }

    const QUrl url(value);
    const QString scheme = url.scheme().toLower();
    const bool subscription = isSubscriptionScheme(scheme) && !url.host().isEmpty();
    const bool profile = isProfileScheme(scheme) && value.contains(QStringLiteral("://"));
    if (!url.isValid() || (!subscription && !profile)) {
        validation->setText(qaTr("Paste the complete subscription or profile link"));
        primaryButton->setEnabled(false);
        return;
    }

    detectedKind = subscription ? LinkKind::Subscription : LinkKind::Profile;
    detectedLinkValid = true;
    QString meta = url.host();
    if (meta.isEmpty()) meta = scheme.toUpper() + QStringLiteral(" · ") + qaTr("profile link");
    detectedMeta->setText(meta);
    detectedKindLabel->setText(subscription ? qaTr("Subscription") : qaTr("Profile"));
    detectedFrame->show();
    primaryButton->setEnabled(true);
}

void QuickAddOverlay::updateManualValidation() {
    if (!manualMode) return;
    bool valid = false;
    if (manualPage == ManualPage::Profile) {
        bool portOk = false;
        const int port = profilePort->text().toInt(&portOk);
        valid = !profileName->text().trimmed().isEmpty()
                && !profileAddress->text().trimmed().isEmpty()
                && portOk && port > 0 && port <= 65535 && profileGroup->currentIndex() >= 0;
        primaryButton->setText(qaTr("Continue"));
    } else {
        const bool subscription = groupType->currentData().toString() == QStringLiteral("subscription");
        const QUrl url(groupUrl->text().trimmed());
        const bool urlOk = !subscription
            || (url.isValid() && isSubscriptionScheme(url.scheme().toLower()) && !url.host().isEmpty());
        valid = !groupName->text().trimmed().isEmpty() && urlOk;
        primaryButton->setText(qaTr("Create"));
    }
    primaryButton->setEnabled(valid);
}

void QuickAddOverlay::updateGroupUrlState() {
    const bool subscription = groupType->currentData().toString() == QStringLiteral("subscription");
    groupUrl->setEnabled(subscription);
    if (!subscription) groupUrl->clear();
    updateManualValidation();
    if (subscription && manualMode && manualPage == ManualPage::Group && isVisible())
        groupUrl->setFocus(Qt::TabFocusReason);
}

void QuickAddOverlay::submit() {
    if (!primaryButton->isEnabled()) return;

    if (!manualMode) {
        if (!detectedLinkValid || !callbacks.addLink) return;
        const LinkKind kind = detectedKind;
        const QString value = linkInput->text().trimmed();
        closeOverlay();
        callbacks.addLink(kind, value);
        return;
    }

    if (manualPage == ManualPage::Profile) {
        if (!callbacks.addProfile) return;
        const QString type = profileType->currentData().toString();
        const int groupId = profileGroup->currentData().toInt();
        const QString name = profileName->text().trimmed();
        const QString address = profileAddress->text().trimmed();
        const QString port = profilePort->text().trimmed();
        closeOverlay();
        callbacks.addProfile(type, groupId, name, address, port);
        return;
    }

    if (!callbacks.addGroup) return;
    const QString name = groupName->text().trimmed();
    const QString url = groupType->currentData().toString() == QStringLiteral("subscription")
        ? groupUrl->text().trimmed() : QString();
    closeOverlay();
    callbacks.addGroup(name, url);
}

void QuickAddOverlay::closeOverlay() {
    hide();
    if (parentWidget() != nullptr) parentWidget()->setFocus(Qt::PopupFocusReason);
}

void QuickAddOverlay::retintIcons() {
    const auto colors = themeManager->Colors();
    headingIcon->setPixmap(MaterialIcon::pixmap(MaterialIcon::Glyph::Add, colors.accent, 19));
    linkAction->setIcon(MaterialIcon::icon(MaterialIcon::Glyph::Public, colors.textSubtle, 18));
    detectedIcon->setPixmap(MaterialIcon::pixmap(MaterialIcon::Glyph::Check, colors.success, 19));
    profileTab->setIcon(MaterialIcon::icon(MaterialIcon::Glyph::Apps, colors.textMuted, 16));
    groupTab->setIcon(MaterialIcon::icon(MaterialIcon::Glyph::Folder, colors.textMuted, 16));
    manualButton->setIcon(MaterialIcon::icon(MaterialIcon::Glyph::Tune, colors.textMuted, 16));
    primaryButton->setIcon(MaterialIcon::icon(MaterialIcon::Glyph::Add, colors.text, 16));
}
