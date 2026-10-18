#include "CommandPalette.h"

#include <QAction>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QPointer>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <algorithm>

namespace {

constexpr int visibleRows = 12;

// "Save &All" → "Save All"; "&&" stays a literal "&".
QString withoutMnemonic(QString text)
{
    text.replace(QLatin1String("&&"), QStringLiteral("\x01"));
    text.remove(QLatin1Char('&'));
    text.replace(QLatin1Char('\x01'), QLatin1Char('&'));
    return text;
}

void collect(const QMenu *menu, const QString &prefix, QList<QPair<QString, QAction *>> &commands)
{
    for (QAction *action : menu->actions()) {
        if (action->isSeparator() || !action->isVisible() || !action->isEnabled())
            continue;
        const QString name = withoutMnemonic(action->text());
        if (const QMenu *submenu = action->menu())
            collect(submenu, prefix + name + QStringLiteral(": "), commands);
        else
            commands.append({prefix + name, action});
    }
}

} // namespace

CommandPalette::CommandPalette(QWidget *parent)
    : OverlayPopup(QStringLiteral("commandPalette"), QSizeF(0.45, 0.55), QSize(560, 360), parent)
{
    search_ = new QLineEdit(this);
    search_->setPlaceholderText(tr("Type a command"));
    search_->setClearButtonEnabled(true);
    search_->installEventFilter(this);
    connect(search_, &QLineEdit::textChanged, this, &CommandPalette::filter);
    connect(search_, &QLineEdit::returnPressed, this, &CommandPalette::runCurrent);

    list_ = new QTreeWidget(this);
    list_->setColumnCount(2);
    list_->setHeaderHidden(true);
    list_->setRootIsDecorated(false);
    list_->setUniformRowHeights(true);
    list_->setFocusPolicy(Qt::NoFocus);
    list_->setFrameShape(QFrame::NoFrame);
    list_->header()->setStretchLastSection(false);
    list_->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    list_->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    connect(list_, &QTreeWidget::itemClicked, this, &CommandPalette::runCurrent);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->setSpacing(6);
    layout->addWidget(search_);
    layout->addWidget(list_);
}

void CommandPalette::popup(const QMenuBar *menuBar)
{
    commands_.clear();
    QList<QPair<QString, QAction *>> found;
    for (const QAction *action : menuBar->actions()) {
        if (const QMenu *menu = action->menu())
            collect(menu, withoutMnemonic(action->text()) + QStringLiteral(": "), found);
    }
    for (const auto &[name, action] : found)
        commands_.append({name, action});

    search_->clear();
    filter();

    showCentered();
    search_->setFocus();
}

int CommandPalette::matchScore(QStringView text, QStringView query)
{
    int score = 0;
    qsizetype position = 0;
    qsizetype previous = -2;
    for (const QChar character : query) {
        if (character.isSpace())
            continue;
        const qsizetype found = text.indexOf(character, position, Qt::CaseInsensitive);
        if (found < 0)
            return -1;
        score += 1;
        if (found == previous + 1)
            score += 5; // consecutive letters
        if (found == 0 || !text.at(found - 1).isLetterOrNumber())
            score += 3; // start of a word
        previous = found;
        position = found + 1;
    }
    return score;
}

void CommandPalette::filter()
{
    const QString query = search_->text().trimmed();
    QList<QPair<int, qsizetype>> matches; // score, command index
    for (qsizetype i = 0; i < commands_.size(); ++i) {
        const int score = query.isEmpty() ? 0 : matchScore(commands_[i].name, query);
        if (score >= 0)
            matches.append({score, i});
    }
    std::stable_sort(matches.begin(), matches.end(), [](const auto &a, const auto &b) { return a.first > b.first; });

    list_->clear();
    for (const auto &[score, index] : matches) {
        const Command &command = commands_[index];
        auto *item =
            new QTreeWidgetItem(list_, {command.name, command.action->shortcut().toString(QKeySequence::NativeText)});
        item->setData(0, Qt::UserRole, QVariant::fromValue(index));
        item->setForeground(1, palette().brush(QPalette::Disabled, QPalette::Text));
        item->setTextAlignment(1, Qt::AlignRight | Qt::AlignVCenter);
    }
    if (matches.isEmpty()) {
        auto *item = new QTreeWidgetItem(list_, {tr("No matching command")});
        item->setFlags(Qt::NoItemFlags);
    } else {
        list_->setCurrentItem(list_->topLevelItem(0));
    }
}

void CommandPalette::runCurrent()
{
    const QTreeWidgetItem *item = list_->currentItem();
    if (!item || !(item->flags() & Qt::ItemIsEnabled))
        return;
    const QPointer<QAction> action = commands_.value(item->data(0, Qt::UserRole).toLongLong()).action;
    dismiss();
    // After the popup has closed and given the focus back.
    QTimer::singleShot(0, this, [action] {
        if (action)
            action->trigger();
    });
}

bool CommandPalette::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == search_ && event->type() == QEvent::KeyPress) {
        const auto *key = static_cast<QKeyEvent *>(event);
        const int row = list_->currentIndex().row();
        const int count = list_->topLevelItemCount();
        int target = -1;
        switch (key->key()) {
        case Qt::Key_Down:
            target = std::min(row + 1, count - 1);
            break;
        case Qt::Key_Up:
            target = std::max(row - 1, 0);
            break;
        case Qt::Key_PageDown:
            target = std::min(row + visibleRows, count - 1);
            break;
        case Qt::Key_PageUp:
            target = std::max(row - visibleRows, 0);
            break;
        case Qt::Key_Escape:
            dismiss();
            return true;
        default:
            return QFrame::eventFilter(watched, event);
        }
        if (target >= 0 && (list_->topLevelItem(target)->flags() & Qt::ItemIsEnabled))
            list_->setCurrentItem(list_->topLevelItem(target));
        return true;
    }
    return QFrame::eventFilter(watched, event);
}
