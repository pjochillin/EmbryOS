#include "syslib.h"

void main(void)
{
  // Test 1: Short sleep (100ms)
  uint64_t start = user_gettime();
  user_delay(100); // 100 milliseconds
  uint64_t end = user_gettime();

  // Calculate elapsed time in milliseconds
  uint64_t elapsed_ns = end - start;
  uint64_t elapsed_ms = elapsed_ns / 1000000;

  // Test 2: Very short sleep (10ms)
  start = user_gettime();
  user_delay(10);
  end = user_gettime();
  elapsed_ns = end - start;
  elapsed_ms = elapsed_ns / 1000000;

  // Test 3: Long sleep (1000ms = 1 second)
  start = user_gettime();
  user_delay(1000);
  end = user_gettime();
  elapsed_ns = end - start;
  elapsed_ms = elapsed_ns / 1000000;

  // If we get here, sleep is working (no crash)
  user_exit();
}
