#pragma once

#include <QString>

#include <memory>

// .gitignore rules of the repository containing a folder. Without a repository,
// nothing is ignored. Usable off the UI thread; one instance per thread.
class IgnoreRules {
public:
    explicit IgnoreRules(const QString &folder);
    ~IgnoreRules();

    IgnoreRules(const IgnoreRules &) = delete;
    IgnoreRules &operator=(const IgnoreRules &) = delete;

    bool isIgnored(const QString &absolutePath, bool isDirectory) const;

private:
    struct Repository;
    std::unique_ptr<Repository> repository_;
};
