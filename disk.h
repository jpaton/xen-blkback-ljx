#ifndef _DISK_H
#define _DISK_H

/**
 * Returns true of bio includes a valid boot block
 */
extern bool valid_boot_block(struct bio *, char *);

#endif
