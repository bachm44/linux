#include "linux/slab.h"
#include <linux/printk.h>
#include "ext2-inc.h"
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/err.h>
#include <linux/buffer_head.h>
#include <linux/time64.h>
#include <linux/kernel.h>

#include "super.h"
#include "ext2-inc.h"
#include "inode.h"

static const struct inode_operations ext2_inode_ops = {};
static const struct file_operations ext2_file_ops = {};
static struct kmem_cache * ext2_inode_cachep;

static struct inode *ext2_alloc_inode(struct super_block *sb)
{
	pr_err("not implemented: ext2_alloc_inode");
	return NULL;
}

static void ext2_destroy_inode(struct inode *i)
{
	pr_err("not implemented: ext2_destroy_inode");
}

static void ext2_free_inode(struct inode *i)
{
	pr_err("not implemented: ext2_free_inode");
}

static int ext2_write_inode(struct inode *i, struct writeback_control *wbc)
{
	pr_err("not implemented: ext2_write_inode");
	return 0;
}

static void ext2_evict_inode(struct inode *)
{
	pr_err("not implemented: ext2_evict_inode");
}

static void ext2_put_super(struct super_block *)
{
	pr_err("not implemented: ext2_put_super");
}

static int ext2_statfs(struct dentry *dentry, struct kstatfs *stat)
{
	pr_info("default: ext2_statfs");
	return simple_statfs(dentry, stat);
}

static int ext2_remount_fs(struct super_block *, int *, char *)
{
	pr_err("not implemented: ext2_remount_fs");
	return 0;
}

static const struct super_operations s_op = {
	.alloc_inode = ext2_alloc_inode,
	.free_inode = ext2_free_inode,
	.write_inode = ext2_write_inode,
	.evict_inode = ext2_evict_inode,
	.put_super = ext2_put_super,
	.statfs = ext2_statfs,
	.remount_fs = ext2_remount_fs,
};

void ext2_save_superblock(struct super_block *sb)
{
	struct buffer_head *bh;

	bh = sb_bread(sb, EXT2_SUPERBLOCK_BLOCK_NUMBER);
	BUG_ON(!bh);

	bh->b_data = (void *)EXT2_SB(sb);
	mark_buffer_dirty(bh);
	sync_dirty_buffer(bh);
	brelse(bh);
}

static int ext2_fill_super(struct super_block *sb, void *data, int silent)
{
	int ret = -EAGAIN;
	struct buffer_head *bh;
	struct ext2_super_block *ext2_sb;

	sb_min_blocksize(sb, EXT2_DEFAULT_BLOCK_SIZE);
	bh = sb_bread(sb, EXT2_SUPERBLOCK_BLOCK_NUMBER);
	BUG_ON(!bh);

	ext2_sb = (struct ext2_super_block *)(((char *)bh->b_data));

	if (unlikely(ext2_sb->s_magic != EXT2_MAGIC)) {
		pr_err("Magic number mismatch when mounting ext2");
		goto release;
	}

	pr_info("Superblock block size: [%d]",
		ext2_super_block_block_size(ext2_sb));

	if (unlikely(ext2_super_block_block_size(ext2_sb) !=
		     EXT2_DEFAULT_BLOCK_SIZE)) {
		pr_err("Ext2 not mounted with standard block size");
		goto release;
	}

	sb->s_maxbytes = EXT2_DEFAULT_BLOCK_SIZE;
	sb->s_op = &s_op;
	sb->s_magic = EXT2_MAGIC;
	sb->s_fs_info = ext2_sb;

	sb->s_root = d_make_root(ext2_inode_root(sb));

	if (!sb->s_root) {
		ret = -ENOMEM;
		pr_err("Memory error when making root inode");
		goto release;
	}

	ret = 0;

	pr_info("Successfully filled superblock");

release:
	brelse(bh);

	return ret;
}

static struct dentry *ext2_mount(struct file_system_type *fs_type, int flags,
				 const char *dev_name, void *data)
{
	struct dentry *ret;
	ret = mount_bdev(fs_type, flags, dev_name, data, ext2_fill_super);

	if (unlikely(IS_ERR(ret)))
		pr_err("ext2 failed to mount.");
	else
		pr_info("ext2 mounted on [%s]", dev_name);

	return ret;
}

static struct file_system_type fs_type = { .name = "ext2-inc",
					   .fs_flags = FS_REQUIRES_DEV,
					   .mount = ext2_mount,
					   .kill_sb = kill_block_super,
					   .owner = THIS_MODULE,
					   .next = NULL };

static int __init ext2_init_inode_cache(void)
{
	pr_info("%s", __func__);
	ext2_inode_cachep = kmem_cache_create(
		"ext2_inode_cache", sizeof(struct ext2_inode_info), 0,
		SLAB_RECLAIM_ACCOUNT | SLAB_ACCOUNT, ext2_inode_init_once);

	if (ext2_inode_cachep == NULL)
		return -ENOMEM;

	return 0;
}

static void ext2_destroy_cachep(void)
{
	kmem_cache_destroy(ext2_inode_cachep);
}

static int __init ext2_init(void)
{
	ext2_init_inode_cache();
	return register_filesystem(&fs_type);
}

static void __exit ext2_exit(void)
{
	ext2_destroy_cachep();
	unregister_filesystem(&fs_type);
}

MODULE_AUTHOR("Bartłomiej Chmiel <incvis@protonmail.com>");
MODULE_LICENSE("GPL");
MODULE_INFO(intree, "Y");

module_init(ext2_init);
module_exit(ext2_exit);
