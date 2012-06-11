#include "boot.h"
#include "bio_fixup.h"
#include "util.h"

/**
 * Tests whether the block I/O included a valid boot block. If it is not valid, return 1.
 * If there is an error, return an error code. If it is valid, return 0. After calling,
 * buf will contain the boot block (or what would have been the boot block if it had been
 * valid.
 */
extern int valid_boot_block(struct bio *bio, char *buf) {
	struct bootblock *bb;
	size_t start_offset;
	int ret;

	JPRINTK("testing boot block...");

	if (! bio_contains(bio, 1, 1))
		return 1;

	/* first byte of boot block */
	start_offset = (1 - bio->bi_sector) * 512;

	if ((ret = copy_block(bio, buf, start_offset, sizeof(struct bootblock))))
		return ret;
	bb = (struct bootblock *) buf;

	/* sanity check */
	if (bb->signature == MBR_SIGNATURE) {
		JPRINTK("valid boot block");
		return 0;
	}
	return 1;
}

