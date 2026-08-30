#pragma once

#include <QColor>
#include <QList>
#include <QMap>
#include <QPair>
#include <QString>
#include <QStringList>

// Semantic color tokens every redesigned screen is built from.
//
// The ramps are deliberately near-neutral: only the accent family carries real
// chroma. A tinted background ramp makes text edges read as soft because the
// eye focuses short and long wavelengths on different planes, so a uniformly
// blue (or violet, or warm) window at low luminance contrast literally looks
// out of focus. Keeping window/surface/border almost gray and spending the
// chroma budget on accents keeps the same theme identity while the type stays
// crisp.
struct ThronedThemeColors {
    QColor window;          // page background
    QColor surface;         // recessed wells: cards, tables, log
    QColor surfaceRaised;   // buttons, inputs, chips
    QColor surfaceHover;
    QColor border;          // hairlines between planes
    QColor borderStrong;    // hover/focus borders
    QColor text;
    QColor textMuted;
    QColor textSubtle;
    QColor accent;
    QColor accentHover;
    QColor accentSoft;      // accent-tinted background
    QColor selection;
    QColor selectionBorder;
    QColor success;
    QColor warning;         // degraded but working: connecting, slow latency, quota low
    QColor danger;          // destructive: close the window, drop a connection
    QColor dangerSoft;      // its tinted hover ground
    QColor controlInactive;
    QColor scrollBar;
    QColor scrollBarHover;
    // 0 flat, 1 full 2009-era gloss: a vertical sheen on the round command button.
    qreal gloss = 0.0;
    bool dark = true;
};

// A skin is a palette plus the things a palette cannot say: its own stylesheet
// fragment, a font, and a folder of icons. Loaded from disk, so adding one is a
// matter of dropping a folder in rather than rebuilding.
struct ThronedSkin {
    QString id;             // folder name
    QString name;           // shown in the theme list
    ThronedThemeColors colors;
    QString styleOverlay;   // appended after the resolved base sheet, so it wins
    QString fontFamily;     // empty leaves the application font alone
    QString iconDir;        // absolute; empty falls back to the built-in glyphs
};

namespace ThronedPalette {

// Token name -> member, so a skin file can set any subset and inherit the rest.
inline const QMap<QString, QColor ThronedThemeColors::*> &ColorFields() {
    static const QMap<QString, QColor ThronedThemeColors::*> fields{
        {QStringLiteral("window"), &ThronedThemeColors::window},
        {QStringLiteral("surface"), &ThronedThemeColors::surface},
        {QStringLiteral("surfaceRaised"), &ThronedThemeColors::surfaceRaised},
        {QStringLiteral("surfaceHover"), &ThronedThemeColors::surfaceHover},
        {QStringLiteral("border"), &ThronedThemeColors::border},
        {QStringLiteral("borderStrong"), &ThronedThemeColors::borderStrong},
        {QStringLiteral("text"), &ThronedThemeColors::text},
        {QStringLiteral("textMuted"), &ThronedThemeColors::textMuted},
        {QStringLiteral("textSubtle"), &ThronedThemeColors::textSubtle},
        {QStringLiteral("accent"), &ThronedThemeColors::accent},
        {QStringLiteral("accentHover"), &ThronedThemeColors::accentHover},
        {QStringLiteral("accentSoft"), &ThronedThemeColors::accentSoft},
        {QStringLiteral("selection"), &ThronedThemeColors::selection},
        {QStringLiteral("selectionBorder"), &ThronedThemeColors::selectionBorder},
        {QStringLiteral("success"), &ThronedThemeColors::success},
        {QStringLiteral("warning"), &ThronedThemeColors::warning},
        {QStringLiteral("danger"), &ThronedThemeColors::danger},
        {QStringLiteral("dangerSoft"), &ThronedThemeColors::dangerSoft},
        {QStringLiteral("controlInactive"), &ThronedThemeColors::controlInactive},
        {QStringLiteral("scrollBar"), &ThronedThemeColors::scrollBar},
        {QStringLiteral("scrollBarHover"), &ThronedThemeColors::scrollBarHover},
    };
    return fields;
}


// Lightness ladder shared by every theme, so panels separate by the same amount
// whichever theme is active:
//   surface (-7) < window < surfaceRaised (+8) < surfaceHover (+9) < border (+7)
inline const QMap<QString, ThronedThemeColors> &Themes() {
    static const QMap<QString, ThronedThemeColors> themes{
        // Cool neutral ground, blue accent. The default.
        {QStringLiteral("throned midnight"), {
            .window = "#1A1C20", .surface = "#131519", .surfaceRaised = "#22252A",
            .surfaceHover = "#2B2F35", .border = "#33363D", .borderStrong = "#4B4F58",
            .text = "#F2F3F5", .textMuted = "#ADB1B8", .textSubtle = "#7C8089",
            .accent = "#3B82F6", .accentHover = "#5C99FF", .accentSoft = "#16243A",
            .selection = "#1B3253", .selectionBorder = "#3D6DA8", .success = "#3ECF8E",
            .warning = "#D9A441", .danger = "#D2434E", .dangerSoft = "#3A2227",
            .controlInactive = "#3B3F47", .scrollBar = "#454951", .scrollBarHover = "#5A5F69",
            .dark = true,
        }},
        // Pure neutral ground, indigo accent.
        {QStringLiteral("throned graphite"), {
            .window = "#1C1D1F", .surface = "#151518", .surfaceRaised = "#242528",
            .surfaceHover = "#2D2E32", .border = "#35363A", .borderStrong = "#4C4E54",
            .text = "#F3F3F4", .textMuted = "#B0B1B5", .textSubtle = "#7F8085",
            .accent = "#6366F1", .accentHover = "#7E80F6", .accentSoft = "#22233C",
            .selection = "#292A46", .selectionBorder = "#4F51B8", .success = "#3ECF8E",
            .warning = "#D9A441", .danger = "#D2434E", .dangerSoft = "#3B2326",
            .controlInactive = "#3C3D42", .scrollBar = "#47484E", .scrollBarHover = "#5C5D64",
            .dark = true,
        }},
        // Neutral ground with a cool cast, cyan accent.
        {QStringLiteral("throned ocean"), {
            .window = "#191D1F", .surface = "#121618", .surfaceRaised = "#202629",
            .surfaceHover = "#2A3134", .border = "#32393C", .borderStrong = "#495155",
            .text = "#F1F4F5", .textMuted = "#ACB3B6", .textSubtle = "#7B8386",
            .accent = "#12B5CB", .accentHover = "#2FCBE0", .accentSoft = "#0E2E36",
            .selection = "#113B47", .selectionBorder = "#1E7E93", .success = "#3ECF8E",
            .warning = "#D6A63F", .danger = "#D14550", .dangerSoft = "#382225",
            .controlInactive = "#394144", .scrollBar = "#444C50", .scrollBarHover = "#596266",
            .dark = true,
        }},
        // Neutral ground with a violet cast, purple accent.
        {QStringLiteral("throned violet"), {
            .window = "#1D1B20", .surface = "#161419", .surfaceRaised = "#26232B",
            .surfaceHover = "#302C36", .border = "#38343E", .borderStrong = "#4F4A58",
            .text = "#F4F2F6", .textMuted = "#B2AEB9", .textSubtle = "#817D89",
            .accent = "#A277FF", .accentHover = "#B593FF", .accentSoft = "#2A2140",
            .selection = "#332954", .selectionBorder = "#6E56B8", .success = "#3ECF8E",
            .warning = "#DCA84A", .danger = "#D6444F", .dangerSoft = "#3D242C",
            .controlInactive = "#3F3A47", .scrollBar = "#4A4553", .scrollBarHover = "#5F5A69",
            .dark = true,
        }},
        // Neutral ground with a warm cast, coral accent.
        {QStringLiteral("throned ember"), {
            .window = "#201C1B", .surface = "#181514", .surfaceRaised = "#2A2523",
            .surfaceHover = "#342E2C", .border = "#3C3634", .borderStrong = "#544C49",
            .text = "#F6F3F2", .textMuted = "#B7B0AE", .textSubtle = "#86807D",
            .accent = "#F2555F", .accentHover = "#FF7480", .accentSoft = "#3B1F22",
            .selection = "#45242A", .selectionBorder = "#A34450", .success = "#3ECF8E",
            .warning = "#E3A83C", .danger = "#E04B4B", .dangerSoft = "#452627",
            .controlInactive = "#443C3A", .scrollBar = "#504846", .scrollBarHover = "#665D5A",
            .dark = true,
        }},
    };
    return themes;
}

inline QStringList ThemeNames() {
    return {
        QStringLiteral("Throned Midnight"),
        QStringLiteral("Throned Graphite"),
        QStringLiteral("Throned Ocean"),
        QStringLiteral("Throned Violet"),
        QStringLiteral("Throned Ember"),
    };
}

// The stylesheet templates are authored with literal hex values that act as
// token names. Resolve swaps each one for the active theme's color, so a
// template can be read as ordinary CSS while still following the theme.
inline QString Resolve(const QString &styleSheetTemplate, const ThronedThemeColors &colors, int baseFontPx) {
    QString result = styleSheetTemplate;
    result.replace(QStringLiteral("%BASE_FONT_PX%"), QString::number(baseFontPx));

    static const QStringList windowTokens{"#1B1E23"};
    static const QStringList surfaceTokens{"#171B21", "#14181E"};
    static const QStringList surfaceRaisedTokens{"#222529", "#22272E", "#252B33", "#272C33", "#2B3037",
                                                 "#1B222A", "#20252B", "#22262C"};
    static const QStringList surfaceHoverTokens{"#292D33", "#292E35", "#24282D", "#2D333B"};
    static const QStringList borderTokens{"#2F3136", "#25292F", "#343C46", "#3A4048", "#343941",
                                          "#3A414A", "#3A424C"};
    static const QStringList borderStrongTokens{"#4A4F57", "#4A535E", "#45505C",
                                                "#3D444D", "#3E454F", "#525C68"};
    static const QStringList textTokens{"#F1F3F5", "#DDE2E7", "#D8DCE1", "#EDF4FA", "#F2FFF9",
                                        "#DDE7F0", "#E5E8EB", "#E1E4E8", "#F7F9FA", "#E7EAED"};
    static const QStringList textMutedTokens{"#A4ABB4", "#AEB7C2", "#9FB2C3", "#C2C7CE", "#B8BEC7"};
    static const QStringList textSubtleTokens{"#8295A6", "#747C86", "#617181", "#747B85"};
    static const QStringList accentTokens{"#237AE9", "#2F91FF", "#3187F3"};
    static const QStringList accentHoverTokens{"#3B8BF0", "#4193F4", "#2F86F1", "#4DA3FF", "#7BBAFF"};
    static const QStringList accentSoftTokens{"#193452", "#263B55", "#193E69", "#182530", "#182631",
                                              "#14242D", "#1B2634", "#263C55", "#182B38", "#222A33"};
    static const QStringList selectionTokens{"#143C48"};
    static const QStringList selectionBorderTokens{"#1D7585", "#2E749A"};
    static const QStringList successTokens{"#2EBC75"};
    static const QStringList controlInactiveTokens{"#34414D"};
    static const QStringList scrollBarTokens{"#344759", "#3A424D", "#3B4C5E"};
    static const QStringList scrollBarHoverTokens{"#4B5663", "#4B6076"};
    static const QStringList warningTokens{"#D9A441", "#E8A33D"};
    static const QStringList dangerTokens{"#C42B35"};
    static const QStringList dangerSoftTokens{"#3A2227"};

    const QList<QPair<const QStringList *, const QColor *>> groups{
        {&windowTokens, &colors.window},
        {&surfaceTokens, &colors.surface},
        {&surfaceRaisedTokens, &colors.surfaceRaised},
        {&surfaceHoverTokens, &colors.surfaceHover},
        {&borderTokens, &colors.border},
        {&borderStrongTokens, &colors.borderStrong},
        {&textTokens, &colors.text},
        {&textMutedTokens, &colors.textMuted},
        {&textSubtleTokens, &colors.textSubtle},
        {&accentTokens, &colors.accent},
        {&accentHoverTokens, &colors.accentHover},
        {&accentSoftTokens, &colors.accentSoft},
        {&selectionTokens, &colors.selection},
        {&selectionBorderTokens, &colors.selectionBorder},
        {&successTokens, &colors.success},
        {&warningTokens, &colors.warning},
        {&dangerTokens, &colors.danger},
        {&dangerSoftTokens, &colors.dangerSoft},
        {&controlInactiveTokens, &colors.controlInactive},
        {&scrollBarTokens, &colors.scrollBar},
        {&scrollBarHoverTokens, &colors.scrollBarHover},
    };
    for (const auto &[tokens, color] : groups) {
        const QString value = color->name(QColor::HexRgb).toUpper();
        for (const QString &token : *tokens) result.replace(token, value, Qt::CaseInsensitive);
    }
    return result;
}

} // namespace ThronedPalette
