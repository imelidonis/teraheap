#ifndef TEST_UTIL_H
#define TEST_UTIL_H

#include <stdio.h>
#include <stdlib.h>

/* Check a condition in every build (the allocator's assertf() is on only
 * with ASSERT). On failure print the message and exit with status 1. */
#define CHECK(cond, ...)                                                    \
  do {                                                                      \
    if (!(cond)) {                                                          \
      fprintf(stderr, "[FAIL] %s:%d: %s: ", __FILE__, __LINE__, #cond);     \
      fprintf(stderr, __VA_ARGS__);                                         \
      fprintf(stderr, "\n");                                                \
      exit(1);                                                              \
    }                                                                       \
  } while (0)

#endif /* TEST_UTIL_H */
