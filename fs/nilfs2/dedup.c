#include "dedup.h"
#include "bmap.h"
#include "dat.h"
#include "mdt.h"
#include "page.h"
#include "segment.h"
#include <linux/types.h>
#include <linux/fs.h>
#include "nilfs.h"
#include <linux/vmalloc.h>

static void print_block_info(const struct nilfs_deduplication_block *block,
			     const struct the_nilfs *nilfs, struct inode *i)
{
	sector_t blocknr;
	struct super_block *sb = nilfs->ns_sb;
	struct nilfs_root *root = NILFS_I(i)->i_root;
	struct inode *inode = nilfs_iget(nilfs->ns_sb, root, block->ino);
	struct buffer_head *bh =
		nilfs_grab_buffer(inode, inode->i_mapping, block->offset, 0);
	const blk_opf_t opf = REQ_SYNC | REQ_OP_READ;

	int ret = nilfs_dat_translate(nilfs->ns_dat, block->vblocknr, &blocknr);

	nilfs_info(
		sb,
		"BLOCK: ino=%ld, cno=%ld, vblocknr=%ld, blocknr=%ld, offset=%ld, dat_translated=%ld, dat_ret=%d",
		block->ino, block->cno, block->vblocknr, block->blocknr,
		block->offset, blocknr, ret);

	map_bh(bh, nilfs->ns_sb, (sector_t)blocknr);
	bh->b_end_io = end_buffer_read_sync;
	get_bh(bh);
	lock_buffer(bh);
	submit_bh(opf, bh);

	nilfs_info(nilfs->ns_sb, "CONTENT: '%s'", bh->b_data);

	unlock_page(bh->b_page);
	put_page(bh->b_page);
	brelse(bh);
	iput(inode);
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
	int ret = 0;
	uint64_t deduplicated = 0;

	nilfs_info(sb, "Starting deduplication");

	BUG_ON(!blocks);
	src = &blocks[0];
	BUG_ON(!src);
	BUG_ON(blocks_count < 2);
	nilfs_info(sb, "SRC: ");
	print_block_info(src, nilfs, inode);

	// TODO check if needed
	if (nilfs_sb_need_update(nilfs))
		set_nilfs_discontinued(nilfs);

	// TODO
	// check if we need to mark bdevs DAT blocks as dirty
	// as in GC case

	for (size_t i = 1; i < blocks_count; ++i) {
		const struct nilfs_deduplication_block *dst = &blocks[i];
		WARN_ON(!dst);

		nilfs_info(sb, "Before deduplication DST: ");
		print_block_info(dst, nilfs, inode);
		if ((ret = nilfs_dat_translate(nilfs->ns_dat, dst->vblocknr,
					       &blocknr)) < 0) {
			nilfs_info(sb, "Block DAT not found, skipping");
			continue;
		}

		if ((ret = nilfs_change_blocknr(bmap, dst->vblocknr,
						src->blocknr) < 0)) {
			nilfs_warn(
				sb,
				"Deduplication failed for block %lld with code %d",
				dst->vblocknr, ret);

			BUG_ON(nilfs_change_blocknr(bmap, dst->vblocknr,
						    dst->blocknr) < 0);
			continue;
		}

		nilfs_info(sb, "After deduplication DST: ");
		print_block_info(dst, nilfs, inode);
		nilfs_dat_translate(dat, dst->vblocknr, &blocknr);

		BUG_ON(blocknr != src->blocknr);
		++deduplicated;
	}

	nilfs_info(sb, "Finished deduplication, deduplicated %ld blocks",
		   deduplicated);

	// TODO check if needed
	nilfs_remove_all_gcinodes(nilfs);

	// TODO check if needed
	clear_nilfs_gc_running(nilfs);

	return 0;
}