#include "EditorStatus.h"

#include "EditorWidget.h"
#include "workspace/EntryPopups.h"

#include <QActionGroup>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QMenu>
#include <QToolButton>

#include <array>
#include <utility>

namespace {

// Spelled out: the status bar has room for no "(s)".
QString spaces(int count)
{
    return count == 1 ? EditorStatus::tr("1 space") : EditorStatus::tr("%1 spaces").arg(count);
}

} // namespace

EditorStatus::EditorStatus(QWidget *parent)
    : QWidget(parent)
{
    position_ = addButton(tr("Go to Line:Column (Ctrl+G)"));
    connect(position_, &QToolButton::clicked, this, &EditorStatus::goToLine);
    lineEnding_ = addButton(tr("Line Separator"));
    connect(lineEnding_, &QToolButton::clicked, this, &EditorStatus::showLineEndingMenu);
    encoding_ = addButton(tr("File Encoding"));
    indentation_ = addButton(tr("Indentation"));
    connect(indentation_, &QToolButton::clicked, this, &EditorStatus::showIndentationMenu);

    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(2);
    for (QToolButton *button : {position_, lineEnding_, encoding_, indentation_})
        layout->addWidget(button);
    hide();
}

QToolButton *EditorStatus::addButton(const QString &toolTip)
{
    auto *button = new QToolButton(this);
    button->setAutoRaise(true);
    button->setFocusPolicy(Qt::NoFocus);
    button->setToolTip(toolTip);
    return button;
}

void EditorStatus::setEditor(EditorWidget *editor)
{
    if (editor_)
        disconnect(editor_, nullptr, this, nullptr);
    // Diffs and other read-only texts have no file to describe.
    editor_ = editor && !editor->filePath().isEmpty() ? editor : nullptr;
    if (editor_) {
        connect(editor_, &QsciScintilla::cursorPositionChanged, this, &EditorStatus::scheduleUpdate);
        connect(editor_, &QsciScintilla::selectionChanged, this, &EditorStatus::scheduleUpdate);
        connect(editor_, &EditorWidget::formatChanged, this, &EditorStatus::update);
    }
    setVisible(editor_ != nullptr);
    update();
}

QString EditorStatus::positionText() const
{
    if (!editor_)
        return {};
    int line = 0;
    int index = 0;
    editor_->getCursorPosition(&line, &index);
    // Column in characters, not in bytes: tabs count as one, like IntelliJ.
    const QString before =
        editor_->text(static_cast<int>(editor_->SendScintilla(QsciScintillaBase::SCI_POSITIONFROMLINE, line)),
                      static_cast<int>(editor_->SendScintilla(QsciScintillaBase::SCI_GETCURRENTPOS)));
    QString text = QStringLiteral("%1:%2").arg(line + 1).arg(before.size() + 1);
    if (editor_->hasSelectedText()) {
        // A CRLF line break counts as one character.
        const QString selected = editor_->selectedText().replace(QLatin1String("\r\n"), QLatin1String("\n"));
        const qsizetype breaks = selected.count(QLatin1Char('\n'));
        const QString chars = selected.size() == 1 ? tr("1 char") : tr("%1 chars").arg(selected.size());
        if (breaks == 0)
            text += tr(" (%1)").arg(chars);
        else if (breaks == 1)
            text += tr(" (%1, 1 line break)").arg(chars);
        else
            text += tr(" (%1, %2 line breaks)").arg(chars).arg(breaks);
    }
    return text;
}

QString EditorStatus::lineEndingText() const
{
    if (!editor_)
        return {};
    switch (editor_->format().lineEnding) {
    case LineEnding::CrLf:
        return QStringLiteral("CRLF");
    case LineEnding::Cr:
        return QStringLiteral("CR");
    case LineEnding::Lf:
        break;
    }
    return QStringLiteral("LF");
}

QString EditorStatus::encodingText() const
{
    if (!editor_)
        return {};
    const TextFileFormat format = editor_->format();
    if (format.isLatin1)
        return QStringLiteral("ISO-8859-1");
    return format.hasUtf8Bom ? QStringLiteral("UTF-8 BOM") : QStringLiteral("UTF-8");
}

QString EditorStatus::indentationText() const
{
    if (!editor_)
        return {};
    return editor_->indentationsUseTabs() ? tr("Tab") : spaces(editor_->tabWidth());
}

void EditorStatus::scheduleUpdate()
{
    if (std::exchange(updateScheduled_, true))
        return;
    QMetaObject::invokeMethod(
        this,
        [this] {
            updateScheduled_ = false;
            update();
        },
        Qt::QueuedConnection);
}

void EditorStatus::update()
{
    position_->setText(positionText());
    lineEnding_->setText(lineEndingText());
    encoding_->setText(encodingText());
    indentation_->setText(indentationText());
    const bool editable = editor_ && !editor_->isReadOnly();
    lineEnding_->setEnabled(editable);
    indentation_->setEnabled(editable);
}

std::optional<std::pair<int, int>> EditorStatus::parseLineColumn(const QString &text)
{
    const QStringList parts = text.trimmed().split(QLatin1Char(':'));
    if (parts.size() > 2)
        return std::nullopt;
    bool lineOk = false;
    const int line = parts[0].trimmed().toInt(&lineOk);
    if (!lineOk || line < 1)
        return std::nullopt;
    int column = 1;
    if (parts.size() == 2 && !parts[1].trimmed().isEmpty()) {
        bool columnOk = false;
        column = parts[1].trimmed().toInt(&columnOk);
        if (!columnOk || column < 1)
            return std::nullopt;
    }
    return std::pair{line - 1, column - 1};
}

void EditorStatus::goToLine()
{
    if (!editor_)
        return;
    QPointer<EditorWidget> editor = editor_;
    auto *popup = new NamePopup(
        tr("Go to Line:Column"), QFileInfo(editor->filePath()).fileName(),
        [editor](const QString &text) -> QString {
            const auto target = parseLineColumn(text);
            if (!target)
                return tr("Type a line number, optionally followed by “:column”");
            if (!editor)
                return {};
            const int line = std::min(target->first, editor->lines() - 1);
            // QScintilla counts the column in characters.
            QString lineText = editor->text(line);
            while (lineText.endsWith(QLatin1Char('\n')) || lineText.endsWith(QLatin1Char('\r')))
                lineText.chop(1);
            editor->setCursorPosition(line, std::min(target->second, static_cast<int>(lineText.size())));
            editor->ensureLineVisible(line);
            editor->SendScintilla(QsciScintillaBase::SCI_VERTICALCENTRECARET);
            editor->setFocus();
            return {};
        },
        editor);
    int line = 0;
    int index = 0;
    editor->getCursorPosition(&line, &index);
    popup->field()->setText(positionText().section(QLatin1Char(' '), 0, 0));
    popup->field()->selectAll();
    popup->field()->setPlaceholderText(QString::number(line + 1));
    popup->popup();
}

void EditorStatus::showLineEndingMenu()
{
    if (!editor_)
        return;
    QMenu menu(this);
    auto *group = new QActionGroup(&menu);
    const std::array<std::pair<LineEnding, QString>, 3> choices{{{LineEnding::Lf, tr("LF - Unix and macOS (\\n)")},
                                                                 {LineEnding::CrLf, tr("CRLF - Windows (\\r\\n)")},
                                                                 {LineEnding::Cr, tr("CR - Classic Mac OS (\\r)")}}};
    for (const auto &[lineEnding, name] : choices) {
        QAction *action = menu.addAction(name);
        action->setCheckable(true);
        action->setChecked(editor_->format().lineEnding == lineEnding);
        group->addAction(action);
        connect(action, &QAction::triggered, this, [this, lineEnding] {
            if (editor_)
                editor_->setLineEnding(lineEnding);
        });
    }
    menu.exec(lineEnding_->mapToGlobal(QPoint(0, 0)) - QPoint(0, menu.sizeHint().height()));
}

void EditorStatus::showIndentationMenu()
{
    if (!editor_)
        return;
    QMenu menu(this);
    auto *group = new QActionGroup(&menu);
    const auto addChoice = [&](const QString &name, bool useTabs, int width) {
        QAction *action = menu.addAction(name);
        action->setCheckable(true);
        action->setChecked(editor_->indentationsUseTabs() == useTabs && (useTabs || editor_->tabWidth() == width));
        group->addAction(action);
        connect(action, &QAction::triggered, this, [this, useTabs, width] {
            if (editor_)
                editor_->setIndentationStyle(useTabs, useTabs ? editor_->tabWidth() : width);
        });
    };
    for (const int width : {2, 4, 8})
        addChoice(spaces(width), false, width);
    addChoice(tr("Tab"), true, 0);
    menu.exec(indentation_->mapToGlobal(QPoint(0, 0)) - QPoint(0, menu.sizeHint().height()));
}
