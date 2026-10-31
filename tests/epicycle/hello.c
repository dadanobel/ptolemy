#include <stdio.h>
int main(int argc, char **argv) { unsigned x = 1; long long s = 0; for (int i = 0; i < 1000; i++) { x = x * 1103515245u + 12345; s += (int)x % 1000; }
  printf("hello %s %u %lld %.3f\n", argv[0], x, s, s / 7.0); return 3; }
