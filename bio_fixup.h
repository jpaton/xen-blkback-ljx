#ifndef _BIO_FIXUP_H
#define _BIO_FIXUP_H

#include <linux/blk_types.h>
#include <linux/bio.h>

#include "common.h"

/**
 * Checks whether some sectors is included in a bio
 */
static inline bool bio_contains(struct bio *bio, sector_t sector, size_t nr_sec) {
	JPRINTK("does bio contain %u?", (unsigned int)sector);
	return sector >= bio->bi_sector && 
		sector + nr_sec <= bio->bi_sector + bio_sectors(bio);
}

#endif /* _BIO_FIXUP_H */
