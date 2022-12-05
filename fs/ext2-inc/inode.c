#include <linux/fs.h>

#include "file.h"
#include "inode.h"
#include "ext2-inc.h"

struct ext2_inode *ext2_get_inode(struct super_block *sb, int inode_number)
{
	return NULL;
}

struct dentry *ext2_lookup(struct inode *, struct dentry *, unsigned int)
{
	return NULL;
}

const char *ext2_get_link(struct dentry *, struct inode *,
			  struct delayed_call *)
{
	pr_err("not implemented: ext2_get_link");
	return NULL;
}

int ext2_permission(struct user_namespace *, struct inode *, int)
{
	pr_err("not implemented: ext2_permission");
	return 0;
}

struct posix_acl *ext2_get_acl(struct inode *, int, bool)
{
	pr_err("not implemented: ext2_get_acl");
	return NULL;
}

int ext2_readlink(struct dentry *, char __user *, int)
{
	pr_err("not implemented: ext2_readlink");
	return 0;
}

int ext2_create(struct user_namespace *, struct inode *, struct dentry *,
		umode_t, bool)
{
	pr_err("not implemented: ext2_create");
	return 0;
}

int ext2_link(struct dentry *, struct inode *, struct dentry *)
{
	pr_err("not implemented: ext2_link");
	return 0;
}

int ext2_unlink(struct inode *, struct dentry *)
{
	pr_err("not implemented: ext2_unlink");
	return 0;
}

int ext2_symlink(struct user_namespace *, struct inode *, struct dentry *,
		 const char *)
{
	pr_err("not implemented: ext2_symlink");
	return 0;
}

int ext2_mkdir(struct user_namespace *, struct inode *, struct dentry *,
	       umode_t)
{
	pr_err("not implemented: ext2_mkdir");
	return 0;
}

int ext2_rmdir(struct inode *, struct dentry *)
{
	pr_err("not implemented: ext2_rmdir");
	return 0;
}

int ext2_mknod(struct user_namespace *, struct inode *, struct dentry *,
	       umode_t, dev_t)
{
	pr_err("not implemented: ext2_mknod");
	return 0;
}

int ext2_rename(struct user_namespace *, struct inode *, struct dentry *,
		struct inode *, struct dentry *, unsigned int)
{
	pr_err("not implemented: ext2_rename");
	return 0;
}

int ext2_setattr(struct user_namespace *, struct dentry *, struct iattr *)
{
	pr_err("not implemented: ext2_setattr");
	return 0;
}

int ext2_getattr(struct user_namespace *, const struct path *, struct kstat *,
		 u32, unsigned int)
{
	pr_err("not implemented: ext2_getattr");
	return 0;
}

ssize_t ext2_listxattr(struct dentry *, char *, size_t)
{
	pr_err("not implemented: ext2_listxattr");
	return 0;
}

int ext2_fiemap(struct inode *, struct fiemap_extent_info *, u64 start, u64 len)
{
	pr_err("not implemented: ext2_fiemap");
	return 0;
}

int ext2_update_time(struct inode *, struct timespec64 *, int)
{
	pr_err("not implemented: ext2_time");
	return 0;
}

int ext2_atomic_open(struct inode *, struct dentry *, struct file *,
		     unsigned open_flag, umode_t create_mode)
{
	pr_err("not implemented: ext2_open");
	return 0;
}

int ext2_tmpfile(struct user_namespace *, struct inode *, struct dentry *,
		 umode_t)
{
	pr_err("not implemented: ext2_tmpfile");
	return 0;
}

int ext2_set_acl(struct user_namespace *, struct inode *, struct posix_acl *,
		 int)
{
	pr_err("not implemented: ext2_set_acl");
	return 0;
}

int ext2_fileattr_set(struct user_namespace *mnt_userns, struct dentry *dentry,
		      struct fileattr *fa)
{
	pr_err("not implemented: ext2_fileattr_set");
	return 0;
}

int ext2_fileattr_get(struct dentry *dentry, struct fileattr *fa)
{
	pr_err("not implemented: ext2_fileattr_get");
	return 0;
}

static const struct inode_operations i_op = {
	.lookup = ext2_lookup,
	.get_link = ext2_get_link,
	.permission = ext2_permission,
	.get_acl = ext2_get_acl,
	.readlink = ext2_readlink,
	.create = ext2_create,
	.link = ext2_link,
	.unlink = ext2_unlink,
	.symlink = ext2_symlink,
	.mkdir = ext2_mkdir,
	.rmdir = ext2_rmdir,
	.mknod = ext2_mknod,
	.rename = ext2_rename,
	.setattr = ext2_setattr,
	.getattr = ext2_getattr,
	.listxattr = ext2_listxattr,
	.fiemap = ext2_fiemap,
	.update_time = ext2_update_time,
	.atomic_open = ext2_atomic_open,
	.tmpfile = ext2_tmpfile,
	.set_acl = ext2_set_acl,
	.fileattr_set = ext2_fileattr_set,
	.fileattr_get = ext2_fileattr_get,
};

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
	struct ext2_inode_info *inode;

	pr_info("ext2_inode_init_once");

	inode = object;
	inode_init_once(inode->i_vfs_inode);
}
