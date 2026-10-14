#include "WorkingTreeDiff.h"

#include <QDir>
#include <QFileInfo>

#include <git2.h>

#include <memory>

namespace {

struct GitFree {
    void operator()(git_repository *p) const { git_repository_free(p); }
    void operator()(git_object *p) const { git_object_free(p); }
    void operator()(git_tree *p) const { git_tree_free(p); }
    void operator()(git_diff *p) const { git_diff_free(p); }
    void operator()(git_patch *p) const { git_patch_free(p); }
};

template <typename T>
using GitPtr = std::unique_ptr<T, GitFree>;

void check(int result, const char *action)
{
    if (result >= 0)
        return;
    const git_error *error = git_error_last();
    throw GitError(QStringLiteral("%1 : %2")
                            .arg(QString::fromUtf8(action),
                                 error ? QString::fromUtf8(error->message) : QStringLiteral("erreur inconnue")));
}

// Garde libgit2 initialisée le temps du calcul.
struct LibGit2 {
    LibGit2() { git_libgit2_init(); }
    ~LibGit2() { git_libgit2_shutdown(); }
};

FileChange fileChangeOf(const git_diff_delta *delta, const QString &prefix)
{
    const QString newPath = QString::fromUtf8(delta->new_file.path).mid(prefix.size());
    const QString oldPath = QString::fromUtf8(delta->old_file.path).mid(prefix.size());
    switch (delta->status) {
    case GIT_DELTA_ADDED:
    case GIT_DELTA_UNTRACKED:
        return {ChangeKind::Added, newPath, {}};
    case GIT_DELTA_DELETED:
        return {ChangeKind::Deleted, oldPath, {}};
    case GIT_DELTA_RENAMED:
        return {ChangeKind::Renamed, newPath, oldPath};
    default:
        return {ChangeKind::Modified, newPath, {}};
    }
}

} // namespace

bool WorkingTreeDiff::compute(const QString &folder, QList<WorkingTreeChange> *changes)
{
    const LibGit2 libgit2;
    changes->clear();

    git_repository *repository = nullptr;
    const QByteArray folderPath = QDir::toNativeSeparators(folder).toUtf8();
    const int opened = git_repository_open_ext(&repository, folderPath.constData(), 0, nullptr);
    if (opened == GIT_ENOTFOUND)
        return false;
    check(opened, "Ouverture du dépôt git");
    GitPtr<git_repository> repositoryGuard(repository);
    if (git_repository_is_bare(repository))
        return false;

    // Sous-dossier du dépôt ouvert : préfixe de ses chemins (« src/ »).
    const QString workDir = QFileInfo(QString::fromUtf8(git_repository_workdir(repository))).canonicalFilePath();
    QString prefix = QDir(workDir).relativeFilePath(QFileInfo(folder).canonicalFilePath());
    prefix = prefix == QLatin1String(".") ? QString() : prefix + QLatin1Char('/');

    // Dépôt sans commit : tout est comparé à un arbre vide.
    GitPtr<git_tree> headTree;
    git_object *head = nullptr;
    const int headFound = git_revparse_single(&head, repository, "HEAD^{tree}");
    if (headFound == 0)
        headTree.reset(reinterpret_cast<git_tree *>(head));
    else if (headFound != GIT_ENOTFOUND)
        check(headFound, "Lecture du dernier commit");

    QByteArray prefixBytes = prefix.toUtf8();
    char *pathspec[] = {prefixBytes.data()};
    git_diff_options options;
    git_diff_options_init(&options, GIT_DIFF_OPTIONS_VERSION);
    options.flags = GIT_DIFF_INCLUDE_UNTRACKED | GIT_DIFF_RECURSE_UNTRACKED_DIRS | GIT_DIFF_SHOW_UNTRACKED_CONTENT;
    if (!prefix.isEmpty()) {
        options.pathspec.strings = pathspec;
        options.pathspec.count = 1;
    }

    git_diff *diff = nullptr;
    check(git_diff_tree_to_workdir_with_index(&diff, repository, headTree.get(), &options),
          "Comparaison avec le dernier commit");
    GitPtr<git_diff> diffGuard(diff);

    git_diff_find_options findOptions;
    git_diff_find_options_init(&findOptions, GIT_DIFF_FIND_OPTIONS_VERSION);
    findOptions.flags = GIT_DIFF_FIND_RENAMES | GIT_DIFF_FIND_FOR_UNTRACKED;
    check(git_diff_find_similar(diff, &findOptions), "Détection des renommages");

    const size_t deltaCount = git_diff_num_deltas(diff);
    for (size_t i = 0; i < deltaCount; ++i) {
        git_patch *patch = nullptr;
        check(git_patch_from_diff(&patch, diff, i), "Calcul du diff");
        GitPtr<git_patch> patchGuard(patch);
        git_buf buffer = GIT_BUF_INIT;
        check(git_patch_to_buf(&buffer, patch), "Formatage du diff");
        const QString text = QString::fromUtf8(buffer.ptr, static_cast<qsizetype>(buffer.size));
        git_buf_dispose(&buffer);
        changes->append({fileChangeOf(git_diff_get_delta(diff, i), prefix), text});
    }
    return true;
}
