// SPDX-License-Identifier: GPL-3.0-only

#include "UnsavedEditGuard.h"

namespace UnsavedEditGuard {

QString reason(const QStringList &files)
{
    return QStringLiteral("%1 %2 unsaved changes in the user's Kodamiko editor: editing the file on disk would lose "
                          "them. Do not retry and do not work around this (with Bash, for instance): ask the user "
                          "to save or discard their changes, then edit again.")
        .arg(files.join(QStringLiteral(", ")), files.size() == 1 ? QStringLiteral("has") : QStringLiteral("have"));
}

} // namespace UnsavedEditGuard
