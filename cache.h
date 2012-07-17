#ifndef _CACHE_H
#define _CACHE_H

/* change these if necessary */
#define LOG_BLOCK_SIZE 	3 	/* log of block size in sectors */
#define SECTOR_SIZE 	512	/* sector size in bytes */
#define CACHE_SIZE	20000	/* maximum cache size in blocks */
#define LJX_BLOCK_SIZE	(SECTOR_SIZE << LOG_BLOCK_SIZE)

extern bool fetch_page(struct xen_blkif *, struct page *page, unsigned int sector_number, unsigned int nsec);

extern void store_page(struct xen_blkif *, struct page *, unsigned int sector_number);

extern void invalidate(struct bio *);

#endif
