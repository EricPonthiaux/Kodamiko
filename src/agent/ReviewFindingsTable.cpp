// SPDX-License-Identifier: GPL-3.0-only

#include "ReviewFindingsTable.h"

#include "ReviewText.h"
#include "ui/ContextMenus.h"

#include <QDir>
#include <QHeaderView>
#include <QMenu>
#include <QPushButton>
#include <QShortcut>
#include <QVBoxLayout>

#include <set>

namespace {

constexpr int severityWidthPx = 80;
constexpr int placeWidthPx = 160;
constexpr int resolveWidthPx = 110;
constexpr int cellMarginPx = 6;

} // namespace

ReviewFindingsTable::ReviewFindingsTable(QWidget *parent)
    : QTableWidget(parent)
{
    setObjectName(QStringLiteral("reviewFindingsTable"));
    setWordWrap(true);
    setTextElideMode(Qt::ElideNone);
    setShowGrid(false);
    // Rows told apart by their background: a padding or a border of the
    // items would also shrink the Resolve buttons' cells.
    setAlternatingRowColors(true);
    setSelectionBehavior(QAbstractItemView::SelectRows);
    setSelectionMode(QAbstractItemView::ExtendedSelection);
    setEditTriggers(QAbstractItemView::NoEditTriggers);
    setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    verticalHeader()->hide();
    setUpColumns();

    setContextMenuPolicy(Qt::CustomContextMenu);
    connect(this, &QWidget::customContextMenuRequested, this, &ReviewFindingsTable::showMenu);
    connect(this, &QTableWidget::itemActivated, this, [this](const QTableWidgetItem *item) {
        if (item->row() < rows_.size())
            emit findingRequested(rows_[item->row()]);
    });
    const auto *dismiss = new QShortcut(QKeySequence::Delete, this, nullptr, nullptr, Qt::WidgetShortcut);
    connect(dismiss, &QShortcut::activated, this, [this] {
        if (session_)
            session_->dismiss(selectedFindings());
    });
}

void ReviewFindingsTable::setUpColumns()
{
    setColumnCount(int(Column::Resolve) + 1);
    setHorizontalHeaderLabels({tr("Severity"), tr("Place"), tr("Problem"), tr("When It Fails"), QString()});
    QHeaderView *columns = horizontalHeader();
    columns->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    columns->setHighlightSections(false);
    columns->setSectionResizeMode(int(Column::Severity), QHeaderView::Fixed);
    columns->setSectionResizeMode(int(Column::Place), QHeaderView::Interactive);
    columns->setSectionResizeMode(int(Column::Problem), QHeaderView::Stretch);
    columns->setSectionResizeMode(int(Column::Scenario), QHeaderView::Stretch);
    columns->setSectionResizeMode(int(Column::Resolve), QHeaderView::Fixed);
    columns->resizeSection(int(Column::Severity), severityWidthPx);
    columns->resizeSection(int(Column::Place), placeWidthPx);
    columns->resizeSection(int(Column::Resolve), resolveWidthPx);
    // The rows grow or shrink with their text, wrapped to the columns'
    // width, never lower than the Resolve button.
    const int buttonHeight = QPushButton(tr("Resolve")).sizeHint().height();
    verticalHeader()->setMinimumSectionSize(buttonHeight + 2 * cellMarginPx);
    connect(columns, &QHeaderView::sectionResized, this, &QTableView::resizeRowsToContents);
}

void ReviewFindingsTable::resizeEvent(QResizeEvent *event)
{
    QTableWidget::resizeEvent(event);
    fitRowsLater();
}

void ReviewFindingsTable::showEvent(QShowEvent *event)
{
    QTableWidget::showEvent(event);
    fitRowsLater();
}

void ReviewFindingsTable::fitRowsLater()
{
    QMetaObject::invokeMethod(this, &QTableView::resizeRowsToContents, Qt::QueuedConnection);
}

void ReviewFindingsTable::setSession(ReviewSession *session)
{
    if (session_)
        session_->disconnect(this);
    session_ = session;
    if (session) {
        connect(session, &ReviewSession::stateChanged, this, &ReviewFindingsTable::fill);
        connect(session, &ReviewSession::findingsChanged, this, &ReviewFindingsTable::fill);
    }
    fill();
}

void ReviewFindingsTable::fill()
{
    clearContents();
    setRowCount(0);
    rows_.clear();
    if (!session_)
        return;
    for (const ReviewReport::Finding &finding : session_->findings())
        addRow(finding);
    resizeRowsToContents();
}

void ReviewFindingsTable::addRow(const ReviewReport::Finding &finding)
{
    const int row = rowCount();
    insertRow(row);
    rows_.append(finding);
    const bool isError = finding.severity == Diagnostic::Severity::Error;
    const QString place =
        QStringLiteral("%1:%2").arg(QDir::toNativeSeparators(QDir(session_->workDir()).relativeFilePath(finding.path)),
                                    QString::number(finding.line));
    const auto setCell = [this, row](Column column, const QString &text) {
        auto *item = new QTableWidgetItem(text);
        item->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        setItem(row, int(column), item);
        return item;
    };
    setCell(Column::Severity, isError ? tr("Error") : tr("Warning"))
        ->setForeground(QColor(isError ? ReviewText::errorColor(palette()) : ReviewText::warningColor(palette())));
    setCell(Column::Place, place)->setToolTip(tr("Show it in the diff (double-click)"));
    setCell(Column::Problem, finding.problem);
    setCell(Column::Scenario, finding.scenario);
    setCellWidget(row, int(Column::Resolve), createResolveCell(finding));
}

QWidget *ReviewFindingsTable::createResolveCell(const ReviewReport::Finding &finding)
{
    auto *cell = new QWidget;
    const bool isSent = session_->isResolving(finding);
    auto *resolve = new QPushButton(isSent ? tr("Sent") : tr("Resolve"), cell);
    resolve->setEnabled(!isSent);
    resolve->setToolTip(tr("Have the %1 session that wrote the code check this problem and fix it if it is real")
                            .arg(session_->agentName()));
    connect(resolve, &QPushButton::clicked, this, [this, finding] {
        if (session_)
            session_->resolve({finding});
    });
    auto *layout = new QVBoxLayout(cell);
    layout->setContentsMargins(cellMarginPx, cellMarginPx, cellMarginPx, cellMarginPx);
    layout->addWidget(resolve, 0, Qt::AlignVCenter);
    return cell;
}

QList<ReviewReport::Finding> ReviewFindingsTable::selectedFindings() const
{
    std::set<int> rows;
    for (const QModelIndex &index : selectionModel()->selectedRows())
        rows.insert(index.row());
    QList<ReviewReport::Finding> findings;
    for (const int row : rows) {
        if (row < rows_.size())
            findings.append(rows_[row]);
    }
    return findings;
}

void ReviewFindingsTable::showMenu(const QPoint &position)
{
    const QList<ReviewReport::Finding> findings = selectedFindings();
    if (findings.isEmpty() || !session_)
        return;
    QMenu menu(this);
    connect(menu.addAction(tr("Resolve")), &QAction::triggered, session_,
            [session = session_, findings] { session->resolve(findings); });
    connect(menu.addAction(tr("Send to %1").arg(session_->agentName())), &QAction::triggered, session_,
            [session = session_, findings] { session->send(findings); });
    menu.addSeparator();
    const QString dismissKey = QKeySequence(QKeySequence::Delete).toString(QKeySequence::NativeText);
    connect(menu.addAction(tr("Dismiss") + QLatin1Char('\t') + dismissKey), &QAction::triggered, session_,
            [session = session_, findings] { session->dismiss(findings); });
    ContextMenus::exec(menu, *this, viewport()->mapToGlobal(position));
}
