/**
 * @brief SVC implementation of unipro stack
 */

#define DBG_COMP DBG_SVC

#include <errno.h>
#include <stddef.h>

#include <nuttx/config.h>
#include <arch/board/board.h>
#include <nuttx/unipro/unipro.h>

#include "up_debug.h"
#include "svc.h"

/* List of registered drivers */
static struct unipro_driver **g_drvs;
unsigned int unipro_cport_count(void) {
    return 5;
}

void unipro_init(void) {
    size_t size = sizeof(struct unipro_driver*) * unipro_cport_count();
    g_drvs = zalloc(size);
    if (!g_drvs) {
        return;
    }
}

int unipro_send(unsigned int cportid, const void *buf, size_t len) {
    struct tsb_switch *sw = svc->sw;
    struct ubuf ub;

    if (!sw) {
        return -ENODEV;
    }

    dbg_info("%s: sending %u bytes\n", __func__, len);

    ub.cportid = cportid;
    ub.data = (unsigned char*)buf;
    ub.size = len;

    switch_data_send(sw, &ub);

    return 0;
}


int unipro_driver_register(struct unipro_driver *drv, unsigned int cportid) {
    lldbg("Registering driver %s on cport: %u\n", drv->name, cportid);
    /*
     * Only cports 4 and 5 are supported on ES2 silicon
     */
    switch (cportid) {
    case 4:
    case 5:
        break;
    default:
        return -EINVAL;
    }

    if (g_drvs[cportid]) {
        return -EADDRINUSE;
    }

    g_drvs[cportid] = drv;
    lldbg("Registered driver %s on cport: %u\n", drv->name, cportid);

    return 0;
}

/*
 * new packet came in from an interface, dispatch it to the right handler?
 */
void unipro_if_rx(struct ubuf *ub) {
    struct unipro_driver *drv;

    drv = g_drvs[ub->cportid];
    if (!drv) {
        return;
    }

    drv->rx_handler(ub);
}
