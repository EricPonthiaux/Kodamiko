// SPDX-License-Identifier: GPL-3.0-only

#include "ProjectFiles.h"

#include "git/IgnoreRules.h"

#include <QDir>

namespace {

using ProjectFiles::Options;

// Walks the folders depth first, collecting files until `maxFiles`.
class FileCollector {
public:
    FileCollector(const QString &folder, const Options &options)
        : options_(options)
        , ignoreRules_(folder)
    {
        if (options.hidden)
            filters_ |= QDir::Hidden;
    }

    void visit(const QString &directory)
    {
        const QFileInfoList entries = QDir(directory).entryInfoList(filters_, QDir::Name);
        for (const QFileInfo &entry : entries) {
            if (isFull())
                return;
            if (entry.isDir())
                visitFolder(entry);
            else if (isWanted(entry))
                result_.append(entry);
        }
    }

    QFileInfoList takeResult() { return std::move(result_); }

private:
    bool isFull() const { return result_.size() >= options_.maxFiles; }

    void visitFolder(const QFileInfo &entry)
    {
        const QString name = entry.fileName();
        const bool isSkippedByName = name == QLatin1String("node_modules") || name == QLatin1String(".git");
        if (entry.isSymLink() || isSkippedByName)
            return;
        const QString path = entry.absoluteFilePath();
        if (!ignoreRules_.isIgnored(path, true))
            visit(path);
    }

    bool isWanted(const QFileInfo &entry) const
    {
        if (options_.accept && !options_.accept(entry))
            return false;
        return !ignoreRules_.isIgnored(entry.absoluteFilePath(), false);
    }

    Options options_;
    IgnoreRules ignoreRules_;
    QDir::Filters filters_ = QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot;
    QFileInfoList result_;
};

} // namespace

namespace ProjectFiles {

QFileInfoList list(const QString &folder, const Options &options)
{
    FileCollector collector(folder, options);
    collector.visit(QDir(folder).absolutePath());
    return collector.takeResult();
}

} // namespace ProjectFiles
