// SPDX-License-Identifier: GPL-3.0-only

#include "Installer.h"

#include "QtDownload.h"
#include "QtRepository.h"
#include "Toolchains.h"
#include "system/Host.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QProcessEnvironment>
#include <QStandardPaths>

namespace {

QNetworkRequest requestFor(const QUrl &url)
{
    QNetworkRequest request(url);
    // GitHub's API refuses requests without one.
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("Duet/%1").arg(QCoreApplication::applicationVersion()));
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

QString hashOf(const QString &path, ChecksumAlgorithm algorithm)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    QCryptographicHash hash(algorithm == ChecksumAlgorithm::Sha512 ? QCryptographicHash::Sha512
                                                                   : QCryptographicHash::Sha256);
    hash.addData(&file);
    return QString::fromLatin1(hash.result().toHex());
}

void makeExecutable(const QString &path)
{
    QFile::setPermissions(path, QFile::permissions(path) | QFile::ExeOwner | QFile::ExeGroup | QFile::ExeOther);
}

} // namespace

Installer::Installer(Toolchains *toolchains, QObject *parent)
    : QObject(parent)
    , toolchains_(toolchains)
    , network_(new QNetworkAccessManager(this))
{
}

Installer::~Installer()
{
    cancel();
}

QStringList Installer::extractCommand(ArchiveFormat format, const QString &archive, const QString &destination)
{
    const auto found = [](const QString &program) { return !QStandardPaths::findExecutable(program).isEmpty(); };
    if (format == ArchiveFormat::TarGz)
        return found(QStringLiteral("tar")) ? QStringList{QStringLiteral("tar"), QStringLiteral("-xzf"), archive,
                                                          QStringLiteral("-C"), destination}
                                            : QStringList{};
    // Windows' and macOS's tar is bsdtar, which reads zip and 7z files too.
    const bool isTarBsdtar = Host::operatingSystem() != OperatingSystem::Linux;
    if (isTarBsdtar && found(QStringLiteral("tar")))
        return {QStringLiteral("tar"), QStringLiteral("-xf"), archive, QStringLiteral("-C"), destination};
    if (found(QStringLiteral("bsdtar")))
        return {QStringLiteral("bsdtar"), QStringLiteral("-xf"), archive, QStringLiteral("-C"), destination};
    // GNU tar does not read 7z files.
    if (format == ArchiveFormat::SevenZip)
        return {};
    if (found(QStringLiteral("unzip")))
        return {QStringLiteral("unzip"), QStringLiteral("-q"), QStringLiteral("-o"), archive,
                QStringLiteral("-d"),    destination};
    for (const QString &python : {QStringLiteral("python3"), QStringLiteral("python")}) {
        if (found(python))
            return {
                python,  QStringLiteral("-I"), QStringLiteral("-m"), QStringLiteral("zipfile"), QStringLiteral("-e"),
                archive, destination};
    }
    return {};
}

void Installer::run(const QList<InstallStep> &steps)
{
    cancel();
    steps_ = steps;
    index_ = -1;
    ok_ = true;
    running_ = true;
    nextStep();
}

void Installer::cancel()
{
    if (!running_)
        return;
    ++generation_;
    running_ = false;
    if (reply_)
        reply_->abort();
    if (process_) {
        process_->kill();
        process_->waitForFinished(2000);
    }
    if (qt_) {
        qt_->cancel();
        qt_->deleteLater();
    }
    file_ = nullptr;
    QDir(stagingFolder()).removeRecursively();
    if (index_ >= 0 && index_ < steps_.size())
        emit stepFinished(index_, tr("Cancelled"));
    emit finished(false);
}

QString Installer::stagingFolder() const
{
    if (index_ < 0 || index_ >= steps_.size())
        return {};
    // Inside the toolchains' folder: moving it in place is a rename.
    const InstallStep &step = steps_.at(index_);
    return toolchains_->root() + QStringLiteral("/.staging/") + step.tool + QLatin1Char('-') + step.version;
}

void Installer::nextStep()
{
    ++index_;
    if (index_ >= steps_.size()) {
        running_ = false;
        QDir(toolchains_->root() + QStringLiteral("/.staging")).removeRecursively();
        toolchains_->refresh();
        emit finished(ok_);
        return;
    }
    startStep();
}

void Installer::startStep()
{
    const InstallStep step = steps_.at(index_);
    emit stepStarted(index_);
    QDir(stagingFolder()).removeRecursively();
    if (!QDir().mkpath(stagingFolder())) {
        fail(tr("Cannot create %1").arg(QDir::toNativeSeparators(stagingFolder())));
        return;
    }
    if (step.tool == QLatin1String(QtRepository::toolId)) {
        installQt();
        return;
    }
    if (step.source) {
        download(*step.source);
        return;
    }
    if (resolver_) {
        download(resolver_(step));
        return;
    }
    const QUrl metadata = ToolCatalog::metadataUrl(step.tool, step.version);
    if (metadata.isEmpty()) {
        download(ToolCatalog::source(step.tool, step.version, ToolCatalog::currentPlatform(), {}));
        return;
    }
    fetch(metadata, [this, step](const QByteArray &body, const QString &error) {
        if (!error.isEmpty()) {
            fail(error);
            return;
        }
        download(ToolCatalog::source(step.tool, step.version, ToolCatalog::currentPlatform(), body));
    });
}

void Installer::fetch(const QUrl &url, std::function<void(const QByteArray &, const QString &)> done)
{
    QNetworkReply *reply = network_->get(requestFor(url));
    reply_ = reply;
    const int generation = generation_;
    connect(reply, &QNetworkReply::finished, this, [this, reply, generation, done = std::move(done)] {
        reply->deleteLater();
        if (generation != generation_)
            return;
        const QString error = replyError(reply);
        done(error.isEmpty() ? reply->readAll() : QByteArray(), error);
    });
}

void Installer::download(const DownloadSource &source)
{
    if (!source.error.isEmpty()) {
        fail(source.error);
        return;
    }
    if (source.format == ArchiveFormat::Npm) {
        installNpm();
        return;
    }
    if (!source.checksum.isEmpty() || source.checksumUrl.isEmpty()) {
        fetchArchive(source, source.checksum);
        return;
    }
    fetch(source.checksumUrl, [this, source](const QByteArray &body, const QString &error) {
        if (!error.isEmpty()) {
            fail(tr("Cannot get the checksum: %1").arg(error));
            return;
        }
        const QString checksum = ToolCatalog::checksumFor(body, source.fileName);
        if (checksum.isEmpty()) {
            fail(tr("No checksum published for %1").arg(source.fileName));
            return;
        }
        fetchArchive(source, checksum);
    });
}

void Installer::fetchArchive(const DownloadSource &source, const QString &expectedChecksum)
{
    const QString name = source.fileName.isEmpty() ? QFileInfo(source.url.path()).fileName() : source.fileName;
    const QString archive = stagingFolder() + QStringLiteral("/") + name;
    file_ = std::make_unique<QFile>(archive);
    if (!file_->open(QIODevice::WriteOnly)) {
        fail(tr("Cannot write %1").arg(QDir::toNativeSeparators(archive)));
        return;
    }
    QNetworkReply *reply = network_->get(requestFor(source.url));
    reply_ = reply;
    const int generation = generation_;
    const int index = index_;
    connect(reply, &QNetworkReply::readyRead, this, [this, reply, generation] {
        if (generation == generation_ && file_)
            file_->write(reply->readAll());
    });
    connect(reply, &QNetworkReply::downloadProgress, this, [this, index, generation](qint64 received, qint64 total) {
        if (generation == generation_)
            emit progress(index, received, std::max<qint64>(total, 0));
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, generation, source, archive, expectedChecksum] {
        reply->deleteLater();
        if (generation == generation_)
            finishArchive(reply, source, archive, expectedChecksum);
    });
}

void Installer::finishArchive(QNetworkReply *reply,
                              const DownloadSource &source,
                              const QString &archive,
                              const QString &expectedChecksum)
{
    if (file_) {
        file_->write(reply->readAll());
        file_->close();
        file_ = nullptr;
    }
    if (const QString error = replyError(reply); !error.isEmpty()) {
        fail(error);
        return;
    }
    if (!expectedChecksum.isEmpty() && hashOf(archive, source.algorithm) != expectedChecksum.toLower()) {
        fail(tr("%1 is damaged: its checksum does not match").arg(QFileInfo(archive).fileName()));
        return;
    }
    extract(source, archive);
}

void Installer::extract(const DownloadSource &source, const QString &archive)
{
    const QString content = stagingFolder() + QStringLiteral("/content");
    QDir().mkpath(content);
    const QStringList command = extractCommand(source.format, archive, content);
    if (command.isEmpty()) {
        fail(source.format == ArchiveFormat::Zip ? tr("Cannot unpack a zip file: install unzip or bsdtar")
                                                 : tr("Cannot unpack the archive: tar is not installed"));
        return;
    }
    auto *process = new QProcess(this);
    process_ = process;
    process->setProcessChannelMode(QProcess::MergedChannels);
    const int generation = generation_;
    connect(process, &QProcess::finished, this,
            [this, process, generation, content, archive](int code, QProcess::ExitStatus status) {
                process->deleteLater();
                if (generation != generation_)
                    return;
                if (status != QProcess::NormalExit || code != 0) {
                    fail(tr("Cannot unpack %1: %2")
                             .arg(QFileInfo(archive).fileName(), QString::fromLocal8Bit(process->readAll()).trimmed()));
                    return;
                }
                QFile::remove(archive);
                // Archives holding a single folder (jdk-17.0.2+8/): its contents.
                const QFileInfoList children =
                    QDir(content).entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden);
                const InstallStep &step = steps_.at(index_);
                complete(children.size() == 1 && children.first().isDir() ? children.first().filePath() : content,
                         toolchains_->installFolder(step.tool, step.version));
            });
    connect(process, &QProcess::errorOccurred, this, [this, process, generation](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart || generation != generation_)
            return;
        process->deleteLater();
        fail(tr("Cannot run %1").arg(process->program()));
    });
    process->start(command.first(), command.mid(1));
}

void Installer::installNpm()
{
    const InstallStep step = steps_.at(index_);
    const ToolSpec *spec = ToolCatalog::find(step.tool);
    const int minimumNode = spec ? spec->minimumRuntime : 0;
    const std::optional<RuntimeInstall> node = toolchains_->runtimeAtLeast(QStringLiteral("node"), minimumNode);
    const QString npm = node ? Toolchains::npmScript(*node) : QString();
    if (!spec || !node || npm.isEmpty()) {
        fail(tr("%1 needs Node.js %2 or newer with npm").arg(step.tool).arg(minimumNode));
        return;
    }
    const QString content = stagingFolder() + QStringLiteral("/content");
    QDir().mkpath(content);
    QStringList arguments{npm,
                          QStringLiteral("install"),
                          QStringLiteral("--prefix"),
                          content,
                          QStringLiteral("--no-audit"),
                          QStringLiteral("--no-fund"),
                          QStringLiteral("--loglevel=error")};
    for (QString package : spec->npmPackages)
        arguments.append(package.replace(QStringLiteral("{version}"), step.version));

    auto *process = new QProcess(this);
    process_ = process;
    process->setProcessChannelMode(QProcess::MergedChannels);
    // Install scripts call node: the same one.
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("PATH"), QFileInfo(node->executable).absolutePath() + QDir::listSeparator() +
                                                   environment.value(QStringLiteral("PATH")));
    process->setProcessEnvironment(environment);
    const int generation = generation_;
    const int index = index_;
    emit progress(index, 0, 0);
    connect(process, &QProcess::finished, this,
            [this, process, generation, content](int code, QProcess::ExitStatus status) {
                process->deleteLater();
                if (generation != generation_)
                    return;
                if (status != QProcess::NormalExit || code != 0) {
                    const QString output = QString::fromLocal8Bit(process->readAll()).trimmed();
                    fail(tr("npm failed: %1").arg(output.section(QLatin1Char('\n'), -3)));
                    return;
                }
                const InstallStep &installed = steps_.at(index_);
                complete(content, toolchains_->installFolder(installed.tool, installed.version));
            });
    connect(process, &QProcess::errorOccurred, this, [this, process, generation](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart || generation != generation_)
            return;
        process->deleteLater();
        fail(tr("Cannot run %1").arg(process->program()));
    });
    process->start(node->executable, arguments);
}

void Installer::installQt()
{
    const InstallStep step = steps_.at(index_);
    const QString content = stagingFolder() + QStringLiteral("/content");
    auto *qt = new QtDownload(
        network_, {.version = step.version, .kit = step.kit, .modules = step.modules, .folder = content}, this);
    qt_ = qt;
    const int generation = generation_;
    const int index = index_;
    connect(qt, &QtDownload::progress, this, [this, index, generation](qint64 received, qint64 total) {
        if (generation == generation_)
            emit progress(index, received, total);
    });
    connect(qt, &QtDownload::finished, this,
            [this, qt, generation, step, content](const QString &version, const QString &error) {
                qt->deleteLater();
                if (generation != generation_)
                    return;
                if (!error.isEmpty()) {
                    fail(error);
                    return;
                }
                complete(content, toolchains_->qtInstallFolder(version, step.kit));
            });
    qt->start();
}

void Installer::complete(const QString &content, const QString &target)
{
    const InstallStep step = steps_.at(index_);
    QDir(target).removeRecursively();
    QDir().mkpath(QFileInfo(target).absolutePath());
    if (!QDir().rename(content, target)) {
        fail(tr("Cannot move the files to %1").arg(QDir::toNativeSeparators(target)));
        return;
    }
    // zipfile (Python) does not keep the permissions.
    if (const ToolSpec *spec = ToolCatalog::find(step.tool); spec && !spec->script) {
        for (const QString &executable : spec->executables) {
            if (QFileInfo::exists(target + QLatin1Char('/') + executable))
                makeExecutable(target + QLatin1Char('/') + executable);
        }
    }
    QFile marker(target + QStringLiteral("/.complete"));
    if (!marker.open(QIODevice::WriteOnly)) {
        fail(tr("Cannot write %1").arg(QDir::toNativeSeparators(marker.fileName())));
        return;
    }
    // A Qt's modules follow its title, one per line (see Toolchains::qtFolder).
    marker.write((QStringList{step.title} + step.modules).join(QLatin1Char('\n')).toUtf8());
    marker.close();
    QDir(stagingFolder()).removeRecursively();
    const QString check = target + QLatin1Char('/') + step.check;
    if (!step.check.isEmpty() && QFileInfo::exists(check))
        makeExecutable(check);
    const bool lacksCheck = !step.check.isEmpty() && !QFileInfo::exists(check);
    const bool lacksExecutable =
        toolchains_->installedExecutable(step.tool, step.version).isEmpty() && ToolCatalog::find(step.tool);
    if (lacksCheck || lacksExecutable) {
        // Unpacked, but not as expected: not usable.
        QFile::remove(marker.fileName());
        fail(tr("%1 does not contain the expected files").arg(step.title));
        return;
    }
    toolchains_->refresh();
    emit stepFinished(index_, QString());
    nextStep();
}

void Installer::fail(const QString &error)
{
    ok_ = false;
    file_ = nullptr;
    QDir(stagingFolder()).removeRecursively();
    emit stepFinished(index_, error);
    nextStep();
}
