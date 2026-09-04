#include <QDir>
#include <QFont>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStyle>
#include <QApplication>
#include <QFile>
#include <QFont>
#include <QPalette>
#include <QColor>
#include <QMap>
#include <QPainter>
#include <QStyleFactory>
#include <QWidget>

#include "include/global/Configs.hpp"
#include "include/ui/setting/ThemeManager.hpp"

#include <QGlobalStatic>

Q_GLOBAL_STATIC(ThemeManager, themeManagerInstance)

ThemeManager *themeManager() {
    return themeManagerInstance();
}

namespace {

const QString StyleTemplateProperty = QStringLiteral("thronedStyleTemplate");

const QMap<QString, ThronedThemeColors> &thronedThemes() {
    return ThronedPalette::Themes();
}

QColor mixColors(const QColor &a, const QColor &b, qreal amount) {
    const auto mix = [amount](int lhs, int rhs) {
        return qRound(lhs * (1.0 - amount) + rhs * amount);
    };
    return QColor(mix(a.red(), b.red()), mix(a.green(), b.green()), mix(a.blue(), b.blue()));
}

} // namespace

extern QString ReadFileText(const QString &path);

struct ThemeColors {
    QColor window, windowText;
    QColor base, alternateBase;
    QColor text;
    QColor button, buttonText;
    QColor brightText;
    QColor highlight, highlightedText;
    QColor link;            // paints the active/running config row
    QColor tooltipBase, tooltipText;
    QColor placeholder;
    QColor disabledText;
};

static QPalette buildThemePalette(const ThemeColors &c) {
    QPalette p;

    const auto setAll = [&](QPalette::ColorRole role, const QColor &col) {
        p.setColor(QPalette::Active, role, col);
        p.setColor(QPalette::Inactive, role, col);
        p.setColor(QPalette::Disabled, role, col);
    };

    setAll(QPalette::Window,          c.window);
    setAll(QPalette::WindowText,      c.windowText);
    setAll(QPalette::Base,            c.base);
    setAll(QPalette::AlternateBase,   c.alternateBase);
    setAll(QPalette::Text,            c.text);
    setAll(QPalette::Button,          c.button);
    setAll(QPalette::ButtonText,      c.buttonText);
    setAll(QPalette::BrightText,      c.brightText);
    setAll(QPalette::ToolTipBase,     c.tooltipBase);
    setAll(QPalette::ToolTipText,     c.tooltipText);
    setAll(QPalette::Highlight,       c.highlight);
    setAll(QPalette::HighlightedText, c.highlightedText);
    setAll(QPalette::Link,            c.link);
    setAll(QPalette::LinkVisited,     c.link);
    setAll(QPalette::PlaceholderText, c.placeholder);

    // Frames and bevels the stylesheet doesn't cover fall back to Qt's light defaults otherwise.
    setAll(QPalette::Light,    c.button.lighter(130));
    setAll(QPalette::Midlight, c.button.lighter(115));
    setAll(QPalette::Mid,      c.button.darker(130));
    setAll(QPalette::Dark,     c.button.darker(160));
    setAll(QPalette::Shadow,   c.window.darker(180));

    // Must follow setAll(), which wrote the Disabled group too.
    p.setColor(QPalette::Disabled, QPalette::WindowText,      c.disabledText);
    p.setColor(QPalette::Disabled, QPalette::Text,            c.disabledText);
    p.setColor(QPalette::Disabled, QPalette::ButtonText,      c.disabledText);
    p.setColor(QPalette::Disabled, QPalette::HighlightedText, c.disabledText);
    p.setColor(QPalette::Disabled, QPalette::Link,            c.disabledText);

    return p;
}

// Lazy: a QPalette must not be constructed before QApplication exists. The keys also define "custom theme".
static const QMap<QString, QPalette> &customThemePalettes() {
    static const QMap<QString, QPalette> palettes = [] {
        QMap<QString, QPalette> m;

        m["flatgray"] = buildThemePalette({
            .window = "#FFFFFF", .windowText = "#57595B",
            .base = "#FFFFFF", .alternateBase = "#F6F6F6",
            .text = "#57595B",
            .button = "#F2F2F2", .buttonText = "#57595B",
            .brightText = "#FFFFFF",
            .highlight = "#D6D6D6", .highlightedText = "#2D2F31",
            .link = "#2A6CB0",
            .tooltipBase = "#FFFFFF", .tooltipText = "#57595B",
            .placeholder = "#9AA0A6", .disabledText = "#B0B0B0",
        });

        m["lightblue"] = buildThemePalette({
            .window = "#EAF7FF", .windowText = "#386487",
            .base = "#FFFFFF", .alternateBase = "#DAEFFF",
            .text = "#386487",
            .button = "#DEF0FE", .buttonText = "#386487",
            .brightText = "#FFFFFF",
            .highlight = "#C0DCF2", .highlightedText = "#1B3B57",
            .link = "#1D6FB8",
            .tooltipBase = "#EAF7FF", .tooltipText = "#386487",
            .placeholder = "#7F9DB5", .disabledText = "#A6BCCE",
        });

        m["softpink"] = buildThemePalette({
            .window = "#FFF0FB", .windowText = "#883983",
            .base = "#FFFFFF", .alternateBase = "#FBDDF5",
            .text = "#883983",
            .button = "#FCE1F6", .buttonText = "#883983",
            .brightText = "#FFFFFF",
            .highlight = "#F1C1E7", .highlightedText = "#5A2456",
            .link = "#B92BA6",
            .tooltipBase = "#FFF0FB", .tooltipText = "#883983",
            .placeholder = "#C08BBA", .disabledText = "#CBA6C6",
        });

        m["blacksoft"] = buildThemePalette({
            .window = "#444444", .windowText = "#DCDCDC",
            .base = "#444444", .alternateBase = "#525252",
            .text = "#DCDCDC",
            .button = "#484848", .buttonText = "#DCDCDC",
            .brightText = "#FFFFFF",
            .highlight = "#646464", .highlightedText = "#FFFFFF",
            .link = "#5AB0FF",
            .tooltipBase = "#484848", .tooltipText = "#DCDCDC",
            .placeholder = "#9A9A9A", .disabledText = "#808080",
        });

        // Mirrors the bundled darkstyle.qss.
        m["qdarkstyle"] = buildThemePalette({
            .window = "#19232D", .windowText = "#DFE1E2",
            .base = "#19232D", .alternateBase = "#37414F",
            .text = "#DFE1E2",
            .button = "#455364", .buttonText = "#DFE1E2",
            .brightText = "#FFFFFF",
            .highlight = "#346792", .highlightedText = "#DFE1E2",
            .link = "#6FC0FF",
            .tooltipBase = "#346792", .tooltipText = "#DFE1E2",
            .placeholder = "#9DA9B5", .disabledText = "#788D9C",
        });

        return m;
    }();
    return palettes;
}

void ThemeManager::ApplyTheme(const QString &theme, bool force) {
    if (this->system_style_name.isEmpty()) {
        this->system_style_name = qApp->style()->name();
        this->system_palette = qApp->palette();
        this->base_font_family = qApp->font().family();
    }

    // A skin may ask for its own face; leaving one behind would follow the user
    // into every other theme, so this is restored on the way out too.
    if (qApp != nullptr) {
        const ThronedSkin *skin = Skin(theme);
        const QString family = skin != nullptr && !skin->fontFamily.isEmpty()
            ? skin->fontFamily : base_font_family;
        if (!family.isEmpty() && qApp->font().family() != family) {
            QFont font = qApp->font();
            font.setFamily(family);
            qApp->setFont(font);
        }
    }

    if (this->current_theme == theme && !force) {
        return;
    }

    const auto lowerTheme = theme.toLower();
    const auto &palettes = customThemePalettes();
    const bool leavingCustom = palettes.contains(current_theme.toLower());
    const bool leavingThroned = IsThronedTheme(current_theme);
    const bool enteringCustom = palettes.contains(lowerTheme);

    if (IsThronedTheme(theme)) {
        // The redesigned UI is built from semantic colors. Fusion gives every
        // platform the same control metrics while the palette also covers menus,
        // popups and any legacy widget not yet styled by the new shell.
        qApp->setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
        const auto colors = Colors(theme);
        qApp->setPalette(buildThemePalette({
            .window = colors.window, .windowText = colors.text,
            .base = colors.surface, .alternateBase = colors.surfaceRaised,
            .text = colors.text,
            .button = colors.surfaceRaised, .buttonText = colors.text,
            .brightText = QColor(Qt::white),
            .highlight = colors.accent, .highlightedText = QColor(Qt::white),
            .link = colors.accent,
            .tooltipBase = colors.surfaceRaised, .tooltipText = colors.text,
            .placeholder = colors.textSubtle, .disabledText = colors.textSubtle,
        }));
        qApp->setStyleSheet({});
    } else if (enteringCustom) {
        // Custom themes own their whole look: install the complete palette first
        // so no color role leaks from Qt or a previously applied theme, then
        // layer the stylesheet on top.
        qApp->setPalette(palettes.value(lowerTheme));
        if (lowerTheme == "qdarkstyle") {
            qApp->setStyleSheet(ReadFileText(":/qdarkstyle/dark/darkstyle.qss"));
        } else {
            qApp->setStyleSheet(ReadFileText(":/qss/" + lowerTheme + ".css"));
        }
    } else if (lowerTheme == "system") {
        // Back to the OS style + palette we snapshotted on first apply.
        if (leavingCustom || leavingThroned) qApp->setPalette(system_palette);
        qApp->setStyleSheet("");
        qApp->setStyle(system_style_name);
    } else {
        // A Qt QStyleFactory style (Fusion, windows11, ...). Let the Qt style own
        // the palette; just drop any custom palette we installed before.
        if (leavingCustom || leavingThroned) qApp->setPalette(system_palette);
        qApp->setStyleSheet("");
        qApp->setStyle(theme);
    }

    // setStyle() and setStyleSheet() refill Qt's per-class platform font table, which outranks the
    // app font; re-asserting the font drops it and the sheet call repolishes what it stamped (#1829).
    const auto activeSheet = qApp->styleSheet();
    qApp->setFont(qApp->font());
    if (!activeSheet.isEmpty()) qApp->setStyleSheet(activeSheet);

    current_theme = theme;

    RefreshRegisteredStyles();
    emit themeChanged(theme);
}

void ThemeManager::LoadSkins() {
    skins.clear();
    // Shipped skins are resources, so an ordinary install stays tidy. Disk
    // roots remain optional extension points for skins the user drops in.
    QStringList roots{QStringLiteral(":/skins"),
                      qApp->applicationDirPath() + QStringLiteral("/skins")};
    if (const QString base = Configs::GetBasePath() + QStringLiteral("/skins"); !roots.contains(base)) {
        roots << base;
    }
    QFileInfoList entries;
    for (const QString &path : roots) {
        if (QDir root(path); root.exists())
            entries << root.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    }

    for (const QFileInfo &entry : entries) {
        QFile manifest(entry.absoluteFilePath() + QStringLiteral("/skin.json"));
        if (!manifest.open(QIODevice::ReadOnly)) continue;
        QJsonParseError error{};
        const auto document = QJsonDocument::fromJson(manifest.readAll(), &error);
        if (error.error != QJsonParseError::NoError || !document.isObject()) {
            std::cerr << "Skin " << entry.fileName().toStdString()
                      << " ignored: " << error.errorString().toStdString() << std::endl;
            continue;
        }
        const QJsonObject json = document.object();

        ThronedSkin skin;
        skin.id = entry.fileName();
        skin.name = json.value(QStringLiteral("name")).toString(skin.id);
        skin.fontFamily = json.value(QStringLiteral("font")).toString();

        // Every unset token keeps the default theme's value, so a skin can
        // restyle three things and stay coherent everywhere else.
        skin.colors = thronedThemes().value(QStringLiteral("throned midnight"));
        skin.colors.dark = json.value(QStringLiteral("dark")).toBool(true);
        skin.colors.gloss = json.value(QStringLiteral("gloss")).toDouble(0.0);
        skin.colors.chartBars = json.value(QStringLiteral("chartBars")).toBool(false);
        const QJsonObject colors = json.value(QStringLiteral("colors")).toObject();
        for (auto it = colors.constBegin(); it != colors.constEnd(); ++it) {
            const auto field = ThronedPalette::ColorFields().constFind(it.key());
            if (field == ThronedPalette::ColorFields().constEnd()) {
                std::cerr << "Skin " << skin.id.toStdString() << ": unknown color '"
                          << it.key().toStdString() << "'" << std::endl;
                continue;
            }
            if (const QColor parsed(it.value().toString()); parsed.isValid())
                skin.colors.*(field.value()) = parsed;
        }

        if (QFile sheet(entry.absoluteFilePath() + QStringLiteral("/skin.qss"));
            sheet.open(QIODevice::ReadOnly)) {
            skin.styleOverlay = QString::fromUtf8(sheet.readAll());
        }
        if (QDir icons(entry.absoluteFilePath() + QStringLiteral("/icons")); icons.exists()) {
            skin.iconDir = icons.absolutePath();
        }

        const QString key = skin.name.trimmed().toLower();
        // A skin must not shadow a built-in theme, or the name would resolve to two things.
        if (key.isEmpty() || thronedThemes().contains(key)) continue;
        skins.insert(key, skin);
    }
}

const ThronedSkin *ThemeManager::Skin(const QString &theme) const {
    const QString requested = (theme.isEmpty() ? current_theme : theme).trimmed().toLower();
    const auto it = skins.constFind(requested);
    return it == skins.constEnd() ? nullptr : &it.value();
}

QStringList ThemeManager::ThronedThemes() const {
    QStringList themes = ThronedPalette::ThemeNames();
    for (const auto &skin : skins) themes << skin.name;
    themes << QStringLiteral("System");
    return themes;
}

bool ThemeManager::IsThronedTheme(const QString &theme) const {
    const QString key = theme.trimmed().toLower();
    return thronedThemes().contains(key) || skins.contains(key);
}

ThronedThemeColors ThemeManager::Colors(const QString &theme) const {
    const QString requested = (theme.isEmpty() ? current_theme : theme).trimmed().toLower();
    if (const auto it = thronedThemes().constFind(requested); it != thronedThemes().cend()) return it.value();
    if (const auto it = skins.constFind(requested); it != skins.constEnd()) return it.value().colors;

    // System/legacy themes still get a coherent semantic palette derived from
    // their live QPalette, so every redesigned screen follows the selection.
    const QPalette palette = requested == QStringLiteral("system") && !system_style_name.isEmpty()
        ? system_palette : (qApp ? qApp->palette() : QPalette());
    const QColor window = palette.color(QPalette::Window);
    const QColor surface = palette.color(QPalette::Base);
    const QColor raised = palette.color(QPalette::Button);
    const QColor text = palette.color(QPalette::WindowText);
    const QColor accent = palette.color(QPalette::Highlight);
    const bool dark = window.lightness() < 128;
    return {
        .window = window,
        .surface = surface,
        .surfaceRaised = raised,
        .surfaceHover = dark ? raised.lighter(125) : raised.darker(108),
        .border = palette.color(QPalette::Mid),
        .borderStrong = palette.color(QPalette::Dark),
        .text = text,
        .textMuted = palette.color(QPalette::PlaceholderText),
        .textSubtle = palette.color(QPalette::Disabled, QPalette::Text),
        .accent = accent,
        .accentHover = dark ? accent.lighter(118) : accent.darker(108),
        .accentSoft = mixColors(surface, accent, dark ? 0.28 : 0.18),
        .selection = mixColors(surface, accent, dark ? 0.35 : 0.24),
        .selectionBorder = dark ? accent.darker(115) : accent.darker(125),
        .success = QColor(QStringLiteral("#32C982")),
        .warning = QColor(QStringLiteral("#D9A441")),
        .danger = QColor(QStringLiteral("#D2434E")),
        .dangerSoft = mixColors(surface, QColor(QStringLiteral("#D2434E")), dark ? 0.22 : 0.14),
        .controlInactive = dark ? raised.lighter(112) : raised.darker(112),
        .scrollBar = dark ? raised.lighter(135) : raised.darker(120),
        .scrollBarHover = dark ? raised.lighter(160) : raised.darker(140),
        .dark = dark,
    };
}

bool ThemeManager::IsDarkTheme(const QString &theme) const {
    const QString lower = theme.toLower();
    if (lower.contains(QStringLiteral("qdarkstyle")) || lower.contains(QStringLiteral("blacksoft"))) return true;
    if (lower.contains(QStringLiteral("flatgray")) || lower.contains(QStringLiteral("lightblue"))
        || lower.contains(QStringLiteral("softpink")) || lower.contains(QStringLiteral("vista"))) return false;
    return Colors(theme).dark;
}

QIcon ThemeManager::PreviewIcon(const QString &theme) const {
    const auto colors = Colors(theme);
    QPixmap preview(64, 22);
    preview.fill(Qt::transparent);
    QPainter painter(&preview);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(colors.border, 1));
    painter.setBrush(colors.window);
    painter.drawRoundedRect(QRectF(0.5, 0.5, 63, 21), 5, 5);
    painter.setPen(Qt::NoPen);
    painter.setBrush(colors.surfaceRaised);
    painter.drawRoundedRect(QRectF(5, 5, 35, 12), 3, 3);
    painter.setBrush(colors.accent);
    painter.drawRoundedRect(QRectF(44, 5, 15, 12), 3, 3);
    return QIcon(preview);
}

QString ThemeManager::ResolveStyleSheet(const QString &styleSheetTemplate) const {
    const int pointSize = qApp && qApp->font().pointSize() > 0 ? qApp->font().pointSize() : 10;
    const int fontPx = qMax(11, (pointSize * 4 + 2) / 3);
    QString sheet = ThronedPalette::Resolve(styleSheetTemplate, Colors(), fontPx);
    // Appended rather than merged: a skin overrides by restating the selector, and
    // gets to reach for gradients, images and fonts that colour substitution alone
    // can never express. The overlay is resolved too, so it can use tokens itself.
    if (const ThronedSkin *skin = Skin(); skin != nullptr && !skin->styleOverlay.isEmpty()) {
        sheet += QLatin1Char('\n');
        sheet += ThronedPalette::Resolve(skin->styleOverlay, Colors(), fontPx);
    }
    return sheet;
}

void ThemeManager::RegisterStyle(QWidget *widget, const QString &styleSheetTemplate) const {
    if (!widget) return;
    widget->setProperty(StyleTemplateProperty.toUtf8().constData(), styleSheetTemplate);
    widget->setStyleSheet(ResolveStyleSheet(styleSheetTemplate));
}

void ThemeManager::RefreshRegisteredStyles() const {
    if (!qApp) return;
    for (QWidget *widget : qApp->allWidgets()) {
        const QVariant styleTemplate = widget->property(StyleTemplateProperty.toUtf8().constData());
        if (styleTemplate.isValid()) widget->setStyleSheet(ResolveStyleSheet(styleTemplate.toString()));
    }
}
