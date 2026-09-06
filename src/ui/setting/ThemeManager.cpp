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
#include <QProxyStyle>
#include <QStandardPaths>
#include <QStyleFactory>
#include <QStyleOption>
#include <QWidget>

#include "include/global/Configs.hpp"
#include "include/ui/setting/ThemeManager.hpp"
#include "include/ui/widget/MaterialIcon.h"

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

// A stylesheet can only reach a drop-down arrow through url(), and a resource file
// cannot follow the theme, so the chevron is rasterised once per colour and skin.
QString chevronAssetPath(const QColor &color, const QString &skinId) {
    static QMap<QString, QString> generated;
    const QString key = color.name(QColor::HexRgb).mid(1) + (skinId.isEmpty() ? QString() : QLatin1Char('-') + skinId);
    if (const auto it = generated.constFind(key); it != generated.constEnd()) return *it;
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    if (dir.isEmpty() || !QDir().mkpath(dir)) return {};
    const QString path = dir + QStringLiteral("/chevron-down-") + key + QStringLiteral(".png");
    if (!QFileInfo::exists(path)
        && !MaterialIcon::pixmap(MaterialIcon::Glyph::ChevronDown, color, 28).save(path, "PNG")) return {};
    generated.insert(key, path);
    return path;
}

// Fusion paints combo, spin and menu arrows from windowText at alpha 160, which on
// the dark grounds lands a few units above the surface and reads as an artefact.
class ThronedStyle final : public QProxyStyle {
public:
    using QProxyStyle::QProxyStyle;

    void drawPrimitive(PrimitiveElement element, const QStyleOption *option,
                       QPainter *painter, const QWidget *widget) const override {
        if (element == PE_IndicatorArrowDown || element == PE_IndicatorArrowUp) {
            const auto colors = themeManager()->Colors();
            const QColor color = !option->state.testFlag(State_Enabled) ? colors.textSubtle
                : option->state.testFlag(State_MouseOver) ? colors.text : colors.textMuted;
            // The glyph carries padding inside its box, so it is drawn a little larger
            // than the slot the style hands us or it reads as a speck.
            const int side = qBound(10, qMin(option->rect.width(), option->rect.height()) + 3, 18);
            if (option->rect.width() >= 7 && option->rect.height() >= 7) {
                const QPixmap glyph = MaterialIcon::pixmap(element == PE_IndicatorArrowDown
                    ? MaterialIcon::Glyph::ChevronDown : MaterialIcon::Glyph::ChevronUp, color, side);
                painter->drawPixmap(alignedRect(option->direction, Qt::AlignCenter,
                    glyph.deviceIndependentSize().toSize(), option->rect), glyph);
                return;
            }
        }
        QProxyStyle::drawPrimitive(element, option, painter, widget);
    }
};

} // namespace

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
    const bool leavingThroned = IsThronedTheme(current_theme);

    if (IsThronedTheme(theme)) {
        // The redesigned UI is built from semantic colors. Fusion gives every
        // platform the same control metrics while the palette also covers menus,
        // popups and any legacy widget not yet styled by the new shell.
        qApp->setStyle(new ThronedStyle(QStyleFactory::create(QStringLiteral("Fusion"))));
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
    } else if (lowerTheme == "system") {
        // Back to the OS style + palette we snapshotted on first apply.
        if (leavingThroned) qApp->setPalette(system_palette);
        qApp->setStyleSheet("");
        qApp->setStyle(system_style_name);
    } else {
        // A Qt QStyleFactory style (Fusion, windows11, ...). Let the Qt style own
        // the palette; just drop any custom palette we installed before.
        if (leavingThroned) qApp->setPalette(system_palette);
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
    if (sheet.contains(QStringLiteral("%CHEVRON_DOWN%"))) {
        const ThronedSkin *skin = Skin();
        sheet.replace(QStringLiteral("%CHEVRON_DOWN%"),
                      chevronAssetPath(Colors().textMuted, skin != nullptr ? skin->id : QString()));
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
