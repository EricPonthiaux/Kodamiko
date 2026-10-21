// SPDX-License-Identifier: GPL-3.0-only

#include "LanguageRegistry.h"

#include <QFileInfo>
#include <QHash>

namespace LanguageRegistry {
namespace {

struct LanguageFiles {
    const char *languageId = nullptr;
    QStringList extensions;
    QStringList fileNames;
};

// ".h" headers are treated as C++: the C++ grammar also reads C, but not the
// other way around.
const QList<LanguageFiles> &knownLanguages()
{
    static const QList<LanguageFiles> languages = {
        {"c", {"c"}, {}},
        {"cpp", {"cpp", "cc", "cxx", "c++", "h", "hpp", "hh", "hxx", "h++", "ipp", "tpp", "inl", "ino"}, {}},
        {"java", {"java"}, {}},
        {"javascript", {"js", "mjs", "cjs", "jsx"}, {}},
        {"typescript", {"ts", "mts", "cts"}, {}},
        {"tsx", {"tsx"}, {}},
        {"json", {"json", "jsonc"}, {}},
        {"cmake", {"cmake"}, {"CMakeLists.txt"}},
        {"markdown", {"md", "markdown"}, {}},
        {"xml", {"xml", "xsd", "xsl", "xslt", "svg", "ui", "qrc", "plist"}, {}},
        {"html", {"html", "htm"}, {}},
        {"css", {"css"}, {}},
        {"bash", {"sh", "bash", "zsh"}, {".bashrc", ".zshrc", ".profile"}},
        {"python", {"py", "pyw"}, {}},
        {"yaml", {"yml", "yaml"}, {}},
        {"makefile", {"mk", "mak"}, {"Makefile", "makefile", "GNUmakefile"}},
        {"properties", {"properties", "ini", "cfg", "conf", "toml"}, {".editorconfig", ".gitconfig"}},
        {"diff", {"diff", "patch"}, {}},
        {"batch", {"bat", "cmd"}, {}},
        {"sql", {"sql"}, {}},
    };
    return languages;
}

} // namespace

QString languageForFile(const QString &filePath)
{
    const QFileInfo info(filePath);
    const QString fileName = info.fileName();
    const QString extension = info.suffix().toLower();

    for (const LanguageFiles &language : knownLanguages()) {
        if (language.fileNames.contains(fileName))
            return QString::fromLatin1(language.languageId);
    }
    for (const LanguageFiles &language : knownLanguages()) {
        if (!extension.isEmpty() && language.extensions.contains(extension))
            return QString::fromLatin1(language.languageId);
    }
    return plainText;
}

} // namespace LanguageRegistry
