/*
 * =====================================================================================
 *
 *       Filename:  ap.c
 *
 *    Description:  
 *
 *        Version:  1.0
 *        Created:  06/15/2015 03:08:39 AM
 *       Revision:  none
 *       Compiler:  gcc
 *
 *         Author:  YOUR NAME (), 
 *   Organization:  
 *
 * =====================================================================================
 */
#include <stdlib.h>

static struct gb_driver gb_ap_drv = {
    .init = gb_ap_init,
    .op_handlers = gb_ap_handlers,
    .op_counters_count = ARRAY_SIZE(gb_ap_handlers),
};

