#include <linux/highmem.h>
#include <linux/gfp.h>
#include <linux/radix-tree.h>
#include <linux/list.h>
#include <linux/spinlock.h>

#include "common.h"
#include "cache.h"

static unsigned int num_cached_blocks = 0;
static LIST_HEAD(lru_list);

/*
 * Little helpful macro to figure out the index and virtual address of the
 * pending_pages[..]. For each 'pending_req' we have have up to
 * BLKIF_MAX_SEGMENTS_PER_REQUEST (11) pages. The seg would be from 0 through
 * 10 and would index in the pending_pages[..].
 */
static inline int vaddr_pagenr(struct pending_req *req, int seg, struct xen_blkbk *blkbk)
{
	return (req - blkbk->pending_reqs) *
		BLKIF_MAX_SEGMENTS_PER_REQUEST + seg;
}

#define pending_page(req, seg, blkbk) pending_pages[vaddr_pagenr(req, seg, blkbk)]

struct cache_entry {
	struct list_head	lru;
	struct radix_tree_root	*radix_tree;
	bool			valid;
	unsigned long		block;		/* block number of this entry */
	char			data[SECTOR_SIZE << LOG_BLOCK_SIZE];
};

static struct cache_entry * new_cache_entry(void) {
	struct cache_entry *entry = kmalloc(sizeof(struct cache_entry), GFP_ATOMIC);
	if (unlikely(!entry))
		return NULL;
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
	struct radix_tree_root *block_cache = &blkif->vbd.block_cache;

	entry = (struct cache_entry *) radix_tree_lookup(block_cache, block);

	return entry;
}

/**
 * Evicts entry from the cache.
 */
static void evict(struct cache_entry *entry) {
	entry->valid = false;
	list_del_init(&entry->lru);
	radix_tree_delete(entry->radix_tree, entry->block);
	kfree(entry);
}

/**
 * removes the entry from the LRU list and blkif's radix tree and kfree's the entry. 
 */
static void evict_page(struct xen_blkif *blkif) {
	struct cache_entry *lru_entry;

	lru_entry = list_entry(lru_list.next, struct cache_entry, lru);
	evict(lru_entry);
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
static int __copy_block(struct page *page, char *buf, size_t start_offset, size_t size, bool direction) {
	char *data;

	data = kmap_atomic(page);
	if (! data) 
		return -ENOMEM;
	if (direction)
		memcpy(buf, data, size);
	else
		memcpy(data, buf, size);
	kunmap_atomic(data);

	return 0;
}

/**
 * Copies bio's data into buf. Buf had better be large enough!
 * Starts from byte start_offset and does size bytes.
 */
static int copy_block_to_buf(struct page *page, char *buf, size_t start_offset, size_t size) {
	return __copy_block(page, buf, start_offset, size, true);
}

/**
 * Copies buf into bio's data.
 * Starts from byte start_offset and does size bytes.
 */
static int copy_buf_to_block(struct page *page, char *buf, size_t start_offset, size_t size) {
	return __copy_block(page, buf, start_offset, size, false);
}

/**
 * loads data from bio into cache 
 */
static int load_data(struct cache_entry *entry, struct page *page) {
	return copy_block_to_buf(page, entry->data, 0, SECTOR_SIZE << LOG_BLOCK_SIZE);
}

/**
 * Compute the checksum of a page
 */
static unsigned long compute_checksum(struct page *page, int *error) {
	unsigned long long checksum;
	unsigned long long *data, index;
       
	checksum = 0;
	data = kmap_atomic(page);	
	if (! data) {
		*error = -ENOMEM;
		return 0;
	}
	for (index = 0; index < PAGE_SIZE / sizeof(unsigned long long); index++) {
		checksum += data[index];
	}
	kunmap_atomic(data);

	return checksum;
}

/**
 * Checks to see whether *page is a page we have previously written into. If it is,
 * check to see whether its contents are likely the same (by using the checksum). Report
 * the result.
 */
static void check_for_eviction(struct xen_blkif *blkif, struct page *page) {
	struct radix_tree_root *pages_seen = &blkif->vbd.eviction_detector;
	unsigned long long *checksum, newchecksum;
	int error;

	checksum = radix_tree_lookup(pages_seen, (unsigned long) page);
	error = 0;
	newchecksum = compute_checksum(page, &error);
	if (error) return;
	if (!checksum) {
		checksum = kmalloc(sizeof(unsigned long long), GFP_ATOMIC);
		if (!checksum) return;
		blkif->vbd.ljx_info.unrecognized_pages++;
		radix_tree_insert(pages_seen, (unsigned long) page, checksum);
	} else {
		if (newchecksum != *checksum) 
			blkif->vbd.ljx_info.changed_pages++;
		else
			blkif->vbd.ljx_info.same_pages++;
	}
	*checksum = newchecksum;
}

/**
 * checks whether bio can be satisfied by cache. If so, satisfies the request
 * and returns true. Otherwise, returns false.
 **/
extern bool fetch_page(
		struct xen_blkif *blkif, 
		struct page *page, 
		unsigned int sector_number, 
		unsigned int nsec
) {
	unsigned long block = sector_number >> LOG_BLOCK_SIZE;
	struct cache_entry *entry;
	bool success = false;

	if (unlikely((sector_number & ((1 << LOG_BLOCK_SIZE) - 1)))) {
		return false;
	}

	check_for_eviction(blkif, page);
	entry = find_entry(block, blkif);
	if (entry && entry->valid) 
		success = !copy_buf_to_block(page, entry->data, 0, nsec * SECTOR_SIZE);
	if (success)
		DPRINTK("HIT on block %lu", block);
	else
		DPRINTK("MISS on block %lu", block);

	return success;
}

/**
 * Possibly stores a completed bio in cache. Assumes that bio has no errors.
 **/
extern void store_page(struct xen_blkif *blkif, struct page *page, unsigned int sector_number) {
	unsigned long block = sector_number >> LOG_BLOCK_SIZE;
	struct cache_entry *entry;
	struct radix_tree_root *block_cache = &blkif->vbd.block_cache;

	if (unlikely(sector_number & ((1 << LOG_BLOCK_SIZE) - 1))) {
		return;
	}

	entry = find_entry(block, blkif);
	if (!entry) {
		/* must create entry */
		entry = new_cache_entry();
		if (!entry)
			return;
		radix_tree_insert(block_cache, block, entry);
		entry->radix_tree = block_cache;
		entry->block = block;
		num_cached_blocks++;
	}
	if (!load_data(entry, page)) { 
		entry->valid = true;
		add_to_cache(entry);
		DPRINTK("loaded block %lu", block);
	}
	else {
		DPRINTK("failed to load block %lu", block);
		num_cached_blocks--;
		list_del_init(&entry->lru);
		kfree(radix_tree_delete(&blkif->vbd.block_cache, block));
	}
	while (num_cached_blocks >= CACHE_SIZE) {
		/* must evict something from cache */
		evict_page(blkif);
		num_cached_blocks--;
	}
}

/**
 * Marks an entry as invalid
 */
extern void invalidate(struct bio *bio, struct xen_blkbk *blkbk) {
	unsigned long block = bio->bi_sector >> LOG_BLOCK_SIZE;
	struct pending_req *preq = bio->bi_private;
	struct cache_entry *entry;
	struct page *page;
	unsigned long i, page_nr;

	DPRINTK("invalidate called -- cache size %u", num_cached_blocks);

	for (i = block;
			i < block + bio_blocks(bio);
			i++) {
		entry = find_entry(i, preq->blkif);
		if (entry) {
			DPRINTK("block %lu found", block);
			evict(entry);
		} else {
			DPRINTK("block %lu NOT found", block);
		}
		for (page_nr = 0; page_nr < preq->nr_pages; page_nr++) {
			page = blkbk->pending_page(preq, page_nr, blkbk);
			radix_tree_delete(&preq->blkif->vbd.eviction_detector, (unsigned long) page);
		}
	}
}
