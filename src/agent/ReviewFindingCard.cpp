// SPDX-License-Identifier: GPL-3.0-only

#include "ReviewFindingCard.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

namespace {

bool isDark(const QPalette &palette)
{
    return palette.color(QPalette::Window).lightness() < 128;
}

QString severityText(Diagnostic::Severity severity)
{
    switch (severity) {
    case Diagnostic::Severity::Error:
        return ReviewFindingCard::tr("Error");
    case Diagnostic::Severity::Warning:
        return ReviewFindingCard::tr("Warning");
    }
    return {};
}

// The colors of the editor's wavy underlines (One Dark, One Light).
QString severityColor(Diagnostic::Severity severity, const QPalette &palette)
{
    switch (severity) {
    case Diagnostic::Severity::Error:
        return isDark(palette) ? QStringLiteral("#e06c75") : QStringLiteral("#e45649");
    case Diagnostic::Severity::Warning:
        return isDark(palette) ? QStringLiteral("#d19a66") : QStringLiteral("#c18401");
    }
    return {};
}

QLabel *wrappedLabel(const QString &text, QWidget *parent)
{
    auto *label = new QLabel(text, parent);
    label->setWordWrap(true);
    label->setTextFormat(Qt::PlainText);
    label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    return label;
}

} // namespace

ReviewFindingCard::ReviewFindingCard(const ReviewReport::Finding &finding,
                                     const QString &location,
                                     const QString &agentName,
                                     QWidget *parent)
    : QFrame(parent)
{
    setFrameShape(QFrame::StyledPanel);

    auto *severity = new QLabel(severityText(finding.severity), this);
    severity->setStyleSheet(
        QStringLiteral("color: %1; font-weight: bold;").arg(severityColor(finding.severity, palette())));
    auto *place = new QLabel(QStringLiteral("<a href=\"open\">%1</a>").arg(location.toHtmlEscaped()), this);
    place->setTextFormat(Qt::RichText);
    place->setToolTip(tr("Show the line in the editor"));
    connect(place, &QLabel::linkActivated, this, &ReviewFindingCard::openRequested);
    auto *send = new QPushButton(tr("Send to %1").arg(agentName), this);
    send->setToolTip(tr("Type this finding in %1's prompt, for you to complete and send").arg(agentName));
    connect(send, &QPushButton::clicked, this, &ReviewFindingCard::sendRequested);
    auto *dismiss = new QPushButton(tr("Dismiss"), this);
    dismiss->setToolTip(tr("Remove this finding from the review and the editor"));
    connect(dismiss, &QPushButton::clicked, this, &ReviewFindingCard::dismissRequested);

    auto *header = new QHBoxLayout;
    header->addWidget(severity);
    header->addWidget(place);
    header->addStretch();
    header->addWidget(send);
    header->addWidget(dismiss);

    auto *layout = new QVBoxLayout(this);
    layout->addLayout(header);
    layout->addWidget(wrappedLabel(finding.problem, this));
    if (!finding.scenario.isEmpty()) {
        QLabel *scenario = wrappedLabel(tr("When it fails: %1").arg(finding.scenario), this);
        scenario->setForegroundRole(QPalette::PlaceholderText);
        layout->addWidget(scenario);
    }
}
