#include "include/ui/widget/ThronedTitleBar.h"
#include "NkrVersion.h"

#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPolygonF>
#include <QStyle>
#include <QStyleOption>
#include <QWindow>

namespace {
constexpr int CaptionButtonWidth = 46;
constexpr int TitleBarHeight = 48;
} // namespace

ThronedLogoMark::ThronedLogoMark(QWidget *parent) : QWidget(parent) {
    setFixedSize(32, 26);
}

void ThronedLogoMark::paintEvent(QPaintEvent *) {
    // Kept pixel-for-pixel with tools/ui-demo/main.cpp::LogoMark.
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
    painter.fillPath(crown, QColor("#ff4d56"));

    QPainterPath shade;
    shade.moveTo(r.left(), r.top() + 2);
    shade.lineTo(r.center().x(), r.bottom());
    shade.lineTo(r.left() + 5, r.bottom());
    shade.quadTo(r.left(), r.bottom(), r.left(), r.bottom() - 5);
    shade.closeSubpath();
    painter.fillPath(shade, QColor(0, 0, 0, 42));
}

ThronedCaptionButton::ThronedCaptionButton(Glyph glyph, QWidget *parent)
    : QToolButton(parent), glyph_(glyph) {
    setFocusPolicy(Qt::NoFocus);
    setAttribute(Qt::WA_Hover, true);
    setFixedSize(CaptionButtonWidth, TitleBarHeight);
}

void ThronedCaptionButton::setGlyph(Glyph glyph) {
    if (glyph_ == glyph) return;
    glyph_ = glyph;
    update();
}

void ThronedCaptionButton::paintEvent(QPaintEvent *) {
    QStyleOption option;
    option.initFrom(this);
    QPainter painter(this);
    // Let the stylesheet paint the hover/pressed fill, then stroke the glyph.
    style()->drawPrimitive(QStyle::PE_Widget, &option, &painter, this);

    const QColor tint = glyph_ == Glyph::Close && underMouse()
                            ? QColor(Qt::white)
                            : palette().color(QPalette::ButtonText);
    QPen pen(tint);
    pen.setWidthF(1.1);
    painter.setPen(pen);

    // Half-pixel centres keep the horizontal and vertical strokes on the pixel
    // grid; only the diagonals of the close glyph need antialiasing.
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

ThronedTitleBar::ThronedTitleBar(const QString &context, QWidget *parent) : QFrame(parent) {
    setObjectName(QStringLiteral("titleBar"));
    setFixedHeight(TitleBarHeight);
    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(14, 0, 0, 0);
    layout->setSpacing(10);
    layout->addWidget(new ThronedLogoMark(this));
    auto *brand = new QLabel(QStringLiteral("Throned"), this);
    brand->setObjectName(QStringLiteral("titleBrand"));
    layout->addWidget(brand);
    // A source build with no -DINPUT_VERSION would otherwise show a bare "v".
    if (context.isEmpty() && !QStringLiteral(NKR_VERSION).isEmpty()) {
        auto *version = new QLabel(QStringLiteral("v%1").arg(QStringLiteral(NKR_VERSION)), this);
        version->setObjectName(QStringLiteral("titleVersion"));
        version->setToolTip(QStringLiteral("Throned %1").arg(QStringLiteral(NKR_VERSION)));
        layout->addWidget(version);
    }
    if (!context.isEmpty()) {
        auto *divider = new QFrame(this);
        divider->setObjectName(QStringLiteral("vSeparator"));
        divider->setFixedSize(1, 27);
        layout->addWidget(divider);
        auto *contextLabel = new QLabel(context, this);
        contextLabel->setObjectName(QStringLiteral("titleContext"));
        layout->addWidget(contextLabel);
    }
    layout->addStretch(1);

    auto makeButton = [this](ThronedCaptionButton::Glyph glyph, const QString &name) {
        auto *button = new ThronedCaptionButton(glyph, this);
        button->setObjectName(name);
        return button;
    };
    minimize_ = makeButton(ThronedCaptionButton::Glyph::Minimize, QStringLiteral("titleMinimize"));
    maximize_ = makeButton(ThronedCaptionButton::Glyph::Maximize, QStringLiteral("titleMaximize"));
    close_ = makeButton(ThronedCaptionButton::Glyph::Close, QStringLiteral("titleClose"));
    layout->addWidget(minimize_);
    layout->addWidget(maximize_);
    layout->addWidget(close_);
    connect(minimize_, &QToolButton::clicked, this, [this] { window()->showMinimized(); });
    connect(maximize_, &QToolButton::clicked, this, [this] {
        window()->isMaximized() ? window()->showNormal() : window()->showMaximized();
    });
    connect(close_, &QToolButton::clicked, this, [this] { window()->close(); });
    window()->installEventFilter(this);
}

bool ThronedTitleBar::eventFilter(QObject *watched, QEvent *event) {
    if (maximize_ && watched == window() && event->type() == QEvent::WindowStateChange) {
        maximize_->setGlyph(window()->isMaximized() ? ThronedCaptionButton::Glyph::Restore
                                                    : ThronedCaptionButton::Glyph::Maximize);
    }
    return QFrame::eventFilter(watched, event);
}

void ThronedTitleBar::mousePressEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton && window()->windowHandle()) {
        window()->windowHandle()->startSystemMove();
        event->accept();
        return;
    }
    QFrame::mousePressEvent(event);
}

void ThronedTitleBar::mouseDoubleClickEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton) {
        window()->isMaximized() ? window()->showNormal() : window()->showMaximized();
        event->accept();
        return;
    }
    QFrame::mouseDoubleClickEvent(event);
}
