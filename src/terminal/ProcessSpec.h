#pragma once

#include <QProcessEnvironment>
#include <QString>
#include <QStringList>

// Programme à lancer dans un terminal.
struct ProcessSpec {
    QString program; // chemin absolu de préférence
    QStringList arguments;
    QString workingDirectory;
    QProcessEnvironment environment;
};

// Environnement commun à tous les programmes lancés dans le terminal : celui
// de l'éditeur, plus les variables qui annoncent un terminal couleur.
QProcessEnvironment terminalEnvironment();

// Shell interactif de l'utilisateur ($SHELL, ou PowerShell sous Windows).
ProcessSpec defaultShell(const QString &workingDirectory);
