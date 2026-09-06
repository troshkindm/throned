#pragma once

#include <QFrame>
#include <QString>
#include <QToolButton>
#include <QWidget>

class QEvent;
class QMouseEvent;
class QObject;

// Shared production copy of the title treatment used by tools/ui-demo/MainPreview.
class ThronedLogoMark final : public QWidget {
public:
    explicit ThronedLogoMark(QWidget *parent = nullptr);

protected:
    void paintEvent(QPaintEvent *event) override;
};

// Window caption button. The glyphs are drawn as pixel-aligned strokes instead
// of text so they keep Windows' proportions at any bar height, and the hover
// area fills the whole button the way a native caption control does.
class ThronedCaptionButton final : public QToolButton {
public:
    enum class Glyph { Minimize,
                       Maximize,
                       Restore,
                       Close };

    ThronedCaptionButton(Glyph glyph, QWidget *parent = nullptr);
    void setGlyph(Glyph glyph);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    Glyph glyph_;
};

class ThronedTitleBar final : public QFrame {
public:
    explicit ThronedTitleBar(const QString &context = {}, QWidget *parent = nullptr);

    [[nodiscard]] ThronedCaptionButton *minimizeButton() const { return minimize_; }

    [[nodiscard]] ThronedCaptionButton *maximizeButton() const { return maximize_; }

    [[nodiscard]] ThronedCaptionButton *closeButton() const { return close_; }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;

private:
    ThronedCaptionButton *minimize_ = nullptr;
    ThronedCaptionButton *maximize_ = nullptr;
    ThronedCaptionButton *close_ = nullptr;
};
