#include "include/ui/setting/ThronedStyle.h"

#include <QAbstractItemView>
#include <QAbstractSpinBox>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QGuiApplication>
#include <QLineEdit>
#include <QMenu>
#include <QPainter>
#include <QPainterPath>
#include <QPointer>
#include <QPushButton>
#include <QRadioButton>
#include <QScrollBar>
#include <QSlider>
#include <QStyleOption>
#include <QTabBar>
#include <QToolButton>
#include <QVariantAnimation>

#include "include/ui/setting/ThemeManager.hpp"
#include "include/ui/widget/MaterialIcon.h"

namespace {

constexpr qreal kControlRadius = 7.0;
constexpr qreal kItemRadius = 5.0;
constexpr int kControlHeight = 30;
constexpr int kMenuPadding = 5;
constexpr int kMenuItemHPadding = 10;
constexpr int kMenuIconGap = 9;
constexpr int kScrollBarExtent = 10;

QColor mix(const QColor &a, const QColor &b, qreal amount) {
    const auto channel = [amount](int lhs, int rhs) { return qRound(lhs * (1.0 - amount) + rhs * amount); };
    return {channel(a.red(), b.red()), channel(a.green(), b.green()), channel(a.blue(), b.blue())};
}

// Half-pixel inset keeps a 1px antialiased stroke on whole pixels.
void roundedPanel(QPainter *painter, const QRectF &rect, qreal radius, const QBrush &fill, const QColor &border) {
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    painter->setPen(border.isValid() ? QPen(border, 1) : QPen(Qt::NoPen));
    painter->setBrush(fill);
    painter->drawRoundedRect(border.isValid() ? rect.adjusted(0.5, 0.5, -0.5, -0.5) : rect, radius, radius);
    painter->restore();
}

void drawGlyph(QPainter *painter, const QRect &rect, MaterialIcon::Glyph glyph, const QColor &color, int pixels) {
    const QPixmap pixmap = MaterialIcon::pixmap(glyph, color, pixels);
    painter->drawPixmap(QStyle::alignedRect(Qt::LeftToRight, Qt::AlignCenter,
                                            pixmap.deviceIndependentSize().toSize(), rect),
                        pixmap);
}

bool enabled(const QStyleOption *option) {
    return option->state.testFlag(QStyle::State_Enabled);
}
bool hovered(const QStyleOption *option) {
    return enabled(option) && option->state.testFlag(QStyle::State_MouseOver);
}
bool pressed(const QStyleOption *option) {
    return enabled(option) && option->state.testFlag(QStyle::State_Sunken);
}
bool keyboardFocused(const QStyleOption *option) {
    return option->state.testFlag(QStyle::State_HasFocus) && option->state.testFlag(QStyle::State_KeyboardFocusChange);
}

void drawInputFrame(QPainter *painter, const QStyleOption *option, const ThronedThemeColors &c, const QColor &fill) {
    const bool focus = enabled(option) && option->state.testFlag(QStyle::State_HasFocus);
    const QColor border = focus ? c.accent : hovered(option) ? c.borderStrong
                                                             : c.border;
    roundedPanel(painter, option->rect, kControlRadius, enabled(option) ? fill : mix(fill, c.window, 0.6), border);
}

void drawCheckMark(QPainter *painter, const QRectF &box, const QColor &color) {
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    painter->setPen(QPen(color, 1.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    QPainterPath path;
    path.moveTo(box.left() + box.width() * 0.26, box.top() + box.height() * 0.52);
    path.lineTo(box.left() + box.width() * 0.43, box.top() + box.height() * 0.69);
    path.lineTo(box.left() + box.width() * 0.75, box.top() + box.height() * 0.33);
    painter->drawPath(path);
    painter->restore();
}

void drawCheckBox(QPainter *painter, const QStyleOption *option, const ThronedThemeColors &c) {
    const QRectF box = QRectF(option->rect).adjusted(0.5, 0.5, -0.5, -0.5);
    const bool on = option->state.testFlag(QStyle::State_On);
    const bool partial = option->state.testFlag(QStyle::State_NoChange);
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    if (on || partial) {
        const QColor fill = !enabled(option) ? c.controlInactive : hovered(option) ? c.accentHover
                                                                                   : c.accent;
        painter->setPen(Qt::NoPen);
        painter->setBrush(fill);
        painter->drawRoundedRect(box, 4, 4);
        if (on) {
            drawCheckMark(painter, box, Qt::white);
        } else {
            painter->setPen(QPen(Qt::white, 1.8, Qt::SolidLine, Qt::RoundCap));
            painter->drawLine(QPointF(box.left() + 4.5, box.center().y()), QPointF(box.right() - 4.5, box.center().y()));
        }
    } else {
        painter->setPen(QPen(hovered(option) ? c.accent : c.borderStrong, 1));
        painter->setBrush(enabled(option) ? c.surface : mix(c.surface, c.window, 0.6));
        painter->drawRoundedRect(box, 4, 4);
    }
    painter->restore();
}

void drawRadio(QPainter *painter, const QStyleOption *option, const ThronedThemeColors &c) {
    const QRectF box = QRectF(option->rect).adjusted(0.5, 0.5, -0.5, -0.5);
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    if (option->state.testFlag(QStyle::State_On)) {
        painter->setPen(Qt::NoPen);
        painter->setBrush(!enabled(option) ? c.controlInactive : hovered(option) ? c.accentHover
                                                                                 : c.accent);
        painter->drawEllipse(box);
        painter->setBrush(Qt::white);
        painter->drawEllipse(box.center(), box.width() * 0.19, box.height() * 0.19);
    } else {
        painter->setPen(QPen(hovered(option) ? c.accent : c.borderStrong, 1));
        painter->setBrush(enabled(option) ? c.surface : mix(c.surface, c.window, 0.6));
        painter->drawEllipse(box);
    }
    painter->restore();
}

// One column shared by every row of a menu, so labels line up whether or not a row carries an icon.
int menuIconColumn(const QStyleOptionMenuItem *item) {
    const int column = qMax(item->maxIconWidth, item->menuHasCheckableItems ? 16 : 0);
    return column > 0 ? column + kMenuIconGap : 0;
}

QRect scrollBarSlider(const QStyleOptionSlider *slider) {
    const bool horizontal = slider->orientation == Qt::Horizontal;
    const QRect rect = slider->rect;
    const int length = horizontal ? rect.width() : rect.height();
    const int range = slider->maximum - slider->minimum;
    int sliderLength = length;
    if (range > 0) {
        sliderLength = qBound(qMin(28, length), int((qint64(slider->pageStep) * length) / (qint64(range) + slider->pageStep)), length);
    }
    const int position = QStyle::sliderPositionFromValue(slider->minimum, slider->maximum, slider->sliderPosition,
                                                         length - sliderLength, slider->upsideDown);
    return horizontal ? QRect(rect.left() + position, rect.top(), sliderLength, rect.height())
                      : QRect(rect.left(), rect.top() + position, rect.width(), sliderLength);
}

QColor faded(const QColor &color, qreal amount) {
    QColor result = color;
    result.setAlphaF(color.alphaF() * qBound(0.0, amount, 1.0));
    return result;
}

} // namespace

// Eases hover in and out for controls this style paints; holds nothing while the pointer is still.
class ThronedHoverFader final : public QObject {
public:
    explicit ThronedHoverFader(QObject *parent)
        : QObject(parent), animate_(!QCoreApplication::arguments().contains(QStringLiteral("-ui-preview"))) {}

    void watch(QWidget *widget) {
        if (animate_) widget->installEventFilter(this);
    }

    [[nodiscard]] qreal progress(const QWidget *widget, bool hoveredNow) const {
        const auto it = running_.constFind(widget);
        return it != running_.constEnd() ? it->value : hoveredNow ? 1.0
                                                                  : 0.0;
    }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override {
        if (event->type() == QEvent::Enter || event->type() == QEvent::Leave) {
            auto *widget = qobject_cast<QWidget *>(watched);
            if (widget != nullptr && widget->isEnabled()) start(widget, event->type() == QEvent::Enter ? 1.0 : 0.0);
        }
        return false;
    }

private:
    struct Fade {
        qreal value = 0.0;
        QVariantAnimation *animation = nullptr;
        QMetaObject::Connection destroyed;
    };

    void start(QWidget *widget, qreal target) {
        Fade &fade = running_[widget];
        if (fade.animation == nullptr) {
            fade.value = 1.0 - target;
            fade.animation = new QVariantAnimation(this);
            fade.animation->setEasingCurve(QEasingCurve::OutCubic);
            const QPointer<QWidget> guard(widget);
            connect(fade.animation, &QVariantAnimation::valueChanged, this, [this, widget, guard](const QVariant &value) {
                if (guard == nullptr) return;
                if (const auto it = running_.find(widget); it != running_.end()) it->value = value.toReal();
                guard->update();
            });
            connect(fade.animation, &QVariantAnimation::finished, this, [this, widget] { finish(widget); });
            fade.destroyed = connect(widget, &QObject::destroyed, this, [this, widget] { finish(widget); });
        }
        fade.animation->stop();
        fade.animation->setStartValue(fade.value);
        fade.animation->setEndValue(target);
        fade.animation->setDuration(qMax(1, qRound(130 * qAbs(target - fade.value))));
        fade.animation->start();
    }

    void finish(const QWidget *widget) {
        const auto it = running_.find(widget);
        if (it == running_.end()) return;
        QVariantAnimation *animation = it->animation;
        disconnect(it->destroyed);
        running_.erase(it);
        if (animation != nullptr) {
            animation->disconnect(this);
            animation->stop();
            animation->deleteLater();
        }
    }

    QHash<const QWidget *, Fade> running_;
    bool animate_;
};

const ThronedThemeColors &ThronedStyle::colors() const {
    // ApplyTheme() installs the style before it records the theme, so the cache keys on the recorded name.
    if (const QString theme = themeManager()->current_theme; cachedTheme_ != theme || cachedTheme_.isEmpty()) {
        cached_ = themeManager()->Colors();
        cachedTheme_ = theme;
    }
    return cached_;
}

void ThronedStyle::drawPrimitive(PrimitiveElement element, const QStyleOption *option, QPainter *painter,
                                 const QWidget *widget) const {
    const ThronedThemeColors &c = colors();
    switch (element) {
        case PE_FrameFocusRect:
        case PE_FrameDefaultButton:
        case PE_FrameMenu:
        case PE_FrameTabBarBase:
        case PE_FrameStatusBarItem:
        case PE_FrameLineEdit:
        case PE_PanelScrollAreaCorner:
            return;
        case PE_IndicatorArrowDown:
        case PE_IndicatorArrowUp:
        case PE_IndicatorArrowRight: {
            if (option->rect.width() < 7 || option->rect.height() < 7) break;
            const QColor color = !enabled(option) ? c.textSubtle : hovered(option) ? c.text
                                                                                   : c.textMuted;
            // The glyph carries padding inside its box, so it is drawn larger than the slot or it reads as a speck.
            const int side = qBound(10, qMin(option->rect.width(), option->rect.height()) + 3, 18);
            drawGlyph(painter, option->rect,
                      element == PE_IndicatorArrowDown ? MaterialIcon::Glyph::ChevronDown
                      : element == PE_IndicatorArrowUp ? MaterialIcon::Glyph::ChevronUp
                                                       : MaterialIcon::Glyph::ChevronRight,
                      color, side);
            return;
        }
        case PE_IndicatorHeaderArrow: {
            const auto *header = qstyleoption_cast<const QStyleOptionHeader *>(option);
            if (header == nullptr || header->sortIndicator == QStyleOptionHeader::None) return;
            drawGlyph(painter, option->rect,
                      header->sortIndicator == QStyleOptionHeader::SortUp ? MaterialIcon::Glyph::ChevronUp
                                                                          : MaterialIcon::Glyph::ChevronDown,
                      c.textMuted, 16);
            return;
        }
        case PE_IndicatorBranch: {
            if (!option->state.testFlag(State_Children)) return;
            drawGlyph(painter, option->rect,
                      option->state.testFlag(State_Open) ? MaterialIcon::Glyph::ChevronDown
                                                         : MaterialIcon::Glyph::ChevronRight,
                      hovered(option) ? c.text : c.textMuted, 16);
            return;
        }
        case PE_PanelButtonCommand: {
            const auto *button = qstyleoption_cast<const QStyleOptionButton *>(option);
            const bool flat = button != nullptr && button->features.testFlag(QStyleOptionButton::Flat);
            const bool primary = button != nullptr && button->features.testFlag(QStyleOptionButton::DefaultButton) && enabled(option);
            const bool checked = option->state.testFlag(State_On);
            QColor fill = c.surfaceRaised;
            QColor border = c.border;
            if (primary) {
                const qreal hover = fader_ != nullptr ? fader_->progress(widget, hovered(option)) : hovered(option) ? 1.0
                                                                                                                    : 0.0;
                fill = pressed(option) ? mix(c.accent, c.window, 0.18) : mix(c.accent, c.accentHover, hover);
                border = fill;
            } else if (!enabled(option)) {
                fill = mix(c.surfaceRaised, c.window, 0.55);
                border = mix(c.border, c.window, 0.45);
            } else if (pressed(option)) {
                fill = c.accentSoft;
                border = c.accent;
            } else if (checked) {
                fill = c.selection;
                border = c.selectionBorder;
            } else {
                const qreal hover = fader_ != nullptr ? fader_->progress(widget, hovered(option)) : hovered(option) ? 1.0
                                                                                                                    : 0.0;
                if (flat && hover <= 0.0) return;
                fill = flat ? faded(c.surfaceHover, hover) : mix(c.surfaceRaised, c.surfaceHover, hover);
                border = flat ? faded(c.borderStrong, hover) : mix(c.border, c.borderStrong, hover);
            }
            roundedPanel(painter, option->rect, kControlRadius, fill, border);
            if (keyboardFocused(option)) {
                roundedPanel(painter, QRectF(option->rect).adjusted(2, 2, -2, -2), kControlRadius - 2, Qt::NoBrush,
                             primary ? QColor(255, 255, 255, 170) : c.accent);
            }
            return;
        }
        case PE_PanelButtonTool:
        case PE_PanelButtonBevel: {
            const bool autoRaise = option->state.testFlag(State_AutoRaise);
            const bool checked = option->state.testFlag(State_On);
            if (pressed(option)) {
                roundedPanel(painter, option->rect, kControlRadius - 1, c.accentSoft, autoRaise ? QColor() : c.accent);
            } else if (checked) {
                roundedPanel(painter, option->rect, kControlRadius - 1, c.selection, c.selectionBorder);
            } else if (!enabled(option)) {
                if (!autoRaise) roundedPanel(painter, option->rect, kControlRadius - 1, mix(c.surfaceRaised, c.window, 0.55), c.border);
            } else {
                const qreal hover = fader_ != nullptr ? fader_->progress(widget, hovered(option)) : hovered(option) ? 1.0
                                                                                                                    : 0.0;
                if (!autoRaise) {
                    roundedPanel(painter, option->rect, kControlRadius - 1, mix(c.surfaceRaised, c.surfaceHover, hover),
                                 mix(c.border, c.borderStrong, hover));
                } else if (hover > 0.0) {
                    roundedPanel(painter, option->rect, kControlRadius - 1, faded(c.surfaceHover, hover), QColor());
                }
            }
            if (keyboardFocused(option)) {
                roundedPanel(painter, QRectF(option->rect).adjusted(1, 1, -1, -1), kControlRadius - 2, Qt::NoBrush, c.accent);
            }
            return;
        }
        case PE_PanelLineEdit: {
            const auto *frame = qstyleoption_cast<const QStyleOptionFrame *>(option);
            // A frameless editor sits inside a combo or spin box that already painted the field.
            if (frame != nullptr && frame->lineWidth <= 0) {
                if (widget == nullptr || (qobject_cast<QComboBox *>(widget->parentWidget()) == nullptr &&
                                          qobject_cast<QAbstractSpinBox *>(widget->parentWidget()) == nullptr)) {
                    painter->fillRect(option->rect, option->palette.brush(QPalette::Base));
                }
                return;
            }
            drawInputFrame(painter, option, c, c.surface);
            return;
        }
        case PE_IndicatorCheckBox:
        case PE_IndicatorItemViewItemCheck:
            drawCheckBox(painter, option, c);
            return;
        case PE_IndicatorRadioButton:
            drawRadio(painter, option, c);
            return;
        case PE_IndicatorMenuCheckMark:
            drawCheckMark(painter, option->rect, enabled(option) ? c.accent : c.textSubtle);
            return;
        case PE_FrameGroupBox:
            roundedPanel(painter, option->rect, kControlRadius + 2, mix(c.window, c.surfaceRaised, 0.32), c.border);
            return;
        case PE_Frame: {
            const auto *frame = qstyleoption_cast<const QStyleOptionFrame *>(option);
            if (frame != nullptr && frame->lineWidth <= 0) return;
            roundedPanel(painter, option->rect, 4, Qt::NoBrush, c.border);
            return;
        }
        case PE_FrameTabWidget: {
            painter->save();
            painter->setPen(QPen(c.border, 1));
            painter->drawLine(option->rect.topLeft(), option->rect.topRight());
            painter->restore();
            return;
        }
        case PE_PanelMenu: {
            const bool rounded = widget != nullptr && widget->testAttribute(Qt::WA_TranslucentBackground);
            roundedPanel(painter, option->rect, rounded ? kControlRadius + 2 : 0, c.surfaceRaised, c.borderStrong);
            return;
        }
        case PE_PanelTipLabel:
            roundedPanel(painter, option->rect, 0, c.surfaceRaised, c.borderStrong);
            return;
        case PE_PanelItemViewItem: {
            const auto *item = qstyleoption_cast<const QStyleOptionViewItem *>(option);
            if (item == nullptr) break;
            if (item->backgroundBrush.style() != Qt::NoBrush) painter->fillRect(option->rect, item->backgroundBrush);
            const bool selected = option->state.testFlag(State_Selected);
            if (!selected && !hovered(option)) return;
            const QColor fill = selected ? c.selection : c.surfaceHover;
            // A row is one pill: only its outer cells get rounded ends.
            const auto position = item->viewItemPosition;
            const bool roundLeft = position == QStyleOptionViewItem::Beginning || position == QStyleOptionViewItem::OnlyOne ||
                                   position == QStyleOptionViewItem::Invalid;
            const bool roundRight = position == QStyleOptionViewItem::End || position == QStyleOptionViewItem::OnlyOne ||
                                    position == QStyleOptionViewItem::Invalid;
            painter->save();
            painter->setRenderHint(QPainter::Antialiasing);
            painter->setPen(Qt::NoPen);
            painter->setBrush(fill);
            const QRectF rect = QRectF(option->rect).adjusted(roundLeft ? 2 : 0, 1, roundRight ? -2 : 0, -1);
            painter->setClipRect(rect);
            painter->drawRoundedRect(rect.adjusted(roundLeft ? 0 : -kItemRadius, 0, roundRight ? 0 : kItemRadius, 0),
                                     kItemRadius, kItemRadius);
            painter->restore();
            return;
        }
        default:
            break;
    }
    QProxyStyle::drawPrimitive(element, option, painter, widget);
}

void ThronedStyle::drawControl(ControlElement element, const QStyleOption *option, QPainter *painter,
                               const QWidget *widget) const {
    const ThronedThemeColors &c = colors();
    switch (element) {
        case CE_PushButtonLabel: {
            const auto *button = qstyleoption_cast<const QStyleOptionButton *>(option);
            if (button == nullptr || !enabled(option) || !button->features.testFlag(QStyleOptionButton::DefaultButton)) break;
            QStyleOptionButton primary(*button);
            primary.palette.setColor(QPalette::ButtonText, Qt::white);
            QProxyStyle::drawControl(element, &primary, painter, widget);
            return;
        }
        case CE_MenuEmptyArea:
        case CE_SizeGrip:
        case CE_Splitter:
        case CE_ToolBar:
            return;
        case CE_MenuItem: {
            const auto *item = qstyleoption_cast<const QStyleOptionMenuItem *>(option);
            if (item == nullptr) break;
            const QRect rect = item->rect;
            painter->save();
            if (item->menuItemType == QStyleOptionMenuItem::Separator) {
                if (item->text.isEmpty()) {
                    painter->setPen(QPen(c.border, 1));
                    painter->drawLine(rect.left() + 6, rect.center().y(), rect.right() - 6, rect.center().y());
                } else {
                    QFont font = item->font;
                    font.setPointSizeF(font.pointSizeF() * 0.88);
                    painter->setFont(font);
                    painter->setPen(c.textSubtle);
                    painter->drawText(rect.adjusted(kMenuItemHPadding, 0, -kMenuItemHPadding, 0),
                                      Qt::AlignVCenter | Qt::AlignLeft | Qt::TextSingleLine, item->text);
                }
                painter->restore();
                return;
            }
            const bool active = enabled(option) && option->state.testFlag(State_Selected);
            if (active) roundedPanel(painter, rect, kItemRadius, c.surfaceHover, QColor());

            const int iconColumn = menuIconColumn(item);
            const QRect iconRect(rect.left() + kMenuItemHPadding, rect.top(), qMax(0, iconColumn - kMenuIconGap), rect.height());
            const bool checkable = item->checkType != QStyleOptionMenuItem::NotCheckable;
            if (!item->icon.isNull()) {
                const int side = pixelMetric(PM_SmallIconSize, option, widget);
                const QIcon::Mode mode = !enabled(option) ? QIcon::Disabled : active ? QIcon::Active
                                                                                     : QIcon::Normal;
                const QRect target = alignedRect(Qt::LeftToRight, Qt::AlignCenter, QSize(side, side), iconRect);
                if (checkable && item->checked) {
                    roundedPanel(painter, QRectF(target).adjusted(-3, -3, 3, 3), 4, c.selection, c.selectionBorder);
                }
                item->icon.paint(painter, target, Qt::AlignCenter, mode, item->checked ? QIcon::On : QIcon::Off);
            } else if (checkable && item->checked) {
                const QRect target = alignedRect(Qt::LeftToRight, Qt::AlignCenter, QSize(16, 16), iconRect);
                if (item->checkType == QStyleOptionMenuItem::Exclusive) {
                    painter->setRenderHint(QPainter::Antialiasing);
                    painter->setPen(Qt::NoPen);
                    painter->setBrush(enabled(option) ? c.accent : c.textSubtle);
                    painter->drawEllipse(QRectF(target).center(), 3.5, 3.5);
                } else {
                    drawCheckMark(painter, target, enabled(option) ? c.accent : c.textSubtle);
                }
            }

            const bool hasArrow = item->menuItemType == QStyleOptionMenuItem::SubMenu;
            QRect textRect = rect.adjusted(kMenuItemHPadding + iconColumn, 0, -kMenuItemHPadding - (hasArrow ? 16 : 0), 0);
            QFont font = item->font;
            if (item->menuItemType == QStyleOptionMenuItem::DefaultItem) font.setBold(true);
            painter->setFont(font);
            int flags = Qt::AlignVCenter | Qt::TextSingleLine | Qt::TextShowMnemonic;
            if (styleHint(SH_UnderlineShortcut, option, widget, nullptr) == 0) flags |= Qt::TextHideMnemonic;
            const qsizetype tab = item->text.indexOf(QLatin1Char('\t'));
            painter->setPen(enabled(option) ? c.text : c.textSubtle);
            painter->drawText(textRect, flags | Qt::AlignLeft, tab >= 0 ? item->text.left(tab) : item->text);
            if (tab >= 0) {
                painter->setPen(c.textSubtle);
                painter->drawText(textRect, flags | Qt::AlignRight, item->text.mid(tab + 1));
            }
            if (hasArrow) {
                drawGlyph(painter, QRect(rect.right() - kMenuItemHPadding - 12, rect.top(), 16, rect.height()),
                          MaterialIcon::Glyph::ChevronRight, enabled(option) ? c.textMuted : c.textSubtle, 16);
            }
            painter->restore();
            return;
        }
        case CE_ProgressBarGroove:
            roundedPanel(painter, option->rect, qMin<qreal>(5, option->rect.height() / 2.0), c.surfaceRaised, c.border);
            return;
        case CE_ProgressBarContents: {
            const auto *bar = qstyleoption_cast<const QStyleOptionProgressBar *>(option);
            if (bar == nullptr || bar->maximum <= bar->minimum) break;
            const qreal ratio = qBound(0.0, qreal(bar->progress - bar->minimum) / (bar->maximum - bar->minimum), 1.0);
            QRectF chunk = QRectF(bar->rect).adjusted(1, 1, -1, -1);
            const qreal width = chunk.width() * ratio;
            if (width < 1) return;
            if (bar->invertedAppearance != (bar->direction == Qt::RightToLeft)) chunk.setLeft(chunk.right() - width);
            chunk.setWidth(width);
            roundedPanel(painter, chunk, qMin<qreal>(4, chunk.height() / 2.0), enabled(option) ? c.accent : c.controlInactive, QColor());
            return;
        }
        case CE_TabBarTabShape: {
            const auto *tab = qstyleoption_cast<const QStyleOptionTab *>(option);
            if (tab == nullptr) break;
            const bool selected = option->state.testFlag(State_Selected);
            const bool vertical = tab->shape == QTabBar::RoundedWest || tab->shape == QTabBar::RoundedEast ||
                                  tab->shape == QTabBar::TriangularWest || tab->shape == QTabBar::TriangularEast;
            const QRectF rect = QRectF(option->rect).adjusted(2, 3, -2, -3);
            if (!selected && hovered(option)) roundedPanel(painter, rect, kItemRadius, c.surfaceHover, QColor());
            if (selected) {
                roundedPanel(painter, rect, kItemRadius, c.surfaceRaised, QColor());
                const QRectF mark = vertical ? QRectF(rect.left(), rect.top() + 6, 3, rect.height() - 12)
                                             : QRectF(rect.left() + 9, rect.bottom() - 2, rect.width() - 18, 2.5);
                roundedPanel(painter, mark, 1.25, enabled(option) ? c.accent : c.controlInactive, QColor());
            }
            return;
        }
        case CE_TabBarTabLabel: {
            const auto *tab = qstyleoption_cast<const QStyleOptionTab *>(option);
            if (tab == nullptr) break;
            QStyleOptionTab label(*tab);
            const QColor text = !enabled(option) ? c.textSubtle : option->state.testFlag(State_Selected) || hovered(option) ? c.text
                                                                                                                            : c.textMuted;
            label.palette.setColor(QPalette::WindowText, text);
            label.palette.setColor(QPalette::ButtonText, text);
            label.state &= ~State_HasFocus;
            QProxyStyle::drawControl(element, &label, painter, widget);
            return;
        }
        case CE_HeaderSection:
        case CE_HeaderEmptyArea: {
            painter->save();
            if (element == CE_HeaderSection && (pressed(option) || hovered(option))) {
                painter->fillRect(option->rect, pressed(option) ? c.surfaceHover : mix(c.surface, c.surfaceHover, 0.6));
            }
            painter->setPen(QPen(c.border, 1));
            const auto *header = qstyleoption_cast<const QStyleOptionHeader *>(option);
            if (header != nullptr && header->orientation == Qt::Vertical) {
                painter->drawLine(option->rect.topRight(), option->rect.bottomRight());
            } else {
                painter->drawLine(option->rect.bottomLeft(), option->rect.bottomRight());
            }
            painter->restore();
            return;
        }
        case CE_HeaderLabel: {
            const auto *header = qstyleoption_cast<const QStyleOptionHeader *>(option);
            if (header == nullptr) break;
            QStyleOptionHeader label(*header);
            label.palette.setColor(QPalette::ButtonText, enabled(option) ? c.textMuted : c.textSubtle);
            QProxyStyle::drawControl(element, &label, painter, widget);
            return;
        }
        case CE_ShapedFrame: {
            const auto *frame = qstyleoption_cast<const QStyleOptionFrame *>(option);
            if (frame == nullptr || (frame->frameShape != QFrame::HLine && frame->frameShape != QFrame::VLine)) break;
            painter->save();
            painter->setPen(QPen(c.border, 1));
            const QPoint center = option->rect.center();
            if (frame->frameShape == QFrame::HLine) {
                painter->drawLine(option->rect.left(), center.y(), option->rect.right(), center.y());
            } else {
                painter->drawLine(center.x(), option->rect.top(), center.x(), option->rect.bottom());
            }
            painter->restore();
            return;
        }
        default:
            break;
    }
    QProxyStyle::drawControl(element, option, painter, widget);
}

void ThronedStyle::drawComplexControl(ComplexControl control, const QStyleOptionComplex *option, QPainter *painter,
                                      const QWidget *widget) const {
    const ThronedThemeColors &c = colors();
    switch (control) {
        case CC_ComboBox: {
            const auto *combo = qstyleoption_cast<const QStyleOptionComboBox *>(option);
            if (combo == nullptr) break;
            if (combo->frame) {
                QStyleOptionComboBox field(*combo);
                // An open popup keeps the field lit; the sunken state would otherwise read as a stuck press.
                if (!combo->editable && !field.state.testFlag(State_On)) field.state &= ~State_HasFocus;
                drawInputFrame(painter, &field, c,
                               combo->editable ? c.surface : hovered(option) ? c.surfaceHover
                                                                             : c.surfaceRaised);
                if (!combo->editable && keyboardFocused(option)) {
                    roundedPanel(painter, QRectF(option->rect).adjusted(2, 2, -2, -2), kControlRadius - 2, Qt::NoBrush, c.accent);
                }
            }
            if (combo->subControls.testFlag(SC_ComboBoxArrow)) {
                const QRect arrow = subControlRect(CC_ComboBox, option, SC_ComboBoxArrow, widget);
                drawGlyph(painter, arrow, MaterialIcon::Glyph::ChevronDown,
                          !enabled(option) ? c.textSubtle : hovered(option) ? c.text
                                                                            : c.textMuted,
                          16);
            }
            return;
        }
        case CC_SpinBox: {
            const auto *spin = qstyleoption_cast<const QStyleOptionSpinBox *>(option);
            if (spin == nullptr) break;
            if (spin->frame) drawInputFrame(painter, option, c, c.surface);
            if (spin->buttonSymbols == QAbstractSpinBox::NoButtons) return;
            for (const SubControl sub: {SC_SpinBoxUp, SC_SpinBoxDown}) {
                if (!spin->subControls.testFlag(sub)) continue;
                const QRect rect = subControlRect(CC_SpinBox, option, sub, widget);
                const bool stepEnabled = enabled(option) && spin->stepEnabled.testFlag(sub == SC_SpinBoxUp ? QAbstractSpinBox::StepUpEnabled
                                                                                                           : QAbstractSpinBox::StepDownEnabled);
                const bool active = stepEnabled && spin->activeSubControls == sub;
                if (active) roundedPanel(painter, QRectF(rect).adjusted(1, 1, -1, -1), 3, c.surfaceHover, QColor());
                drawGlyph(painter, rect, sub == SC_SpinBoxUp ? MaterialIcon::Glyph::ChevronUp : MaterialIcon::Glyph::ChevronDown,
                          !stepEnabled ? c.textSubtle : active ? c.text
                                                               : c.textMuted,
                          14);
            }
            return;
        }
        case CC_ScrollBar: {
            const auto *slider = qstyleoption_cast<const QStyleOptionSlider *>(option);
            if (slider == nullptr) break;
            if (slider->maximum <= slider->minimum) return;
            const bool active = enabled(option) && (slider->activeSubControls.testFlag(SC_ScrollBarSlider) || pressed(option));
            const bool horizontal = slider->orientation == Qt::Horizontal;
            const QRectF handle = QRectF(scrollBarSlider(slider)).adjusted(horizontal ? 1 : 2.5, horizontal ? 2.5 : 1, horizontal ? -1 : -2.5, horizontal ? -2.5 : -1);
            roundedPanel(painter, handle, qMin(handle.width(), handle.height()) / 2.0, active ? c.scrollBarHover : c.scrollBar, QColor());
            return;
        }
        case CC_Slider: {
            const auto *slider = qstyleoption_cast<const QStyleOptionSlider *>(option);
            if (slider == nullptr) break;
            const QRect handle = subControlRect(CC_Slider, option, SC_SliderHandle, widget);
            const bool horizontal = slider->orientation == Qt::Horizontal;
            const QRectF bounds = option->rect;
            const QRectF groove = horizontal ? QRectF(bounds.left() + 2, bounds.center().y() - 1.5, bounds.width() - 4, 4)
                                             : QRectF(bounds.center().x() - 1.5, bounds.top() + 2, 4, bounds.height() - 4);
            roundedPanel(painter, groove, 2, c.controlInactive, QColor());
            QRectF filled = groove;
            const bool fromEnd = horizontal ? slider->upsideDown != (slider->direction == Qt::RightToLeft) : !slider->upsideDown;
            if (horizontal) {
                fromEnd ? filled.setLeft(handle.center().x()) : filled.setRight(handle.center().x());
            } else {
                fromEnd ? filled.setTop(handle.center().y()) : filled.setBottom(handle.center().y());
            }
            roundedPanel(painter, filled, 2, enabled(option) ? c.accent : c.textSubtle, QColor());
            painter->save();
            painter->setRenderHint(QPainter::Antialiasing);
            painter->setPen(QPen(enabled(option) ? c.accent : c.textSubtle, 2));
            painter->setBrush(slider->activeSubControls.testFlag(SC_SliderHandle) ? c.accentHover : c.text);
            painter->drawEllipse(QRectF(handle).center(), 6, 6);
            painter->restore();
            return;
        }
        case CC_GroupBox: {
            const auto *group = qstyleoption_cast<const QStyleOptionGroupBox *>(option);
            if (group == nullptr) break;
            QStyleOptionGroupBox titled(*group);
            titled.textColor = enabled(option) ? c.textMuted : c.textSubtle;
            QProxyStyle::drawComplexControl(control, &titled, painter, widget);
            return;
        }
        default:
            break;
    }
    QProxyStyle::drawComplexControl(control, option, painter, widget);
}

QRect ThronedStyle::subControlRect(ComplexControl control, const QStyleOptionComplex *option, SubControl subControl,
                                   const QWidget *widget) const {
    if (control == CC_ScrollBar) {
        const auto *slider = qstyleoption_cast<const QStyleOptionSlider *>(option);
        if (slider == nullptr) return QProxyStyle::subControlRect(control, option, subControl, widget);
        const QRect handle = scrollBarSlider(slider);
        const QRect rect = slider->rect;
        const bool horizontal = slider->orientation == Qt::Horizontal;
        switch (subControl) {
            case SC_ScrollBarGroove:
                return rect;
            case SC_ScrollBarSlider:
                return handle;
            case SC_ScrollBarSubPage:
                return horizontal ? QRect(rect.left(), rect.top(), handle.left() - rect.left(), rect.height())
                                  : QRect(rect.left(), rect.top(), rect.width(), handle.top() - rect.top());
            case SC_ScrollBarAddPage:
                return horizontal ? QRect(handle.right() + 1, rect.top(), rect.right() - handle.right(), rect.height())
                                  : QRect(rect.left(), handle.bottom() + 1, rect.width(), rect.bottom() - handle.bottom());
            default:
                return {};
        }
    }
    if (control == CC_GroupBox) {
        const auto *group = qstyleoption_cast<const QStyleOptionGroupBox *>(option);
        if (group == nullptr) return QProxyStyle::subControlRect(control, option, subControl, widget);
        // The title sits above the card, never across its border.
        const bool checkable = group->subControls.testFlag(SC_GroupBoxCheckBox);
        const bool titled = checkable || !group->text.isEmpty();
        const int titleHeight = titled ? qMax(group->fontMetrics.height(), 16) + 8 : 0;
        const int indicator = checkable ? pixelMetric(PM_IndicatorWidth, option, widget) : 0;
        const QRect rect = group->rect;
        const QRect frame = rect.adjusted(0, titleHeight, 0, 0);
        QRect result;
        switch (subControl) {
            case SC_GroupBoxFrame:
                return frame;
            case SC_GroupBoxContents:
                return frame.adjusted(3, 3, -3, -3);
            case SC_GroupBoxCheckBox:
                result = QRect(rect.left() + 2, rect.top() + (titleHeight - 2 - indicator) / 2, indicator, indicator);
                break;
            case SC_GroupBoxLabel:
                result = QRect(rect.left() + 2 + (checkable ? indicator + 7 : 0), rect.top(),
                               group->fontMetrics.horizontalAdvance(group->text) + 4, titleHeight - 2);
                break;
            default:
                return QProxyStyle::subControlRect(control, option, subControl, widget);
        }
        return visualRect(group->direction, rect, result);
    }
    return QProxyStyle::subControlRect(control, option, subControl, widget);
}

QSize ThronedStyle::sizeFromContents(ContentsType type, const QStyleOption *option, const QSize &contentsSize,
                                     const QWidget *widget) const {
    if (type == CT_MenuItem) {
        const auto *item = qstyleoption_cast<const QStyleOptionMenuItem *>(option);
        if (item != nullptr) {
            if (item->menuItemType == QStyleOptionMenuItem::Separator) {
                return {contentsSize.width(), item->text.isEmpty() ? 9 : item->fontMetrics.height() + 8};
            }
            const int arrow = item->menuItemType == QStyleOptionMenuItem::SubMenu ? 14 : 0;
            const int shortcutGap = item->text.contains(QLatin1Char('\t')) ? 18 : 0;
            return {contentsSize.width() + kMenuItemHPadding * 2 + menuIconColumn(item) + arrow + shortcutGap + 14,
                    qMax(kControlHeight, item->fontMetrics.height() + 12)};
        }
    }
    QSize size = QProxyStyle::sizeFromContents(type, option, contentsSize, widget);
    switch (type) {
        case CT_PushButton: {
            const auto *button = qstyleoption_cast<const QStyleOptionButton *>(option);
            const bool labelled = button != nullptr && !button->text.isEmpty();
            size.setHeight(qMax(size.height(), kControlHeight));
            if (labelled) size.setWidth(qMax(size.width() + 10, 76));
            break;
        }
        case CT_LineEdit:
        case CT_SpinBox:
            size.setHeight(qMax(size.height(), kControlHeight));
            break;
        case CT_ComboBox:
            size.setHeight(qMax(size.height(), kControlHeight));
            size.rwidth() += 6;
            break;
        case CT_CheckBox:
        case CT_RadioButton:
            size.setHeight(qMax(size.height(), 22));
            break;
        default:
            break;
    }
    return size;
}

int ThronedStyle::pixelMetric(PixelMetric metric, const QStyleOption *option, const QWidget *widget) const {
    switch (metric) {
        // QTabBar sizes its scrollers from this metric, not from their size hint, so a stylesheet width cannot hide them.
        case PM_TabBarScrollButtonWidth:
            if (widget != nullptr && widget->inherits("GroupTabBar")) return 0;
            break;
        case PM_ScrollBarExtent:
            return kScrollBarExtent;
        case PM_ScrollBarSliderMin:
            return 28;
        case PM_MenuPanelWidth:
            return kMenuPadding;
        case PM_MenuHMargin:
        case PM_MenuVMargin:
        case PM_ButtonDefaultIndicator:
        case PM_TabBarBaseOverlap:
        case PM_TabBarTabOverlap:
        case PM_TabBarTabShiftHorizontal:
        case PM_TabBarTabShiftVertical:
        case PM_TabBarBaseHeight:
            return 0;
        case PM_SubMenuOverlap:
            return -3;
        case PM_IndicatorWidth:
        case PM_IndicatorHeight:
        case PM_ExclusiveIndicatorWidth:
        case PM_ExclusiveIndicatorHeight:
            return 16;
        case PM_CheckBoxLabelSpacing:
        case PM_RadioButtonLabelSpacing:
            return 8;
        case PM_TabBarTabHSpace:
            return 26;
        case PM_TabBarTabVSpace:
            return 16;
        case PM_ToolTipLabelFrameWidth:
            return 6;
        case PM_SliderLength:
            return 16;
        case PM_SliderThickness:
        case PM_SliderControlThickness:
            return 18;
        default:
            break;
    }
    return QProxyStyle::pixelMetric(metric, option, widget);
}

int ThronedStyle::styleHint(StyleHint hint, const QStyleOption *option, const QWidget *widget,
                            QStyleHintReturn *returnData) const {
    switch (hint) {
        case SH_EtchDisabledText:
        case SH_DitherDisabledText:
        case SH_DialogButtonBox_ButtonsHaveIcons:
        case SH_UnderlineShortcut:
        case SH_ComboBox_Popup:
        case SH_ScrollBar_Transient:
            return 0;
        case SH_Menu_MouseTracking:
        case SH_ItemView_ShowDecorationSelected:
        case SH_ScrollBar_LeftClickAbsolutePosition:
            return 1;
        case SH_ToolTipLabel_Opacity:
            return 255;
        case SH_Menu_SubMenuPopupDelay:
            return 120;
        case SH_GroupBox_TextLabelColor:
            return int(colors().textMuted.rgba());
        case SH_Table_GridLineColor:
            return int(colors().border.rgba());
        default:
            break;
    }
    return QProxyStyle::styleHint(hint, option, widget, returnData);
}

void ThronedStyle::polish(QWidget *widget) {
    QProxyStyle::polish(widget);
    if (qobject_cast<QPushButton *>(widget) != nullptr || qobject_cast<QToolButton *>(widget) != nullptr ||
        qobject_cast<QComboBox *>(widget) != nullptr || qobject_cast<QAbstractSpinBox *>(widget) != nullptr ||
        qobject_cast<QCheckBox *>(widget) != nullptr || qobject_cast<QRadioButton *>(widget) != nullptr ||
        qobject_cast<QLineEdit *>(widget) != nullptr || qobject_cast<QScrollBar *>(widget) != nullptr ||
        qobject_cast<QTabBar *>(widget) != nullptr || qobject_cast<QSlider *>(widget) != nullptr) {
        widget->setAttribute(Qt::WA_Hover);
    }
    if (qobject_cast<QPushButton *>(widget) != nullptr || qobject_cast<QToolButton *>(widget) != nullptr) {
        if (fader_ == nullptr) fader_ = new ThronedHoverFader(this);
        fader_->watch(widget);
    }
    if (auto *view = qobject_cast<QAbstractItemView *>(widget); view != nullptr && view->viewport() != nullptr) {
        view->viewport()->setAttribute(Qt::WA_Hover);
    }
    // Rounded corners need a translucent popup; a bare X11 server without a compositor would paint them black.
    if (qobject_cast<QMenu *>(widget) != nullptr && QGuiApplication::platformName() != QLatin1String("xcb") &&
        !widget->testAttribute(Qt::WA_TranslucentBackground)) {
        widget->setAttribute(Qt::WA_TranslucentBackground);
        widget->setWindowFlags(widget->windowFlags() | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint);
    }
}
