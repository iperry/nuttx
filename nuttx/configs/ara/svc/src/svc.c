/*
 * Copyright (c) 2015 Google Inc.
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
 */

#define DBG_COMP DBG_SVC

#include <nuttx/config.h>
#include <nuttx/arch.h>
#include <nuttx/util.h>
#include <nuttx/unipro/unipro.h>
#include <nuttx/greybus/unipro.h>
#include <nuttx/greybus/greybus.h>
#include <nuttx/greybus/types.h>

#include <arch/board/board.h>

#include <sys/wait.h>
#include <apps/nsh.h>

#include "string.h"
#include "ara_board.h"
#include "up_debug.h"
#include "interface.h"
#include "svc.h"
#include "vreg.h"
#include "tsb_switch_driver_es2.h"

#define SVCD_PRIORITY      (60)
#define SVCD_STACK_SIZE    (2048)

enum module_state {
    MODULE_STATE_BOOTED,
    MODULE_STATE_PROBED,
};

struct module {
    int is_ap;
    enum module_state state;

};


static struct svc the_svc;
struct svc *svc = &the_svc;

static struct unipro_driver svc_greybus_driver = {
    .name = "svcd-greybus",
    .rx_handler = greybus_rx_handler,
};

static int svc_listen(unsigned int cport) {
    return unipro_driver_register(&svc_greybus_driver, cport);
}

extern void gb_cp_register(int cport);
static struct gb_transport_backend svc_backend = {
    .init = unipro_init,
    .send = unipro_send,
    .listen = svc_listen,
};

extern uint8_t svc_gb_cp_probe_ap(__u8 endo_id, __u8 intf_id);

/* state helpers */
#define svcd_state_running() (svc->state == SVC_STATE_RUNNING)
#define svcd_state_stopped() (svc->state == SVC_STATE_STOPPED)
static inline void svcd_set_state(enum svc_state state){
    svc->state = state;
}

volatile int booted = 0;

/*
 * Static connections table
 *
 * The routing and connections setup is made of two tables:
 * 1) The interface to deviceID mapping table. Every interface is given by
 *    its name (provided in the board file). The deviceID to associate to
 *    the interface is freely chosen.
 *    Cf. the console command 'power' for the interfaces naming.
 *    The physical port to the switch is retrieved from the board file
 *    information, there is no need to supply it in the connection tables.
 *
 * 2) The connections table, given by the deviceID and the CPort to setup, on
 *    both local and remote ends of the link. The routing will be setup in a
 *    bidirectional way.
 *
 * Spring interfaces placement on BDB1B/2A:
 *
 *                             8
 *                            (9)
 *          7     5  6
 *    3  2     4        1
 */
struct svc_interface_device_id {
    char *interface_name;       // Interface name
    uint8_t device_id;          // DeviceID
    uint8_t port_id;            // PortID
    bool found;
};

/*
 * Default routes used on BDB1B demo
 */
#define DEV_ID_APB1              (1)
#define DEV_ID_APB2              (2)
#define DEV_ID_APB3              (3)
#define DEV_ID_GPB1              (4)
#define DEV_ID_GPB2              (5)
#define DEV_ID_SPRING6           (8)
#define DEMO_GPIO_APB1_CPORT     (0)
#define DEMO_GPIO_APB2_CPORT     (5)
#define DEMO_I2C_APB1_CPORT      (1)
#define DEMO_I2C_APB2_CPORT      (4)
#define DEMO_DSI_APB1_CPORT      (16)
#define DEMO_DSI_APB2_CPORT      (16)
#define DEMO_VIBRATOR_APB1_CPORT (2)
#define DEMO_VIBRATOR_APB2_CPORT (3)

/* Interface name to deviceID mapping table */
static struct svc_interface_device_id devid[] = {
    { "apb1", DEV_ID_APB1 },
    { "apb2", DEV_ID_APB2 },
    { "apb3", DEV_ID_APB3 },
    { "gpb1", DEV_ID_GPB1 },
    { "gpb2", DEV_ID_GPB2 },
    { "spring6", DEV_ID_SPRING6 },
};

/* Connections table */
static struct unipro_connection conn[] = {
#if defined(CONFIG_SVC_ROUTE_DEFAULT)
    // APB1, CPort 0 <-> APB2, CPort 5, for GPIO
    {
        .device_id0 = DEV_ID_APB1,
        .cport_id0  = DEMO_GPIO_APB1_CPORT,
        .device_id1 = DEV_ID_APB2,
        .cport_id1  = DEMO_GPIO_APB2_CPORT,
        .flags      = CPORT_FLAGS_CSD_N | CPORT_FLAGS_CSV_N
    },
    // APB1, CPort 1 <-> APB2, CPort 4, for I2C
    {
        .device_id0 = DEV_ID_APB1,
        .cport_id0  = DEMO_I2C_APB1_CPORT,
        .device_id1 = DEV_ID_APB2,
        .cport_id1  = DEMO_I2C_APB2_CPORT,
        .flags      = CPORT_FLAGS_CSD_N | CPORT_FLAGS_CSV_N
    },
    // APB1, CPort 2 <-> APB2, CPort 3, for Vibrator
    {
        .device_id0 = DEV_ID_APB1,
        .cport_id0  = DEMO_VIBRATOR_APB1_CPORT,
        .device_id1 = DEV_ID_APB2,
        .cport_id1  = DEMO_VIBRATOR_APB2_CPORT,
        .flags      = CPORT_FLAGS_CSD_N | CPORT_FLAGS_CSV_N
    },
    // APB1, CPort 16 <-> APB2, CPort 16, for DSI
    {
        .device_id0 = DEV_ID_APB1,
        .cport_id0  = DEMO_DSI_APB1_CPORT,
        .device_id1 = DEV_ID_APB2,
        .cport_id1  = DEMO_DSI_APB2_CPORT,
        .flags      = CPORT_FLAGS_CSD_N | CPORT_FLAGS_CSV_N
    },
#elif defined(CONFIG_SVC_ROUTE_SPRING6_APB2)
    // SPRING6, CPort 0 <-> APB2, CPort 5, for GPIO
    {
        .device_id0 = DEV_ID_SPRING6,
        .cport_id0  = DEMO_GPIO_APB1_CPORT,
        .device_id1 = DEV_ID_APB2,
        .cport_id1  = DEMO_GPIO_APB2_CPORT,
        .flags      = CPORT_FLAGS_CSD_N | CPORT_FLAGS_CSV_N
    },
    // SPRING6, CPort 1 <-> APB2, CPort 4, for I2C
    {
        .device_id0 = DEV_ID_SPRING6,
        .cport_id0  = DEMO_I2C_APB1_CPORT,
        .device_id1 = DEV_ID_APB2,
        .cport_id1  = DEMO_I2C_APB2_CPORT,
        .flags      = CPORT_FLAGS_CSD_N | CPORT_FLAGS_CSV_N
    },
    // SPRING6, CPort 16 <-> APB2, CPort 16, for DSI
    {
        .device_id0 = DEV_ID_SPRING6,
        .cport_id0  = DEMO_DSI_APB1_CPORT,
        .device_id1 = DEV_ID_APB2,
        .cport_id1  = DEMO_DSI_APB2_CPORT,
        .flags      = CPORT_FLAGS_CSD_N | CPORT_FLAGS_CSV_N
    },
#endif
};


static int setup_default_routes(struct tsb_switch *sw) {
    int i, j, rc;
    uint8_t port_id_0, port_id_1;
    bool port_id_0_found, port_id_1_found;
    struct interface *iface;

    /*
     * Setup hard-coded default routes from the routing and
     * connection tables
     */

    /* Setup Port <-> deviceID and configure the Switch routing table */
    for (i = 0; i < ARRAY_SIZE(devid); i++) {
        devid[i].found = false;
        /* Retrieve the portID from the interface name */
        interface_foreach(iface, j) {
            if (!strcmp(iface->name, devid[i].interface_name)) {
                devid[i].port_id = iface->switch_portid;
                devid[i].found = true;

                rc = switch_if_dev_id_set(sw, devid[i].port_id,
                                          devid[i].device_id);
                if (rc) {
                    dbg_error("Failed to assign deviceID %u to interface %s\n",
                              devid[i].device_id, devid[i].interface_name);
                    continue;
                } else {
                    dbg_info("Set deviceID %d to interface %s (portID %d)\n",
                             devid[i].device_id, devid[i].interface_name,
                             devid[i].port_id);
                }
            }
        }
    }

    /* Connections setup */
    for (i = 0; i < ARRAY_SIZE(conn); i++) {
        /* Look up local and peer portIDs for the given deviceIDs */
        port_id_0 = port_id_1 = 0;
        port_id_0_found = port_id_1_found = false;
        for (j = 0; j < ARRAY_SIZE(devid); j++) {
            if (!devid[j].found)
                continue;

            if (devid[j].device_id == conn[i].device_id0) {
                conn[i].port_id0 = port_id_0 = devid[j].port_id;
                port_id_0_found = true;
            }
            if (devid[j].device_id == conn[i].device_id1) {
                conn[i].port_id1 = port_id_1 = devid[j].port_id;
                port_id_1_found = true;
            }
        }

        /* If found, create the requested connection */
        if (port_id_0_found && port_id_1_found) {
            /* Update Switch routing table */
            rc = switch_setup_routing_table(sw,
                                            conn[i].device_id0, port_id_0,
                                            conn[i].device_id1, port_id_1);
            if (rc) {
                dbg_error("Failed to setup routing table [%u:%u]<->[%u:%u]\n",
                          conn[i].device_id0, port_id_0,
                          conn[i].device_id1, port_id_1);
                return -1;
            }

            /* Create connection */
            rc = switch_connection_create(sw, &conn[i]);
            if (rc) {
                dbg_error("Failed to create connection [%u:%u]<->[%u:%u]\n",
                          port_id_0, conn[i].cport_id0,
                          port_id_1, conn[i].cport_id1);
                return -1;
            }
        } else {
            dbg_error("Cannot find portIDs for deviceIDs %d and %d\n",
                      port_id_0, port_id_1);
        }
    }

    switch_dump_routing_table(sw);

    return 0;
}

static struct unipro_connection c = {
    .port_id0 = 14,
    .device_id0 = 0,
    .cport_id0 = 4,

    .port_id1 = 4,
    .device_id1 = 1,
    .cport_id1 = 0,
    .flags = CPORT_FLAGS_E2EFC | CPORT_FLAGS_CSD_N | CPORT_FLAGS_CSV_N,
};

static int check_ap(unsigned int portid) {
    int rc;

    rc = switch_if_dev_id_set(svc->sw, portid, 1);
    if (rc) {
        dbg_error("failed to assign device id\n");
        return rc;
    }

    rc = switch_setup_routing_table(svc->sw, 0, 14, 1, portid);
    if (rc) {
        dbg_error("failed to assign device id\n");
        return rc;
    }

    rc = switch_connection_create(svc->sw, &c);
    if (rc) {
        dbg_error("failed to make connection\n");
        return rc;
    }

    /* turn on e2efc */
    uint32_t spictlb = 0xC;
    if (switch_internal_setattr(svc->sw, SPICTLB, spictlb)) {
        dbg_error("failed to set spictlb\n");
    }

    switch_dump_routing_table(svc->sw);

    svc_gb_cp_probe_ap(0xde, 0xad);
    dbg_info("done probing\n");

//    switch_connection_destroy(svc->sw, &c);
//    spictlb = 0x0;
//    if (switch_internal_setattr(svc->sw, SPICTLB, spictlb)) {
//        dbg_error("failed to set spictlb\n");
//    }
//    dbg_info("done destroying\n");

    /*
     * now tear it down?
     */
    return 0;
}

static int svcd_switch_event_cb(struct tsb_switch_event *event) {
    struct hotplug_event *hp_event;

    pthread_mutex_lock(&svc->lock);
    dbg_info("event received. port: %u val: %u\n", event->mbox.port, event->mbox.val);

    /*
     * check for ap here?
     */
    hp_event = malloc(sizeof *hp_event);
    hp_event->portid = event->mbox.port;
    hp_event->val = event->mbox.val;

    list_add(&svc->hotplug_events, &hp_event->entry);

    pthread_cond_signal(&svc->cv);
    pthread_mutex_unlock(&svc->lock);

    return 0;
}

static int svcd_startup(void) {
    struct ara_board_info *info;
    struct tsb_switch *sw;
    int i, rc;

    /*
     * Board-specific initialization, all boards must define this.
     */
    info = board_init();
    if (!info) {
        dbg_error("%s: No board information provided.\n", __func__);
        goto error0;
    }
    svc->board_info = info;

    /* Init Switch */
    sw = switch_init(&info->sw_data);
    if (!sw) {
        dbg_error("%s: Failed to initialize switch.\n", __func__);
        goto error1;
    }
    svc->sw = sw;

    /* Power on all provided interfaces */
    if (!info->interfaces) {
        dbg_error("%s: No interface information provided\n", __func__);
        goto error2;
    }

    rc = interface_init(info->interfaces,
                        info->nr_interfaces, info->nr_spring_interfaces);
    if (rc < 0) {
        dbg_error("%s: Failed to initialize interfaces\n", __func__);
        goto error2;
    }

//    /* Set up default routes */
//    rc = setup_default_routes(sw);
//    if (rc) {
//        dbg_error("%s: Failed to set default routes\n", __func__);
//    }

    rc = switch_event_register_listener(sw, &svc->evl);

    /* initialize greybus. unipro stack is initialized at the same time. */
    gb_init(&svc_backend);
    gb_cp_register(4);

    /*
     * What now?
     *
     * Start probing all the interfaces until we find the ap.
     *
     * track state in cp
     * wait for bootup events. for each boot up event. send a probe()
     * until we find the ap.
     *
     * who does this?
     * SVC? CP?
     * 
     * 1) Set up CP to AP module
     * 2) Create SVC connection to AP module
     * 3) Send connected() on CP to AP module.
     *
     * Non AP:
     * 1) Send hotplug to AP on SVC conenction
     * 2) AP sends create_connection() request
     * 3) Make connection. E2EFC off?
     * 4) Send connected() to module
     * 5) Send connected() to AP
     */


    /*
     * Enable the switch IRQ
     *
     * Note: the IRQ must be enabled after all NCP commands have been sent
     * for the switch and Unipro devices initialization.
     */
    rc = switch_irq_enable(sw, true);
    if (rc && (rc != -EOPNOTSUPP)) {
        goto error3;
    }

    /* Enable interrupts for all Unipro ports */
    for (i = 0; i < SWITCH_PORT_MAX; i++)
        switch_port_irq_enable(sw, i, true);

    return 0;

error3:
    interface_exit();
error2:
    switch_exit(sw);
    svc->sw = NULL;
error1:
    board_exit();
error0:
    return -1;
}

static int svcd_cleanup(void) {
    interface_exit();

    switch_exit(svc->sw);
    svc->sw = NULL;

    board_exit();
    svc->board_info = NULL;

    return 0;
}

static int svcd_main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    int rc = 0;

    pthread_mutex_lock(&svc->lock);
    rc = svcd_startup();
    if (rc < 0) {
        goto done;
    }

    while (!svc->stop) {
        pthread_cond_wait(&svc->cv, &svc->lock);
        /* check to see if we were told to stop */
        if (svc->stop) {
            dbg_verbose("svc stop requested\n");
            break;
        }

        if (!svc->ap_found) {
            struct hotplug_event *hp;
            struct list_head *node, *next;
            dbg_info("hotpolug event\n");
            /*
             * for each detected interface, send a probe_ap() request.
             *     set a connection on cp0
             *     send probe_ap(), if auth_size == 0, take it down.
             *     if auth_size > 0, set that as the ap.
             */
            list_foreach_safe(&svc->hotplug_events, node, next) {
                hp = list_entry(node, struct hotplug_event, entry);
                dbg_info("hotpolug event: port: %u val: %u\n", hp->portid, hp->val);
                check_ap(hp->portid);
            }
        }
    };

    rc = svcd_cleanup();

done:
    svcd_set_state(SVC_STATE_STOPPED);
    pthread_mutex_unlock(&svc->lock);
    return rc;
}

/*
 * System entrypoint. CONFIG_USER_ENTRYPOINT should point to this function.
 */
int svc_init(int argc, char **argv) {
    int rc;

    dbg_set_config(DBG_SVC | DBG_SWITCH, DBG_INFO);

    svc->sw = NULL;
    svc->board_info = NULL;
    svc->svcd_pid = 0;
    svc->stop = 0;
    pthread_mutex_init(&svc->lock, NULL);
    pthread_cond_init(&svc->cv, NULL);
    svcd_set_state(SVC_STATE_STOPPED);
    list_init(&svc->hotplug_events);

    svc->evl.cb = svcd_switch_event_cb;

    rc = svcd_start();
    if (rc) {
        return rc;
    }

    /*
     * Now start the shell.
     */
    return nsh_main(argc, argv);
}

int svcd_start(void) {
    int rc;

    pthread_mutex_lock(&svc->lock);
    dbg_info("starting svcd\n");
    if (!svcd_state_stopped()) {
        dbg_info("svcd already started\n");
        pthread_mutex_unlock(&svc->lock);
        return -EBUSY;
    }

    rc = task_create("svcd", SVCD_PRIORITY, SVCD_STACK_SIZE, svcd_main, NULL);
    if (rc == ERROR) {
        dbg_error("failed to start svcd\n");
        return rc;
    }
    svc->svcd_pid = rc;

    svc->stop = 0;
    svcd_set_state(SVC_STATE_RUNNING);
    dbg_info("svcd started\n");
    pthread_mutex_unlock(&svc->lock);

    return 0;
}

void svcd_stop(void) {
    int status;
    int rc;
    pid_t pid_tmp;

    pthread_mutex_lock(&svc->lock);
    dbg_verbose("stopping svcd\n");

    pid_tmp = svc->svcd_pid;

    if (!svcd_state_running()) {
        dbg_info("svcd not running\n");
        pthread_mutex_unlock(&svc->lock);
        return;
    }

    /* signal main thread to stop */
    svc->stop = 1;
    pthread_cond_signal(&svc->cv);
    pthread_mutex_unlock(&svc->lock);

    /* wait for the svcd to stop */
    rc = waitpid(pid_tmp, &status, 0);
    if (rc != pid_tmp) {
        dbg_error("failed to stop svcd\n");
    } else {
        dbg_info("svcd stopped\n");
    }
}

