#include <linux/fs.h>
#include <linux/jbd.h>
#include <linux/capability.h>
#include <linux/ext3_fs.h>

#include "ext3.h"
#include "util.h"
#include "bio_fixup.h"

/**
 * Based on fs/ext3/super.c:1630. Can't use bread, however. 
 */
extern int ljx_ext3_fill_super(
		struct ljx_ext3_superblock **pp_lsb, 
		struct ext3_super_block *sb, 
		int silent
) {
	struct ljx_ext3_superblock *lsb;

	lsb = kzalloc(sizeof(struct ljx_ext3_superblock), GFP_KERNEL);
	if (! lsb)
		return -ENOMEM;
	*pp_lsb = lsb;

	lsb->inodes_count = le32_to_cpu(sb->s_inodes_count);
	lsb->blocks_count = le32_to_cpu(sb->s_blocks_count);
	lsb->inode_size = le32_to_cpu(sb->s_inode_size);

	return 0;
}

/**
 * Tests whether the block I/O included a valid superblock. If it is not valid, return 1.
 * If there is an error, return an error code. If it is valid, return 0. After calling,
 * buf will contain the superblock (or what would have been the superblock if it had been
 * valid.
 */
extern int valid_ext3_superblock(struct bio *bio, char *buf) {
	/* TODO: make more sophisticated */
	struct ext3_super_block *sb;
	size_t start_offset;
	int ret;

	JPRINTK("testing superblock for validity");
	if (!bio_contains(bio, 2, 2)) {
		/* bio doesn't contain sector 2 or 3 */
		JPRINTK("no");
		return 1;
	}
	JPRINTK("yes");

	/* compute the first byte of the superblock's expected location */
	start_offset = (2 - bio->bi_sector) * 512;

	/* traverse data and load into buffer */
	if ((ret = copy_block(bio, buf, start_offset, sizeof(struct ext3_super_block))))
		return ret;
	sb = (struct ext3_super_block *) buf;

	/* do some sanity checks */
	/* TODO: this is pretty scant at best; also would be nice to detect when the 
	 * superblock is corrupt and wait until the OS repairs it */
	JPRINTK("inodes count: %d. blocks count: %d.", le32_to_cpu(sb->s_inodes_count), le32_to_cpu(sb->s_blocks_count));
	if (!(sb->s_inodes_count && 
	      sb->s_blocks_count &&
	      sb->s_inode_size &&
	      !(sb->s_inode_size & (sb->s_inode_size - 1)) // check power of 2
	      )) {
		JPRINTK("one was false!");
		JPRINTK("%d", le32_to_cpu(sb->s_inodes_count));
		JPRINTK("%d", le32_to_cpu(sb->s_blocks_count));
		return 1;
	}

	return 0;
}

