/*
 * Copyright (c) 2015 Google, Inc.
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
 * * may be used to endorse or promote products derived from this
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

#include <nuttx/config.h>

#include <errno.h>
#include <debug.h>

#include <nuttx/arch.h>
#include <nuttx/wdog.h>
#include <nuttx/i2c.h>

#include <arch/irq.h>

#include "up_arch.h"

#include "chip.h"
#include "tsb_scm.h"
#include "tsb_i2c.h"


/* disable the verbose debug output */
#undef lldbg
#define lldbg(x...)

static struct i2c_dev_s g_dev;        /* Generic Nuttx I2C device */
static struct i2c_msg_s *g_msgs;      /* Generic messages array */
static sem_t            g_mutex;      /* Only one thread can access at a time */
static sem_t            g_wait;       /* Wait for state machine completion */
static WDOG_ID          g_timeout;    /* Watchdog to timeout when bus hung */

static unsigned int     g_tx_index;
static unsigned int     g_tx_length;
static uint8_t          *g_tx_buffer;

static unsigned int     g_rx_index;
static unsigned int     g_rx_length;
static uint8_t          *g_rx_buffer;

static unsigned int     g_msgs_count;
static int              g_cmd_err;
static int              g_msg_err;
static unsigned int     g_status;
static uint32_t         g_abort_source;
static unsigned int     g_rx_outstanding;


/* I2C controller configuration */
#define TSB_I2C_CONFIG (TSB_I2C_CON_RESTART_EN | \
                        TSB_I2C_CON_MASTER | \
                        TSB_I2C_CON_SLAVE_DISABLE | \
                        TSB_I2C_CON_SPEED_FAST)

#define TSB_I2C_TX_FIFO_DEPTH   8
#define TSB_I2C_RX_FIFO_DEPTH   8

/* IRQs handle by the driver */
#define TSB_I2C_INTR_DEFAULT_MASK (TSB_I2C_INTR_RX_FULL | \
                                   TSB_I2C_INTR_TX_EMPTY | \
                                   TSB_I2C_INTR_TX_ABRT | \
                                   TSB_I2C_INTR_STOP_DET)

#define TIMEOUT                     20              /* 20 ms */
#define TSB_I2C_TIMEOUT  ((1000 * CLK_TCK) / 1000)  /* 1000 ms */


static int i2c_interrupt(int irq, void *context);
static void i2c_timeout(int argc, uint32_t arg, ...);


static uint32_t i2c_read(int offset)
{
    return getreg32(I2C_BASE + offset);
}

static void i2c_write(int offset, uint32_t b)
{
    putreg32(b, I2C_BASE + offset);
}

static void tsb_i2c_clear_int(void)
{
    i2c_read(TSB_I2C_CLR_INTR);
}

static void tsb_i2c_disable_int(void)
{
    i2c_write(TSB_I2C_INTR_MASK, 0);
}

static void i2c_set_enable(int enable)
{
    int i;

    for (i = 0; i < 50; i++) {
        i2c_write(TSB_I2C_ENABLE, enable);

        if ((i2c_read(TSB_I2C_ENABLE_STATUS) & 0x1) == enable)
            return;

        usleep(25);
    }

    lldbg("timeout!");
}

/* Enable the controller */
static void tsb_i2c_enable(void)
{
    i2c_set_enable(1);
}

/* Disable the controller */
static void tsb_i2c_disable(void)
{
    i2c_set_enable(0);

    tsb_i2c_disable_int();
    tsb_i2c_clear_int();
}


/**
 * Initialize the TSB I2 controller
 */
static void tsb_i2c_init(void)
{
    /* Disable the adapter */
    tsb_i2c_disable();

    /* Set timings for Standard and Fast Speed mode */
    i2c_write(TSB_I2C_SS_SCL_HCNT, 28);
    i2c_write(TSB_I2C_SS_SCL_LCNT, 52);
    i2c_write(TSB_I2C_FS_SCL_HCNT, 47);
    i2c_write(TSB_I2C_FS_SCL_LCNT, 65);

    /* Configure Tx/Rx FIFO threshold levels */
    i2c_write(TSB_I2C_TX_TL, TSB_I2C_TX_FIFO_DEPTH - 1);
    i2c_write(TSB_I2C_RX_TL, 0);

    /* configure the i2c master */
    i2c_write(TSB_I2C_CON, TSB_I2C_CONFIG);
}

static int tsb_i2c_wait_bus_ready(void)
{
    int timeout = TIMEOUT;

    while (i2c_read(TSB_I2C_STATUS) & TSB_I2C_STATUS_ACTIVITY) {
        if (timeout <= 0) {
            lldbg("timeout\n");
            return -ETIMEDOUT;
        }
        timeout--;
        usleep(1000);
    }

    return 0;
}

static void tsb_i2c_start_transfer(void)
{
    lldbg("\n");

    /* Disable the adapter */
    tsb_i2c_disable();

    /* write target address */
    i2c_write(TSB_I2C_TAR, g_msgs[g_tx_index].addr);

    /* Disable the interrupts */
    tsb_i2c_disable_int();

    /* Enable the adapter */
    tsb_i2c_enable();

    /* Clear interrupts */
    tsb_i2c_clear_int();

    /* Enable interrupts */
    i2c_write(TSB_I2C_INTR_MASK, TSB_I2C_INTR_DEFAULT_MASK);
}

/**
 * Internal function that handles the read or write transfer
 * It is called from the IRQ handler.
 */
static void tsb_i2c_transfer_msg(void)
{
    uint32_t intr_mask;
    uint32_t addr = g_msgs[g_tx_index].addr;
    uint8_t *buffer = g_tx_buffer;
    uint32_t length = g_tx_length;

    bool need_restart = false;

    int tx_avail;
    int rx_avail;

    lldbg("tx_index %d\n", g_tx_index);

    /* loop over the i2c message array */
    for (; g_tx_index < g_msgs_count; g_tx_index++) {

        if (g_msgs[g_tx_index].addr != addr) {
            lldbg("invalid target address\n");
            g_msg_err = -EINVAL;
            break;
        }

        if (g_msgs[g_tx_index].length == 0) {
            lldbg("invalid message length\n");
            g_msg_err = -EINVAL;
            break;
        }

        if (!(g_status & TSB_I2C_STATUS_WRITE_IN_PROGRESS)) {
            /* init a new msg transfer */
            buffer = g_msgs[g_tx_index].buffer;
            length = g_msgs[g_tx_index].length;

            /* force a restart between messages */
            if (g_tx_index > 0)
                need_restart = true;
        }

        /* Get the amount of free space in the internal buffer */
        tx_avail = TSB_I2C_TX_FIFO_DEPTH - i2c_read(TSB_I2C_TXFLR);
        rx_avail = TSB_I2C_RX_FIFO_DEPTH - i2c_read(TSB_I2C_RXFLR);

        /* loop until one of the fifo is full or buffer is consumed */
        while (length > 0 && tx_avail > 0 && rx_avail > 0) {
            uint32_t cmd = 0;

            if (g_tx_index == g_msgs_count - 1 && length == 1) {
                /* Last msg, issue a STOP */
                cmd |= (1 << 9);
                lldbg("STOP\n");
            }

            if (need_restart) {
                cmd |= (1 << 10); /* RESTART */
                need_restart = false;
                lldbg("RESTART\n");
            }

            if (g_msgs[g_tx_index].flags & I2C_M_READ) {
                if (rx_avail - g_rx_outstanding <= 0)
                    break;

                i2c_write(TSB_I2C_DATA_CMD, cmd | 1 << 8); /* READ */
                lldbg("READ\n");

                rx_avail--;
                g_rx_outstanding++;
            } else {
                i2c_write(TSB_I2C_DATA_CMD, cmd | *buffer++);
                lldbg("WRITE\n");
            }

            tx_avail--;
            length--;
        }

        g_tx_buffer = buffer;
        g_tx_length = length;

        if (length > 0) {
            g_status |= TSB_I2C_STATUS_WRITE_IN_PROGRESS;
            break;
        } else {
            g_status &= ~TSB_I2C_STATUS_WRITE_IN_PROGRESS;
        }
    }

    intr_mask = TSB_I2C_INTR_DEFAULT_MASK;

    /* No more data to write. Stop the TX IRQ */
    if (g_tx_index == g_msgs_count)
        intr_mask &= ~TSB_I2C_INTR_TX_EMPTY;

    /* In case of error, mask all the IRQs */
    if (g_msg_err)
        intr_mask = 0;

    i2c_write(TSB_I2C_INTR_MASK, intr_mask);
}

static void tsb_i2c_read(void)
{
    int rx_valid;

    lldbg("rx_index %d\n", g_rx_index);

    for (; g_rx_index < g_msgs_count; g_rx_index++) {
        uint32_t len;
        uint8_t *buffer;

        if (!(g_msgs[g_rx_index].flags & I2C_M_READ))
            continue;

        if (!(g_status & TSB_I2C_STATUS_READ_IN_PROGRESS)) {
            len = g_msgs[g_rx_index].length;
            buffer = g_msgs[g_rx_index].buffer;
        } else {
            len = g_rx_length;
            buffer = g_rx_buffer;
        }

        rx_valid = i2c_read(TSB_I2C_RXFLR);

        for (; len > 0 && rx_valid > 0; len--, rx_valid--) {
            *buffer++ = i2c_read(TSB_I2C_DATA_CMD);
            g_rx_outstanding--;
        }

        if (len > 0) {
            /* start the read process */
            g_status |= TSB_I2C_STATUS_READ_IN_PROGRESS;
            g_rx_length = len;
            g_rx_buffer = buffer;

            return;
        } else {
            g_status &= ~TSB_I2C_STATUS_READ_IN_PROGRESS;
        }
    }
}

static int tsb_i2c_handle_tx_abort(void)
{
    unsigned long abort_source = g_abort_source;

    lldbg("%s: 0x%x\n", __func__, abort_source);

    if (abort_source & TSB_I2C_TX_ABRT_NOACK) {
        lldbg("%s: TSB_I2C_TX_ABRT_NOACK 0x%x\n", __func__, abort_source);
        return -EREMOTEIO;
    }

    if (abort_source & TSB_I2C_TX_ARB_LOST)
        return -EAGAIN;
    else if (abort_source & TSB_I2C_TX_ABRT_GCALL_READ)
        return -EINVAL; /* wrong g_msgs[] data */
    else
        return -EIO;
}

/* Perform a sequence of I2C transfers */
static int up_i2c_transfer(struct i2c_dev_s *idev, struct i2c_msg_s *msgs, int num)
{
    int ret;

    lldbg("msgs: %d\n", num);

    sem_wait(&g_mutex);

    g_msgs = msgs;
    g_msgs_count = num;
    g_tx_index = 0;
    g_rx_index = 0;
    g_rx_outstanding = 0;

    g_cmd_err = 0;
    g_msg_err = 0;
    g_status = TSB_I2C_STATUS_IDLE;
    g_abort_source = 0;

    ret = tsb_i2c_wait_bus_ready();
    if (ret < 0)
        goto done;

    /*
     * start a watchdog to timeout the transfer if
     * the bus is locked up...
     */
    wd_start(g_timeout, TSB_I2C_TIMEOUT, i2c_timeout, 1, 0);

    /* start the transfers */
    tsb_i2c_start_transfer();

    sem_wait(&g_wait);

    wd_cancel(g_timeout);

    if (g_status == TSB_I2C_STATUS_TIMEOUT) {
        lldbg("controller timed out\n");

        /* Re-init the adapter */
        tsb_i2c_init();
        ret = -ETIMEDOUT;
        goto done;
    }

    tsb_i2c_disable();

    if (g_msg_err) {
        ret = g_msg_err;
        lldbg("error msg_err %x\n", g_msg_err);
        goto done;
    }

    if (!g_cmd_err) {
        ret = 0;
        lldbg("no error %d\n", num);
        goto done;
    }

    /* Handle abort errors */
    if (g_cmd_err == TSB_I2C_ERR_TX_ABRT) {
        ret = tsb_i2c_handle_tx_abort();
        goto done;
    }

    /* default error code */
    ret = -EIO;
    lldbg("unknown error %x\n", ret);

done:
    sem_post(&g_mutex);

    return ret;
}

static uint32_t tsb_i2c_read_clear_intrbits(void)
{
    uint32_t stat = i2c_read(TSB_I2C_INTR_STAT);

    if (stat & TSB_I2C_INTR_RX_UNDER)
        i2c_read(TSB_I2C_CLR_RX_UNDER);
    if (stat & TSB_I2C_INTR_RX_OVER)
        i2c_read(TSB_I2C_CLR_RX_OVER);
    if (stat & TSB_I2C_INTR_TX_OVER)
        i2c_read(TSB_I2C_CLR_TX_OVER);
    if (stat & TSB_I2C_INTR_RD_REQ)
        i2c_read(TSB_I2C_CLR_RD_REQ);
    if (stat & TSB_I2C_INTR_TX_ABRT) {
        /* IC_TX_ABRT_SOURCE reg is cleared upon read, store it */
        g_abort_source = i2c_read(TSB_I2C_TX_ABRT_SOURCE);
        i2c_read(TSB_I2C_CLR_TX_ABRT);
    }
    if (stat & TSB_I2C_INTR_RX_DONE)
        i2c_read(TSB_I2C_CLR_RX_DONE);
    if (stat & TSB_I2C_INTR_ACTIVITY)
        i2c_read(TSB_I2C_CLR_ACTIVITY);
    if (stat & TSB_I2C_INTR_STOP_DET)
        i2c_read(TSB_I2C_CLR_STOP_DET);
    if (stat & TSB_I2C_INTR_START_DET)
        i2c_read(TSB_I2C_CLR_START_DET);
    if (stat & TSB_I2C_INTR_GEN_CALL)
        i2c_read(TSB_I2C_CLR_GEN_CALL);

    return stat;
}

/* I2C interrupt service routine */
static int i2c_interrupt(int irq, void *context)
{
    uint32_t stat, enabled;

    enabled = i2c_read(TSB_I2C_ENABLE);
    stat = i2c_read(TSB_I2C_RAW_INTR_STAT);

    lldbg("enabled=0x%x stat=0x%x\n", enabled, stat);

    if (!enabled || !(stat & ~TSB_I2C_INTR_ACTIVITY))
        return -1;

    stat = tsb_i2c_read_clear_intrbits();

    if (stat & TSB_I2C_INTR_TX_ABRT) {
        lldbg("abort\n");
        g_cmd_err |= TSB_I2C_ERR_TX_ABRT;
        g_status = TSB_I2C_STATUS_IDLE;

        tsb_i2c_disable_int();
        goto tx_aborted;
    }

    if (stat & TSB_I2C_INTR_RX_FULL)
        tsb_i2c_read();

    if (stat & TSB_I2C_INTR_TX_EMPTY)
        tsb_i2c_transfer_msg();

tx_aborted:
    if (stat & TSB_I2C_INTR_TX_ABRT)
        lldbg("aborted %x %x\n", stat, g_abort_source);

    if ((stat & (TSB_I2C_INTR_TX_ABRT | TSB_I2C_INTR_STOP_DET)) || g_msg_err) {
        lldbg("release sem\n");
        sem_post(&g_wait);
    }

    return 0;
}

/**
 * Watchdog handler for timeout of I2C operation
 */
static void i2c_timeout(int argc, uint32_t arg, ...)
{
    lldbg("\n");

    irqstate_t flags = irqsave();

    if (g_status != TSB_I2C_STATUS_IDLE)
    {
        lldbg("finished\n");
        /* Mark the transfer as finished */
        g_status = TSB_I2C_STATUS_TIMEOUT;
        sem_post(&g_wait);
    }

    irqrestore(flags);
}

struct i2c_ops_s dev_i2c_ops;

/**
 * Initialise an I2C device
 */
struct i2c_dev_s *up_i2cinitialize(int port)
{
    lldbg("I2C port %d\n", port);

    /* Only one I2C port on TSB */
    if (port > 0)
        return NULL;

    sem_init(&g_mutex, 0, 1);
    sem_init(&g_wait, 0, 0);

    /* enable I2C pins */
#if defined(CONFIG_TSB_CHIP_REV_ES2)
    tsb_clr_pinshare(TSB_PIN_GPIO21);
    tsb_clr_pinshare(TSB_PIN_GPIO22);
#endif

    /* enable I2C clocks */
    tsb_clk_enable(TSB_CLK_I2CP);
    tsb_clk_enable(TSB_CLK_I2CS);

    /* reset I2C module */
    tsb_reset(TSB_RST_I2CP);
    tsb_reset(TSB_RST_I2CS);

    /* Initialize the I2C controller */
    tsb_i2c_init();

    /* Allocate a watchdog timer */
    g_timeout = wd_create();
    DEBUGASSERT(g_timeout != 0);

    /* Attach Interrupt Handler */
    irq_attach(TSB_IRQ_I2C, i2c_interrupt);

    /* Enable Interrupt Handler */
    up_enable_irq(TSB_IRQ_I2C);

    /* Install our operations */
    g_dev.ops = &dev_i2c_ops;

    return &g_dev;
}

/**
 * Uninitialise an I2C device
 */
int up_i2cuninitialize(struct i2c_dev_s *dev)
{
    lldbg("\n");

    /* Detach Interrupt Handler */
    irq_detach(TSB_IRQ_I2C);

    wd_delete(g_timeout);

    return 0;
}

/**
 * Set the I2C bus frequency (not implemented yet)
 */
static uint32_t up_i2c_setfrequency(struct i2c_dev_s *dev, uint32_t frequency)
{
    lldbg("%d\n", frequency);

    return frequency;
}

/**
 * Set the I2C slave address for a subsequent read/write
 */
static int up_i2c_setaddress(struct i2c_dev_s *dev, int addr, int nbits)
{
    return -ENOSYS;
}

/**
 * Send a block of data on I2C using the previously selected I2C
 * frequency and slave address.
 */
static int up_i2c_write(struct i2c_dev_s *dev, const uint8_t *buffer, int buflen)
{
    return -ENOSYS;
}

/**
 * Receive a block of data on I2C using the previously selected I2C
 * frequency and slave address.
 */
static int up_i2c_read(struct i2c_dev_s *dev, uint8_t *buffer, int buflen)
{
    return -ENOSYS;
}

/**
 * Nuttx i2c device operations structure
 */
struct i2c_ops_s dev_i2c_ops = {
    .setfrequency = up_i2c_setfrequency,
    .setaddress   = up_i2c_setaddress,
    .write        = up_i2c_write,
    .read         = up_i2c_read,
#ifdef CONFIG_I2C_TRANSFER
    .transfer     = up_i2c_transfer
#endif
};
