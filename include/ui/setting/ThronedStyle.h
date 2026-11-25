#pragma once

#include <QProxyStyle>

#include "include/ui/setting/ThronedPalette.hpp"

class ThronedHoverFader;

// Paints every standard control from the theme tokens, so a dialog with no stylesheet matches the shell.
class ThronedStyle final : public QProxyStyle {
public:
    using QProxyStyle::QProxyStyle;

    void drawPrimitive(PrimitiveElement element, const QStyleOption *option, QPainter *painter,
                       const QWidget *widget) const override;
    void drawControl(ControlElement element, const QStyleOption *option, QPainter *painter,
                     const QWidget *widget) const override;
    void drawComplexControl(ComplexControl control, const QStyleOptionComplex *option, QPainter *painter,
                            const QWidget *widget) const override;
    QRect subControlRect(ComplexControl control, const QStyleOptionComplex *option, SubControl subControl,
                         const QWidget *widget) const override;
    QSize sizeFromContents(ContentsType type, const QStyleOption *option, const QSize &contentsSize,
                           const QWidget *widget) const override;
    int pixelMetric(PixelMetric metric, const QStyleOption *option, const QWidget *widget) const override;
    int styleHint(StyleHint hint, const QStyleOption *option, const QWidget *widget,
                  QStyleHintReturn *returnData) const override;
    void polish(QWidget *widget) override;

private:
    const ThronedThemeColors &colors() const;

    ThronedHoverFader *fader_ = nullptr;
    mutable QString cachedTheme_;
    mutable ThronedThemeColors cached_;
};
