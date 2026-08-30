#include "include/ui/utils/ProfileRowDelegate.h"

#include "include/database/entities/Profile.h"
#include "include/ui/setting/ThemeManager.hpp"
#include "include/ui/utils/ProfilesTableModel.h"

#include <QApplication>
#include <QFontMetrics>
#include <QPainter>
#include <QStyle>

namespace {
    constexpr int kPadX = 8;
    constexpr int kChipGap = 8;
    constexpr int kLineGap = 3;
    constexpr int kExitGap = 18;
    constexpr int kExitMinWidth = 92;
    constexpr int kAddressMinWidth = 132;

    QFont primaryFont(const QFont &base) {
        QFont font = base;
        font.setWeight(QFont::DemiBold);
        return font;
    }

    QFont secondaryFont(const QFont &base) {
        QFont font = base;
        if (font.pixelSize() > 0) font.setPixelSize(qMax(9, font.pixelSize() - 2));
        else font.setPointSizeF(qMax(7.0, font.pointSizeF() - 1.5));
        return font;
    }

    // The address line sits one step down from the name, not two: at -2 it was
    // small enough that the exit IP read as part of the server address.
    QFont metaFont(const QFont &base) {
        QFont font = base;
        if (font.pixelSize() > 0) font.setPixelSize(qMax(10, font.pixelSize() - 1));
        else font.setPointSizeF(qMax(7.5, font.pointSizeF() - 0.75));
        return font;
    }

    QFont captionFont(const QFont &base) {
        QFont font = secondaryFont(base);
        font.setWeight(QFont::DemiBold);
        font.setCapitalization(QFont::AllUppercase);
        return font;
    }

    QColor latencyColor(int latencyMs, const ThronedThemeColors &colors) {
        if (latencyMs == Configs::kLatencyConnectOnly) return colors.accent;
        if (latencyMs < 0) return QColor(QStringLiteral("#E06C6C"));
        if (latencyMs == 0) return colors.textSubtle;
        if (latencyMs <= 100) return colors.success;
        if (latencyMs <= 300) return QColor(QStringLiteral("#D9A441"));
        return QColor(QStringLiteral("#E06C6C"));
    }

    // Two stacked baselines inside one cell, both vertically centred as a pair.
    QPair<QRect, QRect> lineRects(const QRect &cell, int topHeight, int bottomHeight) {
        const int block = topHeight + kLineGap + bottomHeight;
        const int top = cell.top() + (cell.height() - block) / 2;
        return {QRect(cell.left(), top, cell.width(), topHeight),
                QRect(cell.left(), top + topHeight + kLineGap, cell.width(), bottomHeight)};
    }

    void drawChip(QPainter *painter, const QRect &rect, const QString &text,
                  const QFont &font, const QColor &line, const QColor &ink) {
        painter->save();
        painter->setFont(font);
        painter->setPen(line);
        painter->setRenderHint(QPainter::Antialiasing, true);
        painter->drawRoundedRect(QRectF(rect).adjusted(0.5, 0.5, -0.5, -0.5), 3, 3);
        painter->setPen(ink);
        painter->drawText(rect, Qt::AlignCenter, text);
        painter->restore();
    }

    int chipWidth(const QFontMetrics &metrics, const QString &text) {
        return metrics.horizontalAdvance(text) + 12;
    }

    // Filled, unlike the outlined protocol chip: a country reads as a stamp, and a
    // flag glyph comes from the emoji font, which ignores the row's type size.
    void drawBadge(QPainter *painter, const QRect &rect, const QString &text,
                   const QFont &font, const QColor &fill, const QColor &ink) {
        painter->save();
        painter->setFont(font);
        painter->setPen(Qt::NoPen);
        painter->setBrush(fill);
        painter->setRenderHint(QPainter::Antialiasing, true);
        painter->drawRoundedRect(QRectF(rect), 2.5, 2.5);
        painter->setPen(ink);
        painter->drawText(rect, Qt::AlignCenter, text);
        painter->restore();
    }

    // Right-aligned stack: a value over a smaller note, either of which may be absent.
    void drawStack(QPainter *painter, const QRect &cell, const QString &top, const QColor &topInk,
                   const QFont &topFont, const QString &bottom, const QColor &bottomInk,
                   const QFont &bottomFont, const QColor &emptyInk) {
        const QFontMetrics topMetrics(topFont);
        const QFontMetrics bottomMetrics(bottomFont);
        if (top.isEmpty() && bottom.isEmpty()) {
            painter->setFont(bottomFont);
            painter->setPen(emptyInk);
            painter->drawText(cell, Qt::AlignRight | Qt::AlignVCenter, QStringLiteral("—"));
            return;
        }
        const auto [topRect, bottomRect] = lineRects(cell, topMetrics.height(), bottomMetrics.height());
        if (!top.isEmpty()) {
            painter->setFont(topFont);
            painter->setPen(topInk);
            painter->drawText(bottom.isEmpty() ? cell : topRect, Qt::AlignRight | Qt::AlignVCenter,
                              topMetrics.elidedText(top, Qt::ElideRight, cell.width()));
        }
        if (!bottom.isEmpty()) {
            painter->setFont(bottomFont);
            painter->setPen(bottomInk);
            painter->drawText(top.isEmpty() ? cell : bottomRect, Qt::AlignRight | Qt::AlignVCenter,
                              bottomMetrics.elidedText(bottom, Qt::ElideRight, cell.width()));
        }
    }
}

int ProfileRowDelegate::metricColumnWidth(int column, const QFont &font) {
    const QFontMetrics plain(font);
    const QFontMetrics small(secondaryFont(font));
    const QFontMetrics bold(primaryFont(font));
    // Widest realistic reading per column, so a column measured while every row is
    // still untested does not clip the results that arrive later.
    switch (column) {
    case ProfilesTableModel::ColcPing:
        return qMax(bold.horizontalAdvance(QStringLiteral("9999 ms")),
                    small.horizontalAdvance(tr("UDP %1").arg(QStringLiteral("999 ms ±99 / 99%")))) + kPadX * 2;
    case ProfilesTableModel::ColcSpeed:
        return qMax(plain.horizontalAdvance(QStringLiteral("↓ 9999 Mbps")),
                    small.horizontalAdvance(QStringLiteral("↑ 9999 Mbps"))) + kPadX * 2;
    case ProfilesTableModel::ColcTraffic:
        return qMax(plain.horizontalAdvance(QStringLiteral("↓ 999.99 MiB")),
                    small.horizontalAdvance(QStringLiteral("↑ 999.99 MiB"))) + kPadX * 2;
    default:
        return 0;
    }
}

void ProfileRowDelegate::setFlash(int row, qreal strength) {
    m_flashRow = row;
    m_flashStrength = qBound(0.0, strength, 1.0);
}

void ProfileRowDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option,
                               const QModelIndex &index) const {
    QStyleOptionViewItem opt(option);
    initStyleOption(&opt, index);
    // The stylesheet owns the row background, selection fill and separators.
    opt.text.clear();
    QStyle *style = opt.widget ? opt.widget->style() : QApplication::style();
    style->drawControl(QStyle::CE_ItemViewItem, &opt, painter, opt.widget);

    const auto visual = index.data(ProfilesTableModel::RowVisualRole).value<ProfilesTableModel::RowVisual>();
    const auto colors = themeManager->Colors();
    const bool selected = opt.state & QStyle::State_Selected;

    if (index.row() == m_flashRow && m_flashStrength > 0.0) {
        QColor flash = colors.accent;
        flash.setAlphaF(0.28 * m_flashStrength);
        painter->fillRect(opt.rect, flash);
    }
    // The tick in the number gutter is easy to lose in a long list; the stripe
    // gives the running row an edge that survives scrolling past it.
    if (visual.running && index.column() == ProfilesTableModel::ColcServer) {
        painter->fillRect(QRect(opt.rect.left(), opt.rect.top() + 1, 3, opt.rect.height() - 2),
                          colors.accent);
    }

    const QColor ink = selected ? opt.palette.color(QPalette::HighlightedText) : colors.text;
    const QColor muted = selected ? opt.palette.color(QPalette::HighlightedText).darker(125) : colors.textMuted;
    const QColor subtle = selected ? opt.palette.color(QPalette::HighlightedText).darker(140) : colors.textSubtle;

    const QFont bold = primaryFont(opt.font);
    const QFont small = secondaryFont(opt.font);
    const QFontMetrics boldMetrics(bold);
    const QFontMetrics smallMetrics(small);

    const QRect cell = opt.rect.adjusted(kPadX, 0, -kPadX, 0);
    painter->save();

    switch (index.column()) {
    case ProfilesTableModel::ColcServer: {
        const auto [nameRect, metaRect] = lineRects(cell, boldMetrics.height(),
                                                    QFontMetrics(metaFont(opt.font)).height());
        const QFont meta = metaFont(opt.font);
        const QFont caption = captionFont(opt.font);
        const QFontMetrics metaMetrics(meta);
        const QFontMetrics captionMetrics(caption);

        const bool hasExit = !visual.country.isEmpty() || !visual.exitIp.isEmpty();
        const int badgeWidth = visual.country.isEmpty()
            ? 0 : captionMetrics.horizontalAdvance(visual.country) + 9;
        const int naturalExitWidth = badgeWidth
            + ((!visual.country.isEmpty() && !visual.exitIp.isEmpty()) ? 7 : 0)
            + metaMetrics.horizontalAdvance(visual.exitIp);
        const int availableExitWidth = qMax(0, cell.width() - kAddressMinWidth - kExitGap);
        const int exitWidth = hasExit
            ? qMin(qMax(kExitMinWidth, naturalExitWidth), availableExitWidth) : 0;
        const bool showExit = hasExit && exitWidth >= 70;

        QRect leftNameRect = nameRect;
        QRect addressRect = metaRect;
        QRect exitNameRect;
        QRect exitMetaRect;
        if (showExit) {
            leftNameRect.setRight(nameRect.right() - exitWidth - kExitGap);
            addressRect.setRight(metaRect.right() - exitWidth - kExitGap);
            exitNameRect = QRect(nameRect.right() - exitWidth + 1, nameRect.top(),
                                 exitWidth, nameRect.height());
            exitMetaRect = QRect(metaRect.right() - exitWidth + 1, metaRect.top(),
                                 exitWidth, metaRect.height());
        }

        const int chipSpace = visual.chip.isEmpty() ? 0 : chipWidth(smallMetrics, visual.chip) + kChipGap;
        painter->setFont(bold);
        painter->setPen(visual.running && !selected ? colors.accent : ink);
        const QString name = boldMetrics.elidedText(visual.name, Qt::ElideRight,
                                                    qMax(0, leftNameRect.width() - chipSpace));
        painter->drawText(leftNameRect, Qt::AlignLeft | Qt::AlignVCenter, name);

        if (!visual.chip.isEmpty()) {
            const int chipX = leftNameRect.left() + boldMetrics.horizontalAdvance(name) + kChipGap;
            const int chipH = smallMetrics.height() + 2;
            const QRect chipRect(chipX, leftNameRect.center().y() - chipH / 2 + 1,
                                 chipWidth(smallMetrics, visual.chip), chipH);
            if (chipRect.right() <= leftNameRect.right())
                drawChip(painter, chipRect, visual.chip, small,
                         selected ? subtle : colors.border, muted);
        }

        painter->setFont(meta);
        painter->setPen(muted);
        painter->drawText(addressRect, Qt::AlignLeft | Qt::AlignVCenter,
                          metaMetrics.elidedText(visual.address, Qt::ElideRight, addressRect.width()));

        if (showExit) {
            // EXIT is a secondary right-hand cluster, not punctuation in the
            // server address. This keeps the address scannable and aligns the
            // egress with the metric columns beside it.
            painter->setFont(caption);
            painter->setPen(subtle);
            painter->drawText(exitNameRect, Qt::AlignRight | Qt::AlignVCenter, tr("exit"));

            const int countryGap = (!visual.country.isEmpty() && !visual.exitIp.isEmpty()) ? 7 : 0;
            const int ipRoom = qMax(0, exitMetaRect.width() - badgeWidth - countryGap);
            const QString shownIp = metaMetrics.elidedText(visual.exitIp, Qt::ElideMiddle, ipRoom);
            const int ipWidth = metaMetrics.horizontalAdvance(shownIp);
            const int totalWidth = badgeWidth + countryGap + ipWidth;
            int x = exitMetaRect.right() - totalWidth + 1;

            if (!visual.country.isEmpty()) {
                const int badgeH = captionMetrics.height() + 2;
                const QRect badge(x, exitMetaRect.center().y() - badgeH / 2,
                                  badgeWidth, badgeH);
                drawBadge(painter, badge, visual.country, caption,
                          selected ? colors.selectionBorder : colors.surfaceHover, muted);
                x = badge.right() + 1 + countryGap;
            }
            if (!shownIp.isEmpty()) {
                painter->setFont(meta);
                painter->setPen(selected ? opt.palette.color(QPalette::HighlightedText)
                                         : colors.accentHover);
                painter->drawText(QRect(x, exitMetaRect.top(), ipWidth, exitMetaRect.height()),
                                  Qt::AlignLeft | Qt::AlignVCenter, shownIp);
            }
        }
        break;
    }
    case ProfilesTableModel::ColcPing: {
        const QColor pingInk = selected ? opt.palette.color(QPalette::HighlightedText)
                                        : latencyColor(visual.latencyMs, colors);
        drawStack(painter, cell, visual.latency, pingInk, bold,
                  visual.udp.isEmpty() ? QString() : tr("UDP %1").arg(visual.udp),
                  visual.udpDegraded && !selected ? QColor(QStringLiteral("#D9A441")) : subtle,
                  small, subtle);
        break;
    }
    case ProfilesTableModel::ColcSpeed:
        drawStack(painter, cell,
                  visual.speedDown.isEmpty() ? QString() : QStringLiteral("↓ ") + visual.speedDown, muted, opt.font,
                  visual.speedUp.isEmpty() ? QString() : QStringLiteral("↑ ") + visual.speedUp, subtle, small,
                  subtle);
        break;
    case ProfilesTableModel::ColcTraffic:
        drawStack(painter, cell,
                  visual.trafficDown.isEmpty() ? QString() : QStringLiteral("↓ ") + visual.trafficDown, muted, opt.font,
                  visual.trafficUp.isEmpty() ? QString() : QStringLiteral("↑ ") + visual.trafficUp, subtle, small,
                  subtle);
        break;
    default:
        break;
    }

    painter->restore();
}

QSize ProfileRowDelegate::sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const {
    const auto visual = index.data(ProfilesTableModel::RowVisualRole).value<ProfilesTableModel::RowVisual>();
    const QFontMetrics boldMetrics{primaryFont(option.font)};
    const QFontMetrics smallMetrics{secondaryFont(option.font)};
    const int height = qMax(ProfileRowDelegate::RowHeight,
                            boldMetrics.height() + QFontMetrics(metaFont(option.font)).height() + kLineGap + 12);

    const QFontMetrics plainMetrics{option.font};
    const auto stackWidth = [&](const QString &top, const QString &bottom) {
        return qMax(plainMetrics.horizontalAdvance(top), smallMetrics.horizontalAdvance(bottom));
    };
    // A floor per metric column: ResizeToContents measures whichever rows it feels
    // like, and a column sized from an untested row clips the tested ones later.
    const auto floorWidth = [&](const QString &plain, const QString &note) {
        return qMax(plainMetrics.horizontalAdvance(plain), smallMetrics.horizontalAdvance(note));
    };

    int width = 0;
    switch (index.column()) {
    case ProfilesTableModel::ColcServer:
        // The name column stretches, so this is a floor that still preserves a
        // readable address beside the optional right-aligned EXIT cluster.
        width = qMax(boldMetrics.horizontalAdvance(visual.name) + chipWidth(smallMetrics, visual.chip) + kChipGap,
                     smallMetrics.horizontalAdvance(visual.address)
                         + ((!visual.country.isEmpty() || !visual.exitIp.isEmpty())
                                ? kExitGap + kExitMinWidth : 0));
        break;
    case ProfilesTableModel::ColcPing:
        width = qMax(floorWidth(QStringLiteral("9999 ms"), tr("UDP %1").arg(QStringLiteral("999 ms ±99 / 99%"))),
                     qMax(boldMetrics.horizontalAdvance(visual.latency),
                          smallMetrics.horizontalAdvance(visual.udp.isEmpty() ? QString() : tr("UDP %1").arg(visual.udp))));
        break;
    case ProfilesTableModel::ColcSpeed:
        width = qMax(floorWidth(QStringLiteral("↓ 9999 Mbps"), QStringLiteral("↑ 9999 Mbps")),
                     stackWidth(QStringLiteral("↓ ") + visual.speedDown, QStringLiteral("↑ ") + visual.speedUp));
        break;
    case ProfilesTableModel::ColcTraffic:
        width = qMax(floorWidth(QStringLiteral("↓ 999.99 MiB"), QStringLiteral("↑ 999.99 MiB")),
                     stackWidth(QStringLiteral("↓ ") + visual.trafficDown, QStringLiteral("↑ ") + visual.trafficUp));
        break;
    default:
        break;
    }
    return {width + kPadX * 2, height};
}
