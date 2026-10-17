#pragma once

#include <QString>

#include <functional>

class GlobalHotkey {
public:
    GlobalHotkey();
    ~GlobalHotkey();

    GlobalHotkey(const GlobalHotkey&) = delete;
    GlobalHotkey& operator=(const GlobalHotkey&) = delete;

    bool registerHotkey(const QString& sequence, QString* error);
    void unregister();
    void setCallback(std::function<void()> callback);

private:
    struct Impl;
    Impl* impl_;
};
