#include "DiffHeaderBar.h"

#include "DiffView.h"
#include "ui/AppFonts.h"
#include "ui/UiIcons.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QResizeEvent>
#include <QToolButton>

#include <algorithm>
#include <tuple>

DiffHeaderBar::DiffHeaderBar(DiffView *diff, QWidget *parent)
    : QFrame(parent)
    , diff_(diff)
    , name_(new QLabel(this))
    , details_(new QLabel(this))
    , stats_(new QLabel(this))
    , sideBySide_(new QToolButton(this))
    , whitespace_(new QToolButton(this))
    , previous_(new QToolButton(this))
    , next_(new QToolButton(this))
{
    setObjectName(QStringLiteral("diffHeader"));
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    AppFonts::setInterfaceFont(name_, 1, QFont::DemiBold);
    name_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    details_->setObjectName(QStringLiteral("historyMuted"));
    details_->setMinimumWidth(0);
    details_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    for (const auto &[button, toolTip, buttonDirection] :
         {std::tuple{previous_, tr("Previous change (Shift+F7)"), DiffView::Direction::Previous},
          std::tuple{next_, tr("Next change (F7)"), DiffView::Direction::Next}}) {
        button->setAutoRaise(true);
        button->setFocusPolicy(Qt::NoFocus);
        button->setToolTip(toolTip);
        const DiffView::Direction direction = buttonDirection;
        connect(button, &QToolButton::clicked, this, [this, direction] { diff_->goToChange(direction); });
    }

    sideBySide_->setCheckable(true);
    sideBySide_->setAutoRaise(true);
    sideBySide_->setFocusPolicy(Qt::NoFocus);
    sideBySide_->setToolTip(tr("Show the old and new versions side by side"));
    connect(sideBySide_, &QToolButton::toggled, this,
            [this](bool checked) { diff_->setMode(checked ? DiffView::Mode::SideBySide : DiffView::Mode::Unified); });
    whitespace_->setCheckable(true);
    whitespace_->setAutoRaise(true);
    whitespace_->setFocusPolicy(Qt::NoFocus);
    whitespace_->setToolTip(tr("Hide whitespace changes"));
    connect(whitespace_, &QToolButton::toggled, this, [this] { emit whitespaceChanged(whitespace()); });

    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(12, 4, 6, 4);
    layout->setSpacing(8);
    layout->addWidget(name_);
    layout->addWidget(details_, 1);
    layout->addWidget(stats_);
    layout->addSpacing(4);
    layout->addWidget(sideBySide_);
    layout->addWidget(whitespace_);
    layout->addWidget(previous_);
    layout->addWidget(next_);

    connect(diff_, &DiffView::contentChanged, this, &DiffHeaderBar::follow);
    // Scintilla scrolls without the scroll bar telling: its own notice.
    connect(diff_, &QsciScintillaBase::SCN_UPDATEUI, this, &DiffHeaderBar::follow);
    follow();
}

void DiffHeaderBar::setPlaceholder(const QString &text)
{
    placeholder_ = text;
    follow();
}

Whitespace DiffHeaderBar::whitespace() const
{
    return whitespace_->isChecked() ? Whitespace::Ignore : Whitespace::Compare;
}

void DiffHeaderBar::applyTheme(const Theme &theme)
{
    dark_ = theme.isDark;
    sideBySide_->setIcon(UiIcons::icon(QStringLiteral("split"), theme.foreground));
    whitespace_->setIcon(UiIcons::icon(QStringLiteral("whitespace"), theme.foreground));
    previous_->setIcon(UiIcons::icon(QStringLiteral("up"), theme.foreground));
    next_->setIcon(UiIcons::icon(QStringLiteral("down"), theme.foreground));
    follow();
}

int DiffHeaderBar::controlsHeight() const
{
    const QMargins margins = contentsMargins() + layout()->contentsMargins();
    const int buttons = std::max({sideBySide_->sizeHint().height(), whitespace_->sizeHint().height(),
                                  previous_->sizeHint().height(), next_->sizeHint().height()});
    return buttons + margins.top() + margins.bottom();
}

QSize DiffHeaderBar::sizeHint() const
{
    const QSize hint = QFrame::sizeHint();
    // Keep the toolbar's height without reserving hidden controls' width.
    return {hint.width(), std::max(hint.height(), controlsHeight())};
}

int DiffHeaderBar::heightForWidth(int width) const
{
    return std::max(QFrame::heightForWidth(width), controlsHeight());
}

void DiffHeaderBar::resizeEvent(QResizeEvent *event)
{
    QFrame::resizeEvent(event);
    if (event->size().height() != event->oldSize().height())
        emit heightChanged(event->size().height());
}

void DiffHeaderBar::follow()
{
    const QList<DiffView::FileSummary> &files = diff_->fileSummaries();
    const bool hasFile = !files.isEmpty();
    name_->setVisible(hasFile);
    details_->setWordWrap(!hasFile);
    for (QWidget *widget :
         {static_cast<QWidget *>(stats_), static_cast<QWidget *>(previous_), static_cast<QWidget *>(next_)})
        widget->setVisible(hasFile);
    // Checked, it stays: what it hides may be all there was.
    whitespace_->setVisible(hasFile || whitespace_->isChecked());
    sideBySide_->setVisible(hasFile);
    if (!hasFile) {
        name_->clear();
        details_->setText(placeholder_);
        details_->setToolTip(placeholder_);
        return;
    }
    const int file = std::max(0, diff_->locate(diff_->firstVisibleLine()).file);
    const DiffView::FileSummary &shown = files.value(file, files.first());
    const qsizetype slash = shown.path.lastIndexOf(QLatin1Char('/'));
    name_->setText(shown.path.mid(slash + 1));
    QStringList details;
    if (slash >= 0)
        details.append(shown.path.left(slash));
    if (!shown.status.isEmpty())
        details.append(shown.status);
    if (files.size() > 1)
        details.append(tr("file %1 of %2").arg(file + 1).arg(files.size()));
    details_->setText(details.join(QStringLiteral("  ·  ")));
    details_->setToolTip(shown.path);
    const QString green = dark_ ? QStringLiteral("#3fb950") : QStringLiteral("#1a7f37");
    const QString red = dark_ ? QStringLiteral("#f85149") : QStringLiteral("#cf222e");
    stats_->setText(QStringLiteral("<span style='color:%1; font-weight:600'>+%2</span>&nbsp; "
                                   "<span style='color:%3; font-weight:600'>−%4</span>")
                        .arg(green)
                        .arg(shown.added)
                        .arg(red)
                        .arg(shown.removed));
}
