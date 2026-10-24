// SPDX-License-Identifier: GPL-3.0-only

#include "ChangeGutter.h"

#include "EditorWidget.h"
#include "TextFile.h"
#include "git/FileChange.h"

#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPlainTextEdit>
#include <QToolButton>
#include <QVBoxLayout>

#include <array>
#include <ranges>

namespace {

// Scintilla marker numbers.
constexpr int addedMarker = 20;
constexpr int modifiedMarker = 21;
constexpr int deletedMarker = 22;

constexpr int markerMask = (1 << addedMarker) | (1 << modifiedMarker) | (1 << deletedMarker);
constexpr int marginWidth = 8;
constexpr int updateDelayMs = 250;
constexpr int popupMaxLines = 15;

struct ChangeColors {
    QColor added;
    QColor modified;
    QColor deleted;
};

ChangeColors colorsFor(const Theme &theme)
{
    if (theme.isDark)
        return {QColor("#5f8a4f"), QColor("#4f7aa6"), QColor("#b0585f")};
    return {QColor("#8cc47e"), QColor("#8bb0e0"), QColor("#d27b80")};
}

// Small triangle at the top of the line: the deleted lines were above it.
QImage deletedMark(const QColor &color, int lineHeight)
{
    const qreal ratio = 2;
    QImage image(QSize(marginWidth, std::max(lineHeight, 8)) * ratio, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.scale(ratio, ratio);
    painter.setPen(Qt::NoPen);
    painter.setBrush(color);
    const std::array<QPointF, 3> points = {QPointF(0, 0), QPointF(6, 0), QPointF(0, 6)};
    painter.drawPolygon(points.data(), int(points.size()));
    return image;
}

// The file's version in the index, or none when the repository is unreadable:
// no marks rather than an error at every keystroke.
std::optional<QByteArray> readIndexVersion(const QString &filePath)
{
    try {
        return LineChanges::indexVersion(filePath);
    } catch (const GitError &) {
        return std::nullopt;
    }
}

QList<LineChange> computeChanges(const QByteArray &base, const QByteArray &text)
{
    try {
        return LineChanges::compute(base, text);
    } catch (const GitError &) {
        return {};
    }
}

// The line that carries a change's mark: a deletion at the end of the file is
// marked on the last line.
int markerLine(const LineChange &change, int lineCount)
{
    return std::min(change.start, lineCount - 1);
}

// The first change after `line`, wrapping around to the first one.
const LineChange &nextChange(const QList<LineChange> &all, int line, int lineCount)
{
    const auto isAfter = [line, lineCount](const LineChange &change) {
        return markerLine(change, lineCount) > line && !change.covers(line, lineCount);
    };
    const auto found = std::ranges::find_if(all, isAfter);
    return found != all.end() ? *found : all.first();
}

// The last change before `line`, wrapping around to the last one.
const LineChange &previousChange(const QList<LineChange> &all, int line, int lineCount)
{
    const auto isBefore = [line, lineCount](const LineChange &change) {
        return markerLine(change, lineCount) < line && !change.covers(line, lineCount);
    };
    const auto reversed = std::views::reverse(all);
    const auto found = std::ranges::find_if(reversed, isBefore);
    return found != reversed.end() ? *found : all.last();
}

QString changeSummary(const LineChange &change)
{
    const int lines = change.kind() == LineChange::Kind::Deleted ? change.baseCount : change.count;
    switch (change.kind()) {
    case LineChange::Kind::Added:
        return lines == 1 ? ChangeGutter::tr("1 added line") : ChangeGutter::tr("%1 added lines").arg(lines);
    case LineChange::Kind::Deleted:
        return lines == 1 ? ChangeGutter::tr("1 deleted line") : ChangeGutter::tr("%1 deleted lines").arg(lines);
    case LineChange::Kind::Modified:
        return lines == 1 ? ChangeGutter::tr("1 modified line") : ChangeGutter::tr("%1 modified lines").arg(lines);
    }
    return {};
}

QToolButton *addToolButton(QWidget *parent, QHBoxLayout *layout, const QString &text, const QString &toolTip)
{
    auto *button = new QToolButton(parent);
    button->setText(text);
    button->setToolTip(toolTip);
    button->setAutoRaise(true);
    layout->addWidget(button);
    return button;
}

// What a popup needs to show the lines of a change as they are in the index.
struct BaseLinesStyle {
    const Theme &theme;
    int maxWidth = 0;
};

// The lines as they are in the index, on the background of deleted lines.
QPlainTextEdit *createBaseLinesView(const QString &baseText,
                                    int lineCount,
                                    const BaseLinesStyle &style,
                                    QWidget *parent)
{
    auto *view = new QPlainTextEdit(parent);
    view->setReadOnly(true);
    view->setLineWrapMode(QPlainTextEdit::NoWrap);
    view->setFont(style.theme.font);
    view->setPlainText(baseText);
    QPalette palette = view->palette();
    const QColor deleted = colorsFor(style.theme).deleted;
    palette.setColor(QPalette::Base, style.theme.isDark ? deleted.darker(300) : deleted.lighter(160));
    palette.setColor(QPalette::Text, style.theme.foreground);
    view->setPalette(palette);
    const QFontMetrics metrics(style.theme.font);
    const int shown = std::min(lineCount, popupMaxLines);
    view->setFixedHeight(metrics.lineSpacing() * shown + 2 * view->frameWidth() +
                         static_cast<int>(2 * view->document()->documentMargin()) + 2);
    int widest = 0;
    for (const QString &text : baseText.split(QLatin1Char('\n')))
        widest = std::max(widest, metrics.horizontalAdvance(text));
    view->setMinimumWidth(std::min(widest + 30, style.maxWidth));
    return view;
}

} // namespace

ChangeGutter::ChangeGutter(EditorWidget *editor)
    : QObject(editor)
    , editor_(editor)
{
    timer_.setSingleShot(true);
    timer_.setInterval(updateDelayMs);
    connect(&timer_, &QTimer::timeout, this, &ChangeGutter::update);
    connect(editor_, &QsciScintilla::textChanged, this, &ChangeGutter::scheduleUpdate);
    connect(editor_, &QsciScintilla::marginClicked, this, [this](int clickedMargin, int line, Qt::KeyboardModifiers) {
        if (clickedMargin == margin)
            showChangeAt(line);
    });

    editor_->setMarginType(margin, QsciScintilla::SymbolMargin);
    editor_->setMarginMarkerMask(margin, markerMask);
    editor_->setMarginSensitivity(margin, true);
    editor_->setMarginWidth(margin, 0);
}

void ChangeGutter::reloadBase()
{
    if (editor_->filePath().isEmpty()) {
        setBase(std::nullopt);
        return;
    }
    setBase(readIndexVersion(editor_->filePath()));
}

void ChangeGutter::setBase(const std::optional<QByteArray> &base)
{
    if (base)
        base_ = TextFile::decode(*base).text.toUtf8();
    else
        base_.reset();
    editor_->setMarginWidth(margin, base_ ? marginWidth : 0);
    update();
}

void ChangeGutter::scheduleUpdate()
{
    upToDate_ = false;
    if (base_)
        timer_.start();
}

void ChangeGutter::update()
{
    timer_.stop();
    upToDate_ = true;
    changes_.clear();
    if (base_) {
        // No copy: the diff is computed before the document can change.
        const QByteArrayView text = editor_->utf8Bytes();
        changes_ = computeChanges(*base_, QByteArray::fromRawData(text.data(), text.size()));
    }

    editor_->markerDeleteAll(addedMarker);
    editor_->markerDeleteAll(modifiedMarker);
    editor_->markerDeleteAll(deletedMarker);
    if (changes_.isEmpty())
        return;
    defineMarkers();
    const int lineCount = editor_->lines();
    for (const LineChange &change : std::as_const(changes_)) {
        switch (change.kind()) {
        case LineChange::Kind::Added:
        case LineChange::Kind::Modified:
            for (int line = change.start; line < change.start + change.count; ++line)
                editor_->markerAdd(line, change.kind() == LineChange::Kind::Added ? addedMarker : modifiedMarker);
            break;
        case LineChange::Kind::Deleted:
            editor_->markerAdd(markerLine(change, lineCount), deletedMarker);
            break;
        }
    }
}

void ChangeGutter::defineMarkers()
{
    const ChangeColors colors = colorsFor(theme_);
    editor_->markerDefine(QsciScintilla::LeftRectangle, addedMarker);
    editor_->setMarkerBackgroundColor(colors.added, addedMarker);
    editor_->markerDefine(QsciScintilla::LeftRectangle, modifiedMarker);
    editor_->setMarkerBackgroundColor(colors.modified, modifiedMarker);
    const int lineHeight = static_cast<int>(editor_->SendScintilla(QsciScintillaBase::SCI_TEXTHEIGHT, 0));
    editor_->markerDefine(deletedMark(colors.deleted, lineHeight), deletedMarker);
}

QList<LineChange> ChangeGutter::changes()
{
    if (!upToDate_)
        update();
    return changes_;
}

std::optional<LineChange> ChangeGutter::changeAt(int line)
{
    const int lineCount = editor_->lines();
    for (const LineChange &change : changes()) {
        if (change.covers(line, lineCount))
            return change;
    }
    return std::nullopt;
}

void ChangeGutter::rollback(const LineChange &change)
{
    if (!base_ || editor_->isReadOnly())
        return;
    const QByteArray text = editor_->text().toUtf8();
    const qsizetype from = LineChanges::lineOffset(text, change.start);
    const qsizetype to = LineChanges::lineOffset(text, change.start + change.count);
    const QByteArray replacement = LineChanges::lines(*base_, change.baseStart, change.baseCount);

    editor_->beginUndoAction();
    editor_->SendScintilla(QsciScintillaBase::SCI_SETTARGETRANGE, static_cast<unsigned long>(from),
                           static_cast<long>(to));
    editor_->SendScintilla(QsciScintillaBase::SCI_REPLACETARGET, uintptr_t(replacement.size()),
                           replacement.constData());
    editor_->endUndoAction();
    editor_->setCursorPosition(std::min(change.start, editor_->lines() - 1), 0);
    if (popup_)
        popup_->close();
    update();
}

bool ChangeGutter::rollbackAtCursor()
{
    const std::optional<LineChange> change = changeAt(cursorLine());
    if (!change)
        return false;
    rollback(*change);
    return true;
}

bool ChangeGutter::goToNext()
{
    return goTo(true);
}

bool ChangeGutter::goToPrevious()
{
    return goTo(false);
}

bool ChangeGutter::goTo(bool forward)
{
    const QList<LineChange> all = changes();
    if (all.isEmpty())
        return false;
    const int line = cursorLine();
    const int lineCount = editor_->lines();
    const LineChange &target = forward ? nextChange(all, line, lineCount) : previousChange(all, line, lineCount);
    const int targetLine = markerLine(target, lineCount);
    editor_->setCursorPosition(targetLine, 0);
    editor_->ensureLineVisible(targetLine);
    editor_->SendScintilla(QsciScintillaBase::SCI_VERTICALCENTRECARET);
    return true;
}

int ChangeGutter::cursorLine() const
{
    int line = 0;
    int index = 0;
    editor_->getCursorPosition(&line, &index);
    return line;
}

QString ChangeGutter::diffText(const LineChange &change) const
{
    if (!base_)
        return {};
    const QByteArray text = editor_->text().toUtf8();
    const auto side = [](int start, int count) {
        // Unified diff numbering: from 1, the preceding line for an empty side.
        return QStringLiteral("%1,%2").arg(count > 0 ? start + 1 : start).arg(count);
    };
    QString diff = QStringLiteral("@@ -%1 +%2 @@\n")
                       .arg(side(change.baseStart, change.baseCount), side(change.start, change.count));
    const auto appendLines = [&diff](const QByteArray &lines, QChar prefix) {
        if (lines.isEmpty())
            return;
        QString decoded = QString::fromUtf8(lines);
        decoded.remove(QLatin1Char('\r'));
        if (decoded.endsWith(QLatin1Char('\n')))
            decoded.chop(1);
        for (const QString &line : decoded.split(QLatin1Char('\n')))
            diff += prefix + line + QLatin1Char('\n');
    };
    appendLines(LineChanges::lines(*base_, change.baseStart, change.baseCount), QLatin1Char('-'));
    appendLines(LineChanges::lines(text, change.start, change.count), QLatin1Char('+'));
    return diff;
}

void ChangeGutter::showChangeAt(int line)
{
    const std::optional<LineChange> found = changeAt(line);
    if (!found || !base_)
        return;
    const LineChange change = *found;
    const QByteArray base = *base_; // implicitly shared, no copy of the text
    if (popup_)
        popup_->close();

    auto *popup = new QFrame(editor_, Qt::Popup);
    popup->setAttribute(Qt::WA_DeleteOnClose);
    // Framed as the other popups (ThemeStyle), its corners on nothing.
    popup->setObjectName(QStringLiteral("changePopup"));
    popup->setAttribute(Qt::WA_TranslucentBackground);
    popup_ = popup;

    auto *layout = new QVBoxLayout(popup);
    layout->setContentsMargins(6, 4, 6, 6);
    layout->setSpacing(4);
    layout->addLayout(createPopupToolbar(popup, change));
    if (change.baseCount > 0) {
        QString baseText = QString::fromUtf8(LineChanges::lines(base, change.baseStart, change.baseCount));
        baseText.remove(QLatin1Char('\r'));
        if (baseText.endsWith(QLatin1Char('\n')))
            baseText.chop(1);
        const BaseLinesStyle style{.theme = theme_, .maxWidth = editor_->viewport()->width() - 40};
        layout->addWidget(createBaseLinesView(baseText, change.baseCount, style, popup));
    }

    popup->adjustSize();
    popup->move(popupOrigin(change));
    popup->show();
}

QHBoxLayout *ChangeGutter::createPopupToolbar(QFrame *popup, const LineChange &change)
{
    auto *toolbar = new QHBoxLayout;
    toolbar->setContentsMargins(0, 0, 0, 0);
    toolbar->setSpacing(2);
    const QToolButton *previous =
        addToolButton(popup, toolbar, QStringLiteral("↑"), tr("Previous Change (Ctrl+Alt+Shift+Up)"));
    const QToolButton *next =
        addToolButton(popup, toolbar, QStringLiteral("↓"), tr("Next Change (Ctrl+Alt+Shift+Down)"));
    QToolButton *rollbackButton = addToolButton(popup, toolbar, tr("Rollback"), tr("Rollback Lines (Ctrl+Alt+Z)"));
    rollbackButton->setEnabled(!editor_->isReadOnly());
    const QToolButton *showDiff = addToolButton(popup, toolbar, tr("Show Diff"), tr("Show this change in a diff tab"));
    toolbar->addStretch();
    auto *label = new QLabel(changeSummary(change), popup);
    label->setObjectName(QStringLiteral("changeSummary"));
    toolbar->addWidget(label);

    connect(previous, &QToolButton::clicked, this, [this] {
        if (goToPrevious())
            showChangeAt(cursorLine());
    });
    connect(next, &QToolButton::clicked, this, [this] {
        if (goToNext())
            showChangeAt(cursorLine());
    });
    connect(rollbackButton, &QToolButton::clicked, this, [this, change] { rollback(change); });
    connect(showDiff, &QToolButton::clicked, this, [this, change, popup] {
        const QString diff = diffText(change);
        popup->close();
        emit diffRequested(tr("Change in %1").arg(QFileInfo(editor_->filePath()).fileName()), diff);
    });
    return toolbar;
}

QPoint ChangeGutter::popupOrigin(const LineChange &change) const
{
    // Under the change's last line, aligned with the text.
    const int belowLine = change.count > 0 ? change.start + change.count - 1 : markerLine(change, editor_->lines());
    const long position = editor_->SendScintilla(QsciScintillaBase::SCI_POSITIONFROMLINE, belowLine);
    const int y = static_cast<int>(editor_->SendScintilla(QsciScintillaBase::SCI_POINTYFROMPOSITION, 0, position)) +
                  static_cast<int>(editor_->SendScintilla(QsciScintillaBase::SCI_TEXTHEIGHT, belowLine));
    return editor_->viewport()->mapToGlobal(QPoint(0, y));
}

void ChangeGutter::applyTheme(const Theme &theme)
{
    theme_ = theme;
    if (!changes_.isEmpty())
        defineMarkers();
}
