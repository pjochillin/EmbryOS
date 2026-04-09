#pragma once

#include <stdint.h>

#include "bd.h"

/* One inode = exactly four 32-bit words (16 bytes). */
#define UFS_INODE_WORDS 4
#define UFS_INODE_BYTES (UFS_INODE_WORDS * 4)
#define UFS_INODES_PER_BLOCK (BLOCK_SIZE / UFS_INODE_BYTES)

/* Pointers per block (32-bit indices into lower device). */
#define UFS_PTRS_PER_BLOCK (BLOCK_SIZE / 4)

#define UFS_MAX_BLOCKS (1 + UFS_PTRS_PER_BLOCK + (UFS_PTRS_PER_BLOCK * UFS_PTRS_PER_BLOCK))

#define UFS_MAGIC 0x55465301u

struct ufs_inode
{
    uint32_t alloc;     /* 0 = free, non-zero = in use */
    uint32_t direct;    /* first data block, or 0 (hole) */
    uint32_t indirect;  /* indirect block ptr, or 0 */
    uint32_t dindirect; /* double-indirect block ptr, or 0 */
};

struct ufs_super
{
    uint32_t magic;
    uint32_t n_inode_blocks; /* blocks 1 .. n_inode_blocks hold inodes */
    uint32_t free_head;      /* first free-list block index; 0 = empty */
    uint32_t total_blocks;   /* total blocks on lower inode */
};

struct ufs_state
{
    struct bd *lower;
    int inode_below;
    struct ufs_super sb;
    int first_data_block; /* first block after super + inode region */
    int max_inode;        /* highest valid inode number (inclusive) */
};

int ufs_alloc(void *st);
int ufs_size(void *st, int inode);
void ufs_read(void *st, int inode, int blk, void *dst);
void ufs_write(void *st, int inode, int blk, const void *src);
void ufs_free(void *st, int inode);

void ufs_init(struct bd *iface, struct ufs_state *s, struct bd *lower,
              int inode_below, int n_inodes);
