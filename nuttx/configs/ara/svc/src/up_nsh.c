/****************************************************************************
 * config/stm3240g_eval/src/up_nsh.c
 * arch/arm/src/board/up_nsh.c
 *
 *   Copyright (C) 2012 Gregory Nutt. All rights reserved.
 *   Author: Gregory Nutt <gnutt@nuttx.org>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 3. Neither the name NuttX nor the names of its contributors may be
 *    used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
 * OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/
#define DBG_COMP DBG_SVC     /* DBG_COMP macro of the component */

#include <nuttx/config.h>
#include <nuttx/util.h>

#include <stdbool.h>
#include <stdio.h>
#include <syslog.h>
#include <errno.h>

#include "up_debug.h"
#include "svc.h"
#include "tsb_switch.h"
#include "svc.h"

#include "stm32.h"

/****************************************************************************
 * Name: nsh_archinitialize
 *
 * Description:
 *   Perform architecture specific initialization
 *
 ****************************************************************************/

#define RETRY_TIME_S    (5)

// 90.8512ms 13500 ish

unsigned int boot_delay = 0;

int failed = 0;
#define TRIGGER_GPIO (GPIO_OUTPUT | GPIO_PUSHPULL | GPIO_OUTPUT_CLEAR | \
                      GPIO_PORTH | GPIO_PIN7)
#define INTERFACE_GPIO (GPIO_OUTPUT | GPIO_PUSHPULL | GPIO_OUTPUT_CLEAR | \
                      GPIO_PORTH | GPIO_PIN8)

extern int ara_svc_main(int argc, char *argv[]);
extern const char* attr_get_name(uint16_t attr);

int nsh_archinitialize(void)
{
#if 0
    const uint32_t regs[] = {
        0xd000,
        0xd010,
        0xd020,
        0xd061,
        0xd083,
        0xd095
    };

    const uint32_t delays[] = {0, 14000};

    uint32_t i, j, v;
    int rc;
    struct tsb_switch *sw;

    for (boot_delay = 0; boot_delay < 500000; boot_delay+= 500) {
        printf("\nUsing delay: %u...", boot_delay);
        if (svc_init()) {
            printf("FAIL");
        } else {
            printf("OK");
            return OK;
        }
//        sw = ara_svc->sw;
//        for (j = 0; j < ARRAY_SIZE(regs); j++) {
//            rc = switch_dme_get(sw, 1, regs[j], 0, &v);
//            if (!rc) {
//                dbg_info("[%x] [%s]: 0x%x\n", regs[j], attr_get_name(regs[j]), v);
//            } else  {
//                dbg_error("failed to read attribute %x: rc: %u\n", regs[j], rc);
//            }
//        }
        svc_exit();
    }
#endif

	return OK;
}
