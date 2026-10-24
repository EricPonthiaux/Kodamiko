// SPDX-License-Identifier: GPL-3.0-only

#include "DeveloperEnvironment.h"

namespace DeveloperEnvironment {

QMap<QString, QString> changedVariables(const QByteArray &setOutput, const QProcessEnvironment &before)
{
    QMap<QString, QString> changed;
    for (const QString &line : QString::fromLocal8Bit(setOutput).split(QLatin1Char('\n'))) {
        const qsizetype equals = line.indexOf(QLatin1Char('='));
        // cmd's per-drive folders ("=C:=C:\...") start with "=".
        if (equals <= 0)
            continue;
        const QString name = line.left(equals);
        QString value = line.mid(equals + 1);
        if (value.endsWith(QLatin1Char('\r')))
            value.chop(1);
        if (!before.contains(name) || before.value(name) != value)
            changed.insert(name, value);
    }
    return changed;
}

} // namespace DeveloperEnvironment
