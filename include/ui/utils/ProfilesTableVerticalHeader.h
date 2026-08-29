#pragma once

#include <QColor>
#include <QHeaderView>

class ProfilesTableModel;
class ProfilesFilterProxyModel;

class ProfilesTableVerticalHeader : public QHeaderView {
    Q_OBJECT
    // paintSection cannot read the ::section QSS rule, and the widget-level rule has
    // to stay transparent for the card corners, so the fill colour comes in by name.
    Q_PROPERTY(QColor sectionBackground MEMBER m_sectionBackground)
public:
    explicit ProfilesTableVerticalHeader(QWidget *parent = nullptr);

    // Sections are numbered in `proxy`'s row space (may be null); labels come from `model`.
    void setProfilesModel(ProfilesTableModel *model, ProfilesFilterProxyModel *proxy = nullptr);
    ProfilesTableModel *profilesModel() const { return m_model; }

    void updateWidthFromRowCount();

protected:
    void paintSection(QPainter *painter, const QRect &rect, int logicalIndex) const override;

private:
    ProfilesTableModel *m_model = nullptr;
    ProfilesFilterProxyModel *m_proxy = nullptr;
    QColor m_sectionBackground;
};
