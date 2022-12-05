#ifndef EXT2_INODE_H
#define EXT2_INODE_H

struct super_block;

struct inode *ext2_inode_root(struct super_block *sb);
void ext2_inode_init_once(void *object);

#endif
