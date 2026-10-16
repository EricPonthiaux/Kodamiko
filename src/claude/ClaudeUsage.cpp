#include "ClaudeUsage.h"

#include <QCoreApplication>
#include <QLocale>
#include <QStringList>

#include <algorithm>

namespace {

std::optional<double> number(const QJsonValue &value)
{
    if (value.isDouble())
        return value.toDouble();
    return std::nullopt;
}

// Reset times may come as seconds (or milliseconds) since the epoch, or as
// ISO 8601 text.
QDateTime dateTime(const QJsonValue &value)
{
    if (value.isDouble()) {
        const auto stamp = static_cast<qint64>(value.toDouble());
        return stamp > 100'000'000'000 ? QDateTime::fromMSecsSinceEpoch(stamp) : QDateTime::fromSecsSinceEpoch(stamp);
    }
    return QDateTime::fromString(value.toString(), Qt::ISODateWithMs);
}

QString percent(double value)
{
    return QStringLiteral("%1%").arg(qRound(value));
}

QString tr(const char *text)
{
    return QCoreApplication::translate("ClaudeUsage", text);
}

} // namespace

ClaudeUsage ClaudeUsage::fromStatusJson(const QJsonObject &status)
{
    ClaudeUsage usage;
    usage.model = status.value(QLatin1String("model")).toObject().value(QLatin1String("display_name")).toString();
    usage.contextPercent =
        number(status.value(QLatin1String("context_window")).toObject().value(QLatin1String("used_percentage")));

    const QJsonObject cost = status.value(QLatin1String("cost")).toObject();
    usage.costUsd = number(cost.value(QLatin1String("total_cost_usd")));
    usage.linesAdded = cost.value(QLatin1String("total_lines_added")).toInt();
    usage.linesRemoved = cost.value(QLatin1String("total_lines_removed")).toInt();

    const QJsonObject limits = status.value(QLatin1String("rate_limits")).toObject();
    const QJsonObject fiveHour = limits.value(QLatin1String("five_hour")).toObject();
    usage.fiveHourPercent = number(fiveHour.value(QLatin1String("used_percentage")));
    usage.fiveHourReset = dateTime(fiveHour.value(QLatin1String("resets_at")));
    const QJsonObject sevenDay = limits.value(QLatin1String("seven_day")).toObject();
    usage.sevenDayPercent = number(sevenDay.value(QLatin1String("used_percentage")));
    usage.sevenDayReset = dateTime(sevenDay.value(QLatin1String("resets_at")));
    return usage;
}

QString ClaudeUsage::summary() const
{
    QStringList parts;
    if (contextPercent)
        parts.append(tr("Context %1").arg(percent(*contextPercent)));
    if (fiveHourPercent)
        parts.append(tr("5h %1").arg(percent(*fiveHourPercent)));
    if (sevenDayPercent)
        parts.append(tr("7d %1").arg(percent(*sevenDayPercent)));
    return parts.join(QStringLiteral(" · "));
}

QString ClaudeUsage::details() const
{
    const QLocale locale;
    const auto resetText = [&](const QDateTime &reset) {
        return reset.isValid() ? tr(", resets %1").arg(locale.toString(reset.toLocalTime(), QLocale::ShortFormat))
                               : QString();
    };
    QStringList lines;
    if (!model.isEmpty())
        lines.append(tr("Model: %1").arg(model));
    if (contextPercent)
        lines.append(tr("Context window used: %1").arg(percent(*contextPercent)));
    if (fiveHourPercent)
        lines.append(tr("5-hour limit used: %1%2").arg(percent(*fiveHourPercent), resetText(fiveHourReset)));
    if (sevenDayPercent)
        lines.append(tr("7-day limit used: %1%2").arg(percent(*sevenDayPercent), resetText(sevenDayReset)));
    if (costUsd)
        lines.append(tr("Session cost: $%1").arg(locale.toString(*costUsd, 'f', 2)));
    if (linesAdded > 0 || linesRemoved > 0)
        lines.append(tr("Lines: +%1 −%2").arg(linesAdded).arg(linesRemoved));
    return lines.join(QLatin1Char('\n'));
}

double ClaudeUsage::highestPercent() const
{
    return std::max({contextPercent.value_or(0), fiveHourPercent.value_or(0), sevenDayPercent.value_or(0)});
}
