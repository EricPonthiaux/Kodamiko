#include "UnsavedEditGuard.h"

namespace UnsavedEditGuard {

QString reason(const QStringList &files)
{
    return QStringLiteral("%1 %2 unsaved changes in the user's Duet editor: editing the file on disk would lose "
                          "them. Do not retry and do not work around this (with Bash, for instance): ask the user "
                          "to save or discard their changes, then edit again.")
        .arg(files.join(QStringLiteral(", ")), files.size() == 1 ? QStringLiteral("has") : QStringLiteral("have"));
}

QJsonObject denyOutput(const QString &reason)
{
    return QJsonObject{
        {QLatin1String("hookSpecificOutput"), QJsonObject{
                                                  {QLatin1String("hookEventName"), QLatin1String("PreToolUse")},
                                                  {QLatin1String("permissionDecision"), QLatin1String("deny")},
                                                  {QLatin1String("permissionDecisionReason"), reason},
                                              }}};
}

} // namespace UnsavedEditGuard
