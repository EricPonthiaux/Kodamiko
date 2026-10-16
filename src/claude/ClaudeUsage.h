#pragma once

#include <QDateTime>
#include <QJsonObject>
#include <QString>

#include <optional>

// Session metrics that Claude Code passes to the status line command:
// model, context window filling, cost and plan usage limits.
struct ClaudeUsage {
    QString model;
    std::optional<double> contextPercent;
    std::optional<double> costUsd;
    int linesAdded = 0;
    int linesRemoved = 0;
    // Plan limits (Pro/Max subscriptions, after the first response).
    std::optional<double> fiveHourPercent;
    QDateTime fiveHourReset;
    std::optional<double> sevenDayPercent;
    QDateTime sevenDayReset;

    static ClaudeUsage fromStatusJson(const QJsonObject &status);

    // "Context 42% · 5h 30% · 7d 12%"; an empty string if nothing is known.
    QString summary() const;
    // Multi-line details for the tooltip.
    QString details() const;
    // The highest of the percentages, to color the summary.
    double highestPercent() const;
};
