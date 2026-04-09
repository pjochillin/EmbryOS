#include "embryos.h"
#include "bd_ufs.h"

static int blk_in_data_region(struct ufs_state *s, int b)
{
    return b >= s->first_data_block && b < (int)s->sb.total_blocks;
}

static void sb_load(struct ufs_state *s)
{
    static struct block b;
    s->lower->read(s->lower->state, s->inode_below, 0, &b);
    memcpy(&s->sb, b.bytes, sizeof(s->sb));
}

static void sb_store(struct ufs_state *s)
{
    static struct block b;
    memset(&b, 0, sizeof(b));
    memcpy(b.bytes, &s->sb, sizeof(s->sb));
    s->lower->write(s->lower->state, s->inode_below, 0, &b);
}

static void ufs_format_freelist(struct ufs_state *s, int first_data, int T)
{
    static uint32_t buf[UFS_PTRS_PER_BLOCK];
    int head = 0;
    int idx = first_data;
    while (idx < T)
    {
        memset(buf, 0, sizeof(buf));
        buf[0] = (uint32_t)head;
        head = idx;
        idx++;
        int slot = 1;
        while (slot < UFS_PTRS_PER_BLOCK && idx < T)
            buf[slot++] = (uint32_t)idx++;
        s->lower->write(s->lower->state, s->inode_below, head, buf);
    }
    s->sb.free_head = (uint32_t)head;
}

static void ufs_format(struct ufs_state *s, int n_ib, int T)
{
    int first_data = 1 + n_ib;
    if (first_data >= T)
        die("ufs_format: no data blocks");

    memset(&s->sb, 0, sizeof(s->sb));
    s->sb.magic = UFS_MAGIC;
    s->sb.n_inode_blocks = (uint32_t)n_ib;
    s->sb.total_blocks = (uint32_t)T;

    for (int b = 1; b <= n_ib; b++)
        s->lower->write(s->lower->state, s->inode_below, b,
                        (void *)&bd_null_block);

    ufs_format_freelist(s, first_data, T);
    sb_store(s);
}

static void data_block_zero(struct ufs_state *s, int b)
{
    s->lower->write(s->lower->state, s->inode_below, b, (void *)&bd_null_block);
}

static int data_alloc(struct ufs_state *s)
{
    static uint32_t buf[UFS_PTRS_PER_BLOCK];
    while (s->sb.free_head != 0)
    {
        int h = (int)s->sb.free_head;
        s->lower->read(s->lower->state, s->inode_below, h, buf);
        for (int i = 1; i < UFS_PTRS_PER_BLOCK; i++)
        {
            if (buf[i] != 0)
            {
                int res = (int)buf[i];
                if (!blk_in_data_region(s, res))
                    die("ufs: corrupt free list");
                buf[i] = 0;
                s->lower->write(s->lower->state, s->inode_below, h, buf);
                data_block_zero(s, res);
                return res;
            }
        }
        int next = (int)buf[0];
        s->sb.free_head = (uint32_t)next;
        sb_store(s);
        if (!blk_in_data_region(s, h))
            die("ufs: corrupt free list");
        data_block_zero(s, h);
        return h;
    }
    return 0;
}

static void data_free(struct ufs_state *s, int b)
{
    static uint32_t buf[UFS_PTRS_PER_BLOCK];
    if (b == 0)
        return;
    if (!blk_in_data_region(s, b))
        die("ufs: free of non-data block");
    int h = (int)s->sb.free_head;
    if (h == 0)
    {
        memset(buf, 0, sizeof(buf));
        s->lower->write(s->lower->state, s->inode_below, b, buf);
        s->sb.free_head = (uint32_t)b;
        sb_store(s);
        return;
    }
    s->lower->read(s->lower->state, s->inode_below, h, buf);
    for (int i = 1; i < UFS_PTRS_PER_BLOCK; i++)
    {
        if (buf[i] == 0)
        {
            buf[i] = (uint32_t)b;
            s->lower->write(s->lower->state, s->inode_below, h, buf);
            return;
        }
    }
    memset(buf, 0, sizeof(buf));
    buf[0] = (uint32_t)h;
    s->lower->write(s->lower->state, s->inode_below, b, buf);
    s->sb.free_head = (uint32_t)b;
    sb_store(s);
}

static void inode_read(struct ufs_state *s, int ino, struct ufs_inode *out)
{
    static struct block b;
    if (ino < 1 || ino > s->max_inode)
        die("ufs: bad inode");
    int slot = ino - 1;
    int iblk = 1 + slot / UFS_INODES_PER_BLOCK;
    s->lower->read(s->lower->state, s->inode_below, iblk, &b);
    memcpy(out, b.bytes + (slot % UFS_INODES_PER_BLOCK) * UFS_INODE_BYTES,
           sizeof(*out));
}

static void inode_write(struct ufs_state *s, int ino, const struct ufs_inode *in)
{
    static struct block b;
    if (ino < 1 || ino > s->max_inode)
        die("ufs: bad inode");
    int slot = ino - 1;
    int iblk = 1 + slot / UFS_INODES_PER_BLOCK;
    s->lower->read(s->lower->state, s->inode_below, iblk, &b);
    memcpy(b.bytes + (slot % UFS_INODES_PER_BLOCK) * UFS_INODE_BYTES, in,
           sizeof(*in));
    s->lower->write(s->lower->state, s->inode_below, iblk, &b);
}

static void ufs_free_indirect(struct ufs_state *s, uint32_t blk)
{
    static uint32_t ibuf[UFS_PTRS_PER_BLOCK];
    if (blk == 0)
        return;
    s->lower->read(s->lower->state, s->inode_below, (int)blk, ibuf);
    for (int i = 0; i < UFS_PTRS_PER_BLOCK; i++)
        if (ibuf[i])
            data_free(s, (int)ibuf[i]);
    data_free(s, (int)blk);
}

static void ufs_free_dindirect(struct ufs_state *s, uint32_t blk)
{
    static uint32_t dbuf[UFS_PTRS_PER_BLOCK];
    if (blk == 0)
        return;
    s->lower->read(s->lower->state, s->inode_below, (int)blk, dbuf);
    for (int i = 0; i < UFS_PTRS_PER_BLOCK; i++)
        if (dbuf[i])
            ufs_free_indirect(s, dbuf[i]);
    data_free(s, (int)blk);
}

static void ufs_mount_check(struct ufs_state *s)
{
    int T = (int)s->sb.total_blocks;
    if (s->sb.free_head != 0)
    {
        int fh = (int)s->sb.free_head;
        if (fh < s->first_data_block || fh >= T)
            die("ufs_init: corrupt free_head");
    }
}

static uint32_t map_ibuf[UFS_PTRS_PER_BLOCK];
static uint32_t map_dbuf[UFS_PTRS_PER_BLOCK];

static int ufs_map(struct ufs_state *s, int ino, int lblk, int alloc,
                   uint32_t *phys)
{
    if (lblk < 0 || lblk >= UFS_MAX_BLOCKS)
        die("ufs_map: bad block index");
    struct ufs_inode in;
    inode_read(s, ino, &in);
    if (in.alloc == 0)
    {
        if (alloc)
            die("ufs_map: inode not allocated");
        *phys = 0;
        return 0;
    }

    if (lblk == 0)
    {
        if (in.direct == 0)
        {
            if (!alloc)
            {
                *phys = 0;
                return 0;
            }
            int b = data_alloc(s);
            if (b == 0)
                return -1;
            in.direct = (uint32_t)b;
            inode_write(s, ino, &in);
        }
        *phys = in.direct;
        return 0;
    }

    if (lblk < 1 + UFS_PTRS_PER_BLOCK)
    {
        int idx = lblk - 1;
        if (in.indirect == 0)
        {
            if (!alloc)
            {
                *phys = 0;
                return 0;
            }
            int ib = data_alloc(s);
            if (ib == 0)
                return -1;
            in.indirect = (uint32_t)ib;
            inode_write(s, ino, &in);
        }
        s->lower->read(s->lower->state, s->inode_below, (int)in.indirect,
                       map_ibuf);
        if (map_ibuf[idx] == 0)
        {
            if (!alloc)
            {
                *phys = 0;
                return 0;
            }
            int b = data_alloc(s);
            if (b == 0)
                return -1;
            map_ibuf[idx] = (uint32_t)b;
            s->lower->write(s->lower->state, s->inode_below,
                            (int)in.indirect, map_ibuf);
        }
        *phys = map_ibuf[idx];
        return 0;
    }

    int idx = lblk - (1 + UFS_PTRS_PER_BLOCK);
    int l1 = idx / UFS_PTRS_PER_BLOCK;
    int l2 = idx % UFS_PTRS_PER_BLOCK;
    if (l1 >= UFS_PTRS_PER_BLOCK)
        die("ufs_map: bad double-indirect index");

    if (in.dindirect == 0)
    {
        if (!alloc)
        {
            *phys = 0;
            return 0;
        }
        int db = data_alloc(s);
        if (db == 0)
            return -1;
        in.dindirect = (uint32_t)db;
        inode_write(s, ino, &in);
    }

    s->lower->read(s->lower->state, s->inode_below, (int)in.dindirect,
                   map_dbuf);
    if (map_dbuf[l1] == 0)
    {
        if (!alloc)
        {
            *phys = 0;
            return 0;
        }
        int ib = data_alloc(s);
        if (ib == 0)
            return -1;
        map_dbuf[l1] = (uint32_t)ib;
        s->lower->write(s->lower->state, s->inode_below, (int)in.dindirect,
                        map_dbuf);
    }

    s->lower->read(s->lower->state, s->inode_below, (int)map_dbuf[l1],
                   map_ibuf);
    if (map_ibuf[l2] == 0)
    {
        if (!alloc)
        {
            *phys = 0;
            return 0;
        }
        int b = data_alloc(s);
        if (b == 0)
            return -1;
        map_ibuf[l2] = (uint32_t)b;
        s->lower->write(s->lower->state, s->inode_below, (int)map_dbuf[l1],
                        map_ibuf);
    }
    *phys = map_ibuf[l2];
    return 0;
}

int ufs_alloc(void *st)
{
    struct ufs_state *s = st;
    for (int i = 1; i <= s->max_inode; i++)
    {
        struct ufs_inode in;
        inode_read(s, i, &in);
        if (in.alloc == 0)
        {
            memset(&in, 0, sizeof(in));
            in.alloc = 1;
            inode_write(s, i, &in);
            return i;
        }
    }
    return 0;
}

int ufs_size(void *st, int inode)
{
    (void)st;
    (void)inode;
    return UFS_MAX_BLOCKS;
}

void ufs_read(void *st, int inode, int blk, void *dst)
{
    struct ufs_state *s = st;
    if (blk < 0 || blk >= UFS_MAX_BLOCKS)
        die("ufs_read: bad offset");
    uint32_t phys;
    (void)ufs_map(s, inode, blk, 0, &phys);
    if (phys == 0)
        memset(dst, 0, BLOCK_SIZE);
    else
        s->lower->read(s->lower->state, s->inode_below, (int)phys, dst);
}

void ufs_write(void *st, int inode, int blk, const void *src)
{
    struct ufs_state *s = st;
    if (blk < 0 || blk >= UFS_MAX_BLOCKS)
        die("ufs_write: bad offset");
    uint32_t phys;
    if (ufs_map(s, inode, blk, 1, &phys) != 0)
        die("ufs_write: disk full");
    s->lower->write(s->lower->state, s->inode_below, (int)phys, src);
}

void ufs_free(void *st, int inode)
{
    struct ufs_state *s = st;
    if (inode < 1 || inode > s->max_inode)
        return;
    struct ufs_inode in;
    inode_read(s, inode, &in);
    if (in.alloc == 0 && in.direct == 0 && in.indirect == 0 &&
        in.dindirect == 0)
        return;
    if (in.direct)
        data_free(s, (int)in.direct);
    if (in.indirect)
        ufs_free_indirect(s, in.indirect);
    if (in.dindirect)
        ufs_free_dindirect(s, in.dindirect);
    memset(&in, 0, sizeof(in));
    inode_write(s, inode, &in);
}

void ufs_init(struct bd *iface, struct ufs_state *s, struct bd *lower,
              int inode_below, int n_inodes)
{
    memset(s, 0, sizeof(*s));
    s->lower = lower;
    s->inode_below = inode_below;

    int T = lower->size(lower->state, inode_below);
    if (T < 2)
        die("ufs_init: disk too small");

    int n_ib_need = (n_inodes + UFS_INODES_PER_BLOCK - 1) / UFS_INODES_PER_BLOCK;
    if (n_ib_need < 1)
        n_ib_need = 1;
    if (1 + n_ib_need >= T)
        die("ufs_init: inode region does not leave room for data blocks");

    sb_load(s);

    int need_format = (s->sb.magic != UFS_MAGIC ||
                       s->sb.total_blocks != (uint32_t)T ||
                       (int)s->sb.n_inode_blocks < n_ib_need);

    if (need_format)
        ufs_format(s, n_ib_need, T);

    int n_ib = (int)s->sb.n_inode_blocks;
    if (n_ib < 1 || 1 + n_ib > T)
        die("ufs_init: corrupt superblock");
    s->first_data_block = 1 + n_ib;
    s->max_inode = n_ib * UFS_INODES_PER_BLOCK;

    ufs_mount_check(s);

    iface->state = s;
    iface->alloc = ufs_alloc;
    iface->size = ufs_size;
    iface->read = ufs_read;
    iface->write = ufs_write;
    iface->free = ufs_free;
}
