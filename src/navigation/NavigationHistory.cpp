#include "NavigationHistory.h"

void NavigationHistory::recordJump(const Location &from)
{
    if (back_.isEmpty() || back_.last() != from)
        back_.append(from);
    if (back_.size() > maxSize)
        back_.removeFirst();
    forward_.clear();
}

std::optional<NavigationHistory::Location> NavigationHistory::back(const Location &current)
{
    if (back_.isEmpty())
        return std::nullopt;
    forward_.append(current);
    return back_.takeLast();
}

std::optional<NavigationHistory::Location> NavigationHistory::forward(const Location &current)
{
    if (forward_.isEmpty())
        return std::nullopt;
    back_.append(current);
    return forward_.takeLast();
}
