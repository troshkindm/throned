#include "include/ui/utils/DataViewHtmlGenerator.h"

#include "include/global/CountryHelper.hpp"
#include "include/global/Configs.hpp"
#include "include/ui/setting/ThemeManager.hpp"

#include <array>

namespace {
struct ItemPriority {
    DataViewItem item;
    DataViewPriority priority;
};

// Within one level, panels render in this order.
constexpr std::array kItemPriorities = {
    ItemPriority{DataViewItem::Scanner, DataViewPriority::Medium},
    ItemPriority{DataViewItem::VpnEndpoint, DataViewPriority::Medium},
    ItemPriority{DataViewItem::AutoSelector, DataViewPriority::Medium},
    ItemPriority{DataViewItem::Download, DataViewPriority::Medium},
};

constexpr int kScannerPanelLines = 3;

constexpr std::array kPriorityOrder = {
    DataViewPriority::Critical,
    DataViewPriority::High,
    DataViewPriority::Medium,
    DataViewPriority::Low,
};

QString dataViewScannerProgressLine(const DataViewHtmlGenerator::ScannerPanelItem &item) {
    const quint64 percent = item.total > 0 ? qMin<quint64>(100, item.tested * 100 / item.total) : 0;
    return QObject::tr("%1 scan in progress %2/%3 (%4%)")
        .arg(item.name, QString::number(item.tested), QString::number(item.total), QString::number(percent));
}
} // namespace

void DataViewHtmlGenerator::setDownloadReport(const DownloadProgressReport &report, bool show) {
    QMutexLocker lk(&mu_);
    download_.visible = show;
    download_.report = report;
}

void DataViewHtmlGenerator::seedSpeedTest(int totalProfiles) {
    QMutexLocker lk(&mu_);
    testProgress.store(0);
    Configs::dataManager->settingsRepo->speed_test_mode == Configs::TestConfig::COUNTRY ? speedtest_.kind = SpeedtestPanelState::Kind::Country : speedtest_.kind = SpeedtestPanelState::Kind::Speed;
    speedtest_.totalProfiles = totalProfiles;
    speedtest_.visible = true;
}

void DataViewHtmlGenerator::setSpeedtestProgress(const QString &profileName, const libcore::SpeedTestResult &result) {
    QMutexLocker lk(&mu_);
    speedtest_.profileName = profileName;
    speedtest_.dlSpeed = QString::fromStdString(result.dl_speed.value());
    speedtest_.ulSpeed = QString::fromStdString(result.ul_speed.value());
    speedtest_.serverCountryFlag = CountryCodeToFlag(CountryNameToCode(QString::fromStdString(result.server_country.value())));
    speedtest_.serverCountry = QString::fromStdString(result.server_country.value());
    speedtest_.serverName = QString::fromStdString(result.server_name.value());
}

void DataViewHtmlGenerator::seedLatencyTest(LatencyTestPanelState::Kind kind, int totalProfiles) {
    QMutexLocker lk(&mu_);
    testProgress.store(0);
    latencyTest_.visible = true;
    latencyTest_.kind = kind;
    latencyTest_.totalProfiles = totalProfiles;
}

void DataViewHtmlGenerator::setAutoSelectorStatus(const QString &summary, const QString &detail) {
    QMutexLocker lk(&mu_);
    autoSelector_.summary = summary;
    autoSelector_.detail = detail;
    autoSelector_.visible = !summary.isEmpty();
}

void DataViewHtmlGenerator::setVpnEndpointStatus(const QString &summary, const QString &detail, bool problem) {
    QMutexLocker lk(&mu_);
    vpnEndpoint_.summary = summary;
    vpnEndpoint_.detail = detail;
    vpnEndpoint_.problem = problem;
    vpnEndpoint_.visible = !summary.isEmpty();
}

void DataViewHtmlGenerator::setScannerPanel(const QList<ScannerPanelItem> &items) {
    QMutexLocker lk(&mu_);
    scanner_ = items;
}

void DataViewHtmlGenerator::clearTestSections() {
    QMutexLocker lk(&mu_);
    latencyTest_ = {};
    speedtest_ = {};
    testProgress.store(0);
}

void DataViewHtmlGenerator::addTestProgress(int count) {
    testProgress.fetch_add(count);
}

QString DataViewHtmlGenerator::buildHtml() {
    QMutexLocker lk(&mu_);
    for (const auto priority: kPriorityOrder) {
        int occupied = 0;
        for (const auto &entry: kItemPriorities) {
            if (entry.priority == priority && itemVisible(entry.item)) ++occupied;
        }
        if (occupied == 0) continue;
        QString html;
        for (const auto &entry: kItemPriorities) {
            if (entry.priority == priority) html += itemHtml(entry.item, occupied > 1);
        }
        return html;
    }
    return {};
}

bool DataViewHtmlGenerator::itemVisible(DataViewItem item) const {
    switch (item) {
        case DataViewItem::Download:
            return download_.visible;
        case DataViewItem::AutoSelector:
            return autoSelector_.visible;
        case DataViewItem::VpnEndpoint:
            return vpnEndpoint_.visible;
        case DataViewItem::Scanner:
            return !scanner_.isEmpty();
    }
    return false;
}

QString DataViewHtmlGenerator::itemHtml(DataViewItem item, bool shared) {
    if (!itemVisible(item)) return {};
    switch (item) {
        case DataViewItem::Download:
            return downloadSectionHtml();
        case DataViewItem::AutoSelector:
            return autoSelectorSectionHtml();
        case DataViewItem::VpnEndpoint:
            return vpnEndpoint_.visible ? vpnEndpointSectionHtml() : QString();
        case DataViewItem::Scanner:
            return scannerSectionHtml(shared);
    }
    return {};
}

QString DataViewHtmlGenerator::scannerSectionHtml(bool compact) {
    const auto &tokens = themeManager()->Colors();
    // The view fits about three lines; when shared, the scanner takes one.
    if (compact) {
        QString line = dataViewScannerProgressLine(scanner_.first());
        if (scanner_.size() > 1) line += QObject::tr(" · +%n more", nullptr, static_cast<int>(scanner_.size() - 1));
        return QString("<p style='text-align:center;margin:0;color:%1;'>%2</p>").arg(tokens.accent.name(), line.toHtmlEscaped());
    }
    const qsizetype shown = scanner_.size() > kScannerPanelLines ? kScannerPanelLines - 1 : scanner_.size();
    QString res;
    for (qsizetype i = 0; i < shown; ++i) {
        const auto &item = scanner_.at(i);
        QString line = dataViewScannerProgressLine(item);
        if (item.found > 0) line += QObject::tr(" · %1 found").arg(item.found);
        res += QString("<p style='text-align:center;margin:0;color:%1;'>%2</p>")
                   .arg(tokens.accent.name(), line.toHtmlEscaped());
    }
    if (shown < scanner_.size()) {
        res += QString("<p style='text-align:center;margin:0;color:%1;'>%2</p>")
                   .arg(tokens.textMuted.name(), QObject::tr("+%1 more").arg(scanner_.size() - shown).toHtmlEscaped());
    }
    return res;
}

QString DataViewHtmlGenerator::vpnEndpointSectionHtml() {
    const auto colour = vpnEndpoint_.problem ? themeManager()->Colors().danger : themeManager()->Colors().accent;
    QString res = QString("<p style='text-align:center;margin:0;color:%1;'>%2</p>")
                      .arg(colour.name(), vpnEndpoint_.summary.toHtmlEscaped());
    if (!vpnEndpoint_.detail.isEmpty()) {
        res += QString("<p style='text-align:center;margin:0;opacity:0.75;'>%1</p>")
                   .arg(vpnEndpoint_.detail.toHtmlEscaped());
    }
    return res;
}

QString DataViewHtmlGenerator::autoSelectorSectionHtml() {
    QString res = QString("<p style='text-align:center;margin:0;'>%1</p>").arg(autoSelector_.summary.toHtmlEscaped());
    if (!autoSelector_.detail.isEmpty()) {
        res += QString("<p style='text-align:center;margin:0;opacity:0.75;'>%1</p>")
                   .arg(autoSelector_.detail.toHtmlEscaped());
    }
    return res;
}

QString DataViewHtmlGenerator::getProgressBar(long long current, long long total) {
    qint64 count = 0;
    if (total > 0) {
        count = 10 * current / total;
    }
    QString progressText;
    for (int i = 0; i < 10; i++) {
        if (count--; count >= 0) {
            progressText += "#";
        } else {
            progressText += "-";
        }
    }
    return progressText;
}

QString DataViewHtmlGenerator::downloadSectionHtml() {
    auto progressText = getProgressBar(download_.report.downloadedSize, download_.report.totalSize);
    const QString stat =
        ReadableSize(download_.report.downloadedSize) + "/" + ReadableSize(download_.report.totalSize);
    return QString("<p style='text-align:center;margin:0;'>Downloading %1: %2 %3</p>")
        .arg(download_.report.fileName, stat, progressText);
}

DataViewHtmlGenerator::TestPanelState DataViewHtmlGenerator::testPanelState() const {
    QMutexLocker lock(&mu_);
    TestPanelState state;
    state.visible = speedtest_.visible || latencyTest_.visible;
    if (speedtest_.visible) {
        state.kind = speedtest_.kind == SpeedtestPanelState::Kind::Country ? TestPanelState::Kind::Country : TestPanelState::Kind::Speed;
        state.total = qMax(0, speedtest_.totalProfiles);
        state.profileName = speedtest_.profileName;
        state.dlSpeed = speedtest_.dlSpeed;
        state.ulSpeed = speedtest_.ulSpeed;
        state.server = speedtest_.serverCountry;
        if (!speedtest_.serverName.isEmpty()) {
            if (!state.server.isEmpty()) state.server += QStringLiteral(" · ");
            state.server += speedtest_.serverName;
        }
    } else {
        state.kind = latencyTest_.kind == LatencyTestPanelState::Kind::Url   ? TestPanelState::Kind::Url
                     : latencyTest_.kind == LatencyTestPanelState::Kind::Udp ? TestPanelState::Kind::Udp
                                                                             : TestPanelState::Kind::Ip;
        state.total = qMax(0, latencyTest_.totalProfiles);
    }
    state.completed = cappedProgress(state.total);
    return state;
}
