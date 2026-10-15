#include "include/ui/widget/TestProgressWidget.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QProgressBar>
#include <QToolButton>
#include <QVBoxLayout>

#include "include/ui/setting/ThemeManager.hpp"
#include "include/ui/widget/ElidedLabel.h"
#include "include/ui/widget/MaterialIcon.h"

TestProgressWidget::TestProgressWidget(QWidget *parent) : QFrame(parent) {
    setObjectName(QStringLiteral("testProgress"));
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(14, 8, 12, 9);
    outer->setSpacing(6);
    auto *header = new QHBoxLayout;
    header->setSpacing(8);
    icon_ = new QLabel(this);
    icon_->setFixedSize(18, 18);
    header->addWidget(icon_);
    title_ = new ElidedLabel(this);
    title_->setObjectName(QStringLiteral("testProgressTitle"));
    title_->setTextFormat(Qt::PlainText);
    title_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    header->addWidget(title_, 1);
    counter_ = new QLabel(this);
    counter_->setObjectName(QStringLiteral("testProgressCounter"));
    header->addWidget(counter_);
    stop_ = new QToolButton(this);
    stop_->setObjectName(QStringLiteral("testProgressStop"));
    stop_->setToolTip(tr("Stop testing"));
    stop_->setAccessibleName(stop_->toolTip());
    stop_->setCursor(Qt::PointingHandCursor);
    stop_->setFixedSize(24, 24);
    header->addWidget(stop_);
    outer->addLayout(header);
    auto *detail = new QHBoxLayout;
    detail->setSpacing(10);
    profile_ = new ElidedLabel(this);
    profile_->setObjectName(QStringLiteral("testProgressProfile"));
    profile_->setTextFormat(Qt::PlainText);
    profile_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    detail->addWidget(profile_, 1);
    download_ = new QLabel(this);
    download_->setObjectName(QStringLiteral("testProgressDownload"));
    download_->setTextFormat(Qt::PlainText);
    detail->addWidget(download_);
    upload_ = new QLabel(this);
    upload_->setObjectName(QStringLiteral("testProgressUpload"));
    upload_->setTextFormat(Qt::PlainText);
    detail->addWidget(upload_);
    outer->addLayout(detail);
    progress_ = new QProgressBar(this);
    progress_->setObjectName(QStringLiteral("testProgressBar"));
    progress_->setTextVisible(false);
    progress_->setFixedHeight(4);
    outer->addWidget(progress_);

    connect(stop_, &QToolButton::clicked, this, [this] {
        stopping_ = true;
        stop_->setEnabled(false);
        title_->setText(tr("Stopping tests…"));
        emit stopRequested();
    });
    connect(themeManager(), &ThemeManager::themeChanged, this, [this] { refreshIcon(); });
    themeManager()->RegisterStyle(this, QStringLiteral(R"(
QFrame#testProgress { background: #1B1E23; border: none; border-top: 1px solid #2F3136; }
QFrame#testProgress QLabel { background: transparent; border: none; }
QLabel#testProgressTitle { color: #F1F3F5; font-weight: 600; }
QLabel#testProgressCounter, QLabel#testProgressProfile { color: #A4ABB4; font-size: 11px; }
QLabel#testProgressDownload { color: #237AE9; font-size: 11px; }
QLabel#testProgressUpload { color: #2EBC75; font-size: 11px; }
QToolButton#testProgressStop { background: transparent; border: none; border-radius: 6px; }
QToolButton#testProgressStop:hover { background: #292D33; }
QProgressBar#testProgressBar { background: #2F3136; border: none; border-radius: 2px; }
QProgressBar#testProgressBar::chunk { background: #237AE9; border-radius: 2px; }
)"));
    refreshIcon();
    hide();
}

void TestProgressWidget::refreshIcon() {
    const auto colors = themeManager()->Colors();
    icon_->setPixmap(MaterialIcon::pixmap(MaterialIcon::Glyph::Bolt, colors.accent, 18));
    stop_->setIcon(MaterialIcon::icon(MaterialIcon::Glyph::Close, colors.textMuted, 15));
}

void TestProgressWidget::setSnapshot(const DataViewHtmlGenerator::TestPanelState &state) {
    if (!state.visible) {
        stopping_ = false;
        stop_->setEnabled(true);
        hide();
        return;
    }
    using Kind = DataViewHtmlGenerator::TestPanelState::Kind;
    const QString title = state.kind == Kind::Url       ? tr("TCP latency")
                          : state.kind == Kind::Udp     ? tr("UDP latency")
                          : state.kind == Kind::Ip      ? tr("IP check")
                          : state.kind == Kind::Country ? tr("Country test")
                                                        : tr("Speed test");
    title_->setText(stopping_ ? tr("Stopping tests…") : title);
    title_->setToolTip(title_->text());
    progress_->setAccessibleName(title);
    const bool batch = state.total > 1;
    progress_->setRange(0, batch ? state.total : 0);
    if (batch) progress_->setValue(state.completed);
    counter_->setText(batch ? tr("%1 / %2 · %3%").arg(state.completed).arg(state.total).arg(100LL * state.completed / state.total) : QString());
    counter_->setVisible(batch);
    const QString detail = state.kind == Kind::Country && !state.server.isEmpty()
                               ? QStringList{state.profileName, state.server}.join(QStringLiteral(" · "))
                               : state.profileName;
    profile_->setText(detail);
    profile_->setToolTip(QStringList{state.profileName, state.server}.join(QLatin1Char('\n')).trimmed());
    profile_->setVisible(!detail.isEmpty());
    download_->setText(state.dlSpeed.isEmpty() ? QString() : QStringLiteral("↓ %1").arg(state.dlSpeed));
    download_->setVisible(!state.dlSpeed.isEmpty());
    upload_->setText(state.ulSpeed.isEmpty() ? QString() : QStringLiteral("↑ %1").arg(state.ulSpeed));
    upload_->setVisible(!state.ulSpeed.isEmpty());
    show();
}
