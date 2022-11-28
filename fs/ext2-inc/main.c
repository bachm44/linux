#include <linux/fs.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/err.h>
#include <linux/buffer_head.h>
#include <linux/time64.h>

#include "super.h"
#include "ext2-inc.h"

static const struct inode_operations ext2_inode_ops = {};
static const struct file_operations ext2_file_ops = {};

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

MODULE_AUTHOR("Bartłomiej Chmiel <incvis@protonmail.com>");
MODULE_LICENSE("GPL");
MODULE_INFO(intree, "Y");

module_init(ext2_init);
module_exit(ext2_exit);