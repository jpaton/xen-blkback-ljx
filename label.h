#ifndef _LABEL_H
#define _LABEL_H

#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/list.h>

#include "common.h"

typedef enum {
	SUPERBLOCK,
	BOOTBLOCK,
	INODE_BLOCK,
	GROUP_DESC,
	JOURNAL,
	DATA,
	UNLABELED
} label_t;

struct label;

typedef int (process_bio_fn) (struct bio *, struct xen_vbd *, struct label *);

struct label {
	struct list_head	list;
	sector_t		sector;
	unsigned int		nr_sec;
	label_t			label;
	process_bio_fn		*processor;
};

static inline struct label *new_label(sector_t sector, unsigned int nr_sec, label_t label) {
	struct label *ret = kzalloc(sizeof(struct label), GFP_KERNEL);
	if (! ret)
		return NULL;
	INIT_LIST_HEAD(&ret->list);
	ret->sector	= sector;
	ret->nr_sec	= nr_sec;
	ret->label	= label;
	return ret;
}

static inline void merge(
		struct list_head *new,
		struct list_head *head
) {
	struct label *next, *prev, *cur;
	bool moretodo;

	cur = list_entry(new, struct label, list);

	do {
		if (new->next == head)
			/* this is at the end of the list */
			goto end;
		next = list_entry(new->next, struct label, list);
		moretodo = false;
		if (next->sector <= cur->sector + cur->nr_sec) {
			/* there is something after us */
			if (next->label == cur->label) {
				cur->nr_sec = next->sector - cur->sector + next->nr_sec;
				list_del(&next->list); 
				kfree(next);
				moretodo = true;
			}
			else
				next->sector = cur->sector + cur->nr_sec;
		}
	} while (moretodo);
end:
	do {
		if (new->prev == head)
			/* this is at the beginning of the list */
			return;
		prev = list_entry(new->prev, struct label, list);
		moretodo = false;
		if (prev->sector + prev->nr_sec >= cur->sector) {
			/* there is something before us */
			if (prev->label == cur->label) {
				cur->sector = prev->sector;
				list_del(&prev->list); 
				kfree(prev);
				moretodo = true;
			}
			else
				prev->nr_sec = cur->sector - prev->sector;
		}
	} while (moretodo);
}

static inline struct label *insert_label(
		struct list_head *head, 
		sector_t sector,
		unsigned int size,
		label_t label
) {
	struct list_head *pos;
	struct label *cur, *next, *new;
	int start, end;

	new = new_label(sector, size, label);
	if (! new)
		return NULL;

	list_for_each(pos, head) {
		cur = list_entry(pos, struct label, list);
		next = list_entry(pos->next, struct label, list);
		start = cur->sector;
		end = cur->sector + cur->nr_sec;
		if (start >= cur->sector && start <= next->sector) {
			break;
		}
	}

	list_add(&new->list, pos);
	merge(&new->list, head);
	return new;
}

static inline void print_label_list(struct list_head *head) {
	struct list_head *pos;
	struct label *label;

	JPRINTK("Label list:");
	list_for_each(pos, head) {
		label = list_entry(pos, struct label, list);
		JPRINTK("\tsector: %lu, size: %u, label: %d",
				(unsigned long) label->sector, label->nr_sec, label->label);
	}
}

static inline unsigned int find_labels(
		sector_t sector, 
		unsigned int nr_sec, 
		struct label **label,
		struct list_head *head
) {
	struct list_head *pos;
	struct label *cur, *first_label = NULL;
	unsigned int num;

	JPRINTK("searching %u - %u", (unsigned int) sector, (unsigned int) sector + nr_sec - 1);

	num = 0;
	list_for_each(pos, head) {
		cur = list_entry(pos, struct label, list);
		if (first_label != NULL) {
			JPRINTK("found another label");
			/* already found the first relevant label */
			if (sector + nr_sec <= cur->sector)
				break;
			num++;
		}
		else if (sector >= cur->sector && sector < cur->sector + cur->nr_sec) {
			JPRINTK("found first label");
			first_label = cur;
			num++;
		}
	}

	JPRINTK("returning %u", num);
	*label = first_label;
	return num;
}

extern void superblock_label(struct xen_vbd *);

#endif
