#pragma once

#include "syntax/Theme.h"

#include <QImage>
#include <QScrollArea>

class QLabel;

// Image viewer (PNG, JPEG, GIF, SVG... depending on the installed Qt
// plugins), opened in a tab instead of a text editor.
// The image is shrunk to fit the tab; Ctrl+wheel or Ctrl++ /
// Ctrl+- zoom, Ctrl+0 returns to actual size.
class ImageView : public QScrollArea {
    Q_OBJECT

public:
    explicit ImageView(const Theme &theme, QWidget *parent = nullptr);

    // True if the file is an image Qt can read (based on its content).
    static bool isImageFile(const QString &path);

    bool load(const QString &path, QString *error);
    QString filePath() const { return filePath_; }
    // Description for the status bar: "640 × 480, zoom 50%".
    QString description() const;

    void applyTheme(const Theme &theme);

signals:
    void zoomChanged();

protected:
    void keyPressEvent(QKeyEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;

private:
    void setZoom(double zoom);
    void zoomToFit();
    void updatePixmap();

    QString filePath_;
    QImage image_;
    QLabel *label_;
    double zoom_ = 1.0;
    bool fitToWindow_ = true;
};
