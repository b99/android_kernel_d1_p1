/******************************************************************************
 *
 * TTY Driver for the Flashless boot over MIPI HSI.
 *
 * Copyright (C) 2011 Intel Mobile Communications
 * Thippa Reddy <thippareddy.dammur@intel.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301,
 * USA
 *
 *
 *****************************************************************************/
/******************************************************************************
  Copyright (C), 1988-2012, Huawei Tech. Co., Ltd.
  File name:     xmd_boot_tty.c
  Author:       Version:        Date: 
  y00185015     1.0             20110822
  Description:  AP侧Flashless功能实现集合，xmd-boot0驱动(tty类)
  Others:
******************************************************************************/

/*==============================================================================
修订历史

 问题单号			修改人		日期		原因
==============================================================================*/

#include <linux/module.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/interrupt.h>
#include <linux/wait.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>
#include <linux/timer.h>
#include <linux/time.h>
#include <linux/dma-mapping.h>
#include <linux/slab.h>
#include <linux/delay.h>

#include <linux/hsi_driver_if.h>
#include "xmd_boot_tty.h"

#include <plat/omap_hsi.h>      
#include <mach/xmd.h>           
#if defined(CONFIG_ARCH_OMAP3)
#include <mach/mux.h>
#endif
#if defined(CONFIG_ARCH_OMAP4)
#include "mux.h"
#endif
#include <linux/sched.h> 

#define pr_dbg pr_debug
#define XMD_BOOT_DEBUG_ENABLE   1

#define GET_SIZE_IN_WORDS(size)									\
	if (size > 4) {												\
		size = (size & 0x3) ? ((size >> 2) + 1):(size >> 2);	\
	} else {													\
		size = 1;												\
	}

static int xmd_boot_tty_open(struct tty_struct *tty, struct file *f);
static void xmd_boot_tty_close(struct tty_struct *tty, struct file *f);
static int xmd_boot_tty_write(struct tty_struct *tty,const unsigned char *buf,int size);
static int xmd_boot_hsi_probe_cb(struct hsi_device *dev);
static int xmd_boot_hsi_remove_cb(struct hsi_device *dev);
static void xmd_boot_tty_rd_cb(struct hsi_device *dev,unsigned int size);
static void xmd_boot_tty_wr_cb(struct hsi_device *dev,unsigned int size);
static void xmd_boot_read_work( struct work_struct *work);

extern int hsi_ll_phy_drv_init(void);
extern struct xmd_data my_xmd;

static struct xmd_boot_if_struct xmd_boot_if;

static int xmd_boot_config_bus(int rx_divisor, int tx_divisor);
static int boot_hsimode = -1;   //boot_hsimode, HSI模式配置值;

static DEFINE_MUTEX(xmd_hsimode_mutex);

int xmd_get_hsimode(void)
{
int hsimode=0;
    
    mutex_lock(&xmd_hsimode_mutex);
    hsimode = boot_hsimode;    //boot_hsimode, HSI模式配置值;
    mutex_unlock(&xmd_hsimode_mutex);
    return hsimode;
}
int xmd_set_hsimode(int hsimode)
{
//    int port=1; //pport->hsi_channel[channel].dev->ch->hsi_port->port_number
int result=0;
struct xmd_hsi_platform_data *pd=my_xmd.hsi_data.hsi_platform_data;

    pr_debug("\nXMD_BOOT_TTY: %s. begin! [hsimode]0x%08x -> 0x%08x;\n",__func__,boot_hsimode,hsimode);
    
    mutex_lock(&xmd_hsimode_mutex);
    
        if( boot_hsimode==hsimode ) {
            pr_warn("XMD: %s. wrong hsimode shift, but continue!\n",__func__);
            //mutex_unlock(&xmd_hsimode_mutex);
            //return status;
        }

//HSI_IOCTL_SET_HSI_MODE,	/* 15, Set HSI hsi_mode value for all channel */
    hsi_ioctl(xmd_boot_if.hsi_dev, HSI_IOCTL_SET_HSI_MODE, (void*)&hsimode);

//XMM_HSI_CAWAKE_LVL, HSI CAWAKE线默认值;
        if( (bool)(hsimode&XMM_HSI_CAWAKE_LVL)==true ) {    //XMM_HSI_CAWAKE_LVL, HSI CAWAKE线默认值;
            omap_mux_init_signal(pd->hsi_cawake_pinmux.muxname, OMAP_PIN_INPUT_PULLUP | \
                OMAP_PIN_OFF_NONE | OMAP_PIN_OFF_WAKEUPENABLE); //.muxval = OMAP_PIN_INPUT_PULLDOWN | OMAP_PIN_OFF_NONE | OMAP_PIN_OFF_WAKEUPENABLE,
        } else {
    #if 1   //#if   1   //#endif
            //xmd_omap_mux_init_signal(pd->hsi_acready_pinmux);
            xmd_omap_mux_init_signal(pd->hsi_cawake_pinmux);
    #else   //#if   1   //#endif
            omap_mux_init_signal(pd->hsi_cawake_pinmux.muxname, OMAP_PIN_INPUT_PULLDOWN | \
                OMAP_PIN_OFF_NONE | OMAP_PIN_OFF_WAKEUPENABLE); //.muxval = OMAP_PIN_INPUT_PULLDOWN | OMAP_PIN_OFF_NONE | OMAP_PIN_OFF_WAKEUPENABLE,
    #endif   //#if   1   //#endif
        }
//XMM_HSI_3_WIRE_TYPE, HSI线模式标志; //XMM_HSI_READY_LVL, HSI READY线默认值;	//目前不支持
        if( (bool)(hsimode&XMM_HSI_3_WIRE_TYPE)==true ) {  //XMM_HSI_3_WIRE_TYPE, HSI线模式标志;
            //boot_hsimode = hsimode;    //boot_hsimode, HSI模式配置值;
        //HSI_IOCTL_3_WIRE
            pr_debug("\n XMD_BOOT_TTY: %s. enter 3_WIRE HSI mode!!\n",__func__);
                //hsi_ioctl(xmd_boot_if.hsi_dev, HSI_IOCTL_3_WIRE, NULL);
            hsi_ioctl(xmd_boot_if.hsi_dev, HSI_IOCTL_SET_WAKE_RX_3WIRES_MODE, NULL);
        }
        else { //if( (bool)(hsimode&XMM_HSI_3_WIRE_TYPE)==true ) {
            //boot_hsimode = hsimode;    //boot_hsimode, HSI模式配置值;
        //HSI_IOCTL_4_WIRE
            pr_debug("\n XMD_BOOT_TTY: %s. enter 4_WIRE HSI mode!!\n",__func__);
                //hsi_ioctl(xmd_boot_if.hsi_dev, HSI_IOCTL_4_WIRE, NULL);
            hsi_ioctl(xmd_boot_if.hsi_dev, HSI_IOCTL_SET_WAKE_RX_4WIRES_MODE, NULL);
            hsi_ioctl(xmd_boot_if.hsi_dev, HSI_IOCTL_ACWAKE_UP, NULL);
        }
//XMM_HSI_SOFT_RESET, HSI SOFT_RESET标志;
        if( (bool)(hsimode&XMM_HSI_SOFT_RESET)==true ) {  //XMM_HSI_SOFT_RESET, HSI SOFT_RESET标志;
            result = hsi_ioctl(xmd_boot_if.hsi_dev,
                              HSI_IOCTL_SW_RESET,
                              NULL);
            if (result)
                printk("\n XMD_BOOT: XMM_HSI_SOFT_RESET error %d. %s %d\n", result, __func__, __LINE__);
            //result = hsi_ioctl(hsi_ll_data.dev[HSI_LL_CTRL_CHANNEL],HSI_IOCTL_SW_RESET,NULL);
        }
//XMM_HSI_OPEN_AGAIN, HSI hsi_open again标志;
        //HSI_IOCTL_SW_RESET -> hsi_open(hsi_ll_data.dev[ch_i])
        if( (bool)(hsimode&XMM_HSI_OPEN_AGAIN)==true )  //XMM_HSI_OPEN_AGAIN, HSI hsi_open again标志;
        { //if (hsi_ll_data.ch[ch_i].open == TRUE) {
            printk("\n XMD_BOOT: %s. ch %d hsi_open start.\n",__func__,0);
            hsi_open(xmd_boot_if.hsi_dev);  //#y00185015#20110919#Add for switch HSI#
        }

//XMM_HSI_EBL_START, HSI EBL_START标志;
        if( (bool)(hsimode&XMM_HSI_EBL_START)==true ) {  //XMM_HSI_EBL_START, HSI EBL_START标志;
        //#20110913#y00185015#Add for hsr_fifo_flush.patch#
            xmd_boot_if.rx_state = 0;
            xmd_boot_if.rx_size = 0;
        //  hsi_read(xmd_boot_if.hsi_dev, &xmd_boot_if.rx_size, 1);
        }
//XMM_HSI_RX_CLK_SET; //XMM_HSI_TX_CLK_SET; //XMM_HSI_RX_CLK,XMM_HSI_TX_CLK;
        if( (bool)(hsimode&XMM_HSI_RX_CLK_SET)==true && (bool)(hsimode&XMM_HSI_TX_CLK_SET)==true ) {  //XMM_HSI_RX_CLK_SET; //XMM_HSI_TX_CLK_SET;
         // /* For 96MHZ Base CLK, 96MHZ(0) 48MHZ(1)  24MHZ(3) */
             result = xmd_boot_config_bus(XMM_MODE_GET_DIV_RX(hsimode),XMM_MODE_GET_DIV_TX(hsimode));
             if (result)
                 printk("\n XMD_BOOT_TTY: result error %d.%s %d\n", result, __func__, __LINE__);
        }
//XMM_HSI_FLUSH_RX, HSI FLUSH_RX标志;
        if( (bool)(hsimode&XMM_HSI_FLUSH_RX)==true ) {  //XMM_HSI_FLUSH_RX, HSI FLUSH_RX标志;
        //#20110913#y00185015#Add for hsr_fifo_flush.patch#
             result = hsi_ioctl(xmd_boot_if.hsi_dev,
                                  HSI_IOCTL_FLUSH_RX,
                                  NULL);
             if (result)
                 printk("\n XMD_BOOT_TTY: HSI_IOCTL_FLUSH_RX error %d. %s %d\n", result, __func__, __LINE__);
        }
//XMM_HSI_FLUSH_TX, HSI FLUSH_TX标志;
        if( (bool)(hsimode&XMM_HSI_FLUSH_TX)==true ) {  //XMM_HSI_FLUSH_TX, HSI FLUSH_TX标志;
            result = hsi_ioctl(xmd_boot_if.hsi_dev,
                              HSI_IOCTL_FLUSH_TX,
                              NULL);
            if (result)
                printk("\n XMD_BOOT_TTY: HSI_IOCTL_FLUSH_TX error %d. %s %d\n", result, __func__, __LINE__);
        }

//XMM_HSI_FLUSH_RXSTATE, HSI FLUSH_RXSTATE标志;
        if( (bool)(hsimode&XMM_HSI_FLUSH_RXSTATE)==true ) {  //XMM_HSI_FLUSH_RXSTATE, HSI FLUSH_RXSTATE标志;
        //#20110913#y00185015#Add for hsr_fifo_flush.patch#
             result = hsi_ioctl(xmd_boot_if.hsi_dev,
                                  HSI_IOCTL_FLUSH_RXSTATE,
                                  NULL);
             if (result)
                 printk("\n XMD_BOOT: HSI_IOCTL_FLUSH_RXSTATE error %d. %s %d\n", result, __func__, __LINE__);
        }
//XMM_HSI_FLUSH_TXSTATE, HSI FLUSH_TXSTATE标志;
        if( (bool)(hsimode&XMM_HSI_FLUSH_TXSTATE)==true ) {  //XMM_HSI_FLUSH_TXSTATE, HSI FLUSH_TXSTATE标志;
            result = hsi_ioctl(xmd_boot_if.hsi_dev,
                              HSI_IOCTL_FLUSH_TXSTATE,
                              NULL);
            if (result)
                printk("\n XMD_BOOT: HSI_IOCTL_FLUSH_TXSTATE error %d. %s %d\n", result, __func__, __LINE__);
        }
//XMM_HSI_OPEN_AGAIN, HSI hsi_open again标志;
        if( (bool)(hsimode&XMM_HSI_OPEN_AGAIN)==true ) {  //XMM_HSI_OPEN_AGAIN, HSI hsi_open again标志;
            //hsi_ll_wakeup_cp(HSI_LL_WAKE_LINE_HIGH);
            printk("\n XMD_BOOT: %s. ch %d hsi_read start.\n",__func__,0);
            hsi_read(xmd_boot_if.hsi_dev, &xmd_boot_if.rx_size, 1);
        }

//XMM_HSI_USE_BOOT, HSI使用模式标志;	//##调试而设#

//XMM_HSI_PSI_STATE, HSI PSI_STATE标志;
//XMM_HSI_EBL_START, HSI EBL_START标志;
//XMM_HSI_IMG_STATE, HSI IMG_STATE标志;   //目前无动作
//XMM_HSI_CODE_DOWN, HSI CODE_DOWN标志;   //目前无动作

//XMM_HSI_3_WIRE_TYPE, HSI线模式标志;
//XMM_HSI_READY_LVL, HSI READY线默认值;    //目前不支持,值同 XMM_HSI_3_WIRE_TYPE;
//XMM_HSI_EXHSI_TYPE, HSI ExHSI模式标志;
//XMM_HSI_CAWAKE_LVL, HSI CAWAKE线默认值;

//XMM_HSI_CLK_UNDIS, HSI时钟取消关闭标志;
//XMM_HSI_PM_UNDFT, HSI PM_unset_default标志;
//XMM_HSI_DISINT_CAWAKE, HSI CAWAKE中断禁止标志;
//XMM_HSI_RD_MDELAY, HSI hsi_read后mdelay标志;    //##调试而设#

//XMM_HSI_HEAD_RD, HSI MIPI_Head读标志;
//XMM_HSI_HEAD_WR, HSI MIPI_Head写标志;   //目前不支持，效果同'1'；

        boot_hsimode = hsimode;    //boot_hsimode, HSI模式配置值;
        mutex_unlock(&xmd_hsimode_mutex);
    pr_dbg("\n XMD_BOOT_TTY: %s. end!\n",__func__);
    
    return( result );
}


static int xmd_boot_tty_write_room(struct tty_struct *tty)
{
    return 8192;
}

#if 0
static void xmd_boot_port_event_cb(struct hsi_device *dev,unsigned int event,void *arg)
{
	switch(event) {
	case HSI_EVENT_BREAK_DETECTED:
#if defined (XMD_BOOT_DEBUG_ENABLE)
		printk("\nXMD_BOOT:Break Event detected.%s %d\n",
				  __func__, __LINE__);
#endif
		break;
	case HSI_EVENT_ERROR:
#if defined (XMD_BOOT_DEBUG_ENABLE)
		printk("\nXMD_BOOT:Error Event detected.%s %d\n",
				  __func__, __LINE__);
#endif
		break;
	case HSI_EVENT_PRE_SPEED_CHANGE:
#if defined (XMD_BOOT_DEBUG_ENABLE)
		printk("\nXMD_BOOT:Pre Speed changer Event detected.%s %d\n",
				  __func__, __LINE__);
#endif
		break;
	case HSI_EVENT_POST_SPEED_CHANGE:
#if defined (XMD_BOOT_DEBUG_ENABLE)
		printk("\nXMD_BOOT:Post Speed changer Event detected.%s %d\n",
				 __func__, __LINE__);
#endif
		break;
	case HSI_EVENT_CAWAKE_UP:
#if defined (XMD_BOOT_DEBUG_ENABLE)
		printk("\nXMD_BOOT:CA wakeup line UP detected.%s %d\n",
				  __func__, __LINE__);
#endif
		break;
	case HSI_EVENT_CAWAKE_DOWN:
#if defined (XMD_BOOT_DEBUG_ENABLE)
		printk("\nXMD_BOOT:CA wakeup line DOWN detected.%s %d\n",
				  __func__, __LINE__);
#endif
		break;
	case HSI_EVENT_HSR_DATAAVAILABLE:
#if defined (XMD_BOOT_DEBUG_ENABLE)
		printk("\nXMD_BOOT:HSR Event detected.%s %d\n",
				  __func__, __LINE__);
#endif
		break;
	default:
#if defined (XMD_BOOT_DEBUG_ENABLE)
		printk("\nXMD_BOOT:Invalid Event detected.%s %d\n",
				  __func__, __LINE__);
#endif
		break;
	}
}
#endif


static struct tty_operations xmd_boot_tty_ops = {
	.open = xmd_boot_tty_open,
	.close = xmd_boot_tty_close,
	.write = xmd_boot_tty_write,
	.write_room = xmd_boot_tty_write_room,
};

static struct hsi_device_driver  xmd_boot_hsi_if =
{
	.ctrl_mask  = ANY_HSI_CONTROLLER,
	.ch_mask[0] = 0,
	.probe      = xmd_boot_hsi_probe_cb,
	.remove     = xmd_boot_hsi_remove_cb,
	.driver     =
	{
		.name = "XMD_BOOT_LAYER",
	},
};

static int xmd_boot_hsi_probe_cb(struct hsi_device *dev)
{
    pr_debug("\n xmd_boot_tty.c:: xmd_boot_hsi_if. %s. %d",__func__,__LINE__);
	hsi_set_read_cb(dev, xmd_boot_tty_rd_cb);
	hsi_set_write_cb(dev, xmd_boot_tty_wr_cb);
	//hsi_set_port_event_cb(dev, xmd_boot_port_event_cb);
	hsi_set_port_event_cb(dev, NULL);

	xmd_boot_if.hsi_dev = dev;
	return 0;
}

static int xmd_boot_hsi_remove_cb(struct hsi_device *dev)
{
    pr_debug("\n xmd_boot_tty.c:: xmd_boot_hsi_if. %s. %d",__func__,__LINE__);
	hsi_set_read_cb(dev, NULL);
	hsi_set_write_cb(dev, NULL);
	hsi_set_port_event_cb(dev, NULL);

	xmd_boot_if.hsi_dev = NULL;
	return 0;
}

static void xmd_boot_read_work(struct work_struct *work)
{
	void *ptr;
	void *tty_ptr;
	unsigned int size;
	unsigned char *tbuf = NULL;
	struct tty_struct *tty = xmd_boot_if.tty_ptr;
	unsigned int tty_data_size = 0;

        pr_dbg("\nXMD_BOOT_TTY::%s: xmd_boot_if.rx_state=%d,xmd_boot_if.rx_size=%d;\n",__func__,xmd_boot_if.rx_state,xmd_boot_if.rx_size);
        if( (bool)(boot_hsimode&XMM_HSI_PSI_STATE)==true )  {//XMM_HSI_PSI_STATE, HSI PSI_STATE标志;
            xmd_boot_if.rx_state = 1;
            ptr = &xmd_boot_if.rx_size;
            size = 4; //xmd_boot_if.rx_size;
            GET_SIZE_IN_WORDS(size)
            tty_data_size = 4;

            tty_ptr = &xmd_boot_if.rx_size;
        } else {
        	if(xmd_boot_if.rx_state) {
        		xmd_boot_if.rx_state = 0;
        		//xmd_boot_if.rx_size = 0;
        		ptr = &xmd_boot_if.rx_size;
        		size = 1;
        		tty_data_size = xmd_boot_if.rx_size;
                    xmd_boot_if.rx_size = 0;
                
        		tty_ptr = xmd_boot_if.rx_ptr;
        	} else {
        		xmd_boot_if.rx_state = 1;
        		ptr = xmd_boot_if.rx_ptr = kmalloc(xmd_boot_if.rx_size,
        											GFP_DMA | GFP_ATOMIC);
        		if(!ptr) {
        			pr_err("\nXMD_BOOT_TTY: Mem alloc failed\n");
        			return;
        		}
        		size = xmd_boot_if.rx_size;
        		GET_SIZE_IN_WORDS(size)
        		tty_data_size = 4;
        		tty_ptr = &xmd_boot_if.rx_size;

                    if( (bool)(boot_hsimode&XMM_HSI_HEAD_RD)==false )  {//XMM_HSI_PSI_STATE, HSI PSI_STATE标志;
                        hsi_read(xmd_boot_if.hsi_dev, ptr, size);
                        return;
                    }
        	}
        }

	tty->low_latency = 1;
	tty_prepare_flip_string(tty, &tbuf, tty_data_size);
	if(!tbuf) {
		pr_err("\nXMD_BOOT_TTY: TTY core failed to alloc memory\n");
		return;
	}
	memcpy((void *)tbuf, tty_ptr, tty_data_size);
    pr_dbg("\n XMD_BOOT_TTY: RX size %d, Rbuf(Hex) =[0x%02x->] 0x%08X,0x%08X,0x%08X;\n", tty_data_size,*(char*)tbuf,*((u32*)tbuf+0),*((u32*)tbuf+1),*((u32*)tbuf+2));

	if(xmd_boot_if.rx_state == 0) {
            kfree(xmd_boot_if.rx_ptr);
            xmd_boot_if.rx_ptr = NULL;
	}
	tty_flip_buffer_push(tty);
	tty_wakeup(tty);

    pr_debug("\n XMD_BOOT_TTY: xmd_boot_read_work end. %d\n",__LINE__);
	hsi_read(xmd_boot_if.hsi_dev, ptr, size);
}

static void xmd_boot_tty_rd_cb(struct hsi_device *dev, unsigned int len)
{
    if( (bool)(boot_hsimode&XMM_HSI_RDDN_LOG)==true )   //XMM_HSI_RDDN_LOG, HSI read_done LOG标志;
        pr_dbg("XMD_BOOT: read_done. size %d -> Rbuf(Hex) = 0x%08X, time %ld ms;\n",xmd_boot_if.rx_size,xmd_boot_if.rx_size,1000*jiffies/HZ);

    if( (bool)(boot_hsimode&XMM_HSI_EBL_START)==false && (xmd_boot_if.rx_size<4) )  {
        pr_dbg("XMD_BOOT: read_done. [if 4HSI && xmd_boot_if.rx_size <4 Force to 4] size %d -> %d;\n",xmd_boot_if.rx_size,4 );
        xmd_boot_if.rx_size = 4;
    }

	PREPARE_WORK(&xmd_boot_if.read_work, xmd_boot_read_work);
	queue_work(xmd_boot_if.read_wq, &xmd_boot_if.read_work);
}

static void xmd_boot_tty_wr_cb(struct hsi_device *dev, unsigned int size)
{
    if( (bool)(boot_hsimode&XMM_HSI_WRDN_LOG)==true )
        pr_dbg("XMD_BOOT: write_done. size(*4) %d, time %ld ms;\n",size*4,1000*jiffies/HZ);

	xmd_boot_if.wr_flag = 1;
	wake_up_interruptible(&xmd_boot_if.wr_ev);
}

// /* For 96MHZ Base CLK, 96MHZ(0) 48MHZ(1)  24MHZ(3) */
static int xmd_boot_config_bus(int rx_divisor, int tx_divisor)
{
	int result = 0;
    
    pr_info("\n XMD_BOOT_TTY: %s([rx_divisor]%d,[tx_divisor]%d).\n",__func__,rx_divisor,tx_divisor);
    xmd_boot_if.c_rx.mode       = HSI_MODE_FRAME;
    xmd_boot_if.c_rx.flow       = HSI_FLOW_SYNCHRONIZED;
    xmd_boot_if.c_rx.frame_size = HSI_FRAMESIZE_DEFAULT;/*1 frame bit, 32 bit payload*/
    xmd_boot_if.c_rx.divisor    = rx_divisor;  
    xmd_boot_if.c_rx.counters   = (HSI_COUNTERS_FT_DEFAULT |
                                                                         HSI_COUNTERS_TB_DEFAULT |
                                                                         HSI_COUNTERS_FB_DEFAULT);
    xmd_boot_if.c_rx.channels   = 1;
    
    result = hsi_ioctl(xmd_boot_if.hsi_dev,
                         HSI_IOCTL_SET_RX,
                       &xmd_boot_if.c_rx);
        if (result)
            pr_warn("\n XMD_BOOT_TTY:RX config error %d. %s %d\n", result, __func__, __LINE__);
        
    xmd_boot_if.c_tx.mode       = HSI_MODE_FRAME;
    xmd_boot_if.c_tx.flow       = HSI_FLOW_SYNCHRONIZED;
    xmd_boot_if.c_tx.frame_size = HSI_FRAMESIZE_DEFAULT;/*1 frame bit, 32 bit payload*/
    xmd_boot_if.c_tx.divisor    = tx_divisor; 
    xmd_boot_if.c_tx.arb_mode   = HSI_ARBMODE_ROUNDROBIN;
    xmd_boot_if.c_tx.channels   = 1;
    
    result = hsi_ioctl(xmd_boot_if.hsi_dev,
                         HSI_IOCTL_SET_TX,
                       &xmd_boot_if.c_tx);
    if (result)
        pr_warn("\n XMD_BOOT_TTY:TX config error %d.%s %d\n", result, __func__, __LINE__);

    return result;
}

static int xmd_boot_tty_open(struct tty_struct *tty, struct file *f)
{
    pr_info("\n XMD_BOOT_TTY: %s begin.\n",__func__);
    if(tty->index > 0) {
        pr_err("\nXMD_BOOT_TTY: %s failed.\n",__func__);
        return -ENODEV;
    }

    xmd_boot_if.tty_ptr = tty;
    xmd_boot_if.rx_state = 0;
    xmd_boot_if.rx_size = 0;

    if( xmd_boot_if.hsi_dev!=NULL ) {
        hsi_open(xmd_boot_if.hsi_dev);

        //Enable read data from the hsi device channel
        hsi_read(xmd_boot_if.hsi_dev, &xmd_boot_if.rx_size, 1);
    }

    pr_dbg("\n XMD_BOOT_TTY: %s end.\n",__func__);
    return 0;
}

static void xmd_boot_tty_close(struct tty_struct *tty, struct file *f)
{
    pr_info("\n XMD_BOOT_TTY: %s begin.\n",__func__);
    if (tty->index > 0) {
        pr_err("\nXMD_BOOT_TTY: %s failed.\n",__func__);
        return;
    }

    //Check whether xmd_boot_hsi driver had been registered yet, if not, do not call hsi function
    if( xmd_boot_if.hsi_dev!=NULL ) {   
        boot_hsimode = XMM_HSI_MODE_HSI_NORM( (XMM_HSI_DIV_48M),(XMM_HSI_DIV_48M) );
        hsi_ioctl(xmd_boot_if.hsi_dev, HSI_IOCTL_SET_HSI_MODE, (void*)&boot_hsimode);

        hsi_write_cancel(xmd_boot_if.hsi_dev);
        hsi_read_cancel(xmd_boot_if.hsi_dev);
        xmd_boot_if.wr_flag = 1;
        wake_up_interruptible(&xmd_boot_if.wr_ev);
        hsi_close(xmd_boot_if.hsi_dev);
    }

    if(xmd_boot_if.rx_ptr) {
        kfree(xmd_boot_if.rx_ptr);
        xmd_boot_if.rx_ptr = NULL;
    }

    pr_debug("\n XMD_BOOT_TTY: %s end.\n",__func__);
    return;
}

static int xmd_boot_tty_write(struct tty_struct *tty, const unsigned char *tbuf, int size)
{
    const unsigned char *buf=tbuf;
    int result;
    int iRet=size;
    static int iCnt=0;

    iCnt++;
    pr_dbg("\n XMD_BOOT_TTY: %s begin. write %d. size %d, time %ld ms;\n",__func__,iCnt,size,1000*jiffies/HZ);
    if( size<=24 )
        pr_debug("\n XMD_BOOT_TTY: write %d. size %d, Tbuf(Hex) = [0x%02x->] 0x%08X,0x%08X;\n", iCnt,size,*((u8*)buf+0),*((u32*)buf+0),*((u32*)buf+1) );

    if(tty->index > 0) {
        result = -ENODEV;
        goto quit_write;
    }

    if( xmd_boot_if.hsi_dev==NULL ) {
        pr_err("\nXMD_BOOT_TTY: %s failed. hsi_drvmode is in XMD_HSI_DRVMODE_NORM now!\n",__func__);
        return -ENODEV;
    }

    GET_SIZE_IN_WORDS(size)
    xmd_boot_if.wr_flag = 0;

    result = hsi_write(xmd_boot_if.hsi_dev, (unsigned int*)buf, size);
    if(!result) {
        wait_event_interruptible_timeout(xmd_boot_if.wr_ev,
                                                            xmd_boot_if.wr_flag == 1, XMD_BOOT_TTY_WRITE_TIMEOUT );
        pr_debug("\n XMD_BOOT_TTY: xmd_boot_tty_write end success. time =%ld ms;\n",1000*jiffies/HZ);

        if( xmd_boot_if.wr_flag == 1 ) {
            return iRet;
        } else {
            hsi_write_cancel(xmd_boot_if.hsi_dev);
            printk("\\XMD_BOOT_TTY: xmd_boot_tty_write failed. time =%ld ms;\n",1000*jiffies/HZ);
            return -EREMOTEIO;
        }
    }

quit_write:
	pr_err("\nXMD_BOOT_TTY: write failed. time =%ld ms;\n",1000*jiffies/HZ);
	return result;
}

int xmd_boot_hsi_drv_init(void)
{
    int result=0;

    xmd_boot_if.hsi_dev = NULL;
    xmd_boot_hsi_if.ch_mask[0] = 1; /* Ch 0 only*/
    result = hsi_register_driver(&xmd_boot_hsi_if);
    if (result) {
        pr_err("\n%s: hsi registration failed.\n",__func__);
        return -ENODEV;
    }
    while(xmd_boot_if.hsi_dev == NULL) {
        /*wait till phy drv registration completes*/
        msleep_interruptible(100);
    }

    return 0;
}

void xmd_boot_hsi_drv_exit(void)
{
    hsi_unregister_driver(&xmd_boot_hsi_if);
    while(xmd_boot_if.hsi_dev != NULL) {
        /*wait till phy drv unregistration completes*/
        msleep_interruptible(100);
    }
}

static int __init xmd_boot_tty_init(void)
{
	int result;

	pr_debug("\nXMD_BOOT_TTY: Initializing XMD BOOT TTY driver\n");
	xmd_boot_if.hsi_dev = NULL;
	init_waitqueue_head(&xmd_boot_if.wr_ev);
	xmd_boot_if.read_wq = create_workqueue("xmd_boot_read_wq");
	INIT_WORK(&xmd_boot_if.read_work, xmd_boot_read_work);

	xmd_boot_if.tty_drv = alloc_tty_driver(1);
	if (!xmd_boot_if.tty_drv) {
		pr_err("\nXMD_BOOT_TTY: alloc_tty_driver failed\n");
		return -ENOMEM;
	}

	xmd_boot_if.tty_drv->magic = TTY_DRIVER_MAGIC;
	xmd_boot_if.tty_drv->owner = THIS_MODULE;
	xmd_boot_if.tty_drv->driver_name = "XMD_BOOT_TTY";
	xmd_boot_if.tty_drv->name = "xmd-boot";
	xmd_boot_if.tty_drv->minor_start = 0;
	xmd_boot_if.tty_drv->major = 0;
	xmd_boot_if.tty_drv->num = 1;
	xmd_boot_if.tty_drv->type = TTY_DRIVER_TYPE_SERIAL;
	xmd_boot_if.tty_drv->subtype = SERIAL_TYPE_NORMAL;
	xmd_boot_if.tty_drv->init_termios = tty_std_termios;
	xmd_boot_if.tty_drv->init_termios.c_iflag = 0;
	xmd_boot_if.tty_drv->init_termios.c_oflag = 0;
	xmd_boot_if.tty_drv->init_termios.c_cflag = B38400 | CS8 | CREAD;
	xmd_boot_if.tty_drv->init_termios.c_lflag = 0;
	xmd_boot_if.tty_drv->flags = TTY_DRIVER_RESET_TERMIOS |
						 TTY_DRIVER_REAL_RAW	  |
						 TTY_DRIVER_DYNAMIC_DEV;

	tty_set_operations(xmd_boot_if.tty_drv, &xmd_boot_tty_ops);

	result = tty_register_driver(xmd_boot_if.tty_drv);
	if(result) {
		pr_err("\nXMD_BOOT_TTY: tty_register_driver failed(%d)", result);
		return result;
	}
	tty_register_device(xmd_boot_if.tty_drv, 0, 0);
	pr_debug("\nXMD_BOOT_TTY: XMD BOOT TTY driver initialized\n");
	return 0;
}

module_init(xmd_boot_tty_init);

MODULE_AUTHOR("Intel");
MODULE_DESCRIPTION("XMD BOOT TTY Driver");
MODULE_LICENSE("GPL");
MODULE_INFO(Version, "1.0-XMD_BOOT_TTY");
