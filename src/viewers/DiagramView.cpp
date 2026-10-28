// SPDX-License-Identifier: GPL-3.0-only

#include "DiagramView.h"

#include "diagram/Board.h"
#include "diagram/Canvas.h"
#include "diagram/Document.h"
#include "diagram/Family.h"
#include "diagram/Graphviz.h"
#include "diagram/Inspector.h"
#include "ui/AppFonts.h"
#include "ui/GripSplitter.h"
#include "ui/UiIcons.h"
#include "ui/UiMetrics.h"

#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QImage>
#include <QLabel>
#include <QMenu>
#include <QPainter>
#include <QSaveFile>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <optional>

#ifdef DUET_WITH_SVG
#include <QSvgGenerator>
#endif

namespace {

constexpr int messageMs = 4000;
constexpr int groupSpacingPx = 12;

// `heightPx` is given for the default interface font.
QToolButton *toolButton(const QString &text,
                        const QString &toolTip,
                        QWidget *parent,
                        int heightPx = UiMetrics::toolbarButtonSize)
{
    auto *button = new QToolButton(parent);
    button->setText(text);
    button->setToolTip(toolTip);
    button->setAccessibleName(text.isEmpty() ? toolTip : text);
    button->setAutoRaise(true);
    button->setFocusPolicy(Qt::TabFocus);
    AppFonts::followApplicationFont(button, [button, heightPx] {
        button->setMinimumHeight(UiMetrics::scaled(heightPx));
        const int iconSize = UiMetrics::scaled(UiMetrics::chipIconSize);
        button->setIconSize(QSize(iconSize, iconSize));
    });
    return button;
}

// An icon that shows or hides a part of the view, shown at first.
QToolButton *visibilityButton(const QString &toolTip, QWidget *parent)
{
    auto *button = toolButton(QString(), toolTip, parent);
    button->setCheckable(true);
    button->setChecked(true);
    return button;
}

// The thin vertical line between two groups of commands.
QFrame *divider(QWidget *parent)
{
    auto *line = new QFrame(parent);
    line->setObjectName(QStringLiteral("diagramToolbarDivider"));
    AppFonts::followApplicationFont(line, [line] { line->setFixedSize(1, UiMetrics::scaled(18)); });
    return line;
}

} // namespace

DiagramView::DiagramView(const Theme &theme, QWidget *parent)
    : QWidget(parent)
    , board_(new diagram::Board(this))
    , canvas_(new diagram::Canvas(board_, this))
    , inspector_(new diagram::Inspector(board_, canvas_, this))
    , message_(new QLabel(this))
    , messageTimer_(new QTimer(this))
{
    AppFonts::followApplicationFont(inspector_, [this] { inspector_->setMinimumWidth(UiMetrics::scaled(220)); });
    auto *splitter = new GripSplitter(Qt::Horizontal, this);
    splitter->addWidget(createCanvasColumn());
    splitter->addWidget(inspector_);
    splitter->setStretchFactor(0, 1);
    splitter->setCollapsible(0, false);
    splitter->setSizes({900, 280});

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(splitter);

    connect(canvas_, &diagram::Canvas::zoomChanged, this, &DiagramView::updateZoom);
    connect(canvas_, &diagram::Canvas::message, this, &DiagramView::showMessage);
    connect(canvas_, &diagram::Canvas::codeRequested, this, &DiagramView::codeRequested);
    connect(inspector_, &diagram::Inspector::sourceApplied, this,
            [this](const diagram::document::Loaded &loaded) { canvas_->settle(loaded, true); });
    messageTimer_->setSingleShot(true);
    messageTimer_->setInterval(messageMs);
    connect(messageTimer_, &QTimer::timeout, message_, &QLabel::clear);
    connect(board_, &diagram::Board::changed, this, &DiagramView::updateModified);

    applyTheme(theme);
    updateZoom(canvas_->zoom());
    setFocusProxy(canvas_);
    // Without it, the first toolbar button takes the focus when the window
    // shows, and its focus outline reads as a pressed button.
    canvas_->setFocus();
}

DiagramView::~DiagramView() = default;

QString DiagramView::selectedElement() const
{
    const diagram::Shape *shape = board_->selectedShape();
    if (!shape)
        return {};
    return shape->key.isEmpty() ? shape->id : shape->key;
}

void DiagramView::revealElement(const QString &reference)
{
    if (!reference.isEmpty())
        canvas_->revealShape(reference);
}

// The toolbar over the canvas alone, so that the properties panel has its
// own header beside it, at the same height.
QWidget *DiagramView::createCanvasColumn()
{
    auto *column = new QWidget(this);
    auto *layout = new QVBoxLayout(column);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(createToolbar(column));
    layout->addWidget(canvas_, 1);
    return column;
}

// Grouped by function: arrange; frame, zoom and grid; the messages; then
// export and the properties panel, which stays reachable once hidden.
QWidget *DiagramView::createToolbar(QWidget *parent)
{
    auto *area = new QWidget(parent);
    area->setObjectName(QStringLiteral("diagramToolbar"));
    area->setAttribute(Qt::WA_StyledBackground);
    UiMetrics::setScaledHeight(area, UiMetrics::panelHeaderHeight);

    layOutButton_ = toolButton(tr("Lay Out"), tr("Arrange the diagram automatically (Ctrl+L)"), area);
    // QToolButton has no icon/text spacing property; spaces follow the font size.
    layOutButton_->setText(QStringLiteral("  ") + layOutButton_->text());
    layOutButton_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    connect(layOutButton_, &QToolButton::clicked, canvas_, [this] { canvas_->arrange(true); });
    fitButton_ = toolButton(tr("Fit"), tr("Show the whole diagram (F)"), area);
    fitButton_->setText(QStringLiteral("  ") + fitButton_->text());
    fitButton_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    connect(fitButton_, &QToolButton::clicked, this, [this] { canvas_->fitView(true); });
    gridButton_ = visibilityButton(tr("Show the grid"), area);
    connect(gridButton_, &QToolButton::toggled, canvas_, &diagram::Canvas::setGridVisible);
    panelButton_ = visibilityButton(tr("Show the properties panel"), area);
    connect(panelButton_, &QToolButton::toggled, inspector_, &QWidget::setVisible);
    message_->setForegroundRole(QPalette::PlaceholderText);
    message_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);

    auto *layout = new QHBoxLayout(area);
    layout->setContentsMargins(8, 0, 8, 0);
    layout->setSpacing(4);
    layout->addWidget(layOutButton_);
    layout->addSpacing(4);
    layout->addWidget(divider(area), 0, Qt::AlignVCenter);
    layout->addSpacing(4);
    layout->addWidget(fitButton_);
    layout->addWidget(createZoomControl(area));
    layout->addWidget(gridButton_);
    layout->addSpacing(groupSpacingPx);
    layout->addWidget(message_, 1);
    layout->addWidget(createExportButton(area));
    layout->addWidget(panelButton_);
    return area;
}

// One framed control: zoom out, the zoom (which resets it), zoom in. Its
// buttons are shorter by the frame, so that it is as high as the others.
QWidget *DiagramView::createZoomControl(QWidget *parent)
{
    auto *frame = new QFrame(parent);
    frame->setObjectName(QStringLiteral("diagramZoom"));
    // As high as its buttons, not as the toolbar.
    frame->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    const int heightPx = UiMetrics::toolbarButtonSize - 4;
    auto *zoomOut = toolButton(QStringLiteral("−"), tr("Zoom out (Ctrl+-)"), frame, heightPx);
    connect(zoomOut, &QToolButton::clicked, this, [this] { canvas_->zoomBy(1 / 1.25); });
    auto *zoomIn = toolButton(QStringLiteral("+"), tr("Zoom in (Ctrl+=)"), frame, heightPx);
    connect(zoomIn, &QToolButton::clicked, this, [this] { canvas_->zoomBy(1.25); });
    zoomButton_ = toolButton(QString(), tr("Reset the zoom to 100 % (Ctrl+0)"), frame, heightPx);
    zoomButton_->setAccessibleName(tr("Reset zoom"));
    AppFonts::followApplicationFont(zoomButton_, [this] {
        zoomButton_->setMinimumWidth(zoomButton_->fontMetrics().horizontalAdvance(QStringLiteral("400 %")) + 16);
    });
    connect(zoomButton_, &QToolButton::clicked, canvas_, &diagram::Canvas::resetZoom);

    auto *layout = new QHBoxLayout(frame);
    layout->setContentsMargins(1, 1, 1, 1);
    layout->setSpacing(0);
    layout->addWidget(zoomOut);
    layout->addWidget(zoomButton_);
    layout->addWidget(zoomIn);
    return frame;
}

QToolButton *DiagramView::createExportButton(QWidget *parent)
{
    exportButton_ = toolButton(tr("Export"), tr("Export the diagram"), parent);
    exportButton_->setObjectName(QStringLiteral("diagramExport"));
    exportButton_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    exportButton_->setPopupMode(QToolButton::InstantPopup);
    auto *menu = new QMenu(exportButton_);
    menu->addAction(tr("Source…"), this, &DiagramView::exportSource);
#ifdef DUET_WITH_SVG
    menu->addAction(tr("SVG Image…"), this, &DiagramView::exportSvg);
#endif
    menu->addAction(tr("PNG Image…"), this, &DiagramView::exportPng);
    exportButton_->setMenu(menu);
    return exportButton_;
}

bool DiagramView::isDiagramFile(const QString &path)
{
    return !diagram::languageOfFile(path).isEmpty();
}

bool DiagramView::load(const QString &path, QString *error)
{
    const QString absolutePath = QFileInfo(path).absoluteFilePath();
    QFile file(absolutePath);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error)
            *error = file.errorString();
        return false;
    }
    const QByteArray content = file.readAll();
    QString message;
    const std::optional<diagram::document::Loaded> loaded =
        diagram::document::load(*board_, absolutePath, content, &message);
    if (!loaded) {
        if (error)
            *error = tr("Invalid diagram: %1").arg(message);
        return false;
    }
    filePath_ = absolutePath;
    diskContent_ = content;
    codec_ = loaded->codec;
    inspector_->setCodec(codec_);
    // Elements without a place (written by hand or by Claude) are laid out
    // or placed beside their neighbors; links without a curve are routed.
    // None of it is a change of the file.
    canvas_->settle(*loaded, false);
    board_->clearHistory();
    savedSnapshot_ = board_->snapshot(false);
    lastModified_ = false;
    if (!loaded->warnings.isEmpty())
        showMessage(loaded->warnings.first());
    canvas_->fitWhenShown();
    canvas_->fitView(false);
    emit modificationChanged(false);
    return true;
}

QByteArray DiagramView::content() const
{
    return codec_ ? diagram::document::save(*board_, *codec_) : QByteArray();
}

bool DiagramView::save(QString *error)
{
    if (filePath_.isEmpty() || !codec_)
        return false;
    const QByteArray bytes = content();
    QSaveFile file(filePath_);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
        if (error)
            *error = file.errorString();
        return false;
    }
    diskContent_ = bytes;
    savedSnapshot_ = board_->snapshot(false);
    updateModified();
    return true;
}

bool DiagramView::reloadFromDisk(QString *error)
{
    return load(filePath_, error);
}

bool DiagramView::isModified() const
{
    return !savedSnapshot_.isEmpty() && board_->snapshot(false) != savedSnapshot_;
}

bool DiagramView::matchesDisk() const
{
    QFile file(filePath_);
    return file.open(QIODevice::ReadOnly) && file.readAll() == diskContent_;
}

void DiagramView::updateModified()
{
    const bool modified = isModified();
    if (modified != lastModified_) {
        lastModified_ = modified;
        emit modificationChanged(modified);
    }
}

void DiagramView::applyTheme(const Theme &theme)
{
    board_->setStyle(diagram::Style::fromTheme(theme));
    layOutButton_->setIcon(UiIcons::icon(QStringLiteral("arrange"), theme.foreground));
    fitButton_->setIcon(UiIcons::icon(QStringLiteral("fit"), theme.foreground));
    exportButton_->setIcon(UiIcons::icon(QStringLiteral("export"), theme.foreground));
    gridButton_->setIcon(UiIcons::icon(QStringLiteral("grid"), theme.foreground));
    panelButton_->setIcon(UiIcons::icon(QStringLiteral("side-panel"), theme.foreground));
}

void DiagramView::showMessage(const QString &text)
{
    message_->setText(text);
    messageTimer_->start();
}

void DiagramView::updateZoom(qreal zoom)
{
    zoomButton_->setText(QStringLiteral("%1 %").arg(qRound(zoom * 100)));
}

namespace {

// The usual extension of a source in `language`; the file's own otherwise.
QString sourceExtension(const QString &language, const QFileInfo &file)
{
    if (language == QLatin1String("plantuml"))
        return QStringLiteral("puml");
    if (language == QLatin1String("mermaid"))
        return QStringLiteral("mmd");
    return file.suffix();
}

} // namespace

void DiagramView::exportSource()
{
    if (!codec_)
        return;
    const QFileInfo info(filePath_);
    const QString extension = sourceExtension(codec_->language(), info);
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Export Source"), info.absoluteDir().filePath(info.completeBaseName() + QLatin1Char('.') + extension));
    if (path.isEmpty())
        return;
    QSaveFile file(path);
    const QByteArray source = diagram::document::source(*board_, *codec_).toUtf8() + '\n';
    if (!file.open(QIODevice::WriteOnly) || file.write(source) != source.size() || !file.commit()) {
        showMessage(tr("Export failed: %1").arg(file.errorString()));
        return;
    }
    showMessage(tr("Exported %1").arg(QFileInfo(path).fileName()));
}

namespace {

// The board in the printable palette, framed with a margin.
struct Printed {
    diagram::Board board;
    QRectF box;

    explicit Printed(const diagram::Board &source)
    {
        board.setFamily(source.family());
        board.setStyle(diagram::Style::printed());
        board.fromJson(source.toJson());
        board.meta = source.meta;
        box = board.contentBounds().adjusted(-24, -24, 24, 24);
    }
};

} // namespace

bool DiagramView::exportImage(const QString &path, QString *error) const
{
    const Printed printed(*board_);
    if (QFileInfo(path).suffix().compare(QStringLiteral("svg"), Qt::CaseInsensitive) == 0) {
#ifdef DUET_WITH_SVG
        QSvgGenerator generator;
        generator.setFileName(path);
        generator.setSize(printed.box.size().toSize());
        generator.setViewBox(QRectF(QPointF(0, 0), printed.box.size()));
        generator.setTitle(QFileInfo(filePath_).completeBaseName());
        QPainter painter(&generator);
        if (!painter.isActive()) {
            if (error)
                *error = tr("Cannot write %1").arg(path);
            return false;
        }
        painter.setRenderHint(QPainter::Antialiasing);
        painter.fillRect(QRectF(QPointF(0, 0), printed.box.size()), printed.board.style().background);
        painter.translate(-printed.box.topLeft());
        diagram::Canvas::paintBoard(painter, printed.board);
        return true;
#else
        if (error)
            *error = tr("This build of Duet cannot write SVG");
        return false;
#endif
    }
    const qreal scale = 3;
    QImage image((printed.box.size() * scale).toSize(), QImage::Format_ARGB32_Premultiplied);
    image.fill(printed.board.style().background);
    {
        QPainter painter(&image);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setRenderHint(QPainter::TextAntialiasing);
        painter.scale(scale, scale);
        painter.translate(-printed.box.topLeft());
        diagram::Canvas::paintBoard(painter, printed.board);
    }
    if (!image.save(path)) {
        if (error)
            *error = tr("Cannot write %1").arg(path);
        return false;
    }
    return true;
}

void DiagramView::exportPng()
{
    const QFileInfo info(filePath_);
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Export PNG"), info.absoluteDir().filePath(info.completeBaseName() + QStringLiteral(".png")),
        tr("PNG Image (*.png)"));
    if (path.isEmpty())
        return;
    QString error;
    showMessage(exportImage(path, &error) ? tr("Exported %1").arg(QFileInfo(path).fileName()) : error);
}

void DiagramView::exportSvg()
{
    const QFileInfo info(filePath_);
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Export SVG"), info.absoluteDir().filePath(info.completeBaseName() + QStringLiteral(".svg")),
        tr("SVG Image (*.svg)"));
    if (path.isEmpty())
        return;
    QString error;
    showMessage(exportImage(path, &error) ? tr("Exported %1").arg(QFileInfo(path).fileName()) : error);
}
