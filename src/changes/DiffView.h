#pragma once

#include "syntax/Theme.h"

#include <QWidget>

class EditorWidget;
class QLabel;
class QListWidget;
class QSplitter;

// Mode « Diff » : changements en cours sur tous les fichiers du projet
// (dépôt git comparé au dernier commit). Liste des fichiers à gauche, diff
// complet à droite ; cliquer un fichier fait défiler jusqu'à son diff.
class DiffView : public QWidget {
    Q_OBJECT

public:
    explicit DiffView(const Theme &theme, QWidget *parent = nullptr);

    void setFolder(const QString &folder);
    // Recalcule le diff ; garde le fichier sélectionné et la position.
    void refresh();

    void applyTheme(const Theme &theme);
    QSplitter *splitter() const { return splitter_; }

signals:
    // Chemin relatif au dossier du projet.
    void openFileRequested(const QString &relativePath);

private:
    void scrollToSelectedFile();

    QString folder_;
    QLabel *summary_;
    QListWidget *files_;
    EditorWidget *diff_;
    QSplitter *splitter_;
};
