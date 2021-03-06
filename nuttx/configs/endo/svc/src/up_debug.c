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
#define DBG_COMP DBG_DBG    /* DBG_COMP macro of the component */
#include <arch/board/board.h>

#include "up_debug.h"
#include "up_internal.h"

/* Debug control internal data */
dbg_ctrl_t dbg_ctrl = { DBG_ALL, DBG_INFO };
/* Debug buffer, dynamically allocated/freed */
char *dbg_msg;
char dbg_rescue[DBG_RESCUE_SIZE];


/* Get the level and components to enable debug for */
void dbg_get_config(uint32_t *comp, uint32_t *level)
{
    *comp = dbg_ctrl.comp;
    *level = dbg_ctrl.lvl;

    dbg_info("%s(): debug comp=0x%x, level=%d\n", __func__,
           dbg_ctrl.comp, dbg_ctrl.lvl);
}

/* Configure the level and components to enable debug for */
int dbg_set_config(uint32_t comp, uint32_t level)
{
    /* DBG_MAX is always enabled */
    if (level > DBG_MAX)
        level = DBG_MAX;

    dbg_ctrl.comp = comp;
    dbg_ctrl.lvl = level;

    dbg_info("%s(): debug comp=0x%x, level=%d\n", __func__,
           dbg_ctrl.comp, dbg_ctrl.lvl);

    return 0;
}

/* Control RGB LED */
void debug_rgb_led(uint8_t r, uint8_t g, uint8_t b)
{
    dbg_verbose("%s(): rgb=%d%d%d\n", __func__, r, g, b);
    stm32_gpiowrite(GPIO_R_LED_EN, !r);
    stm32_gpiowrite(GPIO_G_LED_EN, !g);
    stm32_gpiowrite(GPIO_B_LED_EN, !b);
}
