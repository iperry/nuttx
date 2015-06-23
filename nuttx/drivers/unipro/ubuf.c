#include <stdarg.h>
#include <assert.h>
#include <errno.h>
#include <nuttx/unipro/unipro.h>

#define MAX_UBUF_SIZE       (2048)

#define ALIGNED(_v, _p)    ((((unsigned int)_v) & ((_p) - 1)) == 0)

/*
 * break bufram up into two pools:
 * x number of 512 byte buffers
 * y number of MTU size buffers
 */
struct ubuf_pool {
    struct ubuf *next;
    size_t elem_size;
    size_t align;
    void *base;
    void *top;
} pool;

/*
 * Break bufram up into 
 */

int ubuf_pool_init(void *base, void *top, size_t elem_size, size_t align) {
    struct ubuf *ub;
    size_t nr_elements = (top - base)/elem_size;
    unsigned int i;

    ASSERT(ALIGNED(base, 4));

    pool.next = base;
    pool.base = base;
    pool.top = top;
    pool.elem_size = elem_size;
    pool.align = align;

    if (elem_size > MAX_UBUF_SIZE) {
        return -EINVAL;
    }

    /* alignment must be a power of two */
    if (align & (align - 1)) {
        return -EINVAL;
    }

    for (i = 0; i < nr_elements; i++) {

    }


    return 0;
}

/**
 * @brief allocate a ubuf from a memory pool
 */
struct ubuf *ubuf_alloc(unsigned int size) {
    struct ubuf *ub = (struct ubuf*)malloc(sizeof(struct ubuf));
    if (!ub) {
        return NULL;
    }

    ub->size = size;
    ub->data = malloc(size);
    if (!ub->data) {
        free(ub);
        return NULL;
    }

    return ub;
}

void ubuf_release(struct ubuf *ub) {
    if (ub->data) {
        free(ub->data);
    }

    free(ub);
}


