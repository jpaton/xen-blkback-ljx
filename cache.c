#include <linux/highmem.h>
#include <linux/gfp.h>
#include <linux/radix-tree.h>
#include <linux/list.h>
#include <linux/spinlock.h>

#include "common.h"

/* change these if necessary */
#define LOG_BLOCK_SIZE 	3 	/* log of block size in sectors */
#define SECTOR_SIZE 	512	/* sector size in bytes */
#define CACHE_SIZE	65536	/* maximum cache size in blocks */

static RADIX_TREE(radix_tree, GFP_KERNEL);
static DEFINE_SPINLOCK(tree_lock);
static unsigned int num_cached_blocks = 0;
static LIST_HEAD(lru_list);

struct cache_entry {
	struct list_head 	entry_list;
	struct list_head	lru;
	void 			*data;
	domid_t			domid;
	unsigned int 		handle;
	bool			valid;
};

static struct cache_entry * new_cache_entry(void) {
	struct cache_entry *entry = kzalloc(sizeof(struct cache_entry), GFP_KERNEL);
	INIT_LIST_HEAD(&entry->entry_list);
	INIT_LIST_HEAD(&entry->lru);
	entry->valid = false;
	return entry;
}

/**
 * located the entry list in the radix tree, creating it if necessary
 * @block: disk block number of sought entry 
 **/
static struct cache_entry *find_entry(unsigned int block, struct xen_blkif *blkif) {
	struct list_head *entry_list;
	struct cache_entry *pos;

	entry_list = (struct list_head *) radix_tree_lookup(&radix_tree, block);

	if (entry_list == NULL) {
		entry_list = kmalloc(sizeof(struct list_head), GFP_KERNEL);
		if (! entry_list) {
			JPRINTK("out of memory");
			return NULL;
		}
		INIT_LIST_HEAD(entry_list);
		radix_tree_insert(&radix_tree, block, entry_list);
	}

	list_for_each_entry(pos, entry_list, entry_list) {
		if (pos->handle == blkif->handle &&
				pos->domid == blkif->domid) 
			goto found;
	}

	pos = new_cache_entry();
	pos->handle = blkif->handle;
	pos->domid = blkif->domid;
	list_add(&pos->entry_list, entry_list);
found:
	return pos;
}

static void evict_page(void) {
	struct cache_entry *lru_entry;

	lru_entry = list_entry(lru_list.prev, struct cache_entry, lru);
	lru_entry->valid = false;
	/* TODO: free data */
	list_del_init(&lru_entry->lru);
}

static void add_to_cache(struct cache_entry *entry) {
	if (list_empty(&entry->lru))
		list_add(&entry->lru, &lru_list);
	else 
		list_move(&entry->lru, &lru_list);
}

/**
 * checks whether bio can be satisfied by cache. If so, returns a pointer to the
 * cache copy of the block.
 **/
extern void *fetch_page(struct xen_vbd *vbd, struct bio *bio) {
	unsigned long block = bio->bi_sector << LOG_BLOCK_SIZE;
	struct pending_req *preq = bio->bi_private;
	struct cache_entry *entry;
	unsigned long flags;

	spin_lock_irqsave(&tree_lock, flags);
	entry = find_entry(block, preq->blkif);
	if (entry->valid)
		JPRINTK("HIT");
	else {
		JPRINTK("MISS");
		while (num_cached_blocks >= CACHE_SIZE) {
			/* must evict something from cache */
			evict_page();
		}
		/* TODO: load data here */
		add_to_cache(entry);
	}
	entry->valid = true;
	spin_unlock_irqrestore(&tree_lock, flags);

	return entry->data;
}

/**
 * possibly stores a completed bio in cache
 **/
extern void store_page(struct xen_vbd *vbd, struct bio *bio) {
	unsigned long block = bio->bi_sector << LOG_BLOCK_SIZE;
	struct pending_req *preq = bio->bi_private;
	struct cache_entry *entry;
	unsigned long flags;

	spin_lock_irqsave(&tree_lock, flags);
	entry = find_entry(block, preq->blkif);
	while (num_cached_blocks >= CACHE_SIZE) {
		/* must evict something from cache */
		evict_page();
	}
	/* TODO: load data */
	add_to_cache(entry);
	entry->valid = true;
	spin_unlock_irqrestore(&tree_lock, flags);
}
