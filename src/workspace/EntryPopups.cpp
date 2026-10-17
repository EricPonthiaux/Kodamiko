#include "EntryPopups.h"

#include "ui/AppFonts.h"

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

constexpr int popupWidth = 420;

QString translate(const char *text)
{
    return QCoreApplication::translate("EntryPopups", text);
}

bool isValidComponent(const QString &component)
{
    if (component.isEmpty() || component == QLatin1String(".") || component == QLatin1String(".."))
        return false;
    for (const QChar character : component) {
        if (character.category() == QChar::Other_Control || character == QLatin1Char('/'))
            return false;
#ifdef Q_OS_WIN
        if (QStringLiteral("<>:\"|?*\\").contains(character))
            return false;
#endif
    }
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
    , error_(new QLabel(this))
{
    setAttribute(Qt::WA_DeleteOnClose);
    setFrameShape(QFrame::StyledPanel);

    auto *titleLabel = new QLabel(title, this);
    AppFonts::setInterfaceFont(titleLabel, 1);
    auto *locationLabel = new QLabel(this);
    locationLabel->setForegroundRole(QPalette::PlaceholderText);
    locationLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    locationLabel->setText(locationLabel->fontMetrics().elidedText(location, Qt::ElideMiddle, popupWidth / 2));
    locationLabel->setToolTip(location);

    error_->setWordWrap(true);
    error_->setStyleSheet(QStringLiteral("color: %1;").arg(errorColor(palette())));
    error_->hide();

    auto *header = new QHBoxLayout;
    header->addWidget(titleLabel);
    header->addStretch();
    header->addWidget(locationLabel);

    content_->setSpacing(6);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 6, 8, 8);
    layout->setSpacing(6);
    layout->addLayout(header);
    layout->addLayout(content_);
    layout->addWidget(error_);
}

void EntryPopup::popup()
{
    const QWidget *window = parentWidget()->window();
    const int width = std::min(popupWidth, window->width() - 40);
    setFixedWidth(width);
    adjustSize();
    move(window->mapToGlobal(QPoint((window->width() - width) / 2, (window->height() - height()) / 3)));
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
    adjustSize();
}

void EntryPopup::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape) {
        close();
        return;
    }
    QFrame::keyPressEvent(event);
}

NamePopup::NamePopup(const QString &title, const QString &location, std::function<QString(const QString &)> apply,
                     QWidget *parent)
    : EntryPopup(title, location, parent)
    , apply_(std::move(apply))
    , field_(new QLineEdit(this))
{
    field_->setPlaceholderText(tr("Name, with “/” to create subfolders"));
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
    field_->setStyleSheet(shown ? QStringLiteral("QLineEdit { border: 1px solid %1; border-radius: 3px; padding: 2px; }")
                                      .arg(errorColor(palette()))
                                : QString());
}

ConfirmPopup::ConfirmPopup(const QString &title, const QString &location, const QString &message,
                           const QString &actionText, std::function<bool(ConfirmPopup *)> apply, QWidget *parent)
    : EntryPopup(title, location, parent)
    , apply_(std::move(apply))
    , message_(new QLabel(message, this))
    , cancel_(new QPushButton(tr("Cancel"), this))
    , action_(new QPushButton(actionText, this))
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
    adjustSize();
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
