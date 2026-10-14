#pragma once

#include <QColor>
#include <QFont>
#include <QList>
#include <QString>
#include <QStringView>

#include <utility>

// Style d'une catégorie syntaxique (mot-clé, chaîne, commentaire...).
struct TextStyle {
    QColor foreground;
    bool bold = false;
    bool italic = false;
};

// Palette complète de l'éditeur : couleurs de l'interface du texte et styles
// des catégories syntaxiques. Les catégories portent les noms des captures
// tree-sitter (« keyword », « string.special », « function.method »...).
struct Theme {
    QString name;
    QFont font;

    QColor background;
    QColor foreground;
    QColor caret;
    QColor caretLine;
    QColor selection;
    QColor marginBackground;
    QColor marginForeground;
    QColor braceMatch;

    QList<std::pair<QString, TextStyle>> syntaxStyles;

    static Theme dark();
    static Theme light();

    // Index dans `syntaxStyles` du style le plus spécifique pour une capture :
    // « function.method.builtin » essaie « function.method.builtin », puis
    // « function.method », puis « function ». Renvoie -1 si rien ne correspond.
    int syntaxStyleIndex(QStringView captureName) const;
};
