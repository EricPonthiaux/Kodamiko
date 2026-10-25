// SPDX-License-Identifier: GPL-3.0-only

// Mermaid user journeys (sidecar's fence/mermaid-journey.js): a title,
// sections and scored tasks with their actors. The steps are chained in
// the order they are written.

#include "Journey.h"
#include "viewers/diagram/Board.h"
#include "viewers/diagram/Family.h"
#include "viewers/diagram/SourceText.h"
#include "viewers/diagram/families/Families.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QRegularExpression>

namespace diagram::journey {

namespace {

using namespace diagram::source;

QString defaultSection()
{
    return QCoreApplication::translate("JourneyDiagram", "Journey");
}

const QRegularExpression &header()
{
    static const QRegularExpression pattern(QStringLiteral("^journey\\b"), QRegularExpression::CaseInsensitiveOption);
    return pattern;
}

// The comma-separated actors of a task, blanks dropped.
QJsonArray parseActors(const QString &list)
{
    QJsonArray actors;
    for (const QString &actor : list.split(QLatin1Char(','))) {
        if (!actor.trimmed().isEmpty())
            actors.append(actor.trimmed());
    }
    return actors;
}

class MermaidJourney : public Codec {
public:
    QString id() const override { return QStringLiteral("mermaid-journey"); }

    QString language() const override { return QStringLiteral("mermaid"); }

    QString title() const override { return QStringLiteral("User journey"); }

    const Family &family() const override { return families::journey(); }

    bool matches(const QString &source) const override
    {
        // Comments (the layout, a title) may come first.
        const QStringList lines = mermaidLines(source);
        return !lines.isEmpty() && header().match(lines.first()).hasMatch();
    }

    bool parse(const QString &source, Model &model, QString *) const override
    {
        static const QRegularExpression heading(QStringLiteral("^title\\s+(.+)$"),
                                                QRegularExpression::CaseInsensitiveOption);
        static const QRegularExpression group(QStringLiteral("^section\\s+(.+)$"),
                                              QRegularExpression::CaseInsensitiveOption);
        static const QRegularExpression task(QStringLiteral("^(.+?)\\s*:\\s*([1-5])\\s*(?::\\s*(.*))?$"));
        QString title;
        QString section = defaultSection();
        QString previous;
        int position = 0;
        for (const QString &line : mermaidLines(source)) {
            if (header().match(line).hasMatch())
                continue;
            if (const QRegularExpressionMatch m = heading.match(line); m.hasMatch()) {
                title = m.captured(1).trimmed();
                continue;
            }
            if (const QRegularExpressionMatch m = group.match(line); m.hasMatch()) {
                section = m.captured(1).trimmed();
                continue;
            }
            const QRegularExpressionMatch m = task.match(line);
            if (!m.hasMatch()) {
                model.warnings.append(QStringLiteral("Not understood: ") + line);
                continue;
            }
            const QString key = QStringLiteral("step%1").arg(++position);
            model.nodes.append({QStringLiteral("journey"), key,
                                QJsonObject{{QStringLiteral("text"), m.captured(1).trimmed()},
                                            {QStringLiteral("score"), m.captured(2).toInt()},
                                            {QStringLiteral("actors"), parseActors(m.captured(3))},
                                            {QStringLiteral("section"), section}}});
            if (!previous.isEmpty())
                model.links.append({QStringLiteral("journeyLink"), previous, key, QJsonObject{}});
            previous = key;
        }
        model.meta = QJsonObject{{QStringLiteral("rankdir"), QStringLiteral("LR")}, {QStringLiteral("title"), title}};
        return true;
    }

    QString serialize(const Board &board, const QJsonObject &meta) const override
    {
        // The source has no links: the steps are written in the board's
        // order, a section heading whenever the section changes.
        QStringList out{QStringLiteral("journey")};
        const QString title = meta.value(QStringLiteral("title")).toString();
        if (!title.isEmpty())
            out.append(QStringLiteral("  title ") + title);
        std::optional<QString> section;
        for (const auto &s : board.shapes()) {
            const auto *step = dynamic_cast<const JourneyTaskShape *>(s.get());
            if (!step)
                continue;
            const QString current = step->section.isEmpty() ? defaultSection() : step->section;
            if (!section || current != *section) {
                section = current;
                out.append(QStringLiteral("  section ") + current);
            }
            QString text = step->text;
            // A colon would end the task's text.
            text.replace(QLatin1Char(':'), QStringLiteral(" —"));
            text.replace(QLatin1Char('\n'), QLatin1Char(' '));
            const QString actors =
                step->actors.isEmpty() ? QString() : QStringLiteral(": ") + step->actors.join(QStringLiteral(", "));
            out.append(QStringLiteral("    %1: %2%3").arg(text).arg(step->score ? step->score : 3).arg(actors));
        }
        return out.join(QLatin1Char('\n'));
    }

    QString blank() const override
    {
        return QStringLiteral("journey\n  title New journey\n  section First step\n    First action: 3: User\n"
                              "    Next action: 4: User");
    }
};

} // namespace

} // namespace diagram::journey

namespace diagram::families {

QList<const Codec *> journeyCodecs()
{
    static const journey::MermaidJourney mermaid;
    return {&mermaid};
}

} // namespace diagram::families
