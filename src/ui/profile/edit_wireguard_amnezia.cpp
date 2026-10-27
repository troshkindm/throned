#include "include/ui/profile/edit_wireguard_amnezia.h"

#include <QGuiApplication>
#include <QInputDialog>
#include <QLayout>
#include <QPointer>
#include <QScreen>
#include <QScrollBar>

#include <limits>
#include <optional>

#include "include/api/RPC.h"
#include "include/ui/utils/GuiUtils.hpp"
#include "include/global/Utils.hpp"
#include "include/ui/utils/ValidationForm.h"

void WireguardAmneziaOptions::load(const Configs::wireguard *outbound) {
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

namespace {
QString lastSignatureDomain;

// Text the field's own rule rejects yields nothing, so cross rules stay quiet about it.
std::optional<qint64> amneziaEditorNumber(const QLineEdit *edit, qint64 max) {
    const QString text = edit->text().trimmed();
    if (text.isEmpty()) return 0;
    const auto value = Validation::ToInt(text);
    if (!value || *value < 0 || *value > max) return std::nullopt;
    return value;
}
} // namespace

void WireguardAmneziaOptions::apply(Configs::wireguard *outbound) const {
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
    : QDialog(parent), ui(new Ui::EditWireguardAmnezia), options(options) {
    ui->setupUi(this);
    form = new Validation::Form(this);
    form->attachStatus(ui->buttonBox);
    bindValidation();

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
    connect(ui->generate_signature, &QPushButton::clicked, this, [this] { generateSignature(); });

    ADD_ASTERISK(this)
    form->markAll();

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

EditWireguardAmnezia::~EditWireguardAmnezia() {
    delete ui;
}

void EditWireguardAmnezia::generateSignature() {
    bool ok = false;
    const auto domain = QInputDialog::getText(this, tr("QUIC Signature"), tr("Domain to imitate:"), QLineEdit::Normal,
                                              lastSignatureDomain, &ok)
                            .trimmed();
    if (!ok || domain.isEmpty()) return;
    lastSignatureDomain = domain;

    const auto originalText = ui->generate_signature->text();
    ui->generate_signature->setEnabled(false);
    ui->generate_signature->setText(tr("Generating..."));

    QPointer<EditWireguardAmnezia> self(this);
    runOnNewThread([self, domain, originalText] {
        bool rpcOK = false;
        const auto reply = API::defaultClient->GenQuicSignature(&rpcOK, domain);
        runOnUiThread([self, originalText, rpcOK, reply] {
            if (self == nullptr) return;
            self->ui->generate_signature->setText(originalText);
            self->ui->generate_signature->setEnabled(true);
            const auto error = rpcOK ? QString::fromStdString(reply.error.value_or("")) : tr("Failed to reach the core.");
            if (!error.isEmpty() || reply.packets.empty()) {
                MessageBoxWarning(tr("Failed to generate QUIC signature"), error);
                return;
            }
            const QList<QLineEdit *> fields = {self->ui->i1, self->ui->i2, self->ui->i3, self->ui->i4, self->ui->i5};
            for (qsizetype i = 0; i < fields.size(); ++i) {
                fields[i]->setText(i < static_cast<qsizetype>(reply.packets.size())
                                       ? QString::fromStdString(reply.packets[i])
                                       : QString());
            }
        });
    });
}

void EditWireguardAmnezia::bindValidation() {
    using namespace Validation;
    constexpr qint64 kIntMax = std::numeric_limits<int>::max();

    for (auto *edit: {ui->jc, ui->jmin, ui->jmax}) form->bind(edit, Rules::Int(0, kIntMax));
    for (auto *edit: {ui->s1, ui->s2, ui->s3, ui->s4}) form->bind(edit, Rules::Int(0, 65535));
    for (auto *edit: {ui->h1, ui->h2, ui->h3, ui->h4, ui->content_padding_addition, ui->rekey_after_time, ui->rekey_timeout,
                      ui->reject_after_time, ui->keepalive_timeout, ui->max_handshake_attempts}) {
        form->bind(edit, Configs::Checks::AmneziaRange);
    }
    for (auto *edit: {ui->i1, ui->i2, ui->i3, ui->i4, ui->i5}) form->bind(edit, Configs::Checks::AmneziaSignature);
    form->bind(ui->header_protection_key, Rules::Base64Key(32, false, true, Empty::Allowed));

    form->addRule({ui->h1, ui->h2, ui->h3, ui->h4}, [this] {
        return Configs::Checks::AmneziaHeaders({ui->h1->text(), ui->h2->text(), ui->h3->text(), ui->h4->text()});
    });
    form->addRule({ui->header_protection_key, ui->s1, ui->s2, ui->s3, ui->s4}, [this] {
        QList<qint64> paddings;
        for (const auto *edit: {ui->s1, ui->s2, ui->s3, ui->s4}) {
            const auto value = amneziaEditorNumber(edit, 65535);
            if (!value) return QString();
            paddings << *value;
        }
        return Configs::Checks::AmneziaHeaderProtection(ui->header_protection_key->text(), paddings);
    });
    form->addRule({ui->jmin, ui->jmax}, [this, kIntMax] {
        const auto jmin = amneziaEditorNumber(ui->jmin, kIntMax), jmax = amneziaEditorNumber(ui->jmax, kIntMax);
        if (!jmin || !jmax) return QString();
        return Configs::Checks::AmneziaJunkSizes(*jmin, *jmax); }, nullptr, Severity::Warning);
}

void EditWireguardAmnezia::accept() {
    if (!form->validateAll()) return;
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
