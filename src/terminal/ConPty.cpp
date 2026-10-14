#include "ConPty.h"

#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QStandardPaths>

#include <vector>

namespace {

COORD consoleSize(QSize cells)
{
    return COORD{static_cast<SHORT>(cells.width()), static_cast<SHORT>(cells.height())};
}

// Guillemets selon les règles de CommandLineToArgvW : les barres obliques
// inverses ne sont doublées que devant un guillemet.
QString quoteArgument(const QString &argument)
{
    if (!argument.isEmpty() && !argument.contains(QRegularExpression(QStringLiteral("[\\s\"]"))))
        return argument;

    QString quoted = QStringLiteral("\"");
    int backslashes = 0;
    for (const QChar c : argument) {
        if (c == QLatin1Char('\\')) {
            ++backslashes;
            continue;
        }
        if (c == QLatin1Char('"'))
            quoted += QString(backslashes * 2 + 1, QLatin1Char('\\'));
        else
            quoted += QString(backslashes, QLatin1Char('\\'));
        backslashes = 0;
        quoted += c;
    }
    quoted += QString(backslashes * 2, QLatin1Char('\\'));
    quoted += QLatin1Char('"');
    return quoted;
}

std::wstring commandLine(const QString &program, const QStringList &arguments)
{
    QStringList parts{quoteArgument(QDir::toNativeSeparators(program))};
    for (const QString &argument : arguments)
        parts.append(quoteArgument(argument));
    return parts.join(QLatin1Char(' ')).toStdWString();
}

// Bloc d'environnement Unicode : « CLÉ=valeur\0 » répétés, terminé par « \0 ».
std::wstring environmentBlock(const QProcessEnvironment &environment)
{
    std::wstring block;
    for (const QString &entry : environment.toStringList()) {
        block += entry.toStdWString();
        block.push_back(L'\0');
    }
    block.push_back(L'\0');
    return block;
}

QString resolveProgram(const ProcessSpec &spec)
{
    if (QFileInfo(spec.program).isAbsolute())
        return spec.program;
    return QStandardPaths::findExecutable(spec.program);
}

} // namespace

ConPty::~ConPty()
{
    closeConsole();
    if (outputRead_ != INVALID_HANDLE_VALUE)
        CloseHandle(outputRead_);
    if (process_ != INVALID_HANDLE_VALUE)
        CloseHandle(process_);
}

// Fermer la console termine le programme s'il tourne encore, puis ferme le
// tube de sortie : ReadFile échoue alors dans le thread de lecture, qu'on
// peut attendre.
void ConPty::closeConsole()
{
    if (pseudoConsole_) {
        ClosePseudoConsole(pseudoConsole_);
        pseudoConsole_ = nullptr;
    }
    if (inputWrite_ != INVALID_HANDLE_VALUE) {
        CloseHandle(inputWrite_);
        inputWrite_ = INVALID_HANDLE_VALUE;
    }
    if (reader_.joinable())
        reader_.join();
}

bool ConPty::start(const ProcessSpec &spec, QSize cells)
{
    const QString program = resolveProgram(spec);
    if (program.isEmpty()) {
        errorString_ = tr("Programme introuvable : %1").arg(spec.program);
        return false;
    }

    // Deux tubes : nos écritures → entrée de la console, sortie de la console → nos lectures.
    HANDLE inputRead = INVALID_HANDLE_VALUE;
    HANDLE outputWrite = INVALID_HANDLE_VALUE;
    if (!CreatePipe(&inputRead, &inputWrite_, nullptr, 0) || !CreatePipe(&outputRead_, &outputWrite, nullptr, 0)) {
        setError(tr("Création des tubes"));
        return false;
    }

    const HRESULT created = CreatePseudoConsole(consoleSize(cells), inputRead, outputWrite, 0, &pseudoConsole_);
    // La console a dupliqué ses extrémités des tubes : les nôtres ne servent plus.
    CloseHandle(inputRead);
    CloseHandle(outputWrite);
    if (FAILED(created)) {
        pseudoConsole_ = nullptr;
        setError(tr("Création de la pseudo-console"));
        return false;
    }

    SIZE_T attributeListSize = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &attributeListSize);
    std::vector<char> attributeListBuffer(attributeListSize);
    auto attributeList = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attributeListBuffer.data());
    if (!InitializeProcThreadAttributeList(attributeList, 1, 0, &attributeListSize)
        || !UpdateProcThreadAttribute(attributeList, 0, PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE, pseudoConsole_,
                                      sizeof(HPCON), nullptr, nullptr)) {
        setError(tr("Préparation du processus"));
        return false;
    }

    STARTUPINFOEXW startupInfo{};
    startupInfo.StartupInfo.cb = sizeof(STARTUPINFOEXW);
    startupInfo.lpAttributeList = attributeList;

    std::wstring command = commandLine(program, spec.arguments);
    std::wstring environment = environmentBlock(spec.environment);
    const std::wstring workingDirectory = QDir::toNativeSeparators(spec.workingDirectory).toStdWString();

    PROCESS_INFORMATION processInfo{};
    const BOOL launched = CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE,
                                         EXTENDED_STARTUPINFO_PRESENT | CREATE_UNICODE_ENVIRONMENT,
                                         environment.data(),
                                         workingDirectory.empty() ? nullptr : workingDirectory.c_str(),
                                         &startupInfo.StartupInfo, &processInfo);
    DeleteProcThreadAttributeList(attributeList);
    if (!launched) {
        setError(tr("Lancement de %1").arg(program));
        return false;
    }

    process_ = processInfo.hProcess;
    CloseHandle(processInfo.hThread);
    reader_ = std::thread(&ConPty::readLoop, this);
    exitNotifier_ = new QWinEventNotifier(process_, this);
    connect(exitNotifier_, &QWinEventNotifier::activated, this, &ConPty::onProcessExited);
    return true;
}

void ConPty::readLoop()
{
    char buffer[65536];
    DWORD count = 0;
    while (ReadFile(outputRead_, buffer, sizeof buffer, &count, nullptr) && count > 0) {
        QByteArray chunk(buffer, static_cast<qsizetype>(count));
        QMetaObject::invokeMethod(this, [this, chunk] { emit dataReceived(chunk); }, Qt::QueuedConnection);
    }
}

void ConPty::onProcessExited()
{
    exitNotifier_->setEnabled(false);
    DWORD exitCode = 0;
    if (!GetExitCodeProcess(process_, &exitCode))
        exitCode = static_cast<DWORD>(-1);

    // La console vidée, le thread de lecture a transmis les dernières sorties
    // (en file d'attente) : `finished` est mis à la suite pour arriver après elles.
    closeConsole();
    QMetaObject::invokeMethod(this, [this, exitCode] { emit finished(static_cast<int>(exitCode)); },
                              Qt::QueuedConnection);
}

void ConPty::write(const QByteArray &data)
{
    if (inputWrite_ == INVALID_HANDLE_VALUE)
        return;
    const char *remaining = data.constData();
    DWORD left = static_cast<DWORD>(data.size());
    while (left > 0) {
        DWORD written = 0;
        if (!WriteFile(inputWrite_, remaining, left, &written, nullptr))
            return;
        remaining += written;
        left -= written;
    }
}

void ConPty::resize(QSize cells)
{
    if (pseudoConsole_)
        ResizePseudoConsole(pseudoConsole_, consoleSize(cells));
}

void ConPty::setError(const QString &action)
{
    errorString_ = QStringLiteral("%1 : erreur Windows %2").arg(action).arg(GetLastError());
}

std::unique_ptr<IPty> createPty()
{
    return std::make_unique<ConPty>();
}
