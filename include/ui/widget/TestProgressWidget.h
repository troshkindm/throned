#pragma once

#include <QFrame>

#include "include/ui/utils/DataViewHtmlGenerator.h"

class QLabel;
class QProgressBar;
class QToolButton;

class TestProgressWidget final : public QFrame {
    Q_OBJECT
public:
    explicit TestProgressWidget(QWidget *parent = nullptr);
    void setSnapshot(const DataViewHtmlGenerator::TestPanelState &state);

signals:
    void stopRequested();

private:
    void refreshIcon();
    QLabel *icon_;
    QLabel *title_;
    QLabel *counter_;
    QLabel *profile_;
    QLabel *download_;
    QLabel *upload_;
    QProgressBar *progress_;
    QToolButton *stop_;
    bool stopping_ = false;
};
