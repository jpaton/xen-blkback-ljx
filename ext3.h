#ifndef _EXT3_H
#define _EXT3_H
#include "ljx.h"

struct ljx_ext3_superblock {
};

/**
 * Based on fs/ext3/super.c:1630. Can't use bread, however
 */
extern int ljx_ext3_fill_super(void **, struct ext3_super_block *, int);

/**
 * Tests whether the block I/O included a valid superblock
 */
extern bool valid_ext3_superblock(struct bio *, char *);

#endif
