#ifndef _KMEM_H_
#define _KMEM_H_

#include <nuttx/list.h>

struct kmem_cache {
    struct list_head list;        /* List of all slab caches on the system */
    unsigned int size;            /* The original size of the object */
    unsigned int actual_size;     /* The aligned/padded/added on size  */
    unsigned int align;           /* Alignment as calculated */
    const char *name;             /* Slab name for sysfs */
    int refs;                     /* Use counter */
    void (*ctor)(void *);         /* Called on object slot creation */
    void (*dtor)(void *);         /* Called on object slot destruction? */

    void *base;             // original base address
    void *actual_base;      // aligned base address
};

struct kmem_cache *kmem_cache_create(char *name, void *base, size_t size,
                                     size_t align,
                                     void (*)(void *),
                                     void (*)(void *));

#endif
