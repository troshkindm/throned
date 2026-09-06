#pragma once

#include <QDialog>
#include <QList>
#include <QPointer>
#include <QStringList>

#include <atomic>

#include "ui_dialog_runtime_stats.h"

#include "include/sys/ProcessMetrics.hpp"
#include "include/ui/stats/dialog_endpoint_details.h"

QT_BEGIN_NAMESPACE
namespace Ui {
class DialogRuntimeStats;
}
QT_END_NAMESPACE

class QTimer;

class DialogRuntimeStats : public QDialog {
    Q_OBJECT

public:
    explicit DialogRuntimeStats(QWidget* parent = nullptr);
    ~DialogRuntimeStats() override;

private:
    void refreshLive();
    void probeEgress();

    void applyEndpoints(const QList<Stats::VpnEndpointView>& views);
    void fitEndpointTable();
    void openEndpointDetails(const QString& tag);
    [[nodiscard]] QString selectedEndpointTag() const;

    Ui::DialogRuntimeStats* ui;
    QTimer* timer_ = nullptr;
    Sys::ProcessMetrics metrics_;
    std::atomic<bool> probing_{false};
    std::atomic<bool> connBusy_{false};
    std::atomic<bool> vpnBusy_{false};

    QString lastProbedConfig_;
    qint64 lastProbeSecs_ = 0;
    bool egressSnapshotDone_ = false;

    QPointer<DialogEndpointDetails> details_;
    QList<Stats::VpnEndpointView> endpointViews_;
    QStringList endpointTags_;
    bool endpointsGrown_ = false;
};
