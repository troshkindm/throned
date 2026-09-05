#include "include/ui/stats/diagnostics_window.h"
#include "include/ui/setting/ThemeManager.hpp"
#include "include/ui/widget/MaterialIcon.h"
#include "include/ui/widget/ThronedTitleBar.h"
#include "include/ui/widget/ThronedWindowChrome.h"
#include "include/database/entities/RouteProfile.h"
#include "include/ui/stats/TrafficChartWidget.h"

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDate>
#include <QDateTime>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QGridLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QHostAddress>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QSaveFile>
#include <QScreen>
#include <QScrollArea>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QStyle>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>
#include <algorithm>
#ifdef Q_OS_WIN
#include <windows.h>
#include <tlhelp32.h>
#elif defined(Q_OS_LINUX)
#include <QDir>
#include <QFile>
#endif

namespace {
QString text(const std::optional<std::string> &value) { return QString::fromStdString(value.value_or("")); }
QString processKey(const libcore::ConnectionMetaData &c) { return text(c.process_path).isEmpty() ? text(c.process) : text(c.process_path); }
// sing-box attributes a connection to the process that opened the socket, and an
// application that spawns helpers shows up as several unrelated programs. It has no
// parent matcher either, so the tree has to be resolved here to be shown at all.
QHash<quint32, QPair<quint32, QString>> processTree() {
    QHash<quint32, QPair<quint32, QString>> tree;
#ifdef Q_OS_WIN
    const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return tree;
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    if (Process32FirstW(snapshot, &entry)) {
        do {
            tree.insert(entry.th32ProcessID,
                        {entry.th32ParentProcessID, QString::fromWCharArray(entry.szExeFile)});
        } while (Process32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);
#elif defined(Q_OS_LINUX)
    for (const auto &name : QDir(QStringLiteral("/proc")).entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        bool numeric = false;
        const auto pid = name.toUInt(&numeric);
        if (!numeric) continue;
        QFile stat(QStringLiteral("/proc/") + name + QStringLiteral("/stat"));
        if (!stat.open(QIODevice::ReadOnly | QIODevice::Text)) continue;
        // "pid (comm) state ppid ...", and comm may itself contain spaces and brackets.
        const auto line = QString::fromUtf8(stat.readAll());
        const int close = line.lastIndexOf(QLatin1Char(')'));
        if (close < 0) continue;
        const auto fields = line.mid(close + 1).split(QLatin1Char(' '), Qt::SkipEmptyParts);
        if (fields.size() < 2) continue;
        const auto comm = line.mid(line.indexOf(QLatin1Char('(')) + 1, close - line.indexOf(QLatin1Char('(')) - 1);
        tree.insert(pid, {fields[1].toUInt(), comm});
    }
#endif
    return tree;
}

QString sourceHost(const libcore::ConnectionMetaData &c) {
    const auto endpoint = text(c.source);
    const int colon = endpoint.lastIndexOf(QLatin1Char(':'));
    return colon > 0 ? endpoint.left(colon) : endpoint;
}
// sing-box only looks up the owning process when the routing profile has a process
// rule, so a blank one is ordinary rather than a failure; the client address is then
// the only identity there is, and it is what tells two LAN devices apart.
QString processName(const libcore::ConnectionMetaData &c) {
    if (!text(c.process).isEmpty()) return text(c.process);
    const auto host = sourceHost(c);
    return host.isEmpty() ? DiagnosticsWindow::tr("No application reported")
                          : DiagnosticsWindow::tr("No application reported · %1").arg(host);
}
QString bytes(qint64 value) { return QLocale().formattedDataSize(value); }
QStringList toList(const std::vector<std::string> &values) {
    QStringList out;
    out.reserve(static_cast<int>(values.size()));
    for (const auto &value : values) out << QString::fromStdString(value);
    return out;
}
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
QPixmap dotPixmap(const QColor &color, int size = 10) {
    const qreal ratio = qApp != nullptr ? qApp->devicePixelRatio() : 1.0;
    QPixmap pixmap(QSize(size, size) * ratio);
    pixmap.setDevicePixelRatio(ratio);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    painter.setBrush(color);
    painter.drawEllipse(QRectF(1.5, 1.5, size - 3.0, size - 3.0));
    return pixmap;
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
QString normaliseURL(const QString &raw) {
    QString value = raw.trimmed();
    if (value.isEmpty()) return {};
    if (!value.contains(QStringLiteral("://"))) value.prepend(QStringLiteral("https://"));
    QUrl url(value, QUrl::StrictMode);
    if (!url.isValid() || url.host().isEmpty() || !url.userInfo().isEmpty() || url.hasFragment()
        || (url.scheme() != "https" && url.scheme() != "http") || url.port() == 0) return {};
    return QString::fromUtf8(url.toEncoded());
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

// A section in the side rail. It carries the worst state found inside that section,
// so a problem is visible before the section is opened.
class RailButton final : public QPushButton {
public:
    RailButton(const QString &caption, QWidget *parent = nullptr) : QPushButton(caption, parent) {
        setObjectName(QStringLiteral("diagnosticRail"));
        setCheckable(true);
        setCursor(Qt::PointingHandCursor);
        setAutoDefault(false);
        setIconSize(QSize(10, 10));
    }
    // A dot only ever means "there is a remark in here". No dot is the normal state:
    // a grey one on every entry is decoration, and decoration that looks like a status
    // light is worse than none.
    void setState(const QString &value) {
        state = value;
        setIcon(value.isEmpty() ? QIcon() : QIcon(dotPixmap(toneColor(value))));
    }
    [[nodiscard]] QString stateName() const { return state; }
private:
    QString state;
};

// One line of the overview: a state pip, what was checked, what came back, and the
// screen that fixes it. The action is a plain link out, never a silent repair.
class HealthRow final : public QWidget {
public:
    HealthRow(const QString &caption, QWidget *parent = nullptr) : QWidget(parent) {
        setObjectName(QStringLiteral("diagnosticHealthRow"));
        auto *row = new QHBoxLayout(this);
        row->setContentsMargins(14, 9, 12, 9);
        row->setSpacing(13);
        pip = new QLabel;
        pip->setFixedSize(12, 12);
        pip->setAlignment(Qt::AlignCenter);
        row->addWidget(pip);
        name = label(caption, "title");
        name->setWordWrap(false);
        name->setMinimumWidth(148);
        row->addWidget(name);
        value = label({}, "muted");
        row->addWidget(value, 1);
        action = button({}, "diagnosticHealthAction");
        action->hide();
        row->addWidget(action);
    }
    void set(const QString &tone, const QString &description) {
        state = tone;
        value->setText(description);
        pip->setPixmap(dotPixmap(tone.isEmpty() ? themeManager()->Colors().controlInactive : toneColor(tone)));
        setProperty("attention", tone == QLatin1String("error"));
        style()->unpolish(this);
        style()->polish(this);
    }
    void setAction(const QString &caption, const QString &target) {
        action->setText(caption);
        action->setVisible(!caption.isEmpty());
        actionTarget = target;
    }
    [[nodiscard]] QString stateName() const { return state; }
    [[nodiscard]] QString describe() const { return name->text() + QStringLiteral(": ") + value->text(); }
    QPushButton *action;
    QString actionTarget;
private:
    QLabel *pip;
    QLabel *name;
    QLabel *value;
    QString state;
};

// Throughput over the observation window. "Sent and nothing came back" and "was
// flowing and stopped" are different diagnoses that look identical as totals.
class Sparkline final : public QWidget {
public:
    // As tall as the two-line rows it sits in: an item widget drives the row height,
    // and a short one would clip the process line out of every row.
    explicit Sparkline(QWidget *parent = nullptr) : QWidget(parent) {
        setFixedSize(210, 26);
        setAttribute(Qt::WA_TransparentForMouseEvents);
    }
    void setSamples(const QList<qint64> &values, bool healthy) {
        samples = values;
        ok = healthy;
        update();
    }
protected:
    void paintEvent(QPaintEvent *) override {
        if (samples.size() < 2) return;
        const auto colors = themeManager()->Colors();
        qint64 peak = 1;
        for (const auto sample : samples) peak = qMax(peak, sample);
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        QPainterPath path;
        const qreal top = height() * 0.28;
        const qreal span = height() * 0.44;
        const qreal step = qreal(width() - 2) / qreal(samples.size() - 1);
        for (int i = 0; i < samples.size(); ++i) {
            const qreal x = 1 + step * i;
            const qreal y = top + span - span * qreal(samples[i]) / qreal(peak);
            if (i == 0) path.moveTo(x, y); else path.lineTo(x, y);
        }
        painter.setPen(QPen(ok ? colors.success : colors.warning, 1.4));
        painter.drawPath(path);
    }
private:
    QList<qint64> samples;
    bool ok = true;
};

DiagnosticsWindow::DiagnosticsWindow(QWidget *parent) : QDialog(parent, Qt::Window) {
    setObjectName(QStringLiteral("diagnosticsWindow"));
    setWindowTitle(tr("Diagnostics"));
    resize(940, 800);
    setMinimumSize(760, 600);
    if (auto *display = screen()) resize(size().boundedTo(display->availableGeometry().size() - QSize(32, 32)));

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(1, 1, 1, 1);
    root->setSpacing(0);
    root->addWidget(ThronedChrome::install(this, tr("Diagnostics")));

    auto *body = new QWidget;
    auto *bodyRow = new QHBoxLayout(body);
    bodyRow->setContentsMargins(0, 0, 0, 0);
    bodyRow->setSpacing(0);

    auto *railPanel = new QFrame;
    railPanel->setObjectName(QStringLiteral("diagnosticRailPanel"));
    railPanel->setFixedWidth(178);
    auto *railLayout = new QVBoxLayout(railPanel);
    railLayout->setContentsMargins(10, 13, 10, 10);
    railLayout->setSpacing(3);
    auto *railCaption = label(tr("SECTIONS"), "railCaption");
    railLayout->addWidget(railCaption);
    for (const auto &caption : {tr("Overview"), tr("Address"), tr("Applications"), tr("Statistics"), tr("Report")}) {
        auto *item = new RailButton(caption, railPanel);
        // Fixed, so the bolder checked entry does not push the ones below it out of step.
        item->setFixedHeight(38);
        railLayout->addWidget(item);
        rail.append(item);
    }
    railLayout->addStretch(1);
    overviewStamp = label({}, "subtle");
    overviewStamp->setObjectName(QStringLiteral("diagnosticStamp"));
    railLayout->addWidget(overviewStamp);
    bodyRow->addWidget(railPanel);

    pages = new QStackedWidget;
    pages->setObjectName(QStringLiteral("diagnosticPages"));
    bodyRow->addWidget(pages, 1);
    root->addWidget(body, 1);

    buildOverviewPage();
    buildAddressPage();
    buildApplicationsPage();
    buildStatisticsPage();
    buildReportPage();

    for (int i = 0; i < rail.size(); ++i) {
        connect(rail[i], &QPushButton::clicked, this, [this, i] {
            pages->setCurrentIndex(i);
            for (int j = 0; j < rail.size(); ++j) rail[j]->setChecked(j == i);
            if (i == 2) { poll->start(); requestConnections(); }
            else if (!recording) poll->stop();
            if (i == 3) emit usageRequested(usageRangeDays);
            if (i == 4) refreshReport();
            reportSave->setVisible(i == 4);
            reportCopy->setProperty("primary", i == 4);
            reportCopy->style()->unpolish(reportCopy);
            reportCopy->style()->polish(reportCopy);
        });
    }
    rail[0]->setChecked(true);

    // Saving belongs beside copying, not stranded at the bottom of one page; it only
    // appears on the section that produces a file.
    auto *footer = new QHBoxLayout;
    footer->setContentsMargins(20, 11, 20, 13);
    footer->setSpacing(9);
    reportSave = button(tr("Save to file"), "diagnosticSaveReport");
    reportSave->hide();
    footer->addWidget(reportSave);
    reportCopy = button(tr("Copy report"), "diagnosticCopyReport");
    footer->addWidget(reportCopy);
    footer->addStretch();
    auto *close = button(tr("Close"), "diagnosticClose");
    footer->addWidget(close);
    root->addLayout(footer);
    connect(close, &QPushButton::clicked, this, &QDialog::close);
    connect(reportCopy, &QPushButton::clicked, this, &DiagnosticsWindow::copyReport);
    connect(reportSave, &QPushButton::clicked, this, &DiagnosticsWindow::saveReport);

    poll = new QTimer(this);
    poll->setInterval(1000);
    connect(poll, &QTimer::timeout, this, &DiagnosticsWindow::requestConnections);

    themeManager()->RegisterStyle(this, QStringLiteral(R"(
QDialog#diagnosticsWindow, QWidget#diagnosticPage { background: #1B1E23; color: #F1F3F5; }
QDialog#diagnosticsWindow QLabel { background: transparent; color: #F1F3F5; }
QDialog#diagnosticsWindow QLabel[diagnosticRole="muted"] { color: #A4ABB4; }
QDialog#diagnosticsWindow QLabel[diagnosticRole="subtle"] { color: #747C86; }
QDialog#diagnosticsWindow QLabel[diagnosticRole="heading"] { font-size: 20px; font-weight: 600; }
QDialog#diagnosticsWindow QLabel[diagnosticRole="title"] { font-weight: 600; }
QDialog#diagnosticsWindow QLabel[diagnosticRole="railCaption"] { color: #747C86; padding: 2px 10px 7px; }
QDialog#diagnosticsWindow QLabel[diagnosticRole="stepTitle"] { color: #F1F3F5; }
QDialog#diagnosticsWindow QLabel[diagnosticRole="stepTime"] { color: #747C86; }
QDialog#diagnosticsWindow QLabel[diagnosticRole="factName"] { color: #747C86; }
QDialog#diagnosticsWindow QLabel[diagnosticRole="factValue"] { color: #F1F3F5; }
QDialog#diagnosticsWindow QLabel[diagnosticRole="finding"] {
    color: #E8A33D; background: #3A2227; border-radius: 5px; padding: 5px 8px;
}
QDialog#diagnosticsWindow QLabel[diagnosticRole="totalDown"] { color: #2F91FF; font-size: 22px; font-weight: 600; }
QDialog#diagnosticsWindow QLabel[diagnosticRole="totalUp"] { color: #2EBC75; font-size: 22px; font-weight: 600; }
QDialog#diagnosticsWindow QLabel[diagnosticRole="total"] { font-size: 22px; font-weight: 600; }
QDialog#diagnosticsWindow QLabel[diagnosticRole="totalMuted"] { color: #A4ABB4; font-size: 19px; font-weight: 600; }
QPushButton#diagnosticRange { border-radius: 0; padding: 7px 13px; color: #A4ABB4; }
QPushButton#diagnosticRange[segment="first"] { border-top-left-radius: 6px; border-bottom-left-radius: 6px; }
QPushButton#diagnosticRange[segment="last"] { border-top-right-radius: 6px; border-bottom-right-radius: 6px; }
QPushButton#diagnosticRange:checked { background: #193452; border-color: #237AE9; color: #F1F3F5; font-weight: 600; }
QPushButton#diagnosticUsageTab {
    background: transparent; border: 0; border-bottom: 2px solid transparent;
    border-radius: 0; padding: 8px 15px; color: #A4ABB4;
}
QPushButton#diagnosticUsageTab:hover { background: #222529; }
QPushButton#diagnosticUsageTab:checked { color: #F1F3F5; border-bottom-color: #237AE9; }
QPushButton#diagnosticUsageTab:disabled { color: #747C86; }
QWidget#diagnosticUsageRow { border-top: 1px solid #2F3136; }
QProgressBar#diagnosticUsageBar { background: #222529; border: 0; border-radius: 3px; }
QProgressBar#diagnosticUsageBar::chunk { background: #237AE9; border-radius: 3px; }
QPushButton#diagnosticAddRule::menu-indicator { image: none; }
QFrame#diagnosticRailPanel { background: #171B21; border: 0; border-right: 1px solid #2F3136; }
/* Scoped through the dialog so it outweighs the generic button rule below, which
   otherwise fills every rail entry and makes the whole rail look like a keypad. */
QDialog#diagnosticsWindow QPushButton#diagnosticRail {
    text-align: left; padding: 9px 10px; border: 1px solid transparent; border-radius: 6px;
    background: transparent; color: #A4ABB4;
}
QDialog#diagnosticsWindow QPushButton#diagnosticRail:hover { background: #222529; border-color: #2F3136; }
QDialog#diagnosticsWindow QPushButton#diagnosticRail:checked {
    background: #193452; border-color: #237AE9; color: #F1F3F5; font-weight: 600;
}
QLabel#diagnosticStamp { padding: 6px 10px; }
QStackedWidget#diagnosticPages { background: #1B1E23; }
QScrollArea#diagnosticDetailScroll { background: transparent; }
QFrame#diagnosticVerdict, QFrame#diagnosticConnectionDetail, QFrame#diagnosticCard {
    background: #171B21; border: 1px solid #2F3136; border-radius: 8px;
}
QFrame#diagnosticPath { background: #171B21; border: 1px solid #2F3136; border-radius: 8px; }
QWidget#diagnosticHealthRow { border-top: 1px solid #2F3136; }
QWidget#diagnosticHealthRow[attention="true"] { background: #3A2227; }
QDialog#diagnosticsWindow QPushButton, QToolButton#diagnosticRuleButton {
    background: #222529; border: 1px solid #2F3136; border-radius: 6px; padding: 8px 12px; color: #F1F3F5;
}
QDialog#diagnosticsWindow QPushButton:hover, QToolButton#diagnosticRuleButton:hover { background: #292D33; border-color: #4A4F57; }
QDialog#diagnosticsWindow QPushButton[primary="true"] { background: #193452; border-color: #237AE9; }
QDialog#diagnosticsWindow QPushButton:checked { background: #193452; border-color: #237AE9; }
QDialog#diagnosticsWindow QPushButton:disabled { color: #747C86; border-color: #2F3136; }
QPushButton#diagnosticHealthAction { padding: 5px 10px; color: #2F91FF; }
QLineEdit#diagnosticAddress, QLineEdit#diagnosticCompare, QComboBox#diagnosticApps {
    background: #222529; color: #F1F3F5; border: 1px solid #2F3136; border-radius: 6px;
    padding: 8px 10px; min-height: 20px;
}
QLineEdit#diagnosticAddress:focus, QLineEdit#diagnosticCompare:focus, QComboBox#diagnosticApps:focus { border-color: #237AE9; }
QComboBox#diagnosticApps { padding-right: 28px; }
QComboBox#diagnosticApps::drop-down {
    subcontrol-origin: padding; subcontrol-position: center right;
    width: 26px; border: none; background: transparent;
}
QComboBox#diagnosticApps::down-arrow { image: url("%CHEVRON_DOWN%"); width: 14px; height: 14px; }
QTreeWidget#diagnosticConnections, QTreeWidget#diagnosticMatrix {
    background: #171B21; color: #F1F3F5; border: 1px solid #2F3136; border-radius: 6px; outline: 0;
}
QTreeWidget#diagnosticConnections::item { padding: 9px 7px; border-bottom: 1px solid #2F3136; }
QTreeWidget#diagnosticMatrix::item { padding: 7px 7px; border-bottom: 1px solid #2F3136; }
QTreeWidget#diagnosticConnections::item:selected, QTreeWidget#diagnosticMatrix::item:selected { background: #143C48; color: #F1F3F5; }
QDialog#diagnosticsWindow QHeaderView::section { background: #222529; color: #A4ABB4; border: 0; padding: 8px; }
QPlainTextEdit#diagnosticReportPreview {
    background: #171B21; color: #A4ABB4; border: 1px solid #2F3136; border-radius: 6px; padding: 9px 11px;
}
QDialog#diagnosticsWindow QCheckBox { color: #DDE2E7; spacing: 8px; background: transparent; }
QDialog#diagnosticsWindow QCheckBox::indicator {
    width: 15px; height: 15px; border: 1px solid #4A4F57; border-radius: 4px; background: #22272E;
}
QDialog#diagnosticsWindow QCheckBox::indicator:checked { background: #237AE9; border-color: #4193F4; image: url(:/icon/material/check.svg); }
QDialog#diagnosticsWindow QProgressBar { background: #222529; border: 0; }
QDialog#diagnosticsWindow QProgressBar::chunk { background: #237AE9; }
    )"));
    connect(themeManager(), &ThemeManager::themeChanged, this, &DiagnosticsWindow::refreshTheme);
    refreshTheme();
    refreshOverview();
}

// ---------------------------------------------------------------- pages

void DiagnosticsWindow::buildOverviewPage() {
    auto *page = new QWidget;
    page->setObjectName(QStringLiteral("diagnosticPage"));
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(24, 18, 24, 16);
    layout->setSpacing(11);
    layout->addWidget(label(tr("Connection state"), "heading"));
    layout->addWidget(label(tr("Checked when the window opens. Nothing leaves the machine except two requests through the active outbound."), "muted"));

    auto *card = new QFrame;
    card->setObjectName(QStringLiteral("diagnosticCard"));
    auto *cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(0, 0, 0, 0);
    cardLayout->setSpacing(0);
    for (const auto &caption : {tr("Core"), tr("Profile"), tr("Traffic capture"), tr("Visible address"),
                                tr("DNS"), tr("UDP"), tr("System clock")}) {
        auto *row = new HealthRow(caption, card);
        cardLayout->addWidget(row);
        healthRows.append(row);
        connect(row->action, &QPushButton::clicked, this, [this, row] {
            if (!row->actionTarget.isEmpty()) emit navigateRequested(row->actionTarget);
        });
    }
    layout->addWidget(card);

    auto *actions = new QHBoxLayout;
    recheck = button(tr("Check again"), "diagnosticRecheck");
    recheck->setProperty("primary", true);
    actions->addWidget(recheck);
    actions->addStretch();
    layout->addLayout(actions);
    connect(recheck, &QPushButton::clicked, this, [this] {
        if (healthBusy) return;
        healthBusy = true;
        healthError.clear();
        refreshOverview();
        emit healthRequested();
    });
    layout->addWidget(label(tr("Rows with an action open the screen that fixes that cause. Nothing here changes settings on its own."), "subtle"));
    layout->addStretch(1);
    pages->addWidget(page);
}

void DiagnosticsWindow::buildAddressPage() {
    auto *scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto *page = new QWidget;
    page->setObjectName(QStringLiteral("diagnosticPage"));
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(24, 18, 24, 16);
    layout->setSpacing(11);
    scroll->setWidget(page);

    layout->addWidget(label(tr("Check an address"), "heading"));
    auto *form = new QHBoxLayout;
    address = new QLineEdit;
    address->setObjectName(QStringLiteral("diagnosticAddress"));
    address->setPlaceholderText(QStringLiteral("chatgpt.com"));
    address->setAccessibleName(tr("Website address"));
    address->setClearButtonEnabled(true);
    compareToggle = button(tr("Compare with…"), "diagnosticCompareToggle");
    compareToggle->setCheckable(true);
    check = button(tr("Check"), "diagnosticCheck");
    check->setProperty("primary", true);
    form->addWidget(address, 1);
    form->addWidget(compareToggle);
    form->addWidget(check);
    layout->addLayout(form);

    compare = new QLineEdit;
    compare->setObjectName(QStringLiteral("diagnosticCompare"));
    compare->setPlaceholderText(tr("An address that works, for comparison"));
    compare->setClearButtonEnabled(true);
    compare->hide();
    layout->addWidget(compare);
    connect(compareToggle, &QPushButton::toggled, compare, &QWidget::setVisible);

    auto *contextRow = new QHBoxLayout;
    siteContext = label(tr("The address is matched against your routing rules first, then requested through the outbound they choose."), "muted");
    siteContext->setObjectName(QStringLiteral("diagnosticContext"));
    contextRow->addWidget(siteContext, 1);
    resetRoute = button(tr("Any application"), "diagnosticDefault");
    resetRoute->hide();
    contextRow->addWidget(resetRoute);
    layout->addLayout(contextRow);

    auto *verdict = new QFrame;
    verdict->setObjectName(QStringLiteral("diagnosticVerdict"));
    auto *verdictRow = new QHBoxLayout(verdict);
    verdictRow->setContentsMargins(16, 15, 16, 15);
    verdictRow->setSpacing(13);
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
    auto *verdictButtons = new QHBoxLayout;
    verdictButtons->setSpacing(8);
    for (int i = 0; i < 3; ++i) {
        auto *action = button({}, "diagnosticVerdictAction");
        action->hide();
        verdictButtons->addWidget(action);
        verdictActions.append(action);
        connect(action, &QPushButton::clicked, this, [this, action] {
            const auto target = action->property("target").toString();
            if (target == QLatin1String("matrix")) startMatrix();
            else if (!target.isEmpty()) emit navigateRequested(target);
        });
    }
    verdictButtons->addStretch();
    verdictText->addLayout(verdictButtons);
    verdictRow->addLayout(verdictText, 1);
    layout->addWidget(verdict);

    progress = new QProgressBar;
    progress->setRange(0, 0);
    progress->setTextVisible(false);
    progress->setFixedHeight(3);
    progress->hide();
    layout->addWidget(progress);

    pathCaption = label(tr("Request path"), "muted");
    layout->addWidget(pathCaption);
    auto *path = new QFrame;
    path->setObjectName(QStringLiteral("diagnosticPath"));
    auto *pathLayout = new QVBoxLayout(path);
    pathLayout->setContentsMargins(14, 10, 14, 10);
    pathLayout->setSpacing(0);
    for (int i = 0; i < 5; ++i) {
        auto *step = new DiagnosticStep(i == 0, i == 4, path);
        pathLayout->addWidget(step);
        steps.append(step);
    }
    layout->addWidget(path);
    setStep(0, tr("Route · not checked"), tr("The routing rules decide the outbound before anything is sent."), {});
    setStep(1, tr("DNS · not checked"), tr("Resolved beside the probe, never in front of it, so the proxy still resolves the name itself."), {});
    setStep(2, tr("Connection · not checked"), tr("Establishing the connection through the chosen outbound."), {});
    setStep(3, tr("TLS · not checked"), tr("Certificate and hostname verification remain enabled."), {});
    setStep(4, tr("HTTP · not checked"), tr("One HEAD request, without following redirects or sending account credentials."), {});

    matrixCaption = label(tr("Through the other outbounds"), "muted");
    matrixCaption->hide();
    layout->addWidget(matrixCaption);
    matrix = new QTreeWidget;
    matrix->setObjectName(QStringLiteral("diagnosticMatrix"));
    matrix->setHeaderLabels({tr("Outbound"), tr("DNS"), tr("TLS"), tr("HTTP")});
    matrix->setRootIsDecorated(false);
    matrix->setUniformRowHeights(true);
    matrix->setSelectionMode(QAbstractItemView::NoSelection);
    matrix->setFocusPolicy(Qt::NoFocus);
    matrix->setMaximumHeight(190);
    matrix->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    for (int i = 1; i < 4; ++i) {
        matrix->header()->setSectionResizeMode(i, QHeaderView::Fixed);
        matrix->setColumnWidth(i, 96);
    }
    matrix->hide();
    layout->addWidget(matrix);

    layout->addWidget(label(tr("This test does not reproduce browser DNS, extensions, application login or UDP traffic."), "subtle"));
    layout->addStretch(1);

    connect(check, &QPushButton::clicked, this, &DiagnosticsWindow::startSite);
    connect(address, &QLineEdit::returnPressed, this, &DiagnosticsWindow::startSite);
    connect(compare, &QLineEdit::returnPressed, this, &DiagnosticsWindow::startSite);
    connect(resetRoute, &QPushButton::clicked, this, [this] {
        pinnedProcess.clear(); pinnedOutbound.clear(); resetRoute->hide();
        siteContext->setText(tr("The address is matched against your routing rules first, then requested through the outbound they choose."));
    });
    pages->addWidget(scroll);
}

void DiagnosticsWindow::buildApplicationsPage() {
    auto *page = new QWidget;
    page->setObjectName(QStringLiteral("diagnosticPage"));
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(24, 18, 24, 16);
    layout->setSpacing(11);

    layout->addWidget(label(tr("Application connections"), "heading"));
    layout->addWidget(label(tr("Observe traffic passing through Throned while reproducing the problem."), "muted"));
    auto *form = new QHBoxLayout;
    apps = new QComboBox;
    apps->setObjectName(QStringLiteral("diagnosticApps"));
    apps->setAccessibleName(tr("Application"));
    apps->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    apps->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    apps->addItem(tr("All applications"), QString());
    // Named by what it does, not by a word only this code knows the meaning of; the
    // tooltip spells out the three things that count as "not fine".
    onlyProblems = button(tr("Hide the healthy"), "diagnosticProblemsOnly");
    onlyProblems->setCheckable(true);
    onlyProblems->setToolTip(tr("Keeps only the destinations with a remark: no outbound reported, "
                                "nothing came back, or going direct while the rest is proxied."));
    observe = button(tr("Start observation"), "diagnosticObserve");
    observe->setProperty("primary", true);
    form->addWidget(apps, 1);
    form->addWidget(onlyProblems);
    form->addWidget(observe);
    layout->addLayout(form);

    captureStatus = label(tr("Current connections. Start observation to retain completed connections."), "muted");
    captureStatus->setObjectName(QStringLiteral("diagnosticCaptureStatus"));
    layout->addWidget(captureStatus);
    summary = label({}, "title");
    layout->addWidget(summary);

    // The list needs height, the detail needs almost none, so they sit side by side
    // instead of stacked: vertical space was the scarce axis, not horizontal.
    auto *columns = new QHBoxLayout;
    columns->setSpacing(14);

    connections = new QTreeWidget;
    connections->setObjectName(QStringLiteral("diagnosticConnections"));
    connections->setHeaderLabels({tr("Destination"), tr("Traffic")});
    connections->setRootIsDecorated(false);
    connections->setUniformRowHeights(true);
    connections->setSelectionMode(QAbstractItemView::SingleSelection);
    connections->setMinimumHeight(160);
    // On by default, and it would hand the whole surplus to the traffic column.
    connections->header()->setStretchLastSection(false);
    connections->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    connections->header()->setSectionResizeMode(1, QHeaderView::Fixed);
    connections->setColumnWidth(1, 112);
    columns->addWidget(connections, 1);

    // How much the card holds depends on the connection — a blocked UDP flow explains
    // itself in six lines, a healthy one in two — so the sidebar scrolls rather than
    // compressing its own children on top of each other.
    auto *detailScroll = new QScrollArea;
    detailScroll->setObjectName(QStringLiteral("diagnosticDetailScroll"));
    detailScroll->setWidgetResizable(true);
    detailScroll->setFrameShape(QFrame::NoFrame);
    detailScroll->setFixedWidth(DetailWidth);
    detailScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto *detailFrame = new QFrame;
    detailFrame->setObjectName(QStringLiteral("diagnosticConnectionDetail"));
    auto *detailLayout = new QVBoxLayout(detailFrame);
    detailLayout->setContentsMargins(15, 14, 15, 14);
    detailLayout->setSpacing(9);
    detailScroll->setWidget(detailFrame);

    connectionTitle = label(tr("Select a connection"), "title");
    connectionTitle->setObjectName(QStringLiteral("diagnosticConnectionTitle"));
    detailLayout->addWidget(connectionTitle);
    connectionSpark = new Sparkline;
    detailLayout->addWidget(connectionSpark);
    // Labelled facts rather than a paragraph: every line here is a name and a value,
    // and running them together as prose is what made the card unreadable.
    facts = new QGridLayout;
    facts->setContentsMargins(0, 2, 0, 0);
    facts->setHorizontalSpacing(10);
    facts->setVerticalSpacing(4);
    facts->setColumnStretch(1, 1);
    detailLayout->addLayout(facts);
    // The observations that need attention, kept out of the facts and tinted, so a
    // finding never reads as one more neutral property of the connection.
    findings = new QVBoxLayout;
    findings->setContentsMargins(0, 4, 0, 0);
    findings->setSpacing(4);
    detailLayout->addLayout(findings);
    connectionDetail = label(tr("The list includes only connections visible to the core."), "muted");
    detailLayout->addWidget(connectionDetail);
    ruleDetail = label({}, "muted");
    ruleDetail->setObjectName(QStringLiteral("diagnosticRuleDetail"));
    detailLayout->addWidget(ruleDetail);
    detailLayout->addStretch(1);

    // The actions sit outside the scroll area: a button that scrolls half out of view
    // reads as broken, and they have to look like one set.
    auto *sidebar = new QWidget;
    sidebar->setFixedWidth(DetailWidth);
    auto *sidebarLayout = new QVBoxLayout(sidebar);
    sidebarLayout->setContentsMargins(0, 0, 0, 0);
    sidebarLayout->setSpacing(8);
    sidebarLayout->addWidget(detailScroll, 1);
    diagnoseAddress = button(tr("Check this address"), "diagnosticConnectionCheck");
    diagnoseAddress->setEnabled(false);
    // A push button rather than a tool button: the tool button left-aligns its label
    // while the two beside it centre theirs, and the three have to read as one set.
    addRule = button(tr("Route this…"), "diagnosticAddRule");
    addRule->setEnabled(false);
    addRule->setMenu(new QMenu(addRule));
    dropConnections = button(tr("Drop connections"), "diagnosticDrop");
    dropConnections->setEnabled(false);
    for (auto *action : {static_cast<QWidget *>(diagnoseAddress), static_cast<QWidget *>(addRule),
                         static_cast<QWidget *>(dropConnections)}) {
        action->setFixedHeight(34);
        sidebarLayout->addWidget(action);
    }
    columns->addWidget(sidebar);
    layout->addLayout(columns, 1);
    layout->addWidget(label(tr("HTTPS contents are not recorded. Byte counters cover the connection's lifetime; no reply does not by itself prove a failure."), "subtle"));

    connect(observe, &QPushButton::clicked, this, &DiagnosticsWindow::toggleObservation);
    connect(onlyProblems, &QPushButton::toggled, this, [this](bool on) { problemsOnly = on; rebuildConnections(); });
    connect(apps, &QComboBox::currentIndexChanged, this, [this] { desiredProcess.clear(); rebuildConnections(); });
    connect(connections, &QTreeWidget::itemSelectionChanged, this, &DiagnosticsWindow::showConnection);
    connect(dropConnections, &QPushButton::clicked, this, [this] {
        const auto *group = currentGroup();
        if (group != nullptr && !group->ids.isEmpty()) emit closeConnectionsRequested(group->ids);
    });
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
        rail[1]->click();
        address->setFocus();
    });
    pages->addWidget(page);
}

void DiagnosticsWindow::buildStatisticsPage() {
    auto *page = new QWidget;
    page->setObjectName(QStringLiteral("diagnosticPage"));
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(24, 18, 24, 16);
    layout->setSpacing(11);

    auto *tools = new QHBoxLayout;
    tools->setSpacing(0);
    auto *rangeBox = new QFrame;
    rangeBox->setObjectName(QStringLiteral("diagnosticSegmented"));
    auto *rangeLayout = new QHBoxLayout(rangeBox);
    rangeLayout->setContentsMargins(0, 0, 0, 0);
    rangeLayout->setSpacing(0);
    int index = 0;
    for (const auto &option : {QPair<int, QString>{1, tr("Day")}, {7, tr("7 days")}, {30, tr("30 days")}, {0, tr("All")}}) {
        auto *choice = button(option.second, "diagnosticRange");
        choice->setCheckable(true);
        choice->setChecked(option.first == usageRangeDays);
        choice->setProperty("segment", index == 0 ? "first" : index == 3 ? "last" : "middle");
        const int days = option.first;
        connect(choice, &QPushButton::clicked, this, [this, days] {
            usageRangeDays = days;
            for (auto *other : ranges) other->setChecked(other->property("days").toInt() == days);
            emit usageRequested(days);
        });
        choice->setProperty("days", days);
        rangeLayout->addWidget(choice);
        ranges.append(choice);
        ++index;
    }
    tools->addWidget(rangeBox);
    tools->addStretch();
    usageExport = button(tr("Export CSV"), "diagnosticExport");
    tools->addWidget(usageExport);
    layout->addLayout(tools);
    connect(usageExport, &QPushButton::clicked, this, &DiagnosticsWindow::exportUsage);

    auto *totals = new QHBoxLayout;
    totals->setSpacing(26);
    auto addTotal = [&](QLabel *&value, const QString &caption, const char *role) {
        auto *box = new QVBoxLayout;
        box->setSpacing(0);
        value = label(QStringLiteral("—"), role);
        box->addWidget(value);
        box->addWidget(label(caption, "factName"));
        totals->addLayout(box);
    };
    addTotal(usageDown, tr("Downloaded"), "totalDown");
    addTotal(usageUp, tr("Uploaded"), "totalUp");
    addTotal(usageTotal, tr("Total"), "total");
    addTotal(usageAverage, tr("Per day on average"), "totalMuted");
    totals->addStretch();
    layout->addLayout(totals);

    auto *chartCard = new QFrame;
    chartCard->setObjectName(QStringLiteral("diagnosticCard"));
    auto *chartLayout = new QVBoxLayout(chartCard);
    chartLayout->setContentsMargins(14, 11, 14, 11);
    chartLayout->setSpacing(8);
    auto *chartHead = new QHBoxLayout;
    chartHead->addWidget(label(tr("Over time"), "title"));
    chartLayout->addLayout(chartHead);
    usageChart = new TrafficChartWidget;
    usageChart->setMinimumHeight(140);
    chartLayout->addWidget(usageChart);
    usageChartHost = chartCard;
    layout->addWidget(chartCard);

    auto *tabs = new QHBoxLayout;
    tabs->setSpacing(0);
    int tab = 0;
    for (const auto &caption : {tr("Programs"), tr("Servers"), tr("Sites")}) {
        auto *choice = button(caption, "diagnosticUsageTab");
        choice->setCheckable(true);
        choice->setChecked(tab == 0);
        // The site breakdown needs a table the stats database does not have, and it is
        // the one cut that must not be switched on silently. Shown, disabled, explained.
        if (tab == 2) {
            choice->setEnabled(false);
            choice->setToolTip(tr("Requires recording the domains you open, which is off and not built yet."));
        }
        const int which = tab;
        connect(choice, &QPushButton::clicked, this, [this, which] {
            usageTab = which;
            for (int i = 0; i < usageTabs.size(); ++i) usageTabs[i]->setChecked(i == which);
            refreshUsage();
        });
        tabs->addWidget(choice);
        usageTabs.append(choice);
        ++tab;
    }
    tabs->addStretch();
    layout->addLayout(tabs);

    auto *breakdown = new QFrame;
    breakdown->setObjectName(QStringLiteral("diagnosticCard"));
    auto *breakdownLayout = new QVBoxLayout(breakdown);
    breakdownLayout->setContentsMargins(0, 0, 0, 0);
    breakdownLayout->setSpacing(0);
    usageRows = breakdownLayout;
    layout->addWidget(breakdown, 1);

    usageFootnote = label({}, "subtle");
    layout->addWidget(usageFootnote);
    pages->addWidget(page);
}

void DiagnosticsWindow::buildReportPage() {
    auto *page = new QWidget;
    page->setObjectName(QStringLiteral("diagnosticPage"));
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(24, 18, 24, 16);
    layout->setSpacing(11);
    layout->addWidget(label(tr("Report for support"), "heading"));
    layout->addWidget(label(tr("One paste instead of two. Pick what goes in and see exactly what you are about to send."), "muted"));

    auto *split = new QHBoxLayout;
    split->setSpacing(16);
    auto *choices = new QVBoxLayout;
    choices->setSpacing(4);
    for (const auto &caption : {tr("Connection state"), tr("Address check"), tr("Application connections"),
                                tr("Settings, DNS and log")}) {
        auto *box = new QCheckBox(caption);
        box->setChecked(true);
        choices->addWidget(box);
        reportSections.append(box);
        connect(box, &QCheckBox::toggled, this, &DiagnosticsWindow::refreshReport);
    }
    auto *divider = new QFrame;
    divider->setFrameShape(QFrame::HLine);
    divider->setFixedHeight(1);
    choices->addSpacing(6);
    choices->addWidget(divider);
    choices->addSpacing(6);
    redactToggle = new QCheckBox(tr("Hide personal data"));
    redactToggle->setChecked(true);
    choices->addWidget(redactToggle);
    auto *redactHint = label(tr("Masks the profile name, the visible address and paths to programs."), "subtle");
    redactHint->setContentsMargins(23, 1, 0, 0); // under the checkbox text, not under its box
    choices->addWidget(redactHint);
    choices->addStretch(1);
    connect(redactToggle, &QCheckBox::toggled, this, &DiagnosticsWindow::refreshReport);
    auto *choicesHost = new QWidget;
    choicesHost->setLayout(choices);
    choicesHost->setFixedWidth(230);
    split->addWidget(choicesHost);

    reportPreview = new QPlainTextEdit;
    reportPreview->setObjectName(QStringLiteral("diagnosticReportPreview"));
    reportPreview->setReadOnly(true);
    reportPreview->setLineWrapMode(QPlainTextEdit::NoWrap);
    QFont mono(QStringLiteral("Consolas"));
    mono.setStyleHint(QFont::Monospace);
    mono.setPointSize(qMax(8, QApplication::font().pointSize() - 1));
    reportPreview->setFont(mono);
    split->addWidget(reportPreview, 1);
    layout->addLayout(split, 1);
    pages->addWidget(page);
}

// ---------------------------------------------------------------- theme

void DiagnosticsWindow::refreshTheme() {
    const auto c = themeManager()->Colors();
    check->setIcon(MaterialIcon::icon(MaterialIcon::Glyph::Search, c.text, 18));
    observe->setIcon(MaterialIcon::icon(recording ? MaterialIcon::Glyph::Block : MaterialIcon::Glyph::Process, c.text, 18));
    diagnoseAddress->setIcon(MaterialIcon::icon(MaterialIcon::Glyph::Public, c.text, 18));
    dropConnections->setIcon(MaterialIcon::icon(MaterialIcon::Glyph::Close, c.textMuted, 18));
    onlyProblems->setIcon(MaterialIcon::icon(MaterialIcon::Glyph::Filter, c.textMuted, 18));

    addRule->setIcon(MaterialIcon::icon(MaterialIcon::Glyph::Tune, c.textMuted, 18));
    reportCopy->setIcon(MaterialIcon::icon(MaterialIcon::Glyph::Copy, c.textMuted, 18));
    recheck->setIcon(MaterialIcon::icon(MaterialIcon::Glyph::Reload, c.text, 18));
    compareToggle->setIcon(MaterialIcon::icon(MaterialIcon::Glyph::SwapVertical, c.textMuted, 18));
    verdictIcon->setPixmap(MaterialIcon::pixmap(verdictTone == "ok" ? MaterialIcon::Glyph::Check
        : verdictTone.isEmpty() ? MaterialIcon::Glyph::Search : MaterialIcon::Glyph::Shield, toneColor(verdictTone), 26));
    for (auto *step : steps) step->update();
    refreshRail();
}

void DiagnosticsWindow::refreshRail() {
    QString worst;
    for (const auto *row : healthRows) {
        if (row->stateName() == QLatin1String("error")) { worst = QStringLiteral("error"); break; }
        if (row->stateName() == QLatin1String("warning")) worst = QStringLiteral("warning");
    }
    rail[0]->setState(worst);
    QString apps;
    for (const auto &group : groups) {
        if (group.suspicion >= 2) { apps = QStringLiteral("error"); break; }
        if (group.suspicion == 1) apps = QStringLiteral("warning");
    }
    rail[2]->setState(apps);
    // Address carries the last check's verdict, which is a result the user just read
    // rather than a standing condition; statistics and the report have no state at all.
    for (const int section : {1, 3, 4}) rail[section]->setState({});
}

// ---------------------------------------------------------------- overview

void DiagnosticsWindow::applyLocalState(const LocalState &state) {
    local = state;
    refreshOverview();
}

void DiagnosticsWindow::applyHealth(const libcore::HealthResponse &result, const QString &error) {
    healthBusy = false;
    health = result;
    healthError = error;
    refreshOverview();
}

void DiagnosticsWindow::refreshOverview() {
    if (healthRows.size() < 7) return;
    auto minutes = [](qint64 ms) { return ms / 60000; };

    healthRows[0]->set(local.coreRunning ? "ok" : "error",
        local.coreRunning
            ? (local.coreUptimeMs < 0 ? local.coreVersion
                 : tr("%1 · %2 min").arg(local.coreVersion).arg(minutes(local.coreUptimeMs)))
            : tr("Not started — nothing is being routed"));
    healthRows[0]->setAction(tr("Log"), QStringLiteral("log"));

    healthRows[1]->set(local.profileName.isEmpty() ? "error" : "ok",
        local.profileName.isEmpty() ? tr("No profile is selected")
            : local.profileLatencyMs < 0 ? tr("%1 · %2").arg(local.profileName, local.profileType)
            : tr("%1 · %2 · %3 ms").arg(local.profileName, local.profileType).arg(local.profileLatencyMs));
    healthRows[1]->setAction(tr("Change"), QStringLiteral("profile"));

    const bool capturing = local.tun || local.systemProxy;
    healthRows[2]->set(capturing ? "ok" : "warning",
        local.tun ? tr("TUN is active — every program goes through Throned")
            : local.systemProxy ? tr("System proxy is set — programs that honour it go through Throned")
            : tr("Neither TUN nor the system proxy is on — programs go past Throned"));
    healthRows[2]->setAction(capturing ? QString() : tr("Turn on"), QStringLiteral("interception"));

    const auto externalError = text(health.external_error);
    const auto externalIP = text(health.external_ip);
    if (!healthError.isEmpty())
        healthRows[3]->set("warning", healthError);
    else if (!externalError.isEmpty())
        healthRows[3]->set("error", tr("Could not be checked: %1").arg(externalError));
    else if (externalIP.isEmpty())
        healthRows[3]->set({}, healthBusy ? tr("Checking…") : tr("Not checked yet"));
    else
        healthRows[3]->set("ok", text(health.external_country).isEmpty()
            ? externalIP : tr("%1 · %2").arg(externalIP, text(health.external_country)));
    healthRows[3]->setAction({}, {});

    const auto dnsDomain = text(health.dns_domain);
    if (!health.dns_compared.value_or(false)) {
        const auto dnsError = text(health.dns_error);
        healthRows[4]->set(dnsError.isEmpty() ? QString() : QStringLiteral("warning"),
            dnsError.isEmpty() ? (healthBusy ? tr("Checking…") : tr("Not checked yet")) : dnsError);
        healthRows[4]->setAction({}, {});
    } else if (health.dns_agrees.value_or(false)) {
        healthRows[4]->set("ok", tr("The system and the core agree on %1").arg(dnsDomain));
        healthRows[4]->setAction({}, {});
    } else {
        healthRows[4]->set("error", tr("The system and the core answer differently for %1 — queries are leaving the tunnel")
            .arg(dnsDomain));
        healthRows[4]->setAction(tr("Fix"), QStringLiteral("dns"));
    }

    // The preview injects its own state; the running application gets it from the core.
    const bool udpChecked = health.udp_checked.value_or(false) || !local.udpState.isEmpty();
    const bool udpOk = health.udp_checked.value_or(false) ? health.udp_ok.value_or(false) : local.udpState == "ok";
    if (!udpChecked)
        healthRows[5]->set({}, healthBusy ? tr("Checking…") : tr("Not checked yet"));
    else if (udpOk)
        healthRows[5]->set("ok", health.udp_rtt_ms.value_or(-1) >= 0
            ? tr("Answers in %1 ms — calls, games and QUIC have a path").arg(health.udp_rtt_ms.value_or(0))
            : tr("Answers — calls, games and QUIC have a path"));
    else
        healthRows[5]->set("warning", local.udpDetail.isEmpty()
            ? tr("No answer — calls, games and QUIC will not work") : local.udpDetail);
    healthRows[5]->setAction(udpChecked && !udpOk ? tr("Rules") : QString(), QStringLiteral("rules"));

    if (!health.clock_known.value_or(false)) {
        healthRows[6]->set({}, healthBusy ? tr("Checking…") : tr("Not checked yet"));
    } else {
        const auto skew = health.clock_skew_ms.value_or(0);
        const auto seconds = qAbs(skew) / 1000.0;
        healthRows[6]->set(qAbs(skew) > 300000 ? "error" : qAbs(skew) > 60000 ? "warning" : "ok",
            qAbs(skew) > 300000
                ? tr("Off by %1 s — TLS will fail everywhere until the clock is corrected").arg(seconds, 0, 'f', 0)
                : tr("Off by %1 s — TLS is unaffected").arg(seconds, 0, 'f', 1));
    }
    healthRows[6]->setAction({}, {});

    overviewStamp->setText(healthBusy ? tr("checking…")
        : tr("checked %1").arg(QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss"))));
    recheck->setEnabled(!healthBusy);
    refreshRail();
}

// ---------------------------------------------------------------- address

void DiagnosticsWindow::showAddress(const QString &url) {
    address->setText(url);
    rail[1]->click();
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

void DiagnosticsWindow::setVerdict(const QString &tone, const QString &title, const QString &detail, const QStringList &actions) {
    verdictTone = tone;
    verdictTitle->setText(title);
    verdictDetail->setText(detail);
    for (int i = 0; i < verdictActions.size(); ++i) {
        auto *action = verdictActions[i];
        if (i >= actions.size()) { action->hide(); continue; }
        const auto target = actions[i];
        action->setProperty("target", target);
        action->setText(target == QLatin1String("rules") ? tr("Open routing rules")
            : target == QLatin1String("reachability") ? tr("Try the other servers")
            : target == QLatin1String("dpi") ? tr("Turn on DPI bypass")
            : target == QLatin1String("dns") ? tr("Open DNS settings")
            : target == QLatin1String("matrix") ? tr("Check the other outbounds")
            : target);
        action->setProperty("primary", i == 0);
        action->show();
    }
    refreshTheme();
}

void DiagnosticsWindow::startSite() {
    if (siteBusy) return;
    requestURL = normaliseURL(address->text());
    comparingURL = compareToggle->isChecked() ? normaliseURL(compare->text()) : QString();
    if (requestURL.isEmpty()) {
        setVerdict("warning", tr("Enter a valid HTTP or HTTPS address"),
                   tr("Do not include credentials or a URL fragment."), {});
        siteReport.clear();
        for (int i = 0; i < steps.size(); ++i) setStep(i, tr("Not checked"), {}, {});
        return;
    }
    comparisonSummary.clear();
    matrix->clear();
    matrix->hide();
    matrixCaption->hide();
    routedOutbound.clear(); routedRule.clear(); routedAction.clear();
    siteBusy = true;
    check->setEnabled(false); address->setEnabled(false); resetRoute->setEnabled(false);
    diagnoseAddress->setEnabled(false);
    progress->show();
    setVerdict({}, tr("Checking %1…").arg(QUrl(requestURL).host()),
               tr("Route, DNS, connection, TLS and HTTP · up to 15 seconds"), {});
    siteReport.clear();
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
                   blocked ? QStringList{QStringLiteral("rules")} : QStringList{});
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

void DiagnosticsWindow::applySiteResult(const libcore::DiagnoseSiteResponse &r, const QString &rpcError) {
    const QString error = rpcError.isEmpty() ? text(r.error) : rpcError;
    const QString stage = rpcError.isEmpty() ? text(r.error_stage) : QStringLiteral("core");
    const auto outbound = text(r.outbound_tag);
    const auto status = r.status.value_or(0);
    if (!outbound.isEmpty() && outbound != routedOutbound)
        setStep(0, tr("Route: %1").arg(outbound), routedRule, "ok");

    const auto dnsCore = toList(r.dns_core);
    const auto dnsSystem = toList(r.dns_system);
    if (!r.dns_compared.value_or(false)) {
        setStep(1, tr("DNS · not compared"), tr("The address is already an IP, or one of the resolvers did not answer."), {});
    } else if (r.dns_agrees.value_or(false)) {
        setStep(1, tr("DNS"), tr("The system and the core agree: %1").arg(dnsCore.join(QStringLiteral(", "))),
                "ok", r.dns_ms.value_or(-1));
    } else {
        setStep(1, tr("DNS · answers differ"),
                tr("The core resolved %1, the system resolved %2. Programs using the system resolver go elsewhere.")
                    .arg(dnsCore.join(QStringLiteral(", ")), dnsSystem.join(QStringLiteral(", "))),
                "warning", r.dns_ms.value_or(-1));
    }

    auto render = [&](int index, const QString &name, qint64 ms, const QString &details, const QString &key) {
        setStep(index, ms < 0 ? tr("%1 · not checked").arg(name) : name,
                stage == key ? error : details,
                ms < 0 ? QString() : stage == key ? QStringLiteral("error") : QStringLiteral("ok"), ms);
    };
    render(2, tr("Connection"), r.connect_ms.value_or(-1),
           tr("Established through the outbound. The proxy may resolve the name remotely."), "connect");

    const bool cut = r.tls_cut.value_or(false);
    QString tlsDetail = text(r.tls_version);
    if (!text(r.tls_alpn).isEmpty()) tlsDetail += QStringLiteral(" · ") + text(r.tls_alpn);
    if (!text(r.tls_issuer).isEmpty()) tlsDetail += QStringLiteral("\n") + tr("Issued by %1").arg(text(r.tls_issuer));
    if (r.tls_expires_unix.value_or(0) > 0)
        tlsDetail += QStringLiteral("\n") + tr("Valid until %1").arg(
            QDateTime::fromSecsSinceEpoch(r.tls_expires_unix.value_or(0)).toString(QStringLiteral("dd.MM.yyyy")));
    render(3, cut ? tr("TLS · connection cut") : tr("TLS"), r.tls_ms.value_or(-1), tlsDetail, "tls");
    if (QUrl(requestURL).scheme() == "http") setStep(3, tr("TLS · not used for HTTP"), tr("This URL uses unencrypted HTTP."), {});
    render(4, status > 0 ? tr("HTTP %1").arg(status) : tr("HTTP"), r.http_ms.value_or(-1),
           tr("HEAD request, no redirects, no login."), "http");

    if (stage == "tls" && cut)
        setVerdict("error", tr("The connection is cut right after the site name"),
                   tr("The connection was established and then dropped before the server answered — the signature of filtering on the name, not a refusal by the site. %1").arg(error),
                   {QStringLiteral("dpi"), QStringLiteral("matrix")});
    else if (stage == "connect")
        setVerdict("error", tr("The outbound %1 did not answer").arg(outbound),
                   tr("The request never left Throned, so this is the connection to the server, not the site. %1").arg(error),
                   {QStringLiteral("matrix"), QStringLiteral("reachability")});
    else if (stage == "tls")
        setVerdict("error", tr("TLS failed on the way to the site"),
                   tr("The server accepted the connection but the handshake did not verify. %1").arg(error), {});
    else if (stage == "http")
        setVerdict("error", tr("The outbound connected but the site stayed silent"),
                   tr("A connection through %1 was established and nothing answered. %2").arg(outbound, error),
                   {QStringLiteral("matrix")});
    else if (!error.isEmpty())
        setVerdict("error", tr("Could not run the check"), error, {});
    else if (status >= 400)
        setVerdict("warning", tr("The site answered — with an error"),
                   tr("HTTP %1 came from the site itself over a working connection through %2, so Throned delivered the request.").arg(status).arg(outbound),
                   {QStringLiteral("matrix")});
    else
        setVerdict("ok", status >= 300 ? tr("The site redirected") : tr("The site responded"),
                   tr("HTTP %1 through %2. Routing, connection and TLS all work for this address.").arg(status).arg(outbound),
                   {});
    if (error.isEmpty() && status >= 300) {
        steps[4]->tone = verdictTone;
        steps[4]->setExpanded(status >= 400);
    }
    endSite();
}

void DiagnosticsWindow::endSite() {
    siteBusy = false;
    check->setEnabled(true); address->setEnabled(true); resetRoute->setEnabled(true);
    progress->hide();
    showConnection();
    rescaleSteps();
    siteReport = tr("Throned · address check") + QStringLiteral("\n") + requestURL + QStringLiteral("\n")
        + QDateTime::currentDateTime().toString(Qt::ISODate) + QStringLiteral("\n\n");
    for (const auto *step : steps)
        siteReport += step->heading() + (step->ms < 0 ? QString() : QStringLiteral(" · ") + tr("%1 ms").arg(step->ms))
            + QStringLiteral("\n") + step->body() + QStringLiteral("\n\n");
    siteReport += verdictTitle->text() + QStringLiteral("\n") + verdictDetail->text();
    if (!comparisonSummary.isEmpty()) siteReport += QStringLiteral("\n\n") + comparisonSummary;
    refreshTheme();
    // A comparison is the same check again, so it runs only once the first has landed.
    if (!comparingURL.isEmpty()) {
        const auto next = comparingURL;
        comparisonSummary = tr("Compared with %1").arg(next) + QStringLiteral("\n")
            + tr("Result for %1: %2").arg(QUrl(requestURL).host(), verdictTitle->text());
        comparingURL.clear();
        address->setText(next);
        QTimer::singleShot(0, this, &DiagnosticsWindow::startSite);
    }
}

void DiagnosticsWindow::startMatrix() {
    if (siteBusy || requestURL.isEmpty()) return;
    QStringList others;
    for (const auto &tag : toList(health.outbounds))
        if (!tag.isEmpty() && tag != routedOutbound) others << tag;
    if (others.isEmpty()) {
        matrixCaption->setText(tr("No other outbounds are running to compare against."));
        matrixCaption->show();
        return;
    }
    matrix->clear();
    matrixPending = others.size();
    matrixCaption->setText(tr("Through the other outbounds"));
    matrixCaption->show();
    matrix->show();
    auto *current = new QTreeWidgetItem(matrix);
    current->setText(0, routedOutbound + QStringLiteral(" · ") + tr("current"));
    for (int i = 1; i < 4; ++i) {
        current->setText(i, steps[i]->tone == "ok" ? QStringLiteral("✓") : steps[i]->tone == "error" ? tr("fail") : QStringLiteral("—"));
        current->setForeground(i, toneColor(steps[i]->tone));
    }
    for (const auto &tag : others) {
        auto *item = new QTreeWidgetItem(matrix);
        item->setText(0, tag);
        item->setData(0, Qt::UserRole, tag);
        for (int i = 1; i < 4; ++i) item->setText(i, tr("…"));
    }
    emit matrixRequested(requestURL, others);
}

void DiagnosticsWindow::applyMatrixResult(const QString &outbound, const libcore::DiagnoseSiteResponse &r, const QString &error) {
    for (int i = 0; i < matrix->topLevelItemCount(); ++i) {
        auto *item = matrix->topLevelItem(i);
        if (item->data(0, Qt::UserRole).toString() != outbound) continue;
        const auto stage = error.isEmpty() ? text(r.error_stage) : QStringLiteral("core");
        const auto status = r.status.value_or(0);
        auto cell = [&](int column, const QString &value, const QString &tone) {
            item->setText(column, value);
            item->setForeground(column, toneColor(tone));
        };
        cell(1, !r.dns_compared.value_or(false) ? QStringLiteral("—") : r.dns_agrees.value_or(false) ? QStringLiteral("✓") : QStringLiteral("≠"),
             !r.dns_compared.value_or(false) ? QString() : r.dns_agrees.value_or(false) ? QStringLiteral("ok") : QStringLiteral("warning"));
        cell(2, stage == "tls" ? tr("cut") : r.tls_ms.value_or(-1) < 0 ? QStringLiteral("—") : tr("%1 ms").arg(r.tls_ms.value_or(0)),
             stage == "tls" ? QStringLiteral("error") : r.tls_ms.value_or(-1) < 0 ? QString() : QStringLiteral("ok"));
        cell(3, status > 0 ? QString::number(status) : stage == "connect" ? tr("no route") : QStringLiteral("—"),
             status > 0 && status < 400 ? QStringLiteral("ok") : status >= 400 ? QStringLiteral("warning") : QStringLiteral("error"));
        item->setToolTip(0, error.isEmpty() ? text(r.error) : error);
        break;
    }
    if (--matrixPending <= 0) matrixPending = 0;
}

// ---------------------------------------------------------------- applications

void DiagnosticsWindow::showApplication(const QString &key) {
    desiredProcess = key;
    if (!key.isEmpty()) {
        int index = apps->findData(key);
        if (index < 0) { apps->addItem(QFileInfo(key).fileName(), key); index = apps->count() - 1; }
        const QSignalBlocker block(apps);
        apps->setCurrentIndex(index);
    }
    rail[2]->click();
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
        captured.clear(); downHistory.clear(); lastDownload.clear();
        captureFinished = false; recording = true;
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
        if (key.isEmpty()) {
            if (apps->findData(NoProcessFilter) < 0)
                apps->addItem(tr("Without an application"), NoProcessFilter);
        } else if (apps->findData(key) < 0 && apps->count() < 300) {
            apps->addItem(processName(c), key);
            apps->setItemData(apps->count() - 1, key, Qt::ToolTipRole);
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
    int withoutProcess = 0;
    for (auto it = rows.cbegin(); it != rows.cend(); ++it) {
        const auto &c = it.value();
        if (processKey(c).isEmpty()) ++withoutProcess;
        if (filter == NoProcessFilter) {
            if (!processKey(c).isEmpty()) continue;
        } else if (!filter.isEmpty() && processKey(c) != filter) continue;
        const auto host = text(c.domain).isEmpty() ? text(c.dest) : text(c.domain);
        // Without a process the client address is the identity, or two LAN devices
        // talking to the same host would collapse into one indistinguishable row.
        const auto identity = processKey(c).isEmpty() ? sourceHost(c) : processKey(c);
        const auto key = identity + QLatin1Char('\x1f') + host + QLatin1Char('\x1f')
            + text(c.outbound) + QLatin1Char('\x1f') + text(c.network);
        auto &group = merged[key];
        if (group.count == 0) {
            group.key = key; group.host = host; group.domain = text(c.domain); group.dest = text(c.dest);
            group.process = processName(c); group.processPath = processKey(c);
            group.source = sourceHost(c);
            group.pid = c.process_id.value_or(0);
            group.outbound = text(c.outbound); group.network = text(c.network);
        }
        if (group.matchedRule.isEmpty()) group.matchedRule = text(c.matched_rule);
        if (c.closed_at.value_or(0) <= 0) group.ids << it.key();
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
        auto &history = downHistory[group.key];
        history.append(qMax<qint64>(0, group.download - lastDownload.value(group.key, group.download)));
        while (history.size() > HistorySamples) history.removeFirst();
        lastDownload[group.key] = group.download;
    }
    resolveAncestry();
    if (problemsOnly)
        groups.erase(std::remove_if(groups.begin(), groups.end(),
                                    [](const ConnectionGroup &g) { return g.suspicion == 0; }), groups.end());
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
    } else captureStatus->setText(latest.isEmpty() ? tr("No connections yet. Start Throned and open the application.") : tr("Current connections. Start observation to retain completed connections."));
    // The core only resolves the owning process when the routing profile asks it to,
    // so a list with no applications at all is a settings answer, not a bug report.
    if (withoutProcess > 0 && withoutProcess == total && total > 0)
        captureStatus->setText(captureStatus->text() + QLatin1Char('\n')
            + tr("No application is reported for any connection: the routing profile has no rules that match on a program, so the core never looks one up."));
    else if (withoutProcess > 0)
        captureStatus->setText(captureStatus->text() + QLatin1Char('\n')
            + tr("%n connection(s) report no application — traffic from another device, or a socket that closed before it could be attributed.", "", withoutProcess));
    showConnection();
    refreshRail();
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
        // Two columns rather than four: the outbound and the count read fine on the
        // second line, and the width they used to take is what the list was short of.
        QStringList line{group.process, group.network.toUpper(),
                         group.outbound.isEmpty() ? tr("Unknown outbound") : group.outbound};
        if (group.count > 1) line.insert(2, tr("×%1").arg(group.count));
        if (group.closed >= group.count) line << tr("Closed");
        // The remark that made the row rank where it did, said in the row itself: the
        // filter above is only meaningful if you can see what it would keep.
        if (group.suspicion == 3) line << tr("no outbound");
        else if (group.suspicion == 2) line << tr("no reply");
        else if (group.suspicion == 1) line << tr("bypasses the proxy");
        item->setIcon(0, dotPixmap(group.suspicion >= 2 ? themeManager()->Colors().danger
                                   : group.suspicion == 1 ? themeManager()->Colors().warning
                                   : themeManager()->Colors().success, 9));
        item->setText(0, group.host + QStringLiteral("\n") + line.join(QStringLiteral(" · ")));
        item->setText(1, QStringLiteral("↑ %1\n↓ %2").arg(bytes(group.upload), bytes(group.download)));
        item->setTextAlignment(1, Qt::AlignRight | Qt::AlignVCenter);
        item->setToolTip(0, group.dest + QStringLiteral("\n") + group.processPath + QStringLiteral("\n")
            + (group.matchedRule.isEmpty() ? tr("Default route (no rule reported)") : group.matchedRule));
    }
    for (auto *stale : existing) delete connections->takeTopLevelItem(connections->indexOfTopLevelItem(stale));
    if (!connections->currentItem() && connections->topLevelItemCount())
        connections->setCurrentItem(connections->topLevelItem(0));
    connections->verticalScrollBar()->setValue(scroll);
}

// A snapshot of the whole process table costs a millisecond, so it is taken at most
// twice a second and shared by every group rather than walked per connection.
void DiagnosticsWindow::resolveAncestry() {
    bool needed = false;
    for (const auto &group : groups) needed = needed || group.pid != 0;
    if (!needed) return;
    const auto now = QDateTime::currentMSecsSinceEpoch();
    if (now - ancestryTakenAt > 2000 || ancestry.isEmpty()) {
        ancestry = processTree();
        ancestryTakenAt = now;
    }
    for (auto &group : groups) {
        if (group.pid == 0 || !ancestry.contains(group.pid)) continue;
        const auto own = QFileInfo(group.processPath).fileName();
        quint32 pid = ancestry.value(group.pid).first;
        // Stop at the first ancestor that is a different program: helpers of helpers
        // still belong to the application the user recognises.
        for (int depth = 0; depth < 12 && pid > 4 && ancestry.contains(pid); ++depth) {
            const auto name = ancestry.value(pid).second;
            if (!name.isEmpty() && name.compare(own, Qt::CaseInsensitive) != 0) {
                group.parentProcess = name;
                break;
            }
            pid = ancestry.value(pid).first;
        }
    }
}

// The same rule entries the connections table offers, so a rule added from either
// screen lands in the active routing profile in exactly the same shape.
void DiagnosticsWindow::rebuildRuleMenu(const ConnectionGroup &group) {
    auto *menu = addRule->menu();
    menu->clear();
    struct Candidate { QString label; QString entry; };
    QList<Candidate> candidates;
    if (!group.domain.isEmpty()) {
        candidates.append({tr("This domain — %1").arg(group.domain), QStringLiteral("domain:") + group.domain});
        candidates.append({tr("Domain and subdomains — *.%1").arg(group.domain), QStringLiteral("suffix:") + group.domain});
    }
    if (!group.processPath.isEmpty()) {
        const auto name = QFileInfo(group.processPath).fileName();
        candidates.append({tr("This process — %1").arg(name), QStringLiteral("processName:") + name});
        candidates.append({tr("This executable — %1").arg(name), QStringLiteral("processPath:") + group.processPath});
    }
    const auto address = group.dest.contains(QLatin1Char(']'))
        ? group.dest.section(QLatin1Char(']'), 0, 0).mid(1)
        : group.dest.section(QLatin1Char(':'), 0, 0);
    if (!address.isEmpty() && !QHostAddress(address).isNull())
        candidates.append({tr("This address — %1").arg(address), QStringLiteral("ip:") + address});
    // The whole family: every executable currently seen under the same parent. sing-box
    // has no parent matcher, so this is one rule per program, and a helper that starts
    // later under a new name is not covered until it appears here too.
    QStringList family;
    if (!group.parentProcess.isEmpty()) {
        family << group.parentProcess;
        for (const auto &other : groups)
            if (other.parentProcess == group.parentProcess && !other.processPath.isEmpty())
                family << QFileInfo(other.processPath).fileName();
        family.removeDuplicates();
    }
    addRule->setEnabled(!candidates.isEmpty() || family.size() > 1);
    for (const auto &candidate : candidates) {
        auto *sub = menu->addMenu(candidate.label);
        const struct { int action; QString label; } targets[] = {
            {Configs::proxy, tr("Through proxy")},
            {Configs::bypass, tr("Directly")},
            {Configs::block, tr("Block")},
        };
        for (const auto &target : targets) {
            const auto entry = candidate.entry;
            const int action = target.action;
            connect(sub->addAction(target.label), &QAction::triggered, this,
                    [this, entry, action] { emit ruleRequested(entry, action); });
        }
    }
    if (family.size() < 2) return;
    menu->addSeparator();
    auto *whole = menu->addMenu(tr("The whole application — %1 and %n helper(s)", "", family.size() - 1)
                                    .arg(group.parentProcess));
    for (const auto &target : {QPair<int, QString>{Configs::proxy, tr("Through proxy")},
                               {Configs::bypass, tr("Directly")},
                               {Configs::block, tr("Block")}}) {
        const auto names = family;
        const int action = target.first;
        connect(whole->addAction(target.second), &QAction::triggered, this, [this, names, action] {
            for (const auto &name : names) emit ruleRequested(QStringLiteral("processName:") + name, action);
        });
    }
}

const DiagnosticsWindow::ConnectionGroup *DiagnosticsWindow::currentGroup() const {
    auto *item = connections->currentItem();
    if (item == nullptr) return nullptr;
    const auto key = item->data(0, Qt::UserRole).toString();
    for (const auto &group : groups)
        if (group.key == key) return &group;
    return nullptr;
}

void DiagnosticsWindow::setFacts(const QList<QPair<QString, QString>> &rows, const QStringList &notes) {
    auto clear = [](QLayout *layout) {
        while (auto *item = layout->takeAt(0)) {
            if (auto *widget = item->widget()) widget->deleteLater();
            delete item;
        }
    };
    clear(facts);
    clear(findings);
    for (int i = 0; i < rows.size(); ++i) {
        auto *name = label(rows[i].first, "factName");
        name->setWordWrap(false);
        auto *value = label(rows[i].second, "factValue");
        facts->addWidget(name, i, 0, Qt::AlignTop);
        facts->addWidget(value, i, 1);
    }
    for (const auto &note : notes) findings->addWidget(label(note, "finding"));
}

void DiagnosticsWindow::showConnection() {
    auto *item = connections->currentItem();
    diagnoseAddress->setEnabled(false);
    dropConnections->setEnabled(false);
    if (!item) {
        connectionTitle->setText(tr("Select a connection"));
        connectionDetail->setText(tr("The list includes only connections visible to the core."));
        connectionDetail->show();
        connectionSpark->hide();
        setFacts({}, {});
        ruleDetail->clear();
        return;
    }
    const auto *group = currentGroup();
    if (group == nullptr) return;
    const auto history = downHistory.value(group->key);
    connectionSpark->setSamples(history, group->suspicion == 0);
    connectionSpark->setVisible(history.size() >= 2); // an empty chart is just a hole
    connectionTitle->setText(group->dest.isEmpty() ? group->host : group->dest);
    connectionDetail->hide();

    QList<QPair<QString, QString>> rows{
        {tr("Program"), group->process},
        {tr("Protocol"), group->network.toUpper()},
        {tr("Outbound"), group->outbound.isEmpty() ? tr("Unknown outbound") : group->outbound},
    };
    if (!group->parentProcess.isEmpty()) rows.append({tr("Started by"), group->parentProcess});
    if (group->processPath.isEmpty() && !group->source.isEmpty()) rows.append({tr("Opened from"), group->source});
    rows.append({tr("Connections"), group->closed > 0
        ? tr("%1 · %2 closed").arg(group->count).arg(group->closed) : QString::number(group->count)});

    QStringList notes;
    if (group->outbound.isEmpty())
        notes << tr("The core did not report an outbound for this traffic.");
    if (group->download == 0 && group->upload > 0)
        notes << tr("Nothing came back. On its own this does not identify the cause.");
    if (group->suspicion == 1)
        notes << tr("Goes direct while the rest of the traffic uses the proxy.");
    if (group->processPath.isEmpty())
        notes << tr("No owning program, so process rules cannot match this traffic.");
    if (group->network.compare(QStringLiteral("tcp"), Qt::CaseInsensitive) != 0)
        notes << tr("An HTTP check does not test this UDP or other non-TCP flow.");
    else if (targetURL(group->domain, group->dest).isEmpty())
        notes << tr("No usable destination address was reported.");
    else
        diagnoseAddress->setEnabled(!siteBusy);
    setFacts(rows, notes);

    dropConnections->setEnabled(!group->ids.isEmpty());
    rebuildRuleMenu(*group);
    ruleDetail->setText(group->matchedRule.isEmpty()
        ? tr("No rule matched, so the default outbound is used.")
        : group->matchedRule + QStringLiteral("\n") + explainRuleText(group->matchedRule));
}

// ---------------------------------------------------------------- report

QString DiagnosticsWindow::healthReport() const {
    QStringList out;
    out << tr("Throned · connection state");
    for (const auto *row : healthRows) out << QStringLiteral("  ") + row->describe();
    return out.join(QLatin1Char('\n'));
}

QString DiagnosticsWindow::connectionReport() const {
    QString result = tr("Throned · application connections") + QStringLiteral("\n")
        + QDateTime::currentDateTime().toString(Qt::ISODate) + QStringLiteral("\n") + captureStatus->text() + QStringLiteral("\n");
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

QString DiagnosticsWindow::buildReport() const {
    QStringList parts;
    if (reportSections[0]->isChecked()) parts << healthReport();
    if (reportSections[1]->isChecked() && !siteReport.isEmpty()) parts << siteReport;
    if (reportSections[2]->isChecked() && !groups.isEmpty()) parts << connectionReport();
    if (reportSections[3]->isChecked() && !local.environmentReport.isEmpty()) parts << local.environmentReport;
    QString report = parts.join(QStringLiteral("\n\n"));
    if (!redactToggle->isChecked()) return report;
    // The environment section arrives already redacted; this covers what the window
    // itself collected, and says in the UI exactly which three things it masks.
    const QString mask = QStringLiteral("███");
    if (!local.profileName.isEmpty()) report.replace(local.profileName, mask);
    if (const auto ip = text(health.external_ip); !ip.isEmpty()) report.replace(ip, mask);
    for (const auto &group : groups) {
        if (group.processPath.isEmpty() || group.processPath == group.process) continue;
        report.replace(group.processPath, QFileInfo(group.processPath).fileName());
    }
    return report;
}

void DiagnosticsWindow::applyUsage(const Usage &value) {
    usage = value;
    refreshUsage();
}

void DiagnosticsWindow::refreshUsage() {
    while (auto *item = usageRows->takeAt(0)) {
        if (auto *widget = item->widget()) widget->deleteLater();
        delete item;
    }
    const auto &rows = usageTab == 1 ? usage.servers : usage.apps;
    qint64 up = 0, down = 0;
    for (const auto &row : rows) { up += row.up; down += row.down; }
    usageDown->setText(down > 0 ? bytes(down) : QStringLiteral("—"));
    usageUp->setText(up > 0 ? bytes(up) : QStringLiteral("—"));
    usageTotal->setText(up + down > 0 ? bytes(up + down) : QStringLiteral("—"));
    const auto days = qMax<qint64>(1, usageRangeDays > 0 ? usageRangeDays : qMax<qint64>(1, usage.daysStored));
    usageAverage->setText(up + down > 0 ? bytes((up + down) / days) : QStringLiteral("—"));

    QList<TrafficChartWidget::Bar> bars;
    for (const auto &point : usage.series)
        bars.append({point.bucketStart, point.down, point.up, point.label});
    // Enough labels to orient without them colliding at a 30-day range.
    usageChart->setData(bars, qMax(1, bars.size() / 8), usage.bucketSecs);
    usageChartHost->setVisible(!bars.isEmpty());

    if (!usage.available) {
        usageRows->addWidget(label(tr("The statistics database is unavailable."), "muted"));
    } else if (!usage.recording) {
        usageRows->addWidget(label(tr("Traffic counting is turned off, so there is nothing to show. Turn it on in the settings to start collecting."), "muted"));
    } else if (rows.isEmpty()) {
        usageRows->addWidget(label(tr("Nothing recorded for this period."), "muted"));
    }
    // A single scale for the bars: the top row is full width and the rest read against it.
    qint64 peak = 1;
    for (const auto &row : rows) peak = qMax(peak, row.up + row.down);
    static const QColor swatches[] = {QColor("#3B82F6"), QColor("#5C99FF"), QColor("#3ECF8E"),
                                      QColor("#D9A441"), QColor("#A78BFA"), QColor("#4B4F58")};
    for (int i = 0; i < rows.size(); ++i) {
        const auto &row = rows[i];
        auto *line = new QWidget;
        line->setObjectName(QStringLiteral("diagnosticUsageRow"));
        auto *grid = new QGridLayout(line);
        grid->setContentsMargins(14, 9, 14, 9);
        grid->setHorizontalSpacing(11);
        grid->setVerticalSpacing(1);
        auto *swatch = new QLabel;
        swatch->setFixedSize(10, 10);
        swatch->setPixmap(dotPixmap(swatches[qMin<int>(i, 5)], 10));
        grid->addWidget(swatch, 0, 0, 2, 1);
        auto *name = label(row.name, "factValue");
        name->setWordWrap(false);
        grid->addWidget(name, 0, 1);
        if (!row.detail.isEmpty()) {
            auto *detail = label(row.detail, "factName");
            detail->setWordWrap(false);
            grid->addWidget(detail, 1, 1);
        }
        auto *bar = new QProgressBar;
        bar->setObjectName(QStringLiteral("diagnosticUsageBar"));
        bar->setTextVisible(false);
        bar->setFixedHeight(6);
        bar->setRange(0, 1000);
        bar->setValue(static_cast<int>(1000 * (row.up + row.down) / peak));
        bar->setFixedWidth(150);
        grid->addWidget(bar, 0, 2, 2, 1, Qt::AlignVCenter);
        auto *value = label(bytes(row.up + row.down), "factValue");
        value->setAlignment(Qt::AlignRight | Qt::AlignBottom);
        grid->addWidget(value, 0, 3);
        auto *share = label(tr("%1%").arg(100.0 * (row.up + row.down) / qMax<qint64>(1, up + down), 0, 'f', 0), "factName");
        share->setAlignment(Qt::AlignRight | Qt::AlignTop);
        grid->addWidget(share, 1, 3);
        grid->setColumnStretch(1, 1);
        usageRows->addWidget(line);
    }
    usageRows->addStretch(1);
    usageFootnote->setText(usage.databaseBytes > 0
        ? tr("Stored on this computer only · %1 · %n day(s) of records", "", static_cast<int>(usage.daysStored))
              .arg(bytes(usage.databaseBytes))
        : tr("Stored on this computer only and never sent anywhere."));
    usageExport->setEnabled(!rows.isEmpty());
}

void DiagnosticsWindow::exportUsage() {
    const auto &rows = usageTab == 1 ? usage.servers : usage.apps;
    if (rows.isEmpty()) return;
    const auto path = QFileDialog::getSaveFileName(this, tr("Export statistics"),
        QStringLiteral("throned-traffic-") + QDate::currentDate().toString(Qt::ISODate) + QStringLiteral(".csv"),
        tr("CSV files (*.csv)"));
    if (path.isEmpty()) return;
    QString out = QStringLiteral("name,detail,download_bytes,upload_bytes\n");
    for (const auto &row : rows)
        out += QStringLiteral("\"%1\",\"%2\",%3,%4\n")
            .arg(QString(row.name).replace(QLatin1Char('"'), QStringLiteral("\"\"")),
                 QString(row.detail).replace(QLatin1Char('"'), QStringLiteral("\"\"")))
            .arg(row.down).arg(row.up);
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) return;
    file.write(out.toUtf8());
    file.commit();
}

void DiagnosticsWindow::refreshReport() {
    reportPreview->setPlainText(buildReport());
}

void DiagnosticsWindow::copyReport() {
    const auto value = buildReport();
    if (value.isEmpty()) return;
    QApplication::clipboard()->setText(value);
    reportCopy->setText(tr("Copied"));
    QTimer::singleShot(1800, this, [this] { reportCopy->setText(tr("Copy report")); });
}

void DiagnosticsWindow::saveReport() {
    const auto value = buildReport();
    if (value.isEmpty()) return;
    const auto suggested = QStringLiteral("throned-diagnostics-")
        + QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss")) + QStringLiteral(".txt");
    const auto path = QFileDialog::getSaveFileName(this, tr("Save report"), suggested, tr("Text files (*.txt)"));
    if (path.isEmpty()) return;
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) return;
    file.write(value.toUtf8());
    file.commit();
}
