#include "dedup.h"
#include "bmap.h"
#include "dat.h"
#include <linux/nilfs2_api.h>
#include "mdt.h"
#include "page.h"
#include "segment.h"
#include <linux/types.h>
#include <linux/fs.h>
#include "nilfs.h"
#include <linux/vmalloc.h>

static void
nilfs_dedup_read_block(const struct the_nilfs *nilfs,
		       struct inode *just_some_random_inode,
		       const struct nilfs_deduplication_block *block)
{
	struct nilfs_root *root = NILFS_I(just_some_random_inode)->i_root;
	struct inode *inode = nilfs_iget(nilfs->ns_sb, root, block->ino);
	struct buffer_head *bh =
		nilfs_grab_buffer(inode, inode->i_mapping, block->offset, 0);
	const blk_opf_t opf = REQ_SYNC | REQ_OP_READ;
	sector_t blocknr;
	struct super_block *sb = nilfs->ns_sb;

	BUG_ON(nilfs_dat_translate(nilfs->ns_dat, block->vblocknr, &blocknr));

	map_bh(bh, nilfs->ns_sb, (sector_t)blocknr);
	bh->b_end_io = end_buffer_read_sync;
	get_bh(bh);
	lock_buffer(bh);
	submit_bh(opf, bh);

	nilfs_debug(sb, "CONTENT: '%s'", bh->b_data);

	unlock_page(bh->b_page);
	put_page(bh->b_page);
	brelse(bh);
	iput(inode);
}

static void
nilfs_dedup_print_block_info(const struct nilfs_deduplication_block *block,
			     const struct the_nilfs *nilfs, struct inode *i)
{
	sector_t blocknr;
	struct super_block *sb = nilfs->ns_sb;

	int ret = nilfs_dat_translate(nilfs->ns_dat, block->vblocknr, &blocknr);

	nilfs_info(
		sb,
		"BLOCK: ino=%ld, cno=%ld, vblocknr=%ld, blocknr=%ld, offset=%ld, dat_translated=%ld, dat_ret=%d",
		block->ino, block->cno, block->vblocknr, block->blocknr,
		block->offset, blocknr, ret);

	nilfs_dedup_read_block(nilfs, i, block);
}

static bool
nilfs_dedup_is_block_in_dat(struct the_nilfs *nilfs,
			    const struct nilfs_deduplication_block *block)
{
	sector_t blocknr;
	struct super_block *sb = nilfs->ns_sb;

	const int ret =
		nilfs_dat_translate(nilfs->ns_dat, block->vblocknr, &blocknr);
	if (ret < 0) {
		nilfs_info(
			sb,
			"Block with vblocknr = %ld not found in DAT, skipping",
			block->vblocknr);
		return false;
	}

	return true;
}

static int
nilfs_dedup_reclaim_candidate_dat(struct the_nilfs *nilfs,
				  struct nilfs_deduplication_block *out_info)
{
	struct super_block *sb = nilfs->ns_sb;
	struct inode *inode = nilfs->ns_dat;
	blkcnt_t offset = 0;
	blkcnt_t blknum = 0;
	const blkcnt_t max_blocks = inode->i_blocks * nilfs->ns_blocksize;
	int ret = 0;

	for (offset = 0; offset < max_blocks; ++offset) {
		ret = nilfs_bmap_lookup(NILFS_I(inode)->i_bmap, offset,
					&blknum);
		if (ret)
			continue;

		if (blknum == out_info->blocknr)
			break;
	}

	// block not found in bmap
	BUG_ON(offset >= max_blocks);

	nilfs_info(
		sb,
		"dat inode block with i_blocks = %ld, max_blocks = %ld, blocknr = %ld, offset = %ld",
		inode->i_blocks, max_blocks, out_info->blocknr, offset);

	out_info->offset = offset;

	// dat blocks are managed by mdt not dat, therefore no vblocknr
	out_info->vblocknr = -1;
	return ret;
}

static int
nilfs_dedup_reclaim_candidate(struct the_nilfs *nilfs,
			      struct nilfs_deduplication_block *out_info)
{
	int ret = 0;
	struct super_block *sb = nilfs->ns_sb;

	out_info->cno = -1; // field not needed

	ret = nilfs_get_last_block_in_latest_psegment(nilfs, out_info);
	if (ret < 0)
		return ret;

	if (out_info->ino >= NILFS_FIRST_INO(sb)) {
		// TODO
		// handle normal blocks via nilfs_dat_move
		BUG();
	} else {
		if (out_info->ino == NILFS_DAT_INO) {
			nilfs_dedup_reclaim_candidate_dat(nilfs, out_info);
		} else {
			// TODO
			// remember that vblocknr and offset are GARBAGE
			// handle different inode types
			BUG();

			// struct inode *inode = NULL;
			// blkcnt_t block_count;
			// blkcnt_t block_number;

			// inode = nilfs_iget_locked(sb, NULL, out_info->ino);
			// if (IS_ERR(inode)) {
			// 	nilfs_error(sb, "cannot get ino: %d",
			// 		    PTR_ERR(inode));
			// 	return PTR_ERR(inode);
			// }

			// block_count = inode_block_count(inode);

			// for (block_number = 0; block_number < block_count;
			//      ++block_number) {
			// }
			// iput(inode);
		}
	}

	nilfs_info(
		sb,
		"found block for relocation: ino = %ld, blocknr = %ld, vblocknr = %ld, offset = %ld",
		out_info->ino, out_info->blocknr, out_info->vblocknr,
		out_info->offset);

	return ret;
}

static int nilfs_dedup_reclaim(struct the_nilfs *nilfs, sector_t free_blocknr)
{
	int ret = 0;
	struct super_block *sb = nilfs->ns_sb;

	// candidate block for moving into free_blocknr spot
	struct nilfs_deduplication_block reclaim_candidate;

	ret = nilfs_dedup_reclaim_candidate(nilfs, &reclaim_candidate);
	if (ret < 0) {
		nilfs_warn(sb, "Failed to find candidate: ret = %d", ret);
		return ret;
	}

	return ret;
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

	nilfs_debug(sb, "Starting deduplication");

	BUG_ON(!blocks);
	src = &blocks[0];
	BUG_ON(!src);
	BUG_ON(blocks_count < 2);
	nilfs_debug(sb, "SRC: ");
	nilfs_dedup_print_block_info(src, nilfs, inode);

	// TODO check if needed
	if (nilfs_sb_need_update(nilfs))
		set_nilfs_discontinued(nilfs);

	// TODO
	// check if we need to mark bdevs DAT blocks as dirty
	// as in GC case

	for (size_t i = 1; i < blocks_count; ++i) {
		const struct nilfs_deduplication_block *dst = &blocks[i];
		BUG_ON(!dst);

		nilfs_debug(sb, "Before deduplication DST: ");
		nilfs_dedup_print_block_info(dst, nilfs, inode);

		if (!nilfs_dedup_is_block_in_dat(nilfs, dst))
			continue;

		ret = nilfs_dat_translate(nilfs->ns_dat, dst->vblocknr,
					  &blocknr);
		if (ret < 0) {
			nilfs_debug(sb, "Block DAT not found, skipping");
			continue;
		}

		ret = nilfs_change_blocknr(bmap, dst->vblocknr, src->blocknr);
		if (ret < 0) {
			nilfs_warn(
				sb,
				"Deduplication failed for block %lld with code %d",
				dst->vblocknr, ret);

			continue;
		}

		ret = nilfs_dedup_reclaim(nilfs, dst->blocknr);
		if (ret < 0) {
			nilfs_warn(
				sb,
				"Failed to reclaim space after deduplication");
			continue;
		}

		nilfs_debug(sb, "After deduplication DST: ");
		nilfs_dedup_print_block_info(dst, nilfs, inode);
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