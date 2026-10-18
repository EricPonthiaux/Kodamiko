#include "Installer.h"

#include "Toolchains.h"

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

QString sha256Of(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    QCryptographicHash hash(QCryptographicHash::Sha256);
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
#if defined(Q_OS_WIN) || defined(Q_OS_MACOS)
    // Their tar is bsdtar, which reads zip files too.
    if (found(QStringLiteral("tar")))
        return {QStringLiteral("tar"), QStringLiteral("-xf"), archive, QStringLiteral("-C"), destination};
#endif
    if (found(QStringLiteral("bsdtar")))
        return {QStringLiteral("bsdtar"), QStringLiteral("-xf"), archive, QStringLiteral("-C"), destination};
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
    if (source.checksumUrl.isEmpty()) {
        fetchArchive(source, {});
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
    if (!expectedChecksum.isEmpty() && sha256Of(archive) != expectedChecksum) {
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
                complete(children.size() == 1 && children.first().isDir() ? children.first().filePath() : content);
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
                complete(content);
            });
    connect(process, &QProcess::errorOccurred, this, [this, process, generation](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart || generation != generation_)
            return;
        process->deleteLater();
        fail(tr("Cannot run %1").arg(process->program()));
    });
    process->start(node->executable, arguments);
}

void Installer::complete(const QString &content)
{
    const InstallStep step = steps_.at(index_);
    const QString target = toolchains_->installFolder(step.tool, step.version);
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
    marker.write(step.title.toUtf8());
    marker.close();
    QDir(stagingFolder()).removeRecursively();
    if (toolchains_->installedExecutable(step.tool, step.version).isEmpty() && ToolCatalog::find(step.tool)) {
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
