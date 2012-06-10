#include "util.h"

/**
 * Copies bio's data into buf. Buf had better be large enough!
 * Starts from byte start_offset and does size bytes.
 */
extern int copy_block(struct bio *bio, char *buf, size_t start_offset, size_t size) {
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
			if (byte_idx >= start_offset)
				*(bufPtr++) = data[intra_idx];
		kunmap_atomic(data);
	}

	return 0;
}
