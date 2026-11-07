#ifndef _SHIM_LINUX_FS_EXT2_FS_H
#define _SHIM_LINUX_FS_EXT2_FS_H

#include <shim/linux/types.h>

// Minimal ext2 disk structure definitions for shim
struct ext2_super_block {
    u32 s_inodes_count;
    u32 s_blocks_count;
    u32 s_r_blocks_count;
    u32 s_free_blocks_count;
    u32 s_free_inodes_count;
    u32 s_first_data_block;
    u32 s_log_block_size;
    // ... add more as needed
};

#endif /* _SHIM_LINUX_FS_EXT2_FS_H */
