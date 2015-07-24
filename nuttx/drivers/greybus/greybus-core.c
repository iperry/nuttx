/*
 * Copyright (c) 2014-2015 Google Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 * 1. Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 * 3. Neither the name of the copyright holder nor the names of its
 * contributors may be used to endorse or promote products derived from this
 * software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * Author: Fabien Parent <fparent@baylibre.com>
 */

#include <nuttx/config.h>
#include <nuttx/list.h>
#include <nuttx/unipro/unipro.h>
#include <nuttx/greybus/greybus.h>
#include <nuttx/greybus/debug.h>

#include <arch/atomic.h>
#include <arch/byteorder.h>

#include <stdio.h>
#include <string.h>
#include <errno.h>

#define DEFAULT_STACK_SIZE      2048
#define TYPE_RESPONSE_FLAG      0x80
#define TIMEOUT_IN_MS           1000
#define GB_INVALID_TYPE         0

#define ONE_SEC_IN_MSEC         1000
#define ONE_MSEC_IN_NSEC        1000000

struct gb_cport_driver {
    struct gb_driver *driver;
    struct list_head tx_fifo;
    struct list_head rx_fifo;
    sem_t rx_fifo_lock;
    pthread_t thread;
};

static atomic_t request_id;
static struct gb_cport_driver *g_cport;
static struct gb_transport_backend *transport_backend;

uint8_t gb_errno_to_op_result(int err)
{
    switch (err) {
    case 0:
        return GB_OP_SUCCESS;

    case ENOMEM:
    case -ENOMEM:
        return GB_OP_NO_MEMORY;

    case EINTR:
    case -EINTR:
        return GB_OP_INTERRUPTED;

    case ETIMEDOUT:
    case -ETIMEDOUT:
        return GB_OP_TIMEOUT;

    case EPROTO:
    case -EPROTO:
    case ENOSYS:
    case -ENOSYS:
        return GB_OP_PROTOCOL_BAD;

    case EINVAL:
    case -EINVAL:
        return GB_OP_INVALID;

    case EOVERFLOW:
    case -EOVERFLOW:
        return GB_OP_OVERFLOW;

    case ENODEV:
    case -ENODEV:
    case ENXIO:
    case -ENXIO:
        return GB_OP_NONEXISTENT;

    case EBUSY:
    case -EBUSY:
        return GB_OP_RETRY;

    default:
        return GB_OP_UNKNOWN_ERROR;
    }
}

static int gb_compare_handlers(const void *data1, const void *data2)
{
    const struct gb_operation_handler *handler1 = data1;
    const struct gb_operation_handler *handler2 = data2;
    return (int)handler1->type - (int)handler2->type;
}

static struct gb_operation_handler *find_operation_handler(uint8_t type,
                                                           unsigned int cport)
{
    struct gb_driver *driver = g_cport[cport].driver;
    int l,r;

    if (type == GB_INVALID_TYPE || !driver->op_handlers) {
        return NULL;
    }

    /* binary search -- bsearch from libc is not implemented by nuttx */
    l = 0;
    r = driver->op_handlers_count - 1;
    while (l <= r) {
        int m = (l + r) / 2;
        if (driver->op_handlers[m].type < type)
            l = m + 1;
        else if (driver->op_handlers[m].type > type)
            r = m - 1;
        else
            return &driver->op_handlers[m];
    }

    return NULL;
}

static void gb_process_request(struct gb_operation_hdr *hdr,
                               struct gb_operation *operation)
{
    struct gb_operation_handler *op_handler;
    uint8_t result;

    op_handler = find_operation_handler(hdr->type, operation->cport);
    if (!op_handler) {
        gb_operation_send_response(operation, GB_OP_INVALID);
        return;
    }

    result = op_handler->handler(operation);
    if (hdr->id)
        gb_operation_send_response(operation, result);
}

static bool gb_operation_has_timedout(struct gb_operation *operation)
{
    struct timespec current_time;
    struct timespec timeout_time;

    timeout_time.tv_sec = operation->time.tv_sec +
                          TIMEOUT_IN_MS / ONE_SEC_IN_MSEC;
    timeout_time.tv_nsec = operation->time.tv_nsec +
                          (TIMEOUT_IN_MS % ONE_SEC_IN_MSEC) * ONE_MSEC_IN_NSEC;
    clock_gettime(CLOCK_REALTIME, &current_time);

    if (current_time.tv_sec > timeout_time.tv_sec)
        return true;

    if (current_time.tv_sec < timeout_time.tv_sec)
        return false;

    return current_time.tv_nsec > timeout_time.tv_nsec;
}

static void gb_process_response(struct gb_operation_hdr *hdr,
                                struct gb_operation *operation)
{
    irqstate_t flags;
    struct list_head *iter, *iter_next;
    struct gb_operation *op;
    struct gb_operation_hdr *op_hdr;
    struct gb_operation_hdr timedout_hdr = {
        .size = sizeof(timedout_hdr),
        .result = GB_OP_TIMEOUT,
    };

    list_foreach_safe(&g_cport[operation->cport].tx_fifo, iter, iter_next) {
        op = list_entry(iter, struct gb_operation, list);
        op_hdr = op->request_buffer;

        // Destroy all the operation that have timedout
        if (gb_operation_has_timedout(op)) {
            flags = irqsave();
            list_del(iter);
            irqrestore(flags);

            if (op->callback) {
                timedout_hdr.id = op_hdr->id;
                timedout_hdr.type = TYPE_RESPONSE_FLAG | op_hdr->type;

                op->response_buffer = &timedout_hdr;
                op->callback(op);
                op->response_buffer = NULL;
            }
            gb_operation_unref(op);
            continue;
        }

        if (hdr->id != op_hdr->id)
            continue;

        flags = irqsave();
        list_del(iter);
        irqrestore(flags);

        /* attach this response with the original request */
        gb_operation_ref(operation);
        op->response = operation;
        if (op->callback)
            op->callback(op);
        gb_operation_unref(op);
        break;
    }
}

static void *gb_pending_message_worker(void *data)
{
    const int cportid = (int) data;
    irqstate_t flags;
    struct gb_operation *operation;
    struct list_head *head;
    struct gb_operation_hdr *hdr;
    int retval;

    while (1) {
        retval = sem_wait(&g_cport[cportid].rx_fifo_lock);
        if (retval < 0)
            continue;

        flags = irqsave();
        head = g_cport[cportid].rx_fifo.next;
        list_del(g_cport[cportid].rx_fifo.next);
        irqrestore(flags);

        operation = list_entry(head, struct gb_operation, list);
        hdr = operation->request_buffer;

        if (hdr->type & TYPE_RESPONSE_FLAG)
            gb_process_response(hdr, operation);
        else
            gb_process_request(hdr, operation);
        gb_operation_destroy(operation);
    }

    return NULL;
}

int greybus_rx_handler(unsigned int cport, void *data, size_t size)
{
    irqstate_t flags;
    struct gb_operation *op;
    struct gb_operation_hdr *hdr = data;
    struct gb_operation_handler *op_handler;
    size_t hdr_size;

    if (cport >= unipro_cport_count() || !data)
        return -EINVAL;

    if (!g_cport[cport].driver || !g_cport[cport].driver->op_handlers)
        return 0;

    if (sizeof(*hdr) > size) {
        return -EINVAL; /* Dropping garbage request */
    }

    hdr_size = le16_to_cpu(hdr->size);

    if (hdr_size > size || sizeof(*hdr) > hdr_size) {
        return -EINVAL; /* Dropping garbage request */
    }

    gb_dump(data, size);

    op_handler = find_operation_handler(hdr->type, cport);
    if (op_handler && op_handler->fast_handler) {
        op_handler->fast_handler(cport, data);
        return 0;
    }

    op = gb_operation_create(cport, 0, hdr_size - sizeof(*hdr));
    if (!op)
        return -ENOMEM;

    memcpy(op->request_buffer, data, hdr_size);

    flags = irqsave(); // useless if IRQ's priorities are correct
    list_add(&g_cport[cport].rx_fifo, &op->list);
    sem_post(&g_cport[cport].rx_fifo_lock);
    irqrestore(flags);

    return 0;
}

int gb_register_driver(unsigned int cport, struct gb_driver *driver)
{
    pthread_attr_t thread_attr;
    pthread_attr_t *thread_attr_ptr = &thread_attr;
    int retval;

    DEBUGASSERT(transport_backend);
    DEBUGASSERT(transport_backend->listen);

    if (cport >= unipro_cport_count() || !driver)
        return -EINVAL;

    if (g_cport[cport].driver)
        return -EEXIST;

    if (!driver->op_handlers && driver->op_handlers_count > 0)
        return -EINVAL;

    if (driver->init) {
        retval = driver->init(cport);
        if (retval)
            return retval;
    }

    if (driver->op_handlers) {
        qsort(driver->op_handlers, driver->op_handlers_count,
              sizeof(*driver->op_handlers), gb_compare_handlers);
    }

    if (!driver->stack_size)
        driver->stack_size = DEFAULT_STACK_SIZE;

    retval = pthread_attr_init(&thread_attr);
    if (retval)
        goto pthread_attr_init_error;

    retval = pthread_attr_setstacksize(&thread_attr, driver->stack_size);
    if (retval)
        goto pthread_attr_setstacksize_error;

    retval = pthread_create(&g_cport[cport].thread, &thread_attr,
                            gb_pending_message_worker, (unsigned*) cport);
    if (retval)
        goto pthread_create_error;

    pthread_attr_destroy(&thread_attr);
    thread_attr_ptr = NULL;

    g_cport[cport].driver = driver;

    return 0;

pthread_create_error:
pthread_attr_setstacksize_error:
    if (thread_attr_ptr != NULL)
        pthread_attr_destroy(&thread_attr);
pthread_attr_init_error:
    if (driver->exit)
        driver->exit(cport);
    return retval;
}

int gb_listen(unsigned int cport)
{
    DEBUGASSERT(transport_backend);
    DEBUGASSERT(transport_backend->listen);

    if (cport >= unipro_cport_count() || !g_cport[cport].driver) {
        return -EINVAL;
    }

    return transport_backend->listen(cport);
}

int gb_stop_listening(unsigned int cport)
{
    DEBUGASSERT(transport_backend);
    DEBUGASSERT(transport_backend->stop_listening);

    if (cport >= unipro_cport_count() || !g_cport[cport].driver) {
        return -EINVAL;
    }

    return transport_backend->stop_listening(cport);
}

int gb_operation_send_request(struct gb_operation *operation,
                              gb_operation_callback callback,
                              bool need_response)
{
    struct gb_operation_hdr *hdr = operation->request_buffer;
    int retval = 0;
    irqstate_t flags;

    DEBUGASSERT(operation);
    DEBUGASSERT(transport_backend);
    DEBUGASSERT(transport_backend->send);

    hdr->id = 0;

    flags = irqsave();

    if (need_response) {
        hdr->id = cpu_to_le16(atomic_inc(&request_id));
        if (hdr->id == 0) /* ID 0 is for request with no response */
            hdr->id = cpu_to_le16(atomic_inc(&request_id));
        clock_gettime(CLOCK_REALTIME, &operation->time);
        operation->callback = callback;
        gb_operation_ref(operation);
        list_add(&g_cport[operation->cport].tx_fifo, &operation->list);
    }

    gb_dump(operation->request_buffer, hdr->size);
    retval = transport_backend->send(operation->cport,
                                     operation->request_buffer,
                                     le16_to_cpu(hdr->size));
    if (need_response && retval) {
        list_del(&operation->list);
        gb_operation_unref(operation);
    }

    irqrestore(flags);

    return retval;
}

static void gb_operation_callback_sync(struct gb_operation *operation)
{
    sem_post(&operation->sync_sem);
}

int gb_operation_send_request_sync(struct gb_operation *operation)
{
    int retval;

    sem_init(&operation->sync_sem, 0, 0);

    retval =
        gb_operation_send_request(operation, gb_operation_callback_sync, true);
    if (retval)
        return retval;

    do {
        retval = sem_wait(&operation->sync_sem);
    } while (retval < 0 && errno == EINTR);

    return retval;
}

int gb_operation_send_response(struct gb_operation *operation, uint8_t result)
{
    struct gb_operation_hdr *resp_hdr;
    int retval;
    bool has_allocated_response = false;

    DEBUGASSERT(operation);
    DEBUGASSERT(transport_backend);
    DEBUGASSERT(transport_backend->send);

    if (operation->has_responded)
        return -EINVAL;

    if (!operation->response_buffer) {
        gb_operation_alloc_response(operation, 0);
        if (!operation->response_buffer)
            return -ENOMEM;

        has_allocated_response = true;
    }

    resp_hdr = operation->response_buffer;
    resp_hdr->result = result;

    gb_dump(operation->response_buffer, resp_hdr->size);
    retval = transport_backend->send(operation->cport,
                                     operation->response_buffer,
                                     le16_to_cpu(resp_hdr->size));
    if (retval) {
        if (has_allocated_response) {
            free(operation->response_buffer);
            operation->response_buffer = NULL;
        }
        return retval;
    }

    operation->has_responded = true;
    return retval;
}

void *gb_operation_alloc_response(struct gb_operation *operation, size_t size)
{
    struct gb_operation_hdr *req_hdr;
    struct gb_operation_hdr *resp_hdr;

    DEBUGASSERT(operation);

    operation->response_buffer = malloc(size + sizeof(*resp_hdr));
    if (!operation->response_buffer)
        return NULL;

    memset(operation->response_buffer, 0, size + sizeof(*resp_hdr));

    req_hdr = operation->request_buffer;
    resp_hdr = operation->response_buffer;

    resp_hdr->size = cpu_to_le16(size + sizeof(*resp_hdr));
    resp_hdr->id = req_hdr->id;
    resp_hdr->type = TYPE_RESPONSE_FLAG | req_hdr->type;
    return gb_operation_get_response_payload(operation);
}

void gb_operation_destroy(struct gb_operation *operation)
{
    DEBUGASSERT(operation);
    gb_operation_unref(operation);
}

void gb_operation_ref(struct gb_operation *operation)
{
    DEBUGASSERT(operation);
    DEBUGASSERT(atomic_get(&operation->ref_count) > 0);
    atomic_inc(&operation->ref_count);
}

void gb_operation_unref(struct gb_operation *operation)
{
    DEBUGASSERT(operation);
    DEBUGASSERT(atomic_get(&operation->ref_count) > 0);

    uint32_t ref_count = atomic_dec(&operation->ref_count);
    if (ref_count != 0) {
        return;
    }

    free(operation->request_buffer);
    free(operation->response_buffer);
    if (operation->response) {
        gb_operation_unref(operation->response);
    }
    free(operation);
}


struct gb_operation *gb_operation_create(unsigned int cport, uint8_t type,
                                         uint32_t req_size)
{
    struct gb_operation *operation;
    struct gb_operation_hdr *hdr;

    if (cport >= unipro_cport_count())
        return NULL;

    operation = malloc(sizeof(*operation));
    if (!operation)
        return NULL;

    memset(operation, 0, sizeof(*operation));
    operation->cport = cport;

    operation->request_buffer = malloc(req_size + sizeof(*hdr));
    if (!operation->request_buffer)
        goto malloc_error;

    memset(operation->request_buffer, 0, req_size + sizeof(*hdr));
    hdr = operation->request_buffer;
    hdr->size = cpu_to_le16(req_size + sizeof(*hdr));
    hdr->type = type;

    list_init(&operation->list);
    atomic_init(&operation->ref_count, 1);

    return operation;
malloc_error:
    free(operation);
    return NULL;
}

size_t gb_operation_get_request_payload_size(struct gb_operation *operation)
{
    struct gb_operation_hdr *hdr;

    if (!operation || !operation->request_buffer) {
        return 0;
    }

    hdr = operation->request_buffer;
    if (le16_to_cpu(hdr->size) < sizeof(*hdr)) {
        return 0;
    }

    return le16_to_cpu(hdr->size) - sizeof(*hdr);
}

int gb_init(struct gb_transport_backend *transport)
{
    size_t size = sizeof(*g_cport) * unipro_cport_count();
    int i;

    if (!transport)
        return -EINVAL;

    g_cport = zalloc(size);
    for (i = 0; i < unipro_cport_count(); i++) {
        sem_init(&g_cport[i].rx_fifo_lock, 0, 0);
        list_init(&g_cport[i].rx_fifo);
        list_init(&g_cport[i].tx_fifo);
    }

    atomic_init(&request_id, (uint32_t) 0);

    transport_backend = transport;
    transport_backend->init();

    return 0;
}
