#include "BuildOutputParser.h"

#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>

namespace {

using Severity = Diagnostic::Severity;

constexpr int maximumMessageLength = 400;

// "error: x", "fatal error: x", "warning C4996: x", "error TS2322: x".
bool severityOf(const QString &rest, Severity *severity, QString *message)
{
    static const QRegularExpression pattern(
        QStringLiteral("^(fatal error|error|warning)(?:\\s+[A-Z]+\\d+)?\\s*:\\s*(.*)$"),
        QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch match = pattern.match(rest);
    if (!match.hasMatch())
        return false;
    *severity = match.captured(1).compare(QLatin1String("warning"), Qt::CaseInsensitive) == 0 ? Severity::Warning
                                                                                              : Severity::Error;
    *message = match.captured(2).trimmed();
    return true;
}

int number(const QString &text, int fallback = 0)
{
    bool ok = false;
    const int value = text.toInt(&ok);
    return ok ? value : fallback;
}

} // namespace

BuildOutputParser::BuildOutputParser(const QString &workingDirectory, const QString &projectFolder)
    : workingDirectory_(workingDirectory)
    , projectFolder_(projectFolder)
{
}

void BuildOutputParser::feed(QByteArrayView data)
{
    buffer_.append(data);
    qsizetype start = 0;
    for (qsizetype newline = buffer_.indexOf('\n'); newline >= 0; newline = buffer_.indexOf('\n', start)) {
        parseLine(cleanLine(QString::fromUtf8(buffer_.mid(start, newline - start))));
        start = newline + 1;
    }
    buffer_.remove(0, start);
}

void BuildOutputParser::finish()
{
    if (!buffer_.isEmpty())
        parseLine(cleanLine(QString::fromUtf8(buffer_)));
    buffer_.clear();
    flushPending();
}

QString BuildOutputParser::cleanLine(const QString &line)
{
    static const QRegularExpression escapes(
        QStringLiteral("\x1b\\][^\x07\x1b]*(?:\x07|\x1b\\\\)" // OSC: title, hyperlink
                       "|\x1b[P^_][^\x1b]*\x1b\\\\"           // DCS, PM, APC
                       "|\x1b\\[[0-?]*[ -/]*[@-~]"            // CSI: colors, erasing
                       "|\x1b[()][A-Za-z0-9]|\x1b[=>78DEHMc]" // character sets, modes
                       "|[\x00-\x08\x0b\x0c\x0e-\x1f\x7f]")); // other controls, tab kept
    QString text = line;
    while (text.endsWith(QLatin1Char('\r')))
        text.chop(1);
    if (const qsizetype carriageReturn = text.lastIndexOf(QLatin1Char('\r')); carriageReturn >= 0)
        text = text.mid(carriageReturn + 1);
    return text.remove(escapes);
}

void BuildOutputParser::parseLine(const QString &line)
{
    if (pending_ != Pending::None && continuePending(line))
        return;

    if (line.trimmed().isEmpty()) {
        eslintFile_.clear();
        return;
    }

    Severity severity = Severity::Error;
    QString message;

    // Maven: [ERROR] /src/A.java:[12,5] cannot find symbol
    static const QRegularExpression maven(
        QStringLiteral("^\\[(ERROR|WARNING)\\]\\s+(.+?):\\[(\\d+),(\\d+)\\]\\s*(.*)$"));
    if (const QRegularExpressionMatch match = maven.match(line); match.hasMatch()) {
        add(match.captured(2), number(match.captured(3)), number(match.captured(4)),
            match.captured(1) == QLatin1String("WARNING") ? Severity::Warning : Severity::Error, match.captured(5));
        return;
    }

    // Kotlin: e: file:///src/A.kt:12:5 message, or e: /src/A.kt: (12, 5): message
    static const QRegularExpression kotlin(QStringLiteral("^(e|w):\\s+(?:file://)?(.+?):(\\d+):(\\d+)\\s+(.*)$"));
    static const QRegularExpression oldKotlin(QStringLiteral("^(e|w):\\s+(.+?):\\s*\\((\\d+),\\s*(\\d+)\\):\\s*(.*)$"));
    for (const QRegularExpression *pattern : {&kotlin, &oldKotlin}) {
        if (const QRegularExpressionMatch match = pattern->match(line); match.hasMatch()) {
            add(match.captured(2), number(match.captured(3)), number(match.captured(4)),
                match.captured(1) == QLatin1String("w") ? Severity::Warning : Severity::Error, match.captured(5));
            return;
        }
    }

    // Rust: "error[E0308]: mismatched types", then "  --> src/main.rs:4:5".
    static const QRegularExpression rust(QStringLiteral("^(error|warning)(?:\\[[A-Z]\\d+\\])?:\\s*(.+)$"));
    if (const QRegularExpressionMatch match = rust.match(line); match.hasMatch()) {
        pending_ = Pending::Rust;
        pendingSeverity_ = match.captured(1) == QLatin1String("warning") ? Severity::Warning : Severity::Error;
        pendingMessage_ = match.captured(2).trimmed();
        return;
    }

    // CMake: "CMake Error at CMakeLists.txt:5 (add_executable):", the
    // message on the indented lines that follow.
    static const QRegularExpression cmake(
        QStringLiteral("^CMake (Error|Warning)(?: \\(dev\\))? at (.+?):(\\d+)(?:\\s*\\(.*\\))?:\\s*$"));
    if (const QRegularExpressionMatch match = cmake.match(line); match.hasMatch()) {
        pending_ = Pending::CMake;
        pendingSeverity_ = match.captured(1) == QLatin1String("Warning") ? Severity::Warning : Severity::Error;
        pendingPath_ = match.captured(2);
        pendingLine_ = number(match.captured(3));
        pendingMessage_.clear();
        return;
    }

    if (line.startsWith(QLatin1String("Traceback (most recent call last)"))) {
        pending_ = Pending::Python;
        pythonPath_.clear();
        return;
    }

    // ESLint's stylish output: the file alone on a line, then
    // "  12:5  error  Message  rule-name" under it.
    static const QRegularExpression eslint(
        QStringLiteral("^\\s+(\\d+):(\\d+)\\s+(error|warning)\\s+(.+?)(?:\\s{2,}[\\w@/.-]+)?\\s*$"));
    if (!eslintFile_.isEmpty()) {
        if (const QRegularExpressionMatch match = eslint.match(line); match.hasMatch()) {
            add(eslintFile_, number(match.captured(1)), number(match.captured(2)),
                match.captured(3) == QLatin1String("warning") ? Severity::Warning : Severity::Error, match.captured(4));
            return;
        }
    }
    if (!line.front().isSpace() && QDir::isAbsolutePath(line.trimmed()) && QFileInfo(line.trimmed()).isFile()) {
        eslintFile_ = line.trimmed();
        return;
    }

    // MSVC and tsc: src/a.ts(12,5): error TS2322: message
    static const QRegularExpression parenthesized(
        QStringLiteral("^\\s*(.+?)\\((\\d+)(?:,(\\d+))?(?:,\\d+,\\d+)?\\)\\s*:\\s*(.*)$"));
    if (const QRegularExpressionMatch match = parenthesized.match(line);
        match.hasMatch() && severityOf(match.captured(4), &severity, &message)) {
        add(match.captured(1), number(match.captured(2)), number(match.captured(3)), severity, message);
        return;
    }

    // GCC, Clang, javac, Go, tsc --pretty: src/a.cpp:12:5: error: message
    static const QRegularExpression colon(QStringLiteral("^\\s*(.+?):(\\d+)(?::(\\d+))?(?::|\\s+-\\s)\\s*(.*)$"));
    if (const QRegularExpressionMatch match = colon.match(line); match.hasMatch()) {
        const QString rest = match.captured(4);
        if (severityOf(rest, &severity, &message)) {
            add(match.captured(1), number(match.captured(2)), number(match.captured(3)), severity, message);
        } else if (match.captured(1).endsWith(QLatin1String(".go")) && !rest.trimmed().isEmpty()) {
            // Go's compiler names no severity.
            add(match.captured(1), number(match.captured(2)), number(match.captured(3)), Severity::Error, rest);
        }
    }
}

bool BuildOutputParser::continuePending(const QString &line)
{
    switch (pending_) {
    case Pending::Rust: {
        static const QRegularExpression location(QStringLiteral("^\\s*-->\\s*(.+?):(\\d+):(\\d+)\\s*$"));
        if (const QRegularExpressionMatch match = location.match(line); match.hasMatch()) {
            add(match.captured(1), number(match.captured(2)), number(match.captured(3)), pendingSeverity_,
                pendingMessage_);
            pending_ = Pending::None;
            return true;
        }
        // Rust's own lines of code and notes, indented or behind "|".
        if (line.isEmpty() || line.front().isSpace() || line.startsWith(QLatin1Char('=')))
            return true;
        pending_ = Pending::None; // a header without a location
        return false;
    }
    case Pending::CMake:
        if (line.trimmed().isEmpty())
            return true;
        if (line.front().isSpace()) {
            if (pendingMessage_.size() < maximumMessageLength)
                pendingMessage_ += (pendingMessage_.isEmpty() ? QString() : QStringLiteral(" ")) + line.trimmed();
            return true;
        }
        flushPending();
        return false;
    case Pending::Python: {
        static const QRegularExpression frame(QStringLiteral("^\\s+File \"(.+?)\", line (\\d+)"));
        if (const QRegularExpressionMatch match = frame.match(line); match.hasMatch()) {
            // The deepest frame in the project's own code.
            const QString path = resolve(match.captured(1));
            if (!path.isEmpty() && (path.startsWith(projectFolder_ + QLatin1Char('/')))) {
                pythonPath_ = path;
                pythonLine_ = number(match.captured(2));
            }
            return true;
        }
        if (line.isEmpty() || line.front().isSpace())
            return true;
        pending_ = Pending::None;
        if (!pythonPath_.isEmpty())
            add(pythonPath_, pythonLine_, 0, Severity::Error, line.trimmed());
        return true;
    }
    case Pending::None:
        break;
    }
    return false;
}

void BuildOutputParser::flushPending()
{
    if (pending_ == Pending::CMake)
        add(pendingPath_, pendingLine_, 0, pendingSeverity_, pendingMessage_);
    pending_ = Pending::None;
}

void BuildOutputParser::add(const QString &path, int line, int column, Diagnostic::Severity severity,
                            const QString &message)
{
    const QString resolved = resolve(path);
    if (resolved.isEmpty() || line <= 0)
        return;
    Problem problem;
    problem.path = resolved;
    problem.line = line - 1;
    problem.column = column > 0 ? column - 1 : -1;
    problem.severity = severity;
    problem.message = message.trimmed().left(maximumMessageLength);
    const QString key = QStringLiteral("%1:%2:%3:%4:%5")
                            .arg(problem.path)
                            .arg(problem.line)
                            .arg(problem.column)
                            .arg(static_cast<int>(problem.severity))
                            .arg(problem.message);
    if (seen_.contains(key))
        return;
    seen_.insert(key);
    problems_.append(problem);
}

QString BuildOutputParser::resolve(QString path) const
{
    path = path.trimmed();
    if (path.startsWith(QLatin1String("file://")))
        path = path.mid(7);
    path = QDir::fromNativeSeparators(path);
    if (path.isEmpty())
        return {};
    if (QDir::isAbsolutePath(path))
        return QFileInfo(path).isFile() ? QDir::cleanPath(path) : QString();
    for (const QString &folder : {workingDirectory_, projectFolder_}) {
        const QString candidate = QDir::cleanPath(folder + QLatin1Char('/') + path);
        if (QFileInfo(candidate).isFile())
            return candidate;
    }
    return {};
}
