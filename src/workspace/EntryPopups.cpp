// SPDX-License-Identifier: GPL-3.0-only

#include "EntryPopups.h"

#include "ui/PopupWidgets.h"
#include "ui/ThemeStyle.h"

#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
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
constexpr qreal popupWindowShare = 0.36;
// The header's margins and close button, beside the location.
constexpr int headerReservePx = 64;

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

EntryPopup::EntryPopup(const QString &settingsKey, const QString &title, const QString &location, QWidget *parent)
    : OverlayPopup(settingsKey, QSizeF(popupWindowShare, 0), QSize(minimumPopupWidth, 0), parent)
    , location_(location)
    , header_(new PopupHeader(QStringLiteral("document"), title, this))
    , content_(new QVBoxLayout)
    , error_(new QLabel(this))
    , hint_(new QLabel(this))
    , footerButtons_(new QHBoxLayout)
{
    connect(this, &OverlayPopup::dismissed, this, &QObject::deleteLater);
    connect(header_, &PopupHeader::closeRequested, this, &OverlayPopup::dismiss);

    error_->setObjectName(QStringLiteral("entryError"));
    error_->setTextFormat(Qt::PlainText);
    error_->setWordWrap(true);
    error_->hide();
    content_->setSpacing(10);
    auto *body = new QVBoxLayout;
    body->setContentsMargins(16, 14, 16, 14);
    body->setSpacing(10);
    body->addLayout(content_);
    body->addWidget(error_);

    auto *footer = new QFrame(this);
    footer->setObjectName(QStringLiteral("popupFooter"));
    hint_->setParent(footer);
    hint_->setObjectName(QStringLiteral("footerHint"));
    hint_->setText(tr("Enter to apply · Esc to cancel"));
    footerButtons_->setSpacing(8);
    auto *footerLayout = new QHBoxLayout(footer);
    footerLayout->setContentsMargins(16, 10, 16, 10);
    footerLayout->setSpacing(8);
    footerLayout->addWidget(hint_, 1);
    footerLayout->addLayout(footerButtons_);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(1, 1, 1, 1);
    layout->setSpacing(0);
    layout->addWidget(header_);
    layout->addLayout(body);
    layout->addWidget(footer);

    const Theme &theme = ThemeStyle::applied();
    setStyleSheet(popupStyleSheet(theme, QStringLiteral("QLabel#entryError { color: %1; }").arg(errorColor(palette()))));
    header_->applyTheme(theme);
}

void EntryPopup::setIconName(const QString &iconName)
{
    header_->setIconName(iconName);
    header_->applyTheme(ThemeStyle::applied());
}

void EntryPopup::popup()
{
    showCentered();
    resizeToContent();
    focusContent();
}

void EntryPopup::exec()
{
    QEventLoop loop;
    connect(this, &QObject::destroyed, &loop, &QEventLoop::quit);
    popup();
    loop.exec();
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

void EntryPopup::addFooterButton(QPushButton *button)
{
    button->setMinimumWidth(84);
    footerButtons_->addWidget(button);
}

void EntryPopup::setFooterHint(const QString &hint)
{
    hint_->setText(hint);
}

void EntryPopup::resizeToContent()
{
    if (!isVisible())
        return;
    ensurePolished();
    // The location on one line, cut in the middle: the end of a path matters.
    const int locationWidth = std::max(0, width() - 2 * shadowWidth - headerReservePx);
    header_->setSubtitle(fontMetrics().elidedText(location_, Qt::ElideMiddle, locationWidth));
    const int panelHeight = layout()->totalHeightForWidth(width()) - 2 * shadowWidth;
    setMinimumPanelSize(QSize(minimumPopupWidth, std::max(panelHeight, 0)));
}

void EntryPopup::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape) {
        dismiss();
        return;
    }
    OverlayPopup::keyPressEvent(event);
}

NamePopup::NamePopup(const QString &title,
                     const QString &location,
                     std::function<QString(const QString &)> apply,
                     QWidget *parent)
    : EntryPopup(QStringLiteral("namePopup"), title, location, parent)
    , apply_(std::move(apply))
    , field_(new QLineEdit(this))
{
    field_->setPlaceholderText(tr("Name, with “/” to create subfolders"));
    connect(field_, &QLineEdit::returnPressed, this, &NamePopup::accept);
    connect(field_, &QLineEdit::textEdited, this, [this] { showError({}); });
    content()->addWidget(field_);

    auto *cancel = new QPushButton(tr("Cancel"), this);
    auto *ok = new QPushButton(tr("OK"), this);
    ok->setDefault(true);
    connect(cancel, &QPushButton::clicked, this, &OverlayPopup::dismiss);
    connect(ok, &QPushButton::clicked, this, &NamePopup::accept);
    addFooterButton(cancel);
    addFooterButton(ok);
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
        dismiss();
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
    : EntryPopup(QStringLiteral("confirmPopup"), question.title, question.location, parent)
    , apply_(std::move(apply))
    , message_(new QLabel(question.message, this))
    , cancel_(new QPushButton(tr("Cancel"), this))
    , action_(new QPushButton(question.actionText, this))
{
    message_->setWordWrap(true);
    message_->setTextFormat(Qt::PlainText);
    setIconName(QStringLiteral("discard"));
    setFooterHint(tr("Enter to confirm · Esc to cancel"));
    connect(cancel_, &QPushButton::clicked, this, &OverlayPopup::dismiss);
    connect(action_, &QPushButton::clicked, this, &ConfirmPopup::accept);
    action_->setDefault(true);
    content()->addWidget(message_);
    addFooterButton(cancel_);
    addFooterButton(action_);
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
            dismiss();
        else
            accept();
        return;
    }
    EntryPopup::keyPressEvent(event);
}

void ConfirmPopup::accept()
{
    if (apply_(this))
        dismiss();
}

void ConfirmPopup::focusContent()
{
    action_->setFocus();
}
