// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "outline/OutlineSymbol.h"

#include <QDateTime>
#include <QHash>
#include <QObject>
#include <QThreadPool>

#include <map>
#include <memory>

class IOutlineProvider;

// Definition or declaration found in a project file.
struct SymbolLocation {
    QString name;      // simple name: "bar" for "Foo::bar" or "bar<T>"
    QString container; // enclosing class or namespace, empty at top level
    SymbolKind kind = SymbolKind::Function;
    bool declarationOnly = false;
    QString filePath;
    QString languageId;
    int position = 0; // start of the name, in bytes
    int startLine = 0;
    int endLine = 0;
};

// Symbols of the code files in the working folder, from the same
// tree-sitter queries as the Outline pane. Indexing runs off the UI
// thread and only rereads files modified since the previous time;
// files ignored by git are not read.
class SymbolIndex : public QObject {
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(SymbolIndex)

public:
    explicit SymbolIndex(QObject *parent = nullptr);
    // Waits for the indexing in progress to finish.
    ~SymbolIndex() override;

    void setFolder(const QString &folder);

    QString folder() const { return folder_; }

    // Rereads modified, added or removed files in the background.
    void refresh();
    // Replaces a file's symbols based on its text (unsaved, for
    // example). Immediate: a single file's text is parsed quickly.
    void updateFile(const QString &path, const QByteArray &text);

    // Symbols with simple name `name`, across the whole project.
    QList<SymbolLocation> lookup(const QString &name) const;
    // Every symbol of the project, for Go to Symbol.
    QList<SymbolLocation> symbols() const;
    // Symbols of a file, in text order.
    QList<SymbolLocation> symbolsIn(const QString &path) const;

    bool isIndexing() const { return running_; }

    // Changes whenever the symbols change, for caches built from them.
    quint64 revision() const { return revision_; }

    // Flattened symbols of a file outline.
    static QList<SymbolLocation> flatten(const std::vector<OutlineSymbol> &symbols,
                                         const QString &filePath,
                                         const QString &languageId);

signals:
    void indexingFinished();

private:
    struct FileEntry {
        QDateTime modified;
        QList<SymbolLocation> symbols;
    };

    using Providers = std::map<QString, std::unique_ptr<IOutlineProvider>>;
    struct ScanResult;

    // The provider of `languageId`, nullptr if it has none.
    static IOutlineProvider *providerFor(Providers &providers, const QString &languageId);
    static ScanResult scan(const QString &folder, const QHash<QString, QDateTime> &known, Providers &providers);
    void apply(int generation, const ScanResult &result);

    QString folder_;
    QHash<QString, FileEntry> files_;
    Providers workerProviders_; // used by one indexing at a time, in its thread
    Providers providers_;       // for updateFile, in the UI thread
    QThreadPool pool_;
    int generation_ = 0; // results for a previous folder are ignored
    quint64 revision_ = 0;
    bool running_ = false;
    bool refreshPending_ = false;
};
