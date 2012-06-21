#include <linux/highmem.h>
#include <linux/gfp.h>
#include <linux/radix-tree.h>
#include <linux/list.h>

#include "common.h"

/* change these if necessary */
#define LOG_BLOCK_SIZE 3 /* log of block size in sectors */
#define SECTOR_SIZE 512	 /* sector size in bytes */

static RADIX_TREE(radix_tree, GFP_KERNEL);
static struct spin_lock tree_lock;

struct cache_entry {
	struct list_head 	entry_list;
	void 			*data;
	domid_t			domid;
	unsigned int 		handle;
	bool			valid;
};

static struct cache_entry * new_cache_entry(void) {
	struct cache_entry *entry = kzalloc(sizeof(struct cache_entry), GFP_KERNEL);
	INIT_LIST_HEAD(&entry->entry_list);
	entry->valid = false;
	return entry;
}

/**
 * checks whether bio can be satisfied by cache. If so, returns a pointer to the
 * cache copy of the block.
 **/
extern char *fetch_page(struct xen_vbd *vbd, struct bio *bio) {
	return NULL;
}

/**
 * possibly stores a completed bio in cache
 **/
extern void store_page(struct xen_vbd *vbd, struct bio *bio) {
	unsigned long block = bio->bi_sector << LOG_BLOCK_SIZE;
	struct pending_req *preq = bio->bi_private;
	struct list_head **entry_list = (struct list_head **) radix_tree_lookup_slot(&radix_tree, block);
	struct cache_entry *pos;

	if (*entry_list == NULL) {
		*entry_list = kmalloc(sizeof(struct list_head), GFP_KERNEL);
		INIT_LIST_HEAD(*entry_list);
	}

	list_for_each_entry(pos, *entry_list, entry_list) {
		if (pos->handle == preq->blkif->handle &&
				pos->domid == preq->blkif->domid) 
			goto found;
	}
	pos = new_cache_entry();
	list_add(&pos->entry_list, *entry_list);
found:
	if (! pos->valid) {
		/* cache entry not valid; must copy data in */
		pos->data = kmalloc(sector_size << log_block_size, GFP_KERNEL);
	}
}
