// SPDX-License-Identifier: GPL-3.0-only

#include "ConflictActionBar.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QToolButton>

namespace {

// Between the links, a little apart from the marker's text.
constexpr int linkSpacingPx = 2;
constexpr int marginLeftPx = 12;
// A little smaller than the code, to read as the editor's, not the file's.
constexpr qreal linkFontScale = 0.9;

} // namespace

ConflictActionBar::ConflictActionBar(const Theme &theme, QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(marginLeftPx, 0, 0, 0);
    layout->setSpacing(linkSpacingPx);
    current_ = addLink(tr("Accept Current"), ConflictMarkers::Resolution::Current);
    incoming_ = addLink(tr("Accept Incoming"), ConflictMarkers::Resolution::Incoming);
    both_ = addLink(tr("Accept Both"), ConflictMarkers::Resolution::Both);
    current_->setToolTip(tr("Keep the lines of the branch checked out"));
    incoming_->setToolTip(tr("Keep the lines brought in"));
    both_->setToolTip(tr("Keep the current lines, then the incoming ones"));
    applyTheme(theme);
}

QToolButton *ConflictActionBar::addLink(const QString &text, ConflictMarkers::Resolution resolution)
{
    if (layout()->count() > 0) {
        auto *separator = new QLabel(QStringLiteral("·"), this);
        separator->setObjectName(QStringLiteral("conflictLinkSeparator"));
        layout()->addWidget(separator);
    }
    auto *link = new QToolButton(this);
    link->setText(text);
    link->setAutoRaise(true);
    link->setCursor(Qt::PointingHandCursor);
    link->setFocusPolicy(Qt::NoFocus); // the editor keeps the keyboard
    layout()->addWidget(link);
    connect(link, &QToolButton::clicked, this, [this, resolution] { emit resolutionChosen(resolution); });
    return link;
}

void ConflictActionBar::applyTheme(const Theme &theme)
{
    QFont font = theme.font;
    font.setPointSizeF(font.pointSizeF() * linkFontScale);
    setFont(font);
    setStyleSheet(QStringLiteral("QToolButton { border: none; padding: 0 2px; color: %1; background: transparent; }"
                                 "QToolButton:hover { text-decoration: underline; }"
                                 "QLabel#conflictLinkSeparator { color: %2; }")
                      .arg(theme.accent.name(), theme.mutedText.name()));
    adjustSize();
}

QToolButton *ConflictActionBar::button(ConflictMarkers::Resolution resolution) const
{
    switch (resolution) {
    case ConflictMarkers::Resolution::Current:
        return current_;
    case ConflictMarkers::Resolution::Incoming:
        return incoming_;
    case ConflictMarkers::Resolution::Both:
        return both_;
    }
    return current_;
}
