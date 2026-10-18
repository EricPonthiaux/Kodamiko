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

void load(const QString &filePath, QObject *context, const std::function<void(std::optional<Style>)> &done)
{
    auto *process = new QProcess(context);
    auto *timer = new QTimer(process);
    timer->setSingleShot(true);
    QObject::connect(timer, &QTimer::timeout, process, [process] { process->kill(); });
    // Received by `context`: its destruction disconnects them before it
    // deletes the process, so `done` never runs then.
    QObject::connect(process, &QProcess::errorOccurred, context, [process, done](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart)
            return; // the end is reported by finished
        process->deleteLater();
        done(std::nullopt);
    });
    QObject::connect(process, &QProcess::finished, context, [process, done](int exitCode, QProcess::ExitStatus status) {
        process->deleteLater();
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
