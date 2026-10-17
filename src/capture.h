#pragma once

#include <QString>

namespace Capture {
// Opens Spectacle's native KDE region selector and saves the selection to a
// temporary PNG. Returns an empty string if the user cancelled or capture failed.
QString captureRegion(QString* error);
}  // namespace Capture
