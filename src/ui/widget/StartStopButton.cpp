#include "include/ui/widget/StartStopButton.hpp"

#include <QEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPropertyAnimation>

StartStopButton::StartStopButton(QWidget *parent) : QToolButton(parent) {
    setFocusPolicy(Qt::NoFocus);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    m_ringColor = idleRingColor();

    m_morphAnim = new QPropertyAnimation(this, "morph", this);
    m_dimAnim = new QPropertyAnimation(this, "dim", this);
    m_pressAnim = new QPropertyAnimation(this, "press", this);
    m_ringColorAnim = new QPropertyAnimation(this, "ringColor", this);

    m_spinAnim = new QPropertyAnimation(this, "spin", this);
    m_spinAnim->setStartValue(0.0);
    m_spinAnim->setEndValue(360.0);
    m_spinAnim->setDuration(900);
    m_spinAnim->setLoopCount(-1);
    m_spinAnim->setEasingCurve(QEasingCurve::Linear);

    connect(this, &QAbstractButton::pressed, this, [this] { animate(m_pressAnim, 1.0, 110); });
    connect(this, &QAbstractButton::released, this, [this] { animate(m_pressAnim, 0.0, 160); });

    m_state = State::Disabled;
    applyState(false);
}

QSize StartStopButton::sizeHint() const {
    return {40, 40};
}

void StartStopButton::setState(State s) {
    if (s == m_state) return;
    m_state = s;
    applyState(true);
}


void StartStopButton::applyState(bool animated) {
    const bool interactive = (m_state == State::Idle || m_state == State::Running);
    setEnabled(interactive);
    setCursor(interactive ? Qt::PointingHandCursor : Qt::ArrowCursor);

    switch (m_state) {
        case State::Disabled: setToolTip(tr("Select a profile to start")); break;
        case State::Idle: setToolTip(tr("Start")); break;
        case State::Connecting: setToolTip(tr("Connecting…")); break;
        case State::Running: setToolTip(tr("Stop")); break;
        case State::Disconnecting: setToolTip(tr("Stopping…")); break;
    }

    const qreal morphTarget = (m_state == State::Running || m_state == State::Disconnecting) ? 1.0 : 0.0;
    const qreal dimTarget = (m_state == State::Disabled) ? 0.58 : 1.0;
    const QColor ringTarget = targetRingColor();

    if (animated) {
        animate(m_morphAnim, morphTarget, 210);
        animate(m_dimAnim, dimTarget, 220);
        animate(m_ringColorAnim, ringTarget, 220);
    } else {
        m_morphAnim->stop();
        m_dimAnim->stop();
        m_ringColorAnim->stop();
        m_morph = morphTarget;
        m_dim = dimTarget;
        m_ringColor = ringTarget;
    }

    updateLoops();
    update();
}

void StartStopButton::animate(QPropertyAnimation *anim, const QVariant &to, int duration) {
    anim->stop();
    anim->setDuration(duration);
    anim->setEasingCurve(QEasingCurve::InOutCubic);
    anim->setStartValue(property(anim->propertyName().constData()));
    anim->setEndValue(to);
    anim->start();
}

void StartStopButton::setLoopRunning(QPropertyAnimation *anim, bool running) {
    if (running) {
        if (anim->state() != QAbstractAnimation::Running) anim->start();
        return;
    }
    anim->stop();
    if (anim == m_spinAnim) m_spin = 0.0;
    update();
}


void StartStopButton::updateLoops() {
    const bool spinning = m_state == State::Connecting || m_state == State::Disconnecting;
    setLoopRunning(m_spinAnim, m_shown && spinning);
}

void StartStopButton::showEvent(QShowEvent *e) {
    QToolButton::showEvent(e);
    m_shown = true;
    updateLoops();
}

void StartStopButton::hideEvent(QHideEvent *e) {
    m_shown = false;
    setLoopRunning(m_spinAnim, false);
    QToolButton::hideEvent(e);
}

void StartStopButton::changeEvent(QEvent *e) {
    QToolButton::changeEvent(e);
    update();
}

QColor StartStopButton::idleRingColor() const {
    return QColor(QStringLiteral("#35D07F"));
}

QColor StartStopButton::glyphColor() const {
    switch (m_state) {
    case State::Running:
    case State::Disconnecting: return QColor(QStringLiteral("#FF9AA5"));
    case State::Connecting: return QColor(QStringLiteral("#FFD37A"));
    case State::Disabled: return palette().color(QPalette::Disabled, QPalette::WindowText);
    case State::Idle: return QColor(QStringLiteral("#78E7AA"));
    }
    return QColor(QStringLiteral("#78E7AA"));
}

QColor StartStopButton::targetRingColor() const {
    switch (m_state) {
        case State::Connecting:
        case State::Disconnecting: return QColor(QStringLiteral("#F2B84B"));
        case State::Running: return QColor(QStringLiteral("#F05B67"));
        case State::Disabled: return QColor(QStringLiteral("#59616C"));
        case State::Idle: return idleRingColor();
    }
    return idleRingColor();
}

void StartStopButton::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const QRectF cr = contentsRect();
    const qreal D = qMin(cr.width(), cr.height());
    const QPointF c = cr.center();

    const qreal scale = 1.0 - 0.06 * m_press;
    p.translate(c);
    p.scale(scale, scale);
    p.translate(-c);

    p.setOpacity(m_dim);

    // A compact rounded command replaces the platform/toolbutton chrome used by
    // Throne. Colour is now semantic and stable: green starts, red stops, amber
    // means a transition. A short hover halo gives feedback without pulsing.
    const QRectF shell(c.x() - D * 0.41, c.y() - D * 0.41, D * 0.82, D * 0.82);
    // The spinner and the shell are both circles, so the command reads as one disc.
    if (underMouse() && isEnabled()) {
        QColor halo = m_ringColor;
        halo.setAlpha(36);
        p.setPen(Qt::NoPen);
        p.setBrush(halo);
        p.drawEllipse(shell.adjusted(-2.0, -2.0, 2.0, 2.0));
    }
    QColor fill = m_ringColor;
    fill.setAlpha(m_state == State::Disabled ? 18 : (underMouse() ? 48 : 34));
    QColor border = m_ringColor;
    border.setAlpha(m_state == State::Disabled ? 75 : 175);
    p.setPen(QPen(border, 1.25));
    p.setBrush(fill);
    p.drawEllipse(shell.adjusted(0.65, 0.65, -0.65, -0.65));

    if (m_state == State::Connecting || m_state == State::Disconnecting) {
        const qreal spinnerR = D * 0.27;
        const QRectF spinner(c.x() - spinnerR, c.y() - spinnerR,
                             spinnerR * 2.0, spinnerR * 2.0);
        QColor track = m_ringColor;
        track.setAlpha(45);
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(track, 1.8));
        p.drawEllipse(spinner);
        QPen arcPen(m_ringColor, 1.8);
        arcPen.setCapStyle(Qt::RoundCap);
        p.setPen(arcPen);
        p.drawArc(spinner, static_cast<int>(-m_spin * 16), -105 * 16);
    }

    const qreal h = D * 0.136;
    const qreal t = m_morph;
    auto lerp = [](const QPointF &a, const QPointF &b, qreal k) { return a + (b - a) * k; };
    // Offset to the triangle's centroid; a bbox-centred triangle reads as too far left.
    const qreal triShift = h / 3.0;
    const QPointF tri[4] = {
        c + QPointF(-h + triShift, -h),
        c + QPointF(h + triShift, 0),
        c + QPointF(h + triShift, 0),
        c + QPointF(-h + triShift, h),
    };
    const QPointF sq[4] = {
        c + QPointF(-h, -h),
        c + QPointF(h, -h),
        c + QPointF(h, h),
        c + QPointF(-h, h),
    };
    QPainterPath path;
    path.moveTo(lerp(tri[0], sq[0], t));
    for (int i = 1; i < 4; ++i) path.lineTo(lerp(tri[i], sq[i], t));
    path.closeSubpath();

    // The glyph's rounded corners come from the round-joined pen, not from the path.
    const QColor glyph = glyphColor();
    const qreal corner = D * 0.04;
    QPen gpen(glyph, corner);
    gpen.setJoinStyle(Qt::RoundJoin);
    gpen.setCapStyle(Qt::RoundCap);
    const qreal glyphAlpha = (m_state == State::Connecting || m_state == State::Disconnecting) ? 0.5 : 1.0;
    p.setOpacity(m_dim * glyphAlpha);
    p.setPen(gpen);
    p.setBrush(glyph);
    p.drawPath(path);
}
