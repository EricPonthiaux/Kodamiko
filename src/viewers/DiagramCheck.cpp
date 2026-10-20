// SPDX-License-Identifier: GPL-3.0-only

#include "DiagramCheck.h"

#include "viewers/diagram/Document.h"

namespace DiagramCheck {

QStringList problems(const QString &path, const QByteArray &content)
{
    if (content.trimmed().isEmpty()) // a new, blank diagram
        return {};
    return diagram::document::problems(path, content);
}

QJsonObject feedbackOutput(const QString &file, const QStringList &problems)
{
    QString reason = QStringLiteral("Duet's diagram editor read %1 with problems:").arg(file);
    for (const QString &problem : problems)
        reason += QStringLiteral("\n- ") + problem;
    reason += QStringLiteral("\nFix the file so that it reads as you meant it.");
    return QJsonObject{{QLatin1String("decision"), QLatin1String("block")}, {QLatin1String("reason"), reason}};
}

} // namespace DiagramCheck
