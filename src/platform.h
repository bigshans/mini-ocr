#pragma once

#include <QString>

namespace Platform {

QString serverName();
QString uiFontFamily();
bool copyTextToClipboard(const QString& text);

}  // namespace Platform
