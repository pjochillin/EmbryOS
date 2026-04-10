#include "syslib.h"
#include "stdio.h"
#include "string.h"

#define BSIZE 2048

static char wbuf[BSIZE];
static char rbuf[BSIZE];

static int pass_count;
static int fail_count;

static void fill(char *buf, char val, int n)
{
    for (int i = 0; i < n; i++)
        buf[i] = val;
}

static int check(char *buf, char val, int n)
{
    for (int i = 0; i < n; i++)
        if (buf[i] != val)
            return 0;
    return 1;
}

static void report(const char *name, int ok)
{
    if (ok)
    {
        printf("  PASS: %s\n", name);
        pass_count++;
    }
    else
    {
        printf("  FAIL: %s\n", name);
        fail_count++;
    }
}

/* ---- Test 1: basic write/read one block ---- */
static void test_basic(void)
{
    printf("Test 1: basic write/read\n");
    int f = user_create();
    fill(wbuf, 'A', BSIZE);
    user_write(f, 0, wbuf, BSIZE);
    int n = user_read(f, 0, rbuf, BSIZE);
    report("read back size", n == BSIZE);
    report("read back data", check(rbuf, 'A', BSIZE));
    user_delete(f);
}

/* ---- Test 2: reading a hole returns zeros ---- */
static void test_hole_read(void)
{
    printf("Test 2: hole reads as zeros\n");
    int f = user_create();
    fill(wbuf, 'B', BSIZE);
    user_write(f, 2 * BSIZE, wbuf, BSIZE);
    int n = user_read(f, 0, rbuf, BSIZE);
    report("hole size", n == BSIZE);
    report("hole is zeros", check(rbuf, 0, BSIZE));
    n = user_read(f, 2 * BSIZE, rbuf, BSIZE);
    report("written block ok", n == BSIZE && check(rbuf, 'B', BSIZE));
    user_delete(f);
}

/* ---- Test 3: write into a hole ---- */
static void test_hole_write(void)
{
    printf("Test 3: write into a hole\n");
    int f = user_create();
    fill(wbuf, 'C', BSIZE);
    user_write(f, 3 * BSIZE, wbuf, BSIZE);
    fill(wbuf, 'D', BSIZE);
    user_write(f, 0, wbuf, BSIZE);
    int n = user_read(f, 0, rbuf, BSIZE);
    report("block 0 data", n == BSIZE && check(rbuf, 'D', BSIZE));
    n = user_read(f, 1 * BSIZE, rbuf, BSIZE);
    report("block 1 hole", n == BSIZE && check(rbuf, 0, BSIZE));
    n = user_read(f, 3 * BSIZE, rbuf, BSIZE);
    report("block 3 data", n == BSIZE && check(rbuf, 'C', BSIZE));
    user_delete(f);
}

/* ---- Test 4: file with no blocks ---- */
static void test_empty_file(void)
{
    printf("Test 4: empty file (no blocks)\n");
    int f = user_create();
    int n = user_read(f, 0, rbuf, BSIZE);
    report("read empty = 0 bytes", n == 0);
    user_delete(f);
}

/* ---- Test 5: multi-block sequential (exercises indirect) ---- */
static void test_indirect(void)
{
    printf("Test 5: indirect blocks (5 blocks)\n");
    int f = user_create();
    int ok = 1;
    for (int b = 0; b < 5; b++)
    {
        fill(wbuf, 'E' + b, BSIZE);
        user_write(f, b * BSIZE, wbuf, BSIZE);
    }
    for (int b = 0; b < 5; b++)
    {
        int n = user_read(f, b * BSIZE, rbuf, BSIZE);
        if (n != BSIZE || !check(rbuf, 'E' + b, BSIZE))
            ok = 0;
    }
    report("5-block read/write", ok);
    user_delete(f);
}

/* ---- Test 6: partial block write (sub-block) ---- */
static void test_partial(void)
{
    printf("Test 6: partial block write\n");
    int f = user_create();
    fill(wbuf, 'X', 100);
    user_write(f, 0, wbuf, 100);
    int n = user_read(f, 0, rbuf, 100);
    report("partial size", n == 100);
    report("partial data", check(rbuf, 'X', 100));
    user_delete(f);
}

/* ---- Test 7: delete frees blocks (create/delete/create cycle) ---- */
static void test_delete_reuse(void)
{
    printf("Test 7: delete frees blocks\n");
    int ok = 1;
    for (int round = 0; round < 3; round++)
    {
        int f = user_create();
        fill(wbuf, '0' + round, BSIZE);
        for (int b = 0; b < 4; b++)
            user_write(f, b * BSIZE, wbuf, BSIZE);
        for (int b = 0; b < 4; b++)
        {
            int n = user_read(f, b * BSIZE, rbuf, BSIZE);
            if (n != BSIZE || !check(rbuf, '0' + round, BSIZE))
                ok = 0;
        }
        user_delete(f);
    }
    report("3 rounds create/write/delete", ok);
}

/* ---- Test 8: overwrite existing block ---- */
static void test_overwrite(void)
{
    printf("Test 8: overwrite existing block\n");
    int f = user_create();
    fill(wbuf, 'Y', BSIZE);
    user_write(f, 0, wbuf, BSIZE);
    fill(wbuf, 'Z', BSIZE);
    user_write(f, 0, wbuf, BSIZE);
    int n = user_read(f, 0, rbuf, BSIZE);
    report("overwrite data", n == BSIZE && check(rbuf, 'Z', BSIZE));
    user_delete(f);
}

void main(void)
{
    printf("=== UFS Test Suite ===\n");
    test_basic();
    test_hole_read();
    test_hole_write();
    test_empty_file();
    test_indirect();
    test_partial();
    test_delete_reuse();
    test_overwrite();
    printf("=== Done: %d passed, %d failed ===\n", pass_count, fail_count);
}
