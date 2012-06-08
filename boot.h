#ifndef _BOOT_H
#define _BOOT_H

#include "ljx.h"

struct ljx_bootblock {
};

extern bool valid_boot_block(struct bio *, char *);
extern int fill_boot_block(void **, void *, int);

#endif
