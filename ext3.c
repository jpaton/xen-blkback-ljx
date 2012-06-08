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
 * Tests whether the block I/O included a valid superblock
 */
extern bool valid_ext3_superblock(struct bio *bio, char *buf) {
	/* TODO: make more sophisticated */
	struct ext3_super_block *sb;
	unsigned int seg_idx, intra_idx, byte_idx, start_offset;
	struct bio_vec *bvl;
	char *bufPtr = buf;
	char *data;

	JPRINTK("testing superblock for validity");
	if (!bio_contains(bio, 2) || !bio_contains(bio, 3)) {
		/* bio doesn't contain sector 2 or 3 */
		JPRINTK("no");
		return false;
	}
	JPRINTK("yes");

	/* compute the first byte of the superblock's expected location */
	start_offset = (2 - bio->bi_sector) * 512;

	/* traverse data and load into buffer */
	/* TODO: add check to hopefully skip this and just look directly into the page */
	byte_idx = 0;
	__bio_for_each_segment(bvl, bio, seg_idx, 0) {
		data = kmap_atomic(bvl->bv_page);
		if (! data) {
			printk(KERN_INFO "LJX: ran out of memory checking superblock");
			return false;
		}
		for (intra_idx = bvl->bv_offset;
				bufPtr < buf + 1024 && intra_idx < bvl->bv_offset + bvl->bv_len;
				intra_idx++, byte_idx++)
			if (byte_idx >= start_offset)
				*(bufPtr++) = data[intra_idx];
		kunmap_atomic(data);
	}

	JPRINTK("parsed %d segments", seg_idx);

	sb = (struct ext3_super_block *) buf;

	/* do some sanity checks */
	/* TODO: this is pretty scant at best; also would be nice to detect when the 
	 * superblock is corrupt and wait until the OS repairs it */
	JPRINTK("inodes count: %d. blocks count: %d.", le32_to_cpu(sb->s_inodes_count), le32_to_cpu(sb->s_blocks_count));
	if (!(sb->s_inodes_count && 
	      sb->s_blocks_count)) {
		JPRINTK("one was false!");
		JPRINTK("%d", le32_to_cpu(sb->s_inodes_count));
		JPRINTK("%d", le32_to_cpu(sb->s_blocks_count));
		return false;
	}

	return true;
}

