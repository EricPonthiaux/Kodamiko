#include <cstdio>

class Helper {
public:
    void run() {}
};

class §Printer {
public:
    void print() { std::puts("x"); }
};

int main() { Printer().print(); }
