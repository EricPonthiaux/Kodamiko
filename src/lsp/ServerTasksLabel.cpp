// SPDX-License-Identifier: GPL-3.0-only

#include "ServerTasksLabel.h"

#include <QStringList>

namespace {

constexpr int marginPx = 8;

} // namespace

ServerTasksLabel::ServerTasksLabel(const LanguageServers *servers, QWidget *parent)
    : QLabel(parent)
    , servers_(servers)
{
    setContentsMargins(marginPx, 0, marginPx, 0);
    hide();
    connect(servers_, &LanguageServers::tasksChanged, this, &ServerTasksLabel::update);
}

QString ServerTasksLabel::describe(const LanguageServers::Task &task)
{
    QString text = task.server + QStringLiteral(": ") + task.title;
    if (task.percentage)
        text += QStringLiteral(" %1%").arg(*task.percentage);
    if (!task.message.isEmpty())
        text += QStringLiteral(" (%1)").arg(task.message);
    return text;
}

void ServerTasksLabel::update()
{
    const QList<LanguageServers::Task> tasks = servers_->tasks();
    setVisible(!tasks.isEmpty());
    if (tasks.isEmpty())
        return;
    QStringList lines;
    for (const LanguageServers::Task &task : tasks)
        lines.append(describe(task));
    const QString first = describe(tasks.first());
    setText(tasks.size() == 1 ? first : tr("%1, and %n more", nullptr, int(tasks.size() - 1)).arg(first));
    setToolTip(lines.join(QLatin1Char('\n')));
}
