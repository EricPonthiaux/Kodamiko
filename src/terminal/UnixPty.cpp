// SPDX-License-Identifier: GPL-3.0-only

#include "UnixPty.h"

#include <QFileInfo>
#include <QSocketNotifier>
#include <QStandardPaths>

#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <csignal>
#include <cstring>

#ifdef Q_OS_MACOS
#include <util.h>
#elif defined(Q_OS_FREEBSD)
#include <libutil.h>
#else
#include <pty.h>
#endif

namespace {

// nullptr-terminated array of C strings, for execve().
class CStringArray {
public:
    explicit CStringArray(const QStringList &strings)
    {
        for (const QString &string : strings)
            storage_.append(string.toLocal8Bit());
        for (QByteArray &bytes : storage_)
            pointers_.push_back(bytes.data());
        pointers_.push_back(nullptr);
    }

    char *const *data() { return pointers_.data(); }

private:
    QList<QByteArray> storage_;
    std::vector<char *> pointers_;
};

winsize windowSize(QSize cells)
{
    winsize size{};
    size.ws_col = static_cast<unsigned short>(cells.width());
    size.ws_row = static_cast<unsigned short>(cells.height());
    return size;
}

QString resolveProgram(const ProcessSpec &spec)
{
    if (QFileInfo(spec.program).isAbsolute())
        return spec.program;
    const QStringList searchPath = spec.environment.value(QStringLiteral("PATH")).split(QLatin1Char(':'));
    return QStandardPaths::findExecutable(spec.program, searchPath);
}

} // namespace

UnixPty::~UnixPty()
{
    if (childPid_ > 0) {
        ::kill(childPid_, SIGHUP);
        ::waitpid(childPid_, nullptr, WNOHANG);
    }
    if (masterFd_ >= 0)
        ::close(masterFd_);
}

bool UnixPty::start(const ProcessSpec &spec, QSize cells)
{
    const QString program = resolveProgram(spec);
    if (program.isEmpty()) {
        errorString_ = tr("Program not found: %1").arg(spec.program);
        return false;
    }

    // Everything is prepared before fork(): in the child, only simple system
    // calls are allowed until execve().
    CStringArray argv(QStringList{program} + spec.arguments);
    CStringArray envp(spec.environment.toStringList());
    const QByteArray programPath = program.toLocal8Bit();
    const QByteArray workingDirectory = spec.workingDirectory.toLocal8Bit();
    // Not const: macOS declares forkpty() with a non-const winsize pointer.
    winsize size = windowSize(cells);

    const pid_t pid = ::forkpty(&masterFd_, nullptr, nullptr, &size);
    if (pid < 0) {
        errorString_ = tr("Cannot create the pseudo-terminal: %1").arg(QString::fromLocal8Bit(std::strerror(errno)));
        return false;
    }
    if (pid == 0) {
        if (!workingDirectory.isEmpty() && ::chdir(workingDirectory.constData()) != 0)
            ::_exit(126);
        ::execve(programPath.constData(), argv.data(), envp.data());
        ::_exit(127);
    }

    childPid_ = pid;
    ::fcntl(masterFd_, F_SETFL, ::fcntl(masterFd_, F_GETFL) | O_NONBLOCK);
    ::fcntl(masterFd_, F_SETFD, FD_CLOEXEC);

    readNotifier_ = new QSocketNotifier(masterFd_, QSocketNotifier::Read, this);
    connect(readNotifier_, &QSocketNotifier::activated, this, &UnixPty::readAvailable);
    writeNotifier_ = new QSocketNotifier(masterFd_, QSocketNotifier::Write, this);
    writeNotifier_->setEnabled(false);
    connect(writeNotifier_, &QSocketNotifier::activated, this, &UnixPty::flushPendingWrites);
    return true;
}

void UnixPty::write(const QByteArray &data)
{
    if (masterFd_ < 0)
        return;
    pendingWrites_ += data;
    flushPendingWrites();
}

// The PTY only accepts a limited buffer (a few KB): a long paste is sent in
// chunks, at the pace the program reads it.
void UnixPty::flushPendingWrites()
{
    // Written bytes are skipped with an offset, and the buffer is compacted
    // once per call: removing from the front after each write would be
    // quadratic for a long paste.
    qsizetype offset = 0;
    while (offset < pendingWrites_.size()) {
        const ssize_t written =
            ::write(masterFd_, pendingWrites_.constData() + offset, size_t(pendingWrites_.size() - offset));
        if (written > 0) {
            offset += written;
        } else if (written < 0 && errno == EINTR) {
            continue;
        } else {
            break; // EAGAIN: retry when the PTY is ready again
        }
    }
    pendingWrites_.remove(0, offset);
    writeNotifier_->setEnabled(!pendingWrites_.isEmpty());
}

void UnixPty::resize(QSize cells)
{
    if (masterFd_ < 0)
        return;
    winsize size = windowSize(cells);
    ::ioctl(masterFd_, TIOCSWINSZ, &size);
}

// Everything available is read into one chunk, so the screen is parsed and
// repainted once per batch. The batch is bounded: a program that writes as
// fast as we parse (`cat` of a big file, `yes`) must not starve input and
// painting; the notifier fires again on the next event loop iteration.
bool UnixPty::hasForegroundJob() const
{
    if (masterFd_ < 0)
        return false;
    // forkpty() makes the child a session leader: its process group holds
    // the terminal until a shell gives it to a job it runs.
    const pid_t foreground = ::tcgetpgrp(masterFd_);
    return foreground > 0 && foreground != childPid_;
}

void UnixPty::readAvailable()
{
    constexpr qsizetype chunkSize = 65536;
    constexpr qsizetype maxBatch = 4 * chunkSize;
    QByteArray batch;
    bool exited = false;
    while (batch.size() < maxBatch) {
        const qsizetype start = batch.size();
        batch.resize(start + chunkSize);
        const ssize_t count = ::read(masterFd_, batch.data() + start, size_t(chunkSize));
        batch.resize(start + std::max<qsizetype>(count, 0));
        if (count > 0)
            continue;
        if (count < 0 && errno == EINTR)
            continue;
        // 0 or EIO: the slave side is closed, the program has exited.
        const bool wouldBlock = count < 0 && errno == EAGAIN;
        exited = !wouldBlock;
        break;
    }
    if (!batch.isEmpty())
        emit dataReceived(batch);
    if (exited)
        handleChildExit();
}

void UnixPty::handleChildExit()
{
    readNotifier_->setEnabled(false);
    writeNotifier_->setEnabled(false);

    // The PTY closes just before the process can be reaped: we give it a
    // brief moment, without ever blocking the UI (a program may close its
    // terminal and keep running).
    int status = 0;
    int exitCode = -1;
    for (int attempt = 0; attempt < 20; ++attempt) {
        if (::waitpid(childPid_, &status, WNOHANG) == childPid_) {
            exitCode = WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
            childPid_ = -1;
            break;
        }
        ::usleep(5000);
    }
    emit finished(exitCode);
}

std::unique_ptr<IPty> createPty()
{
    return std::make_unique<UnixPty>();
}
