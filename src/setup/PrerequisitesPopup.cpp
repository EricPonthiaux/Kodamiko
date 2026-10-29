// SPDX-License-Identifier: GPL-3.0-only

#include "PrerequisitesPopup.h"

#include "ui/AppFonts.h"
#include "ui/Paragraph.h"
#include "ui/PopupFooter.h"
#include "ui/PopupMetrics.h"
#include "ui/PopupWidgets.h"
#include "ui/ThemeStyle.h"
#include "ui/UiIcons.h"
#include "ui/UiMetrics.h"

#include <QApplication>
#include <QClipboard>
#include <QDir>
#include <QFrame>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

#include <algorithm>

namespace {

constexpr const char *prerequisiteRules = R"(
QFrame#prerequisiteSummary { background: @summaryTint; border: 1px solid @line; border-radius: 8px; }
QLabel#prerequisiteSummaryIcon { background: @summaryIconTint; border-radius: 10px; }
QLabel#prerequisiteCount { color: @accentText; background: @slate; border-radius: 5px; padding: 5px 9px; }
QFrame#prerequisiteCard { background: @window; border: 1px solid @line; border-radius: 8px; }
QLabel#prerequisiteToolIcon { background: @panel; border: 1px solid @line; border-radius: 8px; }
QLabel#prerequisiteStatus { border: none; border-radius: 5px; padding: 4px 9px; }
QLabel#prerequisiteStatus[state="ready"] { color: @ready; background: @readyTint; }
QLabel#prerequisiteStatus[state="missing"] { color: @error; background: @missingTint; }
QLabel#prerequisiteStatus[state="outdated"] { color: @warning; background: @outdatedTint; }
QLabel#prerequisiteSection { color: @muted; }
QLineEdit#prerequisitePath { color: @muted; background: @base; border: 1px solid @line; border-radius: 5px; padding: 7px 10px; }
QLineEdit#prerequisitePath:focus, QLineEdit#installCommand:focus { border-color: @accent; }
QLineEdit#installCommand { background: @base; border: 1px solid @line; border-radius: 5px; padding: 7px 10px; }
QPushButton { padding: 7px 14px; border-radius: 5px; }
QPushButton#installPrerequisite { color: @accentText; background: @slate; border-color: @line; }
QPushButton#installPrerequisite:hover { border-color: @accent; }
)";

QString statusName(Prerequisite::Status status)
{
    switch (status) {
    case Prerequisite::Status::Missing:
        return QStringLiteral("missing");
    case Prerequisite::Status::Outdated:
        return QStringLiteral("outdated");
    case Prerequisite::Status::Ok:
        return QStringLiteral("ready");
    }
    return {};
}

QString statusText(Prerequisite::Status status)
{
    switch (status) {
    case Prerequisite::Status::Missing:
        return PrerequisitesPopup::tr("Not installed");
    case Prerequisite::Status::Outdated:
        return PrerequisitesPopup::tr("Update required");
    case Prerequisite::Status::Ok:
        return PrerequisitesPopup::tr("Ready");
    }
    return {};
}

QString versionText(const Prerequisite &prerequisite)
{
    if (prerequisite.version.isNull())
        return PrerequisitesPopup::tr("Requires version %1 or newer").arg(prerequisite.minimumVersion.toString());
    if (prerequisite.status == Prerequisite::Status::Outdated)
        return PrerequisitesPopup::tr("Version %1 · Requires %2 or newer")
            .arg(prerequisite.version.toString(), prerequisite.minimumVersion.toString());
    return PrerequisitesPopup::tr("Version %1").arg(prerequisite.version.toString());
}

QLabel *descriptionLabel(const QString &text, QWidget *parent)
{
    auto *label = new QLabel(parent);
    label->setObjectName(QStringLiteral("settingsDescription"));
    Paragraph::setText(label, text);
    return label;
}

QLabel *symbolLabel(const QString &name, const QColor &color, QWidget *parent)
{
    auto *label = new QLabel(parent);
    label->setAlignment(Qt::AlignCenter);
    UiMetrics::setScaledSize(label, QSize(44, 44));
    AppFonts::followApplicationFont(label, [label, name, color] {
        label->setPixmap(UiIcons::icon(name, color).pixmap(UiMetrics::scaledSquare(22), label->devicePixelRatioF()));
    });
    return label;
}

QHBoxLayout *cardHeading(const Prerequisite &prerequisite, const Theme &theme, QWidget *card)
{
    const bool isGit = prerequisite.id == Prerequisite::Id::Git;
    auto *symbol = symbolLabel(isGit ? QStringLiteral("branch") : QStringLiteral("claude"),
                               isGit ? theme.accent : theme.claude, card);
    symbol->setObjectName(QStringLiteral("prerequisiteToolIcon"));
    auto *name = new QLabel(prerequisite.name, card);
    AppFonts::setInterfaceFont(name, 1.1, QFont::DemiBold);
    auto *identity = new QVBoxLayout;
    identity->setSpacing(3);
    identity->addWidget(name);
    identity->addWidget(descriptionLabel(versionText(prerequisite), card));
    auto *status = new QLabel(statusText(prerequisite.status), card);
    status->setObjectName(QStringLiteral("prerequisiteStatus"));
    status->setProperty("state", statusName(prerequisite.status));
    AppFonts::setInterfaceFont(status, 0.9, QFont::DemiBold);

    auto *heading = new QHBoxLayout;
    heading->setSpacing(12);
    heading->addWidget(symbol);
    heading->addLayout(identity, 1);
    heading->addWidget(status, 0, Qt::AlignTop);
    return heading;
}

} // namespace

PrerequisitesPopup::PrerequisitesPopup(const Theme &theme, QWidget *parent)
    : OverlayPopup(QStringLiteral("prerequisites"), QSizeF(0.5, 0.75), QSize(660, 620), parent)
    , theme_(theme)
    , header_(new PopupHeader(QStringLiteral("check"), tr("Prerequisites"), this))
    , cardsLayout_(new QVBoxLayout)
{
    connect(header_, &PopupHeader::closeRequested, this, &OverlayPopup::dismiss);

    header_->setSubtitle(tr("The tools that power your workspace."));

    auto *content = new QWidget(this);
    content->setObjectName(QStringLiteral("pageContent"));
    auto *contentLayout = new QVBoxLayout(content);
    contentLayout->setContentsMargins(22, 20, 22, 20);
    cardsLayout_->setSpacing(PopupMetrics::bodySpacing);
    contentLayout->addLayout(cardsLayout_);
    contentLayout->addStretch();
    auto *scroll = new QScrollArea(this);
    scroll->setObjectName(QStringLiteral("pageScroll"));
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setWidget(content);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(1, 1, 1, 1);
    layout->setSpacing(0);
    layout->addWidget(header_);
    layout->addWidget(scroll, 1);
    layout->addWidget(createFooter());

    applyTheme(theme);
}

QWidget *PrerequisitesPopup::createFooter()
{
    auto *footer = new PopupFooter(this);
    Paragraph::setText(footer->hint(), tr("Installations open in a terminal tab."));
    auto *checkAgain = new QPushButton(tr("Check Again"), footer);
    checkAgain->setObjectName(QStringLiteral("checkPrerequisitesAgain"));
    checkAgain->setDefault(true);
    UiMetrics::setScaledIconSize(checkAgain);
    auto *close = new QPushButton(tr("Close"), footer);
    connect(checkAgain, &QPushButton::clicked, this, &PrerequisitesPopup::recheck);
    connect(close, &QPushButton::clicked, this, &OverlayPopup::dismiss);
    footer->addButton(close);
    footer->addButton(checkAgain);
    return footer;
}

void PrerequisitesPopup::popup()
{
    recheck();
    showCentered();
    findChild<QPushButton *>(QStringLiteral("checkPrerequisitesAgain"))->setFocus();
}

bool PrerequisitesPopup::allOk() const
{
    return std::all_of(prerequisites_.begin(), prerequisites_.end(),
                       [](const Prerequisite &p) { return p.status == Prerequisite::Status::Ok; });
}

void PrerequisitesPopup::recheck()
{
    QApplication::setOverrideCursor(Qt::WaitCursor);
    prerequisites_ = Prerequisites::check();
    QApplication::restoreOverrideCursor();
    rebuild();
    emit checked();
}

void PrerequisitesPopup::applyTheme(const Theme &theme)
{
    theme_ = theme;
    const QColor ready = theme.isDark ? QColor("#b5dba1") : QColor("#397647");
    QString rules = QString::fromLatin1(prerequisiteRules);
    rules.replace(QStringLiteral("@summaryIconTint"), ThemeStyle::mixed(theme.window, theme.accent, 0.16).name());
    rules.replace(QStringLiteral("@summaryTint"), ThemeStyle::mixed(theme.window, theme.accent, 0.06).name());
    rules.replace(QStringLiteral("@readyTint"), ThemeStyle::mixed(theme.window, ready, 0.1).name());
    rules.replace(QStringLiteral("@ready"), ready.name());
    rules.replace(QStringLiteral("@missingTint"), ThemeStyle::mixed(theme.window, theme.error, 0.1).name());
    rules.replace(QStringLiteral("@outdatedTint"), ThemeStyle::mixed(theme.window, theme.warning, 0.1).name());
    setStyleSheet(popupStyleSheet(theme, rules));
    header_->applyTheme(theme);
    findChild<QPushButton *>(QStringLiteral("checkPrerequisitesAgain"))
        ->setIcon(UiIcons::icon(QStringLiteral("refresh"), theme.onAccent));
    rebuild();
}

void PrerequisitesPopup::rebuild()
{
    while (const QLayoutItem *item = cardsLayout_->takeAt(0)) {
        delete item->widget();
        delete item;
    }

    if (prerequisites_.isEmpty())
        return;
    cardsLayout_->addWidget(createSummary());
    auto *section = new QLabel(tr("REQUIRED TOOLS"), this);
    section->setObjectName(QStringLiteral("prerequisiteSection"));
    AppFonts::setInterfaceFont(section, 0.8, QFont::DemiBold, 1.1);
    cardsLayout_->addWidget(section);
    for (const Prerequisite &prerequisite : std::as_const(prerequisites_))
        cardsLayout_->addWidget(createCard(prerequisite));
}

QFrame *PrerequisitesPopup::createSummary()
{
    auto *summary = new QFrame(this);
    summary->setObjectName(QStringLiteral("prerequisiteSummary"));
    auto *symbol = symbolLabel(allOk() ? QStringLiteral("check") : QStringLiteral("settings"), theme_.accent, summary);
    symbol->setObjectName(QStringLiteral("prerequisiteSummaryIcon"));
    auto *title = new QLabel(allOk() ? tr("You're ready to create") : tr("Let's get your workspace ready"), summary);
    title->setWordWrap(true);
    AppFonts::setInterfaceFont(title, 1.25, QFont::DemiBold);
    auto *details = new QVBoxLayout;
    details->setSpacing(5);
    details->addWidget(title);
    details->addWidget(descriptionLabel(allOk() ? tr("All required tools are installed and ready to use.")
                                                : tr("Install or update the tools below, then check again."),
                                        summary));
    const auto ready = std::ranges::count_if(
        prerequisites_, [](const Prerequisite &p) { return p.status == Prerequisite::Status::Ok; });
    auto *count = new QLabel(tr("%1 / %2 ready").arg(ready).arg(prerequisites_.size()), summary);
    count->setObjectName(QStringLiteral("prerequisiteCount"));
    AppFonts::setInterfaceFont(count, 0.85, QFont::DemiBold);
    auto *layout = new QHBoxLayout(summary);
    layout->setContentsMargins(16, 18, 16, 18);
    layout->setSpacing(14);
    layout->addWidget(symbol);
    layout->addLayout(details, 1);
    layout->addWidget(count, 0, Qt::AlignTop);
    return summary;
}

QFrame *PrerequisitesPopup::createCard(const Prerequisite &prerequisite)
{
    auto *card = new QFrame(this);
    card->setObjectName(QStringLiteral("prerequisiteCard"));
    auto *cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(16, 16, 16, 16);
    cardLayout->setSpacing(12);
    cardLayout->addLayout(cardHeading(prerequisite, theme_, card));
    cardLayout->addWidget(descriptionLabel(prerequisite.purpose, card));

    if (!prerequisite.path.isEmpty()) {
        auto *path = new QLineEdit(QDir::toNativeSeparators(prerequisite.path), card);
        path->setObjectName(QStringLiteral("prerequisitePath"));
        path->setAccessibleName(tr("%1 executable path").arg(prerequisite.name));
        path->setToolTip(path->text());
        path->setReadOnly(true);
        path->setCursorPosition(0);
        cardLayout->addWidget(path);
    }

    if (prerequisite.status == Prerequisite::Status::Ok)
        return card;
    if (!prerequisite.installCommand.isEmpty())
        cardLayout->addLayout(createInstallRow(prerequisite, card));
    auto *guide = new QLabel(
        tr("<a href=\"%1\">Installation guide ↗</a>").arg(prerequisite.helpUrl.toString().toHtmlEscaped()), card);
    guide->setTextInteractionFlags(Qt::TextBrowserInteraction);
    guide->setOpenExternalLinks(true);
    cardLayout->addWidget(guide);
    return card;
}

QHBoxLayout *PrerequisitesPopup::createInstallRow(const Prerequisite &prerequisite, QWidget *card)
{
    auto *command = new QLineEdit(prerequisite.installCommand, card);
    command->setObjectName(QStringLiteral("installCommand"));
    command->setReadOnly(true);
    command->setAccessibleName(tr("Install command for %1").arg(prerequisite.name));
    command->setToolTip(prerequisite.installCommand);
    AppFonts::followApplicationFont(
        command, [command] { command->setFont(AppFonts::codeFont(QApplication::font().pointSizeF() * 0.9)); });
    command->setCursorPosition(0);
    auto *copy = new QPushButton(tr("Copy"), card);
    copy->setIcon(UiIcons::icon(QStringLiteral("copy"), theme_.mutedText));
    UiMetrics::setScaledIconSize(copy);
    copy->setAccessibleName(tr("Copy install command for %1").arg(prerequisite.name));
    connect(copy, &QPushButton::clicked, this,
            [text = prerequisite.installCommand] { QApplication::clipboard()->setText(text); });
    auto *install =
        new QPushButton(prerequisite.status == Prerequisite::Status::Outdated ? tr("Update") : tr("Install"), card);
    install->setObjectName(QStringLiteral("installPrerequisite"));
    install->setIcon(UiIcons::icon(QStringLiteral("terminal"), theme_.accentText));
    UiMetrics::setScaledIconSize(install);
    install->setAccessibleName(tr("Install or update %1 in a terminal").arg(prerequisite.name));
    connect(install, &QPushButton::clicked, this, [this, prerequisite] {
        dismiss();
        emit installRequested(tr("Install %1").arg(prerequisite.name),
                              Prerequisites::installProcess(prerequisite, QDir::homePath()));
    });
    auto *commandRow = new QHBoxLayout;
    commandRow->setSpacing(8);
    commandRow->addWidget(command, 1);
    commandRow->addWidget(copy);
    commandRow->addWidget(install);
    return commandRow;
}

void PrerequisitesPopup::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape) {
        dismiss();
        return;
    }
    OverlayPopup::keyPressEvent(event);
}
