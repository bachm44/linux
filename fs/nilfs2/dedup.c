#include "dedup.h"
#include "linux/nilfs2_api.h"
#include <linux/types.h>
#include <linux/fs.h>
#include "nilfs.h"

int nilfs_dedup(struct inode *inode,
		const struct nilfs_deduplication_block *blocks,
		size_t blocks_count)
{
	struct super_block *sb = inode->i_sb;
	const struct nilfs_deduplication_block *src_block;

	nilfs_info(sb, "Starting deduplication");

	WARN_ON(!blocks);
	src_block = &blocks[0];
	WARN_ON(!src_block);

	nilfs_info(sb, "SRC: ino=%ld, blocknr=%ld", blocks[0].ino,
		   blocks[0].blocknr);

	for (size_t i = 1; i < blocks_count; ++i) {
		const struct nilfs_deduplication_block *dst_block = &blocks[i];
		WARN_ON(!dst_block);
		nilfs_info(sb, "DST: ino=%ld, blocknr=%ld", dst_block->ino,
			   dst_block->blocknr);
	}

	return 0;
}