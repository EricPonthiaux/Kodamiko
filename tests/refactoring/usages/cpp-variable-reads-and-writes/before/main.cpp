int §count = 0;
void f() {
  count = 1;
  count++;
  int x = count;
  (void)x;
}
