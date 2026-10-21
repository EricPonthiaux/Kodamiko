// SPDX-License-Identifier: GPL-3.0-only

#include "OccurrenceIndex.h"

#include "OccurrenceScanner.h"
#include "syntax/LanguageRegistry.h"
#include "workspace/ProjectFiles.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <QStandardPaths>
#include <QThread>
#include <QtDebug>

#include <algorithm>

namespace {

constexpr qint64 maximumFileSize = qint64(2) * 1024 * 1024;
constexpr int maximumFiles = 200000;
// Files parsed by one task, merged into the store at once.
constexpr qsizetype batchSize = 64;
// A NUL byte in this many first bytes: a binary file.
constexpr qsizetype binaryProbeSize = 8000;

// A file to (re)index.
struct Pending {
    QString path;
    OccurrenceStore::FileState state;
};

OccurrenceStore::FileState stateOf(const QFileInfo &info)
{
    return {info.size(), info.lastModified().toMSecsSinceEpoch()};
}

// One per thread of the pool: it keeps its parser.
OccurrenceScanner &threadScanner()
{
    thread_local OccurrenceScanner scanner;
    return scanner;
}

OccurrenceStore::IndexedFile indexFile(const Pending &pending)
{
    OccurrenceStore::IndexedFile indexed{pending.path, pending.state, {}};
    QFile file(pending.path);
    if (!file.open(QIODevice::ReadOnly))
        return indexed;
    const QByteArray text = file.readAll();
    // Kept without occurrences, so as not to read it again each time.
    if (text.left(binaryProbeSize).contains('\0'))
        return indexed;
    indexed.occurrences = threadScanner().scan(LanguageRegistry::languageForFile(pending.path), text);
    return indexed;
}

// The project's files that are new or changed since `known`; the paths
// of `known` no longer there go to `removed`.
QList<Pending> changedFiles(const QString &folder,
                            const QHash<QString, OccurrenceStore::FileState> &known,
                            QStringList &removed)
{
    ProjectFiles::Options options;
    options.maxFiles = maximumFiles;
    options.accept = [](const QFileInfo &entry) { return entry.size() <= maximumFileSize; };
    QList<Pending> changed;
    QSet<QString> present;
    for (const QFileInfo &entry : ProjectFiles::list(folder, options)) {
        const QString path = entry.absoluteFilePath();
        present.insert(path);
        const OccurrenceStore::FileState state = stateOf(entry);
        if (known.value(path, {-1, -1}) != state)
            changed.append({path, state});
    }
    for (auto it = known.cbegin(); it != known.cend(); ++it) {
        if (!present.contains(it.key()))
            removed.append(it.key());
    }
    return changed;
}

} // namespace

struct OccurrenceIndex::Task {
    QString folder;
    int generation = 0;
    bool loadSaved = false;
};

OccurrenceIndex::OccurrenceIndex(QObject *parent)
    : QObject(parent)
{
    coordinator_.setMaxThreadCount(1);
    parsers_.setMaxThreadCount(std::max(1, QThread::idealThreadCount() - 1));
}

OccurrenceIndex::~OccurrenceIndex()
{
    ++*generation_;
    coordinator_.waitForDone();
    parsers_.waitForDone();
}

QString OccurrenceIndex::cachePathFor(const QString &folder)
{
    const QByteArray key = QCryptographicHash::hash(QDir::cleanPath(folder).toUtf8(), QCryptographicHash::Sha1);
    return QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + QStringLiteral("/index/") +
           QString::fromLatin1(key.toHex()) + QStringLiteral(".idx");
}

void OccurrenceIndex::setFolder(const QString &folder)
{
    folder_ = folder;
    ++*generation_; // the indexing in progress is abandoned
    loadPending_ = true;
    refresh();
}

void OccurrenceIndex::refresh()
{
    if (folder_.isEmpty())
        return;
    if (running_) {
        refreshPending_ = true;
        return;
    }
    running_ = true;
    const Task task{folder_, *generation_, std::exchange(loadPending_, false)};
    coordinator_.start([this, task] { index(task); });
}

void OccurrenceIndex::index(const Task &task)
{
    const auto isCancelled = [this, &task] { return *generation_ != task.generation; };
    if (task.loadSaved)
        store_->load(cachePathFor(task.folder)); // else empty: all is read
    QStringList removed;
    const QList<Pending> changed = changedFiles(task.folder, store_->fileStates(), removed);
    store_->removeFiles(removed);
    for (qsizetype first = 0; first < changed.size() && !isCancelled(); first += batchSize) {
        const QList<Pending> batch = changed.mid(first, batchSize);
        parsers_.start([store = store_, batch, generation = generation_, expected = task.generation] {
            QList<OccurrenceStore::IndexedFile> indexed;
            indexed.reserve(batch.size());
            for (const Pending &pending : batch) {
                if (*generation != expected)
                    return;
                indexed.append(indexFile(pending));
            }
            store->setFiles(indexed);
        });
    }
    parsers_.waitForDone();
    const bool hasChanged = !changed.isEmpty() || !removed.isEmpty();
    if (!isCancelled() && hasChanged) {
        const QString path = cachePathFor(task.folder);
        QDir().mkpath(QFileInfo(path).absolutePath());
        QString error;
        if (!store_->save(path, &error))
            qWarning().noquote() << "Cannot save the occurrence index to" << path << ":" << error;
    }
    QMetaObject::invokeMethod(this, [this, generation = task.generation] { finish(generation); }, Qt::QueuedConnection);
}

void OccurrenceIndex::finish(int generation)
{
    running_ = false;
    const bool isCurrent = generation == *generation_;
    if (!isCurrent) {
        // The folder changed meanwhile: what was indexed is another one's.
        store_->clear();
        refresh();
        return;
    }
    if (std::exchange(refreshPending_, false))
        refresh();
    else
        emit indexingFinished();
}

void OccurrenceIndex::updateFile(const QString &path, const QByteArray &text)
{
    parsers_.start([store = store_, path, text] {
        OccurrenceStore::IndexedFile indexed{path, {qint64(text.size()), -1}, {}};
        indexed.occurrences = threadScanner().scan(LanguageRegistry::languageForFile(path), text);
        store->setFiles({indexed});
    });
}
