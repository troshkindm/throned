#pragma once

#include <QElapsedTimer>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QString>

#include <atomic>
#include <functional>

namespace Sys {
class GuardProcess;
struct GuardConfig;

// UI thread only, except state() and permitExtraCore().
class KillSwitch : public QObject {
    Q_OBJECT

public:
    enum class State { Disabled,
                       Arming,
                       Armed,
                       Failed };

    static KillSwitch *instance();

    [[nodiscard]] State state() const { return state_.load(); }
    [[nodiscard]] bool allowsStart() const;
    [[nodiscard]] QString failureCode() const { return failureCode_; }
    [[nodiscard]] QString failureMessage() const { return failureMessage_; }
    [[nodiscard]] bool failedForPrivileges() const;
    [[nodiscard]] QString failureText() const;

    void apply();

    void whenSettled(QObject *context, std::function<void(bool armed)> callback);

    // Worker threads only: blocks until the guard permits the extra core's executable or gives up (Windows).
    bool permitExtraCore(const QString &path);

    void shutdown();

signals:
    void stateChanged();

private:
    struct Waiter {
        QPointer<QObject> context;
        bool bound = false;
        // Waits for the pending guard to resolve, not only for the state to leave Arming.
        bool idle = false;
        std::function<void(bool)> callback;
    };

    KillSwitch() = default;

    void spawn(const GuardConfig &config);
    void closeGuard(GuardProcess *guard);
    void readOutput(GuardProcess *guard);
    void readErrors(GuardProcess *guard, bool flush);
    void handleLine(GuardProcess *guard, const QString &line);
    void onReady(GuardProcess *guard);
    void onReadyTimeout(GuardProcess *guard);
    void onStartFailed(GuardProcess *guard);
    void onFinished(GuardProcess *guard, int exitCode, bool crashed);
    void armFailed(const QString &code, const QString &message);
    void protectionLost(const QString &code, const QString &message);
    void fail(const QString &code, const QString &message);
    void setState(State state);
    void settle();
    void whenIdle(std::function<void()> callback);

    std::atomic<State> state_ = State::Disabled;
    GuardProcess *active_ = nullptr;
    GuardProcess *pending_ = nullptr;
    QString failureCode_;
    QString failureMessage_;
    QElapsedTimer lastRespawn_;
    QList<Waiter> waiters_;
    bool shutdown_ = false;
};
} // namespace Sys
