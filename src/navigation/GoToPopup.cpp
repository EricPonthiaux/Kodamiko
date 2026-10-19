#include "GoToPopup.h"

#include "RecentFiles.h"
#include "workspace/CommandPalette.h"
#include "workspace/ProjectFiles.h"

#include <QCheckBox>
#include <QDir>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLineEdit>
#include <QRegularExpression>
#include <QTabBar>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <utility>
#include <vector>

namespace {

constexpr int visibleRows = 14;
constexpr int maxShown = 200;

QString kindName(SymbolKind kind)
{
    switch (kind) {
    case SymbolKind::Namespace:
        return QObject::tr("namespace");
    case SymbolKind::Class:
        return QObject::tr("class");
    case SymbolKind::Struct:
        return QObject::tr("struct");
    case SymbolKind::Interface:
        return QObject::tr("interface");
    case SymbolKind::Enum:
        return QObject::tr("enum");
    case SymbolKind::Type:
        return QObject::tr("type");
    case SymbolKind::Function:
        return QObject::tr("function");
    case SymbolKind::Method:
        return QObject::tr("method");
    case SymbolKind::Field:
        return QObject::tr("field");
    case SymbolKind::Macro:
        return QObject::tr("macro");
    case SymbolKind::Heading:
        return QObject::tr("heading");
    case SymbolKind::Target:
        return QObject::tr("target");
    }
    return {};
}

// "Foo::bar" or "Foo.bar": the container to match, and the name.
std::pair<QString, QString> splitContainer(const QString &query)
{
    static const QRegularExpression separator(QStringLiteral("::|\\.|#"));
    const qsizetype at = query.lastIndexOf(separator);
    if (at < 0)
        return {QString(), query};
    const QRegularExpressionMatch match = separator.match(query, at);
    return {query.left(at), query.mid(at + match.capturedLength())};
}

// How well `symbol` matches `name` and `container` (either may be empty);
// nullopt if it does not.
std::optional<int> symbolScore(const SymbolLocation &symbol, const QString &name, const QString &container)
{
    int score = name.isEmpty() ? 0 : CommandPalette::matchScore(symbol.name, name);
    if (score < 0)
        return std::nullopt;
    if (!container.isEmpty()) {
        const int containerScore = CommandPalette::matchScore(symbol.container, container);
        if (containerScore < 0)
            return std::nullopt;
        score += containerScore;
    }
    if (symbol.name.compare(name, Qt::CaseInsensitive) == 0)
        score += 30;
    else if (symbol.name.startsWith(name, Qt::CaseInsensitive))
        score += 15;
    if (!symbol.declarationOnly)
        score += 2; // the definition before its declaration
    return score;
}

} // namespace

GoToPopup::GoToPopup(SymbolIndex *index, RecentFiles *recent, QWidget *parent)
    : OverlayPopup(QStringLiteral("goTo"), QSizeF(0.55, 0.6), QSize(640, 400), parent)
    , index_(index)
    , recent_(recent)
{
    setBehavior(Behavior::Transient);
    pool_.setMaxThreadCount(1);

    tabs_ = new QTabBar(this);
    tabs_->setObjectName(QStringLiteral("goToTabs"));
    tabs_->setDrawBase(false);
    tabs_->setExpanding(false);
    tabs_->setFocusPolicy(Qt::NoFocus);
    tabs_->addTab(tr("Files"));
    tabs_->addTab(tr("Symbols"));
    tabs_->addTab(tr("Recent Files"));
    tabs_->setTabToolTip(0, tr("Go to File (Ctrl+Shift+N)"));
    tabs_->setTabToolTip(1, tr("Go to Symbol (Ctrl+Alt+Shift+N)"));
    tabs_->setTabToolTip(2, tr("Recent Files (Ctrl+E)"));
    connect(tabs_, &QTabBar::currentChanged, this, [this] {
        editedByClaude_->setVisible(tab() == Tab::Recent);
        filter();
    });

    editedByClaude_ = new QCheckBox(tr("Edited by Claude"), this);
    editedByClaude_->setToolTip(tr("Files Claude wrote, most recent first (Ctrl+E)"));
    editedByClaude_->setFocusPolicy(Qt::NoFocus);
    connect(editedByClaude_, &QCheckBox::toggled, this, &GoToPopup::filter);

    search_ = new QLineEdit(this);
    search_->setClearButtonEnabled(true);
    search_->installEventFilter(this);
    connect(search_, &QLineEdit::textChanged, this, &GoToPopup::filter);
    connect(search_, &QLineEdit::returnPressed, this, &GoToPopup::openCurrent);

    list_ = new QTreeWidget(this);
    list_->setColumnCount(2);
    list_->setHeaderHidden(true);
    list_->setRootIsDecorated(false);
    list_->setUniformRowHeights(true);
    list_->setFocusPolicy(Qt::NoFocus);
    list_->setFrameShape(QFrame::NoFrame);
    list_->setTextElideMode(Qt::ElideLeft); // the end of a path tells the most
    list_->header()->setStretchLastSection(true);
    list_->header()->setSectionResizeMode(0, QHeaderView::Interactive);
    connect(list_, &QTreeWidget::itemClicked, this, &GoToPopup::openCurrent);

    auto *header = new QHBoxLayout;
    header->setContentsMargins(0, 0, 0, 0);
    header->addWidget(tabs_);
    header->addStretch();
    header->addWidget(editedByClaude_);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 4, 6, 6);
    layout->setSpacing(4);
    layout->addLayout(header);
    layout->addWidget(search_);
    layout->addWidget(list_);
}

GoToPopup::~GoToPopup()
{
    pool_.waitForDone();
}

void GoToPopup::setFolder(const QString &folder)
{
    folder_ = folder;
    files_.clear();
    ++generation_;
}

GoToPopup::Tab GoToPopup::tab() const
{
    return static_cast<Tab>(tabs_->currentIndex());
}

void GoToPopup::setTab(Tab tab)
{
    tabs_->setCurrentIndex(static_cast<int>(tab));
}

void GoToPopup::popup(Tab tab)
{
    const bool reopened = isVisible();
    if (reopened && tab == Tab::Recent && this->tab() == Tab::Recent) {
        // Ctrl+E twice: the files edited by Claude, as in IntelliJ.
        editedByClaude_->toggle();
        return;
    }
    symbols_ = index_->symbols();
    scanFiles();
    {
        const QSignalBlocker blocker(editedByClaude_);
        editedByClaude_->setChecked(false);
    }
    const QSignalBlocker blocker(tabs_);
    setTab(tab);
    editedByClaude_->setVisible(tab == Tab::Recent);
    search_->clear();
    filter();
    if (reopened)
        return;

    showCentered();
    list_->setColumnWidth(0, list_->width() * 2 / 5);
    search_->setFocus();
}

void GoToPopup::scanFiles()
{
    if (folder_.isEmpty() || scanning_)
        return;
    scanning_ = true;
    pool_.start([this, folder = folder_, generation = generation_] {
        ProjectFiles::Options options;
        options.hidden = true;
        QStringList files;
        const QDir root(folder);
        for (const QFileInfo &entry : ProjectFiles::list(folder, options))
            files.append(root.relativeFilePath(entry.absoluteFilePath()));
        QMetaObject::invokeMethod(
            this,
            [this, generation, files = std::move(files)] {
                scanning_ = false;
                if (generation != generation_) {
                    scanFiles(); // another folder in the meantime
                    return;
                }
                files_ = files;
                if (isVisible() && tab() != Tab::Symbols)
                    filter();
            },
            Qt::QueuedConnection);
    });
}

std::pair<QString, int> GoToPopup::splitLine(const QString &query)
{
    static const QRegularExpression suffix(QStringLiteral("^(.*?):(\\d+)(?::\\d+)?$"));
    const QRegularExpressionMatch match = suffix.match(query.trimmed());
    if (!match.hasMatch())
        return {query.trimmed(), -1};
    return {match.captured(1), std::max(match.captured(2).toInt() - 1, 0)};
}

int GoToPopup::fileScore(const QString &relativePath, const QString &query)
{
    const QString normalized = QDir::fromNativeSeparators(query);
    return fileScore(relativePath, normalized, normalized.contains(QLatin1Char('/')));
}

// Runs over every file of the project at each keystroke: no allocation.
int GoToPopup::fileScore(QStringView relativePath, QStringView normalized, bool hasSlash)
{
    if (normalized.isEmpty())
        return 0;
    const QStringView name = relativePath.sliced(relativePath.lastIndexOf(QLatin1Char('/')) + 1);
    if (!hasSlash) {
        const int score = CommandPalette::matchScore(name, normalized);
        if (score >= 0) {
            // Whole name first, then names that start with the query.
            int bonus = name.startsWith(normalized, Qt::CaseInsensitive) ? 20 : 0;
            const qsizetype dot = name.lastIndexOf(QLatin1Char('.'));
            const QStringView baseName = dot >= 0 ? name.first(dot) : name;
            if (baseName.compare(normalized, Qt::CaseInsensitive) == 0 ||
                name.compare(normalized, Qt::CaseInsensitive) == 0)
                bonus += 30;
            return 2 * score + bonus;
        }
    }
    // "src/edit" or letters spread over the folders: weaker match.
    const int score = CommandPalette::matchScore(relativePath, normalized);
    return score < 0 ? -1 : score;
}

void GoToPopup::filter()
{
    const QString query = search_->text().trimmed();
    QList<Entry> entries;
    switch (tab()) {
    case Tab::Files:
        search_->setPlaceholderText(tr("Type a file name, optionally followed by “:line”"));
        filterFiles(query, entries);
        break;
    case Tab::Symbols:
        search_->setPlaceholderText(tr("Type a symbol name (“Class::method” narrows it down)"));
        filterSymbols(query, entries);
        break;
    case Tab::Recent:
        search_->setPlaceholderText(tr("Type to filter the recent files"));
        filterRecent(query, entries);
        break;
    }
    shown_ = entries;

    list_->clear();
    const QBrush gray = palette().brush(QPalette::Disabled, QPalette::Text);
    for (qsizetype i = 0; i < shown_.size(); ++i) {
        auto *item = new QTreeWidgetItem(list_, {shown_[i].title, shown_[i].location});
        item->setData(0, Qt::UserRole, QVariant::fromValue(i));
        item->setToolTip(0, shown_[i].filePath);
        item->setForeground(1, gray);
    }
    if (shown_.isEmpty()) {
        auto *item = new QTreeWidgetItem(list_, {emptyMessage(query)});
        item->setFlags(Qt::NoItemFlags);
        return;
    }
    // Recent Files opens on the previous file, as Ctrl+Tab would.
    const bool previousFile =
        tab() == Tab::Recent && query.isEmpty() && !editedByClaude_->isChecked() && list_->topLevelItemCount() > 1;
    list_->setCurrentItem(list_->topLevelItem(previousFile ? 1 : 0));
}

QString GoToPopup::emptyMessage(const QString &query) const
{
    const bool isReadingFiles = scanning_ && files_.isEmpty();
    const bool isIndexing = index_->isIndexing() && symbols_.isEmpty();
    if (tab() == Tab::Files && isReadingFiles)
        return tr("Reading the project's files…");
    if (tab() == Tab::Symbols && isIndexing)
        return tr("Indexing in progress…");
    if (tab() == Tab::Recent && query.isEmpty())
        return editedByClaude_->isChecked() ? tr("Claude has not edited any file yet") : tr("No recent files");
    return tr("Nothing found");
}

QString GoToPopup::relativePath(const QString &path) const
{
    const QString relative = QDir(folder_).relativeFilePath(path);
    return relative.startsWith(QLatin1String("..")) ? QDir::toNativeSeparators(path) : relative;
}

void GoToPopup::filterFiles(const QString &query, QList<Entry> &entries) const
{
    const auto [name, line] = splitLine(query);
    const QString normalized = QDir::fromNativeSeparators(name);
    const bool hasSlash = normalized.contains(QLatin1Char('/'));
    std::vector<std::pair<int, qsizetype>> matches; // score, index in files_
    for (qsizetype i = 0; i < files_.size(); ++i) {
        const int score = fileScore(files_[i], normalized, hasSlash);
        if (score >= 0)
            matches.emplace_back(score, i);
    }
    // Only the shown ones are sorted: one letter matches nearly every file.
    const auto shown = std::min<qsizetype>(static_cast<qsizetype>(matches.size()), maxShown);
    std::partial_sort(matches.begin(), matches.begin() + shown, matches.end(), [this](const auto &a, const auto &b) {
        if (a.first != b.first)
            return a.first > b.first;
        if (files_[a.second].size() != files_[b.second].size())
            return files_[a.second].size() < files_[b.second].size(); // shallower files first
        return a.second < b.second;
    });
    matches.resize(static_cast<size_t>(shown));
    for (const auto &[score, index] : std::as_const(matches)) {
        const QString &path = files_[index];
        const qsizetype slash = path.lastIndexOf(QLatin1Char('/'));
        Entry entry;
        entry.title = path.mid(slash + 1);
        if (line >= 0)
            entry.title += QStringLiteral(":%1").arg(line + 1);
        entry.location = slash >= 0 ? path.left(slash) : QString();
        entry.filePath = QDir(folder_).filePath(path);
        entries.append(entry);
    }
}

void GoToPopup::filterSymbols(const QString &query, QList<Entry> &entries) const
{
    // "Foo::bar" or "Foo.bar": the container must match too.
    const auto [container, name] = splitContainer(query);
    if (name.isEmpty() && container.isEmpty())
        return; // every symbol of the project would be too many to list
    QList<QPair<int, qsizetype>> matches;
    for (qsizetype i = 0; i < symbols_.size(); ++i) {
        if (const std::optional<int> score = symbolScore(symbols_[i], name, container))
            matches.append({*score, i});
    }
    std::stable_sort(matches.begin(), matches.end(), [this](const auto &a, const auto &b) {
        if (a.first != b.first)
            return a.first > b.first;
        return symbols_[a.second].name.size() < symbols_[b.second].name.size();
    });
    if (matches.size() > maxShown)
        matches.resize(maxShown);
    for (const auto &[score, index] : std::as_const(matches))
        entries.append(symbolEntry(index));
}

GoToPopup::Entry GoToPopup::symbolEntry(qsizetype index) const
{
    const SymbolLocation &symbol = symbols_[index];
    Entry entry;
    entry.title = symbol.container.isEmpty() ? symbol.name : symbol.container + QLatin1String("::") + symbol.name;
    entry.location = QStringLiteral("%1 — %2:%3")
                         .arg(kindName(symbol.kind), relativePath(symbol.filePath))
                         .arg(symbol.startLine + 1);
    if (symbol.declarationOnly)
        entry.location += tr(" (declaration)");
    entry.filePath = symbol.filePath;
    entry.symbol = static_cast<int>(index);
    return entry;
}

void GoToPopup::filterRecent(const QString &query, QList<Entry> &entries) const
{
    const bool claudeOnly = editedByClaude_->isChecked();
    const auto [name, line] = splitLine(query);
    // Most recent first: the order is kept, the query only filters.
    for (const QString &path : claudeOnly ? recent_->editedByClaude() : recent_->opened()) {
        if (!QFileInfo::exists(path))
            continue;
        const QString relative = relativePath(path);
        if (!name.isEmpty() && fileScore(relative, name) < 0)
            continue;
        Entry entry;
        entry.title = QFileInfo(path).fileName();
        if (line >= 0)
            entry.title += QStringLiteral(":%1").arg(line + 1);
        entry.location = recentLocation(path, relative, claudeOnly);
        entry.filePath = path;
        entries.append(entry);
    }
}

QString GoToPopup::recentLocation(const QString &path, const QString &relative, bool claudeOnly) const
{
    const qsizetype slash = relative.lastIndexOf(QLatin1Char('/'));
    QString location = slash >= 0 ? relative.left(slash) : QString();
    if (claudeOnly || !recent_->isEditedByClaude(path))
        return location;
    return location + (location.isEmpty() ? tr("edited by Claude") : tr(" — edited by Claude"));
}

void GoToPopup::openCurrent()
{
    const QTreeWidgetItem *item = list_->currentItem();
    if (!item || !(item->flags() & Qt::ItemIsEnabled))
        return;
    const Entry entry = shown_.value(item->data(0, Qt::UserRole).toLongLong());
    const int line = tab() == Tab::Symbols ? -1 : splitLine(search_->text()).second;
    const SymbolLocation symbol = entry.symbol >= 0 ? symbols_.value(entry.symbol) : SymbolLocation{};
    dismiss();
    // After the popup has closed and given the focus back.
    QTimer::singleShot(0, this, [this, entry, line, symbol] {
        if (entry.symbol >= 0)
            emit symbolChosen(symbol);
        else
            emit fileChosen(entry.filePath, line);
    });
}

bool GoToPopup::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == search_ && event->type() == QEvent::KeyPress) {
        const auto *key = static_cast<QKeyEvent *>(event);
        const int row = list_->currentIndex().row();
        const int count = list_->topLevelItemCount();
        int target = -1;
        switch (key->key()) {
        case Qt::Key_Down:
            target = std::min(row + 1, count - 1);
            break;
        case Qt::Key_Up:
            target = std::max(row - 1, 0);
            break;
        case Qt::Key_PageDown:
            target = std::min(row + visibleRows, count - 1);
            break;
        case Qt::Key_PageUp:
            target = std::max(row - visibleRows, 0);
            break;
        case Qt::Key_Tab:
            setTab(static_cast<Tab>((tabs_->currentIndex() + 1) % tabs_->count()));
            return true;
        case Qt::Key_Backtab:
            setTab(static_cast<Tab>((tabs_->currentIndex() + tabs_->count() - 1) % tabs_->count()));
            return true;
        case Qt::Key_E:
            if (key->modifiers() == Qt::ControlModifier) {
                popup(Tab::Recent);
                return true;
            }
            return QFrame::eventFilter(watched, event);
        case Qt::Key_Escape:
            dismiss();
            return true;
        default:
            return QFrame::eventFilter(watched, event);
        }
        if (target >= 0 && (list_->topLevelItem(target)->flags() & Qt::ItemIsEnabled))
            list_->setCurrentItem(list_->topLevelItem(target));
        return true;
    }
    return QFrame::eventFilter(watched, event);
}
