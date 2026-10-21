// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "Usage.h"
#include "index/Occurrence.h"

#include <QHash>

class OccurrenceStore;

// Usages as the Usages pane shows them, worked out from the files' texts
// (the unsaved ones for the open files with changes, given by path): the
// kind of each usage comes from the occurrence scanner. Reads files: meant
// for a worker thread.
namespace UsageFinder {

UsageKind kindOf(OccurrenceRole role);

// The language server's `locations`, each with its kind and its line, by
// file then in text order; a location the scanner does not know is a read.
QList<Usage> classify(const QList<LspProtocol::Location> &locations, const QHash<QString, QString> &unsavedTexts);

// Every occurrence of `word` in the files `index` says it is in, and in the
// unsaved texts: what is found without a language server.
QList<Usage> findWord(const QString &word, const OccurrenceStore *index, const QHash<QString, QString> &unsavedTexts);

} // namespace UsageFinder
