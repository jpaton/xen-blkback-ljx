#ifndef _CACHE_H
#define _CACHE_H

#include "common.h"

/* change these if necessary */
#define LOG_BLOCK_SIZE 	3 	/* log of block size in sectors */
#define SECTOR_SIZE 	512	/* sector size in bytes */
#define CACHE_SIZE	20000	/* maximum cache size in blocks */
#define LJX_BLOCK_SIZE	(SECTOR_SIZE << LOG_BLOCK_SIZE)

#define bio_blocks(bio)	(bio_sectors(bio) >> LOG_BLOCK_SIZE)

struct xen_blkbk {
	struct pending_req	*pending_reqs;
	/* List of all 'pending_req' available */
	struct list_head	pending_free;
	/* And its spinlock. */
	spinlock_t		pending_free_lock;
	wait_queue_head_t	pending_free_wq;
	/* The list of all pages that are available. */
	struct page		**pending_pages;
	/* And the grant handles that are available. */
	grant_handle_t		*pending_grant_handles;
};

extern bool fetch_page(struct xen_blkif *, struct page *page, unsigned int sector_number, unsigned int nsec);

extern void store_page(struct xen_blkif *, struct page *, unsigned int sector_number);

extern void invalidate(struct bio *, struct xen_blkbk *);

#endif
