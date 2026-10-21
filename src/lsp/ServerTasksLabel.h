// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "LanguageServers.h"

#include <QLabel>

// Status bar label of what the language servers are busy with ("clangd:
// indexing 42%"), all of their tasks in its tooltip; hidden when they are
// idle.
class ServerTasksLabel : public QLabel {
    Q_OBJECT

public:
    // `servers` is not owned and must outlive the label.
    explicit ServerTasksLabel(const LanguageServers *servers, QWidget *parent = nullptr);

    // "clangd: indexing 42% (3/9)". Public for tests.
    static QString describe(const LanguageServers::Task &task);

private:
    void update();

    const LanguageServers *servers_;
};
