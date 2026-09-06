#include "include/ui/profile/edit_direct.h"

EditDirect::EditDirect(QWidget *parent)
    : QWidget(parent),
      ui(new Ui::EditDirect) {
    ui->setupUi(this);
}

EditDirect::~EditDirect() {
    delete ui;
}

void EditDirect::onStart(std::shared_ptr<Configs::Profile> _ent) {
    this->ent = _ent;
}

bool EditDirect::onEnd() {
    return true;
}