/*
 * Multi-threaded demo: multiple independently animated elements.
 * - Render thread: redraws all elements every frame (thread_sleep for timing).
 * - Ball threads: each moves one ball at its own speed (independent thread_sleep).
 * - Input thread: blocks in thread_get(); shows last key on row 0.
 * - Semaphore used as mutex to protect shared display updates.
 */
#include "syslib.h"
#include "blockpixel.h"
#include "thread.h"
#include <stddef.h>
#include <stdint.h>

/* Match default shell window (e.g. ur: 39x11). Use 39 cols so we never pass col >= wd. */
#define WIDTH 39
#define HEIGHT 20
#define N_BALLS 4

static struct sema *display_mutex;

/* Shared state: each ball's position and velocity (written by ball threads, read by render). */
static int ball_x[N_BALLS], ball_y[N_BALLS], ball_dx[N_BALLS], ball_dy[N_BALLS];

static const int ball_color[N_BALLS] = {
    ANSI_RED, ANSI_YELLOW, ANSI_GREEN, ANSI_CYAN};
/* Shared delay in ms per step: all balls move at the same speed. */
static unsigned int ball_delay_ms = 100;
/* Global score: how many times balls hit a wall. */
static int wall_hits = 0;

static void ball_thread(void *arg)
{
  int i = *(int *)arg;
  if (i < 0 || i >= N_BALLS)
    return;
  for (;;)
  {
    int x = ball_x[i] + ball_dx[i];
    int y = ball_y[i] + ball_dy[i];
    int dx = ball_dx[i];
    int dy = ball_dy[i];
    int bounced = 0;
    if (x <= 0 || x >= WIDTH - 1)
    {
      dx = -dx;
      bounced = 1;
    }
    if (y <= 0 || y >= HEIGHT - 1)
    {
      dy = -dy;
      bounced = 1;
    }
    if (x < 0)
      x = 0;
    if (x >= WIDTH)
      x = WIDTH - 1;
    if (y < 0)
      y = 0;
    if (y >= HEIGHT)
      y = HEIGHT - 1;
    ball_x[i] = x;
    ball_y[i] = y;
    ball_dx[i] = dx;
    ball_dy[i] = dy;

    if (bounced)
      wall_hits++;

    uint64_t deadline = user_gettime() + (uint64_t)ball_delay_ms * 1000000ULL;
    thread_sleep(deadline);
  }
}

static void render_thread(void *arg)
{
  struct bp bp;
  uint8_t bp_buffer[WIDTH * HEIGHT];
  bp_init(&bp, 0, 1, WIDTH, HEIGHT, bp_buffer);

  for (;;)
  {
    sema_dec(display_mutex);
    for (int px = 0; px < WIDTH; px++)
      for (int py = 0; py < HEIGHT; py++)
        bp_put(&bp, px, py, ANSI_BLUE, BP_LAZY);
    for (int i = 0; i < N_BALLS; i++)
      bp_put(&bp, ball_x[i], ball_y[i], ball_color[i], BP_LAZY);
    bp_flush(&bp);

    /* Draw score in the top row (left side). */
    int s = wall_hits;
    int col = 0;
    for (int x = 0; x < 8; x++)
      user_put(x, 0, CELL(' ', ANSI_WHITE, ANSI_BLACK));
    if (s == 0)
    {
      user_put(col++, 0, CELL('0', ANSI_WHITE, ANSI_BLACK));
    }
    else
    {
      int digits[10];
      int n = 0;
      while (s > 0 && n < 10)
      {
        digits[n++] = s % 10;
        s /= 10;
      }
      for (int k = n - 1; k >= 0; k--)
        user_put(col++, 0, CELL('0' + digits[k], ANSI_WHITE, ANSI_BLACK));
    }

    sema_inc(display_mutex);

    uint64_t deadline = user_gettime() + 45 * 1000000ULL; /* ~22 fps */
    thread_sleep(deadline);
  }
}

static void input_thread_fn(void *arg)
{
  int col = 0;
  for (;;)
  {
    int c = thread_get();
    sema_dec(display_mutex);
    if (c > 0 && c < 256)
    {
      if (c == 27) /* ESC: possible arrow key sequence */
      {
        int c1 = thread_get();
        if (c1 == '[' || c1 == 'O')
        {
          int c2 = thread_get();
          if (c2 == 'A')
          {
            /* Up arrow: speed up significantly (halve delay, min 10 ms). */
            if (ball_delay_ms > 10)
              ball_delay_ms = ball_delay_ms / 2;
          }
          else if (c2 == 'B')
          {
            /* Down arrow: slow down (double delay, max 800 ms). */
            if (ball_delay_ms < 800)
              ball_delay_ms = ball_delay_ms * 2;
          }
        }
      }
      else
      {
        user_put(0, col % WIDTH, CELL((char)c, ANSI_WHITE, ANSI_BLACK));
        col++;
      }
    }
    else if (c == USER_GET_GOT_FOCUS)
      user_put(0, 0, CELL('+', ANSI_GREEN, ANSI_BLACK));
    else if (c == USER_GET_LOST_FOCUS)
      user_put(0, 0, CELL('-', ANSI_YELLOW, ANSI_BLACK));
    sema_inc(display_mutex);
  }
}

static int ball_index[N_BALLS] = {0, 1, 2, 3};

int main(int argc, char **argv)
{
  (void)argc;
  (void)argv;

  thread_init();
  display_mutex = sema_create(1);
  if (!display_mutex)
    return 1;

  /* Start positions and velocities so balls are spread out and move in different directions. */
  ball_x[0] = WIDTH / 4;
  ball_y[0] = HEIGHT / 2;
  ball_dx[0] = 1;
  ball_dy[0] = 1;
  ball_x[1] = WIDTH * 3 / 4;
  ball_y[1] = HEIGHT / 4;
  ball_dx[1] = -1;
  ball_dy[1] = 1;
  ball_x[2] = WIDTH / 2;
  ball_y[2] = HEIGHT * 3 / 4;
  ball_dx[2] = 1;
  ball_dy[2] = -1;
  ball_x[3] = WIDTH / 2;
  ball_y[3] = HEIGHT / 4;
  ball_dx[3] = -1;
  ball_dy[3] = -1;

  thread_create(render_thread, NULL, 4096);
  for (int i = 0; i < N_BALLS; i++)
    thread_create(ball_thread, &ball_index[i], 4096);
  thread_create(input_thread_fn, NULL, 4096);

  thread_exit();
}
