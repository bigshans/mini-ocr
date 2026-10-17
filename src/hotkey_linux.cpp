#include "hotkey.h"

struct GlobalHotkey::Impl {
    std::function<void()> callback;
};

GlobalHotkey::GlobalHotkey() : impl_(new Impl) {}

GlobalHotkey::~GlobalHotkey() {
    delete impl_;
}

bool GlobalHotkey::registerHotkey(const QString&, QString* error) {
    if (error) {
        *error = QStringLiteral("Linux 使用 KDE 系统设置中的自定义快捷键");
    }
    return false;
}

void GlobalHotkey::unregister() {}

void GlobalHotkey::setCallback(std::function<void()> callback) {
    impl_->callback = std::move(callback);
}
