#pragma once

#include "TextFile.h"
#include "syntax/Theme.h"

#include <Qsci/qsciscintilla.h>

#include <memory>

class ChangeGutter;
class ISyntaxHighlighter;

// Editor for one file: QScintilla configured for code, with highlighting
// for the language detected from the file name.
class EditorWidget : public QsciScintilla {
    Q_OBJECT

public:
    explicit EditorWidget(const Theme &theme, QWidget *parent = nullptr);
    ~EditorWidget() override;

    bool load(const QString &path, QString *error);
    bool save(QString *error);

    // Rereads the file, keeping the cursor and scroll positions.
    // The operation can be undone (Ctrl+Z) like an ordinary edit.
    bool reloadFromDisk(QString *error);
    bool matchesDisk() const;

    // Read-only text with no associated file (e.g. a diff).
    void showReadOnlyText(const QString &text, const QString &languageId);

    QString filePath() const { return filePath_; }
    // LanguageRegistry identifier of the displayed text.
    QString languageId() const { return languageId_; }
    // Encoding and line ending of the file, kept when saving.
    TextFileFormat format() const { return format_; }
    // Converts every line ending of the text and uses `lineEnding` for new
    // lines; the conversion can be undone.
    void setLineEnding(LineEnding lineEnding);
    // Indentation inserted by Tab and auto-indent.
    void setIndentationStyle(bool useTabs, int width);
    // Marks of the lines that differ from the git index.
    ChangeGutter *changeGutter() const { return changeGutter_; }
    void applyTheme(const Theme &theme);

signals:
    void languageChanged();
    // The line ending or the indentation settings changed.
    void formatChanged();
    // Ctrl+click on a word (the cursor is already placed there).
    void definitionRequested();

protected:
    void mousePressEvent(QMouseEvent *event) override;

private:
    void configureEditing();
    void installHighlighter(const QString &languageId);
    void updateLineNumberMargin();

    QString filePath_;
    QString languageId_;
    TextFileFormat format_;
    Theme theme_;
    std::unique_ptr<ISyntaxHighlighter> highlighter_;
    ChangeGutter *changeGutter_;
};
