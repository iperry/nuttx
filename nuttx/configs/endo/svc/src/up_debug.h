/****************************************************************************
 * configs/endo/svc/src/up_debug.h
 * SVC support for debug print messages:
 * - allows to control the dump of the data, per component and with
 *   a debug level,
 * - dumps data out to an externally exposed interface (e.g. UART).
 *
 * Copyright (C) 2014 Google, Inc.
 * Google Confidential/Restricted
 *
 ****************************************************************************/
#ifndef __CONFIGS_ENDO_INCLUDE_UP_DEBUG_H
#define __CONFIGS_ENDO_INCLUDE_UP_DEBUG_H

#include <debug.h>
#include <stdio.h>
#include <stdlib.h>

#define printk(format, ...)     syslog(format, ##__VA_ARGS__)


/*
 * Debug levels, from the low level HW access up to application messages.
 * The dbg_pr function dumps messages of the configured debug level and
 * also the messages of higher levels.
 */
enum {
    DBG_INSANE = 0,     /* Insanely verbose */
    DBG_VERBOSE,        /* Low level: registers access etc. */
    DBG_INFO,           /* Informational */
    DBG_WARN,           /* Warning level */
    DBG_ERROR,          /* Critical level */
    DBG_UI,             /* High level (user interaction with the shell etc.) */
    DBG_MAX = DBG_UI    /* Always enabled */
};

/*
 * Components to enable debug for, encoded as a bitmask.
 * Up to 32 components using a uint32_t in the comp field.
 */
enum {
    DBG_COMM            = (1 << 0),
    DBG_DBG             = (1 << 1),
    DBG_EPM             = (1 << 2),
    DBG_GPIO            = (1 << 3),
    DBG_GREYBUS         = (1 << 4),
    DBG_HOTPLUG         = (1 << 5),
    DBG_I2C             = (1 << 6),
    DBG_IOEXP           = (1 << 7),
    DBG_NETWORK         = (1 << 8),
    DBG_POWER           = (1 << 9),
    DBG_SVC             = (1 << 10),
    DBG_SWITCH          = (1 << 11),
    DBG_ALL             = 0xFFFFFFFF,
};

#define DBG_RESCUE_SIZE 64

/* Debug control internal struct */
typedef struct {
    uint32_t comp;      /* Bitmask for components to enable */
    uint32_t lvl;       /* debug level */
} dbg_ctrl_t;

extern dbg_ctrl_t dbg_ctrl;

/* Debug buffer, dynamically allocated/freed */
extern char *dbg_msg;
/* Small statically allocated buffer, use as a rescue in case of OOM */
extern char dbg_rescue[DBG_RESCUE_SIZE];

/* Debug helper macros */
#define dbg_insane(fmt, ...)    dbg_pr(DBG_INSANE, fmt, ##__VA_ARGS__)
#define dbg_verbose(fmt, ...)   dbg_pr(DBG_VERBOSE, fmt, ##__VA_ARGS__)
#define dbg_info(fmt, ...)      dbg_pr(DBG_INFO, fmt, ##__VA_ARGS__)
#define dbg_warn(fmt, ...)      dbg_pr(DBG_WARN, fmt, ##__VA_ARGS__)
#define dbg_error(fmt, ...)     dbg_pr(DBG_ERROR, fmt, ##__VA_ARGS__)
#define dbg_ui(fmt, ...)        dbg_pr(DBG_UI, fmt, ##__VA_ARGS__)

/* Macro value stringify magic */
#define xstr(s)	str(s)
#define str(s)	#s

/*
 * Debug dump function, to be used by components code.
 * Every caller has to define its own DBG_COMP macro.
 */
#define dbg_pr(level, fmt, ...)                                     \
    do {                                                            \
        if ((dbg_ctrl.comp & DBG_COMP) &&                           \
            (level >= dbg_ctrl.lvl)) {                              \
            if (asprintf(&dbg_msg, "[%s] " fmt, xstr(DBG_COMP),     \
                         ##__VA_ARGS__) > 0) {                      \
                printk(dbg_msg);                                    \
                free(dbg_msg);                                      \
            } else {                                                \
                snprintf(dbg_rescue, DBG_RESCUE_SIZE, "OOM: %s\n",  \
                         __func__);                                 \
                printk(dbg_rescue);                                 \
            }                                                       \
        }                                                           \
    } while (0)

/* Pretty print of an uint8_t buffer */
static inline void dbg_print_buf(uint32_t level, uint8_t *buf, uint32_t size)
{
    uint32_t i, idx;
    int ret;
    uint8_t *p;
    char msg[64];

    /* If level not active, do nothing */
    if (level < dbg_ctrl.lvl)
        return;

    /* Print 16-byte lines */
    for (i = 0; i < (size & 0xfffffff0); i += 16) {
        p = buf + i;
        dbg_pr(level, "%04x: %02x %02x %02x %02x %02x %02x %02x %02x | "
                      "%02x %02x %02x %02x %02x %02x %02x %02x\n",
               i, p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7],
               p[8], p[9], p[10], p[11], p[12], p[13], p[14], p[15]);
    }

    if (i >= size)
        return;

    /* Print the remaining of the buffer (< 16 bytes) */
    idx = 0;
    /*  fill the output buffer with the index */
    ret = sprintf(msg, "%04x: ", i);
    if (ret <= 0)
        return;
    else
        idx += ret;
    /*  fill the output buffer with the remaining bytes */
    for (; i < size; i++) {
        /*  fill the output buffer with a separator before the 8th uint8_t */
        if ((i & 0xf) == 8) {
            ret = sprintf(msg + idx, "| ");
            if (ret <= 0)
                return;
            else
                idx += ret;
        }
        /*  fill the output buffer with the next uint8_t */
        p = buf + i;
        ret = sprintf(msg + idx, "%02x ", *p);
        if (ret <= 0)
            return;
        else
            idx += ret;
    }

    /*  fill the output buffer with the final return char */
    ret = sprintf(msg + idx, "\n");
    if (ret <= 0)
        return;
    else
        idx += ret;

    /* Output the string */
    dbg_pr(level, "%s", msg);

    return;
}

extern void dbg_get_config(uint32_t *comp, uint32_t *level);
extern int dbg_set_config(uint32_t comp, uint32_t level);
extern void debug_rgb_led(uint8_t r, uint8_t g, uint8_t b);

#endif // __CONFIGS_ENDO_INCLUDE_UP_DEBUG_H
