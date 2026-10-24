// SPDX-License-Identifier: GPL-3.0-only

#include "ReviewReport.h"

#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>

#include <algorithm>

namespace ReviewReport {

namespace {

QJsonObject stringProperty(const QString &description)
{
    return {{QStringLiteral("type"), QStringLiteral("string")}, {QStringLiteral("description"), description}};
}

QJsonObject findingSchema()
{
    const QJsonObject properties{
        {QStringLiteral("file"), stringProperty(QStringLiteral("The file's path, relative to the project."))},
        {QStringLiteral("line"),
         QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")},
                     {QStringLiteral("minimum"), 1},
                     {QStringLiteral("description"), QStringLiteral("The line of the problem, from 1.")}}},
        {QStringLiteral("severity"),
         QJsonObject{{QStringLiteral("enum"), QJsonArray{QStringLiteral("error"), QStringLiteral("warning")}},
                     {QStringLiteral("description"),
                      QStringLiteral("error: the code gives a wrong result, crashes or loses data. warning: it "
                                     "breaks a rule of the project, or fails only in a rare case.")}}},
        {QStringLiteral("problem"), stringProperty(QStringLiteral("What is wrong, in one or two sentences."))},
        {QStringLiteral("scenario"),
         stringProperty(QStringLiteral("A concrete case where the code fails: the input or state, and the wrong "
                                       "result."))},
    };
    return {{QStringLiteral("type"), QStringLiteral("object")},
            {QStringLiteral("properties"), properties},
            {QStringLiteral("required"),
             QJsonArray{QStringLiteral("file"), QStringLiteral("line"), QStringLiteral("severity"),
                        QStringLiteral("problem"), QStringLiteral("scenario")}}};
}

Finding findingFrom(const QJsonObject &object, const QString &projectFolder)
{
    const QString file = QDir::fromNativeSeparators(object.value(QLatin1String("file")).toString());
    const bool isWarning = object.value(QLatin1String("severity")).toString() == QLatin1String("warning");
    return {.path = QDir::cleanPath(QDir(projectFolder).absoluteFilePath(file)),
            .line = std::max(1, object.value(QLatin1String("line")).toInt(1)),
            .severity = isWarning ? Diagnostic::Severity::Warning : Diagnostic::Severity::Error,
            .problem = object.value(QLatin1String("problem")).toString().trimmed(),
            .scenario = object.value(QLatin1String("scenario")).toString().trimmed()};
}

QString severityName(Diagnostic::Severity severity)
{
    switch (severity) {
    case Diagnostic::Severity::Error:
        return QStringLiteral("error");
    case Diagnostic::Severity::Warning:
        return QStringLiteral("warning");
    }
    return {};
}

} // namespace

QJsonObject schema()
{
    const QJsonObject properties{
        {QStringLiteral("summary"),
         stringProperty(QStringLiteral("One or two sentences on the reviewed code as a whole."))},
        {QStringLiteral("findings"),
         QJsonObject{{QStringLiteral("type"), QStringLiteral("array")},
                     {QStringLiteral("items"), findingSchema()},
                     {QStringLiteral("description"),
                      QStringLiteral("The problems found, the most severe first; empty when there are none.")}}},
    };
    return {{QStringLiteral("type"), QStringLiteral("object")},
            {QStringLiteral("properties"), properties},
            {QStringLiteral("required"), QJsonArray{QStringLiteral("summary"), QStringLiteral("findings")}}};
}

std::optional<Report> parse(const QString &answer, const QString &projectFolder)
{
    const QJsonObject object = QJsonDocument::fromJson(answer.toUtf8()).object();
    const QJsonValue findings = object.value(QLatin1String("findings"));
    if (!findings.isArray())
        return std::nullopt;
    Report report{.summary = object.value(QLatin1String("summary")).toString().trimmed(), .findings = {}};
    for (const QJsonValue &value : findings.toArray()) {
        Finding finding = findingFrom(value.toObject(), projectFolder);
        if (!finding.problem.isEmpty())
            report.findings.append(std::move(finding));
    }
    return report;
}

Problem toProblem(const Finding &finding, const QString &source)
{
    QString message = finding.problem;
    if (!finding.scenario.isEmpty())
        message += QStringLiteral("\nWhen it fails: ") + finding.scenario;
    return {.path = finding.path,
            .line = finding.line - 1,
            .column = -1,
            .endLine = -1,
            .endColumn = -1,
            .severity = finding.severity,
            .message = message,
            .source = source};
}

QString toMarkdown(const QList<Finding> &findings, const QString &projectFolder)
{
    QStringList items;
    for (qsizetype i = 0; i < findings.size(); ++i) {
        const Finding &finding = findings.at(i);
        const QString path = QDir::toNativeSeparators(QDir(projectFolder).relativeFilePath(finding.path));
        QString item = QStringLiteral("%1. **%2:%3** (%4): %5")
                           .arg(QString::number(i + 1), path, QString::number(finding.line),
                                severityName(finding.severity), finding.problem);
        if (!finding.scenario.isEmpty())
            item += QStringLiteral("\n   When it fails: ") + finding.scenario;
        items.append(item);
    }
    return items.join(QLatin1Char('\n'));
}

} // namespace ReviewReport
