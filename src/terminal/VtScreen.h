#pragma once

#include <QColor>
#include <QObject>
#include <QPoint>
#include <QString>

#include <deque>
#include <vector>

#include <vterm.h>

// Une case de la grille du terminal, prête à dessiner.
struct TerminalCell {
    QString text;     // vide pour une case blanche
    QColor foreground;
    QColor background;
    bool bold = false;
    bool italic = false;
    bool underline = false;
    bool strike = false;
    int width = 1;    // 2 pour un caractère large (CJK, emoji)
    bool isContinuation = false; // seconde moitié d'un caractère large
};

// Écran d'un terminal VT/xterm, émulé par libvterm.
//
//   sortie du programme --feed()--> libvterm --> grille de cellules (cell())
//   touches, collages  --sendKey()/paste()--> libvterm --> outputReady()
//
// Les lignes qui sortent par le haut de l'écran sont conservées dans un
// historique ; cell() y accède avec des numéros de ligne négatifs.
class VtScreen : public QObject {
    Q_OBJECT

public:
    VtScreen(int rows, int columns, QObject *parent = nullptr);
    ~VtScreen() override;

    void feed(const QByteArray &data);
    void resize(int rows, int columns);
    void setDefaultColors(const QColor &foreground, const QColor &background);

    int rows() const { return rows_; }
    int columns() const { return columns_; }
    int scrollbackSize() const { return static_cast<int>(scrollback_.size()); }

    // `row` dans [-scrollbackSize(), rows()[ ; 0 = première ligne de l'écran.
    TerminalCell cell(int row, int column) const;
    QString lineText(int row) const;

    QPoint cursorPosition() const { return cursor_; } // x = colonne, y = ligne
    bool isCursorVisible() const { return cursorVisible_; }
    bool isAlternateScreen() const { return alternateScreen_; }

    // Touche pressée. `key` : Qt::Key, `text` : texte produit (QKeyEvent::text).
    // Renvoie false si la touche n'a pas d'équivalent terminal.
    bool sendKey(int key, Qt::KeyboardModifiers modifiers, const QString &text);
    void paste(const QString &text);

signals:
    void outputReady(const QByteArray &data); // à écrire vers le programme
    void screenChanged();
    void titleChanged(const QString &title);
    void bell();

private:
    static int onDamage(VTermRect rect, void *user);
    static int onMoveCursor(VTermPos position, VTermPos oldPosition, int visible, void *user);
    static int onSetTermProp(VTermProp property, VTermValue *value, void *user);
    static int onBell(void *user);
    static int onPushLine(int columns, const VTermScreenCell *cells, void *user);
    static int onPopLine(int columns, VTermScreenCell *cells, void *user);
    static void onOutput(const char *bytes, size_t length, void *user);

    TerminalCell toTerminalCell(const VTermScreenCell &cell) const;
    QColor toQColor(VTermColor color, bool isForeground) const;

    VTerm *terminal_ = nullptr;
    VTermScreen *screen_ = nullptr;
    VTermScreenCallbacks callbacks_{};
    int rows_;
    int columns_;
    QPoint cursor_;
    bool cursorVisible_ = true;
    bool alternateScreen_ = false;
    QColor defaultForeground_ = Qt::lightGray;
    QColor defaultBackground_ = Qt::black;
    QString titleBuffer_;
    std::deque<std::vector<VTermScreenCell>> scrollback_;
};
