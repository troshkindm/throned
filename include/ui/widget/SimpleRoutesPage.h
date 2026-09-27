#pragma once

#include <QHash>
#include <QIcon>
#include <QWidget>

#include "include/database/entities/AppRoutes.h"

class QLabel;
class QLineEdit;
class QPushButton;
class QScrollArea;
class QVBoxLayout;

struct SimpleRouteTarget {
    int outbound = 0;
    QString name;
    QString latencyText;
    int latency = 0;
};

// Simple mode's routing screen: apps found on this computer, the rest of the catalog and the user's own entries.
// Edits stay staged until Apply, because every applied change restarts the connection.
class SimpleRoutesPage : public QWidget {
    Q_OBJECT

public:
    explicit SimpleRoutesPage(QWidget *parent);
    void load(const QList<Configs::AppRoutes::Route> &routes, int restOutbound, int foreignRules, bool editable);
    void setTargets(const SimpleRouteTarget &main, const QList<SimpleRouteTarget> &others);
    void setDetected(const QHash<QString, bool> &runningById);
    void setIcon(const QString &id, const QIcon &icon);
    void setWholeComputer(bool wholeComputer);
    void setConnected(bool connected);
    // Lines from the application picker, added to the user's own entry.
    void addEntries(const QStringList &lines);
    [[nodiscard]] bool isDirty() const;
    // Every maintained list the staged entries use; the owner downloads them before Apply needs them.
    [[nodiscard]] QStringList neededRuleSets() const;
    // Progress of those downloads: nothing shown at total 0, an error replaces the progress.
    void setDownload(int done, int total, const QString &error);
    [[nodiscard]] QWidget *scrollArea() const;

signals:
    void back();
    void openAdvanced();
    void wholeComputerRequested();
    void pickApplication();
    void applyRequested(const QList<Configs::AppRoutes::Route> &routes, int restOutbound);
    void ruleSetsNeeded(const QStringList &names);
    void retryDownload();

private:
    void render();
    void renderFooter();
    QWidget *makeSection(const QString &title, const QString &count);
    QWidget *makeRow(const QString &id);
    QWidget *makeDetail(const QString &id);
    QWidget *makeRestRow();
    Configs::AppRoutes::Route *route(const QString &id);
    const Configs::AppRoutes::CatalogEntry *entry(const QString &id) const;
    [[nodiscard]] int defaultOutbound() const;
    [[nodiscard]] QString targetName(int outbound) const;
    void setRouteEnabled(const QString &id, bool enabled);
    void setRest(int outbound);
    bool addValue(const QString &id, const QString &text);
    void removeValue(const QString &id, int kind, const QString &value);
    [[nodiscard]] bool isModified(const QString &id) const;
    [[nodiscard]] QString categoryName(const QString &category) const;
    void updateSticky();

    QList<Configs::AppRoutes::CatalogEntry> catalog_;
    QList<Configs::AppRoutes::Route> loaded_;
    QList<Configs::AppRoutes::Route> routes_;
    int loadedRest_ = 0;
    int rest_ = 0;
    int foreign_ = 0;
    bool editable_ = true;
    bool wholeComputer_ = true;
    bool connected_ = false;
    QHash<QString, bool> detected_;
    QHash<QString, QIcon> icons_;
    SimpleRouteTarget main_;
    QList<SimpleRouteTarget> others_;
    QString open_;
    QString focusAdd_;
    bool showMore_ = false;
    QLineEdit *search_;
    QScrollArea *scroll_;
    QWidget *content_ = nullptr;
    QWidget *currentCard_ = nullptr;
    QLabel *sentence_;
    QPushButton *apply_;
    QWidget *download_;
    QLabel *downloadText_;
    class QProgressBar *downloadBar_;
    QPushButton *downloadRetry_;
    QStringList lastNeeded_;
    bool downloading_ = false;
    bool renderQueued_ = false;
    struct Section {
        QWidget *widget;
        QString title;
        QString count;
    };
    QList<Section> sections_;
    class StickyHeader *sticky_ = nullptr;
    class ScrollEdgeFade *fade_ = nullptr;
};
