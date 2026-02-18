#include "syslib.h"

void main(void)
{
  // Test timing accuracy with multiple sleeps
  for (int i = 0; i < 10; i++)
  {
    uint64_t start = user_gettime();
    user_delay(50); // 50ms sleep
    uint64_t end = user_gettime();

    uint64_t elapsed_ns = end - start;
    uint64_t elapsed_ms = elapsed_ns / 1000000;
  }

  user_exit();
}