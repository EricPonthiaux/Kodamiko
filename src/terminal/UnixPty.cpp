#include "UnixPty.h"

#include <QFileInfo>
#include <QSocketNotifier>
#include <QStandardPaths>

#include <cerrno>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <unistd.h>

#if defined(Q_OS_MACOS)
#include <util.h>
#elif defined(Q_OS_FREEBSD)
#include <libutil.h>
#else
#include <pty.h>
#endif

namespace {

// Tableau de chaînes C terminé par nullptr, pour execve().
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
        errorString_ = tr("Programme introuvable : %1").arg(spec.program);
        return false;
    }

    // Tout est préparé avant fork() : dans l'enfant, seuls des appels
    // système simples sont permis jusqu'à execve().
    CStringArray argv(QStringList{program} + spec.arguments);
    CStringArray envp(spec.environment.toStringList());
    const QByteArray programPath = program.toLocal8Bit();
    const QByteArray workingDirectory = spec.workingDirectory.toLocal8Bit();
    winsize size = windowSize(cells);

    const pid_t pid = ::forkpty(&masterFd_, nullptr, nullptr, &size);
    if (pid < 0) {
        errorString_ = tr("Création du pseudo-terminal impossible : %1").arg(QString::fromLocal8Bit(std::strerror(errno)));
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

// Le PTY n'accepte qu'un tampon limité (quelques Ko) : un long collage est
// envoyé par morceaux, au rythme où le programme le lit.
void UnixPty::flushPendingWrites()
{
    while (!pendingWrites_.isEmpty()) {
        const ssize_t written = ::write(masterFd_, pendingWrites_.constData(), pendingWrites_.size());
        if (written > 0) {
            pendingWrites_.remove(0, written);
        } else if (written < 0 && errno == EINTR) {
            continue;
        } else {
            break; // EAGAIN : on réessaiera quand le PTY sera de nouveau prêt
        }
    }
    writeNotifier_->setEnabled(!pendingWrites_.isEmpty());
}

void UnixPty::resize(QSize cells)
{
    if (masterFd_ < 0)
        return;
    winsize size = windowSize(cells);
    ::ioctl(masterFd_, TIOCSWINSZ, &size);
}

void UnixPty::readAvailable()
{
    char buffer[65536];
    for (;;) {
        const ssize_t count = ::read(masterFd_, buffer, sizeof buffer);
        if (count > 0) {
            emit dataReceived(QByteArray(buffer, count));
            continue;
        }
        if (count < 0 && errno == EINTR)
            continue;
        if (count < 0 && errno == EAGAIN)
            return;
        // 0 ou EIO : le côté esclave est fermé, le programme est terminé.
        handleChildExit();
        return;
    }
}

void UnixPty::handleChildExit()
{
    readNotifier_->setEnabled(false);
    writeNotifier_->setEnabled(false);

    // Le PTY se ferme juste avant que le processus soit récupérable : on lui
    // laisse un court instant, sans jamais bloquer l'interface (un programme
    // peut fermer son terminal et continuer à tourner).
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
