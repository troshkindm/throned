#pragma once

#include <functional>

#include "include/ui/widget/TrayPopupFrame.hpp"

class QListWidgetItem;

// Every group in one searchable list: the strip cannot show them all, and scanning thirty pills is slower than typing.
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
