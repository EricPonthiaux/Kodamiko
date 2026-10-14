#pragma once

#include "TextFile.h"
#include "syntax/Theme.h"

#include <Qsci/qsciscintilla.h>

#include <memory>

class ISyntaxHighlighter;

// Éditeur d'un fichier : QScintilla configuré pour le code, avec la
// coloration du langage détecté d'après le nom du fichier.
class EditorWidget : public QsciScintilla {
    Q_OBJECT

public:
    explicit EditorWidget(const Theme &theme, QWidget *parent = nullptr);
    ~EditorWidget() override;

    bool load(const QString &path, QString *error);
    bool save(QString *error);

    // Relit le fichier en gardant la position du curseur et du défilement.
    // L'opération s'annule (Ctrl+Z) comme une modification ordinaire.
    bool reloadFromDisk(QString *error);
    bool matchesDisk() const;

    // Texte non modifiable sans fichier associé (ex. : un diff).
    void showReadOnlyText(const QString &text, const QString &languageId);

    QString filePath() const { return filePath_; }
    // Identifiant de LanguageRegistry du texte affiché.
    QString languageId() const { return languageId_; }
    void applyTheme(const Theme &theme);

signals:
    void languageChanged();

private:
    void configureEditing();
    void installHighlighter(const QString &languageId);
    void updateLineNumberMargin();

    QString filePath_;
    QString languageId_;
    TextFileFormat format_;
    Theme theme_;
    std::unique_ptr<ISyntaxHighlighter> highlighter_;
};
