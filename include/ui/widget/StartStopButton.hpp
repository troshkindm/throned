#pragma once

#include <QToolButton>
#include <QColor>

class QPropertyAnimation;

class StartStopButton : public QToolButton {
    Q_OBJECT
    Q_PROPERTY(qreal morph READ morph WRITE setMorph)
    Q_PROPERTY(qreal spin READ spin WRITE setSpin)
    Q_PROPERTY(qreal dim READ dim WRITE setDim)
    Q_PROPERTY(qreal press READ press WRITE setPress)
    Q_PROPERTY(QColor ringColor READ ringColor WRITE setRingColor)

public:
    enum class State { Disabled, Idle, Connecting, Running, Disconnecting };
    Q_ENUM(State)

    // Mirrors the tray-icon modes computed in MainWindow::refresh_status.

    explicit StartStopButton(QWidget *parent = nullptr);

    void setState(State s);
    State state() const { return m_state; }


    QSize sizeHint() const override;
    QSize minimumSizeHint() const override { return sizeHint(); }

    qreal morph() const { return m_morph; }
    void setMorph(qreal v) { m_morph = v; update(); }
    qreal spin() const { return m_spin; }
    void setSpin(qreal v) { m_spin = v; update(); }
    qreal dim() const { return m_dim; }
    void setDim(qreal v) { m_dim = v; update(); }
    qreal press() const { return m_press; }
    void setPress(qreal v) { m_press = v; update(); }
    QColor ringColor() const { return m_ringColor; }
    void setRingColor(const QColor &c) { m_ringColor = c; update(); }

protected:
    void paintEvent(QPaintEvent *) override;
    void showEvent(QShowEvent *) override;
    void hideEvent(QHideEvent *) override;
    void changeEvent(QEvent *) override;

private:
    void applyState(bool animated);
    void animate(QPropertyAnimation *anim, const QVariant &to, int duration);
    void setLoopRunning(QPropertyAnimation *anim, bool running);
    void updateLoops();

    QColor idleRingColor() const;
    QColor glyphColor() const;
    QColor targetRingColor() const;

    State m_state = State::Idle; // forced to Disabled in the constructor

    qreal m_morph = 0.0; // 0 = play triangle, 1 = stop square
    qreal m_spin = 0.0;
    qreal m_dim = 1.0;
    qreal m_press = 0.0;
    QColor m_ringColor;

    QPropertyAnimation *m_morphAnim = nullptr;
    QPropertyAnimation *m_dimAnim = nullptr;
    QPropertyAnimation *m_pressAnim = nullptr;
    QPropertyAnimation *m_ringColorAnim = nullptr;
    QPropertyAnimation *m_spinAnim = nullptr;

    bool m_shown = false;
};
