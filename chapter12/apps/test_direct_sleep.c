#include "syslib.h"

void main(void)
{
  // Test user_sleep() directly with absolute deadlines

  uint64_t now = user_gettime();
  uint64_t deadline1 = now + 100000000ULL; // 100ms from now
  uint64_t deadline2 = now + 500000000ULL; // 500ms from now

  // Sleep until first deadline
  user_sleep(deadline1);

  // Check we're past the deadline
  uint64_t after = user_gettime();
  if (after < deadline1)
  {
    // Error: woke up too early!
  }

  // Sleep until second deadline
  user_sleep(deadline2);

  after = user_gettime();
  if (after < deadline2)
  {
    // Error: woke up too early!
  }

  // Test immediate return for past deadline
  uint64_t past_deadline = now - 1000000ULL; // 1ms in the past
  user_sleep(past_deadline);                 // Should return immediately

  user_exit();
}