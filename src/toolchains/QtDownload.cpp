// SPDX-License-Identifier: GPL-3.0-only

#include "QtDownload.h"

#include "Installer.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>

namespace {

QNetworkRequest requestFor(const QUrl &url)
{
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("Kodamiko/%1").arg(QCoreApplication::applicationVersion()));
    // download.qt.io sends the archives to https mirrors.
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    return request;
}

QString replyError(const QNetworkReply *reply)
{
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (reply->error() == QNetworkReply::NoError && status < 400)
        return {};
    return QObject::tr("%1 (%2)").arg(reply->errorString(), reply->url().toDisplayString());
}

QString sha1Of(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    QCryptographicHash hash(QCryptographicHash::Sha1);
    hash.addData(&file);
    return QString::fromLatin1(hash.result().toHex());
}

QUrl childUrl(const QUrl &folder, const QString &name)
{
    QString base = folder.toString();
    if (!base.endsWith(QLatin1Char('/')))
        base += QLatin1Char('/');
    return QUrl(base + name);
}

} // namespace

QtDownload::QtDownload(QNetworkAccessManager *network, Request request, QObject *parent)
    : QObject(parent)
    , network_(network)
    , request_(std::move(request))
{
}

QtDownload::~QtDownload()
{
    cancel();
}

void QtDownload::start()
{
    get(QtRepository::desktopUrl(ToolCatalog::currentPlatform()),
        [this](const QByteArray &listing) { fetchRelease(listing); });
}

void QtDownload::cancel()
{
    if (reply_) {
        reply_->disconnect(this);
        reply_->abort();
    }
    if (process_) {
        process_->disconnect(this);
        process_->kill();
        process_->waitForFinished(2000);
    }
    file_ = nullptr;
}

void QtDownload::get(const QUrl &url, std::function<void(const QByteArray &)> done)
{
    QNetworkReply *reply = network_->get(requestFor(url));
    reply_ = reply;
    connect(reply, &QNetworkReply::finished, this, [this, reply, done = std::move(done)] {
        reply->deleteLater();
        if (const QString error = replyError(reply); !error.isEmpty()) {
            fail(error);
            return;
        }
        done(reply->readAll());
    });
}

void QtDownload::fetchRelease(const QByteArray &desktopListing)
{
    const std::optional<QtRepository::Release> release = QtRepository::newestRelease(desktopListing, request_.version);
    if (!release) {
        fail(tr("download.qt.io has no Qt %1").arg(request_.version));
        return;
    }
    release_ = *release;
    const QUrl folder = childUrl(QtRepository::desktopUrl(ToolCatalog::currentPlatform()), release_.folder);
    get(folder, [this](const QByteArray &listing) { fetchPackages(listing); });
}

void QtDownload::fetchPackages(const QByteArray &releaseListing)
{
    const QString repository = QtRepository::repositoryFolder(releaseListing, release_, request_.kit);
    if (repository.isEmpty()) {
        fail(tr("download.qt.io has no %1 kit of Qt %2").arg(request_.kit, release_.version.toString()));
        return;
    }
    const QUrl desktop = QtRepository::desktopUrl(ToolCatalog::currentPlatform());
    const QUrl folder = childUrl(childUrl(desktop, release_.folder), repository);
    get(childUrl(folder, QStringLiteral("Updates.xml")),
        [this, folder](const QByteArray &updatesXml) { readPackages(folder, updatesXml); });
}

void QtDownload::readPackages(const QUrl &repository, const QByteArray &updatesXml)
{
    QString error;
    const std::optional<QtPackageSet> packages =
        QtRepository::packages(updatesXml, repository, release_, request_.kit, request_.modules, &error);
    if (!packages) {
        fail(error);
        return;
    }
    packages_ = *packages;
    next_ = 0;
    received_ = 0;
    nextArchive();
}

void QtDownload::nextArchive()
{
    if (next_ >= packages_.archives.size()) {
        succeed();
        return;
    }
    const QtArchive archive = packages_.archives.at(next_);
    get(archive.checksumUrl, [this, archive](const QByteArray &checksumFile) {
        const QString sha1 = QtRepository::checksumOf(checksumFile);
        if (sha1.isEmpty()) {
            fail(tr("No checksum published for %1").arg(archive.fileName));
            return;
        }
        fetchArchive(archive, sha1);
    });
}

void QtDownload::fetchArchive(const QtArchive &archive, const QString &expectedSha1)
{
    // Next to the kit's folder, in Installer's staging folder.
    const QString path = QFileInfo(request_.folder).absolutePath() + QLatin1Char('/') + archive.fileName;
    file_ = std::make_unique<QFile>(path);
    if (!file_->open(QIODevice::WriteOnly)) {
        fail(tr("Cannot write %1").arg(QDir::toNativeSeparators(path)));
        return;
    }
    QNetworkReply *reply = network_->get(requestFor(archive.url));
    reply_ = reply;
    connect(reply, &QNetworkReply::readyRead, this, [this, reply] {
        if (file_)
            file_->write(reply->readAll());
    });
    connect(reply, &QNetworkReply::downloadProgress, this,
            [this](qint64 received, qint64) { emit progress(received_ + received, packages_.compressedSize); });
    connect(reply, &QNetworkReply::finished, this, [this, reply, archive, path, expectedSha1] {
        reply->deleteLater();
        if (file_) {
            file_->write(reply->readAll());
            file_->close();
            file_ = nullptr;
        }
        if (const QString error = replyError(reply); !error.isEmpty()) {
            fail(error);
            return;
        }
        checkArchive(archive, path, expectedSha1);
    });
}

void QtDownload::checkArchive(const QtArchive &archive, const QString &path, const QString &expectedSha1)
{
    if (sha1Of(path) != expectedSha1) {
        QFile::remove(path);
        fail(tr("%1 is damaged: its checksum does not match").arg(archive.fileName));
        return;
    }
    received_ += QFileInfo(path).size();
    unpack(archive, path);
}

void QtDownload::unpack(const QtArchive &archive, const QString &path)
{
    const QString destination = QDir(request_.folder).filePath(archive.folder);
    QDir().mkpath(destination);
    const QStringList command = Installer::extractCommand(ArchiveFormat::SevenZip, path, destination);
    if (command.isEmpty()) {
        fail(tr("Cannot unpack %1: install bsdtar").arg(archive.fileName));
        return;
    }
    auto *process = new QProcess(this);
    process_ = process;
    process->setProcessChannelMode(QProcess::MergedChannels);
    connect(process, &QProcess::finished, this, [this, process, archive, path](int code, QProcess::ExitStatus status) {
        process->deleteLater();
        QFile::remove(path);
        if (status != QProcess::NormalExit || code != 0) {
            fail(
                tr("Cannot unpack %1: %2").arg(archive.fileName, QString::fromLocal8Bit(process->readAll()).trimmed()));
            return;
        }
        ++next_;
        nextArchive();
    });
    connect(process, &QProcess::errorOccurred, this, [this, process](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart)
            return;
        process->deleteLater();
        fail(tr("Cannot run %1").arg(process->program()));
    });
    process->start(command.first(), command.mid(1));
}

void QtDownload::succeed()
{
    // What CMake's find_package(Qt6) reads: without it, nothing works.
    if (!QFileInfo::exists(request_.folder + QStringLiteral("/lib/cmake/Qt6/Qt6Config.cmake"))) {
        fail(tr("Qt %1 %2 does not contain the expected files").arg(release_.version.toString(), request_.kit));
        return;
    }
    emit finished(release_.version.toString(), QString());
}

void QtDownload::fail(const QString &error)
{
    file_ = nullptr;
    emit finished(release_.version.toString(), error);
}
