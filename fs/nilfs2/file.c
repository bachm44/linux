// SPDX-License-Identifier: GPL-2.0+
/*
 * NILFS regular file handling primitives including fsync().
 *
 * Copyright (C) 2005-2008 Nippon Telegraph and Telephone Corporation.
 *
 * Written by Amagai Yoshiji and Ryusuke Konishi.
 */

#include "asm-generic/errno-base.h"
#include "bmap.h"
#include "dat.h"
#include "ifile.h"
#include "linux/bitops.h"
#include "linux/buffer_head.h"
#include "linux/compiler.h"
#include "linux/errno.h"
#include "linux/gfp_types.h"
#include "linux/list.h"
#include "linux/page-flags.h"
#include "linux/types.h"
#include "mdt.h"
#include "page.h"
#include "the_nilfs.h"
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/writeback.h>
#include "nilfs.h"
#include "segment.h"

int nilfs_sync_file(struct file *file, loff_t start, loff_t end, int datasync)
{
	/*
	 * Called from fsync() system call
	 * This is the only entry point that can catch write and synch
	 * timing for both data blocks and intermediate blocks.
	 *
	 * This function should be implemented when the writeback function
	 * will be implemented.
	 */
	struct the_nilfs *nilfs;
	struct inode *inode = file->f_mapping->host;
	int err = 0;

	if (nilfs_inode_dirty(inode)) {
		if (datasync)
			err = nilfs_construct_dsync_segment(inode->i_sb, inode,
							    start, end);
		else
			err = nilfs_construct_segment(inode->i_sb);
	}

	nilfs = inode->i_sb->s_fs_info;
	if (!err)
		err = nilfs_flush_device(nilfs);

	return err;
}

static vm_fault_t nilfs_page_mkwrite(struct vm_fault *vmf)
{
	struct vm_area_struct *vma = vmf->vma;
	struct page *page = vmf->page;
	struct inode *inode = file_inode(vma->vm_file);
	struct nilfs_transaction_info ti;
	int ret = 0;

	if (unlikely(nilfs_near_disk_full(inode->i_sb->s_fs_info)))
		return VM_FAULT_SIGBUS; /* -ENOSPC */

	sb_start_pagefault(inode->i_sb);
	lock_page(page);
	if (page->mapping != inode->i_mapping ||
	    page_offset(page) >= i_size_read(inode) || !PageUptodate(page)) {
		unlock_page(page);
		ret = -EFAULT; /* make the VM retry the fault */
		goto out;
	}

	/*
	 * check to see if the page is mapped already (no holes)
	 */
	if (PageMappedToDisk(page))
		goto mapped;

	if (page_has_buffers(page)) {
		struct buffer_head *bh, *head;
		int fully_mapped = 1;

		bh = head = page_buffers(page);
		do {
			if (!buffer_mapped(bh)) {
				fully_mapped = 0;
				break;
			}
		} while (bh = bh->b_this_page, bh != head);

		if (fully_mapped) {
			SetPageMappedToDisk(page);
			goto mapped;
		}
	}
	unlock_page(page);

	/*
	 * fill hole blocks
	 */
	ret = nilfs_transaction_begin(inode->i_sb, &ti, 1);
	/* never returns -ENOMEM, but may return -ENOSPC */
	if (unlikely(ret))
		goto out;

	file_update_time(vma->vm_file);
	ret = block_page_mkwrite(vma, vmf, nilfs_get_block);
	if (ret) {
		nilfs_transaction_abort(inode->i_sb);
		goto out;
	}
	nilfs_set_file_dirty(inode, 1 << (PAGE_SHIFT - inode->i_blkbits));
	nilfs_transaction_commit(inode->i_sb);

mapped:
	wait_for_stable_page(page);
out:
	sb_end_pagefault(inode->i_sb);
	return block_page_mkwrite_return(ret);
}

static const struct vm_operations_struct nilfs_file_vm_ops = {
	.fault = filemap_fault,
	.map_pages = filemap_map_pages,
	.page_mkwrite = nilfs_page_mkwrite,
};

static int nilfs_file_mmap(struct file *file, struct vm_area_struct *vma)
{
	file_accessed(file);
	vma->vm_ops = &nilfs_file_vm_ops;
	return 0;
}

// #pragma GCC push_options
// #pragma GCC optimize("O0")

struct nilfs_remap_file_args {
	struct inode *src;
	loff_t src_off;
	loff_t len;
	struct inode *dst;
	loff_t dst_off;
};

static blkcnt_t inode_block_count(const struct inode *inode)
{
	const struct the_nilfs *nilfs = inode->i_sb->s_fs_info;
	return DIV_ROUND_UP(inode->i_size, nilfs->ns_blocksize);
}

static inline bool are_inodes_same_size(const struct inode *a,
					const struct inode *b)
{
	return a->i_size == b->i_size;
}

static bool compare_extents(const struct nilfs_remap_file_args *args)
{
	const blkcnt_t src_block_count = inode_block_count(args->src);

	struct super_block *sb = args->src->i_sb;
	struct buffer_head *src_bh = NULL;
	struct buffer_head *dst_bh = NULL;
	void *src_data = NULL;
	void *dst_data = NULL;
	int i = 0;

	nilfs_info(sb, "%s", __func__);

	nilfs_info(
		sb,
		"comparing inodes with src inode size = %d and dst inode size = %d, src_block_count = %d",
		args->src->i_size, args->dst->i_size, src_block_count);

	if (!are_inodes_same_size(args->src, args->dst)) {
		nilfs_info(sb, "inodes have different size");
		return 0;
	}

	while (i < src_block_count) {
		src_bh = nilfs_grab_buffer(args->src, args->src->i_mapping, i,
					   0);
		dst_bh = nilfs_grab_buffer(args->dst, args->dst->i_mapping, i,
					   0);

		if (!src_bh || !dst_bh) {
			nilfs_warn(sb, "cannot fetch buffer heads");
			return false;
		}

		src_data = (char *)src_bh->b_data;
		dst_data = (char *)dst_bh->b_data;

		// nilfs_info(sb, "comparing blocknr %d: %s == %s", i, src_data,
		// 	   dst_data);

		if (memcmp(src_data, dst_data, src_bh->b_size) != 0) {
			nilfs_warn(sb, "inodes have different data");
			brelse(src_bh);
			brelse(dst_bh);
			return false;
		}

		unlock_page(src_bh->b_page);
		put_page(src_bh->b_page);
		brelse(src_bh);

		unlock_page(dst_bh->b_page);
		put_page(dst_bh->b_page);
		brelse(dst_bh);
		++i;
	}

	return true;
}

/* since we do not want to introduce new flag for
   keeping information whether or not inode was deduplicated
   we use this flag. This means, that inode content keeps
   not actual content, but struct nilfs_dedup_info.
   TODO: WARNING: This may lead to very strange inode errors
*/
#define NILFS_DEDUP_FLAG S_CASEFOLD
#define IS_NILFS_DEDUP_INODE(inode) IS_CASEFOLDED(inode)

static int nilfs_reflink(const struct nilfs_remap_file_args *args)
{
	struct super_block *sb = args->src->i_sb;
	struct inode *src = args->src;
	struct inode *dst = args->dst;
	struct nilfs_inode_info *src_info = NILFS_I(src);
	struct nilfs_inode_info *dst_info = NILFS_I(dst);
	struct buffer_head *dst_bh = NULL;
	const struct nilfs_dedup_info dedup_info = { .ino = src->i_ino };

	nilfs_info(sb, "%s", __func__);
	nilfs_info(sb, "dedup_info = {.ino = %lld}", dedup_info.ino);

	if (IS_NILFS_DEDUP_INODE(dst_info)) {
		nilfs_warn(sb,
			   "Deduplication of dedup inodes is not supported");
		return -ENOTSUPP;
	}

	if (inode_block_count(args->src) > 1) {
		nilfs_warn(
			sb,
			"Deduplication of multiple-block inodes is not supported");
		return -ENOTSUPP;
	}

	inode_dio_wait(dst);
	truncate_setsize(dst, 0);
	nilfs_truncate(dst);

	// TODO remember to implement reference counting of inodes (if used for deduplication then do not remove)
	src_info->dedup_ref_count++;

	dst_info->i_flags |= NILFS_DEDUP_FLAG;

	dst_bh = nilfs_grab_buffer(args->dst, args->dst->i_mapping, 0, 0);

	if (unlikely(!dst_bh)) {
		nilfs_error(sb, "failed to grab buffer");
		BUG();
		return -ENOMEM;
	}

	get_bh(dst_bh);
	lock_buffer(dst_bh);

	dst_bh->b_bdev = args->dst->i_sb->s_bdev;
	// TODO remember to adjust blocknr if needed when implementing multiple-block setup
	dst_bh->b_blocknr = 0;
	// TODO remember to memcpy multiple blocks
	memcpy(dst_bh->b_data, &dedup_info, sizeof(struct nilfs_dedup_info));

	set_buffer_uptodate(dst_bh);
	set_buffer_mapped(dst_bh);
	mark_buffer_dirty(dst_bh);
	dst_bh->b_end_io = end_buffer_read_sync;
	unlock_buffer(dst_bh);
	put_bh(dst_bh);

	i_size_write(args->dst, sizeof(struct nilfs_dedup_info));
	set_page_dirty(dst_bh->b_page);

	unlock_page(dst_bh->b_page);
	put_page(dst_bh->b_page);
	nilfs_dirty_inode(dst, 0);

	/*  
	1. Remember about locks (to be added later since for now not
	considering using it concurrently when I/O operation is ongoing).
	*/

	return 0;
}

static bool files_the_same(const struct nilfs_remap_file_args *args)
{
	return (args->dst_off == 0 && args->src_off == 0) &&
	       args->src->i_size == args->len;
}

static int nilfs_clone(const struct nilfs_remap_file_args *args)
{
	if (files_the_same(args)) {
		return nilfs_reflink(args);
	}

	/* 
	in the future merge code from reflink and block level deduplication
	so that attaching dedup lists will be not *duplicated* hehe.
	*/

	nilfs_error(args->src->i_sb, "block level dedupliation not supported");
	return -ENOTSUPP;
}

static int nilfs_extent_same(const struct nilfs_remap_file_args *args)
{
	struct super_block *sb = args->src->i_sb;
	nilfs_info(sb, "%s", __func__);

	if (!compare_extents(args)) {
		nilfs_warn(sb, "extents are not the same");
		return -EBADE;
	}

	return nilfs_clone(args);
}

loff_t nilfs_remap_file_range(struct file *file_src, loff_t pos_in,
			      struct file *file_dst, loff_t pos_out, loff_t len,
			      unsigned int remap_flags)
{
	struct inode *inode_src = file_inode(file_src);
	struct inode *inode_dst = file_inode(file_dst);
	struct super_block *sb = inode_src->i_sb;
	const bool same_inode = inode_src == inode_dst;
	int ret;

	nilfs_info(sb, "%s", __func__);

	// TODO inode locking
	// if (same_inode) {
	// 	inode_lock(inode_src);
	// } else {
	// 	lock_two_nondirectories(inode_src, inode_dst);
	// 	inode_lock(inode_dst);
	// }

	ret = generic_remap_file_range_prep(file_src, pos_in, file_dst, pos_out,
					    &len, remap_flags);

	if (ret < 0 || len == 0) {
		nilfs_warn(
			sb,
			"generic_remap_file_range_prep failed; ret = %d, len = %lld",
			ret, len);
		goto out;
	}

	if (remap_flags & REMAP_FILE_DEDUP) {
		const struct nilfs_remap_file_args args = { .src = inode_src,
							    .src_off = pos_in,
							    .len = len,
							    .dst = inode_dst,
							    .dst_off =
								    pos_out };
		ret = nilfs_extent_same(&args);
	} else {
		nilfs_warn(sb, "unsupported remap_flags");
		ret = -ENOTSUPP;
	}

out:
	if (same_inode) {
		inode_unlock(inode_src);
	} else {
		unlock_two_nondirectories(inode_src, inode_dst);
		inode_unlock(inode_dst);
	}

	if (ret < 0) {
		nilfs_error(sb, "deduplication failed with code: %d", ret);
		return ret;
	}

	nilfs_info(sb, "deduplication succedded, deduplicated %d bytes", len);
	return len;
}

static bool is_deduplicated(const struct inode *inode)
{
	const struct nilfs_inode_info *info = NILFS_I(inode);
	const bool is_dedup_in_info = info && IS_NILFS_DEDUP_INODE(info);
	const bool is_dedup_in_inode = IS_NILFS_DEDUP_INODE(inode);

	// is dedup node content already replaced in memory?
	// checked in order to reduce number of content overrides
	const bool is_no_dedup_in_state =
		test_bit(NILFS_I_NODEDUP, &info->i_state);

	return (is_dedup_in_info || is_dedup_in_inode) && !is_no_dedup_in_state;
}

static int nilfs_override_inode_content(struct inode *inode,
					const struct buffer_head *data)
{
	struct super_block *sb = inode->i_sb;
	struct address_space *address_space = inode->i_mapping;
	// TODO remember to implement multiple block override
	const sector_t block_number = 0;
	struct buffer_head *bh =
		nilfs_grab_buffer(inode, address_space, block_number, 0);
	struct page *page = bh->b_page;
	struct buffer_head *bh_src = NULL;
	struct nilfs_dedup_info *dedup_info = NULL;

	nilfs_info(sb, "%s", __func__);

	if (unlikely(!bh)) {
		nilfs_error(sb, "failed to grab buffer");
		BUG();
		goto release;
	}

	dedup_info = (struct nilfs_dedup_info *)bh->b_data;

	if (unlikely(!dedup_info)) {
		nilfs_error(sb, "failed to read dedup_info from inode data");
		BUG();
		goto release;
	}

	nilfs_info(sb, "dedup_info = {.ino = %lld}", dedup_info->ino);

	/* complete this with first: fixed string for single block,
then override with first block of arbitrary content, and then
full functionality
*/

	const char *str_data = "AEIOUY";
	const unsigned long data_len = strlen(str_data);

	get_bh(bh);
	lock_buffer(bh);

	bh->b_bdev = inode->i_sb->s_bdev;
	bh->b_blocknr = block_number;
	memcpy(bh->b_data, str_data, data_len);

	flush_dcache_page(page);
	set_buffer_uptodate(bh);
	set_buffer_mapped(bh);
	mark_buffer_dirty(bh);
	bh->b_end_io = end_buffer_read_sync;
	unlock_buffer(bh);
	put_bh(bh);

	i_size_write(inode, data_len * sizeof(char));
	set_page_dirty(page);

release:
	unlock_page(page);
	put_page(page);

	return 0;
}

ssize_t nilfs_read_iter(struct kiocb *iocb, struct iov_iter *iter)
{
	struct file *file = iocb->ki_filp;
	struct inode *inode = file_inode(file);
	struct super_block *sb = inode->i_sb;

	nilfs_info(sb, "%s", __func__);

	if (is_deduplicated(inode)) {
		set_bit(NILFS_I_NODEDUP, &NILFS_I(inode)->i_state);

		nilfs_info(sb, "inode is deduplicated, gathering fragments");

		nilfs_override_inode_content(inode, NULL);

		// modify page of this inode with appended deduplicated fragments
	}

	return generic_file_read_iter(iocb, iter);
}

// #pragma GCC pop_options
/*
 * We have mostly NULL's here: the current defaults are ok for
 * the nilfs filesystem.
 */
const struct file_operations nilfs_file_operations = {
	.llseek = generic_file_llseek,
	.read_iter = nilfs_read_iter,
	.write_iter = generic_file_write_iter,
	.unlocked_ioctl = nilfs_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl = nilfs_compat_ioctl,
#endif /* CONFIG_COMPAT */
	.mmap = nilfs_file_mmap,
	.open = generic_file_open,
	/* .release	= nilfs_release_file, */
	.fsync = nilfs_sync_file,
	.splice_read = generic_file_splice_read,
	.splice_write = iter_file_splice_write,
	.remap_file_range = nilfs_remap_file_range
};

const struct inode_operations nilfs_file_inode_operations = {
	.setattr = nilfs_setattr,
	.permission = nilfs_permission,
	.fiemap = nilfs_fiemap,
	.fileattr_get = nilfs_fileattr_get,
	.fileattr_set = nilfs_fileattr_set,
};

/* end of file */
