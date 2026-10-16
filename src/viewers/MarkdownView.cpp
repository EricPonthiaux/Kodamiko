#include "MarkdownView.h"

#include "editor/CodeColorizer.h"
#include "editor/EditorWidget.h"
#include "MarkdownHtml.h"
#include "syntax/LanguageRegistry.h"
#include "ui/GripSplitter.h"

#include <QButtonGroup>
#include <QColor>
#include <QContextMenuEvent>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMenu>
#include <QScrollBar>
#include <QSettings>
#include <QSplitter>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWebEnginePage>
#include <QWebEngineProfile>
#include <QWebEngineScript>
#include <QWebEngineSettings>

namespace {

constexpr int updateDelayMs = 250;

// One off-the-record profile for every preview: nothing is kept on disk.
QWebEngineProfile *previewProfile()
{
    static QWebEngineProfile *profile = [] {
        auto *created = new QWebEngineProfile(QCoreApplication::instance());
        QWebEngineSettings *settings = created->settings();
        settings->setAttribute(QWebEngineSettings::JavascriptEnabled, false); // the page's own scripts
        settings->setAttribute(QWebEngineSettings::JavascriptCanOpenWindows, false);
        settings->setAttribute(QWebEngineSettings::LocalContentCanAccessFileUrls, true); // images
        settings->setAttribute(QWebEngineSettings::LocalContentCanAccessRemoteUrls, true); // badges
        settings->setAttribute(QWebEngineSettings::PluginsEnabled, false);
        settings->setAttribute(QWebEngineSettings::ErrorPageEnabled, false);
        settings->setAttribute(QWebEngineSettings::FocusOnNavigationEnabled, false);
        return created;
    }();
    return profile;
}

// Links leave the page: files go to the editor, the rest to the browser.
class PreviewPage : public QWebEnginePage {
public:
    PreviewPage(MarkdownPreview *preview, std::function<void(const QString &)> openFile)
        : QWebEnginePage(previewProfile(), preview)
        , openFile_(std::move(openFile))
    {
    }

protected:
    bool acceptNavigationRequest(const QUrl &url, NavigationType type, bool isMainFrame) override
    {
        if (type != NavigationTypeLinkClicked || !isMainFrame)
            return true; // the page itself
        // "#heading": within the page.
        if (url.hasFragment() && url.adjusted(QUrl::RemoveFragment) == this->url().adjusted(QUrl::RemoveFragment))
            return true;
        if (url.isLocalFile()) {
            const QString path = QDir::cleanPath(url.toLocalFile());
            if (QFileInfo(path).isFile())
                openFile_(path);
            return false;
        }
        QDesktopServices::openUrl(url);
        return false;
    }

private:
    std::function<void(const QString &)> openFile_;
};

// A string as a JavaScript literal.
QString scriptString(const QString &text)
{
    const QByteArray json = QJsonDocument(QJsonArray{text}).toJson(QJsonDocument::Compact);
    return QString::fromUtf8(json.mid(1, json.size() - 2));
}

} // namespace

MarkdownPreview::MarkdownPreview(const Theme &theme, QWidget *parent)
    : QWebEngineView(parent)
    , dark_(theme.isDark)
    , colorizer_(std::make_unique<CodeColorizer>())
{
    setPage(new PreviewPage(this, [this](const QString &path) { emit openFileRequested(path); }));
    // The page's color before it is loaded, not the view's default white.
    page()->setBackgroundColor(QColor(MarkdownHtml::pageColor(dark_)));
    connect(page(), &QWebEnginePage::loadFinished, this, [this] {
        pageLoading_ = false;
        loaded_ = true;
        showBody();
    });
}

MarkdownPreview::~MarkdownPreview() = default;

void MarkdownPreview::render(const QString &text, const QString &folder)
{
    QString body = MarkdownHtml::body(text, colorizer_.get());
    // The same text (a mode change): the page shown is left as it is.
    if (body == body_ && folder == folder_ && (loaded_ || pageLoading_))
        return;
    body_ = std::move(body);
    if (folder != folder_ || !loaded_) {
        // Relative links and images: the page's address is the file's folder.
        folder_ = folder;
        loadPage();
        return;
    }
    showBody();
}

void MarkdownPreview::loadPage()
{
    loaded_ = false;
    pageLoading_ = true;
    setHtml(MarkdownHtml::page(body_, dark_), QUrl::fromLocalFile(folder_ + QStringLiteral("/")));
}

// The body replaced in place: the page keeps its scroll position.
void MarkdownPreview::showBody()
{
    if (!loaded_)
        return;
    QString script = QStringLiteral("document.querySelector('article').innerHTML = %1;").arg(scriptString(body_));
    if (pendingRatio_ >= 0) {
        script += QStringLiteral("window.scrollTo(0, %1 * (document.documentElement.scrollHeight - window.innerHeight));")
                      .arg(pendingRatio_);
        pendingRatio_ = -1;
    }
    page()->runJavaScript(script, QWebEngineScript::ApplicationWorld, [this](const QVariant &) { emit rendered(); });
}

void MarkdownPreview::scrollToRatio(double ratio)
{
    if (!loaded_) {
        pendingRatio_ = ratio;
        return;
    }
    page()->runJavaScript(
        QStringLiteral("window.scrollTo(0, %1 * (document.documentElement.scrollHeight - window.innerHeight));").arg(ratio),
        QWebEngineScript::ApplicationWorld);
}

void MarkdownPreview::applyTheme(const Theme &theme)
{
    if (theme.isDark == dark_)
        return;
    dark_ = theme.isDark;
    page()->setBackgroundColor(QColor(MarkdownHtml::pageColor(dark_)));
    if (folder_.isEmpty() && body_.isEmpty())
        return;
    // Another style sheet: the page is reloaded, at the same place.
    page()->runJavaScript(QStringLiteral("window.scrollY / Math.max(1, document.documentElement.scrollHeight - window.innerHeight)"),
                          QWebEngineScript::ApplicationWorld, [this](const QVariant &ratio) {
                              pendingRatio_ = ratio.toDouble();
                              loadPage();
                          });
}

// Copying, without the browser's navigation entries.
void MarkdownPreview::contextMenuEvent(QContextMenuEvent *event)
{
    QMenu menu(this);
    menu.addAction(pageAction(QWebEnginePage::Copy));
    menu.addAction(pageAction(QWebEnginePage::CopyLinkToClipboard));
    menu.addSeparator();
    menu.addAction(pageAction(QWebEnginePage::SelectAll));
    menu.exec(event->globalPos());
}

MarkdownPage::MarkdownPage(EditorWidget *editor, const Theme &theme, QWidget *parent)
    : QWidget(parent)
    , editor_(editor)
    , preview_(new MarkdownPreview(theme, this))
{
    editor_->setParent(this);
    connect(preview_, &MarkdownPreview::openFileRequested, this, &MarkdownPage::openFileRequested);

    splitter_ = new GripSplitter(Qt::Horizontal, this);
    splitter_->setChildrenCollapsible(false);
    splitter_->addWidget(editor_);
    splitter_->addWidget(preview_);

    auto *group = new QButtonGroup(this);
    auto *toolbar = new QHBoxLayout;
    toolbar->setContentsMargins(4, 2, 6, 2);
    toolbar->setSpacing(2);
    toolbar->addStretch();
    const QString names[] = {tr("Editor"), tr("Split"), tr("Preview")};
    const QString tips[] = {tr("Editor only"), tr("Editor and preview side by side"), tr("Preview only")};
    for (int i = 0; i < 3; ++i) {
        auto *button = new QToolButton(this);
        button->setText(names[i]);
        button->setToolTip(tips[i]);
        button->setCheckable(true);
        button->setAutoRaise(true);
        button->setFocusPolicy(Qt::NoFocus);
        group->addButton(button, i);
        toolbar->addWidget(button);
        buttons_[i] = button;
    }
    connect(group, &QButtonGroup::idClicked, this, [this](int id) {
        setMode(static_cast<Mode>(id));
        QSettings().setValue(QStringLiteral("markdown/mode"), id);
    });

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addLayout(toolbar);
    layout->addWidget(splitter_, 1);

    updateTimer_.setSingleShot(true);
    updateTimer_.setInterval(updateDelayMs);
    connect(&updateTimer_, &QTimer::timeout, this, &MarkdownPage::updatePreview);
    connect(editor_, &QsciScintilla::textChanged, this, [this] {
        if (mode_ != Mode::Editor)
            updateTimer_.start();
    });
    connect(editor_->verticalScrollBar(), &QScrollBar::valueChanged, this, &MarkdownPage::syncScroll);

    const int saved = QSettings().value(QStringLiteral("markdown/mode"), static_cast<int>(Mode::Preview)).toInt();
    setMode(static_cast<Mode>(std::clamp(saved, 0, 2)));
    setFocusProxy(editor_);
}

void MarkdownPage::setMode(Mode mode)
{
    mode_ = mode;
    buttons_[static_cast<int>(mode)]->setChecked(true);
    editor_->setVisible(mode != Mode::Preview);
    preview_->setVisible(mode != Mode::Editor);
    setFocusProxy(mode == Mode::Preview ? static_cast<QWidget *>(preview_) : editor_);
    if (mode == Mode::Split) {
        const int half = std::max(splitter_->width() / 2, 100);
        splitter_->setSizes({half, half});
    }
    if (mode != Mode::Editor)
        updatePreview();
}

void MarkdownPage::revealEditor()
{
    if (mode_ == Mode::Preview)
        setMode(Mode::Split);
}

void MarkdownPage::updatePreview()
{
    updateTimer_.stop();
    preview_->render(editor_->text(), QFileInfo(editor_->filePath()).absolutePath());
    syncScroll();
}

// In Split mode, the preview shows the same share of the text as the editor.
void MarkdownPage::syncScroll()
{
    if (mode_ != Mode::Split)
        return;
    const QScrollBar *source = editor_->verticalScrollBar();
    if (source->maximum() > 0)
        preview_->scrollToRatio(double(source->value()) / source->maximum());
}

void MarkdownPage::applyTheme(const Theme &theme)
{
    editor_->applyTheme(theme);
    preview_->applyTheme(theme);
}

bool MarkdownPage::isMarkdownFile(const QString &path)
{
    return LanguageRegistry::languageForFile(path) == QLatin1String("markdown");
}
