#pragma once

#include <QAbstractButton>
#include <QFrame>
#include <QList>

class QAbstractScrollArea;
class QButtonGroup;
class QHBoxLayout;
class QLabel;
class QListView;
class QProgressBar;
class QPropertyAnimation;
class QPushButton;
class QScrollArea;
class QStandardItemModel;

// Fades the clipped edge of a scroll area, so a list says it continues without a permanent scrollbar.
class ScrollEdgeFade : public QWidget {
public:
    ScrollEdgeFade(QAbstractScrollArea *area, QColor (*background)());
    // Where the top fade starts, so a pinned header above the list keeps it underneath rather than over itself.
    void setTopInset(int inset);

protected:
    void paintEvent(QPaintEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    QAbstractScrollArea *area_;
    QColor (*background_)();
    int topInset_ = 0;
};

// A panel that slides up over its host with a dimmed backdrop; Escape and a click outside close it.
class SimpleSheet : public QWidget {
    Q_OBJECT
    Q_PROPERTY(qreal progress READ progress WRITE setProgress)

public:
    explicit SimpleSheet(QWidget *host);
    [[nodiscard]] QWidget *panel() const { return panel_; }
    void open();
    void dismiss();
    [[nodiscard]] bool isOpen() const { return open_; }
    [[nodiscard]] qreal progress() const { return progress_; }
    void setProgress(qreal progress);
    static QColor panelColor();

signals:
    void dismissed();

protected:
    bool event(QEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;
    virtual int preferredPanelHeight() const;
    virtual QWidget *focusTarget() const { return panel_; }
    void relayout();

private:
    QWidget *panel_;
    QPropertyAnimation *animation_;
    qreal progress_ = 0.0;
    bool open_ = false;
};

struct SimpleServerEntry {
    int id = -1;
    QString name;
    QString address;
    QString latencyText;
    int latency = 0;
    bool current = false;
};

class SimpleServerSheet : public SimpleSheet {
    Q_OBJECT

public:
    explicit SimpleServerSheet(QWidget *host);
    void setGroups(const QList<QPair<int, QString>> &groups, int shownGroup);
    void setServers(const QList<SimpleServerEntry> &servers);
    void setAllowance(const QString &text, qreal fraction);
    void setTesting(bool testing, bool available);
    [[nodiscard]] int shownGroup() const { return shownGroup_; }
    [[nodiscard]] QListView *list() const { return list_; }
    [[nodiscard]] int profileAt(int row) const;

signals:
    void groupShown(int groupId);
    void serverChosen(int profileId);
    void testRequested();
    void addFromClipboard();
    void addFromLink();
    void allowanceClicked(const QPoint &globalPos);

protected:
    int preferredPanelHeight() const override;
    QWidget *focusTarget() const override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    QScrollArea *tabsArea_;
    QWidget *tabs_;
    QHBoxLayout *tabsLayout_;
    QButtonGroup *tabGroup_;
    QPushButton *test_;
    QPushButton *allowance_;
    QProgressBar *allowanceBar_;
    QListView *list_;
    QStandardItemModel *model_;
    QLabel *empty_;
    int shownGroup_ = -1;
};

enum class SimpleMode { WholeComputer,
                        BrowserOnly,
                        Off };

class SimpleModeOption;

class SimpleModeSheet : public SimpleSheet {
    Q_OBJECT

public:
    explicit SimpleModeSheet(QWidget *host);
    void setState(SimpleMode current, bool browserAvailable, const QString &elevationNote);

signals:
    void modeChosen(SimpleMode mode, bool elevationConfirmed);

protected:
    int preferredPanelHeight() const override;
    QWidget *focusTarget() const override;

private:
    void choose(SimpleMode mode);
    SimpleModeOption *whole_;
    SimpleModeOption *browser_;
    QFrame *elevation_;
    QLabel *elevationText_;
    QPushButton *elevationButton_;
    QString elevationNote_;
    SimpleMode current_ = SimpleMode::Off;
};
