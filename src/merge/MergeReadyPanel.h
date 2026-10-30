// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "git/Integration.h"
#include "ui/Theme.h"

#include <QWidget>

class QLabel;
class QPlainTextEdit;
class QProgressBar;

// The final review before Git commits the resolutions. Keeps the message
// edited by the user across repository refreshes and failed commit hooks.
class MergeReadyPanel : public QWidget {
    Q_OBJECT

public:
    explicit MergeReadyPanel(QWidget *parent);
    void showSummary(const QString &operation, const QStringList &paths, const QString &message);
    void setFinalizing(bool finalizing);
    void applyTheme(const Theme &theme);
    QString message() const;

signals:
    void messageChanged();

private:
    QLabel *icon_;
    QLabel *title_;
    QLabel *description_;
    QLabel *files_;
    QLabel *messageLabel_;
    QPlainTextEdit *message_;
    QLabel *hint_;
    QProgressBar *progress_;
    QString operation_;
    bool messageLoaded_ = false;
};
