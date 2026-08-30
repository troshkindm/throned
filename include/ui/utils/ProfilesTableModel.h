#pragma once

#include <QAbstractTableModel>
#include <QList>
#include <QHash>
#include <QColor>
#include <memory>

namespace Configs {
    class Profile;
}

class ProfilesTableModel : public QAbstractTableModel {
    Q_OBJECT
public:
    enum { ProfileIdRole = Qt::UserRole, RowVisualRole };

    enum Column {
        ColType = 0,
        ColAddress,
        ColName,
        ColTestResult,
        ColTraffic,
        ColUDP,
        ColumnCount,
    };

    // Comfortable rows fold type, address, country and exit IP into one Server
    // column and split what Test Result used to pack into its own columns.
    enum ComfortColumn {
        ColcServer = 0,
        ColcPing,
        ColcSpeed,
        ColcTraffic,
        ComfortColumnCount,
    };

    enum class RowStyle { Compact, Comfortable };

    // The arrow is part of the header label: a QSS header with transparent
    // sections swallows the style's own sort indicator.
    void setSortIndicator(int column, bool descending);

    // Everything a comfortable row paints, resolved once per row.
    struct RowVisual {
        QString name;
        QString chip;      // protocol, drawn as an outlined chip
        QString address;
        QString country;   // two-letter code, drawn as a badge rather than a flag glyph
        QString exitIp;
        QString latency;
        QString udp;
        QString speedDown;
        QString speedUp;
        QString trafficDown;
        QString trafficUp;
        int latencyMs = 0; // kLatencyConnectOnly / negative / 0 = never tested
        bool udpDegraded = false;
        bool running = false;
    };

    void setRowStyle(RowStyle style);
    RowStyle rowStyle() const { return m_rowStyle; }

    // Off until a UDP test has actually produced something to show.
    void setUdpColumnVisible(bool visible);

    // Filterable fields, held in memory so filtering never pages profiles in one
    // at a time through the LRU cache below.
    struct FilterKey {
        QString type;
        QString address;
        QString name;
        QString country;
        int port = 0;
    };

    explicit ProfilesTableModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;
    Qt::DropActions supportedDropActions() const override;
    QStringList mimeTypes() const override;
    QMimeData *mimeData(const QModelIndexList &indexes) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;

    void refreshTable(const QList<int> &ids = {}, bool mayNeedReset = false);

    void refreshProfileId(int profileId);

    void emplaceProfiles(int row1, int row2);

    int indexOfProfile(int id);

    // A filter makes the source row and the display row disagree, hence both arguments.
    QString rowLabel(int sourceRow, int displayRow) const;

    // Null if the profile could not be loaded; valid until the next model change.
    const FilterKey *filterKeyAt(int row) const;

private:
    RowVisual buildRowVisual(const std::shared_ptr<Configs::Profile> &profile, bool isRunning) const;
    void ensureCached(int profileId) const;
    void evictOne() const;
    void setProfileIds(const QList<int> &ids);
    void ensureFilterIndex() const;

    QList<int> m_profileIds;
    mutable QHash<int, int> id2row;
    mutable QHash<int, std::shared_ptr<Configs::Profile>> m_cache;
    mutable QList<int> m_lruOrder;
    int m_cacheSize = 100;

    mutable QHash<int, FilterKey> m_filterKeys;
    bool m_udpColumnVisible = false;
    RowStyle m_rowStyle = RowStyle::Compact;
    int m_sortColumn = -1;
    bool m_sortDescending = false;
    mutable bool m_filterIndexBuilt = false;
};

Q_DECLARE_METATYPE(ProfilesTableModel::RowVisual)
