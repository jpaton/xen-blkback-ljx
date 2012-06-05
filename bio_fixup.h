#ifndef _BIO_FIXUP_H
#define _BIO_FIXUP_H

#include <linux/blk_types.h>
#include <linux/bio.h>

/**
 * Checks whether a sector is included in a bio
 */
bool inline bio_contains(struct bio *bio, sector_t sector) {
	return sector >= bio->bi_sector && sector < bio->bi_sector + bio_sectors(bio);
}

#endif /* _BIO_FIXUP_H */
