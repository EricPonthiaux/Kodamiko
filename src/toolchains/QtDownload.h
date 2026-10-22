// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "QtRepository.h"

#include <QFile>
#include <QObject>
#include <QPointer>

#include <functional>
#include <memory>

class QNetworkAccessManager;
class QNetworkReply;
class QProcess;

// Downloads a Qt kit and its add-on modules from Qt's online repository
// into a folder (see QtRepository): finds the newest release of the
// version asked, reads its Updates.xml, then fetches each archive, checks
// it against the SHA-1 download.qt.io publishes, and unpacks it with
// bsdtar, one after the other. Installer moves the folder in place.
class QtDownload : public QObject {
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(QtDownload)

public:
    struct Request {
        QString version; // "6.10" (the newest 6.10.x), "6.10.3"; empty: the newest
        QString kit;     // "msvc2022_64", "gcc_64", "macos"
        QStringList modules;
        QString folder; // where the kit's files go; created if needed
    };

    // `network` is not owned, and outlives the download.
    QtDownload(QNetworkAccessManager *network, Request request, QObject *parent = nullptr);
    ~QtDownload() override;

    void start();
    // Stops the request or the unpacking under way; finished is not emitted.
    void cancel();

signals:
    void progress(qint64 received, qint64 total);
    // `version` is the release installed ("6.10.3"); `error` is empty when
    // it succeeded.
    void finished(const QString &version, const QString &error);

private:
    void fetchRelease(const QByteArray &desktopListing);
    void fetchPackages(const QByteArray &releaseListing);
    void readPackages(const QUrl &repository, const QByteArray &updatesXml);
    void nextArchive();
    void fetchArchive(const QtArchive &archive, const QString &expectedSha1);
    void checkArchive(const QtArchive &archive, const QString &path, const QString &expectedSha1);
    void unpack(const QtArchive &archive, const QString &path);
    void succeed();
    void fail(const QString &error);
    // GET `url` into memory; `done` gets the body, or an error.
    void get(const QUrl &url, std::function<void(const QByteArray &body)> done);

    QNetworkAccessManager *network_;
    Request request_;
    QtRepository::Release release_;
    QtPackageSet packages_;
    qsizetype next_ = 0;  // the archive to fetch next
    qint64 received_ = 0; // bytes of the archives already unpacked
    QPointer<QNetworkReply> reply_;
    QPointer<QProcess> process_;
    std::unique_ptr<QFile> file_;
};
