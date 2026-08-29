#include <QThread>
#include <QDateTime>
#include <core/server/gen/libcore.pb.h>
#include <include/api/RPC.h>
#include "include/ui/mainwindow_interface.h"
#include <include/stats/connections/connectionLister.hpp>
#include "include/stats/traffic/TrafficStatsManager.hpp"



namespace Stats
{
    // Guards out-of-band ForceUpdate() polls: a tiny byte delta over a tiny interval reads as a spike.
    static constexpr qint64 kSpeedSampleMinMs = 500;

    // Off-view polls still run: they keep the per-app traffic stats sampled.
    static constexpr unsigned long kActivePollMs = 1000;
    static constexpr unsigned long kRelaxedPollMs = 5000;

    ConnectionLister* connection_lister = new ConnectionLister();

    ConnectionLister::ConnectionLister()
    {
        state = std::make_shared<QSet<QString>>();
    }

    void ConnectionLister::ForceUpdate()
    {
        mu.lock();
        update();
        mu.unlock();
    }


    void ConnectionLister::Loop()
    {
        while (true)
        {
            if (stop) return;

            {
                // Woken early by SetInView() and stopLoop().
                QMutexLocker wlk(&waitMu_);
                waitCond_.wait(&waitMu_, inView_.load() ? kActivePollMs : kRelaxedPollMs);
            }

            if (stop) return;
            if (suspend || !Configs::dataManager->settingsRepo->enable_stats) continue;

            mu.lock();
            update();
            mu.unlock();
        }
    }

    void ConnectionLister::SetInView(bool inView)
    {
        const bool was = inView_.exchange(inView);
        if (inView && !was)
        {
            QMutexLocker wlk(&waitMu_);
            waitCond_.wakeAll();
        }
    }

    static ConnectionMetadata metaFromProto(const libcore::ConnectionMetaData& conn)
    {
        ConnectionMetadata c;
        c.id = QString::fromStdString(conn.id.value());
        c.createdAtMs = conn.created_at.value();
        c.dest = QString::fromStdString(conn.dest.value());
        c.upload = conn.upload.value();
        c.download = conn.download.value();
        c.domain = QString::fromStdString(conn.domain.value());
        c.network = QString::fromStdString(conn.network.value());
        c.outbound = QString::fromStdString(conn.outbound.value());
        c.process = QString::fromStdString(conn.process.value());
        c.processPath = QString::fromStdString(conn.process_path.value());
        c.protocol = QString::fromStdString(conn.protocol.value());
        c.closedAtMs = conn.closed_at.value();
        return c;
    }

    void ConnectionLister::update()
    {
        libcore::QueryConnectionsResp resp = API::defaultClient->QueryConnections();
        const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();

        QMap<QString, ConnectionMetadata> toUpdate;
        QMap<QString, ConnectionMetadata> toAdd;
        QSet<QString> newState;
        QList<ConnectionMetadata> sorted;
        QHash<QString, SpeedSample> newSamples;
        for (const auto& conn : resp.active)
        {
            auto c = metaFromProto(conn);

            SpeedSample s;
            if (const auto it = speedSamples_.constFind(c.id); it != speedSamples_.constEnd())
            {
                const qint64 dt = nowMs - it->sampledAtMs;
                if (dt >= kSpeedSampleMinMs)
                {
                    qint64 dUp = c.upload - it->upload;
                    qint64 dDown = c.download - it->download;
                    if (dUp < 0) dUp = 0; // counters only grow; guard against any reset
                    if (dDown < 0) dDown = 0;
                    s.upload = c.upload;
                    s.download = c.download;
                    s.sampledAtMs = nowMs;
                    s.upSpeed = dUp * 1000 / dt;
                    s.downSpeed = dDown * 1000 / dt;
                }
                else
                {
                    s = *it; // window too short: keep last rate and baseline
                }
            }
            else
            {
                s.upload = c.upload; // first sighting: seed baseline, no rate yet
                s.download = c.download;
                s.sampledAtMs = nowMs;
            }
            newSamples.insert(c.id, s);
            c.uploadSpeed = s.upSpeed;
            c.downloadSpeed = s.downSpeed;

            if (sort == Default)
            {
                if (state->contains(c.id))
                {
                    toUpdate[c.id] = c;
                } else
                {
                    toAdd[c.id] = c;
                }
            } else
            {
                sorted.append(c);
            }
            newState.insert(c.id);
        }
        speedSamples_ = newSamples; // drop ids for connections that have closed

        state->clear();
        for (const auto& id : newState) state->insert(id);

        // Credits the live set plus the recently-closed ring (deduped by id), so a connection that opened and closed between polls still counts.
        if (!Configs::dataManager->settingsRepo->disable_traffic_stats)
        {
            QHash<QString, QPair<qint64, qint64>> newLast;
            QSet<QString> currentClosed;

            auto credit = [&](const libcore::ConnectionMetaData& cm, qint64 curUp, qint64 curDown)
            {
                const QString id = QString::fromStdString(cm.id.value());
                qint64 baseUp = 0, baseDown = 0;
                if (const auto it = lastBytes_.constFind(id); it != lastBytes_.constEnd())
                {
                    baseUp = it->first;
                    baseDown = it->second;
                }
                qint64 dUp = curUp - baseUp;
                qint64 dDown = curDown - baseDown;
                if (dUp < 0) dUp = 0;
                if (dDown < 0) dDown = 0;
                if (dUp == 0 && dDown == 0) return;
                QString name = QString::fromStdString(cm.process.value());
                if (name.isEmpty()) name = "Unknown";
                trafficStatsManager->AddAppDelta(name, QString::fromStdString(cm.process_path.value()), dUp, dDown);
            };

            for (const auto& cm : resp.active)
            {
                const qint64 up = cm.upload.value();
                const qint64 down = cm.download.value();
                credit(cm, up, down);
                newLast.insert(QString::fromStdString(cm.id.value()), {up, down});
            }
            for (const auto& cm : resp.closed)
            {
                const QString id = QString::fromStdString(cm.id.value());
                currentClosed.insert(id);
                if (accountedClosed_.contains(id)) continue;
                credit(cm, cm.upload.value(), cm.download.value());
            }
            lastBytes_ = newLast;             // drop evicted / now-closed live ids
            accountedClosed_ = currentClosed; // everything in the ring is accounted
        }

        if (sort == Default)
        {
            runOnUiThread([=,this] {
                auto m = GetMainWindow();
                m->UpdateConnectionList(toUpdate, toAdd);
            });
        } else
        {
            if (sort == ByDownload)
            {
                std::sort(sorted.begin(), sorted.end(), [=,this](const ConnectionMetadata& a, const ConnectionMetadata& b)
                {
                    if (a.download == b.download) return asc ? a.id > b.id : a.id < b.id;
                    return asc ? a.download < b.download : a.download > b.download;
                });
            }
            if (sort == ByUpload)
            {
                std::sort(sorted.begin(), sorted.end(), [=,this](const ConnectionMetadata& a, const ConnectionMetadata& b)
                {
                   if (a.upload == b.upload) return asc ? a.id > b.id : a.id < b.id;
                   return asc ? a.upload < b.upload : a.upload > b.upload;
                });
            }
            if (sort == ByProcess)
            {
                std::sort(sorted.begin(), sorted.end(), [=,this](const ConnectionMetadata& a, const ConnectionMetadata& b)
                {
                    if (a.process == b.process) return asc ? a.id > b.id : a.id < b.id;
                    return asc ? a.process > b.process : a.process < b.process;
                });
            }
            if (sort == ByOutbound)
            {
                std::sort(sorted.begin(), sorted.end(), [=,this](const ConnectionMetadata& a, const ConnectionMetadata& b)
                    {
                        if (a.outbound == b.outbound) return asc ? a.id > b.id : a.id < b.id;
                        return asc ? a.outbound > b.outbound : a.outbound < b.outbound;
                    });
            }
            if (sort == ByProtocol)
            {
                std::sort(sorted.begin(), sorted.end(), [=,this](const ConnectionMetadata& a, const ConnectionMetadata& b)
                    {
                        if (a.protocol == b.protocol) return asc ? a.id > b.id : a.id < b.id;
                        return asc ? a.protocol > b.protocol : a.protocol < b.protocol;
                    });
            }
            if (sort == ByTraffic)
            {
                std::sort(sorted.begin(), sorted.end(), [=,this](const ConnectionMetadata& a, const ConnectionMetadata& b)
                    {
                        const long long ta = a.upload + a.download, tb = b.upload + b.download;
                        if (ta == tb) return asc ? a.id > b.id : a.id < b.id;
                        return asc ? ta < tb : ta > tb;
                    });
            }
            if (sort == ByDownloadSpeed)
            {
                std::sort(sorted.begin(), sorted.end(), [=,this](const ConnectionMetadata& a, const ConnectionMetadata& b)
                    {
                        if (a.downloadSpeed == b.downloadSpeed) return asc ? a.id > b.id : a.id < b.id;
                        return asc ? a.downloadSpeed < b.downloadSpeed : a.downloadSpeed > b.downloadSpeed;
                    });
            }
            if (sort == ByUploadSpeed)
            {
                std::sort(sorted.begin(), sorted.end(), [=,this](const ConnectionMetadata& a, const ConnectionMetadata& b)
                    {
                        if (a.uploadSpeed == b.uploadSpeed) return asc ? a.id > b.id : a.id < b.id;
                        return asc ? a.uploadSpeed < b.uploadSpeed : a.uploadSpeed > b.uploadSpeed;
                    });
            }
            if (sort == BySpeed)
            {
                std::sort(sorted.begin(), sorted.end(), [=,this](const ConnectionMetadata& a, const ConnectionMetadata& b)
                    {
                        const long long sa = a.uploadSpeed + a.downloadSpeed, sb = b.uploadSpeed + b.downloadSpeed;
                        if (sa == sb) return asc ? a.id > b.id : a.id < b.id;
                        return asc ? sa < sb : sa > sb;
                    });
            }
            runOnUiThread([=,this] {
                auto m = GetMainWindow();
                m->UpdateConnectionListWithRecreate(sorted);
            });
        }
    }

    void ConnectionLister::stopLoop()
    {
        stop = true;
        QMutexLocker wlk(&waitMu_);
        waitCond_.wakeAll();
    }

    void ConnectionLister::setSort(const ConnectionSort newSort)
    {
        if (sort == newSort) asc = !asc;
        else
        {
            sort = newSort;
            asc = false;
        }
    }

    void ConnectionLister::restoreSort(const ConnectionSort newSort, const bool ascending)
    {
        sort = newSort;
        asc = ascending;
    }

}
