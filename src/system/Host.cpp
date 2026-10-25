// SPDX-License-Identifier: GPL-3.0-only

#include "Host.h"

namespace Host {

QString systemName()
{
    switch (operatingSystem()) {
    case OperatingSystem::Windows:
        return QStringLiteral("windows");
    case OperatingSystem::MacOS:
        return QStringLiteral("macos");
    case OperatingSystem::Linux:
        return QStringLiteral("linux");
    }
    return {};
}

} // namespace Host
