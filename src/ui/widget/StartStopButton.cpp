#include "include/ui/widget/StartStopButton.hpp"

#include "include/ui/setting/ThemeManager.hpp"

#include <QConicalGradient>
#include <QContextMenuEvent>
#include <QEvent>
#include <QPainter>
#include <QLinearGradient>
#include <QPainterPath>
#include <QPropertyAnimation>

#include "include/sys/KillSwitch.hpp"

StartStopButton::StartStopButton(QWidget *parent) : QToolButton(parent) {
    setFocusPolicy(Qt::NoFocus);
    connect(themeManager(), &ThemeManager::themeChanged, this, [this] {
        m_ringColor = targetRingColor();
        update();
    });
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

void StartStopButton::setMode(Mode m) {
    if (m == m_mode) return;
    m_mode = m;
    if (m_state == State::Running) animate(m_ringColorAnim, targetRingColor(), 320);
}

void StartStopButton::setLock(Lock l) {
    if (l == m_lock) return;
    m_lock = l;
    updateToolTip();
    update();
}

void StartStopButton::updateToolTip() {
    QString tip;
    switch (m_state) {
        case State::Disabled:
            tip = tr("Select a profile to start");
            break;
        case State::Idle:
            tip = tr("Start");
            break;
        case State::Connecting:
            tip = tr("Connecting…");
            break;
        case State::Running:
            tip = tr("Stop");
            break;
        case State::Disconnecting:
            tip = tr("Stopping…");
            break;
    }
    switch (m_lock) {
        case Lock::Hidden:
            break;
        case Lock::Pending:
            tip += "\n" + tr("Kill switch: starting…");
            break;
        case Lock::Blocking:
            tip += "\n" + tr("Kill switch: Tun is not running, so most traffic is blocked");
            break;
        case Lock::Passing:
            tip += "\n" + tr("Kill switch: on — traffic only flows through Throne's Tun");
            break;
        case Lock::Fault:
            tip += "\n" + (Sys::KillSwitch::instance()->failedForIdentity() ? tr("Kill switch is blocking traffic") : tr("Kill switch is not active"));
            break;
    }
    setToolTip(tip);
}

void StartStopButton::applyState(bool animated) {
    const bool interactive = (m_state == State::Idle || m_state == State::Running);
    setEnabled(interactive);
    setCursor(interactive ? Qt::PointingHandCursor : Qt::ArrowCursor);

    updateToolTip();

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

bool StartStopButton::event(QEvent *e) {
    // QWidget::event drops a disabled widget's context menu before consulting the policy, and the button is disabled when nothing can start.
    if (e->type() == QEvent::ContextMenu && !isEnabled() && contextMenuPolicy() == Qt::CustomContextMenu) {
        emit customContextMenuRequested(static_cast<QContextMenuEvent *>(e)->pos());
        return true;
    }
    // The failure kind behind a Fault can change while the lock stays Fault.
    if (e->type() == QEvent::ToolTip && m_lock == Lock::Fault) updateToolTip();
    return QToolButton::event(e);
}

void StartStopButton::changeEvent(QEvent *e) {
    switch (e->type()) {
        case QEvent::StyleChange:
        case QEvent::PaletteChange:
        case QEvent::ThemeChange:
            // The cached chrome was rendered through the old style/palette.
            m_lockCache = QPixmap();
            break;
        default:
            break;
    }
    QToolButton::changeEvent(e);
    update();
}

// Semantic, and taken from the theme rather than the three ad-hoc greens and reds
// this button used to carry: green starts, red stops, amber is a transition.
QColor StartStopButton::idleRingColor() const {
    return themeManager()->Colors().success;
}

QColor StartStopButton::glyphColor() const {
    // The glyph is the ring lifted off its own colour, so a new theme only has to
    // supply the ring and this follows.
    switch (m_state) {
        case State::Disabled:
            return palette().color(QPalette::Disabled, QPalette::WindowText);
        default:
            return targetRingColor().lighter(themeManager()->Colors().dark ? 140 : 115);
    }
}

QColor StartStopButton::targetRingColor() const {
    const auto colors = themeManager()->Colors();
    switch (m_state) {
        case State::Connecting:
        case State::Disconnecting:
            return colors.warning;
        case State::Running:
            return colors.danger;
        case State::Disabled:
            return colors.controlInactive;
        case State::Idle:
            return idleRingColor();
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
    const qreal gloss = qBound(0.0, themeManager()->Colors().gloss, 1.0);
    QColor border = m_ringColor;
    border.setAlpha(m_state == State::Disabled ? 75 : 175);
    p.setPen(QPen(border, 1.25));
    if (gloss > 0.0) {
        // A solid candy disc with a vertical sheen, the way a 2009 skin wants it,
        // instead of the flat tinted wash the neutral themes use.
        QLinearGradient sheen(shell.topLeft(), shell.bottomLeft());
        sheen.setColorAt(0.0, m_ringColor.lighter(static_cast<int>(100 + 55 * gloss)));
        sheen.setColorAt(0.5, m_ringColor);
        sheen.setColorAt(1.0, m_ringColor.darker(static_cast<int>(100 + 35 * gloss)));
        p.setBrush(sheen);
        p.setOpacity(m_dim * (m_state == State::Disabled ? 0.45 : 1.0));
    } else {
        QColor fill = m_ringColor;
        fill.setAlpha(m_state == State::Disabled ? 18 : (underMouse() ? 48 : 34));
        p.setBrush(fill);
    }
    p.drawEllipse(shell.adjusted(0.65, 0.65, -0.65, -0.65));
    if (gloss > 0.0) {
        // The highlight sits in the top third, which is what reads as glass.
        QRectF highlight = shell.adjusted(shell.width() * 0.16, shell.height() * 0.08,
                                          -shell.width() * 0.16, -shell.height() * 0.55);
        QLinearGradient glassGradient(highlight.topLeft(), highlight.bottomLeft());
        QColor glass(255, 255, 255);
        glass.setAlphaF(0.42 * gloss);
        glassGradient.setColorAt(0.0, glass);
        glass.setAlphaF(0.0);
        glassGradient.setColorAt(1.0, glass);
        p.setPen(Qt::NoPen);
        p.setBrush(glassGradient);
        p.drawEllipse(highlight);
        p.setOpacity(m_dim);
    }

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

    if (m_lock != Lock::Hidden) {
        // Outside the press scale and m_dim: the lock must stay legible on a disabled button.
        p.resetTransform();
        paintLock(p, cr);
    }
}

QRectF StartStopButton::lockBox(const QRectF &area) const {
    const qreal D = qMin(area.width(), area.height());
    const qreal L = D * 0.31;
    const qreal margin = D * 0.01;
    return {area.right() - margin - L, area.bottom() - margin - L, L, L};
}

QColor StartStopButton::lockColor() const {
    const bool dark = palette().color(QPalette::Window).lightness() < 128;
    switch (m_lock) {
        case Lock::Pending: {
            QColor neutral = palette().color(QPalette::WindowText);
            neutral.setAlpha(255);
            return neutral;
        }
        case Lock::Blocking:
            return dark ? QColor(0xFF, 0xC1, 0x3B) : QColor(0xC7, 0x84, 0x00);
        case Lock::Fault:
            return dark ? QColor(0xFF, 0x5C, 0x5C) : QColor(0xD3, 0x2F, 0x2F);
        default:
            return dark ? QColor(0x4A, 0xD6, 0x7E) : QColor(0x1E, 0x8E, 0x45);
    }
}

qreal StartStopButton::lockOpacity() const {
    switch (m_lock) {
        case Lock::Pending:
            return 0.55;
        case Lock::Passing:
            return 0.75;
        default:
            return 1.0;
    }
}

void StartStopButton::paintLock(QPainter &p, const QRectF &area) {
    const QRectF box = lockBox(area);
    const qreal L = box.width();
    const qreal pad = L * 0.34;

    const QColor window = palette().color(QPalette::Window);
    const bool dark = window.lightness() < 128;
    const QColor fill = lockColor();

    const qreal dpr = devicePixelRatioF();
    if (m_lockCache.isNull() || m_lockKeyLock != m_lock || !qFuzzyCompare(m_lockKeySize, L) || !qFuzzyCompare(m_lockKeyDpr, dpr)) {
        const qreal side = L + 2 * pad;
        QPixmap pm(QSizeF(side * dpr, side * dpr).toSize());
        pm.setDevicePixelRatio(dpr);
        pm.fill(Qt::transparent);
        QPainter lp(&pm);
        lp.setRenderHint(QPainter::Antialiasing, true);
        lp.translate(pad, pad);

        const qreal sw = L * 0.14;
        const QRectF body(L * 0.11, L * 0.45, L * 0.78, L * 0.55);
        const qreal r = L * 0.215;
        const qreal cx = L / 2;
        QPainterPath shackle;
        shackle.moveTo(cx - r, body.top() + sw / 2);
        shackle.lineTo(cx - r, sw / 2 + r);
        shackle.arcTo(QRectF(cx - r, sw / 2, 2 * r, 2 * r), 180, -180);
        shackle.lineTo(cx + r, body.top() + sw / 2);
        QPainterPathStroker stroker;
        stroker.setWidth(sw);
        stroker.setCapStyle(Qt::FlatCap);
        stroker.setJoinStyle(Qt::RoundJoin);
        QPainterPath bodyPath;
        bodyPath.addRoundedRect(body, L * 0.12, L * 0.12);
        const QPainterPath outer = stroker.createStroke(shackle).united(bodyPath);

        QPainterPath mark;
        mark.setFillRule(Qt::WindingFill);
        if (m_lock == Lock::Blocking) {
            const qreal w = L * 0.09;
            mark.addRoundedRect(QRectF(cx - w / 2, body.top() + body.height() * 0.16, w, body.height() * 0.44), w / 2, w / 2);
            mark.addEllipse(QPointF(cx, body.top() + body.height() * 0.78), w * 0.62, w * 0.62);
        } else {
            const QPointF hole(cx, body.top() + body.height() * 0.40);
            mark.addEllipse(hole, L * 0.08, L * 0.08);
            mark.addRect(QRectF(cx - L * 0.035, hole.y(), L * 0.07, body.height() * 0.32));
        }

        // A cut-out in the window colour keeps the lock apart from the ring and its glow underneath.
        QColor cutout = window;
        cutout.setAlpha(255);
        lp.setPen(QPen(cutout, qMax(1.0, L * 0.08) * 2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        lp.setBrush(cutout);
        lp.drawPath(outer);

        lp.setPen(Qt::NoPen);
        lp.setBrush(fill);
        lp.drawPath(outer.subtracted(mark));

        if (m_lock == Lock::Fault) {
            lp.setPen(QPen(dark ? QColor(255, 255, 255, 220) : QColor(0x5A, 0x00, 0x00), qMax(1.0, L * 0.06),
                           Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            lp.setBrush(Qt::NoBrush);
            lp.drawPath(outer);
        }
        lp.end();

        m_lockCache = pm;
        m_lockKeyLock = m_lock;
        m_lockKeySize = L;
        m_lockKeyDpr = dpr;
    }

    p.setOpacity(lockOpacity());
    p.drawPixmap(box.topLeft() - QPointF(pad, pad), m_lockCache);
}
