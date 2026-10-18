#pragma once

#include "ui/Theme.h"

#include <QTimer>
#include <QWebEngineView>
#include <QWidget>

#include <array>
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

// An HTML page as a browser shows it, from the editor's text: relative
// style sheets and images are found next to the file. The page's scripts
// never run. Links leave the page as in MarkdownPreview.
class HtmlPreview : public QWebEngineView {
    Q_OBJECT

public:
    explicit HtmlPreview(QWidget *parent = nullptr);

    // Shows `text` as the page at `filePath`, at the same scroll position.
    void render(const QString &text, const QString &filePath);
    // Scrolls to the share `ratio` (0 to 1) of the page.
    void scrollToRatio(double ratio);

    // The page is loaded and shows the last rendered text.
    bool isReady() const { return loaded_; }

signals:
    void openFileRequested(const QString &path);
    // The rendered text is shown (after each render).
    void rendered();

protected:
    void contextMenuEvent(QContextMenuEvent *event) override;

private:
    // The page again, from text_, at the same scroll position.
    void reload();
    void loadPage();

    QString text_;
    QString filePath_;
    bool loaded_ = false;
    bool pageLoading_ = false;
    bool reloadPending_ = false;
    double pendingRatio_ = -1;
};

// Tab of a Markdown or HTML file: its editor, its preview, or both side by
// side, chosen with the buttons at the top right (as in IntelliJ). The
// preview follows the text as it is typed. The mode chosen last is kept for
// the next files of the same kind.
class PreviewPage : public QWidget {
    Q_OBJECT

public:
    enum class Kind {
        Markdown,
        Html
    };
    enum class Mode {
        Editor,
        Split,
        Preview
    };

    // Takes `editor`, which holds the file.
    PreviewPage(EditorWidget *editor, Theme theme, QWidget *parent = nullptr);

    EditorWidget *editor() const { return editor_; }

    Kind kind() const { return kind_; }

    // Created the first time the page shows in Split or Preview mode: a
    // web engine for each tab restored at startup would slow it down.
    // nullptr until then, and for the other kind.
    MarkdownPreview *markdownPreview() const { return markdown_; }

    HtmlPreview *htmlPreview() const { return html_; }

    Mode mode() const { return mode_; }

    void setMode(Mode mode);
    // The editor hidden by the Preview mode becomes visible (Split): to show
    // a position or a search in it.
    void revealEditor();
    void applyTheme(const Theme &theme);

    // A Markdown or HTML file: opened with its preview.
    static bool hasPreview(const QString &path);

signals:
    void openFileRequested(const QString &path);

protected:
    void showEvent(QShowEvent *event) override;

private:
    void ensurePreview();
    void updatePreview();
    void syncScroll();
    QWidget *preview() const;
    QString settingsKey(const QString &name) const;

    EditorWidget *editor_;
    Theme theme_;
    Kind kind_;
    MarkdownPreview *markdown_ = nullptr;
    HtmlPreview *html_ = nullptr;
    QSplitter *splitter_;
    static constexpr std::size_t modeCount = 3;
    std::array<QToolButton *, modeCount> buttons_{}; // indexed by Mode
    Mode mode_ = Mode::Preview;
    QTimer updateTimer_;
};
