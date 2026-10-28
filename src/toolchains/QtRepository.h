// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "ToolCatalog.h"

#include <QByteArray>
#include <QList>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QVersionNumber>

#include <optional>

// One archive of a Qt kit: unpacked into the kit's folder, or one of its
// subfolders ("bin" for d3dcompiler_47.dll).
struct QtArchive {
    QUrl url;
    QUrl checksumUrl; // its SHA-1, served by download.qt.io itself, not a mirror
    QString fileName;
    QString folder; // relative to the kit's folder; empty: the folder itself
    bool operator==(const QtArchive &) const = default;
};

// What a kit's installation downloads.
struct QtPackageSet {
    QList<QtArchive> archives;
    qint64 compressedSize = 0; // bytes, for the progress
};

// Qt's online repository (download.qt.io/online/qtsdkrepository), read as
// Qt's installer and aqtinstall read it:
//   <system>/desktop/                    one folder per release ("qt6_6103")
//   <release>/<repository>/Updates.xml   its packages: archives, and where
//                                        each is extracted
// The repository is named after the release ("qt6_6103"), or, on Windows
// from Qt 6.12, after the release and the kit ("qt6_6120_msvc2022_64").
namespace QtRepository {

// The tool id of Qt in install plans and in Kodamiko's store, where a kit is
// <store>/qt/<version>/<kit>, as Qt's installer lays it out.
constexpr auto toolId = "qt";

// The folder listing the releases for `platform`'s desktop.
QUrl desktopUrl(const Platform &platform);

struct Release {
    QVersionNumber version; // 6.10.3
    QString folder;         // "qt6_6103"
};

// The newest release of a listing of desktopUrl() whose version starts with
// `version` ("6.10" → 6.10.3; empty: any); nullopt when there is none.
std::optional<Release> newestRelease(const QByteArray &listing, const QString &version);

// The repository of `kit` in a listing of `release`'s folder; empty when
// none fits.
QString repositoryFolder(const QByteArray &listing, const Release &release, const QString &kit);

// The archives of `kit` of `release` and of its add-on `modules`
// ("qtcharts"), read from the Updates.xml of `repository` (the URL of its
// folder). Nullopt, with `error` saying what is missing, when the
// repository has no such kit or module.
std::optional<QtPackageSet> packages(const QByteArray &updatesXml,
                                     const QUrl &repository,
                                     const Release &release,
                                     const QString &kit,
                                     const QStringList &modules,
                                     QString *error);

// The SHA-1 a checksum file gives (its first word), lower case.
QString checksumOf(const QByteArray &checksumFile);

} // namespace QtRepository
