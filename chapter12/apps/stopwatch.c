#include "syslib.h"
#include "blockpixel.h"

#define WIDTH 39
#define HEIGHT 22

// Color Definitions
#define COLOR_BACKGROUND ANSI_BLACK
#define COLOR_RUNNING ANSI_GREEN
#define COLOR_STOPPED ANSI_RED
#define COLOR_FOCUS ANSI_YELLOW
#define COLOR_DIGITS ANSI_WHITE

struct stopwatch
{
  int running;                       // 0 = stopped, 1 = running
  uint64_t start_time;               // start time
  uint64_t elapsed_time;             // elapsed time
  int has_focus;                     // 0 = no focus, 1 = has focus
  struct bp bp;                      // blockpixel buffer
  uint8_t bp_buffer[WIDTH * HEIGHT]; // blockpixel buffer
};

void stopwatch_init(struct stopwatch *stopwatch)
{
  stopwatch->running = 0;
  stopwatch->start_time = 0;
  stopwatch->elapsed_time = 0;
  stopwatch->has_focus = 1;
  bp_init(&stopwatch->bp, 0, 0, WIDTH, HEIGHT, stopwatch->bp_buffer);
}

uint64_t get_elapsed_time(struct stopwatch *stopwatch)
{
  if (stopwatch->running)
  {
    return stopwatch->elapsed_time + (user_gettime() - stopwatch->start_time);
  }
  return stopwatch->elapsed_time;
}

int get_minutes(uint64_t elapsed_time)
{
  return (elapsed_time / 60000000000ULL) % 60;
}

int get_seconds(uint64_t elapsed_time)
{
  return (elapsed_time / 1000000000ULL) % 60;
}

int get_tenths(uint64_t elapsed_time)
{
  return (elapsed_time / 100000000ULL) % 10;
}

int get_hundredths(uint64_t elapsed_time)
{
  return (elapsed_time / 10000000ULL) % 10;
}

void digit_draw(struct bp *bp, int x, int y, int digit, int color)
{
  // 5x7 digit pattern
  static const uint8_t digits[10][35] = {
      // 0
      {0, 1, 1, 1, 0,
       1, 0, 0, 0, 1,
       1, 0, 0, 1, 1,
       1, 0, 1, 0, 1,
       1, 1, 0, 0, 1,
       1, 0, 0, 0, 1,
       0, 1, 1, 1, 0},

      // 1
      {0, 0, 1, 0, 0,
       0, 1, 1, 0, 0,
       1, 0, 1, 0, 0,
       0, 0, 1, 0, 0,
       0, 0, 1, 0, 0,
       0, 0, 1, 0, 0,
       1, 1, 1, 1, 1},

      // 2
      {0, 1, 1, 1, 0,
       1, 0, 0, 0, 1,
       0, 0, 0, 0, 1,
       0, 0, 0, 1, 0,
       0, 0, 1, 0, 0,
       0, 1, 0, 0, 0,
       1, 1, 1, 1, 1},

      // 3
      {1, 1, 1, 1, 0,
       0, 0, 0, 0, 1,
       0, 0, 1, 1, 0,
       0, 0, 0, 0, 1,
       0, 0, 0, 0, 1,
       1, 0, 0, 0, 1,
       0, 1, 1, 1, 0},

      // 4
      {0, 0, 0, 1, 0,
       0, 0, 1, 1, 0,
       0, 1, 0, 1, 0,
       1, 0, 0, 1, 0,
       1, 1, 1, 1, 1,
       0, 0, 0, 1, 0,
       0, 0, 0, 1, 0},

      // 5
      {1, 1, 1, 1, 1,
       1, 0, 0, 0, 0,
       1, 1, 1, 1, 0,
       0, 0, 0, 0, 1,
       0, 0, 0, 0, 1,
       1, 0, 0, 0, 1,
       0, 1, 1, 1, 0},

      // 6
      {0, 1, 1, 1, 0,
       1, 0, 0, 0, 0,
       1, 0, 0, 0, 0,
       1, 1, 1, 1, 0,
       1, 0, 0, 0, 1,
       1, 0, 0, 0, 1,
       0, 1, 1, 1, 0},

      // 7
      {1, 1, 1, 1, 1,
       0, 0, 0, 0, 1,
       0, 0, 0, 1, 0,
       0, 0, 1, 0, 0,
       0, 1, 0, 0, 0,
       0, 1, 0, 0, 0,
       0, 1, 0, 0, 0},

      // 8
      {0, 1, 1, 1, 0,
       1, 0, 0, 0, 1,
       1, 0, 0, 0, 1,
       0, 1, 1, 1, 0,
       1, 0, 0, 0, 1,
       1, 0, 0, 0, 1,
       0, 1, 1, 1, 0},

      // 9
      {0, 1, 1, 1, 0,
       1, 0, 0, 0, 1,
       1, 0, 0, 0, 1,
       0, 1, 1, 1, 1,
       0, 0, 0, 0, 1,
       0, 0, 0, 0, 1,
       0, 1, 1, 1, 0},
  };

  // draw the digit
  for (int dy = 0; dy < 7; dy++)
  {
    for (int dx = 0; dx < 5; dx++)
    {
      if (digits[digit][dy * 5 + dx])
      {
        bp_put(bp, x + dx, y + dy, color, BP_LAZY);
      }
    }
  }
}

void stopwatch_redraw(struct stopwatch *stopwatch)
{
  // Clear background
  uint8_t bg_color = stopwatch->has_focus ? COLOR_FOCUS : COLOR_BACKGROUND;
  for (int x = 0; x < WIDTH; x++)
  {
    for (int y = 0; y < HEIGHT; y++)
    {
      bp_put(&stopwatch->bp, x, y, bg_color, BP_LAZY);
    }
  }

  // Get current elapsed time
  uint64_t elapsed = get_elapsed_time(stopwatch);
  int minutes = get_minutes(elapsed);
  int seconds = get_seconds(elapsed);
  int tenths = get_tenths(elapsed);
  int hundredths = get_hundredths(elapsed);

  // Digit color
  uint8_t digit_color = stopwatch->running ? COLOR_RUNNING : COLOR_STOPPED;

  int total_display_width = 37;
  int start_x = (WIDTH - total_display_width) / 2;
  int start_y = (HEIGHT - 7) / 2; // Center vertically

  int x = start_x;

  // Draw minutes (2 digits with 1px spacing)
  digit_draw(&stopwatch->bp, x, start_y, minutes / 10, digit_color);
  x += 6; // 5 (digit) + 1 (gap between digits)
  digit_draw(&stopwatch->bp, x, start_y, minutes % 10, digit_color);
  x += 5; // 5 (digit)
  x += 2; // Space between groups

  // Draw seconds (2 digits with 1px spacing)
  digit_draw(&stopwatch->bp, x, start_y, seconds / 10, digit_color);
  x += 6; // 5 (digit) + 1 (gap between digits)
  digit_draw(&stopwatch->bp, x, start_y, seconds % 10, digit_color);
  x += 5; // 5 (digit)
  x += 2; // Space between groups

  // Draw tenths and hundredths (2 digits with 1px spacing)
  digit_draw(&stopwatch->bp, x, start_y, tenths, digit_color);
  x += 6; // 5 (digit) + 1 (gap between digits)
  digit_draw(&stopwatch->bp, x, start_y, hundredths, digit_color);

  // Flush all changes to screen
  bp_flush(&stopwatch->bp);
}

void main(void)
{
  struct stopwatch stopwatch;

  // Initialize stopwatch
  stopwatch_init(&stopwatch);

  // Check actual focus state on startup
  int c = user_get(0);
  if (c == USER_GET_LOST_FOCUS)
  {
    stopwatch.has_focus = 0;
    // Consume any pending focus events to get to current state
    while ((c = user_get(0)) == USER_GET_LOST_FOCUS || c == USER_GET_GOT_FOCUS)
    {
      if (c == USER_GET_GOT_FOCUS)
      {
        stopwatch.has_focus = 1;
      }
      else
      {
        stopwatch.has_focus = 0;
      }
    }
  }
  else if (c == USER_GET_GOT_FOCUS)
  {
    stopwatch.has_focus = 1; // Already correct
  }

  // Initial render
  stopwatch_redraw(&stopwatch);

  // Main loop - continues running even without focus
  for (;;)
  {
    // When stopped, block on input to use no CPU
    // When running, use non-blocking to update display
    int c = stopwatch.running ? user_get(0) : user_get(1);

    // Handle focus events
    if (c == USER_GET_LOST_FOCUS)
    {
      stopwatch.has_focus = 0;
      // If stopped, block to get next event (avoid CPU usage)
      // If running, continue to update display
      if (!stopwatch.running)
      {
        c = user_get(1);
      }
    }
    else if (c == USER_GET_GOT_FOCUS)
    {
      stopwatch.has_focus = 1;
      // If stopped, block to get next event (avoid CPU usage)
      if (!stopwatch.running)
      {
        c = user_get(1);
      }
    }
    // Handle keyboard events
    else if (c == 's')
    {
      // Toggle running state
      if (stopwatch.running)
      {
        uint64_t now = user_gettime();
        stopwatch.elapsed_time += (now - stopwatch.start_time);
        stopwatch.running = 0;
      }
      else
      {
        stopwatch.start_time = user_gettime();
        stopwatch.running = 1;
      }
    }
    else if (c == 'r')
    {
      stopwatch.elapsed_time = 0;
      if (stopwatch.running)
      {
        stopwatch.start_time = user_gettime();
      }
    }
    else if (c == 'q')
    {
      user_exit();
    }

    // Only render when running (or when input received)
    // When stopped and blocking, we won't reach here until input arrives
    if (stopwatch.running || c != USER_GET_NO_INPUT)
    {
      stopwatch_redraw(&stopwatch);
    }
  }
}