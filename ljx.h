#ifndef _LJX_H
#define _LJX_H

#include <linux/module.h> 
#include <linux/kernel.h>
#include <linux/proc_fs.h>
#include <linux/fs.h>
#include <linux/jbd.h>
#include <linux/capability.h>
#include <linux/ext3_fs.h>

#include "ext3.h"
#include "boot.h"

#define procfs_name "blkback-ljx"

extern struct proc_dir_entry *proc_file;

extern void ljx_init(void);

#endif
