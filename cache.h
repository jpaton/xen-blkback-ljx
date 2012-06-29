#ifndef _CACHE_H
#define _CACHE_H

extern void *fetch_page(struct xen_vbd *, struct bio *);

extern void store_page(struct xen_vbd *, struct bio *);

extern void invalidate(struct bio *);

#endif
