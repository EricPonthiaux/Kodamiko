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

QString feedback(const QString &file, const QStringList &problems)
{
    QString reason = QStringLiteral("Kodamiko's diagram editor read %1 with problems:").arg(file);
    for (const QString &problem : problems)
        reason += QStringLiteral("\n- ") + problem;
    reason += QStringLiteral("\nFix the file so that it reads as you meant it.");
    return reason;
}

} // namespace DiagramCheck
