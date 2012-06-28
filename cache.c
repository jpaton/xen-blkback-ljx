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
	struct radix_tree_root	*radix_tree;
	bool			valid;
	unsigned long		block;		/* block number of this entry */
	char			data[SECTOR_SIZE << LOG_BLOCK_SIZE];
};

static struct cache_entry * new_cache_entry(void) {
	struct cache_entry *entry = kmalloc(sizeof(struct cache_entry), GFP_ATOMIC);
	INIT_LIST_HEAD(&entry->lru);
	entry->valid = false;
	return entry;
}

/**
 * locates the cache entry in a given block interface's cache radix tree, returning
 * NULL if it doesn't exist.
 * 
 * @block: disk block number of sought entry 
 * @blkif: the struct xen_blkif whose tree should be searched
 **/
static struct cache_entry *find_entry(unsigned int block, struct xen_blkif *blkif) {
	struct cache_entry *entry;
	struct radix_tree_root *block_cache = &blkif->block_cache;

	entry = (struct cache_entry *) radix_tree_lookup(block_cache, block);

	return entry;
}

/**
 * removes the entry from the LRU list and blkif's radix tree and kfree's the entry. 
 */
static void evict_page(struct xen_blkif *blkif) {
	struct cache_entry *lru_entry;

	lru_entry = list_entry(lru_list.next, struct cache_entry, lru);
	lru_entry->valid = false;
	list_del_init(&lru_entry->lru);
	radix_tree_delete(lru_entry->radix_tree, lru_entry->block);
	kfree(lru_entry);
}

static void add_to_cache(struct cache_entry *entry) {

	if (list_empty(&entry->lru)) 
		list_add_tail(&entry->lru, &lru_list);
	else 
		list_rotate_left(&lru_list);
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

	if ((ret = copy_block_to_buf(bio, entry->data, 0, SECTOR_SIZE << LOG_BLOCK_SIZE)))
		/* error */
		return ret;

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

	spin_lock_irqsave(&lru_lock, flags);
	entry = find_entry(block, preq->blkif);
	if (entry && entry->valid) 
		success = !copy_buf_to_block(bio, entry->data, 0, SECTOR_SIZE << LOG_BLOCK_SIZE);
	spin_unlock_irqrestore(&lru_lock, flags);

	return success;
}

/**
 * Possibly stores a completed bio in cache. Assumes that bio has no errors.
 **/
extern void store_page(struct xen_vbd *vbd, struct bio *bio) {
	unsigned long block = bio->bi_sector << LOG_BLOCK_SIZE;
	struct pending_req *preq = bio->bi_private;
	struct cache_entry *entry;
	struct radix_tree_root *block_cache = &preq->blkif->block_cache;
	unsigned long flags;

	spin_lock_irqsave(&lru_lock, flags);
	entry = find_entry(block, preq->blkif);
	if (!entry) {
		/* must create entry */
		entry = new_cache_entry();
		if (!entry)
			return;
		radix_tree_insert(block_cache, block, entry);
		entry->radix_tree = block_cache;
		num_cached_blocks++;
	}

	if (!load_data(entry, bio)) { 
		entry->valid = true;
		add_to_cache(entry);
	}
	else {
		JPRINTK("failed to load data");
		num_cached_blocks--;
		list_del_init(&entry->lru);
		kfree(radix_tree_delete(&preq->blkif->block_cache, block));
	}

	while (num_cached_blocks >= CACHE_SIZE) {
		/* must evict something from cache */
		evict_page(preq->blkif);
		num_cached_blocks--;
	}

	spin_unlock_irqrestore(&lru_lock, flags);
}
