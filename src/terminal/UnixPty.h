#pragma once

#include "IPty.h"

#include <sys/types.h>

class QSocketNotifier;

// PTY POSIX (Linux, macOS) : forkpty() crée la paire maître/esclave et le
// processus enfant ; on lit et écrit côté maître, en non bloquant.
class UnixPty : public IPty {
    Q_OBJECT

public:
    using IPty::IPty;
    ~UnixPty() override;

    bool start(const ProcessSpec &spec, QSize cells) override;
    void write(const QByteArray &data) override;
    void resize(QSize cells) override;
    QString errorString() const override { return errorString_; }

private:
    void readAvailable();
    void flushPendingWrites();
    void handleChildExit();

    int masterFd_ = -1;
    pid_t childPid_ = -1;
    QSocketNotifier *readNotifier_ = nullptr;
    QSocketNotifier *writeNotifier_ = nullptr;
    QByteArray pendingWrites_; // octets que le PTY n'a pas encore acceptés
    QString errorString_;
};
