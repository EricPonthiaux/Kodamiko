#pragma once

#include "ProcessSpec.h"

#include <QByteArray>
#include <QObject>
#include <QSize>

#include <memory>

// Pseudo-terminal: launches a program that believes it is in a real terminal
// and exchanges raw bytes with it (VT sequences included).
//
// Implementations: UnixPty (forkpty, Linux and macOS), ConPty (Windows 10+).
class IPty : public QObject {
    Q_OBJECT

public:
    using QObject::QObject;

    // `cells`: width = columns, height = rows.
    virtual bool start(const ProcessSpec &spec, QSize cells) = 0;
    virtual void write(const QByteArray &data) = 0;
    virtual void resize(QSize cells) = 0;
    virtual QString errorString() const = 0;

signals:
    void dataReceived(const QByteArray &data);
    void finished(int exitCode);
};

// PTY suited to the current system.
std::unique_ptr<IPty> createPty();
