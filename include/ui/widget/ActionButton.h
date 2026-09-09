#pragma once

#include "include/ui/widget/MaterialIcon.h"

#include <QAbstractButton>
#include <QColor>
#include <QFont>
#include <QString>

#include <memory>

class QTextLayout;
class QTimer;

// One row of the routing sidebar: icon, name, and a count pill. A profile name can
// be far wider than the column, so it scrolls on its own -- the tail of a
// subscription name is the part that tells them apart.
class ActionButton final : public QAbstractButton {
    Q_OBJECT

public:
    ActionButton(MaterialIcon::Glyph glyph, const QString &title, const QColor &tone, QWidget *parent = nullptr);

    ~ActionButton() override;

    void setCount(int count);
    int count() const { return count_; }

    void setScrollsWhenTooLong(bool enabled);

protected:
    void paintEvent(QPaintEvent *) override;

    void hideEvent(QHideEvent *) override;

private:
    // Measuring and drawing must come from the same engine: QFontMetrics and
    // QPainter::drawText disagree about a string whose emoji come from a fallback font.
    void ensureLayout(const QFont &font);

    static constexpr int kMarqueePause = 45;

    MaterialIcon::Glyph glyph_;
    QString title_;
    QColor tone_;
    int count_ = 0;
    QTimer *marquee_ = nullptr;
    int offset_ = 0;
    int overflow_ = 0;
    int naturalWidth_ = -1;
    std::unique_ptr<QTextLayout> layout_;
    QFont layoutFont_;
};
