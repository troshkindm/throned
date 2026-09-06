#pragma once

#include <QColor>
#include <QDialog>
#include <QList>
#include <QMap>
#include <QString>
#include <QStringList>

#include "ui_dialog_endpoint_details.h"

namespace libcore {
struct VPNEndpointStatus;
}

QT_BEGIN_NAMESPACE
namespace Ui {
class DialogEndpointDetails;
}
QT_END_NAMESPACE

namespace Stats {
inline const QColor kStatsAccentColor(0x4F, 0x8A, 0xF7);
inline const QColor kStatsHealthyColor(0x34, 0xC9, 0x8A);
inline const QColor kStatsProblemColor(0xC6, 0x28, 0x28);

// Qt-side copy of libcore::VPNEndpointStatus, built off the UI thread.
struct VpnEndpointView {
    QString tag;
    QString displayName;
    QString state;
    QString error;
    QString server;
    QString network;
    QString cipher;
    QStringList ipv4;
    QStringList ipv6;
    QStringList dns;
    QStringList routes;
    QStringList excludedRoutes;
    QStringList domains;
    int mtu = 0;
    qint64 connectedSince = 0;
    bool connected = false;
};

VpnEndpointView MakeVpnEndpointView(const libcore::VPNEndpointStatus &status);

QString VpnStateText(const QString &state);

QColor VpnStateColor(const QString &state);

QString HumanizeDuration(qint64 seconds);

// Tag -> profile id from the last build; a live status only carries its config tag.
void SetVpnEndpointProfiles(const QMap<QString, int> &tagToProfileID);

QString VpnEndpointDisplayName(const QString &tag);

int VpnEndpointProfileID(const QString &tag);
} // namespace Stats

class DialogEndpointDetails : public QDialog {
    Q_OBJECT

public:
    explicit DialogEndpointDetails(const Stats::VpnEndpointView &view, QWidget *parent = nullptr);
    ~DialogEndpointDetails() override;

    [[nodiscard]] QString tag() const { return tag_; }

    void applyStatus(const Stats::VpnEndpointView &view);

    void markGone();

protected:
    void resizeEvent(QResizeEvent *event) override;

private:
    struct DetailRow {
        enum Kind { Field,
                    Section,
                    Value };

        Kind kind = Field;
        QString key;
        QString label;
        QString value;
        QColor color;
    };

    void setRows(const QList<DetailRow> &rows);
    void fitRowHeights();
    void queueRowFit();
    void copySelection() const;

    Ui::DialogEndpointDetails *ui;
    QString tag_;
    QList<DetailRow> rows_;
    bool rowFitQueued_ = false;
};
