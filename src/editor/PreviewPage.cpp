#include "PreviewPage.h"

#include "CodeColorizer.h"
#include "EditorWidget.h"
#include "MarkdownHtml.h"
#include "PreviewSite.h"
#include "syntax/LanguageRegistry.h"
#include "ui/AppFonts.h"
#include "ui/GripSplitter.h"
#include "ui/UiIcons.h"

#include <QButtonGroup>
#include <QColor>
#include <QContextMenuEvent>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
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

#include <array>
#include <utility>

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
        settings->setAttribute(QWebEngineSettings::LocalContentCanAccessFileUrls, true);   // images
        settings->setAttribute(QWebEngineSettings::LocalContentCanAccessRemoteUrls, true); // badges
        settings->setAttribute(QWebEngineSettings::PluginsEnabled, false);
        settings->setAttribute(QWebEngineSettings::ErrorPageEnabled, false);
        settings->setAttribute(QWebEngineSettings::FocusOnNavigationEnabled, false);
        PreviewSite::serveTo(created);
        return created;
    }();
    return profile;
}

// Links leave the page: files go to the editor, the rest to the browser.
class LinkRoutingPage : public QWebEnginePage {
public:
    LinkRoutingPage(QWebEngineView *view, std::function<void(const QString &)> openFile)
        : QWebEnginePage(previewProfile(), view)
        , openFile_(std::move(openFile))
    {
    }

    // The site the page is served from: links to its files go to the
    // editor. Not owned.
    void setSite(const PreviewSite *site) { site_ = site; }

protected:
    bool acceptNavigationRequest(const QUrl &url, NavigationType type, bool isMainFrame) override
    {
        if (type != NavigationTypeLinkClicked || !isMainFrame)
            return true; // the page itself
        // "#heading": within the page.
        if (url.hasFragment() && url.adjusted(QUrl::RemoveFragment) == this->url().adjusted(QUrl::RemoveFragment))
            return true;
        if (const std::optional<QString> path = localPath(url)) {
            if (QFileInfo(*path).isFile())
                openFile_(*path);
            return false;
        }
        QDesktopServices::openUrl(url);
        return false;
    }

private:
    std::optional<QString> localPath(const QUrl &url) const
    {
        if (url.isLocalFile())
            return QDir::cleanPath(url.toLocalFile());
        if (site_)
            return site_->localPath(url);
        return std::nullopt;
    }

    std::function<void(const QString &)> openFile_;
    const PreviewSite *site_ = nullptr;
};

constexpr const char *scrollRatioScript =
    "window.scrollY / Math.max(1, document.documentElement.scrollHeight - window.innerHeight)";

QString scrollToScript(double ratio)
{
    return QStringLiteral("window.scrollTo(0, %1 * (document.documentElement.scrollHeight - window.innerHeight));")
        .arg(ratio);
}

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
    setPage(new LinkRoutingPage(this, [this](const QString &path) { emit openFileRequested(path); }));
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
        script += scrollToScript(pendingRatio_);
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
    page()->runJavaScript(scrollToScript(ratio), QWebEngineScript::ApplicationWorld);
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
    page()->runJavaScript(QString::fromLatin1(scrollRatioScript), QWebEngineScript::ApplicationWorld,
                          [this](const QVariant &ratio) {
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

HtmlPreview::HtmlPreview(const QString &servedFolder, QWidget *parent)
    : QWebEngineView(parent)
    , site_(servedFolder)
{
    auto *routing = new LinkRoutingPage(this, [this](const QString &path) { emit openFileRequested(path); });
    routing->setSite(&site_);
    setPage(routing);
    // The page's own scripts; its site keeps them away from other files.
    page()->settings()->setAttribute(QWebEngineSettings::JavascriptEnabled, true);
    // As in a browser, a page without a background of its own is white.
    page()->setBackgroundColor(Qt::white);
    connect(page(), &QWebEnginePage::loadFinished, this, [this] {
        pageLoading_ = false;
        loaded_ = true;
        if (reloadPending_) {
            // The text changed during the load: once more, with the new one.
            reloadPending_ = false;
            reload();
            return;
        }
        if (pendingRatio_ >= 0) {
            page()->runJavaScript(scrollToScript(pendingRatio_), QWebEngineScript::ApplicationWorld);
            pendingRatio_ = -1;
        }
        emit rendered();
    });
}

void HtmlPreview::render(const QString &text, const QString &filePath)
{
    if (text == text_ && filePath == filePath_ && (loaded_ || pageLoading_))
        return;
    text_ = text;
    filePath_ = filePath;
    if (pageLoading_) {
        reloadPending_ = true;
        return;
    }
    reload();
}

void HtmlPreview::reload()
{
    pageLoading_ = true;
    if (!loaded_) {
        loadPage();
        return;
    }
    // The page is loaded again: it comes back where it was.
    page()->runJavaScript(QString::fromLatin1(scrollRatioScript), QWebEngineScript::ApplicationWorld,
                          [this](const QVariant &ratio) {
                              if (pendingRatio_ < 0)
                                  pendingRatio_ = ratio.toDouble();
                              loadPage();
                          });
}

void HtmlPreview::loadPage()
{
    loaded_ = false;
    pageLoading_ = true;
    load(site_.serve(filePath_, text_));
}

void HtmlPreview::scrollToRatio(double ratio)
{
    if (!loaded_) {
        pendingRatio_ = ratio;
        return;
    }
    page()->runJavaScript(scrollToScript(ratio), QWebEngineScript::ApplicationWorld);
}

// Copying, and the page in a real browser; without the navigation entries.
void HtmlPreview::contextMenuEvent(QContextMenuEvent *event)
{
    QMenu menu(this);
    menu.addAction(pageAction(QWebEnginePage::Copy));
    menu.addAction(pageAction(QWebEnginePage::CopyLinkToClipboard));
    menu.addSeparator();
    menu.addAction(pageAction(QWebEnginePage::SelectAll));
    menu.addSeparator();
    menu.addAction(tr("Open in Browser"), this, [this] { QDesktopServices::openUrl(QUrl::fromLocalFile(filePath_)); });
    menu.exec(event->globalPos());
}

PreviewPage::PreviewPage(EditorWidget *editor, Theme theme, QString projectFolder, QWidget *parent)
    : QWidget(parent)
    , editor_(editor)
    , theme_(std::move(theme))
    , kind_(LanguageRegistry::languageForFile(editor->filePath()) == QLatin1String("html") ? Kind::Html
                                                                                           : Kind::Markdown)
    , projectFolder_(std::move(projectFolder))
{
    editor_->setParent(this);

    splitter_ = new GripSplitter(Qt::Horizontal, this);
    splitter_->setChildrenCollapsible(false);
    splitter_->addWidget(editor_);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(createToolbar());
    layout->addWidget(splitter_, 1);

    updateTimer_.setSingleShot(true);
    updateTimer_.setInterval(updateDelayMs);
    connect(&updateTimer_, &QTimer::timeout, this, &PreviewPage::updatePreview);
    connect(editor_, &QsciScintilla::textChanged, this, [this] {
        if (mode_ != Mode::Editor && preview())
            updateTimer_.start();
    });
    connect(editor_->verticalScrollBar(), &QScrollBar::valueChanged, this, &PreviewPage::syncScroll);

    // An HTML file is mostly written: its code beside the page.
    const Mode initial = kind_ == Kind::Html ? Mode::Split : Mode::Preview;
    const int saved = QSettings().value(settingsKey(QStringLiteral("mode")), static_cast<int>(initial)).toInt();
    setMode(static_cast<Mode>(std::clamp(saved, 0, 2)));
    setFocusProxy(editor_);
}

QWidget *PreviewPage::createToolbar()
{
    auto *toolbar = new QFrame(this);
    toolbar->setObjectName(QStringLiteral("previewToolbar"));
    auto *layout = new QHBoxLayout(toolbar);
    layout->setContentsMargins(10, 4, 10, 4);
    layout->setSpacing(8);

    auto *format = new QHBoxLayout;
    format->setContentsMargins(0, 0, 0, 0);
    format->setSpacing(5);
    formatIcon_ = new QLabel(toolbar);
    formatIcon_->setFixedSize(14, 14);
    format->addWidget(formatIcon_, 0, Qt::AlignVCenter);
    auto *label = new QLabel(kind_ == Kind::Html ? tr("HTML") : tr("Markdown"), toolbar);
    label->setObjectName(QStringLiteral("previewFormat"));
    AppFonts::setInterfaceFont(label, 0.9, QFont::Medium);
    format->addWidget(label);
    layout->addLayout(format);
    layout->addStretch();

    auto *selector = new QFrame(toolbar);
    selector->setObjectName(QStringLiteral("modeSelector"));
    selector->setAccessibleName(tr("Document view mode"));
    auto *segments = new QHBoxLayout(selector);
    segments->setContentsMargins(2, 2, 2, 2);
    segments->setSpacing(2);
    auto *group = new QButtonGroup(this);

    struct ModeChoice {
        QString name;
        QString toolTip;
    };

    // In the order of Mode, whose values are the buttons' ids.
    const std::array<ModeChoice, modeCount> choices = {{{tr("Editor"), tr("Editor only")},
                                                        {tr("Split"), tr("Editor and preview side by side")},
                                                        {tr("Preview"), tr("Preview only")}}};
    for (std::size_t i = 0; i < choices.size(); ++i) {
        auto *button = new QToolButton(selector);
        button->setText(choices[i].name);
        button->setToolTip(choices[i].toolTip);
        button->setAccessibleName(choices[i].toolTip);
        button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        button->setIconSize(QSize(14, 14));
        AppFonts::setInterfaceFont(button, 0.9, QFont::Normal);
        button->setCheckable(true);
        button->setAutoRaise(true);
        button->setFocusPolicy(Qt::TabFocus);
        button->setCursor(Qt::PointingHandCursor);
        group->addButton(button, int(i));
        segments->addWidget(button);
        buttons_[i] = button;
    }
    connect(group, &QButtonGroup::idClicked, this, [this](int id) {
        setMode(static_cast<Mode>(id));
        QSettings().setValue(settingsKey(QStringLiteral("mode")), id);
    });

    layout->addWidget(selector);
    return toolbar;
}

void PreviewPage::updateToolbarIcons()
{
    formatIcon_->setPixmap(
        UiIcons::icon(QStringLiteral("document"), theme_.mutedText).pixmap(QSize(14, 14), devicePixelRatioF()));
    const std::array<QString, modeCount> names = {QStringLiteral("code"), QStringLiteral("split"),
                                                  QStringLiteral("preview")};
    for (std::size_t i = 0; i < buttons_.size(); ++i)
        buttons_[i]->setIcon(UiIcons::icon(names[i], buttons_[i]->isChecked() ? theme_.accentText : theme_.mutedText));
}

QString PreviewPage::settingsKey(const QString &name) const
{
    return (kind_ == Kind::Html ? QStringLiteral("html/") : QStringLiteral("markdown/")) + name;
}

QWidget *PreviewPage::preview() const
{
    return kind_ == Kind::Html ? static_cast<QWidget *>(html_) : markdown_;
}

void PreviewPage::setMode(Mode mode)
{
    mode_ = mode;
    buttons_[static_cast<std::size_t>(mode)]->setChecked(true);
    updateToolbarIcons();
    if (mode != Mode::Editor && isVisible())
        ensurePreview();
    QWidget *shown = preview();
    editor_->setVisible(mode != Mode::Preview || !shown);
    // Until its first page is drawn, a new web view shows an empty white
    // or black surface: the page's place stays empty meanwhile.
    if (shown)
        shown->setVisible(mode != Mode::Editor && hasRenderedPreview_);
    setFocusProxy(mode == Mode::Preview && shown ? shown : editor_);
    if (mode == Mode::Split) {
        const int half = std::max(splitter_->width() / 2, 100);
        splitter_->setSizes({half, half});
    }
    if (mode != Mode::Editor && shown)
        updatePreview();
}

void PreviewPage::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    if (mode_ != Mode::Editor && !preview())
        setMode(mode_);
}

void PreviewPage::ensurePreview()
{
    if (preview())
        return;
    if (kind_ == Kind::Html) {
        html_ = new HtmlPreview(servedFolder(), this);
        connect(html_, &HtmlPreview::openFileRequested, this, &PreviewPage::openFileRequested);
        connect(html_, &HtmlPreview::rendered, this, &PreviewPage::revealPreview, Qt::SingleShotConnection);
    } else {
        markdown_ = new MarkdownPreview(theme_, this);
        connect(markdown_, &MarkdownPreview::openFileRequested, this, &PreviewPage::openFileRequested);
        connect(markdown_, &MarkdownPreview::rendered, this, &PreviewPage::revealPreview, Qt::SingleShotConnection);
    }
    preview()->hide();
    splitter_->addWidget(preview());
}

QString PreviewPage::servedFolder() const
{
    const QFileInfo file(editor_->filePath());
    const bool isInProject =
        !projectFolder_.isEmpty() && file.absoluteFilePath().startsWith(QDir(projectFolder_).absolutePath() + u'/');
    return isInProject ? projectFolder_ : file.absolutePath();
}

void PreviewPage::revealPreview()
{
    hasRenderedPreview_ = true;
    setMode(mode_); // shown, and in Split mode given half of the room
}

void PreviewPage::revealEditor()
{
    if (mode_ == Mode::Preview)
        setMode(Mode::Split);
}

void PreviewPage::updatePreview()
{
    updateTimer_.stop();
    if (html_)
        html_->render(editor_->text(), QFileInfo(editor_->filePath()).absoluteFilePath());
    else if (markdown_)
        markdown_->render(editor_->text(), QFileInfo(editor_->filePath()).absolutePath());
    else
        return;
    syncScroll();
}

// In Split mode, the preview shows the same share of the text as the editor.
void PreviewPage::syncScroll()
{
    if (mode_ != Mode::Split || !preview())
        return;
    const QScrollBar *source = editor_->verticalScrollBar();
    if (source->maximum() <= 0)
        return;
    const double ratio = double(source->value()) / source->maximum();
    if (html_)
        html_->scrollToRatio(ratio);
    else
        markdown_->scrollToRatio(ratio);
}

void PreviewPage::applyTheme(const Theme &theme)
{
    theme_ = theme;
    updateToolbarIcons();
    editor_->applyTheme(theme);
    // An HTML page has its own colors.
    if (markdown_)
        markdown_->applyTheme(theme);
}

bool PreviewPage::hasPreview(const QString &path)
{
    const QString language = LanguageRegistry::languageForFile(path);
    return language == QLatin1String("markdown") || language == QLatin1String("html");
}
