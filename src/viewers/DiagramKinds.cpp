// SPDX-License-Identifier: GPL-3.0-only

#include "DiagramKinds.h"

#ifdef KDK_WITH_DIAGRAMS
#include "viewers/diagram/Document.h"
#include "viewers/diagram/Family.h"
#endif

namespace DiagramKinds {

QList<DiagramKind> available()
{
    QList<DiagramKind> kinds;
#ifdef KDK_WITH_DIAGRAMS
    for (const diagram::Family *family : diagram::allFamilies()) {
        const QString extension = diagram::document::extension(family->id);
        if (!extension.isEmpty())
            kinds.append({family->id, family->title, extension, diagram::document::blank(family->id)});
    }
#endif
    return kinds;
}

DiagramKind find(const QString &id)
{
    for (const DiagramKind &kind : available()) {
        if (kind.id == id)
            return kind;
    }
    return {};
}

QString withExtension(const QString &name, const DiagramKind &kind)
{
    const QString suffix = QLatin1Char('.') + kind.extension;
    return name.endsWith(suffix, Qt::CaseInsensitive) ? name : name + suffix;
}

} // namespace DiagramKinds
