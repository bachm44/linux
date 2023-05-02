#include "dedup.h"
#include "bmap.h"
#include "linux/buffer_head.h"
#include "linux/nilfs2_api.h"
#include "page.h"
#include <linux/types.h>
#include <linux/fs.h>
#include "nilfs.h"

int nilfs_dedup(struct inode *inode,
		const struct nilfs_deduplication_block *blocks,
		size_t blocks_count)
{
	struct super_block *sb = inode->i_sb;
	const struct nilfs_deduplication_block *src_block;
	struct buffer_head *bh = NULL;
	const struct the_nilfs *nilfs = sb->s_fs_info;
	struct nilfs_bmap *bmap = NILFS_I(nilfs->ns_dat)->i_bmap;

	nilfs_info(sb, "Starting deduplication");

	WARN_ON(!blocks);
	src_block = &blocks[0];
	WARN_ON(!src_block);

	nilfs_info(
		sb,
		"SRC: ino=%ld, cno=%ld, vblocknr=%ld, blocknr=%ld, offset=%ld",
		src_block->ino, src_block->cno, src_block->vblocknr,
		src_block->blocknr, src_block->offset);

	for (size_t i = 1; i < blocks_count; ++i) {
		const struct nilfs_deduplication_block *dst_block = &blocks[i];
		WARN_ON(!dst_block);
		nilfs_info(
			sb,
			"DST: ino=%ld, cno=%ld, vblocknr=%ld, blocknr=%ld, offset=%ld",
			dst_block->ino, dst_block->cno, dst_block->vblocknr,
			dst_block->blocknr, dst_block->offset);
	}

	return 0;
}