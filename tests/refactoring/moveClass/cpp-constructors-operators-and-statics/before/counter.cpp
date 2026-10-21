#include "counter.h"

int Counter::instances = 0;

Counter::Counter()
{
    ++instances;
}

Counter::~Counter() = default;

bool Counter::operator==(const Counter &) const
{
    return true;
}

void unrelated() {}
