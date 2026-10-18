#pragma once

#include "McpRegistry.h"

#include <QCoreApplication>
#include <QThreadPool>

#include <exception>

// Runs a tool's work off the UI thread (reading a repository, walking the
// project) and replies on the UI thread. An exception becomes the tool's
// error.
template <typename Work>
void inBackground(Work work, McpReply reply)
{
    QThreadPool::globalInstance()->start([work = std::move(work), reply = std::move(reply)] {
        McpResult result;
        try {
            result = work();
        } catch (const std::exception &error) {
            result = McpResult::error(QString::fromUtf8(error.what()));
        }
        QMetaObject::invokeMethod(
            QCoreApplication::instance(), [reply, result] { reply(result); }, Qt::QueuedConnection);
    });
}
