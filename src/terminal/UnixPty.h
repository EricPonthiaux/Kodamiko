// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "IPty.h"

#include <QtGlobal>

#include <sys/types.h>

class QSocketNotifier;

// POSIX PTY (Linux, macOS): forkpty() creates the master/slave pair and the
// child process; we read and write on the master side, non-blocking.
class UnixPty : public IPty {
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(UnixPty)

public:
    using IPty::IPty;
    ~UnixPty() override;

    bool start(const ProcessSpec &spec, QSize cells) override;
    void write(const QByteArray &data) override;
    void resize(QSize cells) override;

    QString errorString() const override { return errorString_; }

    bool hasForegroundJob() const override;

private:
    void readAvailable();
    void flushPendingWrites();
    void handleChildExit();

    int masterFd_ = -1;
    pid_t childPid_ = -1;
    QSocketNotifier *readNotifier_ = nullptr;
    QSocketNotifier *writeNotifier_ = nullptr;
    QByteArray pendingWrites_; // bytes the PTY has not accepted yet
    QString errorString_;
};
