#pragma once

class Counter {
public:
    Counter();
    ~Counter();
    bool operator==(const Counter &other) const;
    static int instances;
};
