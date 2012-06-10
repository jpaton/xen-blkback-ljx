#ifndef _UTIL_H
#define _UTIL_H

#include <linux/fs.h>
#include <linux/bio.h>

extern int copy_block(struct bio *, char *);

#endif
