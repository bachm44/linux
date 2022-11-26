#include <linux/fs.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/err.h>
#include <linux/buffer_head.h>
#include <linux/time64.h>

#include "ext2-inc.h"

int ext2_statfs(struct dentry *dentry, struct kstatfs *stat)
{
	return simple_statfs(dentry, stat);
}

int ext2_drop_inode(struct inode *inode)
{
	return generic_delete_inode(inode);
}

static struct super_operations s_op = {
	// .alloc_inode = ext2_alloc_inode,
	// .destroy_inode = ext2_destroy_inode,
	// .free_inode = ext2_free_inode,
	// .dirty_inode = ext2_dirty_inode,
	// .write_inode = ext2_write_inode,
	// .drop_inode = ext2_drop_inode,
	// .evict_inode = ext2_evict_inode,
	// .put_super = ext2_put_super,
	// .sync_fs = ext2_sync_fs,
	// .freeze_super = ext2_freeze_super,
	// .freeze_fs = ext2_freeze_fs,
	// .thaw_super = ext2_thaw_super,
	// .unfreeze_fs = ext2_unfreeze_fs,
	// .statfs = ext2_statfs,
	// .remount_fs = ext2_remount_fs,
	// .umount_begin = ext2_umount_begin,
	// .show_options = ext2_show_options,
	// .show_devname = ext2_show_devname,
	// .show_path = ext2_show_path,
	// .show_stats = ext2_stats,
	// .bdev_try_to_free_page = ext2_bdev_try_to_free_page,
	// .nr_cached_objects = ext2_nr_cached_objects,
	// .free_cached_objects = ext2_free_cached_objects
	.statfs = ext2_statfs,
	.drop_inode = ext2_drop_inode
};

static const struct inode_operations ext2_inode_ops = {};
static const struct file_operations ext2_file_ops = {};

struct ext2_inode *ext2_get_inode(struct super_block *sb, int inode_number)
{
	return NULL;
}

static struct inode *ext2_get_root_inode(struct super_block *sb)
{
	struct inode *root;

	root = iget_locked(sb, EXT2_ROOT_INODE_NUMBER);
	if (IS_ERR(root)) {
		printk("Cannot find root inode: %ld", PTR_ERR(root));
		return NULL;
	}

	// root->i_ino = EXT2_ROOT_INODE_NUMBER;
	// inode_init_owner(NULL, root, NULL, S_IFDIR);
	// root->i_sb = sb;
	// root->i_op = &ext2_inode_ops;
	// root->i_fop = &ext2_file_ops;
	// root->i_atime = ns_to_timespec64(0);
	// root->i_mtime = ns_to_timespec64(0);
	// root->i_ctime = ns_to_timespec64(0);
	// root->i_private = ext2_get_inode(sb, EXT2_ROOT_INODE_NUMBER);

	return root;
}

/**
 * fs/ext2/super.c
*/
static unsigned long get_sb_block(void **data)
{
	unsigned long sb_block;
	char *options = (char *)*data;

	if (!options || strncmp(options, "sb=", 3) != 0)
		return 1; /* Default location */
	options += 3;
	sb_block = simple_strtoul(options, &options, 0);
	if (*options && *options != ',') {
		printk("EXT2-fs: Invalid sb specification: %s\n",
		       (char *)*data);
		return 1;
	}
	if (*options == ',')
		options++;
	*data = (void *)options;
	return sb_block;
}

int ext2_fill_super(struct super_block *sb, void *data, int silent)
{
	int ret = -EAGAIN;
	struct buffer_head *bh;
	struct ext2_super_block *sb_disk;
	unsigned long sb_block = get_sb_block(&data);
	int offset = 0;

	printk("Super block number: %ld", sb_block);
	bh = sb_bread(sb, sb_block);
	if (!bh) {
		printk("Buffer head null");
		goto release;
	}
	BUG_ON(!bh);

	int blocksize = sb_min_blocksize(sb, EXT2_DEFAULT_BLOCK_SIZE);
	if (!blocksize) {
		printk("Could not set blocksize");
		goto release;
	}
	printk("%d", blocksize);

	sb_disk = (struct ext2_super_block *)(((char *)bh->b_data) + offset);

	printk("inodes_cnt: %X", cpu_to_le32(sb_disk->s_inodes_count));
	printk("blocks_cnt: %d", cpu_to_le32(sb_disk->s_blocks_count));
	printk("magic: 0x%X", le16_to_cpu(sb_disk->s_magic));
	printk("magic: 0x%X", sb_disk->s_magic);
	sb_disk->s_magic = le16_to_cpu(sb_disk->s_magic);
	pr_info("Ext magic number in disk is: [0x%X]", sb_disk->s_magic);

	if (unlikely(sb_disk->s_magic != EXT2_MAGIC)) {
		pr_err("Magic number mismatch when mounting ext2");
		goto release;
	}

	pr_info("Superblock block size: [%d]",
		ext2_super_block_block_size(sb_disk));

	if (unlikely(ext2_super_block_block_size(sb_disk) !=
		     EXT2_DEFAULT_BLOCK_SIZE)) {
		pr_err("Ext2 not mounted with standard block size");
		goto release;
	}

	sb->s_maxbytes = EXT2_DEFAULT_BLOCK_SIZE;
	sb->s_op = &s_op;
	sb->s_magic = EXT2_MAGIC;
	sb->s_fs_info = sb_disk;

	sb->s_root = d_make_root(ext2_get_root_inode(sb));

	if (!sb->s_root) {
		ret = -ENOMEM;
		pr_err("Memory error when making root inode");
		goto release;
	}

	ret = 0;

	printk("Successfully filled superblock");

release:
	brelse(bh);

	return ret;
}

static struct dentry *ext2_mount(struct file_system_type *fs_type, int flags,
				 const char *dev_name, void *data)
{
	// TODO create here superblock

	struct dentry *ret;
	ret = mount_bdev(fs_type, flags, dev_name, data, ext2_fill_super);

	if (unlikely(IS_ERR(ret)))
		pr_err("ext2 failed to mount.");
	else
		pr_info("ext2 mounted on [%s]", dev_name);

	return ret;
}

static struct file_system_type fs_type = { .name = "ext2-inc",
					   .fs_flags = 0,
					   .mount = ext2_mount,
					   .kill_sb = kill_block_super,
					   .owner = THIS_MODULE,
					   .next = NULL };

static int __init ext2_init(void)
{
	return register_filesystem(&fs_type);
}

static void __exit ext2_exit(void)
{
	unregister_filesystem(&fs_type);
}

module_init(ext2_init);
module_exit(ext2_exit);

MODULE_AUTHOR("Bartłomiej Chmiel <incvis@protonmail.com");
MODULE_LICENSE("GPL");
MODULE_INFO(intree, "Y");