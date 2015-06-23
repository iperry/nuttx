#ifndef _UBUF_H_
#define _UBUF_H_

#include <stdlib.h>
#include <nuttx/list.h>

/**
 * raw unipro buffer structure
 */
struct ubuf {
    struct ubuf *next;
    unsigned int cportid;
    size_t size;

    unsigned char *head;
    unsigned char *data;
    unsigned char *tail;
    unsigned char *end;
};

struct ubuf *ubuf_alloc(unsigned int size);
void ubuf_release(struct ubuf *ub);

#endif
