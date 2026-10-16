#include "Branches.h"

#include "GitProcess.h"

#include <QObject>
#include <QRegularExpression>

namespace Branches {
namespace {

// Characters git refuses anywhere in a reference name.
bool isForbidden(QChar c)
{
    return c.unicode() < 0x20 || c.unicode() == 0x7f || QStringLiteral(" ~^:?*[\\").contains(c);
}

} // namespace

QString nameError(const QString &name)
{
    if (name.isEmpty())
        return QObject::tr("Enter a name.");
    for (const QChar c : name) {
        if (c == QLatin1Char(' '))
            return QObject::tr("A branch name cannot contain spaces.");
        if (isForbidden(c))
            return QObject::tr("A branch name cannot contain \"%1\".").arg(c.unicode() < 0x20 ? QStringLiteral("\\t") : QString(c));
    }
    if (name.startsWith(QLatin1Char('-')))
        return QObject::tr("A branch name cannot start with \"-\".");
    if (name == QLatin1String("@"))
        return QObject::tr("\"@\" cannot name a branch.");
    if (name.contains(QLatin1String("..")))
        return QObject::tr("A branch name cannot contain \"..\".");
    if (name.contains(QLatin1String("@{")))
        return QObject::tr("A branch name cannot contain \"@{\".");
    if (name.endsWith(QLatin1Char('.')) || name.endsWith(QLatin1Char('/')))
        return QObject::tr("A branch name cannot end with \"%1\".").arg(name.right(1));
    for (const QString &part : name.split(QLatin1Char('/'))) {
        if (part.isEmpty())
            return QObject::tr("A branch name cannot contain \"//\" or start with \"/\".");
        if (part.startsWith(QLatin1Char('.')))
            return QObject::tr("A part of a branch name cannot start with \".\".");
        if (part.endsWith(QLatin1String(".lock")))
            return QObject::tr("A part of a branch name cannot end with \".lock\".");
    }
    return {};
}

QString sanitizedName(const QString &typed)
{
    QString name;
    for (const QChar c : typed) {
        if (c == QLatin1Char(' '))
            name += QLatin1Char('-');
        else if (!isForbidden(c))
            name += c;
    }
    // Runs of "-" from several spaces, and ".." which git refuses.
    static const QRegularExpression dashes(QStringLiteral("-{2,}"));
    name.replace(dashes, QStringLiteral("-"));
    while (name.contains(QLatin1String("..")))
        name.replace(QLatin1String(".."), QLatin1String("."));
    return name;
}

QStringList local(const QString &workDir)
{
    const QByteArray output = Git::run(
        workDir, {QStringLiteral("for-each-ref"), QStringLiteral("--format=%(refname:short)"), QStringLiteral("refs/heads")});
    return QString::fromUtf8(output).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
}

void create(const RepositoryInfo &repository, const QString &name, bool switchTo)
{
    if (switchTo)
        Git::run(repository.workDir, {QStringLiteral("switch"), QStringLiteral("--create"), name});
    else
        Git::run(repository.workDir, {QStringLiteral("branch"), QStringLiteral("--"), name});
}

} // namespace Branches
