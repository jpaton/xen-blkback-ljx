#ifndef _EXT3_H
#define _EXT3_H
#include "ljx.h"
#include "common.h"

#define SECTOR_SIZE 512

struct xen_vbd;

struct ljx_ext3_group_desc {
	bool init;
	unsigned long location;
};

struct ljx_ext3_superblock {
	unsigned int inodes_count;
	unsigned int blocks_count;
	unsigned int inode_size;
	unsigned int first_data_block;
	unsigned int log_block_size;
	unsigned int log_frag_size;
	unsigned int blocks_per_group;
	unsigned int frags_per_group;
	unsigned int inodes_per_group;
	unsigned int first_inode;		/* first non-reserved inode */
	unsigned int journal_inum;		/* inode number of journal file */
	unsigned int inodes_per_block;
	unsigned int desc_per_block;
	unsigned int groups_count;
	unsigned int first_meta_bg;
	unsigned int feature_incompat;
	unsigned int feature_ro_compat;
	unsigned int block_size;
	struct ljx_ext3_group_desc *group_desc;
};

/**
 * Based on fs/ext3/super.c:1630. Can't use bread, however
 */
extern int ljx_ext3_fill_super(
		struct xen_vbd *, 
		struct ext3_super_block *, 
		int
);

/**
 * Tests whether the block I/O included a valid superblock
 */
extern int valid_ext3_superblock(struct bio *, char *);

#endif
