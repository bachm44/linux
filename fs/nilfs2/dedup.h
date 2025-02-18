/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * NILFS deduplication
 *
 * Copyright (C) 2023 Bartłomiej Chmiel
 */

#ifndef _NILFS_DEDUP_H
#define _NILFS_DEDUP_H

#include <linux/types.h>

struct inode;
struct the_nilfs;
struct nilfs_deduplication_block;

int nilfs_dedup(struct inode *inode,
		const struct nilfs_deduplication_block *blocks,
		size_t blocks_count);

int nilfs_get_last_block_in_latest_psegment(
	struct the_nilfs *nilfs, struct nilfs_deduplication_block *out);

#endif /* _NILFS_DEDUP_H */
