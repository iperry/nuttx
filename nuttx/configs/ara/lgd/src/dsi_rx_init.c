#include <stdint.h>
#include <stdlib.h>
#include <pthread.h>

#include <nuttx/gpio.h>
#include <arch/tsb/cdsi.h>
#include <arch/board/cdsi0_offs_def.h>

#define CDSIRX_CLKEN_VAL                                0x00000001
#define CDSIRX_CLKSEL_VAL                               0x00000101
#define CDSIRX_MODE_CONFIG_VAL                          0x00000000
#define CDSIRX_LANE_ENABLE_VAL                          0x00000014
#define CDSIRX_VC_ENABLE_VAL                            0x0000000F
#define CDSIRX_LINE_INIT_COUNT_VAL                      0x000012C0
#define CDSIRX_HSRXTO_COUNT_VAL                         0xFFFFFFFF
#define CDSIRX_FUNC_ENABLE_VAL                          0x00070701
#define CDSIRX_DSI_LPTX_MODE_VAL                        0x00000001
#define CDSIRX_DSI_TATO_COUNT_VAL                       0xFFFFFFFF
#define CDSIRX_DSI_LPTXTO_COUNT_VAL                     0xFFFFFFFF
#define CDSIRX_FUNC_MODE_VAL                            0x00000000
#define CDSIRX_PPI_HSRX_CNTRL_VAL                       0x40000000
#define CDSIRX_PPI_HSRX_COUNT_VAL                       0x0400000C
#define CDSIRX_PPI_DPHY_DLYCNTRL_VAL                    0x00000000
#define CDSIRX_PPI_DPHY_LPRX_THSLD_VAL                  0x000002AA
#define CDSIRX_PPI_DPHY_LPTXTIMECNT_VAL                 0x00000FFF
#define CDSIRX_PPI_DSI_BTA_COUNT_VAL                    0x000407FF
#define CDSIRX_PPI_DSI_DPHYTX_ADJUST_VAL                0x00000002
#define CDSIRX_PPI_DPHY_HSRX_ADJUST_VAL                 0x000002AA
#define CDSIRX_PPI_DPHY_LPRXCALCNTRL_VAL                0x00190040
#define CDSIRX_LPRX_STATE_INT_MASK_VAL                  0x1F1F1F1D

#define AL_RX_BRG_MODE_VAL                              0x00000001
#define AL_RX_BRG_CSI_INFO_VAL                          0x00000000
#define AL_RX_BRG_CSI_DT0_VAL                           0x00000000
#define AL_RX_BRG_CSI_DT1_VAL                           0x00000000
#define AL_RX_BRG_CSI_DT2_VAL                           0x00000000
#define AL_RX_BRG_CSI_DT3_VAL                           0x00000000

static pthread_t g_display_thread;

void press_powerkey(void)
{
    gpio_direction_out(0, 1);
}

void depress_powerkey(void)
{
    gpio_direction_out(0, 0);
}

void lg4892_dsi_init(struct cdsi_dev *dev)
{
    uint32_t rdata0;
    uint32_t rdata1;

    cdsi_write(dev, CDSI0_AL_RX_BRG_MODE_OFFS, AL_RX_BRG_MODE_VAL);
    cdsi_write(dev, CDSI0_AL_RX_BRG_CSI_INFO_OFFS, AL_RX_BRG_CSI_INFO_VAL);
    cdsi_write(dev, CDSI0_AL_RX_BRG_CSI_DT0_OFFS, AL_RX_BRG_CSI_DT0_VAL);
    cdsi_write(dev, CDSI0_AL_RX_BRG_CSI_DT1_OFFS, AL_RX_BRG_CSI_DT1_VAL);
    cdsi_write(dev, CDSI0_AL_RX_BRG_CSI_DT2_OFFS, AL_RX_BRG_CSI_DT2_VAL);
    cdsi_write(dev, CDSI0_AL_RX_BRG_CSI_DT3_OFFS, AL_RX_BRG_CSI_DT3_VAL);

    cdsi_write(dev, CDSI0_CDSIRX_CLKEN_OFFS, CDSIRX_CLKEN_VAL);
    cdsi_write(dev, CDSI0_CDSIRX_FUNC_ENABLE_OFFS, CDSIRX_FUNC_ENABLE_VAL);
    cdsi_write(dev, CDSI0_CDSIRX_PPI_DPHY_LPRXCALCNTRL_OFFS,
               CDSIRX_PPI_DPHY_LPRXCALCNTRL_VAL);
    cdsi_write(dev, CDSI0_CDSIRX_PPI_DPHY_LPRX_THSLD_OFFS, 0x000002AA);
    cdsi_write(dev, CDSI0_CDSIRX_PPI_DPHY_LPRXAUTOCALST_OFFS, 0x00000001);
    cdsi_write(dev, CDSI0_CDSIRX_CLKSEL_OFFS, 0x00000001);
    cdsi_write(dev, CDSI0_CDSIRX_MODE_CONFIG_OFFS, 0x00000000);
    cdsi_write(dev, CDSI0_CDSIRX_LANE_ENABLE_OFFS, CDSIRX_LANE_ENABLE_VAL);
    cdsi_write(dev, CDSI0_CDSIRX_VC_ENABLE_OFFS, CDSIRX_VC_ENABLE_VAL);
    cdsi_write(dev, CDSI0_CDSIRX_LINE_INIT_COUNT_OFFS,
               CDSIRX_LINE_INIT_COUNT_VAL);
    cdsi_write(dev, CDSI0_CDSIRX_HSRXTO_COUNT_OFFS, CDSIRX_HSRXTO_COUNT_VAL);
    cdsi_write(dev, CDSI0_CDSIRX_FUNC_MODE_OFFS, CDSIRX_FUNC_MODE_VAL);
    cdsi_write(dev, CDSI0_CDSIRX_PPI_DPHY_LPTXTIMECNT_OFFS,
               CDSIRX_PPI_DPHY_LPTXTIMECNT_VAL);
    cdsi_write(dev, CDSI0_CDSIRX_PPI_DSI_BTA_COUNT_OFFS,
               CDSIRX_PPI_DSI_BTA_COUNT_VAL);
    cdsi_write(dev, CDSI0_CDSIRX_PPI_DSI_DPHYTX_ADJUST_OFFS,
               CDSIRX_PPI_DSI_DPHYTX_ADJUST_VAL);
    cdsi_write(dev, CDSI0_CDSIRX_PPI_HSRX_CNTRL_OFFS,
               CDSIRX_PPI_HSRX_CNTRL_VAL);
    cdsi_write(dev, CDSI0_CDSIRX_PPI_HSRX_COUNT_OFFS, 0x04000009);
    cdsi_write(dev, CDSI0_CDSIRX_PPI_DPHY_POWERCNTRL_OFFS, 0x00000003);
    cdsi_write(dev, CDSI0_CDSIRX_PPI_DPHY_HSRX_ADJUST_OFFS,
               CDSIRX_PPI_DPHY_HSRX_ADJUST_VAL);
    cdsi_write(dev, CDSI0_CDSIRX_PPI_DPHY_DLYCNTRL_OFFS,
               CDSIRX_PPI_DPHY_DLYCNTRL_VAL);
    cdsi_write(dev, CDSI0_CDSIRX_LPRX_STATE_INT_MASK_OFFS,
               CDSIRX_LPRX_STATE_INT_MASK_VAL);
    cdsi_write(dev, CDSI0_CDSIRX_DSI_RXTRIG_INT_MASK_OFFS, 0x00000000);
    cdsi_write(dev, CDSI0_CDSIRX_RXERR_INT_MASK_OFFS, 0x00000000);
    cdsi_write(dev, CDSI0_CDSIRX_TXERR_INT_MASK_OFFS, 0x00000000);
    cdsi_write(dev, CDSI0_CDSIRX_DSI_VC0_SH_INT_MASK_OFFS, 0x00000000);
    cdsi_write(dev, CDSI0_CDSIRX_DSI_VC1_SH_INT_MASK_OFFS, 0x00000000);
    cdsi_write(dev, CDSI0_CDSIRX_DSI_VC2_SH_INT_MASK_OFFS, 0x00000000);
    cdsi_write(dev, CDSI0_CDSIRX_DSI_VC3_SH_INT_MASK_OFFS, 0x00000000);
    cdsi_write(dev, CDSI0_CDSIRX_DSI_VC0_LN_INT_MASK_OFFS, 0x00000000);
    cdsi_write(dev, CDSI0_CDSIRX_DSI_VC1_LN_INT_MASK_OFFS, 0x00000000);
    cdsi_write(dev, CDSI0_CDSIRX_DSI_VC2_LN_INT_MASK_OFFS, 0x00000000);
    cdsi_write(dev, CDSI0_CDSIRX_DSI_VC3_LN_INT_MASK_OFFS, 0x00000000);
    cdsi_write(dev, CDSI0_CDSIRX_CSI2_VC0_SH_INT_MASK_OFFS, 0x00000000);
    cdsi_write(dev, CDSI0_CDSIRX_CSI2_VC1_SH_INT_MASK_OFFS, 0x00000000);
    cdsi_write(dev, CDSI0_CDSIRX_CSI2_VC2_SH_INT_MASK_OFFS, 0x00000000);
    cdsi_write(dev, CDSI0_CDSIRX_CSI2_VC3_SH_INT_MASK_OFFS, 0x00000000);
    cdsi_write(dev, CDSI0_CDSIRX_CSI2_VC0_LN0_INT_MASK_OFFS, 0x00000000);
    cdsi_write(dev, CDSI0_CDSIRX_CSI2_VC0_LN1_INT_MASK_OFFS, 0x00000000);
    cdsi_write(dev, CDSI0_CDSIRX_CSI2_VC1_LN0_INT_MASK_OFFS, 0x00000000);
    cdsi_write(dev, CDSI0_CDSIRX_CSI2_VC1_LN1_INT_MASK_OFFS, 0x00000000);
    cdsi_write(dev, CDSI0_CDSIRX_CSI2_VC2_LN0_INT_MASK_OFFS, 0x00000000);
    cdsi_write(dev, CDSI0_CDSIRX_CSI2_VC2_LN1_INT_MASK_OFFS, 0x00000000);
    cdsi_write(dev, CDSI0_CDSIRX_CSI2_VC3_LN0_INT_MASK_OFFS, 0x00000000);
    cdsi_write(dev, CDSI0_CDSIRX_CSI2_VC3_LN1_INT_MASK_OFFS, 0x00000000);
    cdsi_write(dev, CDSI0_CDSIRX_DSI_LPTXTO_COUNT_OFFS,
               CDSIRX_DSI_LPTXTO_COUNT_VAL);
    cdsi_write(dev, CDSI0_CDSIRX_DSI_TATO_COUNT_OFFS,
               CDSIRX_DSI_TATO_COUNT_VAL);
    cdsi_write(dev, CDSI0_CDSIRX_DSI_WAITBTA_COUNT_OFFS, 0x10000010);

    rdata0 = cdsi_read(dev, CDSI0_CDSIRX_LPRX_STATE_INT_STAT_OFFS);
    while ((rdata0 & 0x00000010) == 0x00000000) {
        rdata0 = cdsi_read(dev, CDSI0_CDSIRX_LPRX_STATE_INT_STAT_OFFS);
    }
    printf("First LPRX_STATE_INT: %d\n", rdata0);
    cdsi_write(dev, CDSI0_CDSIRX_START_OFFS, 0x00000001);

    press_powerkey();
    rdata1 = cdsi_read(dev, CDSI0_CDSIRX_LPRX_STATE_INT_STAT_OFFS);
    while ((rdata1 & 0x00000001) != 0x00000001) {
        rdata1 = cdsi_read(dev, CDSI0_CDSIRX_LPRX_STATE_INT_STAT_OFFS);
        depress_powerkey();
    }
    printf("Second LPRX_STATE_INT: %d\n", rdata1);

    cdsi_write(dev, CDSI0_CDSIRX_LPRX_STATE_INT_STAT_OFFS, 0x00000001);
    cdsi_write(dev, CDSI0_CDSIRX_DSI_LPTX_MODE_OFFS, CDSIRX_DSI_LPTX_MODE_VAL);
    cdsi_write(dev, CDSI0_CDSIRX_ADDRESS_CONFIG_OFFS, 0x00000000);
}

struct display_panel lg4892_panel = {
    .cdsi_panel_init = lg4892_dsi_init,
};

static void *display_fn(void *p_data)
{
    dsi_initialize(&lg4892_panel, TSB_CDSI0, TSB_CDSI_RX);
    return NULL;
}

int display_init(void)
{
    return pthread_create(&g_display_thread, NULL, display_fn, NULL);
}
