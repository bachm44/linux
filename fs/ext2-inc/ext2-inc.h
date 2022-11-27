#ifndef EXT2_INC_H
#define EXT2_INC_H

#include <linux/types.h>

typedef __u8 u8;
typedef __u16 u16;
typedef __u32 u32;

// #define pr_fmt(fmt) "%s:%s: " fmt, KBUILD_MODNAME, __func__

#define SUPERBLOCK_OFFSET 1024
#define EXT2_MAGIC 0xEF53
#define EXT2_SUPERBLOCK_BLOCK_NUMBER 1
#define EXT2_DEFAULT_BLOCK_SIZE 1024
#define EXT2_ROOT_INODE_NUMBER 2

#pragma pack(push, 1)
struct ext2_inode {
	u16 i_mode; // type_permissions
	u16 i_uid; // user_id
	u32 i_size; // lower_size_bytes
	u32 i_atime; // last_access_time
	u32 i_ctime; // creation_time
	u32 i_mtime; // last_modification_time
	u32 i_dtime; // deletion_time
	u16 i_gid; // group_id
	u16 i_links_count; // hard_links_count
	u32 i_blocks; // disk_sectors_in_use_count
	u32 i_flags;
	u32 i_osd1; // os_specific_value
	u32 i_block[15];
	u32 i_generation;
	u32 i_file_acl;
	u32 i_dir_acl;
	u32 i_faddr; // fragment_block_address
	u32 i_osd2; // os_specific_value
};

struct ext2_super_block {
	u32 s_inodes_count; // total_inodes
	u32 s_blocks_count; // total_blocks
	u32 s_r_blocks_count; // blocks_reserved_for_superuser
	u32 s_free_blocks_count; // unallocated_blocks
	u32 s_free_inodes_count; // unallocated_inodes
	u32 s_first_data_block; // block_number_of_superblock
	u32 s_log_block_size; // log2_block_size
	u32 s_log_frag_size; // log2_fragment_size
	u32 s_blocks_per_group; // blocks_in_each_block_group
	u32 s_frags_per_group; // fragments_in_each_block_group
	u32 s_inodes_per_group; // inodes_in_each_block_grup
	u32 s_mtime; // last_mount_time
	u32 s_wtime; // last_written_time
	u16 s_mnt_count; // times_volume_mounted_since_consistency_check
	u16 s_max_mnt_count; // mounts_allowed_before_consistency_check
	u16 s_magic; // ext2_signature
	u16 s_state; // file_system_state
	u16 s_errors; // error_handling
	u16 s_minor_rev_level; // minor_version
	u32 s_lastcheck; // last_consistency_check
	u32 s_checkinterval; // time_between_consistency_checks
	u32 s_creator_os; // creator_os_id
	u32 s_rev_level; // major_version
	u16 s_def_resuid; // reserved_blocks_user_id
	u16 s_def_resgid; // reserved_blocks_group_id

	//extended superblock fields
	u32 s_first_ino; // first_non_reserved_inode
	u16 s_inode_size;
	u16 s_block_group_nr; // block_group_of_this_superblock
	u32 s_feature_compat; /// optional_features
	u32 s_feature_incompat; // required_features
	u32 s_feature_ro_compat; // non_supported_features
	u8 s_uuid[16];
	u8 s_volume_name[16];
	u8 s_last_mounted[64]; // last_mounted_on_path
	u32 s_algo_bitmap; // compression_algorithms
	u8 s_prealloc_blocks; // file_blocks_to_preallocate
	u8 s_prealloc_dir_blocks; // directories_blocks_to_preallocate
	u16 unused;
	u8 s_journal_uuid[16];
	u32 s_journal_inum; // journal_inode
	u32 s_journal_dev; // journal_device
	u32 s_last_orphan; // head_of_orphan_inode_list
	u8 unused2[788];
};

inline u32 ext2_super_block_block_size(struct ext2_super_block *sb)
{
	return 1024 << sb->s_log_block_size;
}

struct ext2_block_group_descriptor {
	u32 bg_block_bitmap; // block_bitmap_address
	u32 bg_inode_bitmap; // inode_bitmap_address
	u32 bg_inode_table; // inode_table_starting_block
	u16 bg_free_blocks_count; // unallocated_blocks_in_group
	u16 bg_free_inodes_count; // unallocated_inodes_in_group
	u16 bg_used_dirs_count; // directories_in_group
	u16 padding;
	u8 reserved[12];
};
#pragma pack(pop)

#endif