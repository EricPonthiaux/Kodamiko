// SPDX-License-Identifier: GPL-3.0-only

#include "ProjectDetector.h"

#include "ProjectConfig.h"
#include "system/Executables.h"

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
        const QString executable = Executables::virtualEnvironmentPython(environment.path());
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

} // namespace ProjectDetector

namespace {

// A project folder and what walking it found.
class ProjectScan {
public:
    explicit ProjectScan(const QString &folder)
        : folder_(folder)
    {
        int entries = 0;
        walk(folder, 0, scan_, entries);
    }

    // The shallowest file with this name, or an empty string.
    QString marker(const QString &name) const { return scan_.markers.value(name); }

    QString relative(const QString &path) const { return QDir(folder_).relativeFilePath(path); }

    int count(const QStringList &suffixes) const { return ::count(scan_, suffixes); }

    const QString &folder() const { return folder_; }

private:
    QString folder_;
    Scan scan_;
};

// The Java version a .java-version or .sdkmanrc file names.
QString javaVersionFromVersionFile(const QString &name, const QString &path)
{
    if (name != QLatin1String(".sdkmanrc"))
        return ProjectDetector::javaMajor(firstLine(path));
    static const QRegularExpression entry(QStringLiteral(R"(^\s*java\s*=\s*(\S+))"),
                                          QRegularExpression::MultilineOption);
    return ProjectDetector::javaMajor(entry.match(QString::fromUtf8(readFile(path))).captured(1));
}

void detectJavaVersionFiles(const ProjectScan &project, DetectedLanguage &java)
{
    for (const QString &name : {QStringLiteral(".java-version"), QStringLiteral(".sdkmanrc")}) {
        const QString path = project.marker(name);
        if (path.isEmpty() || !java.runtimeVersion.isEmpty())
            continue;
        java.runtimeVersion = javaVersionFromVersionFile(name, path);
        if (!java.runtimeVersion.isEmpty())
            java.evidence = project.relative(path);
    }
}

void detectJavaBuildFile(const ProjectScan &project, DetectedLanguage &java)
{
    const QString path = project.marker(QStringLiteral("pom.xml"));
    if (path.isEmpty())
        return;
    if (java.evidence.isEmpty())
        java.evidence = project.relative(path);
    if (java.runtimeVersion.isEmpty())
        java.runtimeVersion = ProjectDetector::javaVersionFromPom(readFile(path));
}

DetectedLanguage detectJava(const ProjectScan &project)
{
    DetectedLanguage java{QStringLiteral("java"), {}, {}, {}};
    detectJavaVersionFiles(project, java);
    detectJavaBuildFile(project, java);
    const int files = project.count({QStringLiteral("java")});
    if (java.evidence.isEmpty() && files >= minimumSources)
        java.evidence = filesEvidence(files, QStringLiteral("java"));
    return java;
}

void detectJavaScriptManifests(const ProjectScan &project, DetectedLanguage &javascript)
{
    for (const QString &name : {QStringLiteral(".nvmrc"), QStringLiteral(".node-version")}) {
        const QString path = project.marker(name);
        if (path.isEmpty() || !javascript.runtimeVersion.isEmpty())
            continue;
        javascript.runtimeVersion = ProjectDetector::nodeMajor(firstLine(path));
        javascript.evidence = project.relative(path);
    }
    if (const QString package = project.marker(QStringLiteral("package.json")); !package.isEmpty()) {
        if (javascript.runtimeVersion.isEmpty())
            javascript.runtimeVersion = ProjectDetector::nodeVersionFromPackageJson(readFile(package));
        javascript.evidence = project.relative(package);
    }
    const QString tsconfig = project.marker(QStringLiteral("tsconfig.json"));
    if (javascript.evidence.isEmpty() && !tsconfig.isEmpty())
        javascript.evidence = project.relative(tsconfig);
}

DetectedLanguage detectJavaScript(const ProjectScan &project)
{
    DetectedLanguage javascript{QStringLiteral("javascript"), {}, {}, {}};
    detectJavaScriptManifests(project, javascript);
    if (!javascript.evidence.isEmpty())
        return javascript;
    const int ts = project.count({QStringLiteral("ts"), QStringLiteral("tsx"), QStringLiteral("mts")});
    const int js = project.count({QStringLiteral("js"), QStringLiteral("jsx"), QStringLiteral("mjs")});
    if (ts + js < minimumSources)
        return javascript;
    javascript.evidence = ts >= js ? filesEvidence(ts, QStringLiteral("ts")) : filesEvidence(js, QStringLiteral("js"));
    return javascript;
}

DetectedLanguage detectCpp(const ProjectScan &project)
{
    DetectedLanguage cpp{QStringLiteral("cpp"), {}, {}, {}};
    if (const QString cmake = project.marker(QStringLiteral("CMakeLists.txt")); !cmake.isEmpty()) {
        cpp.runtimeVersion = ProjectDetector::cppStandardFromCMake(readFile(cmake));
        cpp.evidence = project.relative(cmake);
    }
    for (const QString &name : {QStringLiteral("compile_commands.json"), QStringLiteral("meson.build")}) {
        if (cpp.evidence.isEmpty() && !project.marker(name).isEmpty())
            cpp.evidence = project.relative(project.marker(name));
    }
    const int sources = project.count({QStringLiteral("cpp"), QStringLiteral("cc"), QStringLiteral("cxx"),
                                       QStringLiteral("c"), QStringLiteral("hpp"), QStringLiteral("h")});
    if (cpp.evidence.isEmpty() && sources >= minimumSources)
        cpp.evidence = QStringLiteral("%1 C/C++ files").arg(sources);
    return cpp;
}

void detectPythonManifests(const ProjectScan &project, DetectedLanguage &python)
{
    if (const QString version = project.marker(QStringLiteral(".python-version")); !version.isEmpty()) {
        if (python.runtimeVersion.isEmpty())
            python.runtimeVersion = ProjectDetector::pythonMinor(firstLine(version));
        python.evidence = project.relative(version);
    }
    if (const QString pyproject = project.marker(QStringLiteral("pyproject.toml")); !pyproject.isEmpty()) {
        if (python.runtimeVersion.isEmpty())
            python.runtimeVersion = ProjectDetector::pythonVersionFromPyproject(readFile(pyproject));
        python.evidence = project.relative(pyproject);
    }
    for (const QString &name : {QStringLiteral("setup.py"), QStringLiteral("requirements.txt")}) {
        if (python.evidence.isEmpty() && !project.marker(name).isEmpty())
            python.evidence = project.relative(project.marker(name));
    }
}

DetectedLanguage detectPython(const ProjectScan &project)
{
    DetectedLanguage python{QStringLiteral("python"), {}, {}, {}};
    detectVirtualEnvironment(project.folder(), python);
    if (!python.runtimePath.isEmpty())
        python.evidence =
            project.relative(QDir::cleanPath(QFileInfo(python.runtimePath).absolutePath() + QStringLiteral("/..")));
    detectPythonManifests(project, python);
    const int files = project.count({QStringLiteral("py")});
    if (python.evidence.isEmpty() && files >= minimumSources)
        python.evidence = filesEvidence(files, QStringLiteral("py"));
    return python;
}

} // namespace

namespace ProjectDetector {

QList<DetectedLanguage> detect(const QString &folder)
{
    const ProjectScan project(folder);
    QList<DetectedLanguage> found;
    for (const DetectedLanguage &language :
         {detectJava(project), detectJavaScript(project), detectCpp(project), detectPython(project)}) {
        if (!language.evidence.isEmpty())
            found.append(language);
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
