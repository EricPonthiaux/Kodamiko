#pragma once

#include <QList>
#include <QString>

// A starting point for a new project: the build file and the first source
// of one language, written so that ProjectDetector finds the language and
// its runtime version (the ones ToolCatalog proposes by default), and a
// .duet/run.json with a Build, a Run and a Debug configuration.
struct ProjectTemplate {
    QString id;          // "empty", or a ProjectLanguages family: "cpp", "java"...
    QString title;       // "C++ · CMake"
    QString description; // what it creates, in one line
};

namespace ProjectTemplates {

// Empty first, then one per language, in ProjectLanguages::families() order.
QList<ProjectTemplate> all();

// "my_app" from "My App!": what the build files name the project (CMake
// target, Python package, npm package, Maven artifact). Lower case, letters,
// digits and underscores, never starting with a digit; "project" when
// nothing is left.
QString identifierOf(const QString &name);

// Whether create() accepts `folder`: it does not exist, or is an empty folder.
bool isFreeFolder(const QString &folder);

// Creates `folder` with the files of the template `id`, the project named
// after the folder; `folder` must be free (isFreeFolder). Returns
// false with a message in `error` if `id` is unknown or a file cannot be
// written; the files written before the failure stay.
[[nodiscard]] bool create(const QString &id, const QString &folder, QString *error);

} // namespace ProjectTemplates
