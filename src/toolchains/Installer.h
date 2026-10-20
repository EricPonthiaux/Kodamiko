// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "InstallPlan.h"
#include "ToolCatalog.h"

#include <QFile>
#include <QObject>
#include <QPointer>

#include <functional>
#include <memory>

class QNetworkAccessManager;
class QNetworkReply;
class QProcess;
class Toolchains;

// Downloads the steps of an InstallPlan into Toolchains' folder, one after
// the other: each archive is fetched (its address first, from the release's
// information), checked against its published SHA-256, unpacked with the
// system's tar (or unzip), and moved in place; npm packages are installed
// with the Node Duet has. A step that fails does not stop the others.
class Installer : public QObject {
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(Installer)

public:
    explicit Installer(Toolchains *toolchains, QObject *parent = nullptr);
    ~Installer() override;

    void run(const QList<InstallStep> &steps);
    void cancel();

    bool isRunning() const { return running_; }

    QList<InstallStep> steps() const { return steps_; }

    // For tests: where each step's archive is, instead of ToolCatalog.
    using SourceResolver = std::function<DownloadSource(const InstallStep &)>;

    void setSourceResolver(SourceResolver resolver) { resolver_ = std::move(resolver); }

    // The command unpacking `archive` into `destination`: program first.
    // Empty when the system has none for that format.
    static QStringList extractCommand(ArchiveFormat format, const QString &archive, const QString &destination);

signals:
    void stepStarted(int index);
    // `total` is 0 when unknown (npm).
    void progress(int index, qint64 received, qint64 total);
    // `error` is empty when the step succeeded.
    void stepFinished(int index, const QString &error);
    void finished(bool ok);

private:
    void startStep();
    void download(const DownloadSource &source);
    void fetchArchive(const DownloadSource &source, const QString &expectedChecksum);
    // The download ended: checks the archive, then unpacks it.
    void finishArchive(QNetworkReply *reply,
                       const DownloadSource &source,
                       const QString &archive,
                       const QString &expectedChecksum);
    void extract(const DownloadSource &source, const QString &archive);
    void installNpm();
    void complete(const QString &content);
    void fail(const QString &error);
    void nextStep();
    // GET `url` into memory; `done` gets the body, or an error.
    void fetch(const QUrl &url, std::function<void(const QByteArray &body, const QString &error)> done);
    QString stagingFolder() const;

    Toolchains *toolchains_;
    QNetworkAccessManager *network_;
    SourceResolver resolver_;
    QList<InstallStep> steps_;
    int index_ = -1;
    bool running_ = false;
    bool ok_ = true;
    // The current step's work: one at a time.
    QPointer<QNetworkReply> reply_;
    QPointer<QProcess> process_;
    std::unique_ptr<QFile> file_;
    // Incremented by cancel(): callbacks of an older generation are ignored.
    int generation_ = 0;
};
