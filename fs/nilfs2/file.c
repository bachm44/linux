// SPDX-License-Identifier: GPL-2.0+
/*
 * NILFS regular file handling primitives including fsync().
 *
 * Copyright (C) 2005-2008 Nippon Telegraph and Telephone Corporation.
 *
 * Written by Amagai Yoshiji and Ryusuke Konishi.
 */

#include "ifile.h"
#include "linux/buffer_head.h"
#include "linux/errno.h"
#include "linux/gfp_types.h"
#include "linux/list.h"
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

static bool compare_extents(const struct nilfs_remap_file_args *args)
{
	struct super_block *sb = args->src->i_sb;
	struct buffer_head *src_bh;
	struct buffer_head *dst_bh;
	void *src_data;
	void *dst_data;
	int i;

	nilfs_info(sb, "%s", __func__);

	if (args->src->i_size != args->dst->i_size) {
		nilfs_info(sb, "inodes have different size");
		return 0;
	}

	i = 0;
	while (i < args->src->i_blocks) {
		src_bh = sb_bread(args->src->i_sb, i);
		dst_bh = sb_bread(args->dst->i_sb, i);

		if (!src_bh || !dst_bh) {
			nilfs_warn(sb, "cannot fetch buffer heads");
			return false;
		}

		src_data = (char *)src_bh->b_data;
		dst_data = (char *)dst_bh->b_data;

		if (memcmp(src_data, dst_data, src_bh->b_size) != 0) {
			nilfs_warn(sb, "inodes have different data");
			brelse(src_bh);
			brelse(dst_bh);
			return false;
		}

		brelse(src_bh);
		brelse(dst_bh);
		++i;
	}

	return true;
}

static int nilfs_reflink(const struct nilfs_remap_file_args *args)
{
	struct super_block *sb = args->src->i_sb;
	struct inode *src = args->src;
	struct inode *dst = args->dst;
	struct nilfs_inode_info *src_info = NILFS_I(src);
	struct nilfs_inode_info *dst_info = NILFS_I(dst);
	struct nilfs_dedup_info *dedup_info;

	nilfs_info(sb, "%s", __func__);

	if (test_bit(NILFS_I_DEDUP, &dst_info->i_state)) {
		nilfs_warn(sb,
			   "Deduplication of dedup inodes is not supported");
		return -ENOTSUPP;
	}

	inode_dio_wait(dst);
	truncate_setsize(dst, 0);
	nilfs_truncate(dst);

	dedup_info = kmalloc(sizeof(*dedup_info), GFP_KERNEL);
	if (unlikely(!dedup_info)) {
		nilfs_error(sb, "cannot allocate memory for dedup_info");
		return -ENOMEM;
	}

	INIT_LIST_HEAD(&dedup_info->head);
	dedup_info->ino = src->i_ino;
	dedup_info->offset = 0;
	dedup_info->blocks_count = src->i_blocks;

	src_info->dedup_ref_count++;
	set_bit(NILFS_I_DEDUP, &src_info->i_state);

	list_add(&dedup_info->head, &dst_info->i_dedup_blocks);

	/*  
	1. For now only in memory deduplication will be available (
	no information about deduplication will be written to the disk,
	but files will be removed/truncated approprietly. This means that
	data will be lost if filesystem goes offline.

	2. Remember about locks (to be added later since for now not
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

	return ret < 0 ? ret : len;
}

static bool is_deduplicated(const struct inode *inode)
{
	const struct nilfs_inode_info *info = NILFS_I(inode);
	return info && !list_empty(&info->i_dedup_blocks);
}

ssize_t nilfs_read_iter(struct kiocb *iocb, struct iov_iter *iter)
{
	struct file *file = iocb->ki_filp;
	struct inode *inode = file_inode(file);
	struct super_block *sb = inode->i_sb;
	struct address_space *address_space = inode->i_mapping;

	nilfs_info(sb, "%s", __func__);

	if (is_deduplicated(inode)) {
		nilfs_info(sb, "inode is deduplicated, gathering fragments");

		struct buffer_head *bh =
			nilfs_grab_buffer(inode, address_space, 0, 0);
		const struct nilfs_inode_info *info = NILFS_I(inode);
		struct nilfs_dedup_info *dedup_info = NULL;
		struct nilfs_dedup_info *n = NULL;

		nilfs_info(sb, "bh->b_data = %s", bh->b_data);

		list_for_each_entry_safe(dedup_info, n, &info->i_dedup_blocks,
					 head) {
			nilfs_info(
				sb,
				"dedup_info for ino %d: offset = %d, ino = %d",
				inode->i_ino, dedup_info->offset,
				dedup_info->ino);
		}

		// modify page of this inode with appended deduplicated fragments
		// read from inode pointed in i_dedup_blocks list.
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
