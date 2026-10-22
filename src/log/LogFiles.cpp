// SPDX-License-Identifier: GPL-3.0-only

#include "LogFiles.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QMutex>
#include <QMutexLocker>
#include <QSysInfo>

#include <cstdio>

namespace {

// The message handler is process-wide by nature (qInstallMessageHandler):
// so is the file it writes to. The mutex serializes the threads that log.
struct LogFile {
    QMutex mutex;
    QFile file;
    QString program;
    QtMessageHandler previous = nullptr;
    bool hasFailed = false;
};

LogFile &logFile()
{
    static LogFile log;
    return log;
}

QString buildType()
{
#ifdef QT_DEBUG
    return QStringLiteral("debug");
#else
    return QStringLiteral("release");
#endif
}

QByteArray header(const QString &program)
{
    const QString text = QStringLiteral("%1 %2 (%3 build), Qt %4, %5, %6, arguments: %7\n")
                             .arg(program, QStringLiteral(DUET_VERSION), buildType(), QString::fromLatin1(qVersion()),
                                  QSysInfo::prettyProductName(), QSysInfo::currentCpuArchitecture(),
                                  QCoreApplication::arguments().join(QLatin1Char(' ')));
    return text.toUtf8();
}

QString levelName(QtMsgType type)
{
    switch (type) {
    case QtDebugMsg:
        return QStringLiteral("debug");
    case QtInfoMsg:
        return QStringLiteral("info");
    case QtWarningMsg:
        return QStringLiteral("warning");
    case QtCriticalMsg:
        return QStringLiteral("critical");
    case QtFatalMsg:
        return QStringLiteral("fatal");
    }
    return QStringLiteral("unknown");
}

// "2026-10-22 14:03:12.345 [warning] duet.lsp: message (lsp/LspClient.cpp:107)":
// the source location exists in debug builds only.
QByteArray formatLine(QtMsgType type, const QMessageLogContext &context, const QString &message)
{
    QString line = QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz"));
    line += QStringLiteral(" [%1] %2: %3").arg(levelName(type), QString::fromLatin1(context.category), message);
    if (context.file)
        line += QStringLiteral(" (%1:%2)").arg(QString::fromUtf8(context.file)).arg(context.line);
    line += QLatin1Char('\n');
    return line.toUtf8();
}

// Called with the mutex held. False when the folder or the file cannot be
// created: said once on the standard error, then the file is left alone.
bool openFile(LogFile &log)
{
    if (log.file.isOpen())
        return true;
    if (log.hasFailed)
        return false;
    const QString folder = LogFiles::folder();
    const QString name = QStringLiteral("%1-%2-%3.log")
                             .arg(log.program,
                                  QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss")))
                             .arg(QCoreApplication::applicationPid());
    log.file.setFileName(QDir(folder).filePath(name));
    if (QDir().mkpath(folder) && log.file.open(QIODevice::WriteOnly | QIODevice::Append)) {
        log.file.write(header(log.program));
        return true;
    }
    log.hasFailed = true;
    const QByteArray reason = QStringLiteral("Cannot write the log file %1: %2\n")
                                  .arg(QDir::toNativeSeparators(log.file.fileName()), log.file.errorString())
                                  .toUtf8();
    std::fputs(reason.constData(), stderr);
    return false;
}

void writeMessage(QtMsgType type, const QMessageLogContext &context, const QString &message)
{
    LogFile &log = logFile();
    {
        const QMutexLocker lock(&log.mutex);
        if (openFile(log)) {
            log.file.write(formatLine(type, context, message));
            log.file.flush();
        }
    }
    // The default handler prints to the standard error, and aborts on qFatal.
    if (log.previous)
        log.previous(type, context, message);
}

void uninstall()
{
    LogFile &log = logFile();
    qInstallMessageHandler(log.previous);
    const QMutexLocker lock(&log.mutex);
    log.file.close();
}

} // namespace

namespace LogFiles {

QString folder()
{
    QString overridden = qEnvironmentVariable("DUET_LOG_DIR");
    if (!overridden.isEmpty())
        return overridden;
    return QDir::homePath() + QStringLiteral("/.duet/logs");
}

void install(const QString &program)
{
    LogFile &log = logFile();
    Q_ASSERT(!log.previous);
    log.program = program;
    log.previous = qInstallMessageHandler(writeMessage);
    // Before the static LogFile is destroyed: messages logged later (Qt's
    // own global objects) go to the standard error alone.
    qAddPostRoutine(uninstall);
}

} // namespace LogFiles
