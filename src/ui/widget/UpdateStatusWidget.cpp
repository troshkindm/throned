#include "include/ui/widget/UpdateStatusWidget.h"

#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QRegularExpression>
#include <QSizePolicy>
#include <QVBoxLayout>

#include "include/global/Utils.hpp"
#include "include/ui/setting/ThemeManager.hpp"
#include "include/ui/widget/MaterialIcon.h"

UpdateStatusWidget::UpdateStatusWidget(QWidget *parent) : QFrame(parent) {
    setObjectName(QStringLiteral("updateStatus"));
    setFixedHeight(50);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(14, 6, 10, 5);
    outer->setSpacing(4);

    auto *row = new QHBoxLayout;
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(9);

    icon_ = new QLabel(this);
    icon_->setObjectName(QStringLiteral("updateStatusIcon"));
    icon_->setFixedSize(20, 20);
    row->addWidget(icon_);

    title_ = new QLabel(this);
    title_->setObjectName(QStringLiteral("updateStatusTitle"));
    title_->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Preferred);
    row->addWidget(title_);

    detail_ = new QLabel(this);
    detail_->setObjectName(QStringLiteral("updateStatusDetail"));
    detail_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    row->addWidget(detail_, 1);

    primary_ = new QPushButton(this);
    primary_->setObjectName(QStringLiteral("updatePrimaryButton"));
    primary_->setCursor(Qt::PointingHandCursor);
    primary_->setFixedHeight(28);
    row->addWidget(primary_);

    secondary_ = new QPushButton(this);
    secondary_->setObjectName(QStringLiteral("updateSecondaryButton"));
    secondary_->setCursor(Qt::PointingHandCursor);
    secondary_->setFixedHeight(28);
    row->addWidget(secondary_);
    outer->addLayout(row);

    progress_ = new QProgressBar(this);
    progress_->setObjectName(QStringLiteral("updateProgress"));
    progress_->setTextVisible(false);
    progress_->setFixedHeight(3);
    outer->addWidget(progress_);

    connect(primary_, &QPushButton::clicked, this, [this] {
        if (state_ == State::Ready) emit restartRequested();
        else if (state_ == State::Error) emit retryRequested();
    });
    connect(secondary_, &QPushButton::clicked, this, &UpdateStatusWidget::dismiss);
    connect(themeManager(), &ThemeManager::themeChanged, this, [this] { refreshIcon(); });

    dismiss();
}

QString UpdateStatusWidget::displayName(const QString &assetName) {
    QString base = QFileInfo(assetName).completeBaseName();
    if (base.startsWith(QStringLiteral("Throned-"), Qt::CaseInsensitive))
        base.remove(0, QStringLiteral("Throned-").size());

    static const QRegularExpression platformSuffix(QStringLiteral(
        R"(-(windows(?:legacy)?(?:32|64)|windows-arm64|linux-(?:amd64|arm64)|macos-(?:amd64|arm64))$)"),
        QRegularExpression::CaseInsensitiveOption);
    base.remove(platformSuffix);
    return base.isEmpty() ? QStringLiteral("Throned") : QStringLiteral("Throned %1").arg(base);
}

void UpdateStatusWidget::setState(State state) {
    state_ = state;
    setProperty("updateState", static_cast<int>(state));
    style()->unpolish(this);
    style()->polish(this);

    const bool actionable = state == State::Ready || state == State::Error;
    primary_->setVisible(actionable);
    secondary_->setVisible(actionable);
    progress_->setVisible(state == State::Downloading || state == State::Preparing);
    setVisible(state != State::Hidden);
    refreshIcon();
}

void UpdateStatusWidget::refreshIcon() {
    if (icon_ == nullptr || state_ == State::Hidden) return;
    const auto colors = themeManager()->Colors();
    MaterialIcon::Glyph glyph = MaterialIcon::Glyph::ArrowDown;
    QColor color = colors.accent;
    if (state_ == State::Preparing) glyph = MaterialIcon::Glyph::Reload;
    if (state_ == State::Ready) {
        glyph = MaterialIcon::Glyph::Check;
        color = colors.success;
    }
    if (state_ == State::Error) {
        glyph = MaterialIcon::Glyph::Block;
        color = colors.danger;
    }
    icon_->setPixmap(MaterialIcon::pixmap(glyph, color, 19));
}

void UpdateStatusWidget::showDownloading(const QString &assetName, qint64 received, qint64 total) {
    setState(State::Downloading);
    title_->setText(tr("Downloading %1").arg(displayName(assetName)));
    if (total > 0) {
        const int value = static_cast<int>(qBound<qint64>(qint64{0}, received * 1000 / total, qint64{1000}));
        progress_->setRange(0, 1000);
        progress_->setValue(value);
        detail_->setText(tr("%1% · %2 of %3")
                             .arg(value / 10)
                             .arg(ReadableSize(qMax<qint64>(0, received)), ReadableSize(total)));
    } else {
        progress_->setRange(0, 0);
        detail_->setText(received > 0
            ? tr("%1 downloaded").arg(ReadableSize(received))
            : tr("Starting download…"));
    }
    detail_->setToolTip(detail_->text());
}

void UpdateStatusWidget::showPreparing(const QString &assetName) {
    setState(State::Preparing);
    title_->setText(tr("Preparing %1").arg(displayName(assetName)));
    detail_->setText(tr("Saving the downloaded package…"));
    detail_->setToolTip(detail_->text());
    progress_->setRange(0, 0);
}

void UpdateStatusWidget::showReady(const QString &assetName) {
    setState(State::Ready);
    title_->setText(tr("%1 is ready").arg(displayName(assetName)));
    detail_->setText(tr("Restart when convenient to install it."));
    detail_->setToolTip(detail_->text());
    primary_->setText(tr("Restart now"));
    secondary_->setText(tr("Later"));
}

void UpdateStatusWidget::showError(const QString &message) {
    setState(State::Error);
    title_->setText(tr("Update download failed"));
    detail_->setText(message.simplified());
    detail_->setToolTip(message);
    primary_->setText(tr("Retry"));
    secondary_->setText(tr("Dismiss"));
}

void UpdateStatusWidget::dismiss() {
    state_ = State::Hidden;
    setProperty("updateState", static_cast<int>(State::Hidden));
    hide();
}
