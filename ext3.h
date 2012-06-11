#ifndef _EXT3_H
#define _EXT3_H
#include "ljx.h"

struct ljx_ext3_superblock {
	u32 inodes_count;
	u32 blocks_count;
	u32 inode_size;
};

/**
 * Based on fs/ext3/super.c:1630. Can't use bread, however
 */
extern int ljx_ext3_fill_super(
		struct ljx_ext3_superblock **, 
		struct ext3_super_block *, 
		int
);

/**
 * Tests whether the block I/O included a valid superblock
 */
extern int valid_ext3_superblock(struct bio *, char *);

#endif
