// SPDX-License-Identifier: GPL-3.0-only

#include "MergeChoicePanel.h"

#include "MergeTexts.h"
#include "changes/DiffView.h"
#include "git/FilePatch.h"
#include "ui/AppFonts.h"

#include <QButtonGroup>
#include <QHBoxLayout>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QPixmap>
#include <QPushButton>
#include <QRadioButton>
#include <QVBoxLayout>

namespace {

constexpr int marginPx = 24;
constexpr int spacingPx = 12;
constexpr int previewSidePx = 160;

using ConflictVersions::Kind;
using Integration::ConflictSide;

QLabel *wrappedLabel(const QString &text, QWidget *parent)
{
    auto *label = new QLabel(text, parent);
    label->setWordWrap(true);
    label->setTextFormat(Qt::PlainText);
    return label;
}

// The side that still has a file the other side deleted, renamed away or
// never had; none when neither has it any more.
std::optional<ConflictSide> survivingSide(Kind kind)
{
    switch (kind) {
    case Kind::DeletedByCurrent:
    case Kind::AddedByIncoming:
        return ConflictSide::Incoming;
    case Kind::DeletedByIncoming:
    case Kind::AddedByCurrent:
        return ConflictSide::Current;
    case Kind::DeletedByBoth:
    case Kind::BothModified:
    case Kind::BothAdded:
    case Kind::RenamedOnBothSides:
        return std::nullopt;
    }
    return std::nullopt;
}

ConflictSide otherSide(ConflictSide side)
{
    return side == ConflictSide::Current ? ConflictSide::Incoming : ConflictSide::Current;
}

QString explanation(const ConflictVersions::File &file, const Integration::ConflictSides &sides)
{
    switch (file.kind) {
    case Kind::DeletedByCurrent:
        return MergeChoicePanel::tr("%1 deleted the file, and %2 changed it. Deleting it loses this change:")
            .arg(sides.current, sides.incoming);
    case Kind::DeletedByIncoming:
        return MergeChoicePanel::tr("%1 deleted the file, and %2 changed it. Deleting it loses this change:")
            .arg(sides.incoming, sides.current);
    case Kind::AddedByCurrent:
    case Kind::AddedByIncoming:
        return MergeChoicePanel::tr("Only one side has the file under this name: the other renamed or deleted it.");
    case Kind::DeletedByBoth:
        return MergeChoicePanel::tr("Both sides moved the file away from this name.");
    case Kind::BothModified:
    case Kind::BothAdded:
        return MergeChoicePanel::tr("Both sides changed this binary file. It has no lines to combine: keep one "
                                    "version.");
    case Kind::RenamedOnBothSides:
        return MergeChoicePanel::tr("Each side gave the file another name; its content is the same. Keep one name:");
    }
    return {};
}

} // namespace

MergeChoicePanel::MergeChoicePanel(const Theme &theme,
                                   const ConflictVersions::File &file,
                                   const Integration::ConflictSides &sides,
                                   QWidget *parent)
    : QWidget(parent)
    , file_(file)
    , sides_(sides)
    , layout_(new QVBoxLayout(this))
{
    layout_->setContentsMargins(marginPx, marginPx, marginPx, marginPx);
    layout_->setSpacing(spacingPx);
    auto *title = new QLabel(file.path, this);
    AppFonts::setInterfaceFont(title, 1.1, QFont::DemiBold);
    title->setTextFormat(Qt::PlainText);
    auto *state = new QLabel(MergeTexts::fileState(file, sides, nullptr), this);
    state->setObjectName(QStringLiteral("pageDescription"));
    state->setTextFormat(Qt::PlainText);
    layout_->addWidget(title);
    layout_->addWidget(state);
    layout_->addWidget(wrappedLabel(explanation(file, sides), this));
    if (file.kind == Kind::RenamedOnBothSides)
        addNames();
    else if (file.kind == Kind::BothModified || file.kind == Kind::BothAdded)
        addVersions();
    else
        addKeepOrDelete(theme);
}

void MergeChoicePanel::applyTheme(const Theme &theme)
{
    if (change_)
        change_->applyTheme(theme);
}

void MergeChoicePanel::addKeepOrDelete(const Theme &theme)
{
    const std::optional<ConflictSide> survivor = survivingSide(file_.kind);
    const std::optional<ConflictVersions::Version> &kept =
        survivor == ConflictSide::Incoming ? file_.incoming : file_.current;
    if (survivor && kept && file_.base && !file_.isBinary)
        addChange(theme, *file_.base, *kept);
    else if (survivor && kept)
        layout_->addWidget(createVersion(QString(), kept), 0, Qt::AlignLeft);
    layout_->addStretch();
    if (survivor) {
        const QString keeper = *survivor == ConflictSide::Current ? sides_.current : sides_.incoming;
        connect(addChoice(tr("Keep the File"), tr("Keep the file as %1 has it").arg(keeper)), &QPushButton::clicked,
                this, [this, survivor] { emit sideChosen(*survivor); });
    }
    const ConflictSide deleting = survivor ? otherSide(*survivor) : ConflictSide::Current;
    connect(addChoice(tr("Delete the File"), tr("Remove the file from the result")), &QPushButton::clicked, this,
            [this, deleting] { emit sideChosen(deleting); });
}

void MergeChoicePanel::addChange(const Theme &theme,
                                 const ConflictVersions::Version &base,
                                 const ConflictVersions::Version &survivor)
{
    DiffFile diff;
    diff.path = file_.path;
    diff.status = tr("changed");
    try {
        diff.patch = FilePatch::between(base.content, survivor.content, file_.path);
    } catch (const GitError &) {
        return; // the choice stays possible without the change shown
    }
    diff.newText = survivor.content;
    change_ = new DiffView(theme, this);
    change_->setFiles({diff});
    layout_->addWidget(change_, 1);
}

void MergeChoicePanel::addVersions()
{
    auto *versions = new QHBoxLayout;
    versions->setSpacing(spacingPx);
    if (file_.base)
        versions->addWidget(createVersion(tr("Common ancestor"), file_.base));
    versions->addWidget(createVersion(sides_.current, file_.current));
    versions->addWidget(createVersion(sides_.incoming, file_.incoming));
    versions->addStretch();
    layout_->addLayout(versions);
    layout_->addStretch();
    connect(addChoice(tr("Keep %1's Version").arg(sides_.current), QString()), &QPushButton::clicked, this,
            [this] { emit sideChosen(ConflictSide::Current); });
    connect(addChoice(tr("Take %1's Version").arg(sides_.incoming), QString()), &QPushButton::clicked, this,
            [this] { emit sideChosen(ConflictSide::Incoming); });
}

void MergeChoicePanel::addNames()
{
    auto *group = new QButtonGroup(this);
    const auto addName = [this, group](const std::optional<ConflictVersions::Version> &version, const QString &side) {
        auto *button = new QRadioButton(tr("%1, as %2 names it").arg(version ? version->path : QString(), side), this);
        group->addButton(button);
        layout_->addWidget(button);
        return button;
    };
    currentName_ = addName(file_.current, sides_.current);
    incomingName_ = addName(file_.incoming, sides_.incoming);
    otherName_ = new QRadioButton(tr("Another name:"), this);
    group->addButton(otherName_);
    otherPath_ = new QLineEdit(file_.path, this);
    otherPath_->setAccessibleName(tr("Another name for the file"));
    connect(otherPath_, &QLineEdit::textEdited, otherName_, [this] { otherName_->setChecked(true); });
    auto *other = new QHBoxLayout;
    other->addWidget(otherName_);
    other->addWidget(otherPath_, 1);
    layout_->addLayout(other);
    currentName_->setChecked(true);
    layout_->addStretch();
    connect(addChoice(tr("Keep This Name"), tr("Keep the file under the chosen name only")), &QPushButton::clicked,
            this, [this] {
                QString path = otherPath_->text().trimmed();
                if (currentName_->isChecked() && file_.current)
                    path = file_.current->path;
                else if (incomingName_->isChecked() && file_.incoming)
                    path = file_.incoming->path;
                if (!path.isEmpty())
                    emit nameChosen(path);
            });
}

QWidget *MergeChoicePanel::createVersion(const QString &caption,
                                         const std::optional<ConflictVersions::Version> &version)
{
    auto *box = new QWidget(this);
    auto *layout = new QVBoxLayout(box);
    layout->setContentsMargins(0, 0, 0, 0);
    auto *preview = new QLabel(box);
    QImage image;
    if (version && image.loadFromData(version->content)) {
        const QPixmap pixmap = QPixmap::fromImage(image);
        const bool isLarge = image.width() > previewSidePx || image.height() > previewSidePx;
        preview->setPixmap(
            isLarge ? pixmap.scaled(previewSidePx, previewSidePx, Qt::KeepAspectRatio, Qt::SmoothTransformation)
                    : pixmap);
        preview->setToolTip(tr("%1 × %2").arg(image.width()).arg(image.height()));
    } else if (version) {
        preview->setText(QLocale().formattedDataSize(version->content.size()));
    } else {
        preview->setText(tr("No file"));
    }
    layout->addWidget(preview, 0, Qt::AlignHCenter);
    if (!caption.isEmpty()) {
        auto *label = new QLabel(caption, box);
        label->setObjectName(QStringLiteral("pageDescription"));
        label->setTextFormat(Qt::PlainText);
        layout->addWidget(label, 0, Qt::AlignHCenter);
    }
    return box;
}

QPushButton *MergeChoicePanel::addChoice(const QString &text, const QString &toolTip)
{
    if (!choices_) {
        auto *choices = new QHBoxLayout;
        choices->addStretch();
        layout_->addLayout(choices);
        choices_ = choices;
    }
    auto *button = new QPushButton(text, this);
    button->setToolTip(toolTip);
    choices_->addWidget(button);
    return button;
}
