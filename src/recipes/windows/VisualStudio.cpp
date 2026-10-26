// SPDX-License-Identifier: GPL-3.0-only

#include "VisualStudio.h"

#include "recipes/DeveloperEnvironment.h"

#include <QDir>
#include <QFileInfo>
#include <QObject>
#include <QProcess>

namespace VisualStudio {
namespace {

struct Capture {
    std::optional<QMap<QString, QString>> variables;
    QString error;
};

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

Capture capture()
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

} // namespace

std::optional<QMap<QString, QString>> environment(QString *error)
{
    static const Capture captured = capture();
    if (!captured.variables)
        *error = captured.error;
    return captured.variables;
}

} // namespace VisualStudio
