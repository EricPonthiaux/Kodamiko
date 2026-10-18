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

namespace {

enum Marker {
    AddedMarker = 20,
    ModifiedMarker,
    DeletedMarker
};
constexpr int markerMask = (1 << AddedMarker) | (1 << ModifiedMarker) | (1 << DeletedMarker);
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
    const QPointF points[] = {{0, 0}, {6, 0}, {0, 6}};
    painter.drawPolygon(points, 3);
    return image;
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
    std::optional<QByteArray> base;
    if (!editor_->filePath().isEmpty()) {
        try {
            base = LineChanges::indexVersion(editor_->filePath());
        } catch (const GitError &) {
            // Unreadable repository: no marks rather than an error at every
            // keystroke.
        }
    }
    setBase(base);
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
        try {
            // No copy: the diff is computed before the document can change.
            const QByteArrayView text = editor_->utf8Bytes();
            changes_ = LineChanges::compute(*base_, QByteArray::fromRawData(text.data(), text.size()));
        } catch (const GitError &) {
        }
    }

    editor_->markerDeleteAll(AddedMarker);
    editor_->markerDeleteAll(ModifiedMarker);
    editor_->markerDeleteAll(DeletedMarker);
    if (changes_.isEmpty())
        return;
    defineMarkers();
    const int lineCount = editor_->lines();
    for (const LineChange &change : std::as_const(changes_)) {
        switch (change.kind()) {
        case LineChange::Kind::Added:
        case LineChange::Kind::Modified:
            for (int line = change.start; line < change.start + change.count; ++line)
                editor_->markerAdd(line, change.kind() == LineChange::Kind::Added ? AddedMarker : ModifiedMarker);
            break;
        case LineChange::Kind::Deleted:
            editor_->markerAdd(std::min(change.start, lineCount - 1), DeletedMarker);
            break;
        }
    }
}

void ChangeGutter::defineMarkers()
{
    const ChangeColors colors = colorsFor(theme_);
    editor_->markerDefine(QsciScintilla::LeftRectangle, AddedMarker);
    editor_->setMarkerBackgroundColor(colors.added, AddedMarker);
    editor_->markerDefine(QsciScintilla::LeftRectangle, ModifiedMarker);
    editor_->setMarkerBackgroundColor(colors.modified, ModifiedMarker);
    const int lineHeight = static_cast<int>(editor_->SendScintilla(QsciScintillaBase::SCI_TEXTHEIGHT, 0));
    editor_->markerDefine(deletedMark(colors.deleted, lineHeight), DeletedMarker);
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
    editor_->SendScintilla(QsciScintillaBase::SCI_REPLACETARGET, static_cast<unsigned long>(replacement.size()),
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
    const auto markerLine = [lineCount](const LineChange &change) { return std::min(change.start, lineCount - 1); };
    const LineChange *target = nullptr;
    if (forward) {
        for (const LineChange &change : all) {
            if (markerLine(change) > line && !change.covers(line, lineCount)) {
                target = &change;
                break;
            }
        }
        if (!target)
            target = &all.first();
    } else {
        for (auto it = all.crbegin(); it != all.crend(); ++it) {
            if (markerLine(*it) < line && !it->covers(line, lineCount)) {
                target = &*it;
                break;
            }
        }
        if (!target)
            target = &all.last();
    }
    editor_->setCursorPosition(markerLine(*target), 0);
    editor_->ensureLineVisible(markerLine(*target));
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
    if (popup_)
        popup_->close();

    auto *popup = new QFrame(editor_, Qt::Popup);
    popup->setAttribute(Qt::WA_DeleteOnClose);
    popup->setFrameShape(QFrame::StyledPanel);
    popup_ = popup;

    const auto addButton = [popup](QHBoxLayout *layout, const QString &text, const QString &toolTip) {
        auto *button = new QToolButton(popup);
        button->setText(text);
        button->setToolTip(toolTip);
        button->setAutoRaise(true);
        layout->addWidget(button);
        return button;
    };
    auto *toolbar = new QHBoxLayout;
    toolbar->setContentsMargins(0, 0, 0, 0);
    toolbar->setSpacing(2);
    QToolButton *previous = addButton(toolbar, QStringLiteral("↑"), tr("Previous Change (Ctrl+Alt+Shift+Up)"));
    QToolButton *next = addButton(toolbar, QStringLiteral("↓"), tr("Next Change (Ctrl+Alt+Shift+Down)"));
    QToolButton *rollbackButton = addButton(toolbar, tr("Rollback"), tr("Rollback Lines (Ctrl+Alt+Z)"));
    rollbackButton->setEnabled(!editor_->isReadOnly());
    QToolButton *showDiff = addButton(toolbar, tr("Show Diff"), tr("Show this change in a diff tab"));
    toolbar->addStretch();
    const int lines = change.kind() == LineChange::Kind::Deleted ? change.baseCount : change.count;
    QString summary;
    switch (change.kind()) {
    case LineChange::Kind::Added:
        summary = lines == 1 ? tr("1 added line") : tr("%1 added lines").arg(lines);
        break;
    case LineChange::Kind::Deleted:
        summary = lines == 1 ? tr("1 deleted line") : tr("%1 deleted lines").arg(lines);
        break;
    case LineChange::Kind::Modified:
        summary = lines == 1 ? tr("1 modified line") : tr("%1 modified lines").arg(lines);
        break;
    }
    auto *label = new QLabel(summary, popup);
    label->setEnabled(false);
    toolbar->addWidget(label);

    auto *layout = new QVBoxLayout(popup);
    layout->setContentsMargins(6, 4, 6, 6);
    layout->setSpacing(4);
    layout->addLayout(toolbar);

    if (change.baseCount > 0) {
        // The lines as they are in the index.
        auto *previousText = new QPlainTextEdit(popup);
        previousText->setReadOnly(true);
        previousText->setLineWrapMode(QPlainTextEdit::NoWrap);
        previousText->setFont(theme_.font);
        QString lines = QString::fromUtf8(LineChanges::lines(*base_, change.baseStart, change.baseCount));
        lines.remove(QLatin1Char('\r'));
        if (lines.endsWith(QLatin1Char('\n')))
            lines.chop(1);
        previousText->setPlainText(lines);
        QPalette palette = previousText->palette();
        const QColor deleted = colorsFor(theme_).deleted;
        palette.setColor(QPalette::Base, theme_.isDark ? deleted.darker(300) : deleted.lighter(160));
        palette.setColor(QPalette::Text, theme_.foreground);
        previousText->setPalette(palette);
        const QFontMetrics metrics(theme_.font);
        const int shown = std::min(change.baseCount, popupMaxLines);
        previousText->setFixedHeight(metrics.lineSpacing() * shown + 2 * previousText->frameWidth() +
                                     static_cast<int>(2 * previousText->document()->documentMargin()) + 2);
        int widest = 0;
        for (const QString &text : lines.split(QLatin1Char('\n')))
            widest = std::max(widest, metrics.horizontalAdvance(text));
        previousText->setMinimumWidth(std::min(widest + 30, editor_->viewport()->width() - 40));
        layout->addWidget(previousText);
    }

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

    // Under the change's last line, aligned with the text.
    const int lineCount = editor_->lines();
    const int markerLine = std::min(change.start, lineCount - 1);
    const int belowLine = change.count > 0 ? change.start + change.count - 1 : markerLine;
    const long position = editor_->SendScintilla(QsciScintillaBase::SCI_POSITIONFROMLINE, belowLine);
    const int y = static_cast<int>(editor_->SendScintilla(QsciScintillaBase::SCI_POINTYFROMPOSITION, 0, position)) +
                  static_cast<int>(editor_->SendScintilla(QsciScintillaBase::SCI_TEXTHEIGHT, belowLine));
    const QPoint origin = editor_->viewport()->mapToGlobal(QPoint(0, y));
    popup->adjustSize();
    popup->move(origin);
    popup->show();
}

void ChangeGutter::applyTheme(const Theme &theme)
{
    theme_ = theme;
    if (!changes_.isEmpty())
        defineMarkers();
}
