#ifndef GEOMETRY_H
#define GEOMETRY_H

#include <stddef.h>

struct point {
    int x, y;
};

struct §polygon {
    struct point *points;
    size_t count;
};

#endif
