#pragma once

#include <QHash>
#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>
#include <memory>

namespace Scanner {
class ScanWorker;

enum class StartMode {
    // Falls back to the initial pass when there is nothing to continue.
    Resume,
    FromInitial,
    RescanResults,
};

struct StartResult {
    QString error;
    bool targetsChanged = false;

    [[nodiscard]] bool ok() const { return error.isEmpty() && !targetsChanged; }
};

struct ScanLiveState {
    bool running = false;
    bool stopping = false;
    quint64 tested = 0;
    quint64 total = 0;
    int found = 0;
    int removed = 0;
    QString stage;
    QStringList log;
    QString error;
};

// UI thread only; owns every run so scans outlive their windows.
class ScanManager : public QObject {
    Q_OBJECT

public:
    static constexpr int kLogLines = 3;

    static ScanManager *instance();

    StartResult Start(int scanId, StartMode mode);

    // Asynchronous: the run persists its position and emits statusChanged when it has stopped.
    void Pause(int scanId);

    // persist=false (restore): no DB write from any run after this call.
    void StopAll(bool persist);

    [[nodiscard]] bool IsRunning(int scanId) const;

    [[nodiscard]] QList<int> RunningScans() const;

    [[nodiscard]] ScanLiveState LiveState(int scanId) const;

    // A visible window showing the scan keeps it out of the data view.
    void SetWatched(int scanId, bool watched);

    [[nodiscard]] bool IsWatched(int scanId) const;

    // Refuses while running; deleteResultList=false detaches the result list into a user list.
    QString DeleteScan(int scanId, bool deleteResultList);

    void RenameScan(int scanId, const QString &name);

    int EnsureResultList(int scanId);

    [[nodiscard]] static QString ResultListName(const QString &scanName);

    void NotifyScansChanged();

signals:
    void progressChanged(int scanId);
    void statusChanged(int scanId);
    void resultsChanged(int scanId);
    void scansChanged();

private:
    explicit ScanManager(QObject *parent);

    void onWorkerFinished(int scanId);

    QHash<int, std::shared_ptr<ScanWorker>> workers;
    QSet<int> watched;
    bool persistDisabled = false;
};
} // namespace Scanner
