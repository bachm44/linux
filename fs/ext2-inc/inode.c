#include "linux/compiler.h"
#include "linux/gfp.h"
#include "linux/slab.h"
#include "linux/slub_def.h"
#include "linux/types.h"
#include <asm-generic/bug.h>
#include <asm-generic/errno-base.h>
#include <linux/bits.h>
#include <linux/buffer_head.h>
#include <linux/dcache.h>
#include <linux/mutex.h>
#include <linux/fs.h>
#include <linux/math.h>

#include "file.h"
#include "inode.h"
#include "ext2-inc.h"
#include "super.h"

struct kmem_cache *ext2_inode_cache = NULL;

static struct ext2_inode *ext2_get_inode(struct super_block *sb,
					 int inode_number)
{
	struct inode *inode = new_inode(sb);
	BUG_ON(!inode);

	return NULL;
}

static int allocate_inode(struct super_block *sb,
			  unsigned long *out_inode_number)
{
	struct ext2_super_block *ext2_sb;
	struct buffer_head *bh;
	struct ext2_block_group_descriptor *ext2_bg;
	struct ext2_inode_info *inode;
	char *bitmap;
	int ret;
	int i;
	char *slot;
	int needle;

	ret = -ENOSPC;

	ext2_sb = EXT2_SB(sb);

	/*
	int group_count_method_1 = DIV_ROUND_UP(ext2_sb->s_blocks_count,
						ext2_sb->s_blocks_per_group);
	int group_count_method_2 = DIV_ROUND_UP(ext2_sb->s_inodes_count,
						ext2_sb->s_inodes_per_group);

	if (unlikely(group_count_method_1 != group_count_method_2)) {
		pr_err("Group count mismatch: %d != %d", group_count_method_1,
		       group_count_method_2);
		return -EIO;
	}

	pr_info("There are %d == %d block groups available",
		group_count_method_1, group_count_method_2);
	*/

	bh = sb_bread(sb, EXT2_SUPERBLOCK_BLOCK_NUMBER + 1);
	BUG_ON(!bh);

	ext2_bg = (struct ext2_block_group_descriptor *)bh->b_data;
	brelse(bh);

	pr_info("Inode bitmap: %d", ext2_bg->bg_inode_bitmap);

	//mutex_lock(&ext2_sb->lock);

	bh = sb_bread(sb, ext2_bg->bg_inode_bitmap);
	BUG_ON(!bh);

	bitmap = (char *)bh->b_data;

	// find slot for inode using bitmap
	for (i = 0; i < ext2_sb->s_inodes_count; ++i) {
		slot = bitmap + i / BITS_PER_BYTE;
		needle = 1 << (i % BITS_PER_BYTE);
		if ((*slot & needle) == 0) {
			*out_inode_number = i;
			*slot |= needle;
			ext2_sb->s_inodes_count++;
			ret = 0;
			pr_info("Found free inode at %lu", *out_inode_number);
			break;
		}
	}

	inode = kmem_cache_alloc(ext2_inode_cache, GFP_KERNEL);

	mark_buffer_dirty(bh);
	sync_dirty_buffer(bh);
	brelse(bh);

	ext2_save_superblock(sb);
	//mutex_unlock(&ext2_sb->lock);

	return ret;
}

static int ext2_create(struct user_namespace *namespace, struct inode *inode,
		       struct dentry *dentry, umode_t mode, bool)
{
	int ret = 0;
	unsigned long inode_number;
	struct super_block *sb;
	struct ext2_super_block *ext2_sb;

	pr_debug("implemented: ext2_create");

	sb = inode->i_sb;
	ext2_sb = sb->s_fs_info;

	ret = allocate_inode(sb, &inode_number);

	return ret;
}

static int ext2_setattr(struct user_namespace *, struct dentry *,
			struct iattr *)
{
	pr_err("not implemented: setattr");
	return 0;
}

static int ext2_getattr(struct user_namespace *, const struct path *,
			struct kstat *, u32, unsigned int)
{
	pr_err("not implemented getattr");
	return 0;
}

static const struct inode_operations i_op = { .getattr = ext2_getattr,
					      .setattr = ext2_setattr,
					      .create = ext2_create };

struct inode *ext2_inode_root(struct super_block *sb)
{
	struct inode *root;

	pr_info("ext2_inode_root");
	root = iget_locked(sb, EXT2_ROOT_INODE_NUMBER);
	BUG_ON(!root);

	root->i_ino = EXT2_ROOT_INODE_NUMBER;
	inode_init_owner(NULL, root, NULL, S_IFDIR);
	root->i_sb = sb;
	root->i_op = &i_op;
	root->i_fop = &ext2_file_operations;
	root->i_atime = ns_to_timespec64(0);
	root->i_mtime = ns_to_timespec64(0);
	root->i_ctime = ns_to_timespec64(0);
	//root->i_private = ext2_get_inode(sb, EXT2_ROOT_INODE_NUMBER);

	return root;
}

void ext2_inode_init_once(void *object)
{
	struct ext2_inode_info *inode = object;

	pr_info("%s", __func__);

	BUG_ON(!object);

	inode_init_once(&inode->i_vfs_inode);
}
