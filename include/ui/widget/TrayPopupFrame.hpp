#pragma once

#include <QFrame>

class QKeyEvent;
class QLineEdit;
class QListWidget;
class QVBoxLayout;

// Shared chrome for the tray popups: frameless always-on-top card, search row with close button, screen-clamped placement; subclasses own the list and item activation.
class TrayPopupFrame : public QFrame {
    Q_OBJECT

public:
    explicit TrayPopupFrame(QWidget *parent = nullptr);

    // preparePopup(), size to fit, clamp to the screen under globalPos, show, focus the search box, afterShow().
    void popupAt(const QPoint &globalPos);

protected:
    void keyPressEvent(QKeyEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

    virtual void preparePopup() = 0;
    virtual void afterShow() {}

    void clearSearch();
    void setListWidget(QListWidget *list);

    QFrame *m_card = nullptr;
    QVBoxLayout *m_cardLayout = nullptr;
    QLineEdit *m_search = nullptr;
    QListWidget *m_list = nullptr;
};
