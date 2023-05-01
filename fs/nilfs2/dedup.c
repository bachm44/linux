#include "dedup.h"
#include <linux/types.h>
#include <linux/fs.h>
#include "nilfs.h"

int nilfs_dedup(struct inode *inode, __u64 blocks_to_consider)
{
	struct super_block *sb = inode->i_sb;
	nilfs_info(
		sb,
		"starting deduplication with arguments: blocks_to_consider=%lld",
		blocks_to_consider);

	return 0;
}