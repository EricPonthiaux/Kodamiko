#pragma once

#include "syntax/Theme.h"

#include <QOpenGLWidget>

#include <functional>
#include <memory>

// Diagram editor (Flowboard) for .flow files, opened in a tab instead of a
// text editor. Flowboard draws with Dear ImGui: this widget hosts its own
// ImGui context, forwards Qt input to it and renders it with OpenGL.
// Saving goes through the editor (Ctrl+S); the diagram's own menus cover
// layout, Mermaid import/export and SVG export.
class DiagramView : public QOpenGLWidget {
    Q_OBJECT

public:
    explicit DiagramView(const Theme &theme, QWidget *parent = nullptr);
    ~DiagramView() override;

    // True for the files this view edits (.flow).
    static bool isDiagramFile(const QString &path);

    bool load(const QString &path, QString *error);
    bool save(QString *error);
    // Reloads the file; unsaved changes are lost.
    bool reloadFromDisk(QString *error);
    QString filePath() const { return filePath_; }
    bool isModified() const;
    // True if the file on disk is the one last loaded or saved here.
    bool matchesDisk() const;

    void applyTheme(const Theme &theme);

signals:
    void modificationChanged(bool modified);

protected:
    bool event(QEvent *event) override;
    void initializeGL() override;
    void paintGL() override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void keyReleaseEvent(QKeyEvent *event) override;
    void inputMethodEvent(QInputMethodEvent *event) override;
    QVariant inputMethodQuery(Qt::InputMethodQuery query) const override;
    void focusInEvent(QFocusEvent *event) override;
    void focusOutEvent(QFocusEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    struct Impl;
    // Runs one ImGui frame; renders it only when `render` is true (from paintGL).
    void runFrame(bool render, const std::function<void()> &beforeFrame = {});
    // Asks for new frames: ImGui needs a few after each event to settle its state.
    void wake();
    void notifyModification();

    std::unique_ptr<Impl> d_;
    QString filePath_;
};
