// SPDX-License-Identifier: GPL-3.0-only

#include "NewProjectForm.h"

#include "agent/vendor/claude/ClaudeTrust.h"
#include "git/GitOperations.h"
#include "projects/ProjectStore.h"
#include "projects/ProjectTemplates.h"
#include "ui/PopupWidgets.h"
#include "workspace/ChoicePopup.h"

#include <QCheckBox>
#include <QDir>
#include <QEvent>
#include <QFileDialog>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSettings>
#include <QVBoxLayout>

namespace {

constexpr int templateRole = Qt::UserRole;
constexpr int descriptionRole = Qt::UserRole + 1;

// The choices of the last project created, proposed for the next one.
constexpr auto locationKey = "newProject/location";
constexpr auto templateKey = "newProject/template";
constexpr auto gitKey = "newProject/git";

QLabel *fieldLabel(const QString &text, QWidget *parent)
{
    auto *label = new QLabel(text, parent);
    label->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    return label;
}

} // namespace

NewProjectForm::NewProjectForm(QWidget *parent)
    : QWidget(parent)
    , header_(new PopupHeader(QStringLiteral("plus"), tr("New Project"), this))
    , name_(new QLineEdit(this))
    , location_(new QLineEdit(this))
    , templates_(new QListWidget(this))
    , description_(new QLabel(this))
    , git_(new QCheckBox(tr("Create a Git repository"), this))
{
    connect(header_, &PopupHeader::closeRequested, this, &NewProjectForm::cancelled);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(header_);
    layout->addWidget(createBody(), 1);
    layout->addWidget(createFooter());
    reset();
}

QWidget *NewProjectForm::createBody()
{
    auto *body = new QWidget(this);
    body->setObjectName(QStringLiteral("pageContent"));

    name_->setPlaceholderText(tr("my-project"));
    location_->setText(QSettings().value(QLatin1String(locationKey), QDir::homePath()).toString());
    auto *browse = new QPushButton(tr("Browse…"), body);
    connect(browse, &QPushButton::clicked, this, &NewProjectForm::browse);

    templates_->setObjectName(QStringLiteral("projectTemplates"));
    templates_->installEventFilter(this);
    const QString lastTemplate = QSettings().value(QLatin1String(templateKey)).toString();
    for (const ProjectTemplate &projectTemplate : ProjectTemplates::all()) {
        auto *item = new QListWidgetItem(projectTemplate.title, templates_);
        item->setData(templateRole, projectTemplate.id);
        item->setData(descriptionRole, projectTemplate.description);
        if (projectTemplate.id == lastTemplate || !templates_->currentItem())
            templates_->setCurrentItem(item);
    }
    description_->setObjectName(QStringLiteral("settingState"));
    description_->setWordWrap(true);
    git_->setChecked(QSettings().value(QLatin1String(gitKey), true).toBool());

    for (const QLineEdit *field : {name_, location_})
        connect(field, &QLineEdit::textChanged, this, &NewProjectForm::updateState);
    connect(templates_, &QListWidget::currentItemChanged, this, &NewProjectForm::updateState);
    connect(name_, &QLineEdit::returnPressed, this, &NewProjectForm::create);

    auto *grid = new QGridLayout(body);
    grid->setContentsMargins(24, 20, 24, 20);
    grid->setHorizontalSpacing(12);
    grid->setVerticalSpacing(10);
    grid->addWidget(fieldLabel(tr("Name"), body), 0, 0);
    grid->addWidget(name_, 0, 1, 1, 2);
    grid->addWidget(fieldLabel(tr("Location"), body), 1, 0);
    grid->addWidget(location_, 1, 1);
    grid->addWidget(browse, 1, 2);
    grid->addWidget(fieldLabel(tr("Start with"), body), 2, 0, Qt::AlignTop);
    grid->addWidget(templates_, 2, 1, 1, 2);
    grid->addWidget(description_, 3, 1, 1, 2);
    grid->addWidget(git_, 4, 1, 1, 2);
    grid->setRowStretch(2, 1);
    grid->setColumnStretch(1, 1);
    return body;
}

QWidget *NewProjectForm::createFooter()
{
    auto *footer = new QFrame(this);
    footer->setObjectName(QStringLiteral("popupFooter"));
    summary_ = new QLabel(footer);
    summary_->setObjectName(QStringLiteral("footerHint"));
    summary_->setWordWrap(true);
    auto *cancel = new QPushButton(tr("Cancel"), footer);
    create_ = new QPushButton(tr("Create"), footer);
    create_->setDefault(true);
    connect(cancel, &QPushButton::clicked, this, &NewProjectForm::cancelled);
    connect(create_, &QPushButton::clicked, this, &NewProjectForm::create);

    auto *layout = new QHBoxLayout(footer);
    layout->setContentsMargins(16, 10, 16, 10);
    layout->setSpacing(8);
    layout->addWidget(summary_, 1);
    for (QPushButton *button : {cancel, create_}) {
        button->setMinimumWidth(84);
        layout->addWidget(button);
    }
    return footer;
}

void NewProjectForm::reset()
{
    name_->clear();
    updateState();
    name_->setFocus();
}

void NewProjectForm::applyTheme(const Theme &theme)
{
    header_->applyTheme(theme);
}

bool NewProjectForm::eventFilter(QObject *watched, QEvent *event)
{
    // The list keeps the item positions of its last layout: after a style
    // sheet change (the padding of its items), the items would be painted
    // at their new height over one another. Queued: the list hears the
    // change before the new rules reach its items' size.
    if (watched == templates_ && event->type() == QEvent::StyleChange)
        QMetaObject::invokeMethod(templates_, &QListWidget::doItemsLayout, Qt::QueuedConnection);
    return QWidget::eventFilter(watched, event);
}

void NewProjectForm::browse()
{
    const QString folder =
        QFileDialog::getExistingDirectory(this, tr("Location of the New Project"), location_->text());
    if (!folder.isEmpty())
        location_->setText(QDir::toNativeSeparators(folder));
}

QString NewProjectForm::targetFolder() const
{
    const QString location = QDir::fromNativeSeparators(location_->text().trimmed());
    return QDir::cleanPath(QDir(location).filePath(name_->text().trimmed()));
}

QString NewProjectForm::problem() const
{
    const QString name = name_->text().trimmed();
    if (name.isEmpty())
        return tr("Name the project.");
    const bool isPathLike = name.contains(QLatin1Char('/')) || name.contains(QLatin1Char('\\'));
    if (isPathLike || name == QLatin1String(".") || name == QLatin1String(".."))
        return tr("A project name cannot contain / or \\.");
    if (!QDir::isAbsolutePath(QDir::fromNativeSeparators(location_->text().trimmed())))
        return tr("Choose the folder the project goes in.");
    if (!ProjectTemplates::isFreeFolder(targetFolder()))
        return tr("%1 already exists.").arg(QDir::toNativeSeparators(targetFolder()));
    return {};
}

void NewProjectForm::updateState()
{
    const QListWidgetItem *item = templates_->currentItem();
    description_->setText(item ? item->data(descriptionRole).toString() : QString());
    const QString why = problem();
    summary_->setText(why.isEmpty() ? tr("Creates %1").arg(QDir::toNativeSeparators(targetFolder())) : why);
    create_->setEnabled(why.isEmpty() && item);
}

void NewProjectForm::create()
{
    const QListWidgetItem *item = templates_->currentItem();
    if (!item || !problem().isEmpty())
        return;
    const QString folder = targetFolder();
    const QString templateId = item->data(templateRole).toString();
    QString error;
    if (!ProjectTemplates::create(templateId, folder, &error)) {
        summary_->setText(error);
        return;
    }
    if (git_->isChecked())
        initRepository(folder);
    trustForClaude(folder);
    QSettings settings;
    settings.setValue(QLatin1String(locationKey), location_->text().trimmed());
    settings.setValue(QLatin1String(templateKey), templateId);
    settings.setValue(QLatin1String(gitKey), git_->isChecked());
    ProjectStore::touch(folder);
    emit created(folder);
}

void NewProjectForm::trustForClaude(const QString &folder)
{
    // The user chose every file of the folder: Claude's first session there
    // need not ask whether to trust them.
    QString error;
    if (!ClaudeTrust::trust(folder, &error))
        showNotice({.title = tr("Folder not trusted by Claude"),
                    .location = {},
                    .message = tr("The project was created in %1, but Claude Code will ask whether to trust it: %2")
                                   .arg(QDir::toNativeSeparators(folder), error),
                    .details = {}},
                   this);
}

void NewProjectForm::initRepository(const QString &folder)
{
    // On the UI thread, unlike the other git operations: "git init" writes
    // a few files locally, in milliseconds.
    try {
        GitOperations::init(folder);
    } catch (const GitError &error) {
        // The project is there all the same: it opens without a repository.
        showNotice({.title = tr("Git repository not created"),
                    .location = {},
                    .message = tr("The project was created in %1, but \"git init\" failed: %2")
                                   .arg(QDir::toNativeSeparators(folder), error.message()),
                    .details = {}},
                   this);
    }
}
