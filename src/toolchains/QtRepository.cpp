// SPDX-License-Identifier: GPL-3.0-only

#include "QtRepository.h"

#include <QHash>
#include <QObject>
#include <QRegularExpression>
#include <QXmlStreamReader>

namespace {

constexpr auto repositoryRoot = "https://download.qt.io/online/qtsdkrepository/";

// "@TargetDir@/6.10.3/msvc2022_64/bin" → "bin" for that version and kit.
constexpr auto targetDirectory = "@TargetDir@";

struct Package {
    QString name;    // "qt.qt6.6103.win64_msvc2022_64"
    QString version; // "6.10.3-0-202603310407", the prefix of its file names
    QStringList archives;
    QHash<QString, QString> targets; // archive → where it is extracted
    qint64 compressedSize = 0;
};

QString hostFolder(const Platform &platform)
{
    const bool isArm = platform.arch == QLatin1String("aarch64");
    if (platform.os == QLatin1String("windows"))
        return isArm ? QStringLiteral("windows_arm64") : QStringLiteral("windows_x86");
    if (platform.os == QLatin1String("mac"))
        return QStringLiteral("mac_x64"); // universal builds
    return isArm ? QStringLiteral("linux_arm64") : QStringLiteral("linux_x64");
}

// "qt6_6103" for 6.10.3: the digits of the version, joined.
QString releaseFolderOf(const QVersionNumber &version)
{
    return QStringLiteral("qt6_%1%2%3")
        .arg(version.majorVersion())
        .arg(version.minorVersion())
        .arg(version.microVersion());
}

// The version a release folder stands for: "qt6_683" → 6.8.3,
// "qt6_6103" → 6.10.3. The patch is always one digit.
QVersionNumber versionOfFolder(const QString &digits)
{
    if (digits.size() < 3)
        return {};
    return QVersionNumber(digits.left(1).toInt(), digits.mid(1, digits.size() - 2).toInt(), digits.right(1).toInt());
}

// The folder names of an index page (href="name/").
QStringList foldersOf(const QByteArray &listing)
{
    static const QRegularExpression href(QStringLiteral(R"(href="([A-Za-z0-9_.]+)/")"));
    QStringList folders;
    for (const QRegularExpressionMatch &match : href.globalMatch(QString::fromUtf8(listing)))
        folders.append(match.captured(1));
    return folders;
}

void readOperations(QXmlStreamReader &xml, Package &package)
{
    QStringList arguments;
    while (xml.readNextStartElement()) {
        if (xml.name() != QLatin1String("Operation")) {
            xml.skipCurrentElement();
            continue;
        }
        const bool isExtract = xml.attributes().value(QLatin1String("name")) == QLatin1String("Extract");
        arguments.clear();
        while (xml.readNextStartElement())
            arguments.append(xml.readElementText().trimmed());
        if (isExtract && arguments.size() == 2)
            package.targets.insert(arguments.at(1), arguments.at(0));
    }
}

void readPackageField(QXmlStreamReader &xml, Package &package)
{
    const auto name = xml.name();
    if (name == QLatin1String("Operations")) {
        readOperations(xml, package);
        return;
    }
    if (name == QLatin1String("UpdateFile")) {
        package.compressedSize = xml.attributes().value(QLatin1String("CompressedSize")).toLongLong();
        xml.skipCurrentElement();
        return;
    }
    const QString text = xml.readElementText().trimmed();
    if (name == QLatin1String("Name"))
        package.name = text;
    else if (name == QLatin1String("Version"))
        package.version = text;
    else if (name == QLatin1String("DownloadableArchives"))
        package.archives = text.split(QLatin1Char(','), Qt::SkipEmptyParts);
}

QHash<QString, Package> packagesOf(const QByteArray &updatesXml)
{
    QHash<QString, Package> packages;
    QXmlStreamReader xml(updatesXml);
    if (!xml.readNextStartElement())
        return packages;
    while (xml.readNextStartElement()) {
        if (xml.name() != QLatin1String("PackageUpdate")) {
            xml.skipCurrentElement();
            continue;
        }
        Package package;
        while (xml.readNextStartElement())
            readPackageField(xml, package);
        for (QString &archive : package.archives)
            archive = archive.trimmed();
        packages.insert(package.name, package);
    }
    return packages;
}

// The kit's package: the one extracted into <version>/<kit>, add-ons and
// debug information aside.
const Package *kitPackage(const QHash<QString, Package> &packages, const QString &prefix, const QString &kitTarget)
{
    for (const Package &package : packages) {
        const QString suffix = package.name.mid(prefix.size());
        const bool isKit = package.name.startsWith(prefix) && !suffix.contains(QLatin1Char('.'));
        if (isKit && package.targets.values().contains(kitTarget))
            return &package;
    }
    return nullptr;
}

void addArchives(const Package &package, const QUrl &repository, const QString &kitTarget, QtPackageSet &set)
{
    const QString base = repository.toString() + QLatin1Char('/') + package.name + QLatin1Char('/') + package.version;
    for (const QString &archive : package.archives) {
        const QString target = package.targets.value(archive, kitTarget);
        QString folder = target.startsWith(kitTarget) ? target.mid(kitTarget.size()) : QString();
        while (folder.startsWith(QLatin1Char('/')))
            folder.remove(0, 1);
        set.archives.append({.url = QUrl(base + archive),
                             .checksumUrl = QUrl(base + archive + QStringLiteral(".sha1")),
                             .fileName = archive,
                             .folder = folder});
    }
    set.compressedSize += package.compressedSize;
}

} // namespace

namespace QtRepository {

QUrl desktopUrl(const Platform &platform)
{
    return QUrl(QLatin1String(repositoryRoot) + hostFolder(platform) + QStringLiteral("/desktop/"));
}

std::optional<Release> newestRelease(const QByteArray &listing, const QString &version)
{
    static const QRegularExpression release(QStringLiteral(R"(^qt6_(\d+)$)"));
    const QVersionNumber wanted = QVersionNumber::fromString(version);
    std::optional<Release> newest;
    for (const QString &folder : foldersOf(listing)) {
        const QRegularExpressionMatch match = release.match(folder);
        if (!match.hasMatch())
            continue;
        const QVersionNumber found = versionOfFolder(match.captured(1));
        const bool fits = !found.isNull() && (wanted.isNull() || wanted.isPrefixOf(found));
        if (fits && (!newest || found > newest->version))
            newest = Release{.version = found, .folder = folder};
    }
    return newest;
}

QString repositoryFolder(const QByteArray &listing, const Release &release, const QString &kit)
{
    const QStringList folders = foldersOf(listing);
    const QString perKit = release.folder + QLatin1Char('_') + kit;
    if (folders.contains(perKit))
        return perKit;
    return folders.contains(release.folder) ? release.folder : QString();
}

std::optional<QtPackageSet> packages(const QByteArray &updatesXml,
                                     const QUrl &repository,
                                     const Release &release,
                                     const QString &kit,
                                     const QStringList &modules,
                                     QString *error)
{
    const QHash<QString, Package> all = packagesOf(updatesXml);
    const QString prefix = QStringLiteral("qt.qt6.%1.").arg(releaseFolderOf(release.version).mid(4));
    const QString kitTarget =
        QStringLiteral("%1/%2/%3").arg(QLatin1String(targetDirectory), release.version.toString(), kit);
    const Package *base = kitPackage(all, prefix, kitTarget);
    if (!base) {
        *error = QObject::tr("Qt %1 has no %2 kit for this system").arg(release.version.toString(), kit);
        return std::nullopt;
    }
    QtPackageSet set;
    addArchives(*base, repository, kitTarget, set);
    // "qt.qt6.6103.win64_msvc2022_64" → "win64_msvc2022_64", which the
    // add-ons' names end with.
    const QString suffix = base->name.mid(prefix.size());
    for (const QString &module : modules) {
        const auto addon = all.constFind(prefix + QStringLiteral("addons.") + module + QLatin1Char('.') + suffix);
        if (addon == all.constEnd()) {
            *error = QObject::tr("Qt %1 has no %2 module for the %3 kit").arg(release.version.toString(), module, kit);
            return std::nullopt;
        }
        addArchives(*addon, repository, kitTarget, set);
    }
    return set;
}

QString checksumOf(const QByteArray &checksumFile)
{
    return QString::fromLatin1(checksumFile).simplified().section(QLatin1Char(' '), 0, 0).toLower();
}

} // namespace QtRepository
