#pragma once

#include "IPty.h"

#include <QWinEventNotifier>

#include <mutex>
#include <thread>

#include <windows.h>

// Windows PTY (ConPTY, Windows 10 1809+). Reading the output is blocking:
// it runs in a dedicated thread that forwards the bytes to the UI thread.
//
// ConPTY quirk: the program exiting does not close the output pipe; only
// closing the pseudo-console does. So we watch the process itself
// (QWinEventNotifier) to detect when it exits.
class ConPty : public IPty {
    Q_OBJECT

public:
    using IPty::IPty;
    ~ConPty() override;

    bool start(const ProcessSpec &spec, QSize cells) override;
    void write(const QByteArray &data) override;
    void resize(QSize cells) override;
    QString errorString() const override { return errorString_; }

private:
    void readLoop();
    void drainOutput();
    void onProcessExited();
    void closeConsole();
    void setError(const QString &action);

    HPCON pseudoConsole_ = nullptr;
    HANDLE inputWrite_ = INVALID_HANDLE_VALUE;
    HANDLE outputRead_ = INVALID_HANDLE_VALUE;
    HANDLE process_ = INVALID_HANDLE_VALUE;
    QWinEventNotifier *exitNotifier_ = nullptr;
    std::thread reader_;
    std::mutex outputMutex_;
    QByteArray pendingOutput_; // read by the thread, not yet forwarded (guarded by outputMutex_)
    QString errorString_;
};
