#pragma once

#include <QProcessEnvironment>
#include <QString>
#include <QStringList>

// Program to launch in a terminal.
struct ProcessSpec {
    QString program; // preferably an absolute path
    QStringList arguments;
    QString workingDirectory;
    QProcessEnvironment environment;
};

// Environment shared by all programs launched in the terminal: the editor's
// own, plus the variables that advertise a color terminal.
QProcessEnvironment terminalEnvironment();

// The user's interactive shell ($SHELL, or PowerShell on Windows).
ProcessSpec defaultShell(const QString &workingDirectory);
