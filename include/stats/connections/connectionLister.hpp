#pragma once
#include <QMutex>
#include <QWaitCondition>
#include <QString>
#include <QHash>
#include <QSet>
#include <QPair>
#include <atomic>

namespace Stats
{
    constexpr int IDKEY = 242315;

    constexpr int DESTKEY = 242316;
    constexpr int DOMAINKEY = 242317;
    constexpr int PROCESSKEY = 242318;
    constexpr int PROCESSPATHKEY = 242319;
    constexpr int OUTBOUNDKEY = 242320;

    enum ConnectionSort
    {
        Default,
        ByDownload,
        ByUpload,
        ByProcess,
        ByTraffic, // total traffic = upload + download
        ByOutbound,
        ByProtocol,
        ByDownloadSpeed,
        ByUploadSpeed,
        BySpeed // total speed = uploadSpeed + downloadSpeed
    };

    class ConnectionMetadata
    {
        public:
        QString id;
        long long createdAtMs;
        long long upload;
        long long download;
        QString outbound;
        QString network;
        QString dest;
        QString protocol;
        QString domain;
        QString process;     // basename, e.g. chrome.exe
        QString processPath;
        long long closedAtMs = 0; // 0 while live
        long long uploadSpeed = 0;   // bytes/sec
        long long downloadSpeed = 0;
    };

    class ConnectionLister
    {
    public:
        ConnectionLister();

        bool suspend = true;

        void Loop();

        void ForceUpdate();

        // Selects the 1 Hz vs relaxed poll cadence; switching to visible wakes the loop at once.
        void SetInView(bool inView);

        void stopLoop();

        void setSort(ConnectionSort newSort);

        // Restores a persisted pair as-is; setSort() would read the repeat as a direction flip.
        void restoreSort(ConnectionSort newSort, bool ascending);

        ConnectionSort getSort() const { return sort; }

        bool isSortAscending() const { return asc; }

    private:
        void update();

        // Rebuilt from the active set on every poll, so it self-prunes.
        struct SpeedSample
        {
            qint64 upload = 0;
            qint64 download = 0;
            qint64 sampledAtMs = 0;
            qint64 upSpeed = 0;
            qint64 downSpeed = 0;
        };
        QHash<QString, SpeedSample> speedSamples_;

        QMutex mu;

        // Interruptible poll sleep: SetInView(true) and stopLoop() wake it early.
        QMutex waitMu_;
        QWaitCondition waitCond_;
        std::atomic<bool> inView_{false};

        bool stop = false;

        std::shared_ptr<QSet<QString>> state;

        ConnectionSort sort = Default;

        bool asc = false;

        // The core's closed ring is non-draining, so closed ids must be deduped; both are rebuilt each poll.
        QHash<QString, QPair<qint64, qint64>> lastBytes_;
        QSet<QString> accountedClosed_;
    };

    extern ConnectionLister* connection_lister;
}
