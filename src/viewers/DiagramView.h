#pragma once

#include "ui/Theme.h"

#include <QByteArray>
#include <QWidget>

class QLabel;
class QTimer;

namespace diagram {
class Board;
class Canvas;
class Codec;
class Inspector;
} // namespace diagram

// Diagram editor for the diagram files (Mermaid .mmd, PlantUML .puml,
// explainer boards, Flowboard's .flow), opened in a tab instead of a text
// editor: the canvas (elements are added from its context menus) and the
// properties of the selection with the diagram's source, in the editor's
// theme. Saving goes through the editor (Ctrl+S); the toolbar lays the
// diagram out, zooms, and exports it.
class DiagramView : public QWidget {
    Q_OBJECT

public:
    explicit DiagramView(const Theme &theme, QWidget *parent = nullptr);
    ~DiagramView() override;

    // True for the files this view edits.
    static bool isDiagramFile(const QString &path);

    bool load(const QString &path, QString *error);
    bool save(QString *error);
    // Reloads the file; unsaved changes are lost.
    bool reloadFromDisk(QString *error);
    QString filePath() const { return filePath_; }
    bool isModified() const;
    // True if the file on disk is the one last loaded or saved here.
    bool matchesDisk() const;
    // The file's content for the diagram as it is now.
    QByteArray content() const;

    void applyTheme(const Theme &theme);

    diagram::Board *board() const { return board_; }
    diagram::Canvas *canvas() const { return canvas_; }
    const diagram::Codec *codec() const { return codec_; }

signals:
    void modificationChanged(bool modified);

private:
    void showMessage(const QString &text);
    void updateZoom(qreal zoom);
    void updateModified();
    void exportSvg();
    void exportPng();
    void exportSource();

    diagram::Board *board_;
    diagram::Canvas *canvas_;
    diagram::Inspector *inspector_;
    const diagram::Codec *codec_ = nullptr;
    QLabel *zoomLabel_;
    QLabel *message_;
    QTimer *messageTimer_;
    QString filePath_;
    QByteArray diskContent_;    // as last read or written: recognizes our own writes
    QByteArray savedSnapshot_;  // the board as last loaded or saved
    bool lastModified_ = false;
};
