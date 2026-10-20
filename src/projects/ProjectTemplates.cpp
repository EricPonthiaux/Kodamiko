#include "ProjectTemplates.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QObject>

#include <algorithm>
#include <optional>

namespace {

// A file of a template: its path in the project and its text, where
// {{identifier}} stands for ProjectTemplates::identifierOf(the folder's name).
struct TemplateFile {
    const char *path;
    const char *text;
};

constexpr auto identifierPlaceholder = "{{identifier}}";

// The runtime versions below are ToolCatalog::defaultRuntimeVersion's (and
// Python's current one): the setup then proposes nothing to change. Each
// template has a Build, a Run and a Debug configuration in .duet/run.json
// (RunConfigurations::parse's format); Run and Debug start what Build built.

QList<TemplateFile> cppFiles()
{
    return {
        {"CMakeLists.txt", R"(cmake_minimum_required(VERSION 3.24)

project({{identifier}} LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_executable({{identifier}} src/main.cpp)
)"},
        {"src/main.cpp", R"(#include <iostream>

int main()
{
    std::cout << "Hello from {{identifier}}!\n";
}
)"},
        {".gitignore", "build/\n"},
        // A Debug build type: the debugger needs its symbols.
        {".duet/run.json", R"({
    "configurations": [
        {
            "command": "cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug && cmake --build build",
            "kind": "build",
            "name": "Build"
        },
        {
            "command": "build/{{identifier}}",
            "kind": "run",
            "name": "Run"
        },
        {
            "command": "gdb build/{{identifier}}",
            "kind": "debug",
            "name": "Debug"
        }
    ]
}
)"},
    };
}

QList<TemplateFile> javaFiles()
{
    return {
        {"pom.xml", R"(<?xml version="1.0" encoding="UTF-8"?>
<project xmlns="http://maven.apache.org/POM/4.0.0"
         xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance"
         xsi:schemaLocation="http://maven.apache.org/POM/4.0.0 https://maven.apache.org/xsd/maven-4.0.0.xsd">
    <modelVersion>4.0.0</modelVersion>

    <groupId>{{identifier}}</groupId>
    <artifactId>{{identifier}}</artifactId>
    <version>0.1.0</version>
    <packaging>jar</packaging>

    <properties>
        <maven.compiler.release>21</maven.compiler.release>
        <project.build.sourceEncoding>UTF-8</project.build.sourceEncoding>
    </properties>
</project>
)"},
        {"src/main/java/Main.java", R"(public class Main {
    public static void main(String[] args) {
        System.out.println("Hello from {{identifier}}!");
    }
}
)"},
        {".gitignore", "target/\n"},
        // Run and Debug start the compiled classes with the JDK directly:
        // no Maven plugin to configure. Debug waits for a debugger on port
        // 5005.
        {".duet/run.json", R"({
    "configurations": [
        {
            "command": "mvn package",
            "kind": "build",
            "name": "Build"
        },
        {
            "command": "java -cp target/classes Main",
            "kind": "run",
            "name": "Run"
        },
        {
            "command": "java -agentlib:jdwp=transport=dt_socket,server=y,suspend=y,address=*:5005 -cp target/classes Main",
            "kind": "debug",
            "name": "Debug"
        }
    ]
}
)"},
    };
}

QList<TemplateFile> javascriptFiles()
{
    return {
        {"package.json", R"({
  "name": "{{identifier}}",
  "version": "0.1.0",
  "private": true,
  "type": "module",
  "engines": {
    "node": ">=24"
  },
  "scripts": {
    "build": "tsc",
    "start": "node dist/index.js"
  },
  "devDependencies": {
    "typescript": "^5.9.0"
  }
}
)"},
        {"tsconfig.json", R"({
  "compilerOptions": {
    "target": "ES2023",
    "module": "NodeNext",
    "moduleResolution": "NodeNext",
    "strict": true,
    "outDir": "dist",
    "sourceMap": true,
    "rootDir": "src"
  },
  "include": ["src"]
}
)"},
        {"src/index.ts", R"(console.log("Hello from {{identifier}}!");
)"},
        {".gitignore", "node_modules/\ndist/\n"},
        // --inspect-brk: the program waits for a debugger (Chrome's
        // chrome://inspect) before its first line.
        {".duet/run.json", R"({
    "configurations": [
        {
            "command": "npm install && npm run build",
            "kind": "build",
            "name": "Build"
        },
        {
            "command": "npm start",
            "kind": "run",
            "name": "Run"
        },
        {
            "command": "node --inspect-brk --enable-source-maps dist/index.js",
            "kind": "debug",
            "name": "Debug"
        }
    ]
}
)"},
    };
}

QList<TemplateFile> pythonFiles()
{
    return {
        {"pyproject.toml", R"([project]
name = "{{identifier}}"
version = "0.1.0"
requires-python = ">=3.12"

[project.scripts]
{{identifier}} = "{{identifier}}.__main__:main"

[build-system]
requires = ["hatchling"]
build-backend = "hatchling.build"
)"},
        {"src/{{identifier}}/__init__.py", ""},
        {"src/{{identifier}}/__main__.py", R"(def main() -> None:
    print("Hello from {{identifier}}!")


if __name__ == "__main__":
    main()
)"},
        {".gitignore", "__pycache__/\n.venv/\ndist/\n"},
        // Nothing to compile: Build checks the syntax of every module.
        {".duet/run.json", R"({
    "configurations": [
        {
            "command": "python -m compileall -q src",
            "kind": "build",
            "name": "Build"
        },
        {
            "command": "python -m {{identifier}}",
            "folder": "src",
            "kind": "run",
            "name": "Run"
        },
        {
            "command": "python -m pdb -m {{identifier}}",
            "folder": "src",
            "kind": "debug",
            "name": "Debug"
        }
    ]
}
)"},
    };
}

// The files of the template `id`; nullopt for an unknown one.
std::optional<QList<TemplateFile>> filesOf(const QString &id)
{
    if (id == QLatin1String("empty"))
        return QList<TemplateFile>{};
    if (id == QLatin1String("cpp"))
        return cppFiles();
    if (id == QLatin1String("java"))
        return javaFiles();
    if (id == QLatin1String("javascript"))
        return javascriptFiles();
    if (id == QLatin1String("python"))
        return pythonFiles();
    return std::nullopt;
}

bool writeFile(const QString &path, const QString &text, QString *error)
{
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) {
        *error = QObject::tr("Cannot create the folder %1").arg(QDir::toNativeSeparators(QFileInfo(path).path()));
        return false;
    }
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::NewOnly)) {
        *error = QObject::tr("Cannot create %1: %2").arg(QDir::toNativeSeparators(path), file.errorString());
        return false;
    }
    file.write(text.toUtf8());
    return true;
}

} // namespace

namespace ProjectTemplates {

QList<ProjectTemplate> all()
{
    return {
        {QStringLiteral("empty"), QObject::tr("Empty Folder"), QObject::tr("No files: the languages are chosen next.")},
        {QStringLiteral("cpp"), QObject::tr("C++ · CMake"),
         QObject::tr("CMakeLists.txt (C++20) and src/main.cpp, an executable.")},
        {QStringLiteral("java"), QObject::tr("Java · Maven"),
         QObject::tr("pom.xml (JDK 21) and src/main/java/Main.java.")},
        {QStringLiteral("javascript"), QObject::tr("TypeScript · Node.js"),
         QObject::tr("package.json (Node.js 24), tsconfig.json and src/index.ts.")},
        {QStringLiteral("python"), QObject::tr("Python · pyproject"),
         QObject::tr("pyproject.toml (Python 3.12) and a package in src/.")},
    };
}

bool isFreeFolder(const QString &folder)
{
    const QFileInfo info(folder);
    if (!info.exists())
        return true;
    return info.isDir() && QDir(folder).isEmpty(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System);
}

QString identifierOf(const QString &name)
{
    QString identifier;
    for (const QChar c : name.toLower()) {
        const bool isKept = c.unicode() < 128 && c.isLetterOrNumber();
        if (isKept)
            identifier.append(c);
        else if (!identifier.endsWith(QLatin1Char('_')))
            identifier.append(QLatin1Char('_'));
    }
    while (identifier.startsWith(QLatin1Char('_')))
        identifier.remove(0, 1);
    while (identifier.endsWith(QLatin1Char('_')))
        identifier.chop(1);
    if (identifier.isEmpty())
        return QStringLiteral("project");
    // npm refuses a name that starts with "_", Python one with a digit.
    if (identifier.front().isDigit())
        identifier.prepend(QStringLiteral("project_"));
    return identifier;
}

bool create(const QString &id, const QString &folder, QString *error)
{
    const std::optional<QList<TemplateFile>> files = filesOf(id);
    if (!files) {
        *error = QObject::tr("Unknown project template \"%1\"").arg(id);
        return false;
    }
    if (!isFreeFolder(folder)) {
        *error = QObject::tr("%1 already exists and is not an empty folder").arg(QDir::toNativeSeparators(folder));
        return false;
    }
    if (!QDir().mkpath(folder)) {
        *error = QObject::tr("Cannot create the folder %1").arg(QDir::toNativeSeparators(folder));
        return false;
    }
    const QString identifier = identifierOf(QFileInfo(folder).fileName());
    const auto filled = [&identifier](const char *text) {
        return QString::fromUtf8(text).replace(QLatin1String(identifierPlaceholder), identifier);
    };
    return std::ranges::all_of(*files, [&](const TemplateFile &file) {
        return writeFile(QDir(folder).filePath(filled(file.path)), filled(file.text), error);
    });
}

} // namespace ProjectTemplates
