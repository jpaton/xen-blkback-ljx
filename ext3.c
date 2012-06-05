#include <linux/fs.h>
#include <linux/jbd.h>
#include <linux/capability.h>
#include <linux/ext3_fs.h>

#include "ext3.h"
#include "bio_fixup.h"

/**
 * Based on fs/ext3/super.c:1630. Can't use bread, however. 
 */
extern int ljx_ext3_fill_super(
		void **superblock, 
		struct ext3_super_block *sb, 
		int silent
) {
	struct ljx_ext3_superblock lsb;

	*superblock = &lsb;

	return 0;
}

/**
 * Tests whether the block I/O included a valid superblock
 */
extern bool valid_ext3_superblock(struct bio *bio, char *buf) {
	/* TODO: write the test */
	struct ext3_super_block *sb;
	int seg_idx, intra_idx;
	struct bio_vec *bvl;
	char *bufPtr = buf;
	char *data;

	if (!bio_contains(bio, 2) || !bio_contains(bio, 3))
		/* bio doesn't contain sector 2 or 3 */
		return false;

	/* traverse data and load into buffer */
	/* TODO: add check to hopefully skip this and just look directly into the page */
	__bio_for_each_segment(bvl, bio, seg_idx, 0) {
		data = kmap_atomic(bvl->bv_page);
		if (! data) {
			printk(KERN_INFO "LJX: ran out of memory checking superblock");
			return false;
		}
		for (intra_idx = bvl->bv_offset;
				bufPtr < buf + 1024 && intra_idx < bvl->bv_offset + bvl->bv_len;
				intra_idx++, bufPtr++)
			*bufPtr = data[intra_idx];
		kunmap_atomic(data);
	}

	sb = (struct ext3_super_block *) buf;

	/* do some sanity checks */
	/* TODO: this is pretty scant at best; also would be nice to detect when the 
	 * superblock is corrupt and wait until the OS repairs it */
	if (!(sb->s_inodes_count && 
	      sb->s_blocks_count &&
	      sb->s_r_blocks_count &&
	      sb->s_first_data_block &&
	      sb->s_log_block_size &&
	      sb->s_log_frag_size))
		return false;

	return true;
}

