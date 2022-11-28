#ifndef EXT2_SUPER_H
#define EXT2_SUPER_H

int ext2_fill_super(struct super_block *sb, void *data, int silent);

#endif
