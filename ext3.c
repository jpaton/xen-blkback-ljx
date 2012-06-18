#include <linux/fs.h>
#include <linux/jbd.h>
#include <linux/capability.h>
#include <linux/ext3_fs.h>

struct label;

#include "ext3.h"
#include "common.h"
#include "label.h"
#include "util.h"
#include "bio_fixup.h"

#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define MIN(a, b) ((a) > (b) ? (b) : (a))

static int process_inode_block (
		struct bio *bio, 
		struct xen_vbd *vbd, 
		struct label *label
) {
	return 0;
}

/* process group descriptors */
static int process_group_desc(
		struct bio *bio, 
		struct xen_vbd *vbd, 
		struct label *label
) {
	struct ext3_group_desc *desc;
	struct ljx_ext3_superblock *lsb = vbd->superblock;
	struct label *tlabel;
	unsigned int num_groups;
	int i, ret;
	void *buf;

	num_groups = label->nr_sec * SECTOR_SIZE / sizeof(struct ext3_group_desc);
	num_groups = lsb->groups_count;
	buf = kzalloc(num_groups * sizeof(struct ext3_group_desc), GFP_KERNEL);
	if ((ret = copy_block(bio, buf, 0, num_groups * sizeof(struct ext3_group_desc))))
		return ret;

	JPRINTK("scanning %u groups", num_groups);
	for (i = 0; i < num_groups; i++) {
		desc = (struct ext3_group_desc *) (buf + i * sizeof(struct ext3_group_desc));
		tlabel = insert_label(
				&vbd->label_list,
				le32_to_cpu(desc->bg_inode_table) * lsb->block_size,
				lsb->inodes_per_group * lsb->inode_size / SECTOR_SIZE,
				INODE_BLOCK);
		tlabel->processor = &process_inode_block;
		JPRINTK("group %u desc:", i);
		JPRINTK("\tblock_bitmap: %u, inode_bitmap: %u, used_dirs: %u",
				le32_to_cpu(desc->bg_block_bitmap),
				(unsigned int) le32_to_cpu(desc->bg_inode_bitmap),
				(unsigned int) le32_to_cpu(desc->bg_used_dirs_count));
	}

	kfree(buf);

	return 0;
}

#define LOGIC_SB_BLOCK 2 /* for now, this is just hardcoded */

#define LJX_EXT3_HAS_INCOMPAT_FEATURE(sb,mask)			\
	( sb->feature_incompat & (mask) )

#define LJX_EXT3_HAS_RO_COMPAT_FEATURE(sb,mask)			\
	( sb->feature_ro_compat & (mask) )

static inline int test_root(int a, int b)
{
	int num = b;

	while (a > num)
		num *= b;
	return num == a;
}

static int ext3_group_sparse(int group)
{
	if (group <= 1)
		return 1;
	if (!(group & 1))
		return 0;
	return (test_root(group, 7) || test_root(group, 5) ||
		test_root(group, 3));
}

static ext3_fsblk_t descriptor_loc(struct ljx_ext3_superblock *sb, int nr) {
	unsigned long bg, first_meta_bg;
	int has_super = 0;

	first_meta_bg = sb->first_meta_bg;
	if (!LJX_EXT3_HAS_INCOMPAT_FEATURE(sb, EXT3_FEATURE_INCOMPAT_META_BG) ||
	    nr < first_meta_bg)
		return (LOGIC_SB_BLOCK + nr + 1);
	bg = sb->desc_per_block * nr;
	if (! (LJX_EXT3_HAS_RO_COMPAT_FEATURE(sb,
				EXT3_FEATURE_RO_COMPAT_SPARSE_SUPER) &&
			!ext3_group_sparse(bg)))
		has_super = 1;
	return (has_super + (
				bg * (ext3_fsblk_t)(sb->blocks_per_group) +
				sb->first_data_block)
	       );
}

/**
 * Based on fs/ext3/super.c:1630. Can't use bread, however. 
 */
extern int ljx_ext3_fill_super(
		struct xen_vbd *vbd,
		struct ext3_super_block *sb, 
		int silent
) {
	struct ljx_ext3_superblock *lsb;
	struct ljx_ext3_superblock **pp_lsb; 
	struct label *label;
	unsigned int blocksize, db_count, i, block, groups_left;

	lsb = kzalloc(sizeof(struct ljx_ext3_superblock), GFP_KERNEL);
	if (! lsb)
		return -ENOMEM;
	pp_lsb = &vbd->superblock;
	*pp_lsb = lsb;

	/* compute blocksize
	minsize = bdev_logical_block_size(vbd->bdev);
	size = EXT3_MIN_BLOCK_SIZE;
	if (size < minsize)
		size = minsize;
	*/
	blocksize = vbd->bdev->bd_block_size;

	lsb->inodes_count      =  le32_to_cpu(sb->s_inodes_count);
	lsb->blocks_count      =  le32_to_cpu(sb->s_blocks_count);
	lsb->inode_size        =  le32_to_cpu(sb->s_inode_size);
	lsb->first_data_block  =  le32_to_cpu(sb->s_first_data_block);
	lsb->log_block_size    =  le32_to_cpu(sb->s_log_block_size);
	lsb->log_frag_size     =  le32_to_cpu(sb->s_log_frag_size);
	lsb->blocks_per_group  =  le32_to_cpu(sb->s_blocks_per_group);
	lsb->frags_per_group   =  le32_to_cpu(sb->s_frags_per_group);
	lsb->inodes_per_group  =  le32_to_cpu(sb->s_inodes_per_group);
	lsb->first_inode       =  le32_to_cpu(sb->s_first_ino);
	lsb->journal_inum      =  le32_to_cpu(sb->s_journal_inum);
	lsb->inodes_per_block  =  blocksize / le32_to_cpu(sb->s_inode_size);
	lsb->desc_per_block    =  blocksize / sizeof(struct ext3_group_desc);
	lsb->groups_count      =  ((le32_to_cpu(sb->s_blocks_count) -
			       le32_to_cpu(sb->s_first_data_block) - 1)
				       / lsb->blocks_per_group) + 1;
	lsb->first_meta_bg     =  le32_to_cpu(sb->s_first_meta_bg);
	lsb->feature_incompat  =  le32_to_cpu(sb->s_feature_incompat);
	lsb->feature_ro_compat  =  le32_to_cpu(sb->s_feature_ro_compat);
	lsb->block_size		=  EXT3_MIN_BLOCK_SIZE << lsb->log_block_size;

	db_count = DIV_ROUND_UP(lsb->groups_count, lsb->desc_per_block);
	lsb->group_desc = kzalloc(db_count * sizeof(struct ljx_ext3_group_desc *),
			GFP_KERNEL);
	if (lsb->group_desc == NULL)
		return -ENOMEM;
	lsb->group_desc = kzalloc(db_count * sizeof(*lsb->group_desc), GFP_KERNEL);
	groups_left = lsb->groups_count;
	for (i = 0; i < db_count; i++) {
		block = descriptor_loc(lsb, i);
		lsb->group_desc[i].init = false;
		lsb->group_desc[i].location = block;
		JPRINTK("group desc at %u", block);
		label = insert_label(
				&vbd->label_list, 
				block, 
				8,
				GROUP_DESC
		);
		label->processor = &process_group_desc;
	}
	JPRINTK("Total number of groups: %u", lsb->groups_count);
	print_label_list(&vbd->label_list);

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

