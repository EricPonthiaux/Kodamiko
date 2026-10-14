#pragma once

#include "FileWatcher.h"
#include "syntax/Theme.h"

#include <QSet>
#include <QTabWidget>

class EditorWidget;
class ImageView;

// Onglets des fichiers ouverts (et des diffs affichés). Recharge les
// fichiers modifiés sur disque — par Claude notamment — et demande quoi
// faire si l'utilisateur avait des modifications non enregistrées.
class EditorTabs : public QTabWidget {
    Q_OBJECT

public:
    explicit EditorTabs(const Theme &theme, QWidget *parent = nullptr);

    // Ouvre un fichier texte dans un éditeur, ou une image dans une
    // visionneuse. Renvoie l'onglet, ou nullptr en cas d'échec.
    QWidget *openFile(const QString &path);
    void showDiff(const QString &title, const QString &diffText);

    EditorWidget *currentEditor() const;
    EditorWidget *editorFor(const QString &path) const;
    ImageView *imageViewFor(const QString &path) const;

    bool saveCurrent();
    bool saveAll();
    void closeCurrent();
    // Ferme tous les onglets, après confirmation s'il reste des modifications.
    bool closeAll();

    // À appeler quand un fichier a pu changer sur disque.
    void syncWithDisk(const QString &path);

    void applyTheme(const Theme &theme);

signals:
    void statusMessage(const QString &message);

private:
    QWidget *openImage(const QString &absolutePath);
    bool closeTab(int index);
    bool saveEditor(EditorWidget *editor);
    void updateTabTitle(EditorWidget *editor);
    QList<EditorWidget *> editors() const;

    Theme theme_;
    FileWatcher watcher_;
    QSet<QString> pathsBeingResolved_; // conflits en attente de réponse
};
