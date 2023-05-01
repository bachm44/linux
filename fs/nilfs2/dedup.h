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

int nilfs_dedup(struct inode *inode, __u64 blocks_to_consider);

#endif /* _NILFS_DEDUP_H */
