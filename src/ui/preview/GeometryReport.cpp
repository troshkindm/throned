#include "include/ui/preview/GeometryReport.h"

#include <algorithm>

#include <QAbstractButton>
#include <QChar>
#include <QFile>
#include <QChar>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QString>
#include <QWidget>

namespace {

// Only named widgets. Qt invents names for the rest, and those move between runs
// for reasons that have nothing to do with the layout.
bool reportable(const QWidget *widget) {
    if (widget == nullptr) return false;
    const QString name = widget->objectName();
    if (name.isEmpty()) return false;
    // Qt names a few of its own parts; they are not this project's layout.
    if (name.startsWith(QLatin1String("qt_"))) return false;
    return name != QLatin1String("ScrollLeftButton") && name != QLatin1String("ScrollRightButton");
}

QString visibleText(const QWidget *widget) {
    if (const auto *label = qobject_cast<const QLabel *>(widget))
        return label->wordWrap() ? QString() : label->text();
    if (const auto *button = qobject_cast<const QAbstractButton *>(widget)) return button->text();
    return {};
}

// The reason this file exists: a label that no longer fits silently becomes an
// ellipsis, and every screenshot of it still looks like a plausible interface.
// Asked of sizeHint rather than of the font, because a stylesheet font-size never
// reaches QWidget::font() and measuring against that reports fits as overflows.
bool textIsCut(const QWidget *widget) {
    // An unshown widget still sits at its default size, which is no layout fault.
    if (!widget->isVisible()) return false;
    const QString text = visibleText(widget);
    // Text the code elided itself ends in an ellipsis: a decision, not an accident.
    if (text.isEmpty() || text.endsWith(QChar(0x2026))) return false;
    const int wanted = widget->sizeHint().width();
    return wanted > 0 && widget->width() > 0 && wanted > widget->width();
}

} // namespace

namespace UiPreview {

QString GeometryReport(QWidget *root) {
    if (root == nullptr) return {};

    QJsonArray entries;
    for (QWidget *widget: root->findChildren<QWidget *>()) {
        if (!reportable(widget)) continue;
        const QRect box(widget->mapTo(root, QPoint(0, 0)), widget->size());
        QJsonObject entry{
            {"name", widget->objectName()},
            {"class", QString::fromLatin1(widget->metaObject()->className())},
            {"x", box.x()},
            {"y", box.y()},
            {"w", box.width()},
            {"h", box.height()},
            {"visible", widget->isVisible()},
        };
        if (textIsCut(widget)) {
            entry["cut"] = true;
            entry["text"] = visibleText(widget);
        }
        entries.append(entry);
    }

    // Sorted by name and position so the report describes the layout rather than
    // the order Qt happened to hand the children over in.
    QList<QJsonValue> sorted;
    sorted.reserve(entries.size());
    for (const QJsonValue &entry: entries) sorted.append(entry);
    std::sort(sorted.begin(), sorted.end(), [](const QJsonValue &a, const QJsonValue &b) {
        const auto left = a.toObject();
        const auto right = b.toObject();
        if (left["name"] != right["name"]) return left["name"].toString() < right["name"].toString();
        if (left["y"] != right["y"]) return left["y"].toInt() < right["y"].toInt();
        return left["x"].toInt() < right["x"].toInt();
    });

    QJsonArray ordered;
    for (const QJsonValue &entry: sorted) ordered.append(entry);
    return QString::fromUtf8(QJsonDocument(ordered).toJson(QJsonDocument::Indented));
}

void SaveGeometryReport(QWidget *root, const QString &imagePath) {
    QString path = imagePath;
    if (path.endsWith(QStringLiteral(".png"), Qt::CaseInsensitive)) path.chop(4);
    path += QStringLiteral(".json");
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) return;
    file.write(GeometryReport(root).toUtf8());
}

} // namespace UiPreview
