#include "file.h"
#include <linux/fs.h>

loff_t ext2_llseek(struct file *, loff_t, int)
{
	pr_err("not implemented: ext2_llseek");
	return 0;
}

ssize_t ext2_read(struct file *, char __user *, size_t, loff_t *)
{
	pr_err("not implemented: ext2_read");
	return 0;
}

ssize_t ext2_write(struct file *, const char __user *, size_t, loff_t *)
{
	pr_err("not implemented: ext2_write");
	return 0;
}

ssize_t ext2_read_iter(struct kiocb *, struct iov_iter *)
{
	pr_err("not implemented: ext2_read_iter");
	return 0;
}

ssize_t ext2_write_iter(struct kiocb *, struct iov_iter *)
{
	pr_err("not implemented: ext2_write_iter");
	return 0;
}

int ext2_iopoll(struct kiocb *kiocb, bool spin)
{
	pr_err("not implemented: ext2_iopoll");
	return 0;
}

int ext2_iterate(struct file *, struct dir_context *)
{
	pr_err("not implemented: ext2_iterate");
	return 0;
}

int ext2_iterate_shared(struct file *, struct dir_context *)
{
	pr_err("not implemented: ext2_iterate_shared");
	return 0;
}

__poll_t ext2_poll(struct file *, struct poll_table_struct *)
{
	pr_err("not implemented: ext2_poll");
	return 0;
}

long ext2_unlocked_ioctl(struct file *, unsigned int, unsigned long)
{
	pr_err("not implemented: ext2_unlocked_ioctl");
	return 0;
}

long ext2_compat_ioctl(struct file *, unsigned int, unsigned long)
{
	pr_err("not implemented: ext2_compat_ioctl");
	return 0;
}

int ext2_mmap(struct file *, struct vm_area_struct *)
{
	pr_err("not implemented: ext2_mmap");
	return 0;
}

int ext2_open(struct inode *, struct file *)
{
	pr_err("not implemented: ext2_open");
	return 0;
}

int ext2_flush(struct file *, fl_owner_t id)
{
	pr_err("not implemented: ext2_flush");
	return 0;
}

int ext2_release(struct inode *, struct file *)
{
	pr_err("not implemented: ext2_release");
	return 0;
}

int ext2_fsync(struct file *, loff_t, loff_t, int datasync)
{
	pr_err("not implemented: ext2_fsync");
	return 0;
}

int ext2_fasync(int, struct file *, int)
{
	pr_err("not implemented: ext2_fasync");
	return 0;
}

int ext2_lock(struct file *, int, struct file_lock *)
{
	pr_err("not implemented: ext2_lock");
	return 0;
}

ssize_t ext2_sendpage(struct file *, struct page *, int, size_t, loff_t *, int)
{
	pr_err("not implemented: ext2_sendpage");
	return 0;
}

unsigned long ext2_get_unmapped_area(struct file *, unsigned long,
				     unsigned long, unsigned long,
				     unsigned long)
{
	pr_err("not implemented: ext2_get_unmapped_area");
	return 0;
}

int ext2_check_flags(int)
{
	pr_err("not implemented: ext2_check_flags");
	return 0;
}

int ext2_flock(struct file *, int, struct file_lock *)
{
	pr_err("not implemented: ext2_flock");
	return 0;
}

ssize_t ext2_splice_write(struct pipe_inode_info *, struct file *, loff_t *,
			  size_t, unsigned int)
{
	pr_err("not implemented: ext2_splice_write");
	return 0;
}

ssize_t ext2_splice_read(struct file *, loff_t *, struct pipe_inode_info *,
			 size_t, unsigned int)
{
	pr_err("not implemented: ext2_splice_read");
	return 0;
}

int ext2_setlease(struct file *, long, struct file_lock **, void **)
{
	pr_err("not implemented: ext2_setlease");
	return 0;
}

long ext2_fallocate(struct file *file, int mode, loff_t offset, loff_t len)
{
	pr_err("not implemented: ext2_fallocate");
	return 0;
}

void ext2_show_fdinfo(struct seq_file *m, struct file *f)
{
	pr_err("not implemented: ext2_show_fdinfo");
}

#ifndef CONFIG_MMU
unsigned ext2_mmap_capabilities(struct file *)
{
	pr_err("not implemented: ext2_mmap_capabilities");
	return 0;
}

#endif
ssize_t ext2_copy_file_range(struct file *, loff_t, struct file *, loff_t,
			     size_t, unsigned int)
{
	pr_err("not implemented: ext2_copy_file_range");
	return 0;
}

loff_t ext2_remap_file_range(struct file *file_in, loff_t pos_in,
			     struct file *file_out, loff_t pos_out, loff_t len,
			     unsigned int remap_flags)
{
	pr_err("not implemented: ext2_remap_file_range");
	return 0;
}

int ext2_fadvise(struct file *, loff_t, loff_t, int)
{
	pr_err("not implemented: ext2_fadvise");
	return 0;
}

const struct file_operations ext2_file_operations = {
	.owner = THIS_MODULE,
	.llseek = ext2_llseek,
	.read = ext2_read,
	.write = ext2_write,
	.read_iter = ext2_read_iter,
	.write_iter = ext2_write_iter,
	.iopoll = ext2_iopoll,
	.iterate = ext2_iterate,
	.iterate_shared = ext2_iterate_shared,
	.poll = ext2_poll,
	.unlocked_ioctl = ext2_unlocked_ioctl,
	.compat_ioctl = ext2_compat_ioctl,
	.mmap = ext2_mmap,
	.mmap_supported_flags = 0,
	.open = ext2_open,
	.flush = ext2_flush,
	.release = ext2_release,
	.fsync = ext2_fsync,
	.fasync = ext2_fasync,
	.lock = ext2_lock,
	.sendpage = ext2_sendpage,
	.get_unmapped_area = ext2_get_unmapped_area,
	.check_flags = ext2_check_flags,
	.flock = ext2_flock,
	.splice_write = ext2_splice_write,
	.splice_read = ext2_splice_read,
	.setlease = ext2_setlease,
	.fallocate = ext2_fallocate,
	.show_fdinfo = ext2_show_fdinfo,
#ifndef CONFIG_MMU
	.mmap_capabilities = ext2_mmap_capabilities,
#endif
	.copy_file_range = ext2_copy_file_range,
	.remap_file_range = ext2_remap_file_range,
	.fadvise = ext2_fadvise
};
