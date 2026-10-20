// SPDX-License-Identifier: GPL-3.0-only

#include "ClangFormatStyle.h"

#include <QFileInfo>
#include <QHash>
#include <QProcess>
#include <QTimer>

namespace ClangFormatStyle {
namespace {

// --dump-config reads a few files: far from this, unless clang-format hangs.
constexpr int timeoutMs = 5000;

// The top-level "Key: value" lines of a dumped configuration.
QHash<QByteArray, QByteArray> topLevelValues(const QByteArray &dumpedConfig)
{
    QHash<QByteArray, QByteArray> values;
    for (const QByteArray &line : dumpedConfig.split('\n')) {
        if (line.isEmpty() || line.front() == ' ')
            continue;
        const qsizetype colon = line.indexOf(':');
        if (colon > 0)
            values.insert(line.left(colon), line.mid(colon + 1).trimmed());
    }
    return values;
}

} // namespace

bool appliesTo(const QString &languageId)
{
    return languageId == QLatin1String("c") || languageId == QLatin1String("cpp") ||
           languageId == QLatin1String("java");
}

std::optional<Style> parse(const QByteArray &dumpedConfig)
{
    const QHash<QByteArray, QByteArray> values = topLevelValues(dumpedConfig);
    bool hasIndentWidth = false;
    const int indentWidth = values.value("IndentWidth").toInt(&hasIndentWidth);
    if (!hasIndentWidth || indentWidth <= 0)
        return std::nullopt;

    Style style;
    style.indentWidth = indentWidth;
    style.tabWidth = values.value("TabWidth", QByteArray::number(indentWidth)).toInt();
    style.columnLimit = values.value("ColumnLimit").toInt();
    style.useTabs = values.value("UseTab", "Never") != "Never";
    return style;
}

void load(const QString &filePath, const QObject *context, const std::function<void(std::optional<Style>)> &done)
{
    // Not a child of `context`: a widget deletes its children before
    // disconnecting them, and the process's end would then reach a
    // half-destroyed `context`. The process deletes itself once it ends.
    auto *process = new QProcess;
    auto *timer = new QTimer(process);
    timer->setSingleShot(true);
    QObject::connect(timer, &QTimer::timeout, process, [process] { process->kill(); });
    QObject::connect(process, &QProcess::finished, process, &QObject::deleteLater);
    QObject::connect(process, &QProcess::errorOccurred, process, [process](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart)
            process->deleteLater();
    });
    // Received by `context`: its destruction disconnects them, so `done`
    // never runs then.
    QObject::connect(process, &QProcess::errorOccurred, context, [done](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart)
            done(std::nullopt); // otherwise, the end is reported by finished
    });
    QObject::connect(process, &QProcess::finished, context, [process, done](int exitCode, QProcess::ExitStatus status) {
        const bool succeeded = status == QProcess::NormalExit && exitCode == 0;
        done(succeeded ? parse(process->readAllStandardOutput()) : std::nullopt);
    });

    // clang-format looks for .clang-format from the file's directory up.
    process->setWorkingDirectory(QFileInfo(filePath).absolutePath());
    process->start(QStringLiteral("clang-format"),
                   {QStringLiteral("--dump-config"), QStringLiteral("--assume-filename=") + filePath});
    timer->start(timeoutMs);
}

} // namespace ClangFormatStyle
