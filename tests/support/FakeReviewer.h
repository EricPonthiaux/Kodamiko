// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "agent/AgentProvider.h"
#include "agent/vendor/claude/ClaudeCodeProvider.h"

#include <utility>

// Claude, but its review is `git --version`, a program every machine of the
// tests has, whose line it reads as the answer it was given.
class FakeReviewer final : public AgentProvider {
public:
    // A report of one error, in a.cpp at line 3, summed up "Mostly sound.".
    static inline const QString oneFindingReport =
        QStringLiteral(R"({"summary": "Mostly sound.", "findings": [{"file": "a.cpp", "line": 3, "severity": "error",)"
                       R"( "problem": "Off by one.", "scenario": "An empty list."}]})");

    explicit FakeReviewer(QString answer)
        : answer_(std::move(answer))
    {
    }

    AgentProviderId id() const override { return claude_.id(); }

    QString name() const override { return claude_.name(); }

    QString iconName() const override { return claude_.iconName(); }

    QColor brandColor() const override { return claude_.brandColor(); }

    const AgentMark &mark() const override { return claude_.mark(); }

    AgentFeatures features() const override { return claude_.features(); }

    QString findExecutable() const override { return claude_.findExecutable(); }

    std::optional<ProcessSpec> sessionProcess(const QString &workingDirectory,
                                              const QString &hookServerName,
                                              const AgentSessionOptions &options,
                                              QString *error) const override
    {
        return claude_.sessionProcess(workingDirectory, hookServerName, options, error);
    }

    std::optional<ProcessSpec> fileRunProcess(const AgentFileRun &run, QString *error) const override
    {
        return claude_.fileRunProcess(run, error);
    }

    QList<AgentModel> models() const override { return claude_.models(); }

    std::optional<ProcessSpec> reviewRunProcess(const AgentReviewRun &run, QString *) const override
    {
        return ProcessSpec{.program = QStringLiteral("git"),
                           .arguments = {QStringLiteral("--version")},
                           .workingDirectory = run.workingDirectory,
                           .environment = QProcessEnvironment::systemEnvironment()};
    }

    AgentRunEvent parseRunOutput(const QByteArray &line, const QString &) const override
    {
        if (!line.startsWith("git version"))
            return {};
        return {.kind = AgentRunEvent::Kind::Finished, .text = answer_};
    }

    std::optional<HookEvent> parseHook(const QByteArray &json) const override { return claude_.parseHook(json); }

    QJsonObject promptContextReply(const QString &context) const override
    {
        return claude_.promptContextReply(context);
    }

    QJsonObject denyToolReply(const QString &reason) const override { return claude_.denyToolReply(reason); }

    QJsonObject blockReply(const QString &reason) const override { return claude_.blockReply(reason); }

    QJsonObject permissionReply(const HookEvent &request, PermissionAnswer answer) const override
    {
        return claude_.permissionReply(request, answer);
    }

    QJsonObject editContextReply(const QString &context) const override { return claude_.editContextReply(context); }

private:
    ClaudeCodeProvider claude_;
    QString answer_;
};
