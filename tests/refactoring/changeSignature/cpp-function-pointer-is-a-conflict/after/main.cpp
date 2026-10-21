int twice(int x, int y) { return 2 * x; }

int (*pointer)(int) = twice;

int main() { return twice(1, 0); }
