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
			"src attempting to free non standard inode: vblocknr = %d, blocknr = %d, start = %d, end = %d, state = %d, ref = %d",
			vblocknr, entry->de_blocknr, entry->de_start,
			entry->de_end, entry->de_state,
			entry->de_reference_count);
		kunmap_atomic(kaddr);
		brelse(entry_bh);
		return -ENOENT;
	}

	entry->de_state = NILFS_DAT_STATE_SOURCE;
	entry->de_reference_count = 2;
	nilfs_info(
		dat->i_sb,
		"created src inode: vblocknr = %d, blocknr = %d, start = %d, end = %d, state = %d, ref = %d",
		vblocknr, entry->de_blocknr, entry->de_start, entry->de_end,
		entry->de_state, entry->de_reference_count);
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
	// no support for linked destination entries
	if (entry->de_state != NILFS_DAT_STATE_STANDARD) {
		nilfs_warn(
			dat->i_sb,
			"dst attempting to free non standard inode: vblocknr = %d, blocknr = %d, start = %d, end = %d, state = %d, ref = %d",
			dst_vblocknr, entry->de_blocknr, entry->de_start,
			entry->de_end, entry->de_state,
			entry->de_reference_count);
		kunmap_atomic(kaddr);
		brelse(entry_bh);
		return -ENOENT;
	}

	if (src_vblocknr == dst_vblocknr) {
		nilfs_warn(
			dat->i_sb,
			"attempting to assign inode to itself: vblocknr = %d",
			src_vblocknr);
		kunmap_atomic(kaddr);
		brelse(entry_bh);
		return -ENOENT;
	}

	entry->de_state = NILFS_DAT_STATE_DESTINATION;
	entry->de_reference_count = 1;
	entry->de_blocknr = src_vblocknr;
	nilfs_info(
		dat->i_sb,
		"created dst inode: vblocknr = %d, blocknr = %d, start = %d, end = %d, state = %d, ref = %d",
		dst_vblocknr, entry->de_blocknr, entry->de_start, entry->de_end,
		entry->de_state, entry->de_reference_count);
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
	int ret = 0;

	// TODO
	// check if we need gcflag set
	nilfs_transaction_lock(sb, &ti, 1);

	ret = nilfs_dat_dedup(dat, src->vblocknr, dst->vblocknr);
	if (ret < 0) {
		nilfs_warn(sb, "nilfs_dat_dedup failed");
		nilfs_mdt_clear_dirty(dat);
		goto out;
	}

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

int nilfs_dedup(struct inode *inode,
		const struct nilfs_deduplication_block *blocks,
		size_t blocks_count)
{
	struct super_block *sb = inode->i_sb;
	const struct nilfs_deduplication_block *src;
	struct the_nilfs *nilfs = sb->s_fs_info;
	struct inode *dat = nilfs->ns_dat;
	struct nilfs_bmap *bmap = NILFS_I(dat)->i_bmap;
	int ret = 0;
	uint64_t deduplicated = 0;

	nilfs_info(sb, "Starting deduplication of %d blocks", blocks_count - 1);

	if (blocks_count > 2) {
		nilfs_warn(sb,
			   "Multiple block dedup is not supported, skipping");
		goto out;
	}

	BUG_ON(!blocks);
	src = &blocks[0];
	BUG_ON(!src);
	BUG_ON(blocks_count < 2);

	if (nilfs_sb_need_update(nilfs))
		set_nilfs_discontinued(nilfs);

	for (size_t i = 1; i < blocks_count; ++i) {
		const struct nilfs_deduplication_block *dst = &blocks[i];
		BUG_ON(!dst);

		ret = nilfs_change_blocknr(bmap, src, dst);
		if (ret < 0) {
			nilfs_warn(
				sb,
				"Deduplication failed for block %lld with code %d",
				dst->vblocknr, ret);

			continue;
		}

		++deduplicated;
	}

out:
	nilfs_info(sb, "Finished deduplication, deduplicated %ld blocks",
		   deduplicated);

	// TODO check if needed
	nilfs_remove_all_gcinodes(nilfs);

	// TODO check if needed
	clear_nilfs_gc_running(nilfs);

	return 0;
}