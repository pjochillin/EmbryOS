#include "syslib.h"

void main(void)
{

  // Process 1: Sleep for 500ms
  user_delay(500);

  // Process 2: Sleep for 200ms
  user_delay(200);

  // Process 3: Sleep for 1000ms
  user_delay(1000);

  user_exit();
}