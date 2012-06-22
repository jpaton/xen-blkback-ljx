#include <linux/highmem.h>
#include <linux/gfp.h>
#include <linux/radix-tree.h>
#include <linux/list.h>
#include <linux/spinlock.h>

#include "common.h"

/* change these if necessary */
#define LOG_BLOCK_SIZE 	3 	/* log of block size in sectors */
#define SECTOR_SIZE 	512	/* sector size in bytes */
#define CACHE_SIZE	10	/* maximum cache size in blocks */

static RADIX_TREE(radix_tree, GFP_ATOMIC);
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
	unsigned long		block;		/* block number of this entry */
};

static struct cache_entry * new_cache_entry(void) {
	struct cache_entry *entry = kzalloc(sizeof(struct cache_entry), GFP_ATOMIC);
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
		entry_list = kmalloc(sizeof(struct list_head), GFP_ATOMIC);
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
	bool last_entry = false;

	lru_entry = list_entry(lru_list.prev, struct cache_entry, lru);
	lru_entry->valid = false;
	if (lru_entry->data)
		kfree(lru_entry->data);
	if (lru_entry->entry_list.next == lru_entry->entry_list.prev)
		last_entry = true;
	list_del_init(&lru_entry->lru);
	list_del_init(&lru_entry->entry_list);
	if (last_entry)
		kfree(radix_tree_delete(&radix_tree, lru_entry->block));
	kfree(lru_entry);
}

static void add_to_cache(struct cache_entry *entry) {
	if (list_empty(&entry->lru))
		list_add(&entry->lru, &lru_list);
	else 
		list_move(&entry->lru, &lru_list);
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

static int load_data(struct cache_entry *entry, struct bio *bio) {
	int ret;

	while (num_cached_blocks >= CACHE_SIZE) {
		/* must evict something from cache */
		evict_page();
	}
	entry->data = kmalloc(SECTOR_SIZE << LOG_BLOCK_SIZE, GFP_ATOMIC);
	if (! entry->data)
		return -ENOMEM;
	if ((ret = copy_block_to_buf(bio, entry->data, 0, SECTOR_SIZE << LOG_BLOCK_SIZE)))
		/* error */
		return ret;
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

	spin_lock_irqsave(&tree_lock, flags);
	entry = find_entry(block, preq->blkif);
	if (entry && entry->valid) 
		success = !copy_buf_to_block(bio, entry->data, 0, SECTOR_SIZE << LOG_BLOCK_SIZE);
	spin_unlock_irqrestore(&tree_lock, flags);

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

	spin_lock_irqsave(&tree_lock, flags);
	entry = find_entry(block, preq->blkif);
	if (entry)
		load_data(entry, bio);
	spin_unlock_irqrestore(&tree_lock, flags);
}
