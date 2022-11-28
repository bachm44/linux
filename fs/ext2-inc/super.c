#include <linux/printk.h>
#include <linux/fs.h>
#include <linux/buffer_head.h>

#include "inode.h"
#include "super.h"
#include "ext2-inc.h"

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

static void ext2_dirty_inode(struct inode *i, int flags)
{
	pr_err("not implemented: ext2_dirty_inode");
}

static int ext2_write_inode(struct inode *i, struct writeback_control *wbc)
{
	pr_err("not implemented: ext2_write_inode");
	return 0;
}

static int ext2_drop_inode(struct inode *inode)
{
	pr_info("ext2_drop_inode");
	return generic_delete_inode(inode);
}

static void ext2_evict_inode(struct inode *)
{
	pr_err("not implemented: ext2_evict_inode");
}

static void ext2_put_super(struct super_block *)
{
	pr_err("not implemented: ext2_put_super");
}

static int ext2_sync_fs(struct super_block *sb, int wait)
{
	pr_err("not implemented: ext2_sync_fs");
	return 0;
}

static int ext2_freeze_super(struct super_block *)
{
	pr_err("not implemented: ext2_freeze_super");
	return 0;
}

static int ext2_freeze_fs(struct super_block *)
{
	pr_err("not implemented: ext2_freeze_fs");
	return 0;
}

static int ext2_thaw_super(struct super_block *)
{
	pr_err("not implemented: ext2_thaw_super");
	return 0;
}

static int ext2_unfreeze_fs(struct super_block *)
{
	pr_err("not implemented: ext2_unfreeze_fs");
	return 0;
}

static int ext2_statfs(struct dentry *dentry, struct kstatfs *stat)
{
	pr_info("ext2_statfs");
	return simple_statfs(dentry, stat);
}

static int ext2_remount_fs(struct super_block *, int *, char *)
{
	pr_err("not implemented: ext2_remount_fs");
	return 0;
}

static void ext2_umount_begin(struct super_block *)
{
	pr_err("not implemented: ext2_umount_begin");
}

static int ext2_show_options(struct seq_file *, struct dentry *)
{
	pr_err("not implemented: ext2_show_options");
	return 0;
}

static int ext2_show_devname(struct seq_file *, struct dentry *)
{
	pr_err("not implemented: ext2_show_devname");
	return 0;
}

static int ext2_show_path(struct seq_file *, struct dentry *)
{
	pr_err("not implemented: ext2_show_path");
	return 0;
}

static int ext2_show_stats(struct seq_file *, struct dentry *)
{
	pr_err("not implemented: ext2_show_stats");
	return 0;
}

static long ext2_nr_cached_objects(struct super_block *,
				   struct shrink_control *)
{
	pr_err("not implemented: ext2_nr_cached_objects");
	return 0;
}

static long ext2_free_cached_objects(struct super_block *,
				     struct shrink_control *)
{
	pr_err("not implemented: ext2_free_cached_objects");
	return 0;
}

static const struct super_operations s_op = {
	.alloc_inode = ext2_alloc_inode,
	.destroy_inode = ext2_destroy_inode,
	.free_inode = ext2_free_inode,
	.dirty_inode = ext2_dirty_inode,
	.write_inode = ext2_write_inode,
	.drop_inode = ext2_drop_inode,
	.evict_inode = ext2_evict_inode,
	.put_super = ext2_put_super,
	.sync_fs = ext2_sync_fs,
	.freeze_super = ext2_freeze_super,
	.freeze_fs = ext2_freeze_fs,
	.thaw_super = ext2_thaw_super,
	.unfreeze_fs = ext2_unfreeze_fs,
	.statfs = ext2_statfs,
	.remount_fs = ext2_remount_fs,
	.umount_begin = ext2_umount_begin,
	.show_options = ext2_show_options,
	.show_devname = ext2_show_devname,
	.show_path = ext2_show_path,
	.show_stats = ext2_show_stats,
	.nr_cached_objects = ext2_nr_cached_objects,
	.free_cached_objects = ext2_free_cached_objects
};

static u32 ext2_super_block_block_size(struct ext2_super_block *sb)
{
	return 1024 << sb->s_log_block_size;
}

int ext2_fill_super(struct super_block *sb, void *data, int silent)
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
