#pragma once

#include "IPty.h"

#include <QWinEventNotifier>

#include <thread>

#include <windows.h>

// PTY Windows (ConPTY, Windows 10 1809+). La lecture des sorties est
// bloquante : elle tourne dans un thread dédié qui renvoie les octets au
// thread de l'interface.
//
// Particularité de ConPTY : la fin du programme ne ferme pas le tube de
// sortie, seule la fermeture de la pseudo-console le fait. On surveille donc
// le processus lui-même (QWinEventNotifier) pour détecter sa fin.
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
    void onProcessExited();
    void closeConsole();
    void setError(const QString &action);

    HPCON pseudoConsole_ = nullptr;
    HANDLE inputWrite_ = INVALID_HANDLE_VALUE;
    HANDLE outputRead_ = INVALID_HANDLE_VALUE;
    HANDLE process_ = INVALID_HANDLE_VALUE;
    QWinEventNotifier *exitNotifier_ = nullptr;
    std::thread reader_;
    QString errorString_;
};
