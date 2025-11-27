#include "include/ui/profile/edit_shadowsocks.h"

#include "include/configs/proxy/Preset.hpp"

EditShadowSocks::EditShadowSocks(QWidget *parent) : QWidget(parent),
                                                    ui(new Ui::EditShadowSocks) {
    ui->setupUi(this);
    ui->method->addItems(Preset::SingBox::ShadowsocksMethods);
}

EditShadowSocks::~EditShadowSocks() {
    delete ui;
}

void EditShadowSocks::onStart(std::shared_ptr<Configs::ProxyEntity> _ent) {
    this->ent = _ent;
    auto outbound = this->ent->ShadowSocks();

    ui->method->setCurrentText(outbound->method);
    ui->uot->setCurrentIndex(outbound->uot);
    ui->password->setText(outbound->password);
    ui->plugin->setCurrentText(outbound->plugin);
    ui->plugin_opts->setText(outbound->plugin_opts);
}

bool EditShadowSocks::onEnd() {
    auto outbound = this->ent->ShadowSocks();

    outbound->method = ui->method->currentText();
    outbound->password = ui->password->text();
    outbound->uot = ui->uot->currentIndex();
    outbound->plugin = ui->plugin->currentText();
    outbound->plugin_opts = ui->plugin_opts->text();

    return true;
}
