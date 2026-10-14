#pragma once

#include "syntax/Theme.h"

#include <QImage>
#include <QScrollArea>

class QLabel;

// Visionneuse d'une image (PNG, JPEG, GIF, SVG... selon les greffons Qt
// installés), ouverte dans un onglet à la place d'un éditeur de texte.
// L'image est réduite pour tenir dans l'onglet ; Ctrl+molette ou Ctrl++ /
// Ctrl+- zooment, Ctrl+0 revient à la taille réelle.
class ImageView : public QScrollArea {
    Q_OBJECT

public:
    explicit ImageView(const Theme &theme, QWidget *parent = nullptr);

    // Vrai si le fichier est une image que Qt sait lire (d'après son contenu).
    static bool isImageFile(const QString &path);

    bool load(const QString &path, QString *error);
    QString filePath() const { return filePath_; }
    // Description pour la barre d'état : « 640 × 480, zoom 50 % ».
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
