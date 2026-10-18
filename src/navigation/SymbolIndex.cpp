#include "SymbolIndex.h"

#include "outline/OutlineProviders.h"
#include "syntax/LanguageRegistry.h"
#include "workspace/ProjectFiles.h"

#include <QFile>
#include <QFileInfo>
#include <QSet>

#include <functional>

namespace {

// Languages whose outline comes from a tree-sitter grammar: the only ones
// where "Go to Definition" makes sense.
const QStringList codeLanguages = {"c", "cpp", "java", "javascript", "typescript", "tsx"};

constexpr qint64 maxFileSize = qint64(2) * 1024 * 1024;
constexpr int maxFiles = 50000;

bool isTypeContainer(SymbolKind kind)
{
    return kind == SymbolKind::Namespace || kind == SymbolKind::Class || kind == SymbolKind::Struct ||
           kind == SymbolKind::Interface || kind == SymbolKind::Enum;
}

// "ns::Foo::bar<T>" → qualifier "ns::Foo", name "bar".
std::pair<QString, QString> splitQualifiedName(QString name)
{
    if (!name.startsWith(QLatin1String("operator"))) {
        const qsizetype angle = name.indexOf(QLatin1Char('<'));
        if (angle > 0)
            name.truncate(angle);
    }
    const qsizetype separator = name.lastIndexOf(QLatin1String("::"));
    if (separator < 0)
        return {QString(), name};
    return {name.left(separator), name.mid(separator + 2)};
}

} // namespace

struct SymbolIndex::ScanResult {
    QHash<QString, FileEntry> updated; // new or modified files
    QSet<QString> present;             // all code files found
};

SymbolIndex::SymbolIndex(QObject *parent)
    : QObject(parent)
    , workerProviders_(createProviders())
    , providers_(createProviders())
{
    pool_.setMaxThreadCount(1);
}

SymbolIndex::~SymbolIndex()
{
    pool_.waitForDone();
}

SymbolIndex::Providers SymbolIndex::createProviders()
{
    // OutlineProviders::create is not thread-safe: create everything here.
    Providers providers;
    for (const QString &languageId : codeLanguages) {
        if (auto provider = OutlineProviders::create(languageId))
            providers.emplace(languageId, std::move(provider));
    }
    return providers;
}

void SymbolIndex::setFolder(const QString &folder)
{
    folder_ = folder;
    files_.clear();
    ++generation_;
    ++revision_;
    refresh();
}

void SymbolIndex::refresh()
{
    if (folder_.isEmpty())
        return;
    if (running_) {
        refreshPending_ = true;
        return;
    }
    running_ = true;
    QHash<QString, QDateTime> known;
    for (auto it = files_.cbegin(); it != files_.cend(); ++it)
        known.insert(it.key(), it->modified);

    pool_.start([this, folder = folder_, known, generation = generation_] {
        ScanResult result = scan(folder, known, workerProviders_);
        QMetaObject::invokeMethod(
            this, [this, generation, result = std::move(result)] { apply(generation, result); }, Qt::QueuedConnection);
    });
}

void SymbolIndex::apply(int generation, const ScanResult &result)
{
    running_ = false;
    if (generation == generation_) {
        const qsizetype before = files_.size();
        for (auto it = files_.begin(); it != files_.end();) {
            if (result.present.contains(it.key()))
                ++it;
            else
                it = files_.erase(it);
        }
        for (auto it = result.updated.cbegin(); it != result.updated.cend(); ++it)
            files_.insert(it.key(), it.value());
        if (files_.size() != before || !result.updated.isEmpty())
            ++revision_;
    }
    if (std::exchange(refreshPending_, false) || generation != generation_)
        refresh();
    else
        emit indexingFinished();
}

SymbolIndex::ScanResult SymbolIndex::scan(const QString &folder,
                                          const QHash<QString, QDateTime> &known,
                                          Providers &providers)
{
    ScanResult result;
    ProjectFiles::Options options;
    options.maxFiles = maxFiles;
    options.accept = [&providers](const QFileInfo &entry) {
        return entry.size() <= maxFileSize &&
               providers.contains(LanguageRegistry::languageForFile(entry.absoluteFilePath()));
    };
    for (const QFileInfo &entry : ProjectFiles::list(folder, options)) {
        const QString path = entry.absoluteFilePath();
        const QString languageId = LanguageRegistry::languageForFile(path);
        result.present.insert(path);
        const QDateTime modified = entry.lastModified();
        if (known.value(path) == modified)
            continue;
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly))
            continue;
        const auto symbols = providers.at(languageId)->symbols(file.readAll());
        result.updated.insert(path, {modified, flatten(symbols, path, languageId)});
    }
    return result;
}

void SymbolIndex::updateFile(const QString &path, const QByteArray &text)
{
    const QString languageId = LanguageRegistry::languageForFile(path);
    const auto provider = providers_.find(languageId);
    if (provider == providers_.end())
        return;
    // Null date: the next indexing will reread the saved file.
    files_.insert(path, {QDateTime(), flatten(provider->second->symbols(text), path, languageId)});
    ++revision_;
}

QList<SymbolLocation> SymbolIndex::lookup(const QString &name) const
{
    QList<SymbolLocation> matches;
    for (const FileEntry &file : files_) {
        for (const SymbolLocation &symbol : file.symbols) {
            if (symbol.name == name)
                matches.append(symbol);
        }
    }
    return matches;
}

QList<SymbolLocation> SymbolIndex::symbols() const
{
    QList<SymbolLocation> result;
    for (const FileEntry &file : files_)
        result.append(file.symbols);
    return result;
}

QList<SymbolLocation> SymbolIndex::symbolsIn(const QString &path) const
{
    return files_.value(path).symbols;
}

QList<SymbolLocation> SymbolIndex::flatten(const std::vector<OutlineSymbol> &symbols,
                                           const QString &filePath,
                                           const QString &languageId)
{
    QList<SymbolLocation> result;
    std::function<void(const std::vector<OutlineSymbol> &, const QString &)> visit =
        [&](const std::vector<OutlineSymbol> &level, const QString &parentName) {
            for (const OutlineSymbol &symbol : level) {
                const auto [qualifier, name] = splitQualifiedName(symbol.name);
                SymbolLocation location;
                location.name = name;
                // "void Foo::bar()": the qualifier takes precedence over the container.
                location.container = qualifier.isEmpty() ? parentName : splitQualifiedName(qualifier).second;
                location.kind = symbol.kind;
                location.declarationOnly = symbol.declarationOnly;
                location.filePath = filePath;
                location.languageId = languageId;
                // The cursor goes to "bar", not to "Foo::".
                location.position =
                    symbol.position + (qualifier.isEmpty() ? 0 : static_cast<int>(qualifier.toUtf8().size()) + 2);
                location.startLine = symbol.startLine;
                location.endLine = symbol.endLine;
                result.append(location);
                visit(symbol.children, isTypeContainer(symbol.kind) ? name : location.container);
            }
        };
    visit(symbols, QString());
    return result;
}
