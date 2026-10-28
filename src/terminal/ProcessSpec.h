// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QMap>
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

// What Kodamiko adds to its own environment for the programs of its terminals:
// the folders of Preferences ▸ Terminal, and the environment of the open
// project's recipe (see RecipeEnvironment).
struct EnvironmentAdditions {
    QMap<QString, QString> variables; // replace the editor's
    QStringList pathFolders;          // put first in the PATH, in this order

    bool operator==(const EnvironmentAdditions &) const = default;
};

// `additions` applied to `environment`: its variables, then its folders in
// front of the PATH.
void addTo(QProcessEnvironment &environment, const EnvironmentAdditions &additions);

// The additions terminalEnvironment() applies from now on. The main window
// sets them when a project opens and when the preferences change: a Kodamiko
// process shows one project, and every program it launches (terminals, run
// configurations, agent sessions) gets them without each caller passing
// them along.
void setTerminalAdditions(const EnvironmentAdditions &additions);

// Environment shared by all programs launched in the terminal: the editor's
// own, the additions of setTerminalAdditions(), and the variables that
// advertise a color terminal.
QProcessEnvironment terminalEnvironment();

// The user's interactive shell ($SHELL, or PowerShell on Windows).
ProcessSpec defaultShell(const QString &workingDirectory);
