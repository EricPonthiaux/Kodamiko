#include "ProjectFiles.h"

#include "git/IgnoreRules.h"

#include <QDir>

namespace ProjectFiles {

QFileInfoList list(const QString &folder, const Options &options)
{
    QFileInfoList result;
    const IgnoreRules ignoreRules(folder);
    QDir::Filters filters = QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot;
    if (options.hidden)
        filters |= QDir::Hidden;

    std::function<void(const QString &)> visit = [&](const QString &directory) {
        const QFileInfoList entries = QDir(directory).entryInfoList(filters, QDir::Name);
        for (const QFileInfo &entry : entries) {
            if (result.size() >= options.maxFiles)
                return;
            const QString path = entry.absoluteFilePath();
            if (entry.isDir()) {
                if (!entry.isSymLink() && entry.fileName() != QLatin1String("node_modules")
                    && entry.fileName() != QLatin1String(".git") && !ignoreRules.isIgnored(path, true))
                    visit(path);
                continue;
            }
            if (options.accept && !options.accept(entry))
                continue;
            if (!ignoreRules.isIgnored(path, false))
                result.append(entry);
        }
    };
    visit(QDir(folder).absolutePath());
    return result;
}

} // namespace ProjectFiles
