// SPDX-License-Identifier: GPL-3.0-only

#include "ProjectCheck.h"

#include "system/Shell.h"

#include <QProcess>
#include <QStringList>

#include <algorithm>

namespace {

constexpr int killWaitMs = 1000;

} // namespace

QString ProjectCheck::Result::lastLines(int count) const
{
    const QStringList lines = output.trimmed().split(QLatin1Char('\n'));
    return lines.mid(std::max<qsizetype>(0, lines.size() - count)).join(QLatin1Char('\n'));
}

ProjectCheck::~ProjectCheck()
{
    stop();
}

void ProjectCheck::start(const QString &command, const QString &folder)
{
    stop();
    process_ = new QProcess(this);
    process_->setWorkingDirectory(folder);
    process_->setProcessChannelMode(QProcess::MergedChannels);
    connect(process_, &QProcess::finished, this, [this](int exitCode, QProcess::ExitStatus status) {
        finish(status == QProcess::NormalExit ? exitCode : -1);
    });
    connect(process_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart)
            finish(-1);
    });
    const ShellCommand check = Shell::systemShellRunning(command);
    process_->start(check.program, check.arguments);
}

void ProjectCheck::stop()
{
    if (!process_)
        return;
    process_->disconnect(this);
    process_->kill();
    process_->waitForFinished(killWaitMs);
    process_->deleteLater();
    process_ = nullptr;
}

void ProjectCheck::finish(int exitCode)
{
    const Result result{.exitCode = exitCode, .output = QString::fromLocal8Bit(process_->readAll())};
    stop();
    emit finished(result);
}
