#pragma once

#include <vector>

template <typename T>
class Queue {
public:
    void push(T item) { items.push_back(item); }

private:
    std::vector<T> items;
};
