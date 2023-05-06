#include "dedup.h"
#include "bmap.h"
#include "dat.h"
#include "linux/buffer_head.h"
#include "linux/nilfs2_api.h"
#include "mdt.h"
#include "page.h"
#include "segment.h"
#include <linux/types.h>
#include <linux/fs.h>
#include "nilfs.h"
#include <linux/vmalloc.h>

static void print_block_info(const struct nilfs_deduplication_block *block,
			     const struct the_nilfs *nilfs)
{
	sector_t blocknr;
	struct super_block *sb = nilfs->ns_sb;

	int ret = nilfs_dat_translate(nilfs->ns_dat, block->vblocknr, &blocknr);

	nilfs_info(
		sb,
		"BLOCK: ino=%ld, cno=%ld, vblocknr=%ld, blocknr=%ld, offset=%ld, dat_translated=%ld, dat_ret=%d",
		block->ino, block->cno, block->vblocknr, block->blocknr,
		block->offset, blocknr, ret);
}

static int
nilfs_dedup_move_inode_block(struct inode *inode,
			     const struct nilfs_deduplication_block *block,
			     struct list_head *buffers)
{
	struct buffer_head *bh;
	int ret;

	ret = nilfs_gccache_submit_read_data(
		inode, block->offset, block->blocknr, block->vblocknr, &bh);

	if (unlikely(ret < 0)) {
		if (ret == -ENOENT)
			nilfs_crit(
				inode->i_sb,
				"%s: invalid virtual block address (data): ino=%llu, cno=%llu, offset=%llu, blocknr=%llu, vblocknr=%llu",
				__func__, (unsigned long long)block->ino,
				(unsigned long long)block->cno,
				(unsigned long long)block->offset,
				(unsigned long long)block->blocknr,
				(unsigned long long)block->vblocknr);
		return ret;
	}
	if (unlikely(!list_empty(&bh->b_assoc_buffers))) {
		nilfs_crit(
			inode->i_sb,
			"%s: conflicting data buffer: ino=%llu, cno=%llu, offset=%llu, blocknr=%llu, vblocknr=%llu",
			__func__, (unsigned long long)block->ino,
			(unsigned long long)block->cno,
			(unsigned long long)block->offset,
			(unsigned long long)block->blocknr,
			(unsigned long long)block->vblocknr);
		brelse(bh);
		return -EEXIST;
	}
	list_add_tail(&bh->b_assoc_buffers, buffers);
	return 0;
}

static int
nilfs_dedup_move_blocks(struct super_block *sb,
			const struct nilfs_deduplication_block *blocks,
			size_t blocks_count)
{
	struct the_nilfs *nilfs = sb->s_fs_info;
	struct inode *inode;
	struct buffer_head *bh, *n;
	LIST_HEAD(buffers);
	ino_t ino;
	__u64 cno;
	int i, ret;

	// we skip first one since, it is a source block
	for (i = 1; i < blocks_count; ++i) {
		const struct nilfs_deduplication_block *block = &blocks[i];

		ino = block->ino;
		cno = block->cno;
		inode = nilfs_iget_for_gc(sb, ino, cno);
		nilfs_info(sb, "moving inode=%ld", block->ino);

		if (IS_ERR(inode)) {
			nilfs_err(sb, "could not get gc inode");
			ret = PTR_ERR(inode);
			goto failed;
		}

		if (list_empty(&NILFS_I(inode)->i_dirty)) {
			/*
			 * Add the inode to GC inode list. Garbage Collection
			 * is serialized and no two processes manipulate the
			 * list simultaneously.
			 */
			igrab(inode);
			list_add(&NILFS_I(inode)->i_dirty,
				 &nilfs->ns_gc_inodes);
		}

		do {
			ret = nilfs_dedup_move_inode_block(inode, block,
							   &buffers);
			if (unlikely(ret < 0)) {
				iput(inode);
				goto failed;
			}
			block++;
		} while (++i < blocks_count && block->ino == ino);

		iput(inode); /* The inode still remains in GC inode list */
	}

	list_for_each_entry_safe(bh, n, &buffers, b_assoc_buffers) {
		ret = nilfs_gccache_wait_and_mark_dirty(bh);
		if (unlikely(ret < 0)) {
			WARN_ON(ret == -EEXIST);
			goto failed;
		}

		BUG_ON((ret = nilfs_bmap_propagate(NILFS_I(inode)->i_bmap,
						   bh)) < 0);

		list_del_init(&bh->b_assoc_buffers);
		brelse(bh);
	}
	return 0;

failed:
	list_for_each_entry_safe(bh, n, &buffers, b_assoc_buffers) {
		list_del_init(&bh->b_assoc_buffers);
		brelse(bh);
	}
	return ret;
}

static __u64 *
make_destination_vblocknrs(const struct nilfs_deduplication_block *blocks,
			   size_t blocks_count)
{
	size_t count = blocks_count - 1;
	__u64 *vblocknrs = vmalloc(sizeof(__u64) * count);
	size_t i;

	for (i = 1; i < blocks_count; ++i) {
		vblocknrs[i - 1] = blocks[i].vblocknr;
	}

	return vblocknrs;
}

int nilfs_dedup(struct inode *inode,
		const struct nilfs_deduplication_block *blocks,
		size_t blocks_count)
{
	struct super_block *sb = inode->i_sb;
	const struct nilfs_deduplication_block *src;
	struct the_nilfs *nilfs = sb->s_fs_info;
	struct inode *dat = nilfs->ns_dat;
	struct nilfs_bmap *bmap = NILFS_I(dat)->i_bmap;
	sector_t blocknr;
	__u64 *vblocknrs = make_destination_vblocknrs(blocks, blocks_count);
	size_t vblocknrs_count = blocks_count - 1;
	int ret = 0;

	nilfs_info(sb, "Starting deduplication");

	BUG_ON(!blocks);
	src = &blocks[0];
	BUG_ON(!src);
	BUG_ON(blocks_count < 2);
	print_block_info(src, nilfs);

	// BUG_ON(nilfs_dedup_move_blocks(sb, blocks, blocks_count) < 0);

	if (nilfs_sb_need_update(nilfs))
		set_nilfs_discontinued(nilfs);

	// nilfs_dat_freev(dat, vblocknrs, vblocknrs_count);
	// nilfs_mdt_mark_dirty(dat);

	// TODO
	// check if we need to mark bdevs DAT blocks as dirty
	// as in GC case

	for (size_t i = 1; i < blocks_count; ++i) {
		const struct nilfs_deduplication_block *dst = &blocks[i];
		WARN_ON(!dst);

		print_block_info(dst, nilfs);

		if (nilfs_change_blocknr(bmap, dst->vblocknr, src->blocknr) <
		    0) {
			nilfs_warn(sb, "Dedplication failed for block %lld",
				   dst->vblocknr);
			continue;
		}

		// nilfs_dat_translate(dat, dst->vblocknr, &blocknr);
		// nilfs_info(sb, "DST: blocknr=%ld", blocknr);
		// BUG_ON(blocknr != src->blocknr);
	}

	nilfs_flush_constructor(nilfs);

	nilfs_remove_all_gcinodes(nilfs);
	clear_nilfs_gc_running(nilfs);
	vfree(vblocknrs);

	return ret;
}