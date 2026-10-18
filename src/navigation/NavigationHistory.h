#pragma once

#include <QList>
#include <QString>

#include <optional>

// Positions visited while jumping from one symbol to another, for Back (Alt+←)
// and Forward (Alt+→), as in a web browser.
class NavigationHistory {
public:
    struct Location {
        QString filePath;
        int position = 0; // in bytes
        bool operator==(const Location &) const = default;
    };

    // To be called before a jump, with the position being left.
    void recordJump(const Location &from);
    // Position to return to; `current` moves to the other stack.
    std::optional<Location> back(const Location &current);
    std::optional<Location> forward(const Location &current);

    bool canGoBack() const { return !back_.isEmpty(); }

    bool canGoForward() const { return !forward_.isEmpty(); }

private:
    static constexpr qsizetype maxSize = 100;
    QList<Location> back_;
    QList<Location> forward_;
};
