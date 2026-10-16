#pragma once

#include "ui/Theme.h"

#include <QTimer>
#include <QWebEngineView>
#include <QWidget>

#include <memory>

class CodeColorizer;
class EditorWidget;
class QSplitter;
class QToolButton;

// Markdown rendered exactly as on GitHub: GitHub's parser (cmark-gfm),
// GitHub's markup and style sheet (github-markdown-css, dark or light after
// the editor theme), in a web view. Code blocks are highlighted with
// GitHub's classes, from the editor's highlighters. Page scripts never run.
// Relative images are found next to the file; links to files are opened in
// the editor, "#heading" links scroll to the heading, others go to the
// browser.
class MarkdownPreview : public QWebEngineView {
    Q_OBJECT

public:
    explicit MarkdownPreview(const Theme &theme, QWidget *parent = nullptr);
    ~MarkdownPreview() override;

    // Renders `text`, keeping the scroll position. `folder`: where relative
    // links and images lead.
    void render(const QString &text, const QString &folder);
    void applyTheme(const Theme &theme);
    // Scrolls to the share `ratio` (0 to 1) of the page.
    void scrollToRatio(double ratio);
    // HTML of the rendered body, as last set.
    QString bodyHtml() const { return body_; }
    // The page is loaded and shows the last rendered text.
    bool isReady() const { return loaded_; }

signals:
    void openFileRequested(const QString &path);
    // The rendered text is shown (after each render).
    void rendered();

protected:
    void contextMenuEvent(QContextMenuEvent *event) override;

private:
    void loadPage();
    void showBody();

    bool dark_ = true;
    QString folder_;
    QString body_;
    bool loaded_ = false;
    bool pageLoading_ = false;
    double pendingRatio_ = -1;
    std::unique_ptr<CodeColorizer> colorizer_;
};

// Tab of a Markdown file: its editor, its preview, or both side by side,
// chosen with the buttons at the top right (as in IntelliJ). The preview
// follows the text as it is typed. The mode chosen last is kept for the
// next files.
class MarkdownPage : public QWidget {
    Q_OBJECT

public:
    enum class Mode { Editor, Split, Preview };

    // Takes `editor`, which holds the file.
    MarkdownPage(EditorWidget *editor, const Theme &theme, QWidget *parent = nullptr);

    EditorWidget *editor() const { return editor_; }
    MarkdownPreview *preview() const { return preview_; }
    Mode mode() const { return mode_; }
    void setMode(Mode mode);
    // The editor hidden by the Preview mode becomes visible (Split): to show
    // a position or a search in it.
    void revealEditor();
    void applyTheme(const Theme &theme);

    static bool isMarkdownFile(const QString &path);

signals:
    void openFileRequested(const QString &path);

private:
    void updatePreview();
    void syncScroll();

    EditorWidget *editor_;
    MarkdownPreview *preview_;
    QSplitter *splitter_;
    QToolButton *buttons_[3];
    Mode mode_ = Mode::Preview;
    QTimer updateTimer_;
};
