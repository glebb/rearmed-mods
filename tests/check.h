#ifndef CHECK_H
#define CHECK_H
#include <stdio.h>

static int check_failed, check_count;
#define CHECK(x)                                                      \
  do {                                                                \
    check_count++;                                                    \
    if (!(x)) {                                                       \
      check_failed++;                                                 \
      fprintf(stderr, "%s:%d: failed: %s\n", __FILE__, __LINE__, #x); \
    }                                                                 \
  } while (0)

static int check_done(const char *name) {
  printf("%s: %d checks, %d failed\n", name, check_count, check_failed);
  return check_failed != 0;
}
#endif
