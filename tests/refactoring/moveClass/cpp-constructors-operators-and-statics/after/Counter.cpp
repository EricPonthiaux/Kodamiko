#include "Counter.h"
#include "counters.h"

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
