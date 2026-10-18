#include "RunConfigurationMenu.h"

#include "ui/ThemeStyle.h"
#include "ui/UiIcons.h"

#include <QDir>
#include <QLabel>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QWidgetAction>

#include <utility>

namespace {

constexpr int rowHeight = 46;
constexpr int rowWidth = 340;
constexpr int iconSize = 16;
constexpr int tileSize = 28;
const QColor runningColor("#4fa36b");

QString iconOf(RunConfiguration::Kind kind)
{
    switch (kind) {
    case RunConfiguration::Kind::Build:
        return QStringLiteral("build");
    case RunConfiguration::Kind::Test:
        return QStringLiteral("test");
    case RunConfiguration::Kind::Run:
        break;
    }
    return QStringLiteral("run");
}

// What a configuration row shows.
struct RowContent {
    RunConfiguration configuration;
    bool isSelected = false;
    bool isRunning = false;
    Theme theme;
};

// One configuration. Highlighted under the mouse and when the keyboard
// makes its action the menu's active one.
class ConfigurationRow : public QWidget {
public:
    ConfigurationRow(QWidgetAction *action, QMenu *menu, RowContent content)
        : QWidget(menu)
        , action_(action)
        , menu_(menu)
        , content_(std::move(content))
    {
        setMouseTracking(true);
        setFixedHeight(rowHeight);
        setMinimumWidth(rowWidth);
        QString toolTip = content_.configuration.command;
        if (!content_.configuration.folder.isEmpty())
            toolTip += QObject::tr("\nin %1").arg(QDir::toNativeSeparators(content_.configuration.folder));
        setToolTip(toolTip);
        QObject::connect(menu, &QMenu::hovered, this, [this] { update(); });
    }

protected:
    void enterEvent(QEnterEvent *) override
    {
        menu_->setActiveAction(action_);
        update();
    }

    void leaveEvent(QEvent *) override { update(); }

    void mouseReleaseEvent(QMouseEvent *event) override
    {
        if (event->button() != Qt::LeftButton || !rect().contains(event->position().toPoint()))
            return;
        // Closes the whole menu chain, then chooses.
        for (QWidget *widget = menu_; qobject_cast<QMenu *>(widget); widget = widget->parentWidget())
            widget->hide();
        action_->trigger();
    }

    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const bool highlighted = menu_->activeAction() == action_ || underMouse();
        if (highlighted)
            paintHighlight(painter);
        const QRectF tile(10, (height() - tileSize) / 2.0, tileSize, tileSize);
        paintTile(painter, tile);
        const int right = paintState(painter);
        paintText(painter, {int(tile.right()) + 12, right});
    }

private:
    // The horizontal span the name and command are drawn in.
    struct TextSpan {
        int left = 0;
        int right = 0;
    };

    void paintHighlight(QPainter &painter) const
    {
        const QRectF frame = QRectF(rect()).adjusted(0, 1, 0, -1);
        QPainterPath path;
        path.addRoundedRect(frame, 2, 2);
        painter.fillPath(path, ThemeStyle::selectionColor(content_.theme));
    }

    QColor tileColor() const
    {
        QColor color = content_.isRunning ? runningColor : content_.theme.accent;
        color.setAlpha(content_.isRunning ? 46 : 38);
        return color;
    }

    QColor iconColor() const { return content_.isRunning ? runningColor : content_.theme.accent; }

    // The kind, on a rounded tile.
    void paintTile(QPainter &painter, const QRectF &tile) const
    {
        QPainterPath tilePath;
        tilePath.addRoundedRect(tile, 3, 3);
        painter.fillPath(tilePath, tileColor());
        const QIcon icon = UiIcons::icon(iconOf(content_.configuration.kind), iconColor());
        icon.paint(&painter, QRect(int(tile.center().x()) - iconSize / 2, int(tile.center().y()) - iconSize / 2,
                                   iconSize, iconSize));
    }

    // On the right: the state. Returns where the text must end.
    int paintState(QPainter &painter) const
    {
        int right = width() - 12;
        if (content_.isSelected) {
            const QRect check(right - iconSize, (height() - iconSize) / 2, iconSize, iconSize);
            UiIcons::icon(QStringLiteral("check"), content_.theme.accent).paint(&painter, check);
            right = check.left() - 8;
        }
        if (content_.isRunning)
            right = paintRunning(painter, right);
        return right;
    }

    // "Running" and its dot, ending at `right`. Returns where they start.
    int paintRunning(QPainter &painter, int right) const
    {
        const QColor &color = runningColor;
        QFont small = font();
        small.setPointSizeF(small.pointSizeF() * 0.85);
        painter.setFont(small);
        const QString label = QObject::tr("Running");
        const int labelWidth = painter.fontMetrics().horizontalAdvance(label);
        const QRect labelRect(right - labelWidth, 0, labelWidth, height());
        painter.setPen(color);
        painter.drawText(labelRect, Qt::AlignVCenter | Qt::AlignRight, label);
        painter.setPen(Qt::NoPen);
        painter.setBrush(color);
        painter.drawEllipse(QPointF(labelRect.left() - 7, height() / 2.0), 3.5, 3.5);
        return labelRect.left() - 16;
    }

    // Name above, command below.
    void paintText(QPainter &painter, TextSpan span) const
    {
        const int textWidth = span.right - span.left;
        QFont nameFont = font();
        if (content_.isSelected)
            nameFont.setWeight(QFont::DemiBold);
        painter.setFont(nameFont);
        const QFontMetrics nameMetrics(nameFont);
        QFont commandFont = font();
        commandFont.setPointSizeF(commandFont.pointSizeF() * 0.85);
        const QFontMetrics commandMetrics(commandFont);
        const int top = (height() - nameMetrics.height() - commandMetrics.height() - 1) / 2;
        painter.setPen(content_.theme.foreground);
        painter.drawText(QRect(span.left, top, textWidth, nameMetrics.height()), Qt::AlignLeft | Qt::AlignVCenter,
                         nameMetrics.elidedText(content_.configuration.name, Qt::ElideRight, textWidth));
        painter.setFont(commandFont);
        painter.setPen(content_.theme.mutedText);
        QString command = content_.configuration.command;
        if (!content_.configuration.folder.isEmpty())
            command = content_.configuration.folder + QStringLiteral(" › ") + command;
        painter.drawText(QRect(span.left, top + nameMetrics.height() + 1, textWidth, commandMetrics.height()),
                         Qt::AlignLeft | Qt::AlignVCenter,
                         commandMetrics.elidedText(command, Qt::ElideMiddle, textWidth));
    }

    QWidgetAction *action_;
    QMenu *menu_;
    RowContent content_;
};

// Text not chosen: a heading or a note.
QLabel *label(QMenu *menu, const QString &text, const QString &style)
{
    auto *label = new QLabel(text, menu);
    label->setStyleSheet(style);
    auto *action = new QWidgetAction(menu);
    action->setDefaultWidget(label);
    action->setEnabled(false);
    menu->addAction(action);
    return label;
}

} // namespace

namespace RunConfigurationMenu {

QWidgetAction *addConfiguration(QMenu *menu, const RunConfiguration &configuration, RowState state, const Theme &theme)
{
    auto *action = new QWidgetAction(menu);
    action->setText(configuration.name);
    action->setDefaultWidget(new ConfigurationRow(action, menu, {configuration, state.selected, state.running, theme}));
    menu->addAction(action);
    return action;
}

void addHeading(QMenu *menu, const QString &text, const Theme &theme)
{
    QLabel *heading = label(menu, text.toUpper(),
                            QStringLiteral("color: %1; font-size: 8pt; font-weight: 600; letter-spacing: 1px;"
                                           " padding: 8px 10px 4px 10px; background: transparent;")
                                .arg(theme.mutedText.name()));
    heading->setMinimumWidth(rowWidth);
}

void addNote(QMenu *menu, const QString &text, const Theme &theme)
{
    QLabel *note =
        label(menu, text,
              QStringLiteral("color: %1; padding: 8px 10px; background: transparent;").arg(theme.mutedText.name()));
    note->setWordWrap(true);
    note->setMaximumWidth(rowWidth);
}

} // namespace RunConfigurationMenu
