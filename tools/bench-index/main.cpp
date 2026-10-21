// SPDX-License-Identifier: GPL-3.0-only

// bench-index <folder> [word…]: how long the occurrence index takes to
// index the folder from nothing, then from its saved index, and to find
// words in it (the plan's budgets: cold < 30 s on 8 cores, warm < 1 s,
// filesContaining < 20 ms). The saved index is the bench's own.

#include "index/OccurrenceIndex.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QTextStream>

#include <cstdio>

namespace {

QTextStream &out()
{
    static QTextStream stream(stdout);
    return stream;
}

// Indexes `folder`; returns the milliseconds it took.
qint64 indexFolder(OccurrenceIndex &index, const QString &folder)
{
    QElapsedTimer timer;
    timer.start();
    QEventLoop loop;
    QObject::connect(&index, &OccurrenceIndex::indexingFinished, &loop, &QEventLoop::quit);
    index.setFolder(folder);
    loop.exec();
    return timer.elapsed();
}

void measureSearches(const OccurrenceStore &store, const QStringList &words)
{
    for (const QString &word : words) {
        QElapsedTimer timer;
        timer.start();
        const qsizetype files = store.filesContaining(word.toUtf8()).size();
        const qint64 filesNs = timer.nsecsElapsed();
        timer.restart();
        qsizetype occurrences = 0;
        store.forEachOccurrence(word.toUtf8(), [&occurrences](const OccurrenceStore::Location &) {
            ++occurrences;
            return true;
        });
        out() << QStringLiteral("  %1: %2 files in %3 ms, %4 occurrences in %5 ms\n")
                     .arg(word)
                     .arg(files)
                     .arg(double(filesNs) / 1e6, 0, 'f', 2)
                     .arg(occurrences)
                     .arg(double(timer.nsecsElapsed()) / 1e6, 0, 'f', 2);
    }
}

} // namespace

int main(int argc, char **argv)
{
    const QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("duet-bench-index"));
    const QStringList arguments = QCoreApplication::arguments().mid(1);
    if (arguments.isEmpty()) {
        std::fputs("usage: bench-index <folder> [word…]\n", stderr);
        return 2;
    }
    const QString &folder = arguments.first();
    QFile::remove(OccurrenceIndex::cachePathFor(folder));

    OccurrenceIndex cold;
    const qint64 coldMs = indexFolder(cold, folder);
    out() << QStringLiteral("cold: %1 files, %2 words in %3 ms\n")
                 .arg(cold.store().fileCount())
                 .arg(cold.store().wordCount())
                 .arg(coldMs);
    const qint64 cacheSize = QFile(OccurrenceIndex::cachePathFor(folder)).size();
    out() << QStringLiteral("saved index: %1 MB\n").arg(double(cacheSize) / 1e6, 0, 'f', 1);

    OccurrenceIndex warm;
    out() << QStringLiteral("warm: %1 ms\n").arg(indexFolder(warm, folder));
    measureSearches(warm.store(), arguments.mid(1));
    out().flush();
    return 0;
}
