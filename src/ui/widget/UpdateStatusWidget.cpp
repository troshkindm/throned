#include "include/ui/widget/UpdateStatusWidget.h"

#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QRegularExpression>
#include <QSizePolicy>
#include <QStyle>
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
    title_->setTextFormat(Qt::PlainText);
    title_->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Preferred);
    row->addWidget(title_);

    detail_ = new QLabel(this);
    detail_->setObjectName(QStringLiteral("updateStatusDetail"));
    detail_->setTextFormat(Qt::PlainText);
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
        if (state_ == State::Ready)
            emit restartRequested();
        else if (state_ == State::Error)
            emit retryRequested();
        else if (state_ == State::Notice) {
            const QString id = activeNoticeId_;
            emit noticeActionRequested(id);
        }
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

    if (state != State::Notice) {
        activeNoticeId_.clear();
        title_->setToolTip({});
    }
    const bool actionable = state == State::Ready || state == State::Error || state == State::Notice;
    primary_->setVisible(actionable && (state != State::Notice || !notices_.value(activeNoticeId_).action.isEmpty()));
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
    if (state_ == State::Notice) {
        const auto severity = notices_.value(activeNoticeId_).severity;
        glyph = severity == Severity::Tip ? MaterialIcon::Glyph::Bell : MaterialIcon::Glyph::Shield;
        color = severity == Severity::Tip ? colors.accent : severity == Severity::Warning ? colors.warning
                                                                                          : colors.danger;
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
    const QString dismissed = activeNoticeId_;
    if (!dismissed.isEmpty()) notices_.remove(dismissed);
    setState(State::Hidden);
    if (!dismissed.isEmpty()) emit noticeDismissed(dismissed);
    showNextNotice();
}

void UpdateStatusWidget::postNotice(const Notice &notice) {
    if (notice.id.isEmpty()) return;
    notices_.insert(notice.id, notice);
    showNextNotice();
}

void UpdateStatusWidget::removeNotice(const QString &id) {
    notices_.remove(id);
    showNextNotice();
}

void UpdateStatusWidget::showNextNotice() {
    // Update progress and its actions always own the slot until explicitly dismissed.
    if (state_ != State::Hidden && state_ != State::Notice) return;
    if (notices_.isEmpty()) {
        setState(State::Hidden);
        return;
    }
    auto next = notices_.cbegin();
    for (auto it = notices_.cbegin(); it != notices_.cend(); ++it) {
        if (it->severity > next->severity ||
            (it->severity == next->severity && it->priority > next->priority)) next = it;
    }
    activeNoticeId_ = next.key();
    title_->setText(next->title);
    title_->setToolTip(next->title);
    detail_->setText(next->detail);
    detail_->setToolTip(next->detail);
    primary_->setText(next->action);
    secondary_->setText(next->dismissText.isEmpty() ? tr("Dismiss") : next->dismissText);
    setState(State::Notice);
}
