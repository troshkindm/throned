#pragma once

#include <functional>

#include "include/ui/widget/TrayPopupFrame.hpp"

class QListWidgetItem;

// Every group in one searchable list. Finding a name among thirty pills is a
// linear search with a mouse, and a group the strip cannot show at all is not
// reachable by scrolling either; this is two keystrokes for both.
class GroupOverflowMenu : public TrayPopupFrame {
    Q_OBJECT

public:
    explicit GroupOverflowMenu(std::function<void(int)> chooseGroup, QWidget *parent = nullptr);

protected:
    void preparePopup() override;

private:
    void rebuild();
    void activateItem(QListWidgetItem *item);

    std::function<void(int)> chooseGroup_;
};
