#include "DiagramView.h"

#include "diagram/Board.h"
#include "diagram/Canvas.h"
#include "diagram/Document.h"
#include "diagram/Family.h"
#include "diagram/Graphviz.h"
#include "diagram/Inspector.h"
#include "ui/GripSplitter.h"

#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QImage>
#include <QLabel>
#include <QMenu>
#include <QPainter>
#include <QSaveFile>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#ifdef DUET_WITH_SVG
#include <QSvgGenerator>
#endif

namespace {

constexpr int messageMs = 4000;

QToolButton *toolButton(const QString &text, const QString &toolTip, QWidget *parent)
{
    auto *button = new QToolButton(parent);
    button->setText(text);
    button->setToolTip(toolTip);
    button->setAutoRaise(true);
    button->setFocusPolicy(Qt::NoFocus);
    return button;
}

} // namespace

DiagramView::DiagramView(const Theme &theme, QWidget *parent)
    : QWidget(parent)
    , board_(new diagram::Board(this))
    , canvas_(new diagram::Canvas(board_, this))
    , inspector_(new diagram::Inspector(board_, canvas_, this))
    , zoomLabel_(new QLabel(this))
    , message_(new QLabel(this))
    , messageTimer_(new QTimer(this))
{
    // Toolbar.
    auto *layOut = toolButton(tr("Lay Out"), tr("Arrange the diagram automatically (Ctrl+L)"), this);
    connect(layOut, &QToolButton::clicked, canvas_, [this] { canvas_->arrange(true); });
    auto *fit = toolButton(tr("Fit"), tr("Show the whole diagram (F)"), this);
    connect(fit, &QToolButton::clicked, this, [this] { canvas_->fitView(true); });
    auto *zoomOut = toolButton(QStringLiteral("−"), tr("Zoom out (Ctrl+-)"), this);
    connect(zoomOut, &QToolButton::clicked, this, [this] { canvas_->zoomBy(1 / 1.25); });
    auto *zoomIn = toolButton(QStringLiteral("+"), tr("Zoom in (Ctrl+=)"), this);
    connect(zoomIn, &QToolButton::clicked, this, [this] { canvas_->zoomBy(1.25); });
    zoomLabel_->setMinimumWidth(zoomLabel_->fontMetrics().horizontalAdvance(QStringLiteral("400 %")));
    zoomLabel_->setAlignment(Qt::AlignCenter);
    zoomLabel_->setToolTip(tr("Zoom (Ctrl+0: 100 %)"));
    message_->setForegroundRole(QPalette::PlaceholderText);

    auto *more = toolButton(tr("More"), tr("Export and panels"), this);
    more->setPopupMode(QToolButton::InstantPopup);
    auto *menu = new QMenu(more);
    menu->addAction(tr("Export Source…"), this, &DiagramView::exportSource);
#ifdef DUET_WITH_SVG
    menu->addAction(tr("Export SVG…"), this, &DiagramView::exportSvg);
#endif
    menu->addAction(tr("Export PNG…"), this, &DiagramView::exportPng);
    menu->addSeparator();
    QAction *grid = menu->addAction(tr("Grid"));
    grid->setCheckable(true);
    grid->setChecked(true);
    connect(grid, &QAction::toggled, canvas_, &diagram::Canvas::setGridVisible);
    QAction *showInspector = menu->addAction(tr("Properties Panel"));
    showInspector->setCheckable(true);
    showInspector->setChecked(true);
    connect(showInspector, &QAction::toggled, inspector_, &QWidget::setVisible);
    more->setMenu(menu);

    auto *toolbar = new QHBoxLayout;
    toolbar->setContentsMargins(6, 2, 6, 2);
    toolbar->setSpacing(2);
    toolbar->addWidget(layOut);
    toolbar->addWidget(fit);
    toolbar->addSpacing(8);
    toolbar->addWidget(zoomOut);
    toolbar->addWidget(zoomLabel_);
    toolbar->addWidget(zoomIn);
    toolbar->addSpacing(12);
    toolbar->addWidget(message_, 1);
    toolbar->addWidget(more);

    // Body.
    inspector_->setMinimumWidth(220);
    auto *splitter = new GripSplitter(Qt::Horizontal, this);
    splitter->addWidget(canvas_);
    splitter->addWidget(inspector_);
    splitter->setStretchFactor(0, 1);
    splitter->setCollapsible(0, false);
    splitter->setSizes({900, 280});

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addLayout(toolbar);
    layout->addWidget(splitter, 1);

    connect(canvas_, &diagram::Canvas::zoomChanged, this, &DiagramView::updateZoom);
    connect(canvas_, &diagram::Canvas::message, this, &DiagramView::showMessage);
    connect(inspector_, &diagram::Inspector::sourceApplied, this,
            [this](const diagram::document::Loaded &loaded) { canvas_->settle(loaded, true); });
    messageTimer_->setSingleShot(true);
    messageTimer_->setInterval(messageMs);
    connect(messageTimer_, &QTimer::timeout, message_, &QLabel::clear);
    connect(board_, &diagram::Board::changed, this, &DiagramView::updateModified);

    applyTheme(theme);
    updateZoom(canvas_->zoom());
    setFocusProxy(canvas_);
}

DiagramView::~DiagramView() = default;

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
    diagram::document::Loaded loaded;
    QString message;
    if (!diagram::document::load(*board_, absolutePath, content, loaded, &message)) {
        if (error)
            *error = tr("Invalid diagram: %1").arg(message);
        return false;
    }
    filePath_ = absolutePath;
    diskContent_ = content;
    codec_ = loaded.codec;
    inspector_->setCodec(codec_);
    // Elements without a place (written by hand or by Claude) are laid out
    // or placed beside their neighbors; links without a curve are routed.
    // None of it is a change of the file.
    canvas_->settle(loaded, false);
    board_->clearHistory();
    savedSnapshot_ = board_->snapshot(false);
    lastModified_ = false;
    if (!loaded.warnings.isEmpty())
        showMessage(loaded.warnings.first());
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
    const QByteArray data = content();
    QSaveFile file(filePath_);
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit()) {
        if (error)
            *error = file.errorString();
        return false;
    }
    diskContent_ = data;
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
}

void DiagramView::showMessage(const QString &text)
{
    message_->setText(text);
    messageTimer_->start();
}

void DiagramView::updateZoom(qreal zoom)
{
    zoomLabel_->setText(QStringLiteral("%1 %").arg(qRound(zoom * 100)));
}

void DiagramView::exportSource()
{
    if (!codec_)
        return;
    const QFileInfo info(filePath_);
    const QString extension = codec_->language() == QLatin1String("plantuml") ? QStringLiteral("puml")
                              : codec_->language() == QLatin1String("mermaid") ? QStringLiteral("mmd")
                                                                               : info.suffix();
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Export Source"), info.absoluteDir().filePath(info.completeBaseName() + QLatin1Char('.') + extension));
    if (path.isEmpty())
        return;
    QSaveFile file(path);
    const QByteArray data = diagram::document::source(*board_, *codec_).toUtf8() + '\n';
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit()) {
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

void DiagramView::exportPng()
{
    const QFileInfo info(filePath_);
    const QString path = QFileDialog::getSaveFileName(this, tr("Export PNG"),
                                                      info.absoluteDir().filePath(info.completeBaseName() + QStringLiteral(".png")),
                                                      tr("PNG Image (*.png)"));
    if (path.isEmpty())
        return;
    Printed printed(*board_);
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
        showMessage(tr("Export failed"));
        return;
    }
    showMessage(tr("Exported %1").arg(QFileInfo(path).fileName()));
}

void DiagramView::exportSvg()
{
#ifdef DUET_WITH_SVG
    const QFileInfo info(filePath_);
    const QString path = QFileDialog::getSaveFileName(this, tr("Export SVG"),
                                                      info.absoluteDir().filePath(info.completeBaseName() + QStringLiteral(".svg")),
                                                      tr("SVG Image (*.svg)"));
    if (path.isEmpty())
        return;
    Printed printed(*board_);
    QSvgGenerator generator;
    generator.setFileName(path);
    generator.setSize(printed.box.size().toSize());
    generator.setViewBox(QRectF(QPointF(0, 0), printed.box.size()));
    generator.setTitle(info.completeBaseName());
    {
        QPainter painter(&generator);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.fillRect(QRectF(QPointF(0, 0), printed.box.size()), printed.board.style().background);
        painter.translate(-printed.box.topLeft());
        diagram::Canvas::paintBoard(painter, printed.board);
    }
    showMessage(tr("Exported %1").arg(QFileInfo(path).fileName()));
#endif
}
