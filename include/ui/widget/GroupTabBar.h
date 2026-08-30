#pragma once

#include <QHash>
#include <QTabBar>
#include <QTabWidget>

// The group strip doubles as the subscription readout: a hairline along the
// bottom of a tab shows how much of that subscription's allowance is spent, so
// the main window gains the information without gaining a row.
class GroupTabBar : public QTabBar {
    Q_OBJECT

public:
    explicit GroupTabBar(QWidget *parent = nullptr);

    // fraction is 0..1 of the allowance used; anything negative clears the line.
    void setUsage(int index, double fraction);

    void clearUsage();

    // An external view such as Favourites can own the visible selection while
    // QTabWidget keeps a real group page current underneath it.
    void setSelectionVisible(bool visible);
    [[nodiscard]] bool isSelectionVisible() const { return selectionVisible_; }

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QHash<int, double> usage_;
    bool selectionVisible_ = true;
};

// Exists only to install GroupTabBar: QTabWidget::setTabBar is protected.
class GroupTabWidget : public QTabWidget {
    Q_OBJECT

public:
    explicit GroupTabWidget(QWidget *parent = nullptr);

    [[nodiscard]] GroupTabBar *groupTabBar() const;
};
