#pragma once

#include "toolchains/InstallPlan.h"

#include <QWidget>

class Installer;
class QLabel;
class QProgressBar;
class QVBoxLayout;
class Toolchains;

// The downloads of a project's tools as they happen: one line per step,
// with its progress, then whether it worked or why not.
class ProvisioningView : public QWidget {
    Q_OBJECT

public:
    explicit ProvisioningView(Toolchains *toolchains, QWidget *parent = nullptr);

    void start(const QList<InstallStep> &steps);
    void cancel();
    bool isRunning() const;

    // The steps that failed in the last run.
    QList<InstallStep> failedSteps() const { return failed_; }

signals:
    void finished(bool ok);

private:
    struct Line {
        QLabel *status;
        QProgressBar *bar;
    };

    void onProgress(int index, qint64 received, qint64 total);
    void onStepFinished(int index, const QString &error);

    Installer *installer_;
    QWidget *card_ = nullptr;
    QVBoxLayout *layout_;
    QList<Line> lines_;
    QList<InstallStep> steps_;
    QList<InstallStep> failed_;
};
