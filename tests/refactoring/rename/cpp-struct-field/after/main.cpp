#include "point.h"

int main() {
  Point p{1, 2};
  p.left += p.y;
  return p.left;
}
