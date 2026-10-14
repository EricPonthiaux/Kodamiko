#pragma once

#include "ProcessSpec.h"

#include <QAbstractScrollArea>
#include <QClipboard>
#include <QFont>

#include <memory>

class IPty;
class VtScreen;
struct Theme;
struct TerminalCell;

// Terminal graphique : relie un PTY (le programme) à un VtScreen (l'écran
// émulé) et dessine la grille de cellules. La barre de défilement parcourt
// l'historique des lignes sorties de l'écran.
class QKeyEvent;

class TerminalWidget : public QAbstractScrollArea {
    Q_OBJECT

public:
    explicit TerminalWidget(QWidget *parent = nullptr);
    ~TerminalWidget() override;

    bool start(const ProcessSpec &spec);
    QString errorString() const { return errorString_; }
    bool isRunning() const { return running_; }

    void applyTheme(const Theme &theme);

    // Vrai pour Ctrl+1/2/3 (changement de mode), que le terminal laisse à
    // l'application. Reconnu par la touche physique : sur un clavier AZERTY,
    // Ctrl+2 arrive comme Ctrl+é.
    static bool isModeShortcut(const QKeyEvent &event);

signals:
    void titleChanged(const QString &title);
    void finished(int exitCode);

protected:
    bool event(QEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void inputMethodEvent(QInputMethodEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void focusInEvent(QFocusEvent *event) override;
    void focusOutEvent(QFocusEvent *event) override;
    bool focusNextPrevChild(bool next) override;

private:
    void onProcessFinished(int exitCode);
    void onScreenChanged();
    void updateGridSize();
    void updateScrollBar();
    void scrollToBottom();

    void paintCell(QPainter &painter, const TerminalCell &cell, int x, int y, bool selected) const;
    void paintCursor(QPainter &painter) const;

    // Position (colonne, ligne de contenu) sous un point du widget ; les
    // lignes de contenu sont négatives dans l'historique.
    QPoint cellAt(const QPoint &widgetPosition) const;
    bool isSelected(int contentRow, int column) const;
    QString selectedText() const;
    void copySelection(QClipboard::Mode mode) const;
    void pasteClipboard();

    std::unique_ptr<IPty> pty_;
    std::unique_ptr<VtScreen> screen_;
    QString errorString_;
    bool running_ = false;

    QFont font_;
    QFont boldFont_;
    QFont italicFont_;
    QSize cellSize_{8, 16};
    int ascent_ = 12;
    QColor foreground_;
    QColor background_;

    int scrollOffset_ = 0; // lignes d'historique affichées au-dessus de l'écran
    bool selecting_ = false;
    QPoint selectionAnchor_{-1, 0};
    QPoint selectionEnd_{-1, 0};
};
