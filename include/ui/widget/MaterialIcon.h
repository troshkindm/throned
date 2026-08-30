#pragma once

#include <QColor>
#include <QIcon>
#include <QPixmap>

namespace MaterialIcon {

enum class Glyph {
    Add,
    ArrowDown,
    ArrowUp,
    Apps,
    Block,
    Bolt,
    Check,
    ChevronDown,
    ChevronUp,
    ChevronRight,
    Close,
    Code,
    Copy,
    Delete,
    Desktop,
    Direct,
    File,
    Filter,
    Folder,
    List,
    Menu,
    More,
    Process,
    Public,
    Reload,
    Routes,
    Search,
    Settings,
    Shield,
    SwapVertical,
    Tools,
    Tune,
    Users,
};

QPixmap pixmap(Glyph glyph, const QColor &color, int pixels = 20);
QIcon icon(Glyph glyph, const QColor &color, int pixels = 20);

} // namespace MaterialIcon
