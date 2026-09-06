#include <algorithm>
#include <cmath>
#include <cstdio>

#include <QColor>
#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QPainter>
#include <QStringList>

namespace {

QString argument(const QStringList &arguments, const QString &name) {
    const int at = arguments.indexOf(name);
    return at >= 0 && at + 1 < arguments.size() ? arguments.at(at + 1) : QString();
}

int fail(const QString &message) {
    std::fprintf(stderr, "%s\n", qPrintable(message));
    return 2;
}

} // namespace

int main(int argc, char *argv[]) {
    QStringList arguments;
    for (int i = 0; i < argc; ++i) arguments.append(QString::fromLocal8Bit(argv[i]));

    const QString expectedPath = argument(arguments, QStringLiteral("--expected"));
    const QString actualPath = argument(arguments, QStringLiteral("--actual"));
    const QString diffPath = argument(arguments, QStringLiteral("--diff"));
    if (expectedPath.isEmpty() || actualPath.isEmpty() || diffPath.isEmpty())
        return fail(QStringLiteral("usage: throned_snapshot_compare --expected file --actual file --diff file"));

    bool toleranceOk = false;
    const int channelTolerance = argument(arguments, QStringLiteral("--channel-tolerance")).toInt(&toleranceOk);
    bool ratioOk = false;
    const double allowedRatio = argument(arguments, QStringLiteral("--max-different-ratio")).toDouble(&ratioOk);
    const int tolerance = toleranceOk ? std::clamp(channelTolerance, 0, 255) : 2;
    const double maximumRatio = ratioOk ? std::clamp(allowedRatio, 0.0, 1.0) : 0.00005;

    QImage expected(expectedPath);
    QImage actual(actualPath);
    if (expected.isNull()) return fail(QStringLiteral("could not read baseline: %1").arg(expectedPath));
    if (actual.isNull()) return fail(QStringLiteral("could not read actual image: %1").arg(actualPath));

    if (expected.size() != actual.size()) {
        QImage diff(expected.width() + actual.width(), std::max(expected.height(), actual.height()),
                    QImage::Format_ARGB32_Premultiplied);
        diff.fill(QColor(QStringLiteral("#ff00ff")));
        QPainter painter(&diff);
        painter.drawImage(0, 0, expected);
        painter.drawImage(expected.width(), 0, actual);
        painter.end();
        QDir().mkpath(QFileInfo(diffPath).absolutePath());
        diff.save(diffPath, "PNG");
        std::fprintf(stderr, "size mismatch: expected %dx%d, actual %dx%d\n",
                     expected.width(), expected.height(), actual.width(), actual.height());
        return 1;
    }

    expected = expected.convertToFormat(QImage::Format_RGBA8888);
    actual = actual.convertToFormat(QImage::Format_RGBA8888);
    QImage diff(actual.size(), QImage::Format_RGBA8888);
    qsizetype different = 0;
    int largestDelta = 0;
    for (int y = 0; y < actual.height(); ++y) {
        const auto *expectedLine = reinterpret_cast<const QRgb *>(expected.constScanLine(y));
        const auto *actualLine = reinterpret_cast<const QRgb *>(actual.constScanLine(y));
        auto *diffLine = reinterpret_cast<QRgb *>(diff.scanLine(y));
        for (int x = 0; x < actual.width(); ++x) {
            const QColor e = QColor::fromRgba(expectedLine[x]);
            const QColor a = QColor::fromRgba(actualLine[x]);
            const int delta = std::max({std::abs(e.red() - a.red()), std::abs(e.green() - a.green()),
                                        std::abs(e.blue() - a.blue()), std::abs(e.alpha() - a.alpha())});
            largestDelta = std::max(largestDelta, delta);
            if (delta > tolerance) {
                ++different;
                diffLine[x] = qRgba(255, 0, 255, 255);
            } else {
                const int gray = qGray(a.rgb());
                diffLine[x] = qRgba(gray / 2, gray / 2, gray / 2, 255);
            }
        }
    }

    const qsizetype pixels = qsizetype(actual.width()) * actual.height();
    const double ratio = pixels == 0 ? 0.0 : double(different) / double(pixels);
    std::printf("different=%lld pixels=%lld ratio=%.8f largest_delta=%d tolerance=%d limit=%.8f\n",
                static_cast<long long>(different), static_cast<long long>(pixels), ratio,
                largestDelta, tolerance, maximumRatio);
    if (ratio <= maximumRatio) return 0;

    QDir().mkpath(QFileInfo(diffPath).absolutePath());
    if (!diff.save(diffPath, "PNG")) return fail(QStringLiteral("could not write diff: %1").arg(diffPath));
    return 1;
}
