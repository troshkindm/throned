#include <QAbstractButton>
#include <QApplication>
#include <QButtonGroup>
#include <QCommandLineParser>
#include <QComboBox>
#include <QDateTime>
#include <QFrame>
#include <QGraphicsDropShadowEffect>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QItemSelectionModel>
#include <QPainter>
#include <QPainterPath>
#include <QPolygonF>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QStackedWidget>
#include <QStyle>
#include <QStyleOption>
#include <QSvgRenderer>
#include <QTableWidget>
#include <QTextEdit>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWindow>

#include <functional>
#include <tuple>

#include "include/ui/setting/ThronedPalette.hpp"

// Neutral tones follow the selected Throned theme; the rest are semantic
// category colors that stay put whichever theme is active.
namespace Palette {
inline QColor Window, Surface, SurfaceRaised, SurfaceHover, Border, BorderStrong;
inline QColor Text, Muted, Subtle, Blue, BlueSoft, Green;
constexpr auto Cyan = "#29B6F6";
constexpr auto Red = "#ff4d56";
constexpr auto Magenta = "#dd2d78";
constexpr auto Purple = "#a66cff";
constexpr auto Amber = "#f5a524";

inline void adopt(const ThronedThemeColors &colors)
{
    Window = colors.window;
    Surface = colors.surface;
    SurfaceRaised = colors.surfaceRaised;
    SurfaceHover = colors.surfaceHover;
    Border = colors.border;
    BorderStrong = colors.borderStrong;
    Text = colors.text;
    Muted = colors.textMuted;
    Subtle = colors.textSubtle;
    Blue = colors.accent;
    BlueSoft = colors.accentSoft;
    Green = colors.success;
}
}

// Paths are from Google Material Design Icons (Apache-2.0).
namespace MaterialPath {
constexpr auto Desktop = "M21 2H3c-1.1 0-2 .9-2 2v12c0 1.1.9 2 2 2h7v2H8v2h8v-2h-2v-2h7c1.1 0 2-.9 2-2V4c0-1.1-.9-2-2-2zm0 14H3V4h18v12z";
constexpr auto Settings = "M19.43 12.98c.04-.32.07-.64.07-.98s-.03-.66-.07-.98l2.11-1.65c.19-.15.24-.42.12-.64l-2-3.46a.5.5 0 0 0-.61-.22l-2.49 1a7.6 7.6 0 0 0-1.69-.98l-.38-2.65A.5.5 0 0 0 14 2h-4a.5.5 0 0 0-.49.42l-.38 2.65c-.61.25-1.17.59-1.69.98l-2.49-1a.5.5 0 0 0-.61.22l-2 3.46a.5.5 0 0 0 .12.64l2.11 1.65c-.04.32-.07.65-.07.98s.03.66.07.98l-2.11 1.65a.5.5 0 0 0-.12.64l2 3.46c.12.21.38.3.61.22l2.49-1c.52.4 1.08.73 1.69.98l.38 2.65c.03.24.24.42.49.42h4c.25 0 .46-.18.49-.42l.38-2.65c.61-.25 1.17-.58 1.69-.98l2.49 1c.23.08.49-.01.61-.22l2-3.46a.5.5 0 0 0-.12-.64l-2.11-1.65zM12 15.5A3.5 3.5 0 1 1 12 8a3.5 3.5 0 0 1 0 7.5z";
constexpr auto Groups = "M4 13a2 2 0 1 0 0-4 2 2 0 0 0 0 4zm16 0a2 2 0 1 0 0-4 2 2 0 0 0 0 4zM12 12a3 3 0 1 0 0-6 3 3 0 0 0 0 6zM4 14c-1 0-1.93.21-2.78.58A2.02 2.02 0 0 0 0 16.43V18h4.5v-1.61c0-.83.23-1.61.63-2.29A7.2 7.2 0 0 0 4 14zm16 0c-.39 0-.76.04-1.13.1.4.68.63 1.46.63 2.29V18H24v-1.57c0-.81-.48-1.53-1.22-1.85A7 7 0 0 0 20 14zm-3.76-.35A10.7 10.7 0 0 0 12 12.75c-1.63 0-3.07.39-4.24.9A3 3 0 0 0 6 16.39V18h12v-1.61a3 3 0 0 0-1.76-2.74z";
constexpr auto Routes = "M9.78 11.16l-1.42 1.42a7.28 7.28 0 0 1-1.79-2.94l1.94-.49c.32.89.77 1.5 1.27 2.01zM11 6L7 2 3 6h3.02c.02.81.08 1.54.19 2.17l1.94-.49C8.08 7.2 8.03 6.63 8.02 6H11zm10 0l-4-4-4 4h2.99c-.1 3.68-1.28 4.75-2.54 5.88-.5.44-1.01.92-1.45 1.55-.34-.49-.73-.88-1.13-1.24L9.46 13.6c.86.75 1.53 1.4 1.53 3.4v5h2v-5c0-1.46.68-2.1 1.79-3.1 1.35-1.22 3.02-2.74 3.15-6.9H21z";
constexpr auto Tools = "M22.7 19 13.6 9.9a6.5 6.5 0 0 0-1.5-6.9L8.3 6.8 5.2 3.7 9 0a6.5 6.5 0 0 0-6.9 1.5 6.5 6.5 0 0 0 8 10.1l9.1 9.1a2.5 2.5 0 0 0 3.5-3.5z";
constexpr auto Filter = "M5.04 4h13.91c.83 0 1.3.95.79 1.61L14 13v6a1 1 0 0 1-1 1h-2a1 1 0 0 1-1-1v-6L4.25 5.61C3.74 4.95 4.21 4 5.04 4z";
constexpr auto Search = "M9.5 3a6.5 6.5 0 1 0 4.1 11.55L19.05 20 21 18.05l-5.45-5.45A6.5 6.5 0 0 0 9.5 3zm0 2a4.5 4.5 0 1 1 0 9 4.5 4.5 0 0 1 0-9z";
constexpr auto More = "M12 8a2 2 0 1 0 0-4 2 2 0 0 0 0 4zm0 2a2 2 0 1 0 0 4 2 2 0 0 0 0-4zm0 6a2 2 0 1 0 0 4 2 2 0 0 0 0-4z";
constexpr auto Copy = "M16 1H4a2 2 0 0 0-2 2v14h2V3h12V1zm3 4H8a2 2 0 0 0-2 2v14c0 1.1.9 2 2 2h11c1.1 0 2-.9 2-2V7a2 2 0 0 0-2-2zm0 16H8V7h11v14z";
constexpr auto Public = "M12 2a10 10 0 1 0 0 20 10 10 0 0 0 0-20zM4 12c0-.61.08-1.21.21-1.78L9 15v1a2 2 0 0 0 2 2v1.93A8 8 0 0 1 4 12zm13.89 5.4A2 2 0 0 0 16 16h-1v-3a1 1 0 0 0-1-1H8v-2h2a1 1 0 0 0 1-1V7h2a2 2 0 0 0 2-2v-.41A8 8 0 0 1 17.89 17.4z";
constexpr auto Rocket = "M6 15a3 3 0 0 0-2.12.88C2.7 17.06 2 22 2 22s4.94-.7 6.12-1.88A3 3 0 0 0 6 15zm11.42-1.35C23.78 7.29 21.66 2.34 21.66 2.34s-4.95-2.12-11.31 4.24l-2.49-.5a2 2 0 0 0-1.81.55L2 10.69l5 2.14L11.17 17l2.14 5 4.05-4.05c.47-.47.68-1.15.55-1.81l-.49-2.49zM16 12.24c-1.32 1.32-3.38 2.4-4.04 2.73l-2.93-2.93c.32-.65 1.4-2.71 2.73-4.04 4.68-4.68 8.23-3.99 8.23-3.99s.69 3.55-3.99 8.23z";
constexpr auto Signal = "M17 4h3v16h-3V4zM5 14h3v6H5v-6zm6-5h3v11h-3V9z";
constexpr auto Shield = "M12 2 4 5v6.09C4 16.14 7.41 20.85 12 22c4.59-1.15 8-5.86 8-10.91V5l-8-3zm0 17.92c-3.45-1.13-6-4.82-6-8.83v-4.7l6-2.25 6 2.25v4.7c0 4-2.55 7.7-6 8.83z";
constexpr auto Apps = "M4 8h4V4H4v4zm6 12h4v-4h-4v4zm-6 0h4v-4H4v4zm0-6h4v-4H4v4zm6 0h4v-4h-4v4zm6-10v4h4V4h-4zm-6 4h4V4h-4v4zm6 6h4v-4h-4v4zm0 6h4v-4h-4v4z";
constexpr auto Process = "M20 4H4a2 2 0 0 0-2 2v12c0 1.1.89 2 2 2h16c1.1 0 2-.9 2-2V6a2 2 0 0 0-2-2zm0 14H4V8h16v10zm-2-1h-6v-2h6v2zM7.5 17l-1.41-1.41L8.67 13l-2.59-2.59L7.5 9l4 4-4 4z";
constexpr auto List = "M4 10.5A1.5 1.5 0 1 0 4 13.5a1.5 1.5 0 0 0 0-3zm0-6A1.5 1.5 0 1 0 4 7.5a1.5 1.5 0 0 0 0-3zm0 12A1.5 1.5 0 1 0 4 19.5a1.5 1.5 0 0 0 0-3zM7 19h14v-2H7v2zm0-6h14v-2H7v2zm0-8v2h14V5H7z";
constexpr auto Help = "M11 18h2v-2h-2v2zm1-16a10 10 0 1 0 0 20 10 10 0 0 0 0-20zm0 18a8 8 0 1 1 0-16 8 8 0 0 1 0 16zm0-14a4 4 0 0 0-4 4h2a2 2 0 1 1 4 0c0 2-3 1.75-3 5h2c0-2.25 3-2.5 3-5a4 4 0 0 0-4-4z";
constexpr auto Add = "M19 13h-6v6h-2v-6H5v-2h6V5h2v6h6v2z";
constexpr auto Expand = "M16.59 8.59 12 13.17 7.41 8.59 6 10l6 6 6-6-1.41-1.41z";
constexpr auto ChevronRight = "M9.29 6.71a.996.996 0 0 0 0 1.41L13.17 12l-3.88 3.88a.996.996 0 1 0 1.41 1.41l4.59-4.59a.996.996 0 0 0 0-1.41L10.7 6.71a.996.996 0 0 0-1.41 0z";
constexpr auto Block = "M12 2a10 10 0 1 0 0 20 10 10 0 0 0 0-20zM4 12c0-1.85.63-3.55 1.69-4.9L16.9 18.31A7.96 7.96 0 0 1 4 12zm14.31 4.9L7.1 5.69A7.96 7.96 0 0 1 18.31 16.9z";
constexpr auto Direct = "M2.01 21 23 12 2.01 3 2 10l15 2-15 2 .01 7z";
constexpr auto Bolt = "M11 21h-1l1-7H7.5c-.88 0-.33-.75-.31-.78C8.48 10.94 10.42 7.54 13 3h1l-1 7h3.5c.4 0 .62.19.4.66C12.97 17.53 11 21 11 21z";
constexpr auto Pause = "M6 19h4V5H6v14zm8-14v14h4V5h-4z";
constexpr auto Stop = "M6 6h12v12H6z";
constexpr auto Devices = "M4 6h18V4H4a2 2 0 0 0-2 2v11H0v3h14v-3H4V6zm19 2h-6a1 1 0 0 0-1 1v10c0 .55.45 1 1 1h6c.55 0 1-.45 1-1V9a1 1 0 0 0-1-1zm-1 9h-4v-7h4v7z";
}

static QString trText(bool ru, const QString &en, const QString &ruText)
{
    return ru ? ruText : en;
}

static QPixmap materialPixmap(const char *path, const QColor &color, int pixels)
{
    const QByteArray svg = QByteArray("<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24'><path fill='")
        + color.name(QColor::HexRgb).toUtf8() + "' d='" + path + "'/></svg>";
    QSvgRenderer renderer(svg);
    QPixmap pixmap(pixels, pixels);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    renderer.render(&painter, QRectF(0, 0, pixels, pixels));
    return pixmap;
}

static QIcon materialIcon(const char *path, const QColor &color = QColor(Palette::Muted), int pixels = 24)
{
    return QIcon(materialPixmap(path, color, pixels));
}

enum class Flag { Russia, Poland, Finland, Germany };

static QPixmap flagPixmap(Flag flag, const QSize &size = QSize(18, 12))
{
    QPixmap pixmap(size);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    const QRect r = pixmap.rect().adjusted(0, 0, -1, -1);
    painter.setClipRect(r);
    switch (flag) {
    case Flag::Russia:
        painter.fillRect(QRect(r.left(), r.top(), r.width(), r.height() / 3), QColor("#ffffff"));
        painter.fillRect(QRect(r.left(), r.top() + r.height() / 3, r.width(), r.height() / 3 + 1), QColor("#1c57a7"));
        painter.fillRect(QRect(r.left(), r.top() + 2 * r.height() / 3, r.width(), r.height() / 3 + 1), QColor("#d52b1e"));
        break;
    case Flag::Poland:
        painter.fillRect(QRect(r.left(), r.top(), r.width(), r.height() / 2), QColor("#ffffff"));
        painter.fillRect(QRect(r.left(), r.top() + r.height() / 2, r.width(), r.height() / 2 + 1), QColor("#dc143c"));
        break;
    case Flag::Finland:
        painter.fillRect(r, QColor("#ffffff"));
        painter.fillRect(QRect(r.left(), r.top() + r.height() / 3, r.width(), r.height() / 3), QColor("#003580"));
        painter.fillRect(QRect(r.left() + r.width() / 3, r.top(), r.width() / 5, r.height()), QColor("#003580"));
        break;
    case Flag::Germany:
        painter.fillRect(QRect(r.left(), r.top(), r.width(), r.height() / 3), QColor("#171717"));
        painter.fillRect(QRect(r.left(), r.top() + r.height() / 3, r.width(), r.height() / 3 + 1), QColor("#dd0000"));
        painter.fillRect(QRect(r.left(), r.top() + 2 * r.height() / 3, r.width(), r.height() / 3 + 1), QColor("#ffce00"));
        break;
    }
    painter.setClipping(false);
    painter.setPen(QColor(255, 255, 255, 45));
    painter.drawRoundedRect(r, 1.5, 1.5);
    return pixmap;
}

static QLabel *flagLabel(Flag flag)
{
    auto *label = new QLabel;
    label->setPixmap(flagPixmap(flag));
    label->setFixedSize(18, 12);
    return label;
}

static QLabel *materialLabel(const char *path, const QColor &color = QColor(Palette::Muted), int pixels = 18)
{
    auto *label = new QLabel;
    label->setPixmap(materialPixmap(path, color, pixels));
    label->setFixedSize(pixels, pixels);
    return label;
}

static QFrame *line(Qt::Orientation orientation)
{
    auto *result = new QFrame;
    result->setFrameShape(orientation == Qt::Horizontal ? QFrame::HLine : QFrame::VLine);
    result->setObjectName(orientation == Qt::Horizontal ? "hSeparator" : "vSeparator");
    if (orientation == Qt::Horizontal) result->setFixedHeight(1);
    else result->setFixedWidth(1);
    return result;
}

static QLabel *textLabel(const QString &text, const char *role = "body")
{
    auto *label = new QLabel(text);
    label->setProperty("role", role);
    return label;
}

static QFrame *card(const char *name = "card")
{
    auto *frame = new QFrame;
    frame->setObjectName(name);
    return frame;
}

static void addShadow(QWidget *widget, int blur = 28, int y = 8)
{
    auto *shadow = new QGraphicsDropShadowEffect(widget);
    shadow->setBlurRadius(blur);
    shadow->setOffset(0, y);
    shadow->setColor(QColor(0, 0, 0, 90));
    widget->setGraphicsEffect(shadow);
}

class LogoMark final : public QWidget
{
public:
    explicit LogoMark(const QColor &color = QColor(Palette::Red), QWidget *parent = nullptr)
        : QWidget(parent), m_color(color)
    {
        setFixedSize(32, 26);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const QRectF r = rect().adjusted(1.5, 2.5, -1.5, -2.5);
        QPainterPath crown;
        crown.moveTo(r.left(), r.top() + 2);
        crown.lineTo(r.left() + r.width() * .29, r.top() + r.height() * .53);
        crown.lineTo(r.center().x(), r.top());
        crown.lineTo(r.left() + r.width() * .71, r.top() + r.height() * .53);
        crown.lineTo(r.right(), r.top() + 2);
        crown.lineTo(r.right(), r.bottom() - 5);
        crown.quadTo(r.right(), r.bottom(), r.right() - 5, r.bottom());
        crown.lineTo(r.left() + 5, r.bottom());
        crown.quadTo(r.left(), r.bottom(), r.left(), r.bottom() - 5);
        crown.closeSubpath();
        painter.fillPath(crown, m_color);

        QPainterPath shade;
        shade.moveTo(r.left(), r.top() + 2);
        shade.lineTo(r.center().x(), r.bottom());
        shade.lineTo(r.left() + 5, r.bottom());
        shade.quadTo(r.left(), r.bottom(), r.left(), r.bottom() - 5);
        shade.closeSubpath();
        painter.fillPath(shade, QColor(0, 0, 0, 42));
    }

private:
    QColor m_color;
};

class Toggle final : public QAbstractButton
{
public:
    explicit Toggle(bool checked, QWidget *parent = nullptr) : QAbstractButton(parent)
    {
        setCheckable(true);
        setChecked(checked);
        setCursor(Qt::PointingHandCursor);
        setFixedSize(38, 20);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(Qt::NoPen);
        painter.setBrush(isChecked() ? QColor(Palette::Green) : QColor("#34414d"));
        painter.drawRoundedRect(rect(), height() / 2.0, height() / 2.0);
        painter.setBrush(QColor("#ffffff"));
        const qreal diameter = height() - 4;
        const qreal x = isChecked() ? width() - diameter - 2 : 2;
        painter.drawEllipse(QRectF(x, 2, diameter, diameter));
    }

    void nextCheckState() override
    {
        QAbstractButton::nextCheckState();
        update();
    }
};

class Dot final : public QWidget
{
public:
    explicit Dot(const QColor &color, int size = 8, QWidget *parent = nullptr)
        : QWidget(parent), m_color(color)
    {
        setFixedSize(size, size);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(Qt::NoPen);
        painter.setBrush(m_color);
        painter.drawEllipse(rect());
    }

private:
    QColor m_color;
};

static QToolButton *iconButton(const QString &text, const QString &tip = {})
{
    auto *button = new QToolButton;
    button->setText(text);
    button->setToolTip(tip);
    button->setCursor(Qt::PointingHandCursor);
    button->setProperty("kind", "icon");
    return button;
}

// Mirrors ThronedCaptionButton: pixel-aligned strokes at Windows' proportions,
// with the hover fill covering the full caption cell.
class CaptionButton final : public QToolButton
{
public:
    enum class Glyph { Minimize, Maximize, Restore, Close };

    CaptionButton(Glyph glyph, const QString &tip, QWidget *parent = nullptr)
        : QToolButton(parent), glyph_(glyph)
    {
        setToolTip(tip);
        setFocusPolicy(Qt::NoFocus);
        setAttribute(Qt::WA_Hover, true);
        setFixedSize(46, 48);
    }

    void setGlyph(Glyph glyph)
    {
        if (glyph_ == glyph) return;
        glyph_ = glyph;
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QStyleOption option;
        option.initFrom(this);
        QPainter painter(this);
        style()->drawPrimitive(QStyle::PE_Widget, &option, &painter, this);

        const QColor tint = glyph_ == Glyph::Close && underMouse() ? QColor(Qt::white) : Palette::Text;
        QPen pen(tint);
        pen.setWidthF(1.1);
        painter.setPen(pen);

        const QPointF centre(qRound(width() / 2.0) - 0.5, qRound(height() / 2.0) - 0.5);
        constexpr qreal half = 5.0;
        switch (glyph_) {
        case Glyph::Minimize:
            painter.drawLine(QPointF(centre.x() - half, centre.y()), QPointF(centre.x() + half, centre.y()));
            break;
        case Glyph::Maximize:
            painter.drawRect(QRectF(centre.x() - half, centre.y() - half, half * 2, half * 2));
            break;
        case Glyph::Restore:
            painter.drawRect(QRectF(centre.x() - half, centre.y() - half + 2, half * 2 - 2, half * 2 - 2));
            painter.drawPolyline(QPolygonF{
                QPointF(centre.x() - half + 2, centre.y() - half),
                QPointF(centre.x() + half, centre.y() - half),
                QPointF(centre.x() + half, centre.y() + half - 2),
            });
            break;
        case Glyph::Close:
            painter.setRenderHint(QPainter::Antialiasing, true);
            painter.drawLine(QPointF(centre.x() - half, centre.y() - half), QPointF(centre.x() + half, centre.y() + half));
            painter.drawLine(QPointF(centre.x() + half, centre.y() - half), QPointF(centre.x() - half, centre.y() + half));
            break;
        }
    }

private:
    Glyph glyph_;
};

static QToolButton *materialButton(const char *path, const QString &tip = {}, int pixels = 18)
{
    auto *button = new QToolButton;
    button->setIcon(materialIcon(path, QColor(Palette::Muted), pixels));
    button->setIconSize(QSize(pixels, pixels));
    button->setToolTip(tip);
    button->setCursor(Qt::PointingHandCursor);
    button->setProperty("kind", "icon");
    return button;
}

static QPushButton *navButton(const char *path, const QString &label)
{
    auto *button = new QPushButton(label);
    button->setIcon(materialIcon(path, QColor(Palette::Muted), 20));
    button->setIconSize(QSize(20, 20));
    button->setProperty("kind", "nav");
    button->setCursor(Qt::PointingHandCursor);
    return button;
}

static QLabel *pill(const QString &text, const QString &tone = "neutral")
{
    auto *label = new QLabel(text);
    label->setProperty("role", "pill");
    label->setProperty("tone", tone);
    label->setAlignment(Qt::AlignCenter);
    label->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    label->setFixedHeight(20);
    label->setMinimumWidth(20);
    return label;
}

static QPushButton *chip(const QString &text, const QString &kind = "domain")
{
    auto *button = new QPushButton(text + "   ×");
    button->setProperty("kind", "chip");
    button->setProperty("chipKind", kind);
    if (kind == "app") {
        button->setIcon(materialIcon(MaterialPath::Apps, QColor(Palette::Blue), 15));
        button->setIconSize(QSize(15, 15));
    } else if (kind == "process") {
        button->setIcon(materialIcon(MaterialPath::Process, QColor(Palette::Green), 15));
        button->setIconSize(QSize(15, 15));
    }
    button->setCursor(Qt::PointingHandCursor);
    return button;
}

class TitleBar final : public QFrame
{
public:
    TitleBar(bool ru, const QString &context, QWidget *parent = nullptr) : QFrame(parent)
    {
        setObjectName("titleBar");
        // The main window continues into a command bar, so it carries no
        // hairline of its own; dialogs close the chrome right here.
        setProperty("flush", context.isEmpty());
        setFixedHeight(48);
        auto *layout = new QHBoxLayout(this);
        layout->setContentsMargins(14, 0, 0, 0);
        layout->setSpacing(10);
        layout->addWidget(new LogoMark);
        layout->addWidget(textLabel("Throned", "brand"));
        if (!context.isEmpty()) {
            auto *divider = line(Qt::Vertical);
            divider->setFixedHeight(27);
            layout->addWidget(divider);
            layout->addWidget(textLabel(context, "titleContext"));
        }
        layout->addStretch();
        auto *minimize = new CaptionButton(CaptionButton::Glyph::Minimize, trText(ru, "Minimize", "Свернуть"));
        auto *maximize = new CaptionButton(CaptionButton::Glyph::Maximize, trText(ru, "Maximize", "Развернуть"));
        auto *close = new CaptionButton(CaptionButton::Glyph::Close, trText(ru, "Close", "Закрыть"));
        close->setObjectName("titleClose");
        for (auto *button : {minimize, maximize, close}) layout->addWidget(button);
        connect(minimize, &QToolButton::clicked, this, [this] { window()->showMinimized(); });
        connect(maximize, &QToolButton::clicked, this, [maximize, this] {
            const bool wasMaximized = window()->isMaximized();
            wasMaximized ? window()->showNormal() : window()->showMaximized();
            maximize->setGlyph(wasMaximized ? CaptionButton::Glyph::Maximize : CaptionButton::Glyph::Restore);
        });
        connect(close, &QToolButton::clicked, this, [this] { window()->close(); });
    }

protected:
    void mousePressEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton && window()->windowHandle()) {
            window()->windowHandle()->startSystemMove();
            event->accept();
            return;
        }
        QFrame::mousePressEvent(event);
    }

    void mouseDoubleClickEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton) {
            window()->isMaximized() ? window()->showNormal() : window()->showMaximized();
            event->accept();
            return;
        }
        QFrame::mouseDoubleClickEvent(event);
    }
};

static QFrame *makeCommandBar(bool ru)
{
    auto *bar = new QFrame;
    bar->setObjectName("commandBar");
    bar->setFixedHeight(54);
    auto *layout = new QHBoxLayout(bar);
    layout->setContentsMargins(14, 7, 10, 7);
    layout->setSpacing(5);
    layout->addWidget(navButton(MaterialPath::Desktop, trText(ru, "Program", "Программа")));
    layout->addWidget(line(Qt::Vertical));
    auto *settings = navButton(MaterialPath::Settings, trText(ru, "Settings", "Настройки"));
    settings->setObjectName("settingsNav");
    layout->addWidget(settings);
    layout->addWidget(line(Qt::Vertical));
    layout->addWidget(navButton(MaterialPath::Groups, trText(ru, "Groups", "Группы")));
    layout->addWidget(line(Qt::Vertical));
    auto *routes = navButton(MaterialPath::Routes, trText(ru, "Routes", "Маршруты"));
    routes->setObjectName("routesNav");
    layout->addWidget(routes);
    layout->addWidget(line(Qt::Vertical));
    layout->addWidget(navButton(MaterialPath::Tools, trText(ru, "Utilities", "Утилиты")));
    layout->addStretch();

    layout->addWidget(textLabel(trText(ru, "TUN mode", "Режим TUN"), "controlLabel"));
    layout->addWidget(new Toggle(true));
    auto *smallDivider = line(Qt::Vertical);
    smallDivider->setFixedHeight(33);
    layout->addWidget(smallDivider);
    layout->addWidget(textLabel(trText(ru, "System proxy", "Системный прокси"), "controlLabel"));
    layout->addWidget(new Toggle(false));

    auto *stop = new QPushButton("■");
    stop->setObjectName("stopButton");
    stop->setCursor(Qt::PointingHandCursor);
    stop->setFixedSize(40, 40);
    layout->addSpacing(8);
    layout->addWidget(stop);
    layout->addSpacing(6);
    return bar;
}

static QFrame *makeGroupTabs(bool ru)
{
    auto *row = new QFrame;
    row->setObjectName("transparent");
    auto *layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);
    for (const auto &[text, selected] : QList<QPair<QString, bool>>{{trText(ru, "Default", "Основная"), true}, {"sub.example.net", false}}) {
        auto *button = new QPushButton(text);
        button->setProperty("kind", "tab");
        button->setProperty("selected", selected);
        button->setCursor(Qt::PointingHandCursor);
        layout->addWidget(button);
    }
    auto *add = materialButton(MaterialPath::Add);
    add->setFixedSize(34, 34);
    layout->addWidget(add);
    layout->addStretch();
    layout->addWidget(materialButton(MaterialPath::Filter, trText(ru, "Filters", "Фильтры")));
    auto *search = new QLineEdit;
    search->setPlaceholderText(trText(ru, "Search servers…", "Поиск серверов…"));
    search->setClearButtonEnabled(true);
    search->setFixedWidth(245);
    // Matches the production nudge: Qt centres the action on the frame, which
    // reads as too high next to the placeholder text.
    QPixmap searchGlyph(18, 18);
    searchGlyph.fill(Qt::transparent);
    {
        QPainter painter(&searchGlyph);
        painter.drawPixmap(0, 1, materialIcon(MaterialPath::Search, Palette::Subtle, 17).pixmap(17, 17));
    }
    search->addAction(QIcon(searchGlyph), QLineEdit::LeadingPosition);
    layout->addWidget(search);
    return row;
}

static QTableWidget *makeProfilesTable(bool ru)
{
    auto *table = new QTableWidget(8, 5);
    table->setObjectName("profilesTable");
    table->setHorizontalHeaderLabels({trText(ru, "Type", "Тип"), trText(ru, "Address", "Адрес"), trText(ru, "Name", "Название"), trText(ru, "Latency / test result", "Задержка / тест"), trText(ru, "Traffic", "Трафик")});
    table->verticalHeader()->setVisible(true);
    table->verticalHeader()->setDefaultSectionSize(34);
    table->verticalHeader()->setFixedWidth(38);
    table->setCornerButtonEnabled(false);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::ExtendedSelection);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setShowGrid(false);
    table->horizontalHeader()->setFixedHeight(34);
    table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    table->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Stretch);

    struct Row { QString type; QString address; QList<Flag> flags; QString name; QString test; QString traffic; bool rocket = false; };
    const QList<Row> rows{
        {"VLESS (Xray)", "192.0.2.10:8443", {Flag::Russia, Flag::Poland}, "Moscow → Poland", "44 ms   164.93 Mbps / 108.56 Mbps", "64.76 MiB ↑   42.30 MiB ↓"},
        {"VLESS (Xray)", "198.51.100.24:443", {Flag::Poland}, "Poland", "54 ms", "215.73 MiB ↑   28.76 GiB ↓"},
        {"VLESS (Xray)", "198.51.100.71:8443", {Flag::Finland}, "Finland", "33 ms", ""},
        {"VLESS (Xray)", "203.0.113.15:443", {Flag::Germany}, "Frankfurt", "64 ms", "156.47 MiB ↑   657.88 MiB ↓"},
        {"Hysteria", "192.0.2.10:16011", {}, "Auto (hy2)", "49 ms", "13.59 GiB ↑   18.46 GiB ↓", true},
        {"VLESS (Xray)", "192.0.2.10:2087", {Flag::Russia, Flag::Germany}, "Moscow → Frankfurt", "68 ms", ""},
        {"VLESS (Xray)", "192.0.2.10:2088", {Flag::Russia, Flag::Finland}, "Moscow → Finland", "37 ms", ""},
        {"VLESS (Xray)", "192.0.2.10:443", {}, "Auto (router)", "46 ms", "26.03 GiB ↑   37.28 GiB ↓"},
    };
    for (int row = 0; row < rows.size(); ++row) {
        // The name column is rendered by a dedicated widget so its vector flag
        // and label keep deterministic spacing on systems without emoji fonts.
        const QStringList values{rows[row].type, rows[row].address, QString(), rows[row].test, rows[row].traffic};
        for (int column = 0; column < values.size(); ++column) {
            auto *item = new QTableWidgetItem(values[column]);
            if (column == 0) item->setData(Qt::UserRole, "type");
            if (column == 3) item->setForeground(QColor(Palette::Green));
            table->setItem(row, column, item);
        }
        auto *nameWidget = new QWidget;
        auto *nameLayout = new QHBoxLayout(nameWidget);
        nameLayout->setContentsMargins(9, 0, 4, 0);
        nameLayout->setSpacing(6);
        if (rows[row].rocket) {
            nameLayout->addWidget(materialLabel(MaterialPath::Rocket, QColor(Palette::Muted), 16));
            nameLayout->addWidget(textLabel(rows[row].name));
        } else if (rows[row].flags.size() == 2 && rows[row].name.contains(" → ")) {
            const auto names = rows[row].name.split(" → ");
            nameLayout->addWidget(flagLabel(rows[row].flags[0]));
            nameLayout->addWidget(textLabel(names[0] + " →"));
            nameLayout->addWidget(flagLabel(rows[row].flags[1]));
            nameLayout->addWidget(textLabel(names[1]));
        } else {
            for (const auto flag : rows[row].flags) nameLayout->addWidget(flagLabel(flag));
            nameLayout->addWidget(textLabel(rows[row].name));
        }
        nameLayout->addStretch();
        table->setCellWidget(row, 2, nameWidget);
    }
    table->selectRow(3);
    table->setMinimumHeight(306);
    return table;
}

static QFrame *makeLogs(bool ru)
{
    auto *panel = card("contentCard");
    auto *layout = new QVBoxLayout(panel);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto *tabs = new QFrame;
    tabs->setObjectName("logTabs");
    auto *tabLayout = new QHBoxLayout(tabs);
    tabLayout->setContentsMargins(10, 5, 10, 5);
    tabLayout->setSpacing(4);
    for (const auto &[label, selected] : QList<QPair<QString, bool>>{
             {trText(ru, "Logs", "Логи"), true}, {trText(ru, "Connections", "Подключения"), false}, {trText(ru, "Connection graph", "График соединений"), false}}) {
        auto *button = new QPushButton(label);
        button->setProperty("kind", "logTab");
        button->setProperty("selected", selected);
        tabLayout->addWidget(button);
    }
    tabLayout->addStretch();
    auto *clear = new QPushButton(trText(ru, "Clear", "Очистить"));
    clear->setProperty("kind", "secondary");
    tabLayout->addWidget(clear);
    auto *copy = new QPushButton(trText(ru, "Copy", "Копировать"));
    copy->setProperty("kind", "secondary");
    tabLayout->addWidget(copy);
    tabLayout->addWidget(textLabel(trText(ru, "Auto-scroll", "Автопрокрутка"), "muted"));
    tabLayout->addWidget(new Toggle(true));
    auto *level = new QComboBox;
    level->addItems({"INFO", "DEBUG", "WARNING"});
    level->setFixedWidth(110);
    tabLayout->addWidget(level);
    layout->addWidget(tabs);

    auto *log = new QTextEdit;
    log->setObjectName("logText");
    log->setReadOnly(true);
    log->setLineWrapMode(QTextEdit::WidgetWidth);
    log->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    log->setHtml(
        "<table width='100%' cellspacing='0' cellpadding='0'>"
        "<tr><td width='116' valign='top'><span style='color:#627589'>[17:39:59.137]</span></td><td width='54' valign='top'><span style='color:#2ebc75'>INFO</span></td><td>[router] &nbsp; exchanged A origin.example.net. 29 IN A 203.0.113.88</td></tr>"
        "<tr><td valign='top'><span style='color:#627589'>[17:39:59.602]</span></td><td valign='top'><span style='color:#2ebc75'>INFO</span></td><td>[router] &nbsp; found process path: C:\\Program Files\\ExampleApp\\<span style='color:#c397ff'>client.exe</span></td></tr>"
        "<tr><td valign='top'><span style='color:#627589'>[17:39:59.602]</span></td><td valign='top'><span style='color:#2ebc75'>INFO</span></td><td>inbound/tun[tun-in]: inbound connection from <span style='color:#f5a524'>172.19.0.1:55759</span></td></tr>"
        "<tr><td valign='top'><span style='color:#627589'>[17:39:59.602]</span></td><td valign='top'><span style='color:#2ebc75'>INFO</span></td><td>outbound/hysteria2[proxy]: outbound connection to <span style='color:#f5a524'>203.0.113.42:443</span></td></tr>"
        "<tr><td valign='top'><span style='color:#627589'>[17:39:59.603]</span></td><td valign='top'><span style='color:#2ebc75'>INFO</span></td><td>[router] &nbsp; found process path: C:\\Program Files\\ExampleApp\\resources\\<span style='color:#c397ff'>client.exe</span></td></tr>"
        "<tr><td valign='top'><span style='color:#627589'>[17:39:59.604]</span></td><td valign='top'><span style='color:#2ebc75'>INFO</span></td><td>outbound/hysteria2[proxy]: outbound connection to <span style='color:#f5a524'>203.0.113.42:443</span></td></tr>"
        "<tr><td valign='top'><span style='color:#627589'>[17:39:59.650]</span></td><td valign='top'><span style='color:#2ebc75'>INFO</span></td><td>[router] &nbsp; process matched route profile <span style='color:#35c2f1'>Development</span></td></tr>"
        "</table>");
    log->setMinimumHeight(120);
    layout->addWidget(log);
    return panel;
}

static QFrame *makeConnectedStatusCard(bool ru)
{
    auto *panel = card("statusCard");
    panel->setFixedHeight(68);
    auto *layout = new QHBoxLayout(panel);
    layout->setContentsMargins(15, 7, 13, 7);
    layout->setSpacing(12);

    auto *location = new QVBoxLayout;
    location->setSpacing(4);
    auto *locationTitle = new QHBoxLayout;
    locationTitle->addWidget(materialLabel(MaterialPath::Public, QColor(Palette::Muted), 17));
    locationTitle->addWidget(textLabel("sub.example.net", "statusPrimary"));
    locationTitle->addStretch();
    location->addLayout(locationTitle);
    auto *locationSub = new QHBoxLayout;
    locationSub->addWidget(flagLabel(Flag::Poland));
    locationSub->addWidget(textLabel("Poland, Warsaw", "muted"));
    locationSub->addStretch();
    location->addLayout(locationSub);
    layout->addLayout(location, 2);

    auto *profile = new QVBoxLayout;
    profile->setSpacing(4);
    auto *profileTitle = new QHBoxLayout;
    profileTitle->addWidget(materialLabel(MaterialPath::Rocket, QColor(Palette::Muted), 17));
    profileTitle->addWidget(textLabel("Auto (hy2)", "statusPrimary"));
    profileTitle->addStretch();
    profile->addLayout(profileTitle);
    profile->addWidget(textLabel(trText(ru, "Active · 18 min", "Активен · 18 мин"), "muted"));
    layout->addLayout(profile, 2);

    auto *endpoint = new QHBoxLayout;
    endpoint->addWidget(materialLabel(MaterialPath::Devices, QColor(Palette::Muted), 21));
    endpoint->addWidget(textLabel("Mixed: 127.0.0.1:2080", "statusPrimary"));
    layout->addLayout(endpoint, 3);

    auto *traffic = new QGridLayout;
    traffic->setHorizontalSpacing(10);
    auto *upload = new QLabel("<span style='color:#2ebc75;font-size:17px'>↑</span>&nbsp;<span style='color:#F1F3F5'>86.00 B</span>");
    upload->setTextFormat(Qt::RichText);
    auto *download = new QLabel("<span style='color:#35c2f1;font-size:17px'>↓</span>&nbsp;<span style='color:#F1F3F5'>49.00 B</span>");
    download->setTextFormat(Qt::RichText);
    traffic->addWidget(upload, 0, 0);
    traffic->addWidget(textLabel("1.37 KiB/s", "muted"), 0, 1);
    traffic->addWidget(download, 1, 0);
    traffic->addWidget(textLabel("906.00 B/s", "muted"), 1, 1);
    layout->addLayout(traffic, 2);
    layout->addWidget(materialLabel(MaterialPath::Signal, QColor(Palette::Green), 22));
    layout->addWidget(materialButton(MaterialPath::Expand));
    return panel;
}

static QFrame *makeSelectionStatus(bool ru, bool running)
{
    auto *panel = card("statusCard");
    panel->setFixedHeight(68);
    auto *layout = new QHBoxLayout(panel);
    layout->setContentsMargins(15, 8, 13, 8);
    layout->setSpacing(10);
    layout->addWidget(materialLabel(running ? MaterialPath::Bolt : MaterialPath::List,
        running ? QColor(Palette::Cyan) : Palette::Blue, 22));

    auto *copy = new QVBoxLayout;
    copy->setSpacing(3);
    copy->addWidget(textLabel(running
        ? trText(ru, "URL test · 4 profiles", "URL-тест · 4 профиля")
        : trText(ru, "4 profiles selected", "Выбрано профилей: 4"), "bodyStrong"));
    copy->addWidget(textLabel(running
        ? trText(ru, "Testing Frankfurt · 3 of 4", "Проверяется Франкфурт · 3 из 4")
        : trText(ru, "Run a batch action without losing the selection", "Запустите массовое действие, не теряя выбор"), "muted"));
    layout->addLayout(copy, 2);

    if (running) {
        auto *progress = new QProgressBar;
        progress->setRange(0, 4);
        progress->setValue(3);
        progress->setFormat("3 / 4");
        progress->setMinimumWidth(250);
        layout->addWidget(progress, 2);
        auto *stop = new QPushButton(trText(ru, "Stop test", "Остановить"));
        stop->setProperty("kind", "secondary");
        layout->addWidget(stop);
    } else {
        auto addAction = [layout](const QString &label, const char *name) {
            auto *button = new QPushButton(label);
            button->setProperty("kind", "secondary");
            button->setObjectName(name);
            layout->addWidget(button);
        };
        addAction(trText(ru, "URL test", "URL-тест"), "urlTestAction");
        addAction(trText(ru, "Speed test", "Тест скорости"), "speedTestAction");
        addAction(trText(ru, "Resolve IP", "Определить IP"), "resolveIpAction");
    }
    layout->addStretch();
    return panel;
}

class MainPreview final : public QWidget
{
public:
    explicit MainPreview(bool ru, const QString &initialStatus = {}, QWidget *parent = nullptr) : QWidget(parent)
    {
        setObjectName("previewRoot");
        auto *layout = new QVBoxLayout(this);
        layout->setContentsMargins(1, 1, 1, 1);
        layout->setSpacing(0);
        layout->addWidget(new TitleBar(ru, {}));
        // Title bar and command bar share one background and one closing
        // hairline, so the chrome reads as a header instead of stacked cards.
        layout->addWidget(makeCommandBar(ru));

        auto *body = new QWidget;
        body->setObjectName("body");
        auto *bodyLayout = new QVBoxLayout(body);
        bodyLayout->setContentsMargins(10, 10, 10, 10);
        bodyLayout->setSpacing(7);
        bodyLayout->addWidget(makeGroupTabs(ru));

        auto *tableCard = card("contentCard");
        auto *tableLayout = new QVBoxLayout(tableCard);
        tableLayout->setContentsMargins(0, 0, 0, 0);
        auto *table = makeProfilesTable(ru);
        tableLayout->addWidget(table);
        bodyLayout->addWidget(tableCard, 3);
        bodyLayout->addWidget(makeLogs(ru), 2);
        auto *statuses = new QStackedWidget;
        statuses->setObjectName("mainStatusStack");
        auto *connectedStatus = makeConnectedStatusCard(ru);
        auto *selectionStatus = makeSelectionStatus(ru, false);
        auto *runningStatus = makeSelectionStatus(ru, true);
        statuses->addWidget(connectedStatus);
        statuses->addWidget(selectionStatus);
        statuses->addWidget(runningStatus);
        statuses->setFixedHeight(68);

        connect(table->selectionModel(), &QItemSelectionModel::selectionChanged, this, [table, statuses, connectedStatus, selectionStatus] {
            statuses->setCurrentWidget(table->selectionModel()->selectedRows().size() > 1 ? selectionStatus : connectedStatus);
        });
        if (auto *urlTest = selectionStatus->findChild<QPushButton *>("urlTestAction")) {
            connect(urlTest, &QPushButton::clicked, this, [statuses, runningStatus] { statuses->setCurrentWidget(runningStatus); });
        }
        if (initialStatus == "selected" || initialStatus == "url-test") {
            table->clearSelection();
            for (const int row : {0, 2, 3, 5}) {
                table->setRangeSelected(QTableWidgetSelectionRange(row, 0, row, table->columnCount() - 1), true);
            }
            statuses->setCurrentWidget(initialStatus == "url-test" ? runningStatus : selectionStatus);
        }
        layout->addWidget(body, 1);
        // Bottom chrome mirrors the header band.
        layout->addWidget(statuses);
    }
};

static QFrame *fieldBox(const QString &label, const QString &value, int width = 350)
{
    auto *box = new QFrame;
    box->setObjectName("fieldBox");
    auto *layout = new QVBoxLayout(box);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(6);
    layout->addWidget(textLabel(label, "fieldLabel"));
    auto *input = new QLineEdit(value);
    input->setMinimumWidth(width);
    layout->addWidget(input);
    return box;
}

static QFrame *segmented(bool ru, bool advanced)
{
    auto *box = new QFrame;
    box->setObjectName("segmented");
    auto *layout = new QHBoxLayout(box);
    layout->setContentsMargins(3, 3, 3, 3);
    layout->setSpacing(2);
    for (const auto &[label, selected, objectName] : QList<std::tuple<QString, bool, QString>>{
             {trText(ru, "Simple", "Простой"), !advanced, "simpleMode"},
             {trText(ru, "Advanced", "Расширенный"), advanced, "advancedMode"}}) {
        auto *button = new QPushButton(label);
        button->setObjectName(objectName);
        button->setProperty("kind", "segment");
        button->setProperty("selected", selected);
        button->setCursor(Qt::PointingHandCursor);
        layout->addWidget(button);
    }
    return box;
}

static QPushButton *actionButton(const char *iconPath, const QString &label, int count, const QString &tone, bool selected)
{
    auto *button = new QPushButton;
    button->setProperty("kind", "routeAction");
    button->setProperty("selected", selected);
    button->setProperty("tone", tone);
    auto *layout = new QHBoxLayout(button);
    layout->setContentsMargins(14, 0, 12, 0);
    layout->setSpacing(12);
    QColor iconColor(Palette::Muted);
    if (tone == "green") iconColor = QColor(Palette::Green);
    if (tone == "blue") iconColor = QColor(Palette::Blue);
    if (tone == "red") iconColor = QColor(Palette::Red);
    if (tone == "purple") iconColor = QColor(Palette::Purple);
    auto *icon = materialLabel(iconPath, iconColor, 18);
    layout->addWidget(icon);
    layout->addWidget(textLabel(label, "routeActionText"));
    layout->addStretch();
    layout->addWidget(pill(QString::number(count), selected ? "blue" : "neutral"));
    button->setFixedHeight(44);
    button->setCursor(Qt::PointingHandCursor);
    return button;
}

static QFrame *ruleCard(const char *iconPath, const QString &title, const QString &subtitle,
                         const QList<QPair<QString, QString>> &chips, const QString &tone,
                         bool ru, bool toggleCard = false, bool expanded = true)
{
    auto *panel = card("ruleSection");
    panel->setProperty("tone", tone);
    panel->setProperty("expanded", expanded);
    auto *layout = new QVBoxLayout(panel);
    layout->setContentsMargins(14, 12, 14, 12);
    layout->setSpacing(9);

    auto *heading = new QHBoxLayout;
    QColor iconColor(Palette::Muted);
    if (tone == "blue") iconColor = QColor(Palette::Blue);
    if (tone == "cyan") iconColor = QColor(Palette::Cyan);
    if (tone == "green") iconColor = QColor(Palette::Green);
    auto *icon = materialLabel(iconPath, iconColor, 18);
    icon->setFixedWidth(22);
    heading->addWidget(icon);
    auto *titles = new QVBoxLayout;
    titles->setSpacing(2);
    auto *titleRow = new QHBoxLayout;
    titleRow->addWidget(textLabel(title, "sectionTitle"));
    if (!toggleCard) titleRow->addWidget(pill(QString::number(chips.size())));
    titleRow->addStretch();
    titles->addLayout(titleRow);
    auto *subtitleLabel = textLabel(subtitle, "muted");
    subtitleLabel->setVisible(expanded);
    titles->addWidget(subtitleLabel);
    heading->addLayout(titles, 1);
    auto *expander = materialButton(expanded ? MaterialPath::Expand : MaterialPath::ChevronRight,
        expanded ? trText(ru, "Collapse", "Свернуть") : trText(ru, "Expand", "Развернуть"), 18);
    heading->addWidget(expander);
    layout->addLayout(heading);

    auto *details = new QWidget;
    details->setObjectName("transparent");
    auto *detailsLayout = new QVBoxLayout(details);
    detailsLayout->setContentsMargins(0, 0, 0, 0);
    detailsLayout->setSpacing(7);
    if (toggleCard) {
        auto *quick = new QHBoxLayout;
        quick->setContentsMargins(34, 4, 0, 0);
        quick->addWidget(new Toggle(true));
        auto *copy = new QVBoxLayout;
        copy->setSpacing(1);
        copy->addWidget(textLabel(trText(ru, "Route local proxy traffic through proxy", "Направлять локальный proxy-трафик через прокси"), "bodyStrong"));
        copy->addWidget(textLabel(trText(ru, "Also route mixed-in and socks-in traffic through this outbound.", "Также направлять трафик mixed-in и socks-in через этот outbound."), "muted"));
        quick->addLayout(copy, 1);
        quick->addWidget(iconButton("?"));
        detailsLayout->addLayout(quick);
    } else if (chips.isEmpty()) {
        auto *empty = textLabel(trText(ru, "No rules added yet.", "Правила пока не добавлены."), "empty");
        empty->setContentsMargins(34, 0, 0, 0);
        detailsLayout->addWidget(empty);
    } else {
        auto *chipLayout = new QHBoxLayout;
        chipLayout->setContentsMargins(34, 3, 0, 0);
        chipLayout->setSpacing(8);
        for (const auto &[label, kind] : chips) chipLayout->addWidget(chip(label, kind));
        chipLayout->addStretch();
        detailsLayout->addLayout(chipLayout);
    }
    details->setVisible(expanded);
    layout->addWidget(details);
    QObject::connect(expander, &QToolButton::clicked, panel, [details, subtitleLabel, expander, ru] {
        const bool show = !details->isVisible();
        details->setVisible(show);
        subtitleLabel->setVisible(show);
        expander->setIcon(materialIcon(show ? MaterialPath::Expand : MaterialPath::ChevronRight,
            QColor(Palette::Muted), 18));
        expander->setToolTip(show ? trText(ru, "Collapse", "Свернуть") : trText(ru, "Expand", "Развернуть"));
    });
    return panel;
}

static QFrame *makeRoutingSidebar(bool ru)
{
    auto *sidebar = card("sidebar");
    sidebar->setFixedWidth(228);
    auto *layout = new QVBoxLayout(sidebar);
    layout->setContentsMargins(10, 12, 10, 10);
    layout->setSpacing(6);
    layout->addWidget(textLabel(trText(ru, "Routing actions", "Действия маршрутизации"), "sidebarTitle"));
    layout->addSpacing(8);
    layout->addWidget(actionButton(MaterialPath::Direct, trText(ru, "Direct", "Напрямую"), 1, "green", false));
    layout->addWidget(actionButton(MaterialPath::Shield, trText(ru, "Proxy", "Прокси"), 9, "blue", true));
    layout->addWidget(actionButton(MaterialPath::Block, trText(ru, "Block", "Блокировать"), 2, "red", false));
    layout->addWidget(actionButton(MaterialPath::Routes, "WARP bypass", 0, "purple", false));
    layout->addStretch();

    auto *stats = card("statsCard");
    auto *statsLayout = new QVBoxLayout(stats);
    statsLayout->setContentsMargins(14, 12, 14, 12);
    statsLayout->setSpacing(8);
    statsLayout->addWidget(textLabel("⌁  " + trText(ru, "Profile statistics", "Статистика профиля"), "bodyStrong"));
    auto *total = new QHBoxLayout;
    total->addWidget(textLabel(trText(ru, "Total rules", "Всего правил"), "muted"));
    total->addStretch();
    total->addWidget(textLabel("12", "bodyStrong"));
    statsLayout->addLayout(total);
    auto *updated = new QHBoxLayout;
    updated->addWidget(textLabel(trText(ru, "Last updated", "Обновлено"), "muted"));
    updated->addStretch();
    updated->addWidget(textLabel(trText(ru, "18 min ago", "18 мин назад"), "bodyStrong"));
    statsLayout->addLayout(updated);
    layout->addWidget(stats);
    return sidebar;
}

static QWidget *makeRulesContent(bool ru)
{
    auto *content = new QWidget;
    content->setObjectName("transparent");
    auto *layout = new QVBoxLayout(content);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(8);

    auto *hero = new QHBoxLayout;
    auto *shield = materialLabel(MaterialPath::Shield, QColor(Palette::Blue), 24);
    hero->addWidget(shield);
    auto *copy = new QVBoxLayout;
    copy->setSpacing(3);
    copy->addWidget(textLabel(trText(ru, "Proxy rules", "Правила прокси"), "heroTitle"));
    copy->addWidget(textLabel(trText(ru, "Define traffic that should be routed through a proxy outbound.", "Выберите трафик, который должен идти через прокси."), "muted"));
    hero->addLayout(copy, 1);
    auto *priority = new QPushButton("↕  " + trText(ru, "Rule order", "Порядок правил"));
    priority->setProperty("kind", "secondary");
    auto *addRule = new QPushButton("+  " + trText(ru, "Add rule", "Добавить правило"));
    addRule->setObjectName("primaryButton");
    hero->addWidget(addRule);
    hero->addWidget(priority);
    layout->addLayout(hero);

    layout->addWidget(ruleCard(MaterialPath::Bolt, trText(ru, "Quick options", "Быстрые настройки"),
                               trText(ru, "Common routing behavior.", "Частые сценарии маршрутизации."), {}, "blue", ru, true));
    layout->addWidget(ruleCard(MaterialPath::Apps, trText(ru, "Applications", "Приложения"),
                               trText(ru, "Match by installed app, running process, or executable.", "Выбор установленной программы, процесса или файла."),
                               {{"Demo Chat", "app"}, {"Message Desk", "app"}}, "blue", ru));
    layout->addWidget(ruleCard(MaterialPath::Public, trText(ru, "Domains & rule sets", "Домены и наборы правил"),
                               trText(ru, "Match domain names and remote geosite lists.", "Домены и удалённые списки geosite."),
                               {{"assistant.example", "domain"}, {"gateway.example", "domain"}, {"Example Chat · Geosite", "ruleset"}, {"Example AI · Geosite", "ruleset"}}, "cyan", ru));
    layout->addWidget(ruleCard(MaterialPath::Process, trText(ru, "Processes & IP/CIDR", "Процессы и IP/CIDR"),
                               trText(ru, "Match process path, port, IP address, or CIDR range.", "Путь процесса, порт, IP-адрес или диапазон CIDR."),
                               {{"DemoClient.exe", "process"}, {"192.0.2.0/24", "network"}}, "green", ru, false, false));
    layout->addWidget(ruleCard(MaterialPath::List, trText(ru, "Advanced / raw rules", "Расширенные / raw-правила"),
                               trText(ru, "Nested conditions, exact priority, and lossless JSON.", "Вложенные условия, точный приоритет и JSON без потерь."),
                               {}, "cyan", ru, false, false));
    layout->addStretch();
    return content;
}

static QFrame *conditionPill(const QString &label, const QString &value, const QString &tone = "neutral")
{
    auto *panel = card("conditionPill");
    panel->setProperty("tone", tone);
    auto *layout = new QHBoxLayout(panel);
    layout->setContentsMargins(9, 5, 9, 5);
    layout->setSpacing(7);
    auto *kind = textLabel(label, "conditionKind");
    kind->setProperty("tone", tone);
    layout->addWidget(kind);
    layout->addWidget(textLabel(value, "conditionValue"));
    return panel;
}

static QFrame *orderedRule(int priority, const QString &name, const QString &action,
                           const QList<QPair<QString, QString>> &conditions,
                           const QString &tone, bool ru, bool opaque = false)
{
    auto *panel = card("orderedRule");
    if (opaque) panel->setProperty("opaque", true);
    auto *layout = new QHBoxLayout(panel);
    layout->setContentsMargins(13, 12, 13, 12);
    layout->setSpacing(13);
    layout->addWidget(textLabel("⋮⋮", "dragHandle"));
    auto *number = pill(QString::number(priority), "priority");
    number->setFixedWidth(34);
    layout->addWidget(number);
    layout->addWidget(new Toggle(true));

    auto *body = new QVBoxLayout;
    body->setSpacing(8);
    auto *heading = new QHBoxLayout;
    heading->addWidget(textLabel(name, "bodyStrong"));
    heading->addWidget(pill(action, tone));
    if (opaque) heading->addWidget(pill(trText(ru, "Preserved JSON", "JSON сохранён"), "amber"));
    heading->addStretch();
    body->addLayout(heading);
    auto *matchers = new QHBoxLayout;
    matchers->setSpacing(7);
    matchers->addWidget(textLabel(trText(ru, "ALL", "ВСЕ"), "logic"));
    for (const auto &[kind, value] : conditions) matchers->addWidget(conditionPill(kind, value, tone));
    matchers->addStretch();
    body->addLayout(matchers);
    layout->addLayout(body, 1);
    auto *json = new QPushButton("{ }  JSON");
    json->setProperty("kind", "secondary");
    layout->addWidget(json);
    layout->addWidget(iconButton("⋯"));
    return panel;
}

static QWidget *makeAdvancedRulesContent(bool ru)
{
    auto *content = new QWidget;
    content->setObjectName("transparent");
    auto *layout = new QVBoxLayout(content);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(8);

    auto *hero = new QHBoxLayout;
    auto *icon = materialLabel(MaterialPath::List, QColor(Palette::Cyan), 24);
    hero->addWidget(icon);
    auto *copy = new QVBoxLayout;
    copy->setSpacing(3);
    copy->addWidget(textLabel(trText(ru, "Ordered rule list", "Упорядоченный список правил"), "heroTitle"));
    copy->addWidget(textLabel(trText(ru, "First match wins. Reordering here changes the actual sing-box priority.", "Срабатывает первое совпадение. Порядок здесь — реальный приоритет sing-box."), "muted"));
    hero->addLayout(copy, 1);
    auto *source = new QPushButton("{ }  " + trText(ru, "Full source", "Весь JSON"));
    source->setProperty("kind", "secondary");
    hero->addWidget(source);
    auto *validate = new QPushButton("✓  " + trText(ru, "Validate", "Проверить"));
    validate->setProperty("kind", "secondary");
    hero->addWidget(validate);
    layout->addLayout(hero);

    auto *notice = card("advancedNotice");
    auto *noticeLayout = new QHBoxLayout(notice);
    noticeLayout->setContentsMargins(12, 9, 12, 9);
    noticeLayout->addWidget(new Dot(QColor(Palette::Blue)));
    noticeLayout->addWidget(textLabel(trText(ru, "Showing Proxy rules · original global positions are preserved", "Показаны правила Proxy · позиции в общем списке сохранены"), "muted"));
    noticeLayout->addStretch();
    auto *showAll = new QPushButton(trText(ru, "Show all actions", "Показать все"));
    showAll->setProperty("kind", "link");
    noticeLayout->addWidget(showAll);
    layout->addWidget(notice);

    layout->addWidget(orderedRule(2, trText(ru, "AI services", "AI-сервисы"), "proxy",
                                  {{trText(ru, "Domain", "Домен"), "assistant.example"}, {"Rule set", "geosite:example-ai"}}, "blue", ru));
    layout->addWidget(orderedRule(4, trText(ru, "Development tools", "Инструменты разработки"), "proxy",
                                  {{trText(ru, "Application", "Приложение"), "Demo Editor"}, {trText(ru, "Process", "Процесс"), "DemoClient.exe"}}, "blue", ru));
    layout->addWidget(orderedRule(7, trText(ru, "Local proxy inbounds", "Локальные proxy-inbound"), "proxy",
                                  {{"Inbound", "mixed-in"}, {"Inbound", "socks-in"}}, "blue", ru));
    layout->addWidget(orderedRule(9, trText(ru, "Imported custom rule", "Импортированное правило"), "proxy",
                                  {{"Custom", trText(ru, "Unknown matcher", "Неизвестное условие")}}, "blue", ru, true));

    auto *fallback = card("fallbackCard");
    auto *fallbackLayout = new QHBoxLayout(fallback);
    fallbackLayout->setContentsMargins(14, 11, 14, 11);
    fallbackLayout->addWidget(textLabel("↳", "ruleIcon"));
    fallbackLayout->addWidget(textLabel(trText(ru, "Unmatched traffic", "Остальной трафик"), "bodyStrong"));
    fallbackLayout->addStretch();
    fallbackLayout->addWidget(pill("direct", "green"));
    layout->addWidget(fallback);
    layout->addStretch();
    return content;
}

class RoutesPreview final : public QWidget
{
public:
    explicit RoutesPreview(bool ru, bool advanced, QWidget *parent = nullptr) : QWidget(parent)
    {
        setObjectName("previewRoot");
        auto *root = new QVBoxLayout(this);
        root->setContentsMargins(1, 1, 1, 1);
        root->setSpacing(0);
        root->addWidget(new TitleBar(ru, trText(ru, "Route profile", "Профиль маршрутизации")));

        auto *body = new QWidget;
        body->setObjectName("body");
        auto *layout = new QVBoxLayout(body);
        layout->setContentsMargins(12, 8, 12, 10);
        layout->setSpacing(9);

        auto *top = new QHBoxLayout;
        top->setSpacing(14);
        top->addWidget(fieldBox(trText(ru, "Name", "Название"), trText(ru, "Development", "Разработка"), 250));
        auto *outbound = fieldBox(trText(ru, "Default outbound", "Outbound по умолчанию"), "direct", 250);
        auto *outboundInput = qobject_cast<QLineEdit *>(outbound->layout()->itemAt(1)->widget());
        outboundInput->setReadOnly(true);
        top->addWidget(outbound);
        auto *mode = new QFrame;
        auto *modeLayout = new QVBoxLayout(mode);
        modeLayout->setContentsMargins(0, 0, 0, 0);
        modeLayout->setSpacing(6);
        modeLayout->addWidget(textLabel(trText(ru, "Mode", "Режим"), "fieldLabel"));
        modeLayout->addWidget(segmented(ru, advanced));
        top->addWidget(mode);
        top->addStretch();
        auto *help = new QPushButton("?  " + trText(ru, "Help", "Помощь"));
        help->setProperty("kind", "secondary");
        help->setFixedHeight(42);
        top->addWidget(help, 0, Qt::AlignBottom);
        layout->addLayout(top);

        auto *work = new QHBoxLayout;
        work->setSpacing(10);
        work->addWidget(makeRoutingSidebar(ru));
        auto *rules = new QFrame;
        rules->setObjectName("transparent");
        auto *rulesLayout = new QVBoxLayout(rules);
        rulesLayout->setContentsMargins(0, 0, 0, 0);
        auto *scroll = new QScrollArea;
        scroll->setObjectName("rulesScroll");
        scroll->setWidgetResizable(true);
        scroll->setFrameShape(QFrame::NoFrame);
        scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        scroll->setWidget(advanced ? makeAdvancedRulesContent(ru) : makeRulesContent(ru));
        rulesLayout->addWidget(scroll);
        work->addWidget(rules, 1);
        layout->addLayout(work, 1);

        auto *footer = new QHBoxLayout;
        footer->addWidget(textLabel(trText(ru, "Changes are saved only after validation.", "Изменения сохранятся только после проверки."), "muted"));
        footer->addStretch();
        auto *cancel = new QPushButton(trText(ru, "Cancel", "Отмена"));
        cancel->setProperty("kind", "secondary");
        cancel->setMinimumWidth(138);
        footer->addWidget(cancel);
        auto *save = new QPushButton(trText(ru, "Save profile", "Сохранить"));
        save->setObjectName("primaryButton");
        save->setMinimumWidth(150);
        footer->addWidget(save);
        layout->addLayout(footer);
        root->addWidget(body, 1);
    }
};

static QFrame *settingsField(const QString &label, QWidget *control, const QString &hint = {})
{
    auto *row = new QFrame;
    row->setObjectName("settingsField");
    auto *layout = new QGridLayout(row);
    layout->setContentsMargins(12, 9, 12, 9);
    layout->setHorizontalSpacing(18);
    layout->setVerticalSpacing(3);
    layout->addWidget(textLabel(label, "bodyStrong"), 0, 0);
    if (!hint.isEmpty()) layout->addWidget(textLabel(hint, "muted"), 1, 0);
    if (!qobject_cast<QAbstractButton *>(control)) control->setMinimumWidth(270);
    layout->addWidget(control, 0, 1, hint.isEmpty() ? 1 : 2, 1);
    layout->setColumnStretch(0, 1);
    return row;
}

static QFrame *settingsSection(const QString &title, const QString &subtitle)
{
    auto *section = card("settingsSection");
    auto *layout = new QVBoxLayout(section);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    auto *heading = new QFrame;
    heading->setObjectName("settingsHeading");
    auto *headingLayout = new QVBoxLayout(heading);
    headingLayout->setContentsMargins(14, 11, 14, 9);
    headingLayout->setSpacing(2);
    headingLayout->addWidget(textLabel(title, "sectionTitle"));
    headingLayout->addWidget(textLabel(subtitle, "muted"));
    layout->addWidget(heading);
    return section;
}

class SettingsPreview final : public QWidget
{
public:
    explicit SettingsPreview(bool ru, QWidget *parent = nullptr) : QWidget(parent)
    {
        setObjectName("previewRoot");
        auto *root = new QVBoxLayout(this);
        root->setContentsMargins(1, 1, 1, 1);
        root->setSpacing(0);
        root->addWidget(new TitleBar(ru, trText(ru, "Settings", "Настройки")));

        auto *body = new QWidget;
        body->setObjectName("body");
        auto *bodyLayout = new QHBoxLayout(body);
        bodyLayout->setContentsMargins(12, 10, 12, 10);
        bodyLayout->setSpacing(10);

        auto *sidebar = card("sidebar");
        sidebar->setFixedWidth(220);
        auto *sidebarLayout = new QVBoxLayout(sidebar);
        sidebarLayout->setContentsMargins(10, 12, 10, 12);
        sidebarLayout->setSpacing(6);
        sidebarLayout->addWidget(textLabel(trText(ru, "Settings", "Настройки"), "sidebarTitle"));
        const QList<QPair<QString, bool>> pages{
            {trText(ru, "Common", "Основные"), true}, {trText(ru, "Connection", "Подключение"), false},
            {"TUN", false}, {trText(ru, "DNS && testing", "DNS и тесты"), false},
            {trText(ru, "Hotkeys", "Горячие клавиши"), false}, {trText(ru, "Appearance", "Внешний вид"), false},
            {trText(ru, "Backup", "Резервные копии"), false}};
        for (const auto &[label, selected] : pages) {
            auto *button = new QPushButton(label);
            button->setProperty("kind", "settingsNav");
            button->setProperty("selected", selected);
            sidebarLayout->addWidget(button);
        }
        sidebarLayout->addStretch();
        sidebarLayout->addWidget(textLabel(trText(ru, "Throned 1.2 preview", "Throned 1.2 preview"), "muted"));
        bodyLayout->addWidget(sidebar);

        auto *content = new QScrollArea;
        content->setObjectName("settingsScroll");
        content->setWidgetResizable(true);
        content->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        auto *page = new QWidget;
        page->setObjectName("settingsPage");
        auto *pageLayout = new QVBoxLayout(page);
        pageLayout->setContentsMargins(14, 8, 14, 8);
        pageLayout->setSpacing(10);
        pageLayout->addWidget(textLabel(trText(ru, "Common settings", "Основные настройки"), "heroTitle"));
        pageLayout->addWidget(textLabel(trText(ru, "Everyday proxy, inbound, and test behavior.", "Прокси, входящие подключения и тестирование."), "muted"));

        auto *inbound = settingsSection(trText(ru, "Inbound", "Входящие подключения"),
            trText(ru, "Local proxy endpoint used by applications.", "Локальная точка прокси для приложений."));
        auto *inboundLayout = qobject_cast<QVBoxLayout *>(inbound->layout());
        auto *address = new QLineEdit("127.0.0.1");
        inboundLayout->addWidget(settingsField(trText(ru, "Listen address", "Адрес"), address));
        auto *port = new QLineEdit("2080");
        inboundLayout->addWidget(settingsField(trText(ru, "Mixed port", "Mixed-порт"), port,
            trText(ru, "SOCKS and HTTP on one local port", "SOCKS и HTTP на одном локальном порту")));
        inboundLayout->addWidget(settingsField(trText(ru, "Random port", "Случайный порт"), new Toggle(false),
            trText(ru, "Choose a free port on every start", "Выбирать свободный порт при каждом запуске")));
        pageLayout->addWidget(inbound);

        auto *testing = settingsSection(trText(ru, "Testing", "Тестирование"),
            trText(ru, "Defaults used by URL and speed tests.", "Параметры URL-тестов и проверки скорости."));
        auto *testingLayout = qobject_cast<QVBoxLayout *>(testing->layout());
        auto *url = new QLineEdit("https://latency.example/generate_204");
        testingLayout->addWidget(settingsField(trText(ru, "Latency test URL", "URL для проверки задержки"), url));
        auto *timeout = new QComboBox;
        timeout->addItems({"3000 ms", "5000 ms", "10000 ms"});
        testingLayout->addWidget(settingsField(trText(ru, "Timeout", "Тайм-аут"), timeout));
        auto *mode = new QComboBox;
        mode->addItems({trText(ru, "Download + upload", "Загрузка + отдача"), trText(ru, "Download only", "Только загрузка")});
        testingLayout->addWidget(settingsField(trText(ru, "Speed test mode", "Режим проверки скорости"), mode));
        pageLayout->addWidget(testing);
        pageLayout->addStretch();
        content->setWidget(page);
        bodyLayout->addWidget(content, 1);
        root->addWidget(body, 1);
    }
};

static QString styleSheet()
{
    return QString::fromUtf8(R"QSS(
* {
    font-family: "Segoe UI Variable Text", "Segoe UI", sans-serif;
    font-size: 14px;
    font-weight: 400;
    color: #F1F3F5;
}
QWidget#previewRoot { background: #1B1E23; border: 1px solid #2F3136; }
QWidget#body { background: #1B1E23; }
QWidget#transparent, QFrame#transparent { background: transparent; border: none; }
QFrame#titleBar { background: #1B1E23; border: none; border-bottom: 1px solid #2F3136; }
QFrame#titleBar[flush="true"] { border-bottom: none; }
QFrame#titleBar QToolButton { background: transparent; border: none; }
QFrame#titleBar QToolButton:hover { background: #292D33; }
QFrame#titleBar QToolButton#titleClose:hover { background: #C42B35; }
QFrame#hSeparator, QFrame#vSeparator { color: #2F3136; background: #2F3136; border: none; }
QLabel[role="brand"] { font-size: 18px; font-weight: 700; }
QLabel[role="titleContext"] { font-size: 14px; font-weight: 650; color: #D8DCE1; }
QLabel[role="body"] { font-size: 14px; }
QLabel[role="bodyStrong"] { font-size: 14px; font-weight: 650; }
QLabel[role="muted"] { color: #B8BEC7; font-size: 13px; }
QLabel[role="empty"] { color: #627589; font-style: italic; padding: 8px 0; }
QLabel[role="controlLabel"] { font-size: 14px; font-weight: 550; }
QLabel[role="fieldLabel"] { color: #D0D4DA; font-size: 13px; font-weight: 550; }
QLabel[role="sectionTitle"] { font-size: 15px; font-weight: 700; }
QLabel[role="sidebarTitle"] { font-size: 14px; font-weight: 700; padding: 0 3px; }
QLabel[role="heroTitle"] { font-size: 18px; font-weight: 700; }
QLabel[role="heroIcon"] { font-size: 35px; color: #2f91ff; }
QLabel[role="ruleIcon"] { font-size: 23px; font-weight: 600; }
QLabel[role="routeActionIcon"] { font-size: 22px; }
QLabel[role="routeActionText"] { font-size: 14px; font-weight: 600; }
QLabel[role="conditionKind"] { font-size: 14px; font-weight: 700; }
QLabel[role="conditionValue"] { font-size: 14px; color: #E2E7EC; }
QLabel[role="logic"] { color: #74899b; font-size: 12px; font-weight: 700; letter-spacing: 1px; }
QLabel[role="dragHandle"] { color: #627589; font-size: 17px; }
QLabel[role="statusPrimary"] { font-size: 14px; font-weight: 550; }
QLabel[role="statusIcon"] { color: #9fb2c3; font-size: 25px; }
QLabel[role="trafficUp"] { color: #2ebc75; font-size: 22px; }
QLabel[role="trafficDown"] { color: #35c2f1; font-size: 22px; }
QLabel[role="signal"] { color: #2ebc75; font-size: 19px; letter-spacing: 1px; }
QLabel[tone="blue"] { color: #2f91ff; }
QLabel[tone="cyan"] { color: #35c2f1; }
QLabel[tone="green"] { color: #2ebc75; }
QLabel[tone="red"] { color: #ff4d56; }
QLabel[tone="purple"] { color: #a66cff; }
QLabel[role="pill"] { background: #222529; color: #D4D8DE; border-radius: 5px; padding: 1px 6px; font-size: 12px; font-weight: 650; min-width: 18px; max-height: 20px; }
QLabel[role="pill"][tone="blue"] { background: #237AE9; color: white; }
QLabel[role="pill"][tone="green"] { background: #163f30; color: #57d89a; }
QLabel[role="pill"][tone="amber"] { background: #4b3616; color: #ffc462; }
QLabel[role="pill"][tone="priority"] { background: #1c2b38; color: #a9bccb; border: 1px solid #344759; }
QFrame#commandBar { background: #1B1E23; border: none; border-bottom: 1px solid #2F3136; }
QFrame#statusCard { background: #1B1E23; border: none; border-top: 1px solid #2F3136; }
QFrame#contentCard, QFrame#sidebar, QFrame#rulesContainer {
    background: #171B21; border: 1px solid #2F3136; border-radius: 7px;
}
QFrame#statsCard { background: #171B21; border: 1px solid #2F3136; border-radius: 6px; }
QFrame#ruleSection { background: #171B21; border: 1px solid #2F3136; border-radius: 7px; }
QFrame#orderedRule, QFrame#advancedNotice, QFrame#fallbackCard { background: #171B21; border: 1px solid #2F3136; border-radius: 6px; }
QFrame#orderedRule[opaque="true"] { border-color: #745628; background: #191d20; }
QFrame#conditionPill { background: #222529; border: 1px solid #2F3136; border-radius: 5px; }
QFrame#settingsSection { background: #171B21; border: 1px solid #2F3136; border-radius: 7px; }
QFrame#settingsHeading { background: #1B1E23; border: none; border-bottom: 1px solid #2F3136; }
QFrame#settingsField { background: transparent; border: none; border-bottom: 1px solid #292C31; }
QFrame#fieldBox, QFrame#segmented { background: transparent; border: none; }
QFrame#segmented { background: #171B21; border: 1px solid #2F3136; border-radius: 6px; }
QPushButton, QToolButton, QComboBox, QLineEdit { outline: none; }
QPushButton { background: #222529; border: 1px solid #2F3136; border-radius: 5px; padding: 6px 10px; }
QPushButton:hover, QToolButton:hover { background: #292D33; border-color: #4A4F57; }
QPushButton:pressed { background: #12202c; }
QPushButton[kind="nav"] { background: transparent; border: none; border-radius: 6px; font-size: 14px; font-weight: 550; padding: 7px 9px; }
QPushButton[kind="nav"]:hover { background: #292D33; }
QPushButton[kind="tab"], QPushButton[kind="logTab"] {
    background: transparent; border: none; border-bottom: 2px solid transparent;
    padding: 6px 13px; color: #A4ABB4; font-weight: 500;
}
QPushButton[kind="tab"]:hover, QPushButton[kind="logTab"]:hover { color: #F1F3F5; }
QPushButton[kind="tab"][selected="true"], QPushButton[kind="logTab"][selected="true"] {
    color: #F1F3F5; background: #182530; border-bottom: 2px solid #237AE9;
}
QPushButton[kind="secondary"] { background: #222529; border-color: #2F3136; color: #E1E4E8; padding: 6px 10px; }
QPushButton[kind="link"] { background: transparent; border: none; color: #54a9ff; padding: 3px 6px; }
QPushButton[kind="chip"] { background: #1D2025; border: 1px solid #292D33; border-radius: 5px; padding: 5px 8px; color: #C9CED5; text-align: left; }
QPushButton[kind="chip"][chipKind="app"] { background: #1B2634; border-color: #263C55; }
QPushButton[kind="chip"][chipKind="ruleset"] { background: #222034; border-color: #3C3657; color: #D4CFFF; }
QPushButton[kind="chip"][chipKind="process"] { background: #192923; border-color: #28473A; }
QPushButton[kind="chip"][chipKind="network"] { background: #25231D; border-color: #46402E; }
QPushButton[kind="chip"]:hover { border-color: #2f91ff; }
QPushButton[kind="segment"] { background: transparent; border: none; min-width: 78px; padding: 6px 11px; color: #A4ABB4; }
QPushButton[kind="segment"][selected="true"] { background: #237AE9; border: 1px solid #4193F4; color: #ffffff; }
QPushButton[kind="routeAction"] { background: #222529; border: 1px solid #2F3136; border-radius: 5px; padding: 0; text-align: left; }
QPushButton[kind="routeAction"]:hover { background: #192735; }
QPushButton[kind="routeAction"][selected="true"] { background: #193452; border: 1px solid #237AE9; }
QPushButton[kind="settingsNav"] { background: transparent; border: 1px solid transparent; padding: 9px 11px; text-align: left; }
QPushButton[kind="settingsNav"]:hover { background: #222529; }
QPushButton[kind="settingsNav"][selected="true"] { background: #193452; border-color: #237AE9; color: #ffffff; }
QPushButton#primaryButton { background: #237AE9; border: 1px solid #4193F4; color: white; font-weight: 600; padding: 7px 17px; }
QPushButton#primaryButton:hover { background: #3191ff; }
QPushButton#stopButton { background: #26181d; border: 2px solid #ff4d56; border-radius: 20px; color: #ff4d56; font-size: 15px; padding: 0; }
QToolButton[kind="icon"] { background: transparent; border: 1px solid transparent; border-radius: 5px; color: #B7BDC5; font-size: 15px; padding: 4px; }
QToolButton[kind="icon"][danger="true"]:hover { background: #c42b35; color: white; }
QLineEdit, QComboBox { background: #171B21; border: 1px solid #2F3136; border-radius: 5px; padding: 7px 9px 7px 5px; selection-background-color: #237AE9; }
QLineEdit:focus, QComboBox:focus { border-color: #2f91ff; }
QComboBox::drop-down { border: none; width: 28px; }
QTableWidget#profilesTable { background: #171B21; alternate-background-color: #1A1E24; border: none; border-radius: 6px; gridline-color: #2F3136; selection-background-color: #143C48; selection-color: #ffffff; }
QTableWidget#profilesTable::item { border-bottom: 1px solid #2F3136; padding: 3px 7px; }
QTableWidget#profilesTable::item:selected { border-top: 1px solid #1d7585; border-bottom: 1px solid #1d7585; }
QHeaderView::section { background: #171B21; color: #C2C7CE; border: none; border-right: 1px solid #2F3136; border-bottom: 1px solid #2F3136; padding: 5px 8px; font-weight: 500; }
QHeaderView::section:vertical { color: #8295a6; }
QTableCornerButton::section { background: #171B21; border: none; border-right: 1px solid #2F3136; border-bottom: 1px solid #2F3136; }
QFrame#logTabs { background: #101820; border: none; border-bottom: 1px solid #263544; border-top-left-radius: 9px; border-top-right-radius: 9px; }
QTextEdit#logText { background: #171B21; border: none; padding: 8px 10px; font-family: "Cascadia Mono", "Consolas", monospace; font-size: 13px; selection-background-color: #315d86; }
QScrollArea#rulesScroll { background: transparent; border: none; }
QScrollArea#rulesScroll > QWidget > QWidget { background: #1B1E23; }
QScrollArea#settingsScroll { background: transparent; border: none; }
QScrollArea#settingsScroll > QWidget > QWidget, QWidget#settingsPage { background: #1B1E23; }
QProgressBar { background: #222529; border: 1px solid #2F3136; border-radius: 6px; height: 20px; text-align: center; color: #F1F3F5; }
QProgressBar::chunk { background: #237AE9; border-radius: 5px; }
QScrollBar:vertical { background: transparent; width: 11px; margin: 3px; }
QScrollBar::handle:vertical { background: #344759; border-radius: 4px; min-height: 34px; }
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }
QScrollBar:horizontal { background: #171B21; height: 10px; margin: 2px 3px; }
QScrollBar::handle:horizontal { background: #344759; border-radius: 4px; min-width: 42px; }
QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { width: 0; }
QScrollBar::add-page:horizontal, QScrollBar::sub-page:horizontal { background: transparent; }
QToolTip { background: #1b2834; border: 1px solid #3a5063; color: #edf4fa; padding: 5px; }
)QSS");
}

int main(int argc, char *argv[])
{
    QString requestedScale;
    for (int i = 1; i + 1 < argc; ++i) {
        if (QString::fromLocal8Bit(argv[i]) == "--scale") requestedScale = QString::fromLocal8Bit(argv[i + 1]);
    }
    if (!requestedScale.isEmpty()) qputenv("QT_SCALE_FACTOR", requestedScale.toUtf8());

    QApplication app(argc, argv);
    QApplication::setApplicationName("Throned UI Preview");
    QApplication::setApplicationVersion("1.2-preview");
    app.setStyle("Fusion");
    QFont font("Segoe UI Variable Text", 10);
    font.setStyleStrategy(QFont::PreferAntialias);
    app.setFont(font);

    QCommandLineParser parser;
    parser.setApplicationDescription("Standalone Throned Qt Widgets UI preview");
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addOption({"screen", "Screen to render: main, routes, or settings.", "screen", "main"});
    parser.addOption({"mode", "Routing editor mode: simple or advanced.", "mode", "simple"});
    parser.addOption({"status", "Main status: connected, selected, or url-test.", "status", "connected"});
    parser.addOption({"locale", "Preview locale: en or ru.", "locale", "en"});
    parser.addOption({"theme", "Theme: midnight, graphite, ocean, violet or ember.", "theme", "midnight"});
    parser.addOption({"scale", "Qt scale factor.", "factor", "1.0"});
    parser.addOption({"output", "Save a PNG and exit.", "path"});
    parser.addOption({"width", "Preview width in device-independent pixels.", "pixels"});
    parser.addOption({"height", "Preview height in device-independent pixels.", "pixels"});
    parser.process(app);

    const auto &themes = ThronedPalette::Themes();
    const QString themeKey = QStringLiteral("throned ") + parser.value("theme").trimmed().toLower();
    const ThronedThemeColors colors = themes.value(themeKey, themes.value(QStringLiteral("throned midnight")));
    Palette::adopt(colors);
    QPalette appPalette = app.palette();
    for (auto group : {QPalette::Active, QPalette::Inactive, QPalette::Disabled}) {
        appPalette.setColor(group, QPalette::Window, colors.window);
        appPalette.setColor(group, QPalette::WindowText, colors.text);
        appPalette.setColor(group, QPalette::Base, colors.surface);
        appPalette.setColor(group, QPalette::AlternateBase, colors.surfaceRaised);
        appPalette.setColor(group, QPalette::Text, colors.text);
        appPalette.setColor(group, QPalette::Button, colors.surfaceRaised);
        appPalette.setColor(group, QPalette::ButtonText, colors.text);
        appPalette.setColor(group, QPalette::Highlight, colors.accent);
        appPalette.setColor(group, QPalette::HighlightedText, QColor(Qt::white));
        appPalette.setColor(group, QPalette::PlaceholderText, colors.textSubtle);
    }
    app.setPalette(appPalette);
    app.setStyleSheet(ThronedPalette::Resolve(styleSheet(), colors, 14));

    const bool ru = parser.value("locale").compare("ru", Qt::CaseInsensitive) == 0;
    const bool requestedRoutes = parser.value("screen").compare("routes", Qt::CaseInsensitive) == 0;
    const bool requestedSettings = parser.value("screen").compare("settings", Qt::CaseInsensitive) == 0;
    const bool requestedAdvanced = parser.value("mode").compare("advanced", Qt::CaseInsensitive) == 0;
    const QString requestedStatus = parser.value("status").toLower();
    auto *host = new QStackedWidget;
    host->setObjectName("previewHost");
    host->setWindowTitle(requestedRoutes ? "Throned — Route profile" : requestedSettings ? "Throned — Settings" : "Throned");

    std::function<void(const QString &, bool)> showScreen;
    showScreen = [host, ru, requestedStatus, &showScreen](const QString &screen, bool advanced) {
        QWidget *next = nullptr;
        if (screen == "routes") next = new RoutesPreview(ru, advanced);
        else if (screen == "settings") next = new SettingsPreview(ru);
        else next = new MainPreview(ru, requestedStatus);
        auto *old = host->currentWidget();
        host->addWidget(next);
        host->setCurrentWidget(next);
        if (old) {
            host->removeWidget(old);
            old->deleteLater();
        }
        if (auto *routes = next->findChild<QPushButton *>("routesNav")) {
            QObject::connect(routes, &QPushButton::clicked, next, [&showScreen] { showScreen("routes", false); });
        }
        if (auto *settings = next->findChild<QPushButton *>("settingsNav")) {
            QObject::connect(settings, &QPushButton::clicked, next, [&showScreen] { showScreen("settings", false); });
        }
        if (auto *simple = next->findChild<QPushButton *>("simpleMode")) {
            QObject::connect(simple, &QPushButton::clicked, next, [&showScreen] { showScreen("routes", false); });
        }
        if (auto *advancedButton = next->findChild<QPushButton *>("advancedMode")) {
            QObject::connect(advancedButton, &QPushButton::clicked, next, [&showScreen] { showScreen("routes", true); });
        }
    };
    showScreen(requestedRoutes ? "routes" : requestedSettings ? "settings" : "main", requestedAdvanced);
    QSize previewSize = requestedRoutes || requestedSettings ? QSize(1050, 720) : QSize(1100, 760);
    if (parser.isSet("width")) previewSize.setWidth(parser.value("width").toInt());
    if (parser.isSet("height")) previewSize.setHeight(parser.value("height").toInt());
    host->resize(previewSize);
    host->setMinimumSize(960, 680);
    host->setWindowFlag(Qt::FramelessWindowHint, true);
    host->show();

    if (parser.isSet("output")) {
        const QString output = parser.value("output");
        QTimer::singleShot(700, host, [host, output, &app] {
            const bool ok = host->grab().save(output, "PNG");
            app.exit(ok ? 0 : 2);
        });
    }
    return app.exec();
}
