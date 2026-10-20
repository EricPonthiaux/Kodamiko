// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QByteArray>
#include <QList>
#include <QString>

// What a project's files say about its languages: the build files and
// version files (pom.xml, .nvmrc, pyproject.toml,
// CMakeLists.txt...) near its root, and the extensions of its sources.
struct DetectedLanguage {
    QString language;       // a ProjectLanguages family
    QString runtimeVersion; // empty when no file says it
    QString runtimePath;    // the project's own runtime (.venv/bin/python)
    QString evidence;       // "pom.xml", "12 .cpp files"...
};

namespace ProjectDetector {

// The languages of the project in `folder`, in ProjectLanguages::families()
// order. Looks a few levels down, skipping dependencies and build output.
QList<DetectedLanguage> detect(const QString &folder);

// The parsers, public for tests. Each returns an empty string when the
// text does not say.
// "17" from <maven.compiler.release>, <maven.compiler.source>, <release>
// or <java.version> ("1.8" gives "8"; ${properties} are resolved).
QString javaVersionFromPom(const QByteArray &pom);
// "17" from "17.0.2", "1.8", "java=17.0.2-tem" (.sdkmanrc)...
QString javaMajor(const QString &version);
// "20" from "v20.11.1", ">=18", "^22.1", "20"; empty for "lts/*" or "node".
QString nodeMajor(const QString &spec);
// engines.node of a package.json, as nodeMajor.
QString nodeVersionFromPackageJson(const QByteArray &json);
// "3.12" from "3.12.1", ">=3.10", "~=3.11", "3.12/envs/x" (pyenv).
QString pythonMinor(const QString &spec);
// requires-python of a pyproject.toml, as pythonMinor.
QString pythonVersionFromPyproject(const QByteArray &toml);
// "c++20" from set(CMAKE_CXX_STANDARD 20) or cxx_std_20.
QString cppStandardFromCMake(const QByteArray &cmake);

} // namespace ProjectDetector
