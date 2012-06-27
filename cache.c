#include <linux/highmem.h>
#include <linux/gfp.h>
#include <linux/radix-tree.h>
#include <linux/list.h>
#include <linux/spinlock.h>

#include "common.h"

/* change these if necessary */
#define LOG_BLOCK_SIZE 	3 	/* log of block size in sectors */
#define SECTOR_SIZE 		512	/* sector size in bytes */
#define CACHE_SIZE		10	/* maximum cache size in blocks */

static unsigned int num_cached_blocks = 0;
static LIST_HEAD(lru_list);
static DEFINE_SPINLOCK(lru_lock);

struct cache_entry {
	struct list_head	lru;
	void 			*data;
	bool			valid;
	unsigned long		block;		/* block number of this entry */
};

static struct cache_entry * new_cache_entry(void) {
	struct cache_entry *entry = kzalloc(sizeof(struct cache_entry), GFP_ATOMIC);
	INIT_LIST_HEAD(&entry->lru);
	entry->valid = false;
	return entry;
}

/**
 * locates the cache entry in a given block interface's cache radix tree, creating it if
 * necessary.
 * @block: disk block number of sought entry 
 * @blkif: the struct xen_blkif whose tree should be searched
 **/
static struct cache_entry *find_entry(unsigned int block, struct xen_blkif *blkif) {
	struct cache_entry *entry;
	struct radix_tree_root *block_cache = &blkif->block_cache;

	entry = (struct cache_entry *) radix_tree_lookup(block_cache, block);

	if (entry == NULL) {
		/* must create entry */
		entry = new_cache_entry();
		if (!entry)
			return NULL;
		radix_tree_insert(block_cache, block, entry);
	}

	return entry;
}

/**
 * removes the entry from the LRU list and blkif's radix tree and kfree's the entry. Returns
 * pointer to the entry's data buffer.
 */
static void *evict_page(struct xen_blkif *blkif) {
	struct cache_entry *lru_entry;
	unsigned long flags;
	void *data;

	spin_lock_irqsave(&lru_lock, flags);
	if (list_empty(&lru_list)) {
		JPRINTK("LRU list was empty!");
		return NULL;
	}
	lru_entry = list_entry(lru_list.prev, struct cache_entry, lru);
	lru_entry->valid = false;
	data = lru_entry->data;
	JPRINTK("deleting entry from lru list");
	if (lru_entry->lru.next == LIST_POISON1)
		JPRINTK("LIST_POISON1");
	if (lru_entry->lru.prev == LIST_POISON2)
		JPRINTK("LIST_POISON2");
	list_del(&lru_entry->lru);
	spin_unlock_irqrestore(&lru_lock, flags);
	//kfree(lru_entry);

	return data;
}

static void add_to_cache(struct cache_entry *entry) {
	unsigned long flags;

	spin_lock_irqsave(&lru_lock, flags);
	if (list_empty(&entry->lru)) {
		JPRINTK("empty");
		list_add(&entry->lru, &lru_list);
	}
	else {
		JPRINTK("moving");
		list_move(&entry->lru, &lru_list);
	}
	spin_unlock_irqrestore(&lru_lock, flags);
}

/**
 * direction == true: copy block to buf
 * direction == false: copy buf to block
 */
static int __copy_block(struct bio *bio, char *buf, size_t start_offset, size_t size, bool direction) {
	unsigned int seg_idx, intra_idx, byte_idx;
	struct bio_vec *bvl;
	char *bufPtr = buf;
	char *data;

	byte_idx = 0;
	__bio_for_each_segment(bvl, bio, seg_idx, 0) {
		data = kmap_atomic(bvl->bv_page);
		if (! data) 
			return -ENOMEM;
		for (intra_idx = bvl->bv_offset;
				bufPtr < buf + size && intra_idx < bvl->bv_offset + bvl->bv_len;
				intra_idx++, byte_idx++)
			if (byte_idx >= start_offset) {
				if (direction)
					*(bufPtr++) = data[intra_idx];
				else
					data[intra_idx] = *(bufPtr++);
			}
		kunmap_atomic(data);
	}

	return 0;
}

/**
 * Copies bio's data into buf. Buf had better be large enough!
 * Starts from byte start_offset and does size bytes.
 */
static int copy_block_to_buf(struct bio *bio, char *buf, size_t start_offset, size_t size) {
	return __copy_block(bio, buf, start_offset, size, true);
}

/**
 * Copies buf into bio's data.
 * Starts from byte start_offset and does size bytes.
 */
static int copy_buf_to_block(struct bio *bio, char *buf, size_t start_offset, size_t size) {
	return __copy_block(bio, buf, start_offset, size, false);
}

/**
 * loads data from bio into cache 
 */
static int load_data(struct cache_entry *entry, struct bio *bio) {
	int ret;

	entry->data = kmalloc(SECTOR_SIZE << LOG_BLOCK_SIZE, GFP_ATOMIC);
	if (! entry->data)
		return -ENOMEM;
	/*
	if ((ret = copy_block_to_buf(bio, entry->data, 0, SECTOR_SIZE << LOG_BLOCK_SIZE)))
		* error *
		return ret;
	*/
	add_to_cache(entry);
	entry->valid = true;

	return 0;
}

/**
 * checks whether bio can be satisfied by cache. If so, satisfies the request
 * and returns true. Otherwise, returns false.
 **/
extern bool fetch_page(struct xen_vbd *vbd, struct bio *bio) {
	unsigned long block = bio->bi_sector << LOG_BLOCK_SIZE;
	struct pending_req *preq = bio->bi_private;
	struct cache_entry *entry;
	unsigned long flags;
	bool success = false;

	spin_lock_irqsave(&preq->blkif->cache_lock, flags);
	entry = find_entry(block, preq->blkif);
	if (entry && entry->valid) 
		success = !copy_buf_to_block(bio, entry->data, 0, SECTOR_SIZE << LOG_BLOCK_SIZE);
	spin_unlock_irqrestore(&preq->blkif->cache_lock, flags);

	return success;
}

/**
 * Possibly stores a completed bio in cache. Assumes that bio has no errors.
 **/
extern void store_page(struct xen_vbd *vbd, struct bio *bio) {
	unsigned long block = bio->bi_sector << LOG_BLOCK_SIZE;
	struct pending_req *preq = bio->bi_private;
	struct cache_entry *entry;
	unsigned long flags;

	spin_lock_irqsave(&preq->blkif->cache_lock, flags);
	entry = find_entry(block, preq->blkif);
	if (entry) {
		if (!load_data(entry, bio))
			num_cached_blocks++;
		else {
			JPRINTK("failed to load data");
			kfree(radix_tree_delete(&preq->blkif->block_cache, block));
		}
	}
	while (num_cached_blocks >= CACHE_SIZE) {
		/* must evict something from cache */
		kfree(evict_page(preq->blkif));
		num_cached_blocks--;
	}
	spin_unlock_irqrestore(&preq->blkif->cache_lock, flags);
}
