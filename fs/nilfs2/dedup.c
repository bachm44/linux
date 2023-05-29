#include "dedup.h"
#include "bmap.h"
#include "dat.h"
#include <linux/fs.h>
#include <linux/nilfs2_ondisk.h>
#include <linux/nilfs2_api.h>
#include "mdt.h"
#include "page.h"
#include "segment.h"
#include <linux/types.h>
#include <linux/fs.h>
#include "nilfs.h"
#include <linux/vmalloc.h>

static void
nilfs_dedup_print_block_info(const struct nilfs_deduplication_block *block,
			     const struct the_nilfs *nilfs, struct inode *i)
{
	sector_t blocknr;
	struct super_block *sb = nilfs->ns_sb;

	int ret = nilfs_dat_translate(nilfs->ns_dat, block->vblocknr, &blocknr);

	nilfs_debug(
		sb,
		"BLOCK: ino=%ld, cno=%ld, vblocknr=%ld, blocknr=%ld, offset=%ld, dat_translated=%ld, dat_ret=%d",
		block->ino, block->cno, block->vblocknr, block->blocknr,
		block->offset, blocknr, ret);
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
		nilfs_warn(
			sb,
			"Block with vblocknr = %ld not found in DAT, skipping",
			block->vblocknr);
		return false;
	}

	return true;
}

static sector_t nilfs_dat_last_key(struct inode *dat, sector_t min_vblocknr)
{
	struct buffer_head *bh = NULL;

	min_vblocknr += 100000; // FIXME
	while (nilfs_palloc_get_entry_block(dat, min_vblocknr, false, &bh) >= 0)
		++min_vblocknr;

	// nilfs_bmap_last_key(NILFS_I(dat)->i_bmap, &min_vblocknr);

	return min_vblocknr;
}

static int nilfs_dat_make(struct inode *dat, sector_t blocknr,
			  sector_t min_vblocknr, sector_t *vblocknr)
{
	__u64 out;
	__u64 last_key = nilfs_dat_last_key(dat, min_vblocknr);
	struct buffer_head *bh = NULL;
	int ret = 0;
	void *kaddr = NULL;
	struct super_block *sb = dat->i_sb;
	struct the_nilfs *nilfs = sb->s_fs_info;
	struct nilfs_dat_entry *entry = NULL;

	nilfs_debug(
		sb,
		"searching for last vblocknr: min vblocknr = %ld, last_key = %ld",
		min_vblocknr, last_key);
	BUG_ON(nilfs_dat_translate(dat, last_key, &out) >= 0);

	ret = nilfs_palloc_get_entry_block(dat, last_key, true, &bh);
	if (ret < 0)
		return ret;

	kaddr = kmap_atomic(bh->b_page);
	entry = nilfs_palloc_block_get_entry(dat, last_key, bh, kaddr);
	entry->de_blocknr = cpu_to_le64(blocknr);
	entry->de_end = 0;
	entry->de_start = 0;
	entry->de_reference_count = 1;
	entry->de_state = NILFS_DAT_STATE_STANDARD;
	kunmap_atomic(kaddr);

	mark_buffer_dirty(bh);
	nilfs_mdt_mark_dirty(dat);

	struct nilfs_palloc_req req = { .pr_entry_nr = last_key,
					.pr_entry_bh = bh };
	ret = nilfs_dat_prepare_alloc(dat, &req);
	if (ret < 0) {
		nilfs_warn(sb, "Failed to prepare dat alloc: ret %d", ret);
		return ret;
	}

	brelse(bh);

	ret = nilfs_segctor_write_block(last_key, nilfs);
	if (ret < 0) {
		nilfs_warn(sb, "Failed to write vblocknr = %ld, ret = %d",
			   last_key, ret);
		return ret;
	}

	BUG_ON(nilfs_dat_translate(dat, last_key, &out) < 0);
	BUG_ON(out != blocknr);

	*vblocknr = last_key;
	return ret;
}

static int nilfs_free_blocknr(struct inode *dat, sector_t dst_vblocknr,
			      sector_t free_blocknr)
{
	sector_t vblocknr;
	struct super_block *sb = dat->i_sb;
	int ret = 0;
	sector_t vblocknrs[1];

	ret = nilfs_dat_make(dat, free_blocknr, dst_vblocknr, &vblocknr);
	if (ret < 0) {
		nilfs_warn(sb, "Failed to create new DAT object: ret = %d",
			   ret);
		return ret;
	}

	// vblocknrs[0] = vblocknr;
	// ret = nilfs_dat_freev(dat, vblocknrs, 1);
	// if (ret < 0) {
	// 	nilfs_warn(sb, "Failed to free vblocknr %ld: ret = %d",
	// 		   vblocknr, ret);
	// 	return ret;
	// }

	return ret;
}

static int nilfs_dat_dedup_make_src(struct inode *dat, sector_t vblocknr)
{
	struct buffer_head *entry_bh;
	struct nilfs_dat_entry *entry;
	void *kaddr;
	int ret;

	ret = nilfs_palloc_get_entry_block(dat, vblocknr, 0, &entry_bh);
	if (ret < 0)
		return ret;

	/*
	 * The given disk block number (blocknr) is not yet written to
	 * the device at this point.
	 *
	 * To prevent nilfs_dat_translate() from returning the
	 * uncommitted block number, this makes a copy of the entry
	 * buffer and redirects nilfs_dat_translate() to the copy.
	 */
	if (!buffer_nilfs_redirected(entry_bh)) {
		ret = nilfs_mdt_freeze_buffer(dat, entry_bh);
		if (ret) {
			brelse(entry_bh);
			return ret;
		}
	}

	kaddr = kmap_atomic(entry_bh->b_page);
	entry = nilfs_palloc_block_get_entry(dat, vblocknr, entry_bh, kaddr);
	if (unlikely(entry->de_blocknr == cpu_to_le64(0))) {
		nilfs_crit(dat->i_sb,
			   "%s: invalid vblocknr = %llu, [%llu, %llu)",
			   __func__, (unsigned long long)vblocknr,
			   (unsigned long long)le64_to_cpu(entry->de_start),
			   (unsigned long long)le64_to_cpu(entry->de_end));
		kunmap_atomic(kaddr);
		brelse(entry_bh);
		return -EINVAL;
	}
	if (entry->de_state != NILFS_DAT_STATE_STANDARD) {
		nilfs_warn(
			dat->i_sb,
			"attempting to free non standard inode type %d, skipping",
			entry->de_state);
		return -ENOENT;
	}

	entry->de_state = NILFS_DAT_STATE_SOURCE;
	entry->de_reference_count = 2;
	kunmap_atomic(kaddr);

	mark_buffer_dirty(entry_bh);
	nilfs_mdt_mark_dirty(dat);

	brelse(entry_bh);
	return 0;
}

static int nilfs_dat_dedup_make_dst(struct inode *dat, sector_t src_vblocknr,
				    sector_t dst_vblocknr)
{
	struct buffer_head *entry_bh;
	struct nilfs_dat_entry *entry;
	void *kaddr;
	int ret;

	ret = nilfs_palloc_get_entry_block(dat, dst_vblocknr, 0, &entry_bh);
	if (ret < 0)
		return ret;

	/*
	 * The given disk block number (blocknr) is not yet written to
	 * the device at this point.
	 *
	 * To prevent nilfs_dat_translate() from returning the
	 * uncommitted block number, this makes a copy of the entry
	 * buffer and redirects nilfs_dat_translate() to the copy.
	 */
	if (!buffer_nilfs_redirected(entry_bh)) {
		ret = nilfs_mdt_freeze_buffer(dat, entry_bh);
		if (ret) {
			brelse(entry_bh);
			return ret;
		}
	}

	kaddr = kmap_atomic(entry_bh->b_page);
	entry = nilfs_palloc_block_get_entry(dat, dst_vblocknr, entry_bh,
					     kaddr);
	if (unlikely(entry->de_blocknr == cpu_to_le64(0))) {
		nilfs_crit(dat->i_sb,
			   "%s: invalid vblocknr = %llu, [%llu, %llu)",
			   __func__, (unsigned long long)dst_vblocknr,
			   (unsigned long long)le64_to_cpu(entry->de_start),
			   (unsigned long long)le64_to_cpu(entry->de_end));
		kunmap_atomic(kaddr);
		brelse(entry_bh);
		return -EINVAL;
	}
	entry->de_state = NILFS_DAT_STATE_DESTINATION;
	entry->de_reference_count = 1;
	entry->de_blocknr = src_vblocknr;
	kunmap_atomic(kaddr);

	mark_buffer_dirty(entry_bh);
	nilfs_mdt_mark_dirty(dat);

	brelse(entry_bh);
	return 0;
}

static int nilfs_dat_dedup(struct inode *dat, sector_t src_vblocknr,
			   sector_t dst_vblocknr)
{
	const int ret = nilfs_dat_dedup_make_src(dat, src_vblocknr);
	if (ret < 0)
		return ret;

	return nilfs_dat_dedup_make_dst(dat, src_vblocknr, dst_vblocknr);
}

static int nilfs_change_blocknr(struct nilfs_bmap *bmap,
				const struct nilfs_deduplication_block *src,
				const struct nilfs_deduplication_block *dst)
{
	struct nilfs_transaction_info ti;
	struct super_block *sb = bmap->b_inode->i_sb;
	struct the_nilfs *nilfs = sb->s_fs_info;
	struct nilfs_sc_info *sci = nilfs->ns_writer;
	struct inode *dat = nilfs->ns_dat;
	sector_t free_blocknr;
	int ret = 0;

	// TODO
	// check if we need gcflag set
	nilfs_transaction_lock(sb, &ti, 1);

	ret = nilfs_dat_translate(dat, dst->vblocknr, &free_blocknr);
	if (ret < 0)
		goto out;

	ret = nilfs_dat_dedup(dat, src->vblocknr, dst->vblocknr);
	if (ret < 0) {
		nilfs_warn(sb, "nilfs_dat_dedup failed");
		nilfs_segctor_abort_construction(sci, nilfs, ret);
		nilfs_mdt_clear_dirty(dat);
		goto out;
	}

	ret = nilfs_free_blocknr(dat, dst->vblocknr, free_blocknr);
	if (ret < 0)
		goto out;

	ret = nilfs_segctor_move_block(sci);
	if (ret < 0) {
		nilfs_warn(sb, "Failed to write vblocknr = %ld, ret = %d",
			   dst->vblocknr, ret);
		goto out;
	}

out:
	nilfs_transaction_unlock(sb);
	return ret;
}

static void nilfs_dedup_read_block(
	const struct the_nilfs *nilfs, struct inode *just_some_random_inode,
	const struct nilfs_deduplication_block *block, struct buffer_head **out)
{
	struct nilfs_root *root = NILFS_I(just_some_random_inode)->i_root;
	struct inode *inode =
		nilfs_iget_for_gc(nilfs->ns_sb, block->ino, block->cno);
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

	*out = bh;

	// unlock_page(bh->b_page);
	// put_page(bh->b_page);
	// brelse(bh);
	iput(inode);
}

static void nilfs_dedup_validate(struct the_nilfs *nilfs, struct inode *inode,
				 const struct nilfs_deduplication_block *src,
				 const struct nilfs_deduplication_block *dst)
{
	struct buffer_head *src_bh = NULL;
	struct buffer_head *dst_bh = NULL;

	nilfs_dedup_read_block(nilfs, inode, src, &src_bh);
	nilfs_dedup_read_block(nilfs, inode, src, &dst_bh);

	if (memcmp(src_bh->b_data, dst_bh->b_data, nilfs->ns_blocksize) != 0) {
		nilfs_warn(nilfs->ns_sb,
			   "blocks are not the same: '%s' != '%s'",
			   src_bh->b_data, dst_bh->b_data);
	}

	unlock_page(src_bh->b_page);
	put_page(src_bh->b_page);
	brelse(src_bh);
	unlock_page(dst_bh->b_page);
	put_page(dst_bh->b_page);
	brelse(dst_bh);
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
	sector_t blocknr = 0;
	int ret = 0;
	uint64_t deduplicated = 0;

	nilfs_info(sb, "Starting deduplication of %d blocks", blocks_count);

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

		ret = nilfs_change_blocknr(bmap, src, dst);
		if (ret < 0) {
			nilfs_warn(
				sb,
				"Deduplication failed for block %lld with code %d",
				dst->vblocknr, ret);

			continue;
		}

		nilfs_debug(sb, "After deduplication DST: ");
		nilfs_dedup_print_block_info(dst, nilfs, inode);

		// nilfs_dat_translate(dat, dst->vblocknr, &blocknr);
		// BUG_ON(blocknr != src->blocknr);
		// nilfs_dedup_validate(nilfs, inode, src, dst);

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