#include <linux/compiler.h>
#include <linux/gfp.h>
#include <linux/slab.h>
#include <linux/slub_def.h>
#include <linux/time64.h>
#include <linux/types.h>
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

static struct ext2_inode_info *ext2_get_root_inode(struct super_block *sb)
{
	struct buffer_head *bh;
	struct ext2_inode *inode;
	struct ext2_inode_info *result;
	int inode_table_block = 5;

	pr_info("%s", __func__);

	bh = sb_bread(sb, inode_table_block);
	BUG_ON(!bh);

	inode = (struct ext2_inode *)(bh->b_data);
	inode += 1; // root inode has index 2

	BUG_ON(!ext2_inode_cachep);
	result = kmem_cache_alloc(ext2_inode_cachep, GFP_KERNEL);
	memcpy(&result->inode, inode, sizeof(*result));
	brelse(bh);

	return result;
}

static int ext2_create(struct user_namespace *namespace, struct inode *inode,
		       struct dentry *dentry, umode_t mode, bool)
{
	pr_info("not implemented: %s", __func__);
	BUG_ON(1);
	return -1;
}

static int ext2_setattr(struct user_namespace *, struct dentry *,
			struct iattr *)
{
	pr_err("not implemented: %s", __func__);
	return 0;
}

static int ext2_getattr(struct user_namespace *namespace,
			const struct path *path, struct kstat *stat,
			u32 request_mask, unsigned int query_flags)
{
	pr_info("default: %s", __func__);
	return simple_getattr(namespace, path, stat, request_mask, query_flags);
}

struct dentry *ext2_lookup(struct inode *inode, struct dentry *dentry,
			   unsigned int)
{
	pr_err("not implemented: %s", __func__);
	return NULL;
}

static const struct inode_operations i_op = { .lookup = ext2_lookup,
					      .getattr = ext2_getattr,
					      .setattr = ext2_setattr,
					      .create = ext2_create };

struct inode *ext2_inode_root(struct super_block *sb)
{
	struct inode *root;
	struct ext2_inode_info *info;

	pr_info("%s", __func__);
	root = iget_locked(sb, EXT2_ROOT_INODE_NUMBER);
	BUG_ON(!root);

	info = ext2_get_root_inode(sb);

	root->i_ino = EXT2_ROOT_INODE_NUMBER;
	inode_init_owner(NULL, root, NULL, S_IFDIR);
	root->i_sb = sb;
	root->i_op = &i_op;
	root->i_fop = &ext2_file_operations;
	root->i_atime.tv_sec = info->inode.i_atime;
	root->i_mtime.tv_sec = info->inode.i_mtime;
	root->i_ctime.tv_sec = info->inode.i_ctime;
	root->i_private = info;

	return root;
}

void ext2_inode_init_once(void *object)
{
	struct ext2_inode_info *inode = object;

	pr_info("%s", __func__);

	BUG_ON(!object);

	inode_init_once(&inode->i_vfs_inode);
}
