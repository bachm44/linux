#include "file.h"
#include "linux/export.h"
#include <linux/fs.h>

const struct file_operations ext2_file_operations = {
	.owner = THIS_MODULE,
	.llseek = generic_file_llseek,
	.read_iter = generic_file_read_iter,
	.write_iter = generic_file_write_iter,
	.mmap = generic_file_mmap,
	.fsync = generic_file_fsync,
	.splice_read = generic_file_splice_read,
};
