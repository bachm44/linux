#include <linux/fs.h>

#include "inode.h"
#include "ext2-inc.h"

struct ext2_inode *ext2_get_inode(struct super_block *sb, int inode_number)
{
	return NULL;
}

struct inode *ext2_inode_root(struct super_block *sb)
{
	struct inode *root;

	pr_info("ext2_inode_root");
	root = iget_locked(sb, EXT2_ROOT_INODE_NUMBER);
	if (IS_ERR(root)) {
		pr_err("Cannot find root inode: %ld", PTR_ERR(root));
		return NULL;
	}

	root->i_ino = EXT2_ROOT_INODE_NUMBER;
	inode_init_owner(NULL, root, NULL, S_IFDIR);
	root->i_sb = sb;
	//root->i_op = &ext2_inode_ops;
	//root->i_fop = &ext2_file_ops;
	root->i_atime = ns_to_timespec64(0);
	root->i_mtime = ns_to_timespec64(0);
	root->i_ctime = ns_to_timespec64(0);
	//root->i_private = ext2_get_inode(sb, EXT2_ROOT_INODE_NUMBER);

	return root;
}

void ext2_inode_init_once(void *object)
{
	struct ext2_inode_info *inode;

	pr_info("ext2_inode_init_once");

	inode = object;
	inode_init_once(inode->i_vfs_inode);
}
