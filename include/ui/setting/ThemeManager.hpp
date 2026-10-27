#pragma once

#include <QObject>
#include <QColor>
#include <QHash>
#include <QIcon>
#include <QPalette>
#include <QMap>
#include <QStringList>
#include <QStyle>

#include "include/ui/setting/ThronedPalette.hpp"

class QWidget;

class ThemeManager : public QObject {
    Q_OBJECT
public:
    QString system_style_name = "";
    QPalette system_palette;     // snapshot of the OS palette, taken on first apply
    QString current_theme = "0"; // int: 0:system 1+:builtin string: QStyleFactory

    void ApplyTheme(const QString &theme, bool force = false);
    [[nodiscard]] QStringList ThronedThemes() const;
    [[nodiscard]] bool IsThronedTheme(const QString &theme) const;
    [[nodiscard]] bool IsDarkTheme(const QString &theme) const;
    [[nodiscard]] ThronedThemeColors Colors(const QString &theme = {}) const;
    [[nodiscard]] QIcon PreviewIcon(const QString &theme) const;
    [[nodiscard]] QString ResolveStyleSheet(const QString &styleSheetTemplate) const;

    // Bundled skins are resources; optional user skins live in <base>/skins/<id>/.
    void LoadSkins();
    [[nodiscard]] const ThronedSkin *Skin(const QString &theme = {}) const;
    void RegisterStyle(QWidget *widget, const QString &styleSheetTemplate) const;
    void RefreshRegisteredStyles() const;

    // QStyle::standardIcon loads from the system on every call: about 9 ms for a message box icon on Windows.
    QIcon standardIcon(QStyle::StandardPixmap pixmap);
signals:
    void themeChanged(QString themeName);

private:
    QMap<QString, ThronedSkin> skins; // keyed by lowercased display name
    QString base_font_family;
    QHash<int, QIcon> standardIcons; // the current style's; ApplyTheme() reloads the message box icons
};

ThemeManager *themeManager();
