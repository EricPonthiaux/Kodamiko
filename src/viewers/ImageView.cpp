#include "ImageView.h"

#include <QImageReader>
#include <QKeyEvent>
#include <QLabel>
#include <QLocale>
#include <QPainter>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

namespace {

constexpr double minZoom = 0.05;
constexpr double maxZoom = 32.0;
constexpr double zoomStep = 1.25;

// Checkerboard drawn under the transparent areas of the image.
QBrush checkerboard(const QColor &background)
{
    QPixmap tile(16, 16);
    const QColor other = background.lightness() < 128 ? background.lighter(130) : background.darker(110);
    tile.fill(background);
    QPainter painter(&tile);
    painter.fillRect(0, 0, 8, 8, other);
    painter.fillRect(8, 8, 8, 8, other);
    return QBrush(tile);
}

} // namespace

ImageView::ImageView(const Theme &theme, QWidget *parent)
    : QScrollArea(parent)
    , label_(new QLabel(this))
{
    label_->setAlignment(Qt::AlignCenter);
    setWidget(label_);
    setAlignment(Qt::AlignCenter);
    setFrameShape(QFrame::NoFrame);
    setFocusPolicy(Qt::StrongFocus);
    applyTheme(theme);
}

bool ImageView::isImageFile(const QString &path)
{
    return !QImageReader::imageFormat(path).isEmpty();
}

bool ImageView::load(const QString &path, QString *error)
{
    QImageReader reader(path);
    reader.setAutoTransform(true); // EXIF orientation of photos
    QImage image = reader.read();
    if (image.isNull()) {
        *error = reader.errorString();
        return false;
    }
    filePath_ = path;
    image_ = std::move(image);
    if (fitToWindow_)
        zoomToFit();
    else
        updatePixmap();
    return true;
}

QString ImageView::description() const
{
    return tr("%1 × %2, zoom %3%")
        .arg(image_.width())
        .arg(image_.height())
        .arg(QLocale().toString(std::round(zoom_ * 100)));
}

void ImageView::applyTheme(const Theme &theme)
{
    QPalette palette = this->palette();
    palette.setColor(QPalette::Window, theme.background);
    palette.setColor(QPalette::WindowText, theme.foreground);
    setPalette(palette);
    setBackgroundRole(QPalette::Window);
    viewport()->setAutoFillBackground(true);

    QPalette labelPalette = label_->palette();
    labelPalette.setBrush(QPalette::Window, checkerboard(theme.background));
    label_->setPalette(labelPalette);
    label_->setAutoFillBackground(true);
}

void ImageView::setZoom(double zoom)
{
    zoom_ = std::clamp(zoom, minZoom, maxZoom);
    updatePixmap();
}

void ImageView::zoomToFit()
{
    if (image_.isNull())
        return;
    const QSize available = viewport()->size();
    const double fit = std::min(double(available.width()) / image_.width(),
                                double(available.height()) / image_.height());
    setZoom(std::min(1.0, fit)); // shrink large images, without enlarging small ones
}

void ImageView::updatePixmap()
{
    if (image_.isNull())
        return;
    const QSize size = (QSizeF(image_.size()) * zoom_).toSize().expandedTo(QSize(1, 1));
    // Above 100%, keep pixels sharp rather than smoothing them.
    const Qt::TransformationMode mode = zoom_ < 1.0 ? Qt::SmoothTransformation : Qt::FastTransformation;
    label_->setPixmap(QPixmap::fromImage(image_.scaled(size, Qt::KeepAspectRatio, mode)));
    label_->resize(size);
    emit zoomChanged();
}

void ImageView::keyPressEvent(QKeyEvent *event)
{
    if (event->matches(QKeySequence::ZoomIn) || (event->modifiers() & Qt::ControlModifier && event->key() == Qt::Key_Equal)) {
        fitToWindow_ = false;
        setZoom(zoom_ * zoomStep);
    } else if (event->matches(QKeySequence::ZoomOut)) {
        fitToWindow_ = false;
        setZoom(zoom_ / zoomStep);
    } else if (event->modifiers() & Qt::ControlModifier && event->key() == Qt::Key_0) {
        fitToWindow_ = false;
        setZoom(1.0);
    } else {
        QScrollArea::keyPressEvent(event);
    }
}

void ImageView::resizeEvent(QResizeEvent *event)
{
    QScrollArea::resizeEvent(event);
    if (fitToWindow_)
        zoomToFit();
}

void ImageView::wheelEvent(QWheelEvent *event)
{
    if (!(event->modifiers() & Qt::ControlModifier)) {
        QScrollArea::wheelEvent(event);
        return;
    }
    const int delta = event->angleDelta().y();
    if (delta == 0)
        return;
    fitToWindow_ = false;
    setZoom(delta > 0 ? zoom_ * zoomStep : zoom_ / zoomStep);
    event->accept();
}
