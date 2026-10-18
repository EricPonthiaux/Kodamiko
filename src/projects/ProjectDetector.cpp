#include "ProjectDetector.h"

#include "ProjectConfig.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

namespace {

// Folders that hold dependencies, build output or tools' data, not the
// project's own sources.
bool skipped(const QString &name)
{
    static const QStringList names{
        QStringLiteral("node_modules"), QStringLiteral("target"),      QStringLiteral("out"),
        QStringLiteral("dist"),         QStringLiteral("venv"),        QStringLiteral("__pycache__"),
        QStringLiteral("vendor"),       QStringLiteral("third_party"), QStringLiteral("_deps"),
        QStringLiteral("bin"),          QStringLiteral("obj")};
    return name.startsWith(QLatin1Char('.')) || name.startsWith(QLatin1String("build")) ||
           names.contains(name, Qt::CaseInsensitive);
}

// Files deeper than this are not looked at.
constexpr int maximumDepth = 4;
// Build files are looked for this deep (a backend/ or app/ folder).
constexpr int markerDepth = 1;
constexpr int maximumEntries = 20000;

struct Scan {
    QHash<QString, QString> markers; // file name → its shallowest path
    QHash<QString, int> extensions;  // suffix → count
};

void walk(const QString &folder, int depth, Scan &scan, int &entries)
{
    const QFileInfoList children =
        QDir(folder).entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden, QDir::Name);
    QFileInfoList folders;
    for (const QFileInfo &child : children) {
        if (++entries > maximumEntries)
            return;
        if (child.isDir()) {
            if (!skipped(child.fileName()) && !child.isSymLink())
                folders.append(child);
            continue;
        }
        if (depth <= markerDepth && !scan.markers.contains(child.fileName()))
            scan.markers.insert(child.fileName(), child.filePath());
        ++scan.extensions[child.suffix().toLower()];
    }
    if (depth >= maximumDepth)
        return;
    for (const QFileInfo &child : folders)
        walk(child.filePath(), depth + 1, scan, entries);
}

QByteArray readFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return file.read(1 << 20);
}

QString firstLine(const QString &path)
{
    return QString::fromUtf8(readFile(path)).section(QLatin1Char('\n'), 0, 0).trimmed();
}

int count(const Scan &scan, const QStringList &suffixes)
{
    int total = 0;
    for (const QString &suffix : suffixes)
        total += scan.extensions.value(suffix);
    return total;
}

// Enough of a language's files to be more than a stray script.
constexpr int minimumSources = 3;

QString filesEvidence(int files, const QString &suffix)
{
    return QStringLiteral("%1 .%2 files").arg(files).arg(suffix);
}

// The project's own Python environment, and its version.
void detectVirtualEnvironment(const QString &folder, DetectedLanguage &python)
{
    for (const QString &name : {QStringLiteral(".venv"), QStringLiteral("venv"), QStringLiteral("env")}) {
        const QDir environment(QDir(folder).filePath(name));
        if (!environment.exists(QStringLiteral("pyvenv.cfg")))
            continue;
#ifdef Q_OS_WIN
        const QString executable = environment.filePath(QStringLiteral("Scripts/python.exe"));
#else
        const QString executable = environment.filePath(QStringLiteral("bin/python"));
#endif
        if (!QFileInfo::exists(executable))
            continue;
        python.runtimePath = executable;
        static const QRegularExpression version(QStringLiteral(R"(^\s*version(?:_info)?\s*=\s*([0-9.]+))"),
                                                QRegularExpression::MultilineOption);
        const QRegularExpressionMatch match =
            version.match(QString::fromUtf8(readFile(environment.filePath(QStringLiteral("pyvenv.cfg")))));
        if (match.hasMatch())
            python.runtimeVersion = ProjectDetector::pythonMinor(match.captured(1));
        return;
    }
}

} // namespace

namespace ProjectDetector {

QString javaMajor(const QString &version)
{
    static const QRegularExpression number(QStringLiteral(R"((\d+)(?:\.(\d+))?)"));
    const QRegularExpressionMatch match = number.match(version);
    if (!match.hasMatch())
        return {};
    // 1.8 is Java 8.
    if (match.captured(1) == QLatin1String("1") && !match.captured(2).isEmpty())
        return match.captured(2);
    return match.captured(1);
}

QString javaVersionFromPom(const QByteArray &pom)
{
    const QString text = QString::fromUtf8(pom);
    const auto tag = [&text](const QString &name) {
        const QRegularExpression expression(
            QStringLiteral("<%1>\\s*([^<\\s]+)\\s*</%1>").arg(QRegularExpression::escape(name)));
        return expression.match(text).captured(1);
    };
    for (const QString &name : {QStringLiteral("maven.compiler.release"), QStringLiteral("maven.compiler.source"),
                                QStringLiteral("release"), QStringLiteral("java.version"), QStringLiteral("source")}) {
        QString value = tag(name);
        // A property: ${java.version}.
        for (int i = 0; i < 3 && value.startsWith(QLatin1String("${")) && value.endsWith(QLatin1Char('}')); ++i)
            value = tag(value.mid(2, value.size() - 3));
        const QString major = javaMajor(value);
        if (!major.isEmpty() && !value.startsWith(QLatin1String("${")))
            return major;
    }
    return {};
}

QString javaVersionFromGradle(const QByteArray &script)
{
    const QString text = QString::fromUtf8(script);
    static const QList<QRegularExpression> expressions{
        QRegularExpression(QStringLiteral(R"(JavaLanguageVersion\s*\.\s*of\s*\(\s*['"]?(\d+))")),
        QRegularExpression(QStringLiteral(R"(jvmToolchain\s*\(?\s*(\d+))")),
        QRegularExpression(
            QStringLiteral(R"((?:source|target)Compatibility\s*=?\s*JavaVersion\s*\.\s*VERSION_(\d+(?:_\d+)?))")),
        QRegularExpression(QStringLiteral(R"((?:source|target)Compatibility\s*=?\s*['"]?(\d+(?:\.\d+)?))")),
    };
    for (const QRegularExpression &expression : expressions) {
        const QRegularExpressionMatch match = expression.match(text);
        if (match.hasMatch())
            return javaMajor(match.captured(1).replace(QLatin1Char('_'), QLatin1Char('.')));
    }
    return {};
}

QString nodeMajor(const QString &spec)
{
    static const QRegularExpression number(QStringLiteral(R"(^\s*(?:[v=^~><]|>=|<=)*\s*(\d+))"));
    return number.match(spec).captured(1);
}

QString nodeVersionFromPackageJson(const QByteArray &json)
{
    return nodeMajor(QJsonDocument::fromJson(json).object().value("engines").toObject().value("node").toString());
}

QString pythonMinor(const QString &spec)
{
    static const QRegularExpression number(QStringLiteral(R"((\d+)\.(\d+))"));
    const QRegularExpressionMatch match = number.match(spec);
    return match.hasMatch() ? match.captured(1) + QLatin1Char('.') + match.captured(2) : QString();
}

QString pythonVersionFromPyproject(const QByteArray &toml)
{
    static const QRegularExpression requiresPython(QStringLiteral(R"(requires-python\s*=\s*["']([^"']+)["'])"));
    return pythonMinor(requiresPython.match(QString::fromUtf8(toml)).captured(1));
}

QString cppStandardFromCMake(const QByteArray &cmake)
{
    static const QList<QRegularExpression> expressions{
        QRegularExpression(QStringLiteral(R"(CMAKE_CXX_STANDARD\s+(\d+))")),
        QRegularExpression(QStringLiteral(R"(cxx_std_(\d+))")),
    };
    for (const QRegularExpression &expression : expressions) {
        const QRegularExpressionMatch match = expression.match(QString::fromUtf8(cmake));
        if (match.hasMatch())
            return QStringLiteral("c++") + match.captured(1);
    }
    return {};
}

QList<DetectedLanguage> detect(const QString &folder)
{
    Scan scan;
    int entries = 0;
    walk(folder, 0, scan, entries);
    const auto marker = [&scan](const QString &name) { return scan.markers.value(name); };
    const auto relative = [&folder](const QString &path) { return QDir(folder).relativeFilePath(path); };

    QList<DetectedLanguage> found;

    // Java
    {
        DetectedLanguage java{QStringLiteral("java"), {}, {}, {}};
        for (const QString &name : {QStringLiteral(".java-version"), QStringLiteral(".sdkmanrc")}) {
            if (!marker(name).isEmpty() && java.runtimeVersion.isEmpty()) {
                QString line = firstLine(marker(name));
                if (name == QLatin1String(".sdkmanrc")) {
                    static const QRegularExpression entry(QStringLiteral(R"(^\s*java\s*=\s*(\S+))"),
                                                          QRegularExpression::MultilineOption);
                    line = entry.match(QString::fromUtf8(readFile(marker(name)))).captured(1);
                }
                java.runtimeVersion = javaMajor(line);
                if (!java.runtimeVersion.isEmpty())
                    java.evidence = relative(marker(name));
            }
        }
        for (const QString &name :
             {QStringLiteral("pom.xml"), QStringLiteral("build.gradle.kts"), QStringLiteral("build.gradle")}) {
            const QString path = marker(name);
            if (path.isEmpty())
                continue;
            if (java.evidence.isEmpty())
                java.evidence = relative(path);
            if (java.runtimeVersion.isEmpty()) {
                const QByteArray text = readFile(path);
                java.runtimeVersion =
                    name == QLatin1String("pom.xml") ? javaVersionFromPom(text) : javaVersionFromGradle(text);
            }
        }
        const int files = count(scan, {QStringLiteral("java")});
        if (java.evidence.isEmpty() && files >= minimumSources)
            java.evidence = filesEvidence(files, QStringLiteral("java"));
        if (!java.evidence.isEmpty())
            found.append(java);
    }

    // JavaScript / TypeScript
    {
        DetectedLanguage javascript{QStringLiteral("javascript"), {}, {}, {}};
        for (const QString &name : {QStringLiteral(".nvmrc"), QStringLiteral(".node-version")}) {
            if (!marker(name).isEmpty() && javascript.runtimeVersion.isEmpty()) {
                javascript.runtimeVersion = nodeMajor(firstLine(marker(name)));
                javascript.evidence = relative(marker(name));
            }
        }
        if (const QString package = marker(QStringLiteral("package.json")); !package.isEmpty()) {
            if (javascript.runtimeVersion.isEmpty())
                javascript.runtimeVersion = nodeVersionFromPackageJson(readFile(package));
            javascript.evidence = relative(package);
        }
        if (javascript.evidence.isEmpty() && !marker(QStringLiteral("tsconfig.json")).isEmpty())
            javascript.evidence = relative(marker(QStringLiteral("tsconfig.json")));
        const int ts = count(scan, {QStringLiteral("ts"), QStringLiteral("tsx"), QStringLiteral("mts")});
        const int js = count(scan, {QStringLiteral("js"), QStringLiteral("jsx"), QStringLiteral("mjs")});
        if (javascript.evidence.isEmpty() && ts + js >= minimumSources)
            javascript.evidence =
                ts >= js ? filesEvidence(ts, QStringLiteral("ts")) : filesEvidence(js, QStringLiteral("js"));
        if (!javascript.evidence.isEmpty())
            found.append(javascript);
    }

    // C / C++
    {
        DetectedLanguage cpp{QStringLiteral("cpp"), {}, {}, {}};
        if (const QString cmake = marker(QStringLiteral("CMakeLists.txt")); !cmake.isEmpty()) {
            cpp.runtimeVersion = cppStandardFromCMake(readFile(cmake));
            cpp.evidence = relative(cmake);
        }
        for (const QString &name : {QStringLiteral("compile_commands.json"), QStringLiteral("meson.build")}) {
            if (cpp.evidence.isEmpty() && !marker(name).isEmpty())
                cpp.evidence = relative(marker(name));
        }
        const int sources = count(scan, {QStringLiteral("cpp"), QStringLiteral("cc"), QStringLiteral("cxx"),
                                         QStringLiteral("c"), QStringLiteral("hpp"), QStringLiteral("h")});
        if (cpp.evidence.isEmpty() && sources >= minimumSources)
            cpp.evidence = QStringLiteral("%1 C/C++ files").arg(sources);
        if (!cpp.evidence.isEmpty())
            found.append(cpp);
    }

    // Python
    {
        DetectedLanguage python{QStringLiteral("python"), {}, {}, {}};
        detectVirtualEnvironment(folder, python);
        if (!python.runtimePath.isEmpty())
            python.evidence =
                relative(QDir::cleanPath(QFileInfo(python.runtimePath).absolutePath() + QStringLiteral("/..")));
        if (const QString version = marker(QStringLiteral(".python-version")); !version.isEmpty()) {
            if (python.runtimeVersion.isEmpty())
                python.runtimeVersion = pythonMinor(firstLine(version));
            python.evidence = relative(version);
        }
        if (const QString pyproject = marker(QStringLiteral("pyproject.toml")); !pyproject.isEmpty()) {
            if (python.runtimeVersion.isEmpty())
                python.runtimeVersion = pythonVersionFromPyproject(readFile(pyproject));
            python.evidence = relative(pyproject);
        }
        for (const QString &name : {QStringLiteral("setup.py"), QStringLiteral("requirements.txt")}) {
            if (python.evidence.isEmpty() && !marker(name).isEmpty())
                python.evidence = relative(marker(name));
        }
        const int files = count(scan, {QStringLiteral("py")});
        if (python.evidence.isEmpty() && files >= minimumSources)
            python.evidence = filesEvidence(files, QStringLiteral("py"));
        if (!python.evidence.isEmpty())
            found.append(python);
    }

    // In the order of the families.
    QList<DetectedLanguage> ordered;
    for (const QString &family : ProjectLanguages::families()) {
        for (const DetectedLanguage &language : std::as_const(found)) {
            if (language.language == family)
                ordered.append(language);
        }
    }
    return ordered;
}

} // namespace ProjectDetector
