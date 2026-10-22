// SPDX-License-Identifier: GPL-3.0-only

#include "DeveloperEnvironment.h"

#include <QDir>
#include <QFileInfo>
#include <QObject>
#include <QProcess>
#include <QVersionNumber>

namespace {

struct Capture {
    std::optional<QMap<QString, QString>> variables;
    QString error;
};

#ifdef Q_OS_WIN
constexpr int vswhereTimeoutMs = 10'000;
// vcvars64 takes about two seconds; a slow disk or antivirus, much more.
constexpr int vcvarsTimeoutMs = 60'000;

QString visualStudioInstaller()
{
    return QDir::fromNativeSeparators(
               qEnvironmentVariable("ProgramFiles(x86)", QStringLiteral("C:/Program Files (x86)"))) +
           QStringLiteral("/Microsoft Visual Studio/Installer");
}

// The newest Visual Studio with the x64 C++ tools, empty if none.
QString visualStudioWithCppTools(const QString &vswhere)
{
    QProcess process;
    process.start(vswhere,
                  {QStringLiteral("-latest"), QStringLiteral("-products"), QStringLiteral("*"),
                   QStringLiteral("-requires"), QStringLiteral("Microsoft.VisualStudio.Component.VC.Tools.x86.x64"),
                   QStringLiteral("-property"), QStringLiteral("installationPath")});
    if (!process.waitForFinished(vswhereTimeoutMs))
        return {};
    return QString::fromLocal8Bit(process.readAllStandardOutput()).section(QLatin1Char('\n'), 0, 0).trimmed();
}

Capture captureVisualStudio()
{
    const QString installer = visualStudioInstaller();
    const QString vswhere = installer + QStringLiteral("/vswhere.exe");
    if (!QFileInfo::exists(vswhere))
        return {
            .variables = std::nullopt,
            .error =
                QObject::tr("Visual Studio is not installed (%1 is missing)").arg(QDir::toNativeSeparators(vswhere))};
    const QString installation = visualStudioWithCppTools(vswhere);
    if (installation.isEmpty())
        return {.variables = std::nullopt,
                .error = QObject::tr("No Visual Studio has the C++ tools: add the \"Desktop development with C++\" "
                                     "workload in the Visual Studio Installer")};
    const QString vcvars = QDir(installation).filePath(QStringLiteral("VC/Auxiliary/Build/vcvars64.bat"));
    if (!QFileInfo::exists(vcvars))
        return {.variables = std::nullopt, .error = QObject::tr("%1 is missing").arg(QDir::toNativeSeparators(vcvars))};

    QProcess cmd;
    // vcvars64 asks vswhere for Visual Studio's CMake and Ninja: without
    // it in the PATH, they are left out.
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("PATH"), QDir::toNativeSeparators(installer) + QLatin1Char(';') +
                                                   environment.value(QStringLiteral("PATH")));
    cmd.setProcessEnvironment(environment);
    cmd.setProgram(qEnvironmentVariable("ComSpec", QStringLiteral("cmd.exe")));
    // Native: cmd's own quoting of a path with spaces, which QProcess's
    // would break.
    cmd.setNativeArguments(QStringLiteral("/d /s /c \"\"%1\" >nul && set\"").arg(QDir::toNativeSeparators(vcvars)));
    cmd.start();
    const bool finished = cmd.waitForFinished(vcvarsTimeoutMs);
    if (!finished || cmd.exitStatus() != QProcess::NormalExit || cmd.exitCode() != 0)
        return {.variables = std::nullopt,
                .error = QObject::tr("%1 failed: %2")
                             .arg(QDir::toNativeSeparators(vcvars),
                                  finished ? QString::fromLocal8Bit(cmd.readAllStandardError()).trimmed()
                                           : QObject::tr("no answer after %1 s").arg(vcvarsTimeoutMs / 1000))};
    return {.variables = DeveloperEnvironment::changedVariables(cmd.readAllStandardOutput(),
                                                                QProcessEnvironment::systemEnvironment()),
            .error = {}};
}
#else
Capture captureVisualStudio()
{
    return {.variables = std::nullopt, .error = QObject::tr("Visual Studio's environment exists on Windows only")};
}
#endif

} // namespace

namespace DeveloperEnvironment {

std::optional<QMap<QString, QString>> visualStudio(QString *error)
{
    static const Capture capture = captureVisualStudio();
    if (!capture.variables)
        *error = capture.error;
    return capture.variables;
}

QMap<QString, QString> changedVariables(const QByteArray &setOutput, const QProcessEnvironment &before)
{
    QMap<QString, QString> changed;
    for (const QString &line : QString::fromLocal8Bit(setOutput).split(QLatin1Char('\n'))) {
        const qsizetype equals = line.indexOf(QLatin1Char('='));
        // cmd's per-drive folders ("=C:=C:\...") start with "=".
        if (equals <= 0)
            continue;
        const QString name = line.left(equals);
        QString value = line.mid(equals + 1);
        if (value.endsWith(QLatin1Char('\r')))
            value.chop(1);
        if (!before.contains(name) || before.value(name) != value)
            changed.insert(name, value);
    }
    return changed;
}

} // namespace DeveloperEnvironment
