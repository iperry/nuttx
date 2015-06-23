#include <stdlib.h>
#include <errno.h>

#include <nuttx/mm/kmem.h>

//#define ALIGN(_x, _align)   (-(-(_x) & -(_align)))
#define ALIGN(_x, _align)   (((_x) + ((_align) - 1)) & ~(_align - 1))

/* global list of caches */
struct list_head cache_list;

struct kmem_cache *kmem_cache_create(char *name, void *base, size_t size,
                                     size_t align,
                                     void (*ctor)(void *context),
                                     void (*dtor)(void *context)) {
    struct kmem_cache *cache;
    void *end;
    cache = malloc(sizeof(*cache));
    if (!cache) {
        return NULL;
    }

    cache->name = name;
    cache->size = size;
    cache->actual_size = ALIGN(size, align);
    cache->refs = 0;
    cache->ctor = ctor ? ctor : NULL;
    cache->dtor = dtor ? dtor : NULL;


    /*
     * start at base, and point the objects till end
     */
    cache->base = base;
    cache->actual_base = ALIGN(base, cache->actual_size);


    list_add(&cache_list, &cache->list);
    return cache;
}

