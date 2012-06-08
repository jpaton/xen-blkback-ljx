#ifndef _BOOT_H
#define _BOOT_H

#include "ljx.h"

#define CODE_SIZE 440 /* size of MBR code area in bytes */

/* MBR on disk */

#define MBR_SIGNATURE	0x55AA	/* note: this is little-endian */

struct chs_address {
	__u8	head; /* CHS head */
	union {
		__u8 sector; /* bits 5-0 are sector number */
		__u8 hi_cylinder; /* bits 7-6 are high bits (9-8) of cylinder */
	};
	__u8	lo_cylinder;	/* bits 7-0 of cylinder */
};

struct bootblock {
	__le32		disk_signature; 	/* ??? */
	__le16		usually_null;		/* why? */
	struct {				/* primary partition entry */
		__u8	status; 		/* 0x80 = bootable, 0x00 = non-bootable, other = invalid */
		struct chs_address start_chs; 	/* CHS address of first absolute sector in partition */
		__u8	partition_type; 	/* partition type */
		struct chs_address end_chs; 	/* CHS address of last absolute sector in partition */
		__le32	lba_start; 		/* LBA of first absolute sector in partition */
		__le32	sectors;		/* number of sectors in partition */
	} partition[4];
	__le16		signature;		/* MBR signature */
};

/* MBR in memory */

/* first, partition types for MBR */
enum partition_type {
	EMPTY_PT 	= 0x00,
	ext_chs		= 0x05,
	ntfs		= 0x07,
	coherent_swap	= 0x09,
	ext_lba		= 0x0F,
	swap		= 0x82,
};

extern inline char *decode_partition(char type) {
	switch (type) {
		case 0x00:
			return "empty";
		case 0x05:
			return "ext_chs";
		case 0x07:
			return "ntfs";
		case 0x09:
			return "coherent_swap";
		case 0x0F:
			return "ext_lba";
		case 0x82:
			return "swap";
		default:
			return "unknown";
	}
}

struct partition_record {
	char	status;		/* 0x80 = bootable, 0x00 = non-bootable, other = invalid */ 
	char	type;		/* partition type */
	int	sector;		/* starting sector */
	int	size;		/* size in sector */
};

struct ljx_bootblock {
	struct partition_record	partition[4];
};

extern bool valid_boot_block(struct bio *, char *);
extern int fill_boot_block(void **, void *, int);

#endif
