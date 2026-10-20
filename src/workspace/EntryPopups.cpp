// SPDX-License-Identifier: GPL-3.0-only

#include "EntryPopups.h"

#include "ui/AppFonts.h"
#include "ui/UiMetrics.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

#include <algorithm>

namespace {

constexpr int minimumPopupWidth = 480;
constexpr int maximumPopupWidth = 640;
constexpr qreal popupWindowShare = 0.4;
constexpr int windowMargin = 20;

QString translate(const char *text)
{
    return QCoreApplication::translate("EntryPopups", text);
}

bool isValidCharacter(QChar character)
{
#ifdef Q_OS_WIN
    if (QStringLiteral("<>:\"|?*\\").contains(character))
        return false;
#endif
    return character.category() != QChar::Other_Control && character != QLatin1Char('/');
}

bool isValidComponent(const QString &component)
{
    if (component.isEmpty() || component == QLatin1String(".") || component == QLatin1String(".."))
        return false;
    if (!std::ranges::all_of(component, isValidCharacter))
        return false;
#ifdef Q_OS_WIN
    if (component.endsWith(QLatin1Char('.')) || component.endsWith(QLatin1Char(' ')))
        return false;
#endif
    return true;
}

QString invalidName(const QString &name)
{
    return translate("“%1” is an invalid file name").arg(name);
}

QString alreadyExists(const QFileInfo &info)
{
    return (info.isDir() ? translate("A folder with the name “%1” already exists")
                         : translate("A file with the name “%1” already exists"))
        .arg(info.fileName());
}

// Error outline and text: the colors of the usage warning in the status bar.
QString errorColor(const QPalette &palette)
{
    return palette.color(QPalette::Window).lightness() < 128 ? QStringLiteral("#e06c75") : QStringLiteral("#e45649");
}

} // namespace

EntryTarget resolveNewEntry(const QString &root, const QString &folder, const QString &name, bool isFolder)
{
    const QString emptyError = isFolder ? translate("Specify a folder name") : translate("Specify a file name");
    const QString trimmed = name.trimmed();
    const QString normalized = QDir::fromNativeSeparators(trimmed);
    const QStringList components = normalized.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    if (components.isEmpty() || (!isFolder && normalized.endsWith(QLatin1Char('/'))))
        return {{}, emptyError};
    if (normalized.startsWith(QLatin1Char('/')) || !std::all_of(components.begin(), components.end(), isValidComponent))
        return {{}, invalidName(trimmed)};

    QString path = QDir::cleanPath(folder);
    for (qsizetype i = 0; i < components.size(); ++i) {
        path += QLatin1Char('/') + components[i];
        const QFileInfo info(path);
        if (info.exists() && (i == components.size() - 1 || !info.isDir()))
            return {{}, alreadyExists(info)};
    }
    if (!path.startsWith(QDir::cleanPath(root) + QLatin1Char('/')))
        return {{}, invalidName(trimmed)};
    return {path, {}};
}

EntryTarget resolveRename(const QString &path, const QString &name)
{
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty())
        return {{}, translate("Specify a new name")};
    if (!isValidComponent(trimmed))
        return {{}, invalidName(trimmed)};
    const QFileInfo current(path);
    if (trimmed == current.fileName())
        return {path, {}};
    const QString renamed = QDir(current.absolutePath()).filePath(trimmed);
    const QFileInfo target(renamed);
    // On a case-insensitive file system, "readme.md" → "README.md" finds
    // the file itself.
    const bool sameFile = trimmed.compare(current.fileName(), Qt::CaseInsensitive) == 0;
    if (target.exists() && !sameFile)
        return {{}, alreadyExists(target)};
    return {renamed, {}};
}

EntryPopup::EntryPopup(const QString &title, const QString &location, QWidget *parent)
    : QFrame(parent, Qt::Popup)
    , content_(new QVBoxLayout)
    , location_(new QLabel(this))
    , error_(new QLabel(this))
{
    setAttribute(Qt::WA_DeleteOnClose);
    setFrameShape(QFrame::StyledPanel);

    auto *titleLabel = new QLabel(title, this);
    titleLabel->setTextFormat(Qt::PlainText);
    titleLabel->setWordWrap(true);
    AppFonts::setInterfaceFont(titleLabel, 1);
    location_->setTextFormat(Qt::PlainText);
    location_->setForegroundRole(QPalette::PlaceholderText);
    location_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    location_->setToolTip(location);
    location_->setVisible(!location.isEmpty());

    error_->setTextFormat(Qt::PlainText);
    error_->setWordWrap(true);
    error_->setStyleSheet(QStringLiteral("color: %1;").arg(errorColor(palette())));
    error_->hide();

    auto *header = new QVBoxLayout;
    header->setSpacing(UiMetrics::scaled(4));
    header->addWidget(titleLabel);
    header->addWidget(location_);

    content_->setSpacing(UiMetrics::scaled(12));
    auto *layout = new QVBoxLayout(this);
    const int horizontalPadding = UiMetrics::scaled(20);
    const int verticalPadding = UiMetrics::scaled(16);
    layout->setContentsMargins(horizontalPadding, verticalPadding, horizontalPadding, verticalPadding);
    layout->setSpacing(UiMetrics::scaled(12));
    layout->addLayout(header);
    layout->addLayout(content_);
    layout->addWidget(error_);
}

void EntryPopup::popup()
{
    const QWidget *window = parentWidget()->window();
    const QRect available = window->contentsRect();
    const int preferredWidth = std::clamp(qRound(available.width() * popupWindowShare),
                                          UiMetrics::scaled(minimumPopupWidth), UiMetrics::scaled(maximumPopupWidth));
    const int width = std::min(preferredWidth, std::max(1, available.width() - 2 * UiMetrics::scaled(windowMargin)));
    setFixedWidth(width);
    const QMargins padding = layout()->contentsMargins();
    const int locationWidth = std::max(0, contentsRect().width() - padding.left() - padding.right());
    location_->setText(location_->fontMetrics().elidedText(location_->toolTip(), Qt::ElideMiddle, locationWidth));
    resizeToContent();
    const QPoint offset((available.width() - width) / 2, std::max(0, (available.height() - height()) / 3));
    move(window->mapToGlobal(available.topLeft() + offset));
    show();
    focusContent();
}

QString EntryPopup::error() const
{
    return !error_->isHidden() ? error_->text() : QString();
}

void EntryPopup::showError(const QString &message)
{
    if (message.isEmpty() && error_->isHidden())
        return;
    error_->setText(message);
    error_->setVisible(!message.isEmpty());
    errorChanged(!message.isEmpty());
    resizeToContent();
}

void EntryPopup::resizeToContent()
{
    ensurePolished();
    const int contentHeight = layout()->totalHeightForWidth(width());
    resize(width(), contentHeight >= 0 ? contentHeight : sizeHint().height());
}

void EntryPopup::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape) {
        close();
        return;
    }
    QFrame::keyPressEvent(event);
}

NamePopup::NamePopup(const QString &title,
                     const QString &location,
                     std::function<QString(const QString &)> apply,
                     QWidget *parent)
    : EntryPopup(title, location, parent)
    , apply_(std::move(apply))
    , field_(new QLineEdit(this))
{
    field_->setPlaceholderText(tr("Name, with “/” to create subfolders"));
    field_->setMinimumHeight(UiMetrics::scaled(36));
    connect(field_, &QLineEdit::returnPressed, this, &NamePopup::accept);
    connect(field_, &QLineEdit::textEdited, this, [this] { showError({}); });
    content()->addWidget(field_);
}

void NamePopup::setName(const QString &name, bool isFolder)
{
    field_->setPlaceholderText(QString());
    field_->setText(name);
    const qsizetype dot = name.lastIndexOf(QLatin1Char('.'));
    field_->setSelection(0, isFolder || dot <= 0 ? name.size() : dot);
}

void NamePopup::accept()
{
    const QString message = apply_(field_->text().trimmed());
    if (message.isEmpty())
        close();
    else
        showError(message);
}

void NamePopup::focusContent()
{
    field_->setFocus();
}

void NamePopup::errorChanged(bool shown)
{
    // The outline only: the field keeps the style's look otherwise.
    field_->setStyleSheet(shown ? QStringLiteral("QLineEdit { border-color: %1; }").arg(errorColor(palette()))
                                : QString());
}

ConfirmPopup::ConfirmPopup(const Question &question, std::function<bool(ConfirmPopup *)> apply, QWidget *parent)
    : EntryPopup(question.title, question.location, parent)
    , apply_(std::move(apply))
    , message_(new QLabel(question.message, this))
    , cancel_(new QPushButton(tr("Cancel"), this))
    , action_(new QPushButton(question.actionText, this))
{
    message_->setWordWrap(true);
    message_->setTextFormat(Qt::PlainText);
    connect(cancel_, &QPushButton::clicked, this, &QWidget::close);
    connect(action_, &QPushButton::clicked, this, &ConfirmPopup::accept);

    auto *buttons = new QHBoxLayout;
    buttons->addStretch();
    buttons->addWidget(cancel_);
    buttons->addWidget(action_);
    content()->addWidget(message_);
    content()->addLayout(buttons);
}

void ConfirmPopup::setMessage(const QString &message, const QString &actionText)
{
    message_->setText(message);
    action_->setText(actionText);
    resizeToContent();
}

QString ConfirmPopup::message() const
{
    return message_->text();
}

void ConfirmPopup::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
        if (focusWidget() == cancel_)
            close();
        else
            accept();
        return;
    }
    EntryPopup::keyPressEvent(event);
}

void ConfirmPopup::accept()
{
    if (apply_(this))
        close();
}

void ConfirmPopup::focusContent()
{
    action_->setFocus();
}
