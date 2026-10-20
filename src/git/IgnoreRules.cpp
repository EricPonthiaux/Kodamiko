// SPDX-License-Identifier: GPL-3.0-only

#include "IgnoreRules.h"

#include "LibGit2.h"

#include <QDir>

struct IgnoreRules::Repository {
    libgit2::Session session;
    libgit2::Ptr<git_repository> repository;
    QDir workDir;
};

IgnoreRules::IgnoreRules(const QString &folder)
{
    auto repository = std::make_unique<Repository>();
    git_repository *opened = nullptr;
    const QByteArray folderPath = QDir::toNativeSeparators(folder).toUtf8();
    if (git_repository_open_ext(&opened, folderPath.constData(), 0, nullptr) < 0)
        return;
    repository->repository.reset(opened);
    if (git_repository_is_bare(opened))
        return;
    repository->workDir = QDir(QString::fromUtf8(git_repository_workdir(opened)));
    repository_ = std::move(repository);
}

IgnoreRules::~IgnoreRules() = default;

bool IgnoreRules::isIgnored(const QString &absolutePath, bool isDirectory) const
{
    if (!repository_)
        return false;
    QString relativePath = repository_->workDir.relativeFilePath(absolutePath);
    if (relativePath.startsWith(QLatin1String("..")))
        return false;
    // "build/" in .gitignore only matches folders: the "/" is needed.
    if (isDirectory)
        relativePath += QLatin1Char('/');
    int ignored = 0;
    if (git_ignore_path_is_ignored(&ignored, repository_->repository.get(), relativePath.toUtf8().constData()) < 0)
        return false;
    return ignored != 0;
}
