// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "ClaudeMark.h"
#include "agent/AgentProvider.h"

// Claude Code: the `claude` CLI started by ClaudeLauncher, its hooks' JSON
// and their replies.
class ClaudeCodeProvider final : public AgentProvider {
public:
    AgentProviderId id() const override { return AgentProviderId::ClaudeCode; }

    QString name() const override;
    QString iconName() const override;
    QColor brandColor() const override;

    const AgentMark &mark() const override { return mark_; }

    AgentFeatures features() const override;

    QString findExecutable() const override;
    std::optional<ProcessSpec> sessionProcess(const QString &workingDirectory,
                                              const QString &hookServerName,
                                              const AgentSessionOptions &options,
                                              QString *error) const override;

    std::optional<ProcessSpec> fileRunProcess(const AgentFileRun &run, QString *error) const override;
    QList<AgentModel> models() const override;
    std::optional<ProcessSpec> reviewRunProcess(const AgentReviewRun &run, QString *error) const override;
    AgentRunEvent parseRunOutput(const QByteArray &line, const QString &workingDirectory) const override;

    std::optional<HookEvent> parseHook(const QByteArray &json) const override;
    QJsonObject promptContextReply(const QString &context) const override;
    QJsonObject denyEditReply(const QString &reason) const override;
    QJsonObject blockReply(const QString &reason) const override;

private:
    ClaudeMark mark_;
};
