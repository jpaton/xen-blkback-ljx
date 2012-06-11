#ifndef _LABEL_H
#define _LABEL_H

#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/list.h>

typedef enum {
	SUPERBLOCK,
	BOOTBLOCK,
	INODE,
	GROUP_DESC,
	JOURNAL,
	DATA,
	UNLABELED
} label_t;

struct label {
	struct list_head	list;
	sector_t		sector;
	unsigned int		nr_sec;
	label_t			label;
};

static inline struct label *new_label(sector_t sector, unsigned int nr_sec, label_t label) {
	struct label *ret = kzalloc(sizeof(struct label), GFP_KERNEL);
	INIT_LIST_HEAD(&ret->list);
	ret->sector	= sector;
	ret->nr_sec	= nr_sec;
	ret->label	= label;
	return ret;
}

static inline void insert_label(
		struct list_head *head, 
		sector_t sector,
		unsigned int size,
		label_t label
) {
	struct list_head *pos;
	struct label *cur, *next, *new;
	int start, end;

	new = new_label(sector, size, label);

	list_for_each(pos, head) {
		cur = list_entry(pos, struct label, list);
		next = list_entry(pos->next, struct label, list);
		start = cur->sector;
		end = cur->sector + cur->nr_sec;
		//if (start >= cur->sector && start <= next->sector) {
			
	}
}

#endif
