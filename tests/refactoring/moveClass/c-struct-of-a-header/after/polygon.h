#ifndef POLYGON_H
#define POLYGON_H

#include <stddef.h>

struct polygon {
    struct point *points;
    size_t count;
};

#endif
