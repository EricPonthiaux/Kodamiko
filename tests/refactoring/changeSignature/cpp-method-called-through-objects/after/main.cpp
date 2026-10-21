#include "shape.h"

int main() {
  Shape s;
  Shape *p = &s;
  return s.area(2, false) + p->area(3, false);
}
