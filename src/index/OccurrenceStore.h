// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "Occurrence.h"

#include <QByteArray>
#include <QByteArrayView>
#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>

#include <cstdint>
#include <functional>
#include <shared_mutex>
#include <vector>

// The words of a folder's files and where they occur, shared by the
// threads that index the files and those that search them: every function
// is thread-safe; searches run together, a change waits for them.
//
// Each word has an id and the list of the files it occurs in; each file
// keeps its occurrences encoded by PostingCodec. A file indexed again gets
// a new id, the old one is left out of the searches until the files are
// renumbered, once the dead ones are many: a change touches only the
// files it changes.
class OccurrenceStore {
public:
    // What tells that a file changed since it was indexed.
    struct FileState {
        qint64 size = 0;
        qint64 modifiedMs = 0; // -1: an unsaved text, read again next time

        bool operator==(const FileState &) const = default;
    };

    struct IndexedFile {
        QString path;
        FileState state;
        QList<Occurrence> occurrences;
    };

    // An occurrence of a searched word.
    struct Location {
        QString path;
        std::uint32_t offset = 0;
        std::uint32_t line = 0;
        OccurrenceRole role = OccurrenceRole::Reference;

        bool operator==(const Location &) const = default;
    };

    // Adds the files, or replaces their occurrences.
    void setFiles(const QList<IndexedFile> &files);
    void removeFiles(const QStringList &paths);
    void clear();

    QHash<QString, FileState> fileStates() const;
    qsizetype fileCount() const;
    qsizetype wordCount() const;

    // The files where `word` occurs (case-sensitive), in no order.
    QStringList filesContaining(QByteArrayView word) const;
    // Each occurrence of `word`, file by file, until `visit` returns false
    // (enough found, or the search was cancelled). `visit` runs while the
    // store is read: it must not change the store.
    void forEachOccurrence(QByteArrayView word, const std::function<bool(const Location &)> &visit) const;
    // The occurrences of `path`, by word then in text order.
    QList<Occurrence> occurrencesIn(const QString &path) const;

    // Writes the store to `path` for a later load(); false, with `error`,
    // if it cannot.
    bool save(const QString &path, QString *error) const;
    // Replaces the store with what save() wrote; false (the store then
    // empty) if the file is missing, of another version, or damaged.
    bool load(const QString &path);

private:
    struct File {
        QString path;
        FileState state;
        QByteArray postings;
        bool isAlive = true;
    };

    std::uint32_t wordIdLocked(QByteArrayView word);
    void addFileLocked(const QString &path, FileState state, QByteArray postings);
    void killFileLocked(const QString &path);
    // Renumbers the files without the dead ones.
    void compactLocked();
    void clearLocked();

    mutable std::shared_mutex mutex_;
    std::vector<File> files_;
    QHash<QString, std::uint32_t> fileIds_; // of the living files
    qsizetype deadFiles_ = 0;
    std::vector<QByteArray> words_;
    QHash<QByteArray, std::uint32_t> wordIds_;
    std::vector<std::vector<std::uint32_t>> filesOfWord_; // by word id, increasing file ids
};
