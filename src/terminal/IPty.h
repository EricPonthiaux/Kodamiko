#pragma once

#include "ProcessSpec.h"

#include <QByteArray>
#include <QObject>
#include <QSize>

#include <memory>

// Pseudo-terminal : lance un programme qui se croit dans un vrai terminal et
// échange avec lui des octets bruts (séquences VT comprises).
//
// Implémentations : UnixPty (forkpty, Linux et macOS), ConPty (Windows 10+).
class IPty : public QObject {
    Q_OBJECT

public:
    using QObject::QObject;

    // `cells` : largeur = colonnes, hauteur = lignes.
    virtual bool start(const ProcessSpec &spec, QSize cells) = 0;
    virtual void write(const QByteArray &data) = 0;
    virtual void resize(QSize cells) = 0;
    virtual QString errorString() const = 0;

signals:
    void dataReceived(const QByteArray &data);
    void finished(int exitCode);
};

// PTY adapté au système courant.
std::unique_ptr<IPty> createPty();
