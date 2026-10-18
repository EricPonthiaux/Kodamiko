#include "RunConfigurationMenu.h"

#include "ui/UiIcons.h"

#include <QDir>
#include <QLabel>
#include <QLinearGradient>
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

// One configuration. Highlighted under the mouse and when the keyboard
// makes its action the menu's active one.
class ConfigurationRow : public QWidget {
public:
    ConfigurationRow(QWidgetAction *action,
                     QMenu *menu,
                     const RunConfiguration &configuration,
                     bool selected,
                     bool running,
                     Theme theme)
        : QWidget(menu)
        , action_(action)
        , menu_(menu)
        , configuration_(configuration)
        , selected_(selected)
        , running_(running)
        , theme_(std::move(theme))
    {
        setMouseTracking(true);
        setFixedHeight(rowHeight);
        setMinimumWidth(rowWidth);
        QString toolTip = configuration.command;
        if (!configuration.folder.isEmpty())
            toolTip += QObject::tr("\nin %1").arg(QDir::toNativeSeparators(configuration.folder));
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
        const QRectF frame = QRectF(rect()).adjusted(0, 1, 0, -1);
        if (highlighted) {
            QLinearGradient gradient(frame.topLeft(), frame.topRight());
            gradient.setColorAt(0, theme_.gradientStart);
            gradient.setColorAt(1, theme_.gradientEnd);
            QPainterPath path;
            path.addRoundedRect(frame, 6, 6);
            painter.fillPath(path, gradient);
        }
        const QColor text = highlighted ? QColor(Qt::white) : theme_.foreground;
        const QColor muted = highlighted ? QColor(255, 255, 255, 190) : theme_.mutedText;

        // The kind, on a rounded tile.
        const QRectF tile(10, (height() - tileSize) / 2.0, tileSize, tileSize);
        QPainterPath tilePath;
        tilePath.addRoundedRect(tile, 6, 6);
        QColor tileColor = highlighted ? QColor(255, 255, 255, 40) : theme_.accent;
        if (!highlighted)
            tileColor.setAlpha(running_ ? 0 : 38);
        if (running_ && !highlighted) {
            tileColor = runningColor;
            tileColor.setAlpha(46);
        }
        painter.fillPath(tilePath, tileColor);
        const QColor iconColor = highlighted ? QColor(Qt::white) : running_ ? runningColor : theme_.accent;
        const QIcon icon = UiIcons::icon(iconOf(configuration_.kind), iconColor);
        icon.paint(&painter, QRect(int(tile.center().x()) - iconSize / 2, int(tile.center().y()) - iconSize / 2,
                                   iconSize, iconSize));

        // On the right: the state.
        int right = width() - 12;
        if (selected_) {
            const QRect check(right - iconSize, (height() - iconSize) / 2, iconSize, iconSize);
            UiIcons::icon(QStringLiteral("check"), highlighted ? QColor(Qt::white) : theme_.accent)
                .paint(&painter, check);
            right = check.left() - 8;
        }
        if (running_) {
            QFont small = font();
            small.setPointSizeF(small.pointSizeF() * 0.85);
            painter.setFont(small);
            const QString label = QObject::tr("Running");
            const int labelWidth = painter.fontMetrics().horizontalAdvance(label);
            const QRect labelRect(right - labelWidth, 0, labelWidth, height());
            painter.setPen(highlighted ? QColor(Qt::white) : runningColor);
            painter.drawText(labelRect, Qt::AlignVCenter | Qt::AlignRight, label);
            painter.setPen(Qt::NoPen);
            painter.setBrush(highlighted ? QColor(Qt::white) : runningColor);
            painter.drawEllipse(QPointF(labelRect.left() - 7, height() / 2.0), 3.5, 3.5);
            right = labelRect.left() - 16;
        }

        // Name above, command below.
        const int left = int(tile.right()) + 12;
        const int textWidth = right - left;
        QFont nameFont = font();
        if (selected_)
            nameFont.setWeight(QFont::DemiBold);
        painter.setFont(nameFont);
        const QFontMetrics nameMetrics(nameFont);
        QFont commandFont = font();
        commandFont.setPointSizeF(commandFont.pointSizeF() * 0.85);
        const QFontMetrics commandMetrics(commandFont);
        const int top = (height() - nameMetrics.height() - commandMetrics.height() - 1) / 2;
        painter.setPen(text);
        painter.drawText(QRect(left, top, textWidth, nameMetrics.height()), Qt::AlignLeft | Qt::AlignVCenter,
                         nameMetrics.elidedText(configuration_.name, Qt::ElideRight, textWidth));
        painter.setFont(commandFont);
        painter.setPen(muted);
        QString command = configuration_.command;
        if (!configuration_.folder.isEmpty())
            command = configuration_.folder + QStringLiteral(" › ") + command;
        painter.drawText(QRect(left, top + nameMetrics.height() + 1, textWidth, commandMetrics.height()),
                         Qt::AlignLeft | Qt::AlignVCenter,
                         commandMetrics.elidedText(command, Qt::ElideMiddle, textWidth));
    }

private:
    QWidgetAction *action_;
    QMenu *menu_;
    RunConfiguration configuration_;
    bool selected_;
    bool running_;
    Theme theme_;
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

QWidgetAction *addConfiguration(QMenu *menu,
                                const RunConfiguration &configuration,
                                bool selected,
                                bool running,
                                const Theme &theme)
{
    auto *action = new QWidgetAction(menu);
    action->setText(configuration.name);
    action->setDefaultWidget(new ConfigurationRow(action, menu, configuration, selected, running, theme));
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
