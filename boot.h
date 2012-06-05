#ifndef _BOOT_H
#define _BOOT_H

#include "ljx.h"

extern bool valid_boot_block(struct bio *, void *);
extern int fill_boot_block(void **, void *, int);

#endif
