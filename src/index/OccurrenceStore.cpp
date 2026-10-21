// SPDX-License-Identifier: GPL-3.0-only

#include "OccurrenceStore.h"

#include "PostingCodec.h"

#include <QDataStream>
#include <QFile>
#include <QSaveFile>

#include <mutex>

namespace {

// Changed whenever what save() writes changes.
constexpr quint32 fileMagic = 0x44494458; // "DIDX"
constexpr quint32 formatVersion = 1;
// Renumbered once the dead files are this many, and as many as the
// living ones.
constexpr qsizetype compactionThreshold = 1000;

} // namespace

std::uint32_t OccurrenceStore::wordIdLocked(QByteArrayView word)
{
    const QByteArray key = word.toByteArray();
    const auto found = wordIds_.constFind(key);
    if (found != wordIds_.cend())
        return *found;
    const auto id = std::uint32_t(words_.size());
    words_.push_back(key);
    filesOfWord_.emplace_back();
    wordIds_.insert(key, id);
    return id;
}

void OccurrenceStore::addFileLocked(const QString &path, FileState state, QByteArray postings)
{
    const auto fileId = std::uint32_t(files_.size());
    for (const std::uint32_t wordId : PostingCodec::wordIds(postings))
        filesOfWord_[wordId].push_back(fileId); // ids grow: the list stays sorted
    files_.push_back({path, state, std::move(postings)});
    fileIds_.insert(path, fileId);
}

void OccurrenceStore::killFileLocked(const QString &path)
{
    const auto found = fileIds_.constFind(path);
    if (found == fileIds_.cend())
        return;
    File &file = files_[*found];
    file.isAlive = false;
    file.postings.clear();
    fileIds_.erase(found);
    ++deadFiles_;
}

void OccurrenceStore::setFiles(const QList<IndexedFile> &files)
{
    const std::unique_lock lock(mutex_);
    for (const IndexedFile &file : files) {
        QList<PostingCodec::Posting> postings;
        postings.reserve(file.occurrences.size());
        for (const Occurrence &occurrence : file.occurrences)
            postings.append({wordIdLocked(occurrence.word), occurrence.offset, occurrence.line, occurrence.role});
        killFileLocked(file.path);
        addFileLocked(file.path, file.state, PostingCodec::encode(std::move(postings)));
    }
    const bool hasManyDeadFiles = deadFiles_ >= compactionThreshold && deadFiles_ >= fileIds_.size();
    if (hasManyDeadFiles)
        compactLocked();
}

void OccurrenceStore::removeFiles(const QStringList &paths)
{
    const std::unique_lock lock(mutex_);
    for (const QString &path : paths)
        killFileLocked(path);
}

void OccurrenceStore::clear()
{
    const std::unique_lock lock(mutex_);
    clearLocked();
}

void OccurrenceStore::clearLocked()
{
    files_.clear();
    fileIds_.clear();
    deadFiles_ = 0;
    words_.clear();
    wordIds_.clear();
    filesOfWord_.clear();
}

void OccurrenceStore::compactLocked()
{
    std::vector<std::uint32_t> newIds(files_.size(), UINT32_MAX);
    std::vector<File> living;
    living.reserve(fileIds_.size());
    for (std::size_t id = 0; id < files_.size(); ++id) {
        if (!files_[id].isAlive)
            continue;
        newIds[id] = std::uint32_t(living.size());
        living.push_back(std::move(files_[id]));
    }
    for (std::vector<std::uint32_t> &files : filesOfWord_) {
        std::erase_if(files, [&newIds](std::uint32_t id) { return newIds[id] == UINT32_MAX; });
        for (std::uint32_t &id : files)
            id = newIds[id];
    }
    files_ = std::move(living);
    for (auto it = fileIds_.begin(); it != fileIds_.end(); ++it)
        it.value() = newIds[it.value()];
    deadFiles_ = 0;
}

QHash<QString, OccurrenceStore::FileState> OccurrenceStore::fileStates() const
{
    const std::shared_lock lock(mutex_);
    QHash<QString, FileState> states;
    states.reserve(fileIds_.size());
    for (auto it = fileIds_.cbegin(); it != fileIds_.cend(); ++it)
        states.insert(it.key(), files_[it.value()].state);
    return states;
}

qsizetype OccurrenceStore::fileCount() const
{
    const std::shared_lock lock(mutex_);
    return fileIds_.size();
}

qsizetype OccurrenceStore::wordCount() const
{
    const std::shared_lock lock(mutex_);
    return qsizetype(words_.size());
}

QStringList OccurrenceStore::filesContaining(QByteArrayView word) const
{
    const std::shared_lock lock(mutex_);
    const auto found = wordIds_.constFind(word.toByteArray());
    if (found == wordIds_.cend())
        return {};
    QStringList paths;
    for (const std::uint32_t fileId : filesOfWord_[*found]) {
        if (files_[fileId].isAlive)
            paths.append(files_[fileId].path);
    }
    return paths;
}

void OccurrenceStore::forEachOccurrence(QByteArrayView word, const std::function<bool(const Location &)> &visit) const
{
    const std::shared_lock lock(mutex_);
    const auto found = wordIds_.constFind(word.toByteArray());
    if (found == wordIds_.cend())
        return;
    for (const std::uint32_t fileId : filesOfWord_[*found]) {
        const File &file = files_[fileId];
        if (!file.isAlive)
            continue;
        for (const PostingCodec::Posting &posting : PostingCodec::find(file.postings, *found)) {
            if (!visit({file.path, posting.offset, posting.line, posting.role}))
                return;
        }
    }
}

QList<Occurrence> OccurrenceStore::occurrencesIn(const QString &path) const
{
    const std::shared_lock lock(mutex_);
    QList<Occurrence> occurrences;
    const auto found = fileIds_.constFind(path);
    if (found == fileIds_.cend())
        return occurrences;
    PostingCodec::decode(files_[*found].postings, [&](const PostingCodec::Posting &posting) {
        occurrences.append({words_[posting.wordId], posting.offset, posting.line, posting.role});
        return true;
    });
    return occurrences;
}

bool OccurrenceStore::save(const QString &path, QString *error) const
{
    const std::shared_lock lock(mutex_);
    QSaveFile output(path);
    if (!output.open(QIODevice::WriteOnly)) {
        *error = output.errorString();
        return false;
    }
    QDataStream out(&output);
    out << fileMagic << formatVersion << quint32(words_.size());
    for (const QByteArray &word : words_)
        out << word;
    out << quint32(fileIds_.size());
    for (const File &file : files_) {
        if (file.isAlive)
            out << file.path << file.state.size << file.state.modifiedMs << file.postings;
    }
    if (!output.commit()) {
        *error = output.errorString();
        return false;
    }
    return true;
}

bool OccurrenceStore::load(const QString &path)
{
    const std::unique_lock lock(mutex_);
    clearLocked();
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return false;
    QDataStream in(&file);
    quint32 magic = 0;
    quint32 version = 0;
    quint32 wordCount = 0;
    in >> magic >> version >> wordCount;
    if (magic != fileMagic || version != formatVersion || in.status() != QDataStream::Ok)
        return false;
    for (quint32 i = 0; i < wordCount && in.status() == QDataStream::Ok; ++i) {
        QByteArray word;
        in >> word;
        wordIdLocked(word);
    }
    quint32 fileCount = 0;
    in >> fileCount;
    for (quint32 i = 0; i < fileCount && in.status() == QDataStream::Ok; ++i) {
        QString filePath;
        FileState state;
        QByteArray postings;
        in >> filePath >> state.size >> state.modifiedMs >> postings;
        // A word id past the dictionary: a damaged file, dropped whole.
        const QList<std::uint32_t> ids = PostingCodec::wordIds(postings);
        if (!ids.isEmpty() && ids.last() >= words_.size()) {
            clearLocked();
            return false;
        }
        addFileLocked(filePath, state, std::move(postings));
    }
    if (in.status() != QDataStream::Ok) {
        clearLocked();
        return false;
    }
    return true;
}
