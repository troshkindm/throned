#include "include/ui/widget/MaterialIcon.h"

#include "include/ui/setting/ThemeManager.hpp"

#include <QPainter>
#include <QFileInfo>
#include <QGuiApplication>
#include <QSvgRenderer>

namespace {

const char *pathFor(MaterialIcon::Glyph glyph) {
    using MaterialIcon::Glyph;
    switch (glyph) {
    case Glyph::Add:
        return "M19 13h-6v6h-2v-6H5v-2h6V5h2v6h6v2z";
    case Glyph::ArrowDown:
        return "M20 12l-1.41-1.41L13 16.17V4h-2v12.17l-5.59-5.58L4 12l8 8 8-8z";
    case Glyph::ArrowUp:
        return "M4 12l1.41 1.41L11 7.83V20h2V7.83l5.59 5.58L20 12l-8-8-8 8z";
    case Glyph::Apps:
        return "M4 8h4V4H4v4zm6 12h4v-4h-4v4zm-6 0h4v-4H4v4zm0-6h4v-4H4v4zm6 0h4v-4h-4v4zm6-10v4h4V4h-4zm-6 4h4V4h-4v4zm6 6h4v-4h-4v4zm0 6h4v-4h-4v4z";
    case Glyph::Bell:
        return "M12 22c1.1 0 2-.9 2-2h-4c0 1.1.9 2 2 2zm6-6v-5c0-3.07-1.64-5.64-4.5-6.32V4c0-.83-.67-1.5-1.5-1.5S10.5 3.17 10.5 4v.68C7.63 5.36 6 7.92 6 11v5l-2 2v1h16v-1l-2-2z";
    case Glyph::BellOff:
        return "M20 18.69 7.84 6.14 5.27 3.49 4 4.76l2.8 2.8v.01C6.28 8.56 6 9.73 6 11v5l-2 2v1h13.73l2 2L21 19.72l-1-1.03zM12 22c1.11 0 2-.89 2-2h-4c0 1.11.89 2 2 2zm6-7.32V11c0-3.08-1.64-5.64-4.5-6.32V4c0-.83-.67-1.5-1.5-1.5s-1.5.67-1.5 1.5v.68c-.15.03-.29.08-.42.12L18 14.68z";
    case Glyph::Block:
        return "M12 2a10 10 0 1 0 0 20 10 10 0 0 0 0-20zM4 12c0-1.85.63-3.55 1.69-4.9L16.9 18.31A7.96 7.96 0 0 1 4 12zm14.31 4.9L7.1 5.69A7.96 7.96 0 0 1 18.31 16.9z";
    case Glyph::Bolt:
        return "M11 21h-1l1-7H7.5c-.88 0-.33-.75-.31-.78C8.48 10.94 10.42 7.54 13 3h1l-1 7h3.5c.4 0 .62.19.4.66C12.97 17.53 11 21 11 21z";
    case Glyph::Campaign:
        return "M18 11v2h4v-2h-4zm-2 6.61c.96.71 2.21 1.65 3.2 2.39.4-.53.8-1.07 1.2-1.6-.99-.74-2.24-1.68-3.2-2.4-.4.54-.8 1.08-1.2 1.61zM20.4 5.6c-.4-.53-.8-1.07-1.2-1.6-.99.74-2.24 1.68-3.2 2.4.4.53.8 1.07 1.2 1.6.96-.72 2.21-1.65 3.2-2.4zM4 9c-1.1 0-2 .9-2 2v2c0 1.1.9 2 2 2h1v4h2v-4h1l5 3V6L8 9H4zm11.5 3c0-1.33-.58-2.53-1.5-3.35v6.69c.92-.81 1.5-2.01 1.5-3.34z";
    case Glyph::Check:
        return "M9 16.17 4.83 12l-1.42 1.41L9 19 21 7l-1.41-1.41z";
    case Glyph::ChevronDown:
        return "M16.59 8.59 12 13.17 7.41 8.59 6 10l6 6 6-6-1.41-1.41z";
    case Glyph::ChevronUp:
        return "M7.41 15.41 12 10.83l4.59 4.58L18 14l-6-6-6 6 1.41 1.41z";
    case Glyph::ChevronRight:
        return "M9.29 6.71a.996.996 0 0 0 0 1.41L13.17 12l-3.88 3.88a.996.996 0 1 0 1.41 1.41l4.59-4.59a.996.996 0 0 0 0-1.41L10.7 6.71a.996.996 0 0 0-1.41 0z";
    case Glyph::Close:
        return "M18.3 5.71 12 12l6.3 6.29-1.41 1.42-6.3-6.3-6.3 6.3-1.41-1.42L9.17 12l-6.29-6.3 1.41-1.41 6.3 6.3 6.3-6.3 1.41 1.42z";
    case Glyph::Code:
        return "M9.4 16.6 4.8 12l4.6-4.6L8 6l-6 6 6 6 1.4-1.4zm5.2 0 4.6-4.6-4.6-4.6L16 6l6 6-6 6-1.4-1.4z";
    case Glyph::Copy:
        return "M16 1H4c-1.1 0-2 .9-2 2v14h2V3h12V1zm3 4H8c-1.1 0-2 .9-2 2v14c0 1.1.9 2 2 2h11c1.1 0 2-.9 2-2V7c0-1.1-.9-2-2-2zm0 16H8V7h11v14z";
    case Glyph::Delete:
        return "M6 19c0 1.1.9 2 2 2h8c1.1 0 2-.9 2-2V7H6v12zm2-10h8v10H8V9zm7.5-5-1-1h-5l-1 1H5v2h14V4z";
    case Glyph::Desktop:
        return "M21 2H3c-1.1 0-2 .9-2 2v12c0 1.1.9 2 2 2h7v2H8v2h8v-2h-2v-2h7c1.1 0 2-.9 2-2V4c0-1.1-.9-2-2-2zm0 14H3V4h18v12z";
    case Glyph::Direct:
        return "M2.01 21 23 12 2.01 3 2 10l15 2-15 2 .01 7z";
    case Glyph::File:
        return "M14 2H6a2 2 0 0 0-2 2v16a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2V8l-6-6zm1 7V3.5L20.5 9H15z";
    case Glyph::Filter:
        return "M5.04 4h13.91c.83 0 1.3.95.79 1.61L14 13v6a1 1 0 0 1-1 1h-2a1 1 0 0 1-1-1v-6L4.25 5.61C3.74 4.95 4.21 4 5.04 4z";
    case Glyph::Folder:
        return "M10 4H2c-1.1 0-2 .9-2 2v12c0 1.1.9 2 2 2h20c1.1 0 2-.9 2-2V8c0-1.1-.9-2-2-2h-10l-2-2z";
    case Glyph::List:
        return "M4 10.5A1.5 1.5 0 1 0 4 13.5a1.5 1.5 0 0 0 0-3zm0-6A1.5 1.5 0 1 0 4 7.5a1.5 1.5 0 0 0 0-3zm0 12A1.5 1.5 0 1 0 4 19.5a1.5 1.5 0 0 0 0-3zM7 19h14v-2H7v2zm0-6h14v-2H7v2zm0-8v2h14V5H7z";
    case Glyph::Menu:
        return "M3 6h18v2H3V6zm0 5h18v2H3v-2zm0 5h18v2H3v-2z";
    case Glyph::More:
        return "M6 10a2 2 0 1 0 0 4 2 2 0 0 0 0-4zm6 0a2 2 0 1 0 0 4 2 2 0 0 0 0-4zm6 0a2 2 0 1 0 0 4 2 2 0 0 0 0-4z";
    case Glyph::Process:
        return "M20 4H4a2 2 0 0 0-2 2v12c0 1.1.89 2 2 2h16c1.1 0 2-.9 2-2V6a2 2 0 0 0-2-2zm0 14H4V8h16v10zm-2-1h-6v-2h6v2zM7.5 17l-1.41-1.41L8.67 13l-2.59-2.59L7.5 9l4 4-4 4z";
    case Glyph::Public:
        return "M12 2a10 10 0 1 0 0 20 10 10 0 0 0 0-20zM4 12c0-.61.08-1.21.21-1.78L9 15v1a2 2 0 0 0 2 2v1.93A8 8 0 0 1 4 12zm13.89 5.4A2 2 0 0 0 16 16h-1v-3a1 1 0 0 0-1-1H8v-2h2a1 1 0 0 0 1-1V7h2a2 2 0 0 0 2-2v-.41A8 8 0 0 1 17.89 17.4z";
    // A traced ECG line: five quads for the strokes, four discs for the joints, all
    // wound the same way so the overlaps fill instead of punching holes.
    case Glyph::Pulse:
        return "M2 10.9h5v2.2H2z"
               "M5.99 11.57 8.99 4.57l2.02.86-3 7z"
               "M11.08 4.77l3 14-2.16.46-3-14z"
               "M11.99 18.57l3-7 2.02.86-3 7z"
               "M16 10.9h6v2.2h-6z"
               "M5.9 12a1.1 1.1 0 1 1 2.2 0 1.1 1.1 0 1 1-2.2 0z"
               "M8.9 5a1.1 1.1 0 1 1 2.2 0 1.1 1.1 0 1 1-2.2 0z"
               "M11.9 19a1.1 1.1 0 1 1 2.2 0 1.1 1.1 0 1 1-2.2 0z"
               "M14.9 12a1.1 1.1 0 1 1 2.2 0 1.1 1.1 0 1 1-2.2 0z";
    case Glyph::Reload:
        return "M17.65 6.35C16.2 4.9 14.21 4 12 4a8 8 0 1 0 7.75 10h-2.1A6 6 0 1 1 12 6c1.66 0 3.14.69 4.22 1.78L13 11h7V4l-2.35 2.35z";
    // Deliberately not the swap arrows: the routing status sits next to the
    // proxy up/down indicator, and two identical glyphs read as one thing.
    case Glyph::Routes:
        return "M9.78 11.16l-1.42 1.42a7.28 7.28 0 0 1-1.79-2.94l1.94-.49c.32.89.77 1.5 1.27 2.01zM11 6L7 2 3 6h3.02c.02.81.08 1.54.19 2.17l1.94-.49C8.08 7.2 8.03 6.63 8.02 6H11zm10 0l-4-4-4 4h2.99c-.1 3.68-1.28 4.75-2.54 5.88-.5.44-1.01.92-1.45 1.55-.34-.49-.73-.88-1.13-1.24L9.46 13.6c.86.75 1.53 1.4 1.53 3.4v5h2v-5c0-1.46.68-2.1 1.79-3.1 1.35-1.22 3.02-2.74 3.15-6.9H21z";
    case Glyph::Search:
        return "M9.5 3a6.5 6.5 0 1 0 4.1 11.55L19.05 20 21 18.05l-5.45-5.45A6.5 6.5 0 0 0 9.5 3zm0 2a4.5 4.5 0 1 1 0 9 4.5 4.5 0 0 1 0-9z";
    case Glyph::Settings:
        return "M19.43 12.98c.04-.32.07-.65.07-.98s-.02-.66-.07-.98l2.11-1.65a.5.5 0 0 0 .12-.64l-2-3.46a.5.5 0 0 0-.61-.22l-2.49 1a7.3 7.3 0 0 0-1.69-.98L14.5 2.42A.49.49 0 0 0 14 2h-4a.49.49 0 0 0-.49.42l-.38 2.65c-.61.25-1.17.59-1.69.98l-2.49-1a.49.49 0 0 0-.61.22l-2 3.46a.49.49 0 0 0 .12.64l2.11 1.65c-.04.32-.08.66-.08.98s.03.66.08.98l-2.11 1.65a.5.5 0 0 0-.12.64l2 3.46c.12.22.37.31.61.22l2.49-1c.52.4 1.08.73 1.69.98l.38 2.65c.04.24.24.42.49.42h4c.25 0 .46-.18.49-.42l.38-2.65c.61-.25 1.17-.58 1.69-.98l2.49 1c.23.08.49 0 .61-.22l2-3.46a.5.5 0 0 0-.12-.64l-2.11-1.65zM12 15.5A3.5 3.5 0 1 1 12 8a3.5 3.5 0 0 1 0 7.5z";
    case Glyph::Star:
        return "M12 17.27 18.18 21l-1.64-7.03L22 9.24l-7.19-.61L12 2 9.19 8.63 2 9.24l5.46 4.73L5.82 21z";
    case Glyph::StarOutline:
        return "M22 9.24l-7.19-.62L12 2 9.19 8.63 2 9.24l5.46 4.73L5.82 21 12 17.27 18.18 21l-1.63-7.03zM12 15.4l-3.76 2.27c.34-1.45.63-2.71 1-4.29l-3.33-2.88 4.4-.38L12 6.1l1.71 4.04 4.38.36-3.33 2.89 1 4.28z";
    case Glyph::Shield:
        return "M12 2 4 5v6.09C4 16.14 7.41 20.85 12 22c4.59-1.15 8-5.86 8-10.91V5l-8-3zm0 17.92c-3.45-1.13-6-4.82-6-8.83v-4.7l6-2.25 6 2.25v4.7c0 4-2.55 7.7-6 8.83z";
    case Glyph::SwapVertical:
        return "M17 16.41V7.59L15.59 9 14.17 7.59 18 3.76l3.83 3.83L20.41 9 19 7.59v8.82L20.41 15l1.42 1.41L18 20.24l-3.83-3.83L15.59 15 17 16.41zM7 7.59v8.82L8.41 15l1.42 1.41L6 20.24l-3.83-3.83L3.59 15 5 16.41V7.59L3.59 9 2.17 7.59 6 3.76l3.83 3.83L8.41 9 7 7.59z";
    case Glyph::Tools:
        return "M22.7 19 13.6 9.9c.9-2.3.4-5-1.5-6.9-2-2-5-2.4-7.3-1.1l4.3 4.3-3 3-4.4-4.3C.4 7.2.8 10.2 2.8 12.2c1.9 1.9 4.6 2.4 6.9 1.5l9.1 9.1c.4.4 1 .4 1.4 0l2.5-2.5c.4-.3.4-.9 0-1.3z";
    case Glyph::Tune:
        return "M3 17v2h6v-2H3zM3 5v2h10V5H3zm10 16v-2h8v-2h-8v-2h-2v6h2zM7 9v2H3v2h4v2h2V9H7zm14 4v-2H11v2h10zm-6-4h2V7h4V5h-4V3h-2v6z";
    case Glyph::Users:
        return "M16 11c1.66 0 2.99-1.34 2.99-3S17.66 5 16 5s-3 1.34-3 3 1.34 3 3 3zm-8 0c1.66 0 2.99-1.34 2.99-3S9.66 5 8 5 5 6.34 5 8s1.34 3 3 3zm0 2c-2.33 0-7 1.17-7 3.5V19h14v-2.5C15 14.17 10.33 13 8 13zm8 0c-.29 0-.62.02-.97.05 1.16.84 1.97 1.97 1.97 3.45V19h6v-2.5c0-2.33-4.67-3.5-7-3.5z";
    }
    return "";
}

// A skin overrides a glyph by dropping <name>.svg or <name>.png into its icons/
// folder; the names are the enum spelled in kebab case.
const char *glyphName(MaterialIcon::Glyph glyph) {
    using MaterialIcon::Glyph;
    switch (glyph) {
    case Glyph::Add: return "add";
    case Glyph::ArrowDown: return "arrow-down";
    case Glyph::ArrowUp: return "arrow-up";
    case Glyph::Apps: return "apps";
    case Glyph::Bell: return "bell";
    case Glyph::BellOff: return "bell-off";
    case Glyph::Block: return "block";
    case Glyph::Bolt: return "bolt";
    case Glyph::Campaign: return "campaign";
    case Glyph::Check: return "check";
    case Glyph::ChevronDown: return "chevron-down";
    case Glyph::ChevronUp: return "chevron-up";
    case Glyph::ChevronRight: return "chevron-right";
    case Glyph::Close: return "close";
    case Glyph::Code: return "code";
    case Glyph::Copy: return "copy";
    case Glyph::Delete: return "delete";
    case Glyph::Desktop: return "desktop";
    case Glyph::Direct: return "direct";
    case Glyph::File: return "file";
    case Glyph::Filter: return "filter";
    case Glyph::Folder: return "folder";
    case Glyph::List: return "list";
    case Glyph::Menu: return "menu";
    case Glyph::More: return "more";
    case Glyph::Process: return "process";
    case Glyph::Public: return "public";
    case Glyph::Pulse: return "pulse";
    case Glyph::Reload: return "reload";
    case Glyph::Routes: return "routes";
    case Glyph::Search: return "search";
    case Glyph::Settings: return "settings";
    case Glyph::Star: return "star";
    case Glyph::StarOutline: return "star-outline";
    case Glyph::Shield: return "shield";
    case Glyph::SwapVertical: return "swap-vertical";
    case Glyph::Tools: return "tools";
    case Glyph::Tune: return "tune";
    case Glyph::Users: return "users";
    }
    return "";
}
} // namespace

namespace MaterialIcon {

QPixmap pixmap(Glyph glyph, const QColor &color, int pixels) {
    // Rasterise at the screen's ratio. A 1x pixmap upscaled by the compositor is
    // the difference between a crisp glyph and a smeared one at 125% and up.
    const qreal ratio = qGuiApp != nullptr ? qGuiApp->devicePixelRatio() : 1.0;

    // A skin's own art is used as authored: it carries its own colour, which is
    // the whole point of shipping a glossy icon set rather than a tinted path.
    if (const ThronedSkin *skin = themeManager() != nullptr ? themeManager()->Skin() : nullptr;
        skin != nullptr && !skin->iconDir.isEmpty()) {
        const QString stem = skin->iconDir + QLatin1Char('/') + QLatin1String(glyphName(glyph));
        for (const QString &suffix : {QStringLiteral(".svg"), QStringLiteral(".png")}) {
            const QString path = stem + suffix;
            if (!QFileInfo::exists(path)) continue;
            QPixmap art(QSize(pixels, pixels) * ratio);
            art.setDevicePixelRatio(ratio);
            art.fill(Qt::transparent);
            QPainter painter(&art);
            painter.setRenderHint(QPainter::Antialiasing);
            painter.setRenderHint(QPainter::SmoothPixmapTransform);
            if (suffix == QStringLiteral(".svg")) {
                QSvgRenderer skinned(path);
                if (!skinned.isValid()) continue;
                skinned.render(&painter, QRectF(0, 0, pixels, pixels));
            } else {
                const QPixmap source(path);
                if (source.isNull()) continue;
                painter.drawPixmap(QRectF(0, 0, pixels, pixels), source, source.rect());
            }
            painter.end();
            return art;
        }
    }

    const QByteArray svg = QByteArray("<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24'><path fill='")
        + color.name(QColor::HexRgb).toUtf8() + "' d='" + pathFor(glyph) + "'/></svg>";
    QSvgRenderer renderer(svg);
    QPixmap result(QSize(pixels, pixels) * ratio);
    result.setDevicePixelRatio(ratio);
    result.fill(Qt::transparent);
    QPainter painter(&result);
    painter.setRenderHint(QPainter::Antialiasing);
    renderer.render(&painter, QRectF(0, 0, pixels, pixels));
    return result;
}

QIcon icon(Glyph glyph, const QColor &color, int pixels) {
    return QIcon(pixmap(glyph, color, pixels));
}

} // namespace MaterialIcon
