#pragma once

#include <QString>

namespace vofa::app {

// Installs a rotating file logger at <configDir>/logs/vofa2.log. QML and Qt
// warnings are captured too, which makes field issues diagnosable even for a
// WIN32 GUI build without a console.
void installFileLogger(const QString& configDir);

} // namespace vofa::app
