#pragma once

#include <QListWidgetItem>
#include <QWidget>
#include <memory>

#include "include/database/entities/IpList.h"

class QLabel;
class QToolButton;

class IpListItem : public QWidget {
    Q_OBJECT

public:
    IpListItem(QWidget *parent, std::shared_ptr<Configs::IpList> list, QListWidgetItem *item,
               const QString &ownerScanName = {});

    [[nodiscard]] int ListId() const { return list->id; }

    [[nodiscard]] const std::shared_ptr<Configs::IpList> &List() const { return list; }

    void SetList(std::shared_ptr<Configs::IpList> list, const QString &ownerScanName = {});

    void SetRefreshing(bool refreshing);

    // The owning list shows the actions of the hovered row only.
    void SetActionsVisible(bool visible);

signals:
    void editRequested();

    void refreshRequested();

    void deleteRequested();

protected:
    void changeEvent(QEvent *event) override;

    void resizeEvent(QResizeEvent *event) override;

private:
    std::shared_ptr<Configs::IpList> list;

    QListWidgetItem *item;

    QString subtitleText;

    QString errorText;

    bool refreshing = false;

    bool actionsVisible = false;

    QLabel *nameLabel = nullptr;

    QLabel *subtitleLabel = nullptr;

    QLabel *errorLabel = nullptr;

    QLabel *busyLabel = nullptr;

    QToolButton *refreshButton = nullptr;

    QToolButton *editButton = nullptr;

    QToolButton *deleteButton = nullptr;

    [[nodiscard]] bool canRefresh() const;

    void updateActions() const;

    void applyListTexts(const QString &ownerScanName);

    void updateElidedTexts() const;

    void applyColors() const;

    void applyIconColors() const;
};
