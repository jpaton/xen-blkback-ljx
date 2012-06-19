#include <linux/highmem.h>
#include <linux/radix-tree.h>

static RADIX_TREE_ROOT(radix_tree);
static struct spin_lock tree_lock;

/**
 * checks whether bio can be satisfied by cache. If so, returns a pointer to the
 * cache copy of the block.
 **/
extern char *find_get_page(struct xen_vbd *vbd, struct bio *bio) {
	return NULL;
}

/**
 * possibly stores a completed bio in cache
 **/
extern void put_page(struct xen_vbd *vbd, struct bio *bio) {
}
