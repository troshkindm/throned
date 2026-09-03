#include "include/ui/profile/edit_wireguard_amnezia.h"

#include <QGuiApplication>
#include <QLayout>
#include <QScreen>
#include <QScrollBar>

#include "include/global/GuiUtils.hpp"
#include "include/global/Utils.hpp"

void WireguardAmneziaOptions::load(const Configs::wireguard *outbound)
{
    if (outbound == nullptr) return;
    jc = outbound->jc;
    jmin = outbound->jmin;
    jmax = outbound->jmax;
    s1 = outbound->s1;
    s2 = outbound->s2;
    s3 = outbound->s3;
    s4 = outbound->s4;
    h1 = outbound->h1;
    h2 = outbound->h2;
    h3 = outbound->h3;
    h4 = outbound->h4;
    i1 = outbound->i1;
    i2 = outbound->i2;
    i3 = outbound->i3;
    i4 = outbound->i4;
    i5 = outbound->i5;
    headerProtectionKey = outbound->header_protection_key;
    contentPaddingAddition = outbound->content_padding_addition;
    rekeyAfterTime = outbound->rekey_after_time;
    rekeyTimeout = outbound->rekey_timeout;
    rejectAfterTime = outbound->reject_after_time;
    keepaliveTimeout = outbound->keepalive_timeout;
    maxHandshakeAttempts = outbound->max_handshake_attempts;
    randomTrailers = outbound->random_trailers;
    disableCookies = outbound->disable_cookies;
}

void WireguardAmneziaOptions::apply(Configs::wireguard *outbound) const
{
    if (outbound == nullptr) return;
    outbound->jc = jc;
    outbound->jmin = jmin;
    outbound->jmax = jmax;
    outbound->s1 = s1;
    outbound->s2 = s2;
    outbound->s3 = s3;
    outbound->s4 = s4;
    outbound->h1 = h1;
    outbound->h2 = h2;
    outbound->h3 = h3;
    outbound->h4 = h4;
    outbound->i1 = i1;
    outbound->i2 = i2;
    outbound->i3 = i3;
    outbound->i4 = i4;
    outbound->i5 = i5;
    outbound->header_protection_key = headerProtectionKey;
    outbound->content_padding_addition = contentPaddingAddition;
    outbound->rekey_after_time = rekeyAfterTime;
    outbound->rekey_timeout = rekeyTimeout;
    outbound->reject_after_time = rejectAfterTime;
    outbound->keepalive_timeout = keepaliveTimeout;
    outbound->max_handshake_attempts = maxHandshakeAttempts;
    outbound->random_trailers = randomTrailers;
    outbound->disable_cookies = disableCookies;
}

EditWireguardAmnezia::EditWireguardAmnezia(QWidget *parent, WireguardAmneziaOptions *options)
    : QDialog(parent)
    , ui(new Ui::EditWireguardAmnezia)
    , options(options)
{
    ui->setupUi(this);

    ui->jc->setText(Int2String(options->jc));
    ui->jmin->setText(Int2String(options->jmin));
    ui->jmax->setText(Int2String(options->jmax));
    ui->s1->setText(Int2String(options->s1));
    ui->s2->setText(Int2String(options->s2));
    ui->s3->setText(Int2String(options->s3));
    ui->s4->setText(Int2String(options->s4));
    ui->h1->setText(options->h1);
    ui->h2->setText(options->h2);
    ui->h3->setText(options->h3);
    ui->h4->setText(options->h4);
    ui->i1->setText(options->i1);
    ui->i2->setText(options->i2);
    ui->i3->setText(options->i3);
    ui->i4->setText(options->i4);
    ui->i5->setText(options->i5);
    ui->header_protection_key->setText(options->headerProtectionKey);
    ui->content_padding_addition->setText(options->contentPaddingAddition);
    ui->rekey_after_time->setText(options->rekeyAfterTime);
    ui->rekey_timeout->setText(options->rekeyTimeout);
    ui->reject_after_time->setText(options->rejectAfterTime);
    ui->keepalive_timeout->setText(options->keepaliveTimeout);
    ui->max_handshake_attempts->setText(options->maxHandshakeAttempts);
    ui->random_trailers->setChecked(options->randomTrailers);
    ui->disable_cookies->setChecked(options->disableCookies);

    connect(ui->buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(ui->buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);

    ADD_ASTERISK(this)

    // QScrollArea::sizeHint() caps at 36x24 font cells, so size the window off the content instead.
    for (auto *nested: ui->scroll_content->findChildren<QLayout *>()) nested->activate();
    const auto margins = layout()->contentsMargins();
    const auto frame = 2 * ui->scroll->frameWidth();
    const auto wanted = ui->scroll->widget()->sizeHint() +
                        QSize(frame + ui->scroll->verticalScrollBar()->sizeHint().width() +
                              margins.left() + margins.right(),
                              frame + margins.top() + margins.bottom() +
                              ui->buttonBox->sizeHint().height() + layout()->spacing());

    const auto *scr = screen() != nullptr ? screen() : QGuiApplication::primaryScreen();
    resize(scr != nullptr ? wanted.boundedTo(scr->availableGeometry().size()) : wanted);
}

EditWireguardAmnezia::~EditWireguardAmnezia()
{
    delete ui;
}

void EditWireguardAmnezia::accept() {
    options->jc = ui->jc->text().trimmed().toInt();
    options->jmin = ui->jmin->text().trimmed().toInt();
    options->jmax = ui->jmax->text().trimmed().toInt();
    options->s1 = ui->s1->text().trimmed().toInt();
    options->s2 = ui->s2->text().trimmed().toInt();
    options->s3 = ui->s3->text().trimmed().toInt();
    options->s4 = ui->s4->text().trimmed().toInt();
    options->h1 = ui->h1->text().trimmed();
    options->h2 = ui->h2->text().trimmed();
    options->h3 = ui->h3->text().trimmed();
    options->h4 = ui->h4->text().trimmed();
    options->i1 = ui->i1->text().trimmed();
    options->i2 = ui->i2->text().trimmed();
    options->i3 = ui->i3->text().trimmed();
    options->i4 = ui->i4->text().trimmed();
    options->i5 = ui->i5->text().trimmed();
    options->headerProtectionKey = ui->header_protection_key->text().trimmed();
    options->contentPaddingAddition = ui->content_padding_addition->text().trimmed();
    options->rekeyAfterTime = ui->rekey_after_time->text().trimmed();
    options->rekeyTimeout = ui->rekey_timeout->text().trimmed();
    options->rejectAfterTime = ui->reject_after_time->text().trimmed();
    options->keepaliveTimeout = ui->keepalive_timeout->text().trimmed();
    options->maxHandshakeAttempts = ui->max_handshake_attempts->text().trimmed();
    options->randomTrailers = ui->random_trailers->isChecked();
    options->disableCookies = ui->disable_cookies->isChecked();

    QDialog::accept();
}
