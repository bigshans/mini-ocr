#include "hotkey.h"

#include <QKeyCombination>
#include <QKeySequence>
#include <QWidget>

#include <windows.h>

#ifndef MOD_NOREPEAT
#define MOD_NOREPEAT 0x4000
#endif

#include <functional>
#include <utility>

namespace {

class HotkeyWindow : public QWidget {
public:
    std::function<void()> callback;

    bool nativeEvent(const QByteArray& eventType, void* message, qintptr* result) override {
        if (eventType == "windows_generic_MSG") {
            MSG* msg = static_cast<MSG*>(message);
            if (msg && msg->message == WM_HOTKEY) {
                if (callback) callback();
                if (result) *result = 0;
                return true;
            }
        }
        return QWidget::nativeEvent(eventType, message, result);
    }
};

UINT virtualKeyForQtKey(int key) {
    if (key >= Qt::Key_A && key <= Qt::Key_Z) return static_cast<UINT>('A' + key - Qt::Key_A);
    if (key >= Qt::Key_0 && key <= Qt::Key_9) return static_cast<UINT>('0' + key - Qt::Key_0);
    if (key >= Qt::Key_F1 && key <= Qt::Key_F24) {
        return static_cast<UINT>(VK_F1 + key - Qt::Key_F1);
    }

    switch (key) {
        case Qt::Key_Space: return VK_SPACE;
        case Qt::Key_Tab: return VK_TAB;
        case Qt::Key_Return: return VK_RETURN;
        case Qt::Key_Enter: return VK_RETURN;
        case Qt::Key_Escape: return VK_ESCAPE;
        case Qt::Key_Backspace: return VK_BACK;
        case Qt::Key_Delete: return VK_DELETE;
        case Qt::Key_Insert: return VK_INSERT;
        case Qt::Key_Home: return VK_HOME;
        case Qt::Key_End: return VK_END;
        case Qt::Key_PageUp: return VK_PRIOR;
        case Qt::Key_PageDown: return VK_NEXT;
        case Qt::Key_Left: return VK_LEFT;
        case Qt::Key_Right: return VK_RIGHT;
        case Qt::Key_Up: return VK_UP;
        case Qt::Key_Down: return VK_DOWN;
        case Qt::Key_Print: return VK_SNAPSHOT;
        case Qt::Key_Pause: return VK_PAUSE;
        default: break;
    }

    if (key > 0 && key < 0x10000) {
        const SHORT scan = VkKeyScanW(static_cast<WCHAR>(key));
        if (scan != -1) return static_cast<UINT>(LOBYTE(scan));
    }
    return 0;
}

}  // namespace

struct GlobalHotkey::Impl {
    HotkeyWindow* window = nullptr;
    int id = 1;
    bool registered = false;
    std::function<void()> callback;
};

GlobalHotkey::GlobalHotkey() : impl_(new Impl) {}

GlobalHotkey::~GlobalHotkey() {
    unregister();
    delete impl_->window;
    delete impl_;
}

bool GlobalHotkey::registerHotkey(const QString& sequence, QString* error) {
    unregister();

    const QKeySequence seq(sequence);
    if (seq.isEmpty()) {
        if (error) *error = QStringLiteral("快捷键为空");
        return false;
    }

    const QKeyCombination combo = seq[0];
    const Qt::KeyboardModifiers modifiers = combo.keyboardModifiers();
    const int key = combo.key();

    UINT winModifiers = 0;
    if (modifiers & Qt::ControlModifier) winModifiers |= MOD_CONTROL;
    if (modifiers & Qt::AltModifier) winModifiers |= MOD_ALT;
    if (modifiers & Qt::ShiftModifier) winModifiers |= MOD_SHIFT;
    if (modifiers & Qt::MetaModifier) winModifiers |= MOD_WIN;

    if (winModifiers == 0) {
        if (error) *error = QStringLiteral("快捷键至少需要包含 Ctrl、Alt、Shift 或 Win");
        return false;
    }

    const UINT vk = virtualKeyForQtKey(key);
    if (vk == 0) {
        if (error) *error = QStringLiteral("无法映射该按键到 Windows 虚拟键");
        return false;
    }

    if (!impl_->window) {
        impl_->window = new HotkeyWindow;
        impl_->window->setAttribute(Qt::WA_DontShowOnScreen);
        impl_->window->setWindowFlags(Qt::Tool);
        impl_->window->winId();
        impl_->window->callback = [this] {
            if (impl_->callback) impl_->callback();
        };
    }

    const HWND hwnd = reinterpret_cast<HWND>(impl_->window->winId());
    if (!RegisterHotKey(hwnd, impl_->id, winModifiers | MOD_NOREPEAT, vk)) {
        if (error) *error = QStringLiteral("快捷键已被其他程序占用");
        return false;
    }

    impl_->registered = true;
    return true;
}

void GlobalHotkey::unregister() {
    if (!impl_->registered || !impl_->window) return;
    UnregisterHotKey(reinterpret_cast<HWND>(impl_->window->winId()), impl_->id);
    impl_->registered = false;
}

void GlobalHotkey::setCallback(std::function<void()> callback) {
    impl_->callback = std::move(callback);
    if (impl_->window) {
        impl_->window->callback = [this] {
            if (impl_->callback) impl_->callback();
        };
    }
}
