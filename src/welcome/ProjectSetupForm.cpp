#include "ProjectSetupForm.h"

#include "toolchains/ToolCatalog.h"
#include "toolchains/Toolchains.h"
#include "ui/PopupWidgets.h"

#include <QComboBox>
#include <QDir>
#include <QLabel>
#include <QSignalBlocker>
#include <QVBoxLayout>
#include <QVersionNumber>

#include <algorithm>

namespace {

const QString versionKey = QStringLiteral("version");
const QString pathKey = QStringLiteral("path");

QVariant item(const QString &version, const QString &path = {})
{
    return QVariantMap{{versionKey, version}, {pathKey, path}};
}

QString runtimeLabel(const QString &family)
{
    if (family == QLatin1String("java"))
        return QObject::tr("JDK");
    if (family == QLatin1String("javascript"))
        return QObject::tr("Node.js");
    if (family == QLatin1String("python"))
        return QObject::tr("Python interpreter");
    return QObject::tr("C++ standard");
}

QString runtimeDescription(const QString &family)
{
    if (family == QLatin1String("java"))
        return QObject::tr("The Java the project is built with (JAVA_HOME of Maven and Gradle).");
    if (family == QLatin1String("javascript"))
        return QObject::tr("The Node.js the project runs on.");
    if (family == QLatin1String("python"))
        return QObject::tr("Its packages are the ones completed. Duet does not download Python.");
    return QObject::tr("For the files compile_commands.json does not cover.");
}

QString serverTitle(const QString &id)
{
    if (id.isEmpty())
        return QObject::tr("None (words of the project only)");
    const ToolSpec *spec = ToolCatalog::find(id);
    return spec ? spec->title : id;
}

} // namespace

ProjectSetupForm::ProjectSetupForm(Toolchains *toolchains, QWidget *parent)
    : QWidget(parent)
    , toolchains_(toolchains)
    , layout_(new QVBoxLayout(this))
{
    layout_->setContentsMargins(0, 0, 0, 0);
    layout_->setSpacing(8);
    for (const QString &family : ProjectLanguages::families()) {
        Card card;
        card.family = family;
        build(card);
        cards_.append(card);
    }
    layout_->addStretch();
}

void ProjectSetupForm::build(Card &card)
{
    layout_->addWidget(sectionHeading(ProjectLanguages::title(card.family)));
    auto *settings = new SettingsCard(this);
    card.enabled = new ToggleSwitch(this);
    card.runtime = new QComboBox(this);
    card.server = new QComboBox(this);
    card.serverVersion = new QComboBox(this);
    for (QComboBox *combo : {card.runtime, card.server, card.serverVersion}) {
        combo->setMinimumWidth(300);
        combo->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    }
    // The description of the first row says why it was found (set by fill).
    QWidget *enabledRow =
        settings->addRow(tr("Use %1").arg(ProjectLanguages::title(card.family)), QStringLiteral(" "), card.enabled);
    card.evidence = enabledRow->findChild<QLabel *>(QStringLiteral("settingsDescription"));
    card.rows = {settings->addRow(runtimeLabel(card.family), runtimeDescription(card.family), card.runtime),
                 settings->addRow(tr("Language server"), tr("Completes members, imports, signatures."), card.server),
                 settings->addRow(tr("Server version"), tr("Downloaded by Duet, or the one in your PATH."),
                                  card.serverVersion)};
    layout_->addWidget(settings);

    const QString family = card.family;
    const auto cardOf = [this, family]() -> Card & {
        return *std::find_if(cards_.begin(), cards_.end(), [&](const Card &c) { return c.family == family; });
    };
    connect(card.enabled, &QAbstractButton::toggled, this, [this, cardOf] {
        updateRows(cardOf());
        emit changed();
    });
    connect(card.runtime, &QComboBox::currentIndexChanged, this, &ProjectSetupForm::changed);
    connect(card.server, &QComboBox::currentIndexChanged, this, [this, cardOf] {
        Card &card = cardOf();
        LanguageSetup setup = setupOf(card);
        setup.serverVersion.clear();
        toolchains_->complete(setup);
        fillServerVersions(card, setup.server, setup.serverVersion);
        updateRows(card);
        emit changed();
    });
    connect(card.serverVersion, &QComboBox::currentIndexChanged, this, &ProjectSetupForm::changed);
}

ProjectConfig ProjectSetupForm::detectedProject(const QString &folder, const Toolchains &toolchains,
                                                QList<DetectedLanguage> *detected)
{
    ProjectConfig project;
    project.path = QDir(folder).absolutePath();
    project.name = QFileInfo(project.path).fileName();
    const QList<DetectedLanguage> found = ProjectDetector::detect(project.path);
    for (const DetectedLanguage &language : found) {
        LanguageSetup setup;
        setup.language = language.language;
        setup.runtimeVersion = language.runtimeVersion;
        setup.runtimePath = language.runtimePath;
        toolchains.complete(setup);
        project.languages.append(setup);
    }
    if (detected)
        *detected = found;
    return project;
}

void ProjectSetupForm::setProject(const ProjectConfig &project, const QList<DetectedLanguage> &detected)
{
    project_ = project;
    for (Card &card : cards_) {
        LanguageSetup setup;
        setup.language = card.family;
        setup.enabled = false;
        if (const LanguageSetup *saved = project.language(card.family))
            setup = *saved;
        toolchains_->complete(setup);
        QString evidence = tr("Not found in the project.");
        for (const DetectedLanguage &language : detected) {
            if (language.language == card.family)
                evidence = tr("Found: %1").arg(language.evidence);
        }
        if (!project.language(card.family) && detected.isEmpty() && project.configured)
            evidence = tr("Not used by the project.");
        fill(card, setup, evidence);
    }
    emit changed();
}

void ProjectSetupForm::detectAgain()
{
    QList<DetectedLanguage> detected;
    ProjectConfig project = detectedProject(project_.path, *toolchains_, &detected);
    project.lastOpened = project_.lastOpened;
    setProject(project, detected);
}

void ProjectSetupForm::fill(Card &card, const LanguageSetup &setup, const QString &evidence)
{
    // The switch is not blocked: it draws its knob from its own signal.
    const QSignalBlocker serverBlocker(card.server);
    card.evidence->setText(evidence);
    card.enabled->setChecked(setup.enabled);

    fillRuntimes(card, setup);

    card.server->clear();
    for (const QString &id : ToolCatalog::servers(card.family))
        card.server->addItem(serverTitle(id), id);
    card.server->addItem(serverTitle({}), QString());
    card.server->setCurrentIndex(std::max(0, card.server->findData(setup.server)));
    fillServerVersions(card, setup.server, setup.serverVersion);
    updateRows(card);
}

void ProjectSetupForm::fillRuntimes(Card &card, const LanguageSetup &setup)
{
    const QSignalBlocker blocker(card.runtime);
    card.runtime->clear();
    int selected = -1;
    if (card.family == QLatin1String("cpp")) {
        for (const QString &standard : ToolCatalog::runtimeVersions(card.family))
            card.runtime->addItem(QString(standard).replace(QLatin1String("c++"), QLatin1String("C++")), item(standard));
        selected = card.runtime->findData(item(setup.runtimeVersion));
    } else if (card.family == QLatin1String("python")) {
        QList<RuntimeInstall> pythons = toolchains_->runtimes(QStringLiteral("python"));
        const bool listed = std::any_of(pythons.cbegin(), pythons.cend(), [&](const RuntimeInstall &python) {
            return python.executable == setup.runtimePath;
        });
        if (!setup.runtimePath.isEmpty() && !listed)
            pythons.prepend({QStringLiteral("python"), setup.runtimeVersion, QFileInfo(setup.runtimePath).absolutePath(),
                             setup.runtimePath, false});
        for (const RuntimeInstall &python : std::as_const(pythons)) {
            QString where = QDir::toNativeSeparators(python.executable);
            if (!project_.path.isEmpty() && python.executable.startsWith(project_.path + QLatin1Char('/')))
                where = tr("the project's %1").arg(QDir(project_.path).relativeFilePath(python.executable));
            card.runtime->addItem(tr("Python %1 — %2").arg(python.version.isEmpty() ? tr("?") : python.version, where),
                                  item(python.version, python.executable));
        }
        if (pythons.isEmpty())
            card.runtime->addItem(tr("No Python found on this machine"), item(setup.runtimeVersion));
        selected = card.runtime->findData(item(setup.runtimeVersion, setup.runtimePath));
        if (selected < 0) {
            for (int i = 0; i < card.runtime->count(); ++i) {
                if (card.runtime->itemData(i).toMap().value(versionKey) == setup.runtimeVersion) {
                    selected = i;
                    break;
                }
            }
        }
    } else {
        const QString tool = ToolCatalog::runtimeOf(card.family);
        const ToolSpec *spec = ToolCatalog::find(tool);
        QStringList versions = ToolCatalog::runtimeVersions(card.family);
        for (const RuntimeInstall &runtime : toolchains_->runtimes(tool)) {
            if (!versions.contains(runtime.version))
                versions.append(runtime.version);
        }
        if (!setup.runtimeVersion.isEmpty() && !versions.contains(setup.runtimeVersion))
            versions.append(setup.runtimeVersion);
        std::sort(versions.begin(), versions.end(),
                  [](const QString &a, const QString &b) { return a.toInt() > b.toInt(); });
        for (const QString &version : std::as_const(versions)) {
            const std::optional<RuntimeInstall> found = toolchains_->runtime(tool, version);
            const QString state = !found       ? tr("will download")
                                  : found->managed ? tr("downloaded")
                                                   : tr("on this machine");
            card.runtime->addItem(tr("%1 %2 — %3").arg(spec ? spec->title : tool, version, state), item(version));
        }
        selected = card.runtime->findData(item(setup.runtimeVersion));
    }
    card.runtime->setCurrentIndex(std::max(0, selected));
}

void ProjectSetupForm::fillServerVersions(Card &card, const QString &server, const QString &selected)
{
    const QSignalBlocker blocker(card.serverVersion);
    card.serverVersion->clear();
    if (server.isEmpty())
        return;
    const QString system = toolchains_->systemProgram(server);
    card.serverVersion->addItem(system.isEmpty() ? tr("From the PATH — not found")
                                                 : tr("From the PATH — %1").arg(QDir::toNativeSeparators(system)),
                                QLatin1String(ProjectLanguages::systemVersion));
    QStringList versions;
    if (const ToolSpec *spec = ToolCatalog::find(server))
        versions = spec->versions;
    for (const QString &version : toolchains_->installedVersions(server)) {
        if (!versions.contains(version))
            versions.append(version);
    }
    std::sort(versions.begin(), versions.end(), [](const QString &a, const QString &b) {
        return QVersionNumber::fromString(a) > QVersionNumber::fromString(b);
    });
    for (const QString &version : std::as_const(versions)) {
        const QString state = toolchains_->isInstalled(server, version) ? tr("downloaded") : tr("will download");
        card.serverVersion->addItem(tr("%1 — %2").arg(version, state), version);
    }
    card.serverVersion->setCurrentIndex(std::max(0, card.serverVersion->findData(selected)));
}

void ProjectSetupForm::updateRows(Card &card)
{
    const bool enabled = card.enabled->isChecked();
    for (QWidget *row : std::as_const(card.rows))
        row->setVisible(enabled);
    // No server: no version to choose.
    if (enabled)
        card.rows.last()->setVisible(!card.server->currentData().toString().isEmpty());
}

LanguageSetup ProjectSetupForm::setupOf(const Card &card) const
{
    LanguageSetup setup;
    setup.language = card.family;
    setup.enabled = card.enabled->isChecked();
    const QVariantMap runtime = card.runtime->currentData().toMap();
    setup.runtimeVersion = runtime.value(versionKey).toString();
    setup.runtimePath = runtime.value(pathKey).toString();
    setup.server = card.server->currentData().toString();
    setup.serverVersion = setup.server.isEmpty() ? QString() : card.serverVersion->currentData().toString();
    return setup;
}

ProjectConfig ProjectSetupForm::project() const
{
    ProjectConfig project = project_;
    project.configured = true;
    project.languages.clear();
    for (const Card &card : cards_) {
        // The languages the project does not use are not kept.
        const LanguageSetup setup = setupOf(card);
        if (setup.enabled || project_.language(card.family))
            project.languages.append(setup);
    }
    return project;
}
