#include "include/ui/stats/diagnostics_window.h"
#include "include/ui/setting/ThemeManager.hpp"
#include "include/ui/widget/MaterialIcon.h"
#include "include/ui/widget/ThronedTitleBar.h"
#include "include/ui/widget/ThronedWindowChrome.h"

#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QDateTime>
#include <QFileInfo>
#include <QFrame>
#include <QGridLayout>
#include <QHeaderView>
#include <QMouseEvent>
#include <QPainter>
#include <QLabel>
#include <QLineEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QScreen>
#include <QSignalBlocker>
#include <QTabWidget>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>
#include <algorithm>

namespace {
QString text(const std::optional<std::string> &value) { return QString::fromStdString(value.value_or("")); }
QString processKey(const libcore::ConnectionMetaData &c) { return text(c.process_path).isEmpty() ? text(c.process) : text(c.process_path); }
QString processName(const libcore::ConnectionMetaData &c) { return text(c.process).isEmpty() ? DiagnosticsWindow::tr("Unknown process") : text(c.process); }
QString route(const libcore::ConnectionMetaData &c) { return text(c.outbound).isEmpty() ? DiagnosticsWindow::tr("Unknown outbound") : text(c.outbound); }
QString rule(const libcore::ConnectionMetaData &c) { return text(c.matched_rule).isEmpty() ? DiagnosticsWindow::tr("Default route (no rule reported)") : text(c.matched_rule); }
QString bytes(qint64 value) { return QLocale().formattedDataSize(value); }
QLabel *label(const QString &value, const char *role = nullptr) {
    auto *widget = new QLabel(value);
    widget->setTextFormat(Qt::PlainText);
    widget->setWordWrap(true);
    widget->setTextInteractionFlags(Qt::TextSelectableByMouse);
    if (role) widget->setProperty("diagnosticRole", role);
    return widget;
}
QPushButton *button(const QString &value, const char *name) {
    auto *widget = new QPushButton(value);
    widget->setObjectName(QLatin1String(name));
    widget->setCursor(Qt::PointingHandCursor);
    widget->setAutoDefault(false);
    widget->setIconSize(QSize(18, 18));
    return widget;
}
QColor toneColor(const QString &tone) {
    const auto c = themeManager()->Colors();
    return tone == "ok" ? c.success : tone == "error" ? c.danger : tone == "warning" ? c.warning : c.controlInactive;
}
// The core reports a rule the way it is written in the config; a user reading it in a
// support chat needs to know which condition caught them, not the exact syntax.
QString explainRuleText(const QString &rule) {
    const auto conditions = rule.section(QStringLiteral("=>"), 0, 0);
    const auto action = rule.section(QStringLiteral("=>"), 1);
    QStringList reasons;
    if (action.contains(QStringLiteral("reject")))
        reasons << DiagnosticsWindow::tr("The rule blocks this traffic.");
    if (conditions.contains(QStringLiteral("process_name")) || conditions.contains(QStringLiteral("process_path")))
        reasons << DiagnosticsWindow::tr("Chosen by the program that opened the connection.");
    if (conditions.contains(QStringLiteral("domain")) || conditions.contains(QStringLiteral("geosite")))
        reasons << DiagnosticsWindow::tr("Chosen by the domain.");
    if (conditions.contains(QStringLiteral("geoip")) || conditions.contains(QStringLiteral("ip_")))
        reasons << DiagnosticsWindow::tr("Chosen by the destination IP address.");
    if (conditions.contains(QStringLiteral("network=")))
        reasons << DiagnosticsWindow::tr("Chosen by the protocol, not by the address.");
    if (conditions.contains(QStringLiteral("port")))
        reasons << DiagnosticsWindow::tr("Chosen by the destination port.");
    return reasons.isEmpty() ? DiagnosticsWindow::tr("Reported by the core as written in the routing profile.")
                             : reasons.join(QLatin1Char(' '));
}
QString targetURL(const QString &domain, const QString &dest) {
    QUrl endpoint(QStringLiteral("tcp://") + dest);
    const auto host = domain.isEmpty() ? endpoint.host() : domain;
    if (host.isEmpty()) return {};
    const int port = endpoint.port(443);
    QUrl url;
    url.setScheme(port == 80 ? QStringLiteral("http") : QStringLiteral("https"));
    url.setHost(host);
    if (port != 80 && port != 443) url.setPort(port);
    return url.toString();
}
}

// One stage of the request, drawn as a node on a rail with its duration as a bar on
// a scale shared by the whole path, so a slow phase is visible without reading times.
class DiagnosticStep final : public QWidget {
public:
    explicit DiagnosticStep(bool first, bool last, QWidget *parent = nullptr)
        : QWidget(parent), isFirst(first), isLast(last) {
        setObjectName(QStringLiteral("diagnosticStep"));
        setCursor(Qt::PointingHandCursor);
        setAttribute(Qt::WA_Hover);
        auto *grid = new QGridLayout(this);
        grid->setContentsMargins(0, 6, 4, 6);
        grid->setHorizontalSpacing(10);
        grid->setVerticalSpacing(2);
        grid->setColumnMinimumWidth(0, RailWidth);
        title = label({}, "stepTitle");
        title->setWordWrap(false);
        title->setTextInteractionFlags(Qt::NoTextInteraction);
        grid->addWidget(title, 0, 1);
        bar = new QWidget(this);
        bar->setFixedSize(132, 18);
        bar->setAttribute(Qt::WA_TransparentForMouseEvents);
        grid->addWidget(bar, 0, 2);
        time = label({}, "stepTime");
        time->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        time->setMinimumWidth(58);
        time->setTextInteractionFlags(Qt::NoTextInteraction);
        grid->addWidget(time, 0, 3);
        detail = label({}, "muted");
        detail->setContentsMargins(0, 2, 0, 6);
        detail->hide();
        grid->addWidget(detail, 1, 1, 1, 3);
        grid->setColumnStretch(1, 1);
    }

    void setContent(const QString &heading, const QString &body, const QString &state, qint64 duration) {
        title->setText(heading);
        detail->setText(body);
        detail->setVisible(expanded && !body.isEmpty());
        tone = state;
        ms = duration;
        time->setText(duration < 0 ? QString() : DiagnosticsWindow::tr("%1 ms").arg(duration));
        update();
    }
    // The bar is a slice of the whole request, offset by everything before it, so the
    // rows read left-to-right as elapsed time rather than as four unrelated meters.
    void setSpan(qint64 startedAt, qint64 total) { offset = startedAt; scale = total; update(); }
    void setExpanded(bool value) {
        expanded = value;
        detail->setVisible(value && !detail->text().isEmpty());
    }
    [[nodiscard]] QString heading() const { return title->text(); }
    [[nodiscard]] QString body() const { return detail->text(); }

    QString tone;
    qint64 ms = -1;

protected:
    void paintEvent(QPaintEvent *) override {
        const auto colors = themeManager()->Colors();
        const QColor accentTone = toneColor(tone);
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const int centerX = RailWidth / 2;
        const int centerY = bar->geometry().center().y();
        painter.setPen(QPen(colors.border, 1.5));
        if (!isFirst) painter.drawLine(centerX, 0, centerX, centerY - 7);
        if (!isLast) painter.drawLine(centerX, centerY + 7, centerX, height());
        painter.setPen(Qt::NoPen);
        painter.setBrush(hovered ? colors.surfaceHover : colors.window);
        painter.drawEllipse(QPointF(centerX, centerY), 5.5, 5.5);
        painter.setBrush(accentTone);
        painter.drawEllipse(QPointF(centerX, centerY), tone.isEmpty() ? 2.5 : 4.0, tone.isEmpty() ? 2.5 : 4.0);
        if (ms < 0 || scale <= 0) return;
        const QRectF track(bar->geometry().x(), centerY - 2.5, bar->width(), 5);
        painter.setBrush(colors.surfaceRaised);
        painter.drawRoundedRect(track, 2.5, 2.5);
        const qreal start = track.width() * qBound(0.0, qreal(offset) / qreal(scale), 1.0);
        QRectF fill(track.x() + start, track.y(),
                    qMax(4.0, qMin(track.width() - start, track.width() * qreal(ms) / qreal(scale))), track.height());
        painter.setBrush(accentTone);
        painter.drawRoundedRect(fill, 2.5, 2.5);
    }
    void mousePressEvent(QMouseEvent *event) override {
        if (event->button() == Qt::LeftButton && !detail->text().isEmpty()) setExpanded(!expanded);
        QWidget::mousePressEvent(event);
    }
    void enterEvent(QEnterEvent *event) override { hovered = true; update(); QWidget::enterEvent(event); }
    void leaveEvent(QEvent *event) override { hovered = false; update(); QWidget::leaveEvent(event); }

private:
    static constexpr int RailWidth = 22;
    QLabel *title;
    QLabel *detail;
    QLabel *time;
    QWidget *bar;
    qint64 scale = 0;
    qint64 offset = 0;
    bool expanded = false;
    bool hovered = false;
    const bool isFirst;
    const bool isLast;
};

DiagnosticsWindow::DiagnosticsWindow(QWidget *parent) : QDialog(parent, Qt::Window) {
    setObjectName(QStringLiteral("diagnosticsWindow"));
    setWindowTitle(tr("Diagnostics"));
    resize(880, 860);
    setMinimumSize(620, 560);
    if (auto *display = screen()) resize(size().boundedTo(display->availableGeometry().size() - QSize(32, 32)));
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(1, 1, 1, 1);
    root->setSpacing(0);
    root->addWidget(ThronedChrome::install(this, tr("Diagnostics")));
    tabs = new QTabWidget;
    tabs->setObjectName(QStringLiteral("diagnosticTabs"));
    root->addWidget(tabs, 1);

    // A page whose height is driven by one stretching widget must not sit in a scroll
    // area: the area would hand it its minimum and scroll the rest of the page instead.
    auto page = [this](const QString &title, bool scrollable) {
        auto *content = new QWidget;
        content->setObjectName(QStringLiteral("diagnosticPage"));
        auto *layout = new QVBoxLayout(content);
        layout->setContentsMargins(24, 18, 24, 16);
        layout->setSpacing(11);
        if (!scrollable) {
            tabs->addTab(content, title);
            return layout;
        }
        auto *scroll = new QScrollArea;
        scroll->setWidgetResizable(true);
        scroll->setFrameShape(QFrame::NoFrame);
        scroll->setWidget(content);
        tabs->addTab(scroll, title);
        return layout;
    };
    auto *site = page(tr("Site"), true);
    site->addWidget(label(tr("Check a website"), "heading"));
    site->addWidget(label(tr("A fresh request through the running connection"), "muted"));
    auto *form = new QHBoxLayout;
    address = new QLineEdit;
    address->setObjectName(QStringLiteral("diagnosticAddress"));
    address->setPlaceholderText(QStringLiteral("chatgpt.com"));
    address->setAccessibleName(tr("Website address"));
    address->setClearButtonEnabled(true);
    check = button(tr("Check"), "diagnosticCheck");
    check->setProperty("primary", true);
    form->addWidget(address, 1);
    form->addWidget(check);
    site->addLayout(form);
    auto *contextRow = new QHBoxLayout;
    siteContext = label(tr("The address is matched against your routing rules first, then requested through the outbound they choose."), "muted");
    siteContext->setObjectName(QStringLiteral("diagnosticContext"));
    contextRow->addWidget(siteContext, 1);
    resetRoute = button(tr("Any application"), "diagnosticDefault");
    resetRoute->hide();
    contextRow->addWidget(resetRoute);
    site->addLayout(contextRow);
    auto *verdict = new QFrame;
    verdict->setObjectName(QStringLiteral("diagnosticVerdict"));
    auto *verdictRow = new QHBoxLayout(verdict);
    verdictRow->setContentsMargins(16, 16, 16, 16);
    verdictRow->setSpacing(14);
    verdictIcon = new QLabel;
    verdictIcon->setFixedSize(26, 26);
    verdictRow->addWidget(verdictIcon, 0, Qt::AlignTop);
    auto *verdictText = new QVBoxLayout;
    verdictText->setSpacing(6);
    verdictTitle = label(tr("Enter an address to begin"), "title");
    verdictTitle->setObjectName(QStringLiteral("diagnosticVerdictTitle"));
    verdictDetail = label(tr("Start a connection in Throned, then check where the request stops."), "muted");
    verdictText->addWidget(verdictTitle);
    verdictText->addWidget(verdictDetail);
    verdictAction = button({}, "diagnosticVerdictAction");
    verdictAction->hide();
    verdictText->addWidget(verdictAction, 0, Qt::AlignLeft);
    verdictRow->addLayout(verdictText, 1);
    site->addWidget(verdict);
    progress = new QProgressBar;
    progress->setRange(0, 0);
    progress->setTextVisible(false);
    progress->setFixedHeight(3);
    progress->hide();
    site->addWidget(progress);
    pathCaption = label(tr("Request path"), "muted");
    site->addWidget(pathCaption);
    auto *path = new QFrame;
    path->setObjectName(QStringLiteral("diagnosticPath"));
    auto *pathLayout = new QVBoxLayout(path);
    pathLayout->setContentsMargins(14, 10, 14, 10);
    pathLayout->setSpacing(0);
    for (int i = 0; i < 4; ++i) {
        auto *step = new DiagnosticStep(i == 0, i == 3, path);
        pathLayout->addWidget(step);
        steps.append(step);
    }
    site->addWidget(path);
    setStep(0, tr("Route · not checked"), tr("The routing rules decide the outbound before anything is sent."), {});
    setStep(1, tr("Connection and DNS · not checked"), tr("DNS may be resolved by the proxy. Its time is included in connection establishment."), {});
    setStep(2, tr("TLS · not checked"), tr("Certificate and hostname verification remain enabled."), {});
    setStep(3, tr("HTTP · not checked"), tr("One HEAD request, without following redirects or sending account credentials."), {});
    site->addWidget(label(tr("This test does not reproduce browser DNS, extensions, application login or UDP traffic."), "muted"));
    site->addStretch(1);

    auto *app = page(tr("Application"), false);
    app->addWidget(label(tr("Application connections"), "heading"));
    app->addWidget(label(tr("Observe traffic passing through Throned while reproducing the problem."), "muted"));
    auto *appForm = new QHBoxLayout;
    apps = new QComboBox;
    apps->setObjectName(QStringLiteral("diagnosticApps"));
    apps->setAccessibleName(tr("Application"));
    apps->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    apps->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    apps->addItem(tr("All applications"), QString());
    observe = button(tr("Start observation"), "diagnosticObserve");
    observe->setProperty("primary", true);
    appForm->addWidget(apps, 1);
    appForm->addWidget(observe);
    app->addLayout(appForm);
    captureStatus = label(tr("Current connections. Start observation to retain completed connections."), "muted");
    captureStatus->setObjectName(QStringLiteral("diagnosticCaptureStatus"));
    app->addWidget(captureStatus);
    summary = label({}, "title");
    app->addWidget(summary);
    connections = new QTreeWidget;
    connections->setObjectName(QStringLiteral("diagnosticConnections"));
    connections->setHeaderLabels({tr("Address / protocol"), tr("Outbound"), tr("Traffic / state")});
    connections->setRootIsDecorated(false);
    connections->setUniformRowHeights(true);
    connections->setSelectionMode(QAbstractItemView::SingleSelection);
    connections->setMinimumHeight(150);
    connections->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    connections->header()->setSectionResizeMode(1, QHeaderView::Interactive);
    connections->header()->setSectionResizeMode(2, QHeaderView::Interactive);
    connections->setColumnWidth(1, 155);
    connections->setColumnWidth(2, 175);
    app->addWidget(connections, 1);
    auto *detailFrame = new QFrame;
    detailFrame->setObjectName(QStringLiteral("diagnosticConnectionDetail"));
    auto *detailLayout = new QVBoxLayout(detailFrame);
    detailLayout->setContentsMargins(16, 14, 16, 14);
    detailLayout->setSpacing(10);
    connectionTitle = label(tr("Select a connection"), "title");
    connectionTitle->setObjectName(QStringLiteral("diagnosticConnectionTitle"));
    connectionDetail = label(tr("The list includes only connections visible to the core."), "muted");
    detailLayout->addWidget(connectionTitle);
    detailLayout->addWidget(connectionDetail);
    auto *detailActions = new QHBoxLayout;
    diagnoseAddress = button(tr("Check this address"), "diagnosticConnectionCheck");
    diagnoseAddress->setEnabled(false);
    explainRule = new QToolButton;
    explainRule->setObjectName(QStringLiteral("diagnosticRuleButton"));
    explainRule->setText(tr("Matched rule"));
    explainRule->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    explainRule->setCheckable(true);
    explainRule->setEnabled(false);
    detailActions->addWidget(diagnoseAddress);
    detailActions->addWidget(explainRule);
    detailActions->addStretch();
    detailLayout->addLayout(detailActions);
    ruleDetail = label({}, "muted");
    ruleDetail->setObjectName(QStringLiteral("diagnosticRuleDetail"));
    ruleDetail->hide();
    detailLayout->addWidget(ruleDetail);
    app->addWidget(detailFrame);
    app->addWidget(label(tr("HTTPS contents are not recorded. Byte counters cover the connection's lifetime; no reply does not by itself prove a failure."), "muted"));
    auto *footer = new QHBoxLayout;
    footer->setContentsMargins(24, 12, 24, 14);
    report = button(tr("Copy report"), "diagnosticCopyReport");
    report->setEnabled(false);
    footer->addWidget(report);
    footer->addStretch();
    auto *close = button(tr("Close"), "diagnosticClose");
    footer->addWidget(close);
    root->addLayout(footer);
    connect(close, &QPushButton::clicked, this, &QDialog::close);
    connect(report, &QPushButton::clicked, this, &DiagnosticsWindow::copyReport);
    connect(check, &QPushButton::clicked, this, &DiagnosticsWindow::startSite);
    connect(address, &QLineEdit::returnPressed, this, &DiagnosticsWindow::startSite);
    connect(resetRoute, &QPushButton::clicked, this, [this] {
        pinnedProcess.clear(); pinnedOutbound.clear(); resetRoute->hide();
        siteContext->setText(tr("The address is matched against your routing rules first, then requested through the outbound they choose."));
    });
    connect(verdictAction, &QPushButton::clicked, this, [this] {
        if (verdictActionKind == QLatin1String("rules")) emit routingRulesRequested();
        else if (verdictActionKind == QLatin1String("reachability")) emit reachabilityRequested();
    });
    connect(observe, &QPushButton::clicked, this, &DiagnosticsWindow::toggleObservation);
    connect(apps, &QComboBox::currentIndexChanged, this, [this] { desiredProcess.clear(); rebuildConnections(); });
    connect(connections, &QTreeWidget::itemSelectionChanged, this, &DiagnosticsWindow::showConnection);
    connect(explainRule, &QToolButton::toggled, ruleDetail, &QWidget::setVisible);
    connect(diagnoseAddress, &QPushButton::clicked, this, [this] {
        const auto *group = currentGroup();
        if (group == nullptr || siteBusy) return;
        pinnedProcess = group->processPath;
        pinnedOutbound = group->outbound;
        address->setText(targetURL(group->domain, group->dest));
        resetRoute->setVisible(!pinnedProcess.isEmpty());
        siteContext->setText(pinnedProcess.isEmpty()
            ? tr("The address is matched against your routing rules first, then requested through the outbound they choose.")
            : tr("Rules are matched as if %1 opened the address. Application login is not reproduced.").arg(group->process));
        tabs->setCurrentIndex(0);
        address->setFocus();
    });
    poll = new QTimer(this);
    poll->setInterval(1000);
    connect(poll, &QTimer::timeout, this, &DiagnosticsWindow::requestConnections);
    connect(tabs, &QTabWidget::currentChanged, this, [this](int index) {
        report->setEnabled(index == 1 || !siteReport.isEmpty());
        if (index == 1) { poll->start(); requestConnections(); }
        else if (!recording) poll->stop();
    });
    themeManager()->RegisterStyle(this, QStringLiteral(R"(
QDialog#diagnosticsWindow, QWidget#diagnosticPage { background: #1B1E23; color: #F1F3F5; }
QDialog#diagnosticsWindow QLabel { background: transparent; color: #F1F3F5; }
QDialog#diagnosticsWindow QLabel[diagnosticRole="muted"] { color: #A4ABB4; }
QDialog#diagnosticsWindow QLabel[diagnosticRole="heading"] { font-size: 20px; font-weight: 600; }
QDialog#diagnosticsWindow QLabel[diagnosticRole="title"] { font-weight: 600; }
QTabWidget#diagnosticTabs::pane { border: 0; border-top: 1px solid #2F3136; }
QTabWidget#diagnosticTabs QTabBar::tab { background: #1B1E23; color: #A4ABB4; padding: 12px 24px; border-bottom: 2px solid transparent; }
QTabWidget#diagnosticTabs QTabBar::tab:selected { color: #F1F3F5; border-bottom: 2px solid #237AE9; }
QFrame#diagnosticVerdict, QFrame#diagnosticConnectionDetail { background: #171B21; border: 1px solid #2F3136; border-radius: 8px; }
QFrame#diagnosticPath { background: #171B21; border: 1px solid #2F3136; border-radius: 8px; }
QWidget#diagnosticStep QLabel[diagnosticRole="stepTitle"] { color: #F1F3F5; }
QWidget#diagnosticStep QLabel[diagnosticRole="stepTime"] { color: #747C86; }
QDialog#diagnosticsWindow QPushButton, QToolButton#diagnosticRuleButton { background: #222529; border: 1px solid #2F3136; border-radius: 6px; padding: 8px 12px; color: #F1F3F5; }
QDialog#diagnosticsWindow QPushButton:hover, QToolButton#diagnosticRuleButton:hover { background: #292D33; border-color: #4A4F57; }
QDialog#diagnosticsWindow QPushButton[primary="true"] { background: #193452; border-color: #237AE9; }
QDialog#diagnosticsWindow QPushButton:disabled { color: #747C86; border-color: #2F3136; }
QLineEdit#diagnosticAddress, QComboBox#diagnosticApps { background: #222529; color: #F1F3F5; border: 1px solid #2F3136; border-radius: 6px; padding: 8px 10px; min-height: 20px; }
QLineEdit#diagnosticAddress:focus, QComboBox#diagnosticApps:focus { border-color: #237AE9; }
QComboBox#diagnosticApps { padding-right: 28px; }
QComboBox#diagnosticApps::drop-down {
    subcontrol-origin: padding; subcontrol-position: center right;
    width: 26px; border: none; background: transparent;
}
QComboBox#diagnosticApps::down-arrow { image: url("%CHEVRON_DOWN%"); width: 14px; height: 14px; }
QTreeWidget#diagnosticConnections { background: #171B21; color: #F1F3F5; border: 1px solid #2F3136; border-radius: 6px; outline: 0; }
QTreeWidget#diagnosticConnections::item { padding: 9px 7px; border-bottom: 1px solid #2F3136; }
QTreeWidget#diagnosticConnections::item:selected { background: #143C48; color: #F1F3F5; }
QTreeWidget#diagnosticConnections QHeaderView::section { background: #222529; color: #A4ABB4; border: 0; padding: 8px; }
QDialog#diagnosticsWindow QProgressBar { background: #222529; border: 0; }
QDialog#diagnosticsWindow QProgressBar::chunk { background: #237AE9; }
    )"));
    connect(themeManager(), &ThemeManager::themeChanged, this, &DiagnosticsWindow::refreshTheme);
    refreshTheme();
}

void DiagnosticsWindow::refreshTheme() {
    const auto c = themeManager()->Colors();
    auto tone = [&](const QString &value) { return value == "ok" ? c.success : value == "error" ? c.danger : value == "warning" ? c.warning : c.textMuted; };
    check->setIcon(MaterialIcon::icon(MaterialIcon::Glyph::Search, c.text, 18));
    observe->setIcon(MaterialIcon::icon(recording ? MaterialIcon::Glyph::Block : MaterialIcon::Glyph::Process, c.text, 18));
    diagnoseAddress->setIcon(MaterialIcon::icon(MaterialIcon::Glyph::Public, c.text, 18));
    explainRule->setIcon(MaterialIcon::icon(MaterialIcon::Glyph::Routes, c.textMuted, 18));
    report->setIcon(MaterialIcon::icon(MaterialIcon::Glyph::Copy, c.textMuted, 18));
    tabs->setTabIcon(0, MaterialIcon::icon(MaterialIcon::Glyph::Public, c.textMuted, 18));
    tabs->setTabIcon(1, MaterialIcon::icon(MaterialIcon::Glyph::Process, c.textMuted, 18));
    verdictIcon->setPixmap(MaterialIcon::pixmap(verdictTone == "ok" ? MaterialIcon::Glyph::Check : verdictTone.isEmpty() ? MaterialIcon::Glyph::Search : MaterialIcon::Glyph::Shield, tone(verdictTone), 26));
    for (auto *step : steps) step->update();
}

void DiagnosticsWindow::setStep(int index, const QString &title, const QString &detail, const QString &tone, qint64 ms) {
    auto *step = steps[index];
    step->setContent(title, detail, tone, ms);
    step->setExpanded(tone == "error" || tone == "warning");
}

void DiagnosticsWindow::rescaleSteps() {
    qint64 total = 0;
    for (const auto *step : steps) total += qMax<qint64>(0, step->ms);
    qint64 elapsed = 0;
    for (auto *step : steps) {
        step->setSpan(elapsed, total);
        elapsed += qMax<qint64>(0, step->ms);
    }
    pathCaption->setText(total > 0 ? tr("Request path · %1 ms in total").arg(total) : tr("Request path"));
}

void DiagnosticsWindow::startSite() {
    if (siteBusy) return;
    QString raw = address->text().trimmed();
    if (!raw.contains(QStringLiteral("://"))) raw.prepend(QStringLiteral("https://"));
    QUrl url(raw, QUrl::StrictMode);
    if (!url.isValid() || url.host().isEmpty() || !url.userInfo().isEmpty() || url.hasFragment()
        || (url.scheme() != "https" && url.scheme() != "http") || url.port() == 0) {
        setVerdict("warning", tr("Enter a valid HTTP or HTTPS address"),
                   tr("Do not include credentials or a URL fragment."), {});
        siteReport.clear();
        report->setEnabled(false);
        for (int i = 0; i < steps.size(); ++i) setStep(i, tr("Not checked"), {}, {});
        return;
    }
    requestURL = QString::fromUtf8(url.toEncoded());
    routedOutbound.clear(); routedRule.clear(); routedAction.clear();
    siteBusy = true;
    check->setEnabled(false); address->setEnabled(false); resetRoute->setEnabled(false);
    diagnoseAddress->setEnabled(false);
    progress->show();
    setVerdict({}, tr("Checking %1…").arg(url.host()), tr("Route, connection, TLS and HTTP · up to 15 seconds"), {});
    siteReport.clear();
    report->setEnabled(false);
    for (int i = 0; i < steps.size(); ++i) setStep(i, tr("Waiting for result…"), {}, {});
    libcore::PreviewRouteRequest request;
    request.url = requestURL.toStdString();
    request.process_path = pinnedProcess.toStdString();
    request.network = "tcp";
    emit routeRequested(request);
}

// The route decides which outbound the probe uses, so it runs first and the probe
// only starts when the rules would actually let the address out.
void DiagnosticsWindow::applyRoutePreview(const libcore::PreviewRouteResponse &r, const QString &rpcError) {
    if (!siteBusy) return;
    const QString error = rpcError.isEmpty() ? text(r.error) : rpcError;
    routedOutbound = text(r.outbound_tag);
    routedRule = text(r.matched_rule);
    routedAction = text(r.action);
    if (!error.isEmpty() || routedOutbound.isEmpty()) {
        const bool blocked = routedAction == "reject" || routedAction == "hijack-dns";
        setStep(0, blocked ? tr("Route: blocked by a rule") : tr("Route unavailable"),
                error.isEmpty() ? routedRule : error, "error");
        setVerdict("error", blocked ? tr("A routing rule blocks this address") : tr("The route could not be resolved"),
                   blocked ? tr("Matched rule: %1. Nothing was sent — remove or narrow the rule to allow it.").arg(routedRule)
                           : error,
                   blocked ? QStringLiteral("rules") : QString());
        endSite();
        return;
    }
    QString detail = routedRule.isEmpty()
        ? tr("No rule matched, so the default outbound is used.")
        : tr("Matched rule: %1").arg(routedRule);
    if (!r.address_resolved.value_or(false))
        detail += QStringLiteral("\n") + tr("Rules that match on IP are not evaluated for a hostname.");
    if (pinnedProcess.isEmpty() && !routedRule.isEmpty())
        detail += QStringLiteral("\n") + tr("Process rules are only matched when an application is selected.");
    setStep(0, tr("Route: %1").arg(routedOutbound), detail, "ok");
    rescaleSteps();
    libcore::DiagnoseSiteRequest request;
    request.url = requestURL.toStdString();
    request.outbound_tag = routedOutbound.toStdString();
    emit siteRequested(request);
}

void DiagnosticsWindow::endSite() {
    siteBusy = false;
    check->setEnabled(true); address->setEnabled(true); resetRoute->setEnabled(true);
    progress->hide();
    showConnection();
    rescaleSteps();
    siteReport = tr("Throned · website diagnostics") + QStringLiteral("\n") + requestURL + QStringLiteral("\n")
        + QDateTime::currentDateTime().toString(Qt::ISODate) + QStringLiteral("\n\n");
    for (const auto *step : steps)
        siteReport += step->heading() + (step->ms < 0 ? QString() : QStringLiteral(" · ") + tr("%1 ms").arg(step->ms))
            + QStringLiteral("\n") + step->body() + QStringLiteral("\n\n");
    siteReport += verdictTitle->text() + QStringLiteral("\n") + verdictDetail->text();
    report->setEnabled(true);
    refreshTheme();
}

void DiagnosticsWindow::setVerdict(const QString &tone, const QString &title, const QString &detail, const QString &action) {
    verdictTone = tone;
    verdictTitle->setText(title);
    verdictDetail->setText(detail);
    verdictActionKind = action;
    verdictAction->setVisible(!action.isEmpty());
    if (action == QLatin1String("rules")) verdictAction->setText(tr("Open routing rules"));
    else if (action == QLatin1String("reachability")) verdictAction->setText(tr("Try the other servers"));
    refreshTheme();
}

void DiagnosticsWindow::applySiteResult(const libcore::DiagnoseSiteResponse &r, const QString &rpcError) {
    const QString error = rpcError.isEmpty() ? text(r.error) : rpcError;
    const QString stage = rpcError.isEmpty() ? text(r.error_stage) : QStringLiteral("core");
    const auto outbound = text(r.outbound_tag);
    const auto status = r.status.value_or(0);
    auto render = [&](int index, const QString &name, qint64 ms, const QString &details, const QString &key) {
        setStep(index, ms < 0 ? tr("%1 · not checked").arg(name) : name,
                stage == key ? error : details,
                ms < 0 ? QString() : stage == key ? QStringLiteral("error") : QStringLiteral("ok"), ms);
    };
    if (!outbound.isEmpty() && outbound != routedOutbound)
        setStep(0, tr("Route: %1").arg(outbound), routedRule, "ok");
    render(1, tr("Connection and DNS"), r.connect_ms.value_or(-1), tr("DNS may be resolved by the proxy; it has no separate timing."), "connect");
    render(2, tr("TLS"), r.tls_ms.value_or(-1), text(r.tls_version) + QStringLiteral("\n") + tr("Certificate and hostname verified."), "tls");
    if (QUrl(requestURL).scheme() == "http") setStep(2, tr("TLS · not used for HTTP"), tr("This URL uses unencrypted HTTP."), {});
    render(3, status > 0 ? tr("HTTP %1").arg(status) : tr("HTTP"), r.http_ms.value_or(-1), tr("HEAD request, no redirects, no login."), "http");
    if (stage == "connect")
        setVerdict("error", tr("The outbound %1 did not answer").arg(outbound),
                   tr("The request never left Throned, so this is the connection to the server, not the site. %1").arg(error),
                   QStringLiteral("reachability"));
    else if (stage == "tls")
        setVerdict("error", tr("TLS failed on the way to the site"),
                   tr("The server accepted the connection but the handshake did not verify. %1").arg(error), {});
    else if (stage == "http")
        setVerdict("error", tr("The outbound connected but the site stayed silent"),
                   tr("A connection through %1 was established and nothing answered. %2").arg(outbound, error),
                   QStringLiteral("reachability"));
    else if (!error.isEmpty())
        setVerdict("error", tr("Could not run the check"), error, {});
    else if (status >= 400)
        setVerdict("warning", tr("The site answered — with an error"),
                   tr("HTTP %1 came from the site itself over a working connection through %2, so Throned delivered the request.").arg(status).arg(outbound),
                   QStringLiteral("reachability"));
    else
        setVerdict("ok", status >= 300 ? tr("The site redirected") : tr("The site responded"),
                   tr("HTTP %1 through %2. Routing, connection and TLS all work for this address.").arg(status).arg(outbound), {});
    if (error.isEmpty() && status >= 300) {
        steps[3]->tone = verdictTone;
        steps[3]->setExpanded(status >= 400);
    }
    endSite();
}

void DiagnosticsWindow::showApplication(const QString &key) {
    desiredProcess = key;
    if (!key.isEmpty()) {
        int index = apps->findData(key);
        if (index < 0) { apps->addItem(QFileInfo(key).fileName(), key); index = apps->count()-1; }
        const QSignalBlocker block(apps);
        apps->setCurrentIndex(index);
    }
    tabs->setCurrentIndex(1);
    rebuildConnections();
    requestConnections();
}

void DiagnosticsWindow::requestConnections() {
    if (pollBusy || (captureFinished && !recording)) return;
    pollBusy = true;
    emit connectionsRequested();
}

void DiagnosticsWindow::toggleObservation() {
    if (recording) {
        recording = false; captureFinished = true;
        captureStopped = QDateTime::currentMSecsSinceEpoch();
        poll->stop();
        observe->setText(tr("Start observation"));
    } else {
        captured.clear(); captureFinished = false; recording = true;
        captureStarted = QDateTime::currentMSecsSinceEpoch(); captureStopped = 0;
        observe->setText(tr("Stop observation"));
        poll->start();
        requestConnections();
    }
    rebuildConnections(); refreshTheme();
}

void DiagnosticsWindow::applyConnections(const libcore::QueryConnectionsResp &r, const QString &error) {
    pollBusy = false;
    if (!error.isEmpty()) {
        captureStatus->setText(tr("Could not read connections: %1").arg(error));
        return;
    }
    if (captureFinished && !recording) return; // a late poll must not change a stopped recording
    latest.clear();
    auto ingest = [&](const libcore::ConnectionMetaData &c, bool closed) {
        const auto id = text(c.id);
        if (id.isEmpty()) return;
        if (!closed && latest.size() < MaxConnections) latest.insert(id, c);
        if (recording && (!closed || c.closed_at.value_or(0) >= captureStarted)
            && (captured.contains(id) || captured.size() < MaxConnections)) captured.insert(id, c);
        const auto key = processKey(c);
        if (!key.isEmpty() && apps->findData(key) < 0 && apps->count() < 300) {
            apps->addItem(processName(c), key);
            apps->setItemData(apps->count()-1, key, Qt::ToolTipRole);
        }
    };
    for (const auto &c : r.active) ingest(c, false);
    for (const auto &c : r.closed) ingest(c, true);
    rebuildConnections();
}

void DiagnosticsWindow::rebuildConnections() {
    const auto &rows = (recording || captureFinished) ? captured : latest;
    const auto filter = apps->currentData().toString();
    QHash<QString, ConnectionGroup> merged;
    QHash<QString, int> outboundUse;
    qint64 up = 0, down = 0;
    int total = 0;
    for (auto it = rows.cbegin(); it != rows.cend(); ++it) {
        const auto &c = it.value();
        if (!filter.isEmpty() && processKey(c) != filter) continue;
        const auto host = text(c.domain).isEmpty() ? text(c.dest) : text(c.domain);
        const auto key = processKey(c) + QLatin1Char('\x1f') + host + QLatin1Char('\x1f')
            + text(c.outbound) + QLatin1Char('\x1f') + text(c.network);
        auto &group = merged[key];
        if (group.count == 0) {
            group.key = key; group.host = host; group.domain = text(c.domain); group.dest = text(c.dest);
            group.process = processName(c); group.processPath = processKey(c);
            group.outbound = text(c.outbound); group.network = text(c.network);
        }
        if (group.matchedRule.isEmpty()) group.matchedRule = text(c.matched_rule);
        ++group.count;
        if (c.closed_at.value_or(0) > 0) ++group.closed;
        group.upload += c.upload.value_or(0);
        group.download += c.download.value_or(0);
        up += c.upload.value_or(0); down += c.download.value_or(0);
        ++total;
        if (!text(c.outbound).isEmpty()) outboundUse[text(c.outbound)] += 1;
    }
    // "direct while everything else is proxied" is the shape of a misrouted app, so it
    // is worth surfacing; it is only a hint, which is why it scores below hard signals.
    QString dominant;
    for (auto it = outboundUse.cbegin(); it != outboundUse.cend(); ++it)
        if (dominant.isEmpty() || it.value() > outboundUse.value(dominant)) dominant = it.key();
    groups = merged.values();
    for (auto &group : groups) {
        if (group.outbound.isEmpty()) group.suspicion = 3;
        else if (group.download == 0 && group.upload > 0) group.suspicion = 2;
        else if (!dominant.isEmpty() && group.outbound != dominant
                 && group.outbound.compare(QStringLiteral("direct"), Qt::CaseInsensitive) == 0) group.suspicion = 1;
    }
    std::sort(groups.begin(), groups.end(), [](const ConnectionGroup &a, const ConnectionGroup &b) {
        if (a.suspicion != b.suspicion) return a.suspicion > b.suspicion;
        if (a.count != b.count) return a.count > b.count;
        if (a.host != b.host) return a.host < b.host;
        return a.key < b.key;
    });
    syncConnectionRows();
    summary->setText(tr("%1 destinations · %2 connections · sent %3 · received %4")
                         .arg(groups.size()).arg(total).arg(bytes(up), bytes(down)));
    if (recording || captureFinished) {
        const auto elapsed = ((recording ? QDateTime::currentMSecsSinceEpoch() : captureStopped) - captureStarted) / 1000;
        captureStatus->setText((recording ? tr("Observing · %1 s. Reproduce the problem in the application.") : tr("Observation stopped · %1 s.")).arg(elapsed)
            + (captured.size() >= MaxConnections ? tr(" Limit reached: the first 1000 connections are retained.") : QString()));
    } else captureStatus->setText(latest.isEmpty() ? tr("No connections yet. Start Throned and open the application; process detection depends on the inbound and platform.") : tr("Current connections. Start observation to retain completed connections."));
    showConnection();
}

// Rows are reused rather than rebuilt: a poll a second through clear() would throw the
// scroll position and the selection away while the user is reading the list.
void DiagnosticsWindow::syncConnectionRows() {
    const QSignalBlocker block(connections);
    const int scroll = connections->verticalScrollBar()->value();
    QHash<QString, QTreeWidgetItem *> existing;
    for (int i = 0; i < connections->topLevelItemCount(); ++i) {
        auto *item = connections->topLevelItem(i);
        existing.insert(item->data(0, Qt::UserRole).toString(), item);
    }
    for (int i = 0; i < groups.size(); ++i) {
        const auto &group = groups[i];
        auto *item = existing.take(group.key);
        if (item == nullptr) {
            item = new QTreeWidgetItem;
            item->setData(0, Qt::UserRole, group.key);
            connections->insertTopLevelItem(i, item);
        } else if (connections->indexOfTopLevelItem(item) != i) {
            connections->insertTopLevelItem(i, connections->takeTopLevelItem(connections->indexOfTopLevelItem(item)));
        }
        QString subtitle = group.process + QStringLiteral(" · ") + group.network.toUpper();
        if (group.count > 1) subtitle += QStringLiteral(" · ") + tr("%1 connections").arg(group.count);
        item->setText(0, group.host + QStringLiteral("\n") + subtitle);
        item->setText(1, group.outbound.isEmpty() ? tr("Unknown outbound") : group.outbound);
        item->setText(2, QStringLiteral("↑ %1  ↓ %2\n").arg(bytes(group.upload), bytes(group.download))
            + (group.closed >= group.count ? tr("Closed") : tr("Tracked")));
        item->setToolTip(0, group.dest + QStringLiteral("\n") + group.processPath);
        item->setToolTip(1, group.matchedRule.isEmpty() ? tr("Default route (no rule reported)") : group.matchedRule);
    }
    for (auto *stale : existing) delete connections->takeTopLevelItem(connections->indexOfTopLevelItem(stale));
    if (!connections->currentItem() && connections->topLevelItemCount())
        connections->setCurrentItem(connections->topLevelItem(0));
    connections->verticalScrollBar()->setValue(scroll);
}

const DiagnosticsWindow::ConnectionGroup *DiagnosticsWindow::currentGroup() const {
    auto *item = connections->currentItem();
    if (item == nullptr) return nullptr;
    const auto key = item->data(0, Qt::UserRole).toString();
    for (const auto &group : groups)
        if (group.key == key) return &group;
    return nullptr;
}

void DiagnosticsWindow::showConnection() {
    auto *item = connections->currentItem();
    diagnoseAddress->setEnabled(false);
    explainRule->setEnabled(item != nullptr);
    if (!item) {
        connectionTitle->setText(tr("Select a connection"));
        connectionDetail->setText(tr("The list includes only connections visible to the core."));
        ruleDetail->clear();
        return;
    }
    const auto *group = currentGroup();
    if (group == nullptr) return;
    connectionTitle->setText(group->dest.isEmpty() ? group->host : group->dest);
    QString detail = group->process + QStringLiteral(" · ") + group->network.toUpper()
        + QStringLiteral("\n") + tr("Outbound: %1").arg(group->outbound.isEmpty() ? tr("Unknown outbound") : group->outbound);
    if (group->count > 1)
        detail += QStringLiteral("\n") + tr("%1 connections to this destination, %2 already closed.").arg(group->count).arg(group->closed);
    if (group->download == 0 && group->upload > 0)
        detail += QStringLiteral("\n") + tr("No return traffic recorded. This alone does not identify the cause.");
    if (group->network.compare(QStringLiteral("tcp"), Qt::CaseInsensitive) != 0)
        detail += QStringLiteral("\n") + tr("An HTTP check does not test this UDP or other non-TCP flow.");
    else if (targetURL(group->domain, group->dest).isEmpty())
        detail += QStringLiteral("\n") + tr("No usable destination address was reported.");
    else {
        diagnoseAddress->setEnabled(!siteBusy);
        detail += QStringLiteral("\n") + tr("Address check uses HTTPS unless the destination port is 80. You can edit the URL before checking.");
    }
    connectionDetail->setText(detail);
    // A wrapping QLabel reports one line as its minimum, so the layout would squeeze the
    // card down to that when the page is tight. Hold a floor for the lines it actually has.
    connectionDetail->setMinimumHeight(connectionDetail->fontMetrics().lineSpacing() * (detail.count(QLatin1Char('\n')) + 1) + 4);
    ruleDetail->setText(group->matchedRule.isEmpty()
        ? tr("No rule matched, so the default outbound is used.")
        : group->matchedRule + QStringLiteral("\n") + explainRuleText(group->matchedRule));
}

QString DiagnosticsWindow::connectionReport() const {
    QString result = tr("Throned · application connections") + QStringLiteral("\n") + QDateTime::currentDateTime().toString(Qt::ISODate) + QStringLiteral("\n") + captureStatus->text() + QStringLiteral("\n");
    // Ordered as shown, so the entry the window flagged first is first in the paste too.
    for (const auto &group : groups)
        result += QStringLiteral("\n%1 · %2 · %3 ×%4 → %5\n%6\n↑ %7 · ↓ %8\n")
            .arg(group.process, group.dest.isEmpty() ? group.host : group.dest, group.network)
            .arg(group.count)
            .arg(group.outbound.isEmpty() ? tr("Unknown outbound") : group.outbound,
                 group.matchedRule.isEmpty() ? tr("Default route (no rule reported)") : group.matchedRule,
                 bytes(group.upload), bytes(group.download));
    return result;
}

void DiagnosticsWindow::copyReport() {
    const auto value = tabs->currentIndex() == 0 ? siteReport : connectionReport();
    if (value.isEmpty()) return;
    QApplication::clipboard()->setText(value);
    report->setText(tr("Copied"));
    QTimer::singleShot(1800, this, [this] { report->setText(tr("Copy report")); });
}
