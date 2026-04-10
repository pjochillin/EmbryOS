# UFS Design Explanation

## On-Disk Layout

```
Block 0              : Superblock
Blocks 1..N          : i-node blocks (packed, 128 inodes per block)
Blocks (N+1)..(T-1)  : Data, indirect, double-indirect, and free-list blocks
```

Where N = `n_inode_blocks` and T = total blocks on the lower device.

---

## Superblock Layout

The superblock occupies block 0 and contains four 32-bit fields (16 bytes total):

| Offset | Field            | Description                                      |
|-------:|------------------|--------------------------------------------------|
|      0 | `magic`          | Magic number `0x55465301` identifying a UFS disk |
|      4 | `n_inode_blocks` | Number of blocks reserved for inodes (blocks 1..N) |
|      8 | `free_head`      | Block index of the first free-list block (0 = empty) |
|     12 | `total_blocks`   | Total number of blocks on the lower device       |

On boot, `ufs_init` reads block 0 and checks the magic number and geometry.
If the disk is unformatted or the geometry changed, it reformats automatically.

---

## i-Node Structure

Each inode is exactly four 32-bit words (16 bytes), so one 2048-byte block
holds 128 inodes (`BLOCK_SIZE / 16`). Inodes are numbered starting from 1.
Inode `i` lives in block `1 + (i-1)/128` at byte offset `((i-1) % 128) * 16`.

| Word | Field      | Description                                 |
|-----:|------------|---------------------------------------------|
|    0 | `alloc`    | 0 = free, non-zero = allocated              |
|    1 | `direct`   | Block pointer for logical block 0, or 0     |
|    2 | `indirect` | Block pointer to an indirect block, or 0    |
|    3 | `dindirect`| Block pointer to a double-indirect block, or 0 |

The logical-to-physical mapping is:

- **Block 0** — via `direct` (1 block)
- **Blocks 1..512** — via `indirect`, which points to a block containing 512
  32-bit pointers to data blocks
- **Blocks 513..262,657** — via `dindirect`, which points to a block of 512
  pointers to indirect blocks, each of which holds 512 data-block pointers

Maximum file size = 1 + 512 + 512 x 512 = 262,657 blocks.

---

## Block Allocation Strategy

Blocks are allocated on demand by `data_alloc()`:

1. Read the free-list head block.
2. Scan slots 1..511 for the first non-zero entry. If found, take that block
   number, clear the slot, write the free-list block back, zero the allocated
   block, and return it.
3. If all slots 1..511 are empty, the head block itself is exhausted. Advance
   `free_head` to `buf[0]` (the next free-list block in the chain), persist
   the superblock, zero the old head block, and return it as the allocated
   block.
4. If `free_head` is 0, the disk is full — return 0.

Every allocated block is zero-filled before being returned, which guarantees
that new indirect blocks start with all-null pointers and new data blocks
contain zeros (the `bd.h` contract).

Blocks are freed by `data_free()`, which inserts a block into the current
free-list head. If there is room (a zero slot in the head block), the block
number is written there. If the head block is full, the freed block becomes
the new head with a forward pointer to the old head.

---

## Free-List Organization

The free list is a singly linked list of blocks. Each free-list block has the
same layout as an indirect block — an array of 512 32-bit entries:

| Index | Meaning                                        |
|------:|------------------------------------------------|
|     0 | Pointer to the next free-list block (0 = end)  |
| 1..511| Block numbers of free data blocks (0 = unused) |

The superblock's `free_head` field points to the first block in the chain.
During formatting, `ufs_format_freelist` builds the chain by iterating over
all data blocks from `first_data` to `T-1`, grouping them into free-list
blocks of up to 511 entries each. The last free-list block written becomes
the head. The chain naturally handles partially filled blocks — the last
block in the chain may have fewer than 511 entries.

---

## How Holes Are Represented and Handled

A **hole** is a logical block that has never been written. Holes are
represented by null (0) pointers at every level:

- `direct == 0` means logical block 0 is a hole.
- A null entry in an indirect block means that logical block is a hole.
- `indirect == 0` means all blocks in the indirect range (1..512) are holes.
- Similarly for the double-indirect pointer and its sub-blocks.

**Reading a hole:** `ufs_map` with `alloc=0` returns `*phys = 0`. Then
`ufs_read` fills the destination buffer with zeros (`memset(dst, 0, BLOCK_SIZE)`).

**Writing into a hole:** `ufs_map` with `alloc=1` allocates all necessary
metadata blocks (indirect/double-indirect) and the data block itself, filling
in the null pointers along the way. Since newly allocated blocks are zeroed,
any sibling pointers in a freshly allocated indirect block are automatically
null (holes), which is correct.

This means files are **sparse** — only blocks that have been explicitly
written consume disk space. A file can have block 0 and block 100 allocated
with everything in between being holes that read as zeros.

---

## Implementation Note: Static Buffers

All block-sized temporary variables (2048 bytes each) are declared `static`
rather than placed on the stack. The per-process kernel stack in EmbryOS is
only ~4 KiB (one page shared with the PCB and trap frame), so stack-allocating
even a single block-sized buffer risks overflow. The Big Kernel Lock guarantees
single-threaded kernel execution, making static buffers safe from concurrent
access.

---

## Test Cases

A dedicated test app (`apps/test_ufs.c`) exercises the UFS implementation
through the user-level system call interface. It runs 8 tests covering all
required edge cases:

| # | Test Name             | What It Verifies                                   |
|---|-----------------------|----------------------------------------------------|
| 1 | Basic write/read      | Write a full block of 'A's, read it back, compare  |
| 2 | Hole reads as zeros   | Write block 2, read block 0 — must be all zeros    |
| 3 | Write into a hole     | Write blocks 0 and 3, verify block 1 is zeros      |
| 4 | Empty file            | Create a file, read immediately — returns 0 bytes   |
| 5 | Indirect blocks       | Write/read 5 sequential blocks (block 0 = direct, blocks 1-4 = indirect) |
| 6 | Partial block write   | Write 100 bytes, read back 100 bytes correctly      |
| 7 | Delete frees blocks   | Create/write/delete 3 rounds — if blocks leaked, later rounds would fail |
| 8 | Overwrite existing    | Write 'Y', overwrite with 'Z', verify 'Z'          |

Run from the EmbryOS shell by typing `test_ufs`. Expected output:
`14 passed, 0 failed`.

## AI Usage

We used AI for initial implementation of the file system, but it got a lot of bugs, so we debug the functions ourselves and we also used AI to develop test cases 