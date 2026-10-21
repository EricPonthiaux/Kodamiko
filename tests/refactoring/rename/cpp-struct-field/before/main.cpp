#include "point.h"

int main() {
  Point p{1, 2};
  p.x += p.y;
  return p.x;
}
