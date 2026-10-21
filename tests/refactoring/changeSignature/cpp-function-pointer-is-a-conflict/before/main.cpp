int §twice(int x) { return 2 * x; }

int (*pointer)(int) = twice;

int main() { return twice(1); }
