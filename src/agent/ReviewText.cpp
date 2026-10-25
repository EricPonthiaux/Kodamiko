// SPDX-License-Identifier: GPL-3.0-only

#include "ReviewText.h"

#include "ReviewSession.h"

#include <QCoreApplication>

#include <algorithm>

namespace ReviewText {

namespace {

QString tr(const char *text)
{
    return QCoreApplication::translate("ReviewText", text);
}

} // namespace

QString errorColor(const QPalette &palette)
{
    return palette.color(QPalette::Window).lightness() < 128 ? QStringLiteral("#e06c75") : QStringLiteral("#e45649");
}

QString warningColor(const QPalette &palette)
{
    return palette.color(QPalette::Window).lightness() < 128 ? QStringLiteral("#d29922") : QStringLiteral("#9a6700");
}

QString checkFailure(const ReviewRequest::Check &check)
{
    return tr("`%1` failed with exit code %2: fix the code, or review it anyway.")
        .arg(check.command, QString::number(check.result.exitCode));
}

QString summary(const ReviewSession &session)
{
    if (!session.findings().isEmpty())
        return session.summary();
    const QString none = tr("No problems found.");
    return session.summary().isEmpty() ? none : session.summary() + QLatin1Char('\n') + none;
}

QString lastSteps(const QStringList &steps, int count, const QPalette &palette)
{
    const QStringList shown = steps.mid(std::max<qsizetype>(0, steps.size() - count));
    const QString muted = palette.color(QPalette::PlaceholderText).name();
    QStringList lines;
    for (qsizetype i = 0; i < shown.size(); ++i) {
        const QString text = shown.at(i).toHtmlEscaped();
        lines.append(i + 1 == shown.size() ? text + QStringLiteral("…")
                                           : QStringLiteral("<span style=\"color:%1\">%2</span>").arg(muted, text));
    }
    return lines.join(QStringLiteral("<br>"));
}

QString elapsed(std::chrono::milliseconds time)
{
    const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(time).count();
    return QStringLiteral("%1:%2").arg(seconds / 60).arg(seconds % 60, 2, 10, QLatin1Char('0'));
}

} // namespace ReviewText
