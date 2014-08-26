/*
 * drivers/power/bq27510_battery.c
 *
 * BQ27510 battery driver
 *
 * Copyright (C) 2012-2015 HUAWEI, Inc.
 * Author: HUAWEI, Inc.
 *
 * This package is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/param.h>
#include <linux/jiffies.h>
#include <linux/workqueue.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>
#include <linux/idr.h>
#include <linux/i2c.h>
#include <asm/unaligned.h>
#include <linux/interrupt.h>
#include <asm/irq.h>
#include <linux/module.h>
#include <linux/i2c/bq27510_battery.h>
#include <linux/io.h>
#include <linux/uaccess.h>
#include <linux/vmalloc.h>
#include <linux/fs.h>
#include <linux/fcntl.h>
#include <linux/ctype.h>
#include <linux/slab.h>
#include <mach/gpio.h>

#include "power_supply.h"
#include <linux/i2c/bq2416x.h>
#include <hsad/config_interface.h>



static DEFINE_IDR(bq27510_battery_id);
static DEFINE_MUTEX(bq27510_battery_mutex);


typedef unsigned int uint32;

#define MAGIC_NUMBER 0x12345678
#define GAS_GAUGE_FIRMWARE_NAME  32

struct firmware_header_entry
{
    uint32 magic_number;
    char file_name[48];
    uint32 offset;
    uint32 length;
};

struct i2c_client* g_battery_measure_by_bq27510_i2c_client = NULL;
struct bq27510_device_info* g_battery_measure_by_bq27510_device = NULL;
/*extern a notifier list for battery low notification*/
extern struct blocking_notifier_head notifier_list;


static struct i2c_driver bq27510_battery_driver;
/*disable i2c read or write operating when firmware update*/
static unsigned int gBq27510DownloadFirmwareFlag = BSP_NORMAL_MODE;

/*
 * Common code for bq27510 devices
 */

enum
{
    BQ27510_NORMAL_MODE,
    BQ27510_UPDATE_FIRMWARE,
    BQ27510_LOCK_MODE
};

struct bq27510_context
{
    unsigned int temperature;
    unsigned int capacity;
    unsigned int volt;
    unsigned int bat_current;
    unsigned int remaining_capacity;
    unsigned int full_capacity;
    unsigned int battery_present;
    unsigned int battery_health;
    unsigned char state;
    unsigned long locked_timeout_jiffies;//after updating firmware, device can not be accessed immediately
    unsigned int i2c_error_count;
    unsigned int lock_count;
};

struct bq27510_context gauge_context =
{
    .temperature = 2830,//10 degree
    .capacity = 50,     //50 percent
    .volt = 3700,       // 3.7 V
    .bat_current = 200,// 200 mA
    .remaining_capacity = 800,//mAH
	.full_capacity = 1800,
    .battery_present = 1,
    .battery_health = POWER_SUPPLY_HEALTH_GOOD,
    .state = BQ27510_NORMAL_MODE,
    .i2c_error_count = 0
};

#define GAS_GAUGE_I2C_ERR_STATICS() ++gauge_context.i2c_error_count
#define GAS_GAUGE_LOCK_STATICS() ++gauge_context.lock_count

static int bq27510_is_accessible(void)
{
    if(gauge_context.state == BQ27510_UPDATE_FIRMWARE)
    {
        BQ27510_DBG("bq27510 isn't accessible,It's updating firmware!\n");
        GAS_GAUGE_LOCK_STATICS();
        return 0;
    }
    else if(gauge_context.state == BQ27510_NORMAL_MODE){
        return 1;
	}else //if(gauge_context.state == BQ27510_LOCK_MODE)
    {
        if(time_is_before_jiffies(gauge_context.locked_timeout_jiffies))
        {
            gauge_context.state = BQ27510_NORMAL_MODE;
            return 1;
        }
        else
        {
            BQ27510_DBG("bq27510 isn't accessible after firmware updated immediately!\n");
            return 0;
        }
    }
}


static int bq27510_i2c_read_word(struct bq27510_device_info *di,u8 reg)
{
    int err = 0;
    int i = 0;

    mutex_lock(&bq27510_battery_mutex);
    for(i = 0; i < 5; i++){
        err = i2c_smbus_read_word_data(di->client,reg);
        if (err < 0) {
            GAS_GAUGE_I2C_ERR_STATICS();
            BQ27510_ERR("[%s,%d] i2c_smbus_read_byte_data failed\n",__FUNCTION__,__LINE__);
        } else {
            break;
        }
        msleep(5);
    }
    mutex_unlock(&bq27510_battery_mutex);

    return err;
}

/* added for Firmware upgrade begine */
static int bq27510_i2c_word_write(struct i2c_client *client, u8 reg, u16 value)
{
    int err = 0;
    int i = 0;

    mutex_lock(&bq27510_battery_mutex);
    for(i = 0; i < 5; i++){
        err = i2c_smbus_write_word_data(client, reg, value);
        if (err < 0) {
            GAS_GAUGE_I2C_ERR_STATICS();
            BQ27510_ERR("[%s,%d] i2c_smbus_write_word_data failed\n",__FUNCTION__,__LINE__);
        } else {
            break;
        }
        msleep(5);
    }
    mutex_unlock(&bq27510_battery_mutex);

    return err;
}

static int bq27510_i2c_bytes_write(struct i2c_client *client, u8 reg, u8 *pBuf, u16 len)
{
    int i2c_ret = 0, i = 0,j = 0;
    u8 *p;

    p = pBuf;

    mutex_lock(&bq27510_battery_mutex);
    for(i=0; i<len; i+=I2C_SMBUS_BLOCK_MAX)
    {
        j = ((len - i) > I2C_SMBUS_BLOCK_MAX) ? I2C_SMBUS_BLOCK_MAX : (len - i);
        i2c_ret = i2c_smbus_write_i2c_block_data(client, reg+i, j, p+i);

        if (i2c_ret < 0)
        {
            GAS_GAUGE_I2C_ERR_STATICS();
            BQ27510_ERR("[%s,%d] i2c_transfer failed\n",__FUNCTION__,__LINE__);
            break;
        }
    }
    mutex_unlock(&bq27510_battery_mutex);

    return i2c_ret;
}

static int bq27510_i2c_bytes_read(struct i2c_client *client, u8 reg, u8 *pBuf, u16 len)
{
    int i2c_ret = 0, i = 0, j = 0;
    u8 *p;

    p = pBuf;

    mutex_lock(&bq27510_battery_mutex);
    for(i=0; i<len; i+=I2C_SMBUS_BLOCK_MAX)
    {
        j = ((len - i) > I2C_SMBUS_BLOCK_MAX) ? I2C_SMBUS_BLOCK_MAX : (len - i);
        i2c_ret = i2c_smbus_read_i2c_block_data(client, reg+i, j, p+i);
        if (i2c_ret < 0)
        {
            GAS_GAUGE_I2C_ERR_STATICS();
            BQ27510_ERR("[%s,%d] i2c_transfer failed\n",__FUNCTION__,__LINE__);
            break;
        }
    }
    mutex_unlock(&bq27510_battery_mutex);

    return i2c_ret;
}

static int bq27510_i2c_bytes_read_and_compare(struct i2c_client *client, u8 reg, u8 *pSrcBuf, u8 *pDstBuf, u16 len)
{
    int i2c_ret = 0;

    i2c_ret = bq27510_i2c_bytes_read(client, reg, pSrcBuf, len);
    if(i2c_ret < 0)
    {
        GAS_GAUGE_I2C_ERR_STATICS();
        BQ27510_ERR("[%s,%d] bq27510_i2c_bytes_read failed\n",__FUNCTION__,__LINE__);
        return i2c_ret;
    }

    i2c_ret = strncmp(pDstBuf, pSrcBuf, len);

    return i2c_ret;
}
/* added for Firmware upgrade end */

/*
 * Return the battery temperature in Celcius degrees
 * Or < 0 if something fails.
 */
int bq27510_battery_temperature(struct bq27510_device_info *di)
{
    int data = -1;

    if(bq27510_is_accessible())
    {
        data = bq27510_i2c_read_word(di,BQ27510_REG_TEMP);
        if(data < 0)
             BQ27510_ERR("i2c error in reading temperature!");
        else
             gauge_context.temperature = data;
    }

    if(data < 0)
        data = gauge_context.temperature;

    data = (data-CONST_NUM_2730)/CONST_NUM_10;
    BQ27510_DBG("read temperature result = %d Celsius\n",data);
    return data ;
}

/*
 * Return the battery Voltage in milivolts
 * Or < 0 if something fails.
 */
int bq27510_battery_voltage(struct bq27510_device_info *di)
{
    int data = -1;

    if(!bq27510_is_accessible())
        return gauge_context.volt;

    data = bq27510_i2c_read_word(di,BQ27510_REG_VOLT);
    if(data < 0) {
        BQ27510_ERR("i2c error in reading voltage!");
        data = gauge_context.volt;
    } else
        gauge_context.volt = data;

    BQ27510_DBG("read voltage result = %d mVolts\n",data);
    return data;//adapt android upper layer unit: mV
}

/*
 * Return the battery average current
 * Note that current can be negative signed as well
 * Or 0 if something fails.
 */
short bq27510_battery_current(struct bq27510_device_info *di)
{
    int data = -1;
    short nCurr = 0;

    if(!bq27510_is_accessible())
        return gauge_context.bat_current;

    data = bq27510_i2c_read_word(di,BQ27510_REG_AI);
    if(data < 0) {
         BQ27510_ERR("i2c error in reading current!");
         data = gauge_context.bat_current;
     } else
         gauge_context.bat_current = data;


    nCurr = (signed short)data;

    BQ27510_DBG("read current result = %d mA\n", nCurr);

    return nCurr;
}

/*
 * Return the battery Relative State-of-Charge
 * The reture value is 0 - 100%
 * Or < 0 if something fails.
 */
int bq27510_battery_capacity(struct bq27510_device_info *di)
{
    int data = 0;

    if(!bq27510_is_accessible())
        return gauge_context.capacity;

    data = bq27510_i2c_read_word(di,BQ27510_REG_SOC);
    if((data < 0)||(data > 100)) {
        BQ27510_ERR("i2c error in reading capacity!");
        data = gauge_context.capacity;
    }
    else
        gauge_context.capacity = data;

    BQ27510_DBG("read soc result = %d Hundred Percents\n",data);
    return data;
}

/*
 * Return the battery RemainingCapacity
 * The reture value is mAh
 * Or < 0 if something fails.
 */
int bq27510_battery_rm(struct bq27510_device_info *di)
{
    int data = -1;

    if(!bq27510_is_accessible())
        return gauge_context.remaining_capacity;

    data = bq27510_i2c_read_word(di,BQ27510_REG_RM);
    if(data < 0) {
        BQ27510_ERR("i2c error in reading remain capacity!");
        data = gauge_context.remaining_capacity;
    } else
        gauge_context.remaining_capacity = data;


    BQ27510_DBG("read rm result = %d mAh\n",data);
    return data;
}

/*
 * Return the battery FullChargeCapacity
 * The reture value is mAh
 * Or < 0 if something fails.
 */
int bq27510_battery_fcc(struct bq27510_device_info *di)
{
    int data = 0;

    if(!bq27510_is_accessible())
        return 0;

    data = bq27510_i2c_read_word(di,BQ27510_REG_FCC);
    if(data < 0) {
        BQ27510_ERR("i2c error in reading FCC!");
        data = gauge_context.full_capacity;
    } else
        gauge_context.full_capacity = data;

    BQ27510_DBG("read fcc result = %d mAh\n",data);
    return data;
}

/*
 * Return the battery Time to Empty
 * Or < 0 if something fails
 */
int bq27510_battery_tte(struct bq27510_device_info *di)
{
    int data = 0;

    if(!bq27510_is_accessible())
        return 0;

    data = bq27510_i2c_read_word(di,BQ27510_REG_TTE);
    if(data < 0){
        BQ27510_ERR("i2c error in reading TTE!");
        return 0;
    }
    BQ27510_DBG("read tte result = %d minutes\n",data);
    return data;
}

/*
 * Return the battery Time to Full
 * Or < 0 if something fails
 */
int bq27510_battery_ttf(struct bq27510_device_info *di)
{
    int data = 0;

    if(!bq27510_is_accessible())
        return 0;

    data = bq27510_i2c_read_word(di,BQ27510_REG_TTF);
    if(data < 0) {
        BQ27510_ERR("i2c error in reading TTF!");
        return 0;
    }

    BQ27510_DBG("read ttf result = %d minutes\n",data);
    return data;
}

/*
 * Return whether the battery charging is full
 *
 */
int is_bq27510_battery_full(struct bq27510_device_info *di)
{
    int data = 0;

    if(!bq27510_is_accessible())
        return 0;

    data = bq27510_i2c_read_word(di,BQ27510_REG_FLAGS);
    if(data < 0) {
        BQ27510_ERR("i2c error in reading battery charging full bit!");
        data = bq27510_i2c_read_word(di,BQ27510_REG_FLAGS);
        if(data < 0)
            return 0;
    }

    BQ27510_DBG("read flags result = 0x%x \n",data);
    return (data & BQ27510_FLAG_FC);
}

/*
 * Return whether the battery is discharging now
 *
 */
int is_bq27510_battery_discharging(struct bq27510_device_info *di)
{
    int data = 0;

    if(!bq27510_is_accessible())
        return 0;

    data = bq27510_i2c_read_word(di,BQ27510_REG_FLAGS);
    if(data < 0) {
        BQ27510_ERR("i2c error in reading battery discharging!");
        return 0;
    }

    BQ27510_DBG("read flags result = 0x%x \n",data);
    return (data & BQ27510_FLAG_DSG);
}

/*
 * Return the battery`s status flag
 * The reture value is in hex
 * Or < 0 if something fails.
 */
int is_bq27510_battery_reach_threshold(struct bq27510_device_info *di)
{
    int data = 0;

    if(!bq27510_is_accessible())
        return 0;

    if(!is_bq27510_battery_exist(di))
        return 0;

    data = bq27510_i2c_read_word(di,BQ27510_REG_FLAGS);
    if(data < 0)
        return 0;

    return data;
}

#define STATE_HARDWARE_ERR 2
static int bq27510_get_firmware_version_by_i2c(struct i2c_client *client)
{
    unsigned int data = 0;
    int id = 0;
    int ver = 0;

    mutex_lock(&bq27510_battery_mutex);
    if (0 != i2c_smbus_write_word_data(client,BQ27510_REG_CTRL,BQ27510_REG_FIRMWARE_ID)) {
        mutex_unlock(&bq27510_battery_mutex);
        return -1;
    }
    mdelay(2);
    id = i2c_smbus_read_word_data(client,BQ27510_REG_CTRL);
    mdelay(2);
    if (0 != i2c_smbus_write_word_data(client,BQ27510_REG_DFCLS,BQ27510_REG_FIRMWARE_VERSION)) {
        mutex_unlock(&bq27510_battery_mutex);
        return -1;
    }
    mdelay(2);
    ver = i2c_smbus_read_byte_data(client,BQ27510_REG_FLASH);
    mdelay(2);
    mutex_unlock(&bq27510_battery_mutex);
    if (id < 0 || ver < 0)
        return -1;
    data = (id << 16) | ver;
    return data;
}

static ssize_t bq27510_get_firmware_version(struct device_driver *driver, char *buf)
{
    int version = 0;
    int version_config = 0;
    int error = 0;

    if(!bq27510_is_accessible()) {
        error =  -1;
        goto exit;
    }

    version = bq27510_get_firmware_version_by_i2c(g_battery_measure_by_bq27510_i2c_client);
    if(version < 0) {
        error = -1;
    }
exit:
    if(error)
        return sprintf(buf,"Fail to read");
    else {
        get_hw_config_int("gas_gauge/version", &version_config, NULL);
        return sprintf(buf,"%x(%x)",version,version_config);
    }
}

static ssize_t bq27510_check_firmware_version(struct device_driver *driver, char *buf)
{
    int version = 0;
    int version_config = 0;
    int error = -1;

    if(!bq27510_is_accessible())
        return 0;

    version = bq27510_get_firmware_version_by_i2c(g_battery_measure_by_bq27510_i2c_client);
    if (version < 0) {
        error = -STATE_HARDWARE_ERR;
    } else if (get_hw_config_int("gas_gauge/version", &version_config, NULL)) {
            if (version == version_config)
                error = 0;
    }
    return sprintf(buf,"%d",error);
}

static ssize_t bq27510_check_qmax(struct device_driver *driver, char *buf)
{
    int control_status = 0;
    int qmax = 0,qmax1 = 0,data = 0;
    int design_capacity = 0;
    int error = 0;

    mutex_lock(&bq27510_battery_mutex);
    i2c_smbus_write_word_data(g_battery_measure_by_bq27510_i2c_client,BQ27510_REG_CTRL,BQ27510_REG_CTRS);
    mdelay(2);
    control_status  = i2c_smbus_read_word_data(g_battery_measure_by_bq27510_i2c_client,BQ27510_REG_CTRL);
    mdelay(2);
    i2c_smbus_write_word_data(g_battery_measure_by_bq27510_i2c_client,BQ27510_REG_DFCLS,BQ27510_REG_CLASS_ID);
    mdelay(2);
    qmax = i2c_smbus_read_byte_data(g_battery_measure_by_bq27510_i2c_client,BQ27510_REG_QMAX);
    printk("qmax = %d\n",qmax);
    mdelay(2);
    qmax1 = i2c_smbus_read_byte_data(g_battery_measure_by_bq27510_i2c_client,BQ27510_REG_QMAX1);
    printk("qmax1 = %d\n",qmax1);
    mutex_unlock(&bq27510_battery_mutex);
    data = (qmax << 8) | qmax1;
    if (!get_hw_config_int("gas_gauge/capacity", &design_capacity, NULL))
        printk(KERN_ERR "[%s,%d] gas gauge capacity required in hw_configs.xml\n",__FUNCTION__,__LINE__);

    if (data >= (design_capacity * 4 / 5) && data <= (design_capacity * 6 / 5 ))
        error = 1;

    return sprintf(buf,"%d\n",error);
}

static ssize_t bq27510_get_capacity(struct device_driver *driver, char *buf)
{
    int design_capacity = 1670;

    if (!get_hw_config_int("gas_gauge/capacity", &design_capacity, NULL))
        printk(KERN_ERR "[%s,%d] gas gauge capacity required in hw_configs.xml\n",__FUNCTION__,__LINE__);
    
    return sprintf(buf,"%d\n",design_capacity);
}
/*===========================================================================
  Function:       bq27510_battery_status
  Description:    get the battery status of battery
  Calls:
  Called By:
  Input:          struct bq27510_device_info *
  Output:         none
  Return:         0->"Unknown", 1->"Charging", 2->"Discharging", 3->"Not charging", 4->"Full"
  Others:         none
===========================================================================*/
int bq27510_battery_status(struct bq27510_device_info *di)
{
    int data=0;
    int status =0;
    short m_current = 0;

    if(!bq27510_is_accessible())
        return 0;

    /*Use the current direction to decide charging or discharging*/
    m_current = bq27510_battery_current(di);
    data = bq27510_i2c_read_word(di,BQ27510_REG_FLAGS);
    if(data < 0) {
        BQ27510_ERR("i2c error in reading battery status!");
        return 0;
    }
    BQ27510_DBG("read status result = %d minutes\n",data);

    if (data & BQ27510_FLAG_FC)
        status = POWER_SUPPLY_STATUS_FULL;
    else if ( (data & BQ27510_FLAG_DSG) && m_current < 0 )
        status = POWER_SUPPLY_STATUS_DISCHARGING;
    else if ( m_current > 0)
        status = POWER_SUPPLY_STATUS_CHARGING;
    else if ( 0 == m_current )
        status = POWER_SUPPLY_STATUS_NOT_CHARGING;

    return status;
}

/*===========================================================================
  Function:       bq27510_battery_health
  Description:    get the health status of battery
  Calls:
  Called By:
  Input:          struct bq27510_device_info *
  Output:         none
  Return:         0->"Unknown", 1->"Good", 2->"Overheat", 3->"Dead", 4->"Over voltage",
                  5->"Unspecified failure", 6->"Cold",
  Others:         none
===========================================================================*/
int bq27510_battery_health(struct bq27510_device_info *di)
{
    int data=0;
    int status =0;
    int battery_temperature = 0;

    if(!bq27510_is_accessible())
        return gauge_context.battery_health;

    data = bq27510_i2c_read_word(di,BQ27510_REG_FLAGS);
    if(data < 0) {
        BQ27510_ERR("i2c error in reading battery health!");
        return POWER_SUPPLY_HEALTH_GOOD;
    }

    BQ27510_DBG("read health result = %d \n",data);
    battery_temperature = bq27510_battery_temperature(di);
    if (battery_temperature < BATTERY_TEMPERATURE_COLD)
        status = POWER_SUPPLY_HEALTH_COLD;

    else if (data & (BQ27510_FLAG_OTC | BQ27510_FLAG_OTD) )
        status = POWER_SUPPLY_HEALTH_OVERHEAT;

   else
        status = POWER_SUPPLY_HEALTH_GOOD;

    gauge_context.battery_health = status;
    return status;
}

/*===========================================================================
  Function:       bq27510_battery_capacity_level
  Description:    get the capacity level of battery
  Calls:
  Called By:
  Input:          struct bq27510_device_info *
  Output:         none
  Return:         capacity percent
  Others:         none
===========================================================================*/
int bq27510_battery_capacity_level(struct bq27510_device_info *di)
{
    int data=0;
    int data_capacity = 0;
    int status =0;

    if(!bq27510_is_accessible())
       return POWER_SUPPLY_CAPACITY_LEVEL_UNKNOWN;

    data = bq27510_i2c_read_word(di,BQ27510_REG_FLAGS);
    data_capacity = bq27510_battery_capacity(di);

    BQ27510_DBG("read capactiylevel result = %d \n",data);
    if(data < 0)
        return POWER_SUPPLY_CAPACITY_LEVEL_NORMAL;

    if (data & BQ27510_FLAG_SOCF )
        status = POWER_SUPPLY_CAPACITY_LEVEL_CRITICAL;
    else if (data & BQ27510_FLAG_SOC1 )
        status = POWER_SUPPLY_CAPACITY_LEVEL_LOW;
    else if (data & BQ27510_FLAG_FC )
        status = POWER_SUPPLY_CAPACITY_LEVEL_FULL;
    else if( data_capacity > 95)
        status = POWER_SUPPLY_CAPACITY_LEVEL_HIGH;
    else
        status = POWER_SUPPLY_CAPACITY_LEVEL_NORMAL;

    return status;
}

/*===========================================================================
  Function:       bq27510_battery_technology
  Description:    get the battery technology
  Calls:
  Called By:
  Input:           struct bq27510_device_info *
  Output:         none
  Return:         value of battery technology
  Others:         none
===========================================================================*/
int bq27510_battery_technology(struct bq27510_device_info *di)
{
   /*Default technology is "Li-poly"*/
    int data = POWER_SUPPLY_TECHNOLOGY_LIPO;
    return data;
}


/*===========================================================================
  Function:       is_bq27510_battery_exist
  Description:    get the status of battery exist
  Calls:
  Called By:
  Input:          struct bq27510_device_info *
  Output:         none
  Return:         1->battery present; 0->battery not present.
  Others:         none
===========================================================================*/
int is_bq27510_battery_exist(struct bq27510_device_info *di)
{
    int data = 0;

    if(!bq27510_is_accessible())
        return gauge_context.battery_present;

    data = bq27510_i2c_read_word(di,BQ27510_REG_FLAGS);
    BQ27510_DBG("is_exist flags result = 0x%x \n",data);
    if(data < 0) {
        BQ27510_ERR("i2c error in reading battery_exist!");
        data = bq27510_i2c_read_word(di,BQ27510_REG_FLAGS);
    }

    if(data >= 0){
        gauge_context.battery_present = !!(data & BQ27510_FLAG_DET);
    } else {
        printk("i2c read BQ27510_REG_FLAGS error = %d \n",data);
    }

    return gauge_context.battery_present;
}

EXPORT_SYMBOL_GPL(bq27510_battery_temperature);
EXPORT_SYMBOL_GPL(bq27510_battery_voltage);
EXPORT_SYMBOL_GPL(bq27510_battery_current);
EXPORT_SYMBOL_GPL(bq27510_battery_tte);
EXPORT_SYMBOL_GPL(bq27510_battery_ttf);
EXPORT_SYMBOL_GPL(is_bq27510_battery_full);
EXPORT_SYMBOL_GPL(is_bq27510_battery_exist);
EXPORT_SYMBOL_GPL(bq27510_battery_status);
EXPORT_SYMBOL_GPL(bq27510_battery_health);
EXPORT_SYMBOL_GPL(bq27510_battery_capacity);
EXPORT_SYMBOL_GPL(bq27510_battery_capacity_level);
EXPORT_SYMBOL_GPL(bq27510_battery_rm);
EXPORT_SYMBOL_GPL(bq27510_battery_fcc);
EXPORT_SYMBOL_GPL(is_bq27510_battery_reach_threshold);
/*
 * Return the battery Control
 * Or < 0 if something fails
 */
#define to_bq27510_device_info(x) container_of((x), \
                  struct bq27510_device_info, bat);


/* added for Firmware upgrade begine */
static int bq27510_atoi(const char *s)
{
    int k = 0;

    k = 0;
    while (*s != '\0' && *s >= '0' && *s <= '9') {
        k = 10 * k + (*s - '0');
        s++;
    }
    return k;
}

static unsigned long bq27510_strtoul(const char *cp, unsigned int base)
{
    unsigned long result = 0,value;

    while (isxdigit(*cp) && (value = isdigit(*cp) ? *cp-'0' : (islower(*cp)
          ? toupper(*cp) : *cp)-'A'+10) < base) {
        result = result*base + value;
        cp++;
    }

    return result;
}

/*Parse the bqfs/dffs file's addr and data*/
static int bq27510_firmware_program(struct i2c_client *client, const unsigned char *pgm_data, unsigned int filelen)
{
    unsigned int i = 0, j = 0, ulDelay = 0, ulReadNum = 0;
    unsigned int ulCounter = 0, ulLineLen = 0;
    unsigned char temp = 0;
    unsigned char *p_cur;
    unsigned char pBuf[BSP_MAX_ASC_PER_LINE] = { 0 };
    unsigned char p_src[BSP_I2C_MAX_TRANSFER_LEN] = { 0 };
    unsigned char p_dst[BSP_I2C_MAX_TRANSFER_LEN] = { 0 };
    unsigned char ucTmpBuf[16] = { 0 };

bq27510_firmware_program_begin:
    if(ulCounter > 10)
    {
        return -1;
    }

    p_cur = (unsigned char *)pgm_data;

    while(1)
    {
        while (*p_cur == '\r' || *p_cur == '\n')
        {
            p_cur++;
        }

        if((p_cur - pgm_data) >= filelen)
        {
            printk("Download success\n");
            break;
        }

        i = 0;
        ulLineLen = 0;

        memset(p_src, 0x00, sizeof(p_src));
        memset(p_dst, 0x00, sizeof(p_dst));
        memset(pBuf, 0x00, sizeof(pBuf));

        /*获取一行数据，去除空格*/
        while(i < BSP_MAX_ASC_PER_LINE)
        {
            temp = *p_cur++;
            i++;
            if(('\r' == temp) || ('\n' == temp))
            {
                break;
            }
            if(' ' != temp)
            {
                pBuf[ulLineLen++] = temp;
            }
        }

        p_src[0] = pBuf[0];
        p_src[1] = pBuf[1];

        if(('W' == p_src[0]) || ('C' == p_src[0]))
        {
            for(i=2,j=0; i<ulLineLen; i+=2,j++)
            {
                memset(ucTmpBuf, 0x00, sizeof(ucTmpBuf));
                memcpy(ucTmpBuf, pBuf+i, 2);
                p_src[2+j] = bq27510_strtoul(ucTmpBuf, 16);
            }

            temp = (ulLineLen -2)/2;
            ulLineLen = temp + 2;
        }
        else if('X' == p_src[0])
        {
            memset(ucTmpBuf, 0x00, sizeof(ucTmpBuf));
            memcpy(ucTmpBuf, pBuf+2, ulLineLen-2);
            ulDelay = bq27510_atoi(ucTmpBuf);
        }
        else if('R' == p_src[0])
        {
            memset(ucTmpBuf, 0x00, sizeof(ucTmpBuf));
            memcpy(ucTmpBuf, pBuf+2, 2);
            p_src[2] = bq27510_strtoul(ucTmpBuf, 16);
            memset(ucTmpBuf, 0x00, sizeof(ucTmpBuf));
            memcpy(ucTmpBuf, pBuf+4, 2);
            p_src[3] = bq27510_strtoul(ucTmpBuf, 16);
            memset(ucTmpBuf, 0x00, sizeof(ucTmpBuf));
            memcpy(ucTmpBuf, pBuf+6, ulLineLen-6);
            ulReadNum = bq27510_atoi(ucTmpBuf);
        }

        if(':' == p_src[1])
        {
            switch(p_src[0])
            {
                case 'W' :

                    #if 0
                    printk("W: ");
                    for(i=0; i<ulLineLen-4; i++)
                    {
                        printk("%x ", p_src[4+i]);
                    }
                    printk(KERN_ERR "\n");
                    #endif

                    if(bq27510_i2c_bytes_write(client, p_src[3], &p_src[4], ulLineLen-4) < 0)
                    {
                         printk(KERN_ERR "[%s,%d] bq27510_i2c_bytes_write failed len=%d\n",__FUNCTION__,__LINE__,ulLineLen-4);
                    }

                    break;

                case 'R' :
                    if(bq27510_i2c_bytes_read(client, p_src[3], p_dst, ulReadNum) < 0)
                    {
                        printk(KERN_ERR "[%s,%d] bq27510_i2c_bytes_read failed\n",__FUNCTION__,__LINE__);
                    }
                    break;

                case 'C' :
                    if(bq27510_i2c_bytes_read_and_compare(client, p_src[3], p_dst, &p_src[4], ulLineLen-4))
                    {
                        ulCounter++;
                        printk(KERN_ERR "[%s,%d] bq27510_i2c_bytes_read_and_compare failed\n",__FUNCTION__,__LINE__);
                        goto bq27510_firmware_program_begin;
                    }
                    break;

                case 'X' :
                    msleep(ulDelay);
                    break;

                default:
                    return 0;
            }
        }

    }

    return 0;

}

static int bq27510_firmware_download(struct i2c_client *client, const unsigned char *pgm_data, unsigned int len)
{
    int iRet;

     gauge_context.state = BQ27510_UPDATE_FIRMWARE;

    /*Enter Rom Mode */
    iRet = bq27510_i2c_word_write(client, BSP_ENTER_ROM_MODE_CMD, BSP_ENTER_ROM_MODE_DATA);
    if(0 != iRet)
    {
        printk(KERN_ERR "[%s,%d] bq27510_i2c_word_write failed\n",__FUNCTION__,__LINE__);
    }
    msleep(10);


    /*change i2c addr*/
    g_battery_measure_by_bq27510_i2c_client->addr = BSP_ROM_MODE_I2C_ADDR;

    /*program bqfs*/
    iRet = bq27510_firmware_program(client, pgm_data, len);
    if(0 != iRet)
    {
        printk(KERN_ERR "[%s,%d] bq27510_firmware_program failed\n",__FUNCTION__,__LINE__);
    }

    /*change i2c addr*/
    g_battery_measure_by_bq27510_i2c_client->addr = BSP_NORMAL_MODE_I2C_ADDR;

   gauge_context.locked_timeout_jiffies =  jiffies + msecs_to_jiffies(5000);
   gauge_context.state = BQ27510_LOCK_MODE;
   if (0 != bq27510_i2c_word_write(client,BQ27510_REG_CTRL,0x0041))/* reset cmd*/
        printk(KERN_ERR "[%s,%d] write reset failed\n",__FUNCTION__,__LINE__);
   else
        printk(KERN_ERR "[%s,%d] bq27510 download reset\n",__FUNCTION__,__LINE__);

    return iRet;
}

static bool get_gas_gauge_firmware_name(char *config_name)
{
    if(get_hw_config("gas_gauge/firmware_name", config_name, GAS_GAUGE_FIRMWARE_NAME, NULL))
    {
        return true;
    }
    else
    {
        return false;
    }
}

#define ID_LEN   12
int get_gas_version_id(char * id, char * name)
{
    char *end, *start;
    char *temp;
    int i = 0;
    start = strrchr(name, '_');
    end = strchr(name, '.');
    if (start == NULL || end == NULL || (start > end) )
        return 1;
    start++;
    temp = id;
    while ((i < (ID_LEN - 1)) && (start != end)) {
        *temp++ = *start++;
        i++;
    }
    *temp = '\0';
    return 0;
}

static int bq27510_update_firmware(struct i2c_client *client, const char *pFilePath)
{
    char *buf;
    struct file *filp;
    struct inode *inode = NULL;
    mm_segment_t oldfs;
    unsigned int length;
    int ret = 0;
    struct firmware_header_entry entry;
    loff_t pos;
    ssize_t vfs_read_retval = 0;
    char config_name[GAS_GAUGE_FIRMWARE_NAME] = "unknown";
    char id[ID_LEN];
    char current_id[ID_LEN];
    int temp;
    /* open file */
    if (!get_gas_gauge_firmware_name(config_name)) {
        printk(KERN_ERR "[%s,%d] gas gauge firmware name required in hw_configs.xml\n",__FUNCTION__,__LINE__);
        return -1;
    }
    oldfs = get_fs();
    set_fs(KERNEL_DS);
    filp = filp_open(pFilePath, O_RDONLY, S_IRUSR);
    if (IS_ERR(filp))
    {
        printk(KERN_ERR "[%s,%d] filp_open failed\n",__FUNCTION__,__LINE__);
        set_fs(oldfs);
        return -1;
    }

    if (!filp->f_op)
    {
        printk(KERN_ERR "[%s,%d] File Operation Method Error\n",__FUNCTION__,__LINE__);
        filp_close(filp, NULL);
        set_fs(oldfs);
        return -1;
    }
    vfs_read_retval = filp->f_op->read(filp, (char __user *)(&temp), sizeof(int), &filp->f_pos);
    filp->f_pos = 0;

    if(vfs_read_retval != sizeof(int))
    {
        printk(KERN_ERR "[%s,%d] read file error\n",__FUNCTION__,__LINE__);
        filp_close(filp, NULL);
        set_fs(oldfs);
        return -1;
    }
    if (temp != MAGIC_NUMBER){
        printk(KERN_ERR "[%s,%d] gas gauge firmware no merged\n",__FUNCTION__,__LINE__);

        inode = filp->f_path.dentry->d_inode;
        if (!inode)
        {
            printk(KERN_ERR "[%s,%d] Get inode from filp failed\n",__FUNCTION__,__LINE__);
            filp_close(filp, NULL);
            set_fs(oldfs);
            return -1;
        }
        /* file's size */
        length = i_size_read(inode->i_mapping->host);
        entry.offset = 0;
    } else {
        while (1) {
            vfs_read_retval = filp->f_op->read(filp, (char __user *)(&entry), sizeof(struct firmware_header_entry), &filp->f_pos);
            printk("%s--- %s---0---\n", config_name, entry.file_name);
            if(vfs_read_retval != sizeof(entry)) {
                printk(KERN_ERR "[%s,%d] Get magic_number error\n",__FUNCTION__,__LINE__);
                filp_close(filp, NULL);
                set_fs(oldfs);
                return -1;
            }
            if (entry.magic_number != MAGIC_NUMBER) {
                printk(KERN_ERR "[%s,%d] Get magic_number error\n",__FUNCTION__,__LINE__);
                filp_close(filp, NULL);
                set_fs(oldfs);
                return -1;
            }
            if (strncmp(entry.file_name, config_name, strlen(config_name)) == 0) {
                length = entry.length;
                if (!get_gas_version_id(id, entry.file_name))
                    printk(KERN_ERR "gas gauge download firmware version ID = [%s] \n",id);

                sprintf(current_id, "%x", bq27510_get_firmware_version_by_i2c(client));
                printk(KERN_ERR "gas gauge curent firmware version ID = [%s] \n",current_id);
                if (strncasecmp(id, current_id, ID_LEN) == 0) {
                    printk(KERN_ERR "no need to update gas gauge firmware\n");
                    return 0;
                }
                break;
            }
            if (entry.file_name[0] == '\0') {
                printk(KERN_ERR "[%s,%d] no gas gauge firmware to download \n",__FUNCTION__,__LINE__);
                filp_close(filp, NULL);
                set_fs(oldfs);
                return -1;
            }
        }
    }
    printk("bq27510 firmware image size is %d \n",length);
    if (!( length > 0 && length < BSP_FIRMWARE_FILE_SIZE))
    {
        printk(KERN_ERR "[%s,%d] Get file size error\n",__FUNCTION__,__LINE__);
        filp_close(filp, NULL);
        set_fs(oldfs);
        return -1;
    }

    /* allocation buff size */
    buf = vmalloc(length+(length%2));       /* buf size if even */
    if (!buf)
    {
        printk(KERN_ERR "[%s,%d] Alloctation memory failed\n",__FUNCTION__,__LINE__);
        filp_close(filp, NULL);
        set_fs(oldfs);
        return -1;
    }
    pos = entry.offset;
    /* read data */
    if (filp->f_op->read(filp, buf, length, &pos) != length)
    {
        printk(KERN_ERR "[%s,%d] File read error\n",__FUNCTION__,__LINE__);
        filp_close(filp, NULL);
        filp_close(filp, NULL);
        set_fs(oldfs);
        vfree(buf);
        return -1;
    }

    ret = bq27510_firmware_download(client, (const char*)buf, length);

    filp_close(filp, NULL);
    set_fs(oldfs);
    vfree(buf);

    return ret;
}

/*Firmware upgrade sysfs store interface*/
static ssize_t bq27510_attr_store(struct device_driver *driver,const char *buf, size_t count)
{
    int iRet = 0;
    unsigned char path_image[255];

    if(NULL == buf || count >255 || count == 0 || strnchr(buf, count, 0x20))
        return -1;

    memcpy (path_image, buf,  count);
    /* replace '\n' with  '\0'  */
    if((path_image[count-1]) == '\n')
        path_image[count-1] = '\0';
    else
        path_image[count] = '\0';

    /*enter firmware bqfs download*/
    gBq27510DownloadFirmwareFlag = BSP_FIRMWARE_DOWNLOAD_MODE;
    iRet = bq27510_update_firmware(g_battery_measure_by_bq27510_i2c_client, path_image);
    gBq27510DownloadFirmwareFlag = BSP_NORMAL_MODE;

    /* added for shutdown system in charging, begin */
    /* begin: added for refresh Qmax*/
    i2c_smbus_write_word_data(g_battery_measure_by_bq27510_i2c_client,0x00,0x0021);
    /* end: added for refresh Qmax*/
    /* added for shutdown system in charging, end */
    return iRet;
}

/* Firmware upgrade sysfs show interface*/
static ssize_t bq27510_attr_show(struct device_driver *driver, char *buf)
{
    int iRet = 0;

    if(NULL == buf)
    {
        return -1;
    }

    mutex_lock(&bq27510_battery_mutex);
    i2c_smbus_write_word_data(g_battery_measure_by_bq27510_i2c_client,0x00,0x0008);
    msleep(2);
    iRet = i2c_smbus_read_word_data(g_battery_measure_by_bq27510_i2c_client,0x00);
    mutex_unlock(&bq27510_battery_mutex);
    if(iRet < 0)
    {
        return sprintf(buf, "%s", "Coulometer Damaged or Firmware Error");
    }
    else
    {
        return sprintf(buf, "%x", iRet);
    }

}

static ssize_t bq27510_show_gaugelog(struct device_driver *driver, char *buf)
{
    int temp, voltage, cur, capacity, rm , fcc, control_status,ttf,si;
    u16 flag;
    u8 qmax, qmax1;
    if(NULL == buf)
    {
        return -1;
    }


    if(!bq27510_is_accessible())
        return sprintf(buf,"bq27510 is busy because of updating(%d)",gauge_context.state);


    temp =  bq27510_battery_temperature(g_battery_measure_by_bq27510_device);

    if(BSP_NORMAL_MODE != gBq27510DownloadFirmwareFlag)
    {
        return -1;
    }

    mdelay(2);
    voltage = bq27510_battery_voltage(g_battery_measure_by_bq27510_device);
    mdelay(2);
    cur = bq27510_i2c_read_word(g_battery_measure_by_bq27510_device,BQ27510_REG_AI);
    mdelay(2);
    capacity = bq27510_i2c_read_word(g_battery_measure_by_bq27510_device,BQ27510_REG_SOC);
    mdelay(2);
    flag = bq27510_i2c_read_word(g_battery_measure_by_bq27510_device,BQ27510_REG_FLAGS);
    mdelay(2);
    rm =  bq27510_i2c_read_word(g_battery_measure_by_bq27510_device,BQ27510_REG_RM);
    mdelay(2);
    fcc =  bq27510_i2c_read_word(g_battery_measure_by_bq27510_device,BQ27510_REG_FCC);
    mdelay(2);
    ttf = bq27510_i2c_read_word(g_battery_measure_by_bq27510_device,BQ27510_REG_TTF);
    mdelay(2);
    si = bq27510_i2c_read_word(g_battery_measure_by_bq27510_device,BQ27510_REG_SI);
    mutex_lock(&bq27510_battery_mutex);
    i2c_smbus_write_word_data(g_battery_measure_by_bq27510_i2c_client,BQ27510_REG_CTRL,BQ27510_REG_CTRS);
    mdelay(2);
    control_status  = i2c_smbus_read_word_data(g_battery_measure_by_bq27510_i2c_client,BQ27510_REG_CTRL);
    mdelay(2);
    i2c_smbus_write_word_data(g_battery_measure_by_bq27510_i2c_client,BQ27510_REG_DFCLS,BQ27510_REG_CLASS_ID);
    mdelay(2);
    qmax = i2c_smbus_read_byte_data(g_battery_measure_by_bq27510_i2c_client,BQ27510_REG_QMAX);
    mdelay(2);
    qmax1 = i2c_smbus_read_byte_data(g_battery_measure_by_bq27510_i2c_client,BQ27510_REG_QMAX1);
    mdelay(2);
    mutex_unlock(&bq27510_battery_mutex);

    if(qmax < 0)
    {
        return sprintf(buf, "%s", "Coulometer Damaged or Firmware Error \n");
    }
    else
    {
      sprintf(buf, "%-9d  %-9d  %-4d  %-5d  %-6d  %-6d  %-6d  %-6d  0x%-5.4x  0x%-5.2x  0x%-5.2x  0x%-5.2x  ",
                    voltage,  (signed short)cur, capacity, rm, fcc, (signed short)ttf, (signed short)si, temp, flag, control_status, qmax, qmax1 );
        return strlen(buf);
    }
}

static int bq27510_atoh(const char *s)
{
    int k = 0;

    k = 0;
    while (*s != '\0' && ((*s >= '0' && *s <= '9' )||(*s >= 'A' && *s <= 'F' ))) {
        k<<=4;
        if(*s>='A'&& *s<='F')
            k |= 10+*s -'A';
        else k |=*s - '0';
            s++;
    }
    return k;
}


static ssize_t bq27510_debug_store(struct device_driver *driver,const char *buf, size_t count)
{
    g_battery_measure_by_bq27510_i2c_client->addr =  bq27510_atoh(buf);
    return count;
}


static ssize_t bq27510_debug_show(struct device_driver *driver, char *buf)
{
    return sprintf(buf,"bq27510 i2c addr=%x I2C error count=%d lock count=%d",g_battery_measure_by_bq27510_i2c_client->addr,gauge_context.i2c_error_count,
                    gauge_context.lock_count);
}

static DRIVER_ATTR(debug, S_IRUGO|S_IWUSR,bq27510_debug_show,bq27510_debug_store);



/*define a sysfs interface for firmware upgrade*/
static DRIVER_ATTR(state, S_IRUGO|S_IWUGO, bq27510_attr_show, bq27510_attr_store);

static DRIVER_ATTR(gaugelog, S_IRUGO|S_IWUGO, bq27510_show_gaugelog,NULL);
static DRIVER_ATTR(firmware_version, S_IRUGO|S_IWUSR, bq27510_get_firmware_version, NULL);
static DRIVER_ATTR(firmware_check, S_IRUGO|S_IWUSR, bq27510_check_firmware_version, NULL);
static DRIVER_ATTR(qmax, S_IRUGO, bq27510_check_qmax,NULL);
static DRIVER_ATTR(capacity, S_IRUGO, bq27510_get_capacity,NULL);
/* added for Firmware upgrade end */

/*
 * Use BAT_LOW not BAT_GD. When battery capacity is below SOC1, BAT_LOW PIN will pull up and cause a
 * interrput, this is the interrput callback.
 */
static irqreturn_t bq27510_abnormal_status_interrupt(int irq, void *_di)
{
    struct bq27510_device_info *di = _di;

    schedule_delayed_work(&di->notifier_work, 0);

    return IRQ_HANDLED;
}


/*===========================================================================
  Function:       interrupt_notifier_work
  Description:    send a notifier event to sysfs
  Calls:
  Called By:
  Input:          struct work_struct *
  Output:         none
  Return:         none
  Others:         none
===========================================================================*/
static void interrupt_notifier_work(struct work_struct *work)
{
    struct bq27510_device_info *di = container_of(work,
        struct bq27510_device_info, notifier_work.work);
    long int events;

    int low_bat_flag = is_bq27510_battery_reach_threshold(di);

    if(!is_bq27510_battery_exist(di) || !(low_bat_flag & BQ27510_FLAG_SOC1))
        return ;

    if(time_is_after_jiffies(di->timeout_jiffies))
    {
        di->timeout_jiffies = jiffies + msecs_to_jiffies(DISABLE_ACCESS_TIME);
        return;
    }

    if(!(low_bat_flag & BQ27510_FLAG_SOCF))
    {
        events = BATTERY_LOW_WARNING;
        //SOC1 operation:notify upper to get the level now
        dev_info(&di->client->dev,"low battery warning event\n");
    }
    else if(low_bat_flag & BQ27510_FLAG_SOCF)
    {
        events = BATTERY_LOW_SHUTDOWN;
        dev_info(&di->client->dev,"low battery shutdown event\n");
    }
    else
        return;

    di->timeout_jiffies = jiffies + msecs_to_jiffies(DISABLE_ACCESS_TIME);

    blocking_notifier_call_chain(&notifier_list, events, NULL);
    return;

}

static int bq27510_battery_probe(struct i2c_client *client,
                                 const struct i2c_device_id *id)
{
    int num = 0;
    char *name;
    int retval = 0, ret =0;
    struct bq27510_device_info *di;

    if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
        BQ27510_ERR("[%s,%d]: need I2C_FUNC_I2C\n",__FUNCTION__,__LINE__);
        return -ENODEV;
    }

    i2c_smbus_write_word_data(client,0x00,0x0008);
    msleep(2);
    retval = i2c_smbus_read_word_data(client,0x00);
    if(retval < 0) {
        dev_err(&client->dev,"[%s,%d] Coulometer Damaged or Firmware Error\n",__FUNCTION__,__LINE__);
    } else {
        dev_info(&client->dev, "Normal Mode and read Firmware version=%04x\n", retval);
    }

    retval = driver_create_file(&(bq27510_battery_driver.driver), &driver_attr_state);
    if (0 != retval) {
        printk("failed to create sysfs entry(state): %d\n", retval);
        return -1;
    }


    retval = driver_create_file(&(bq27510_battery_driver.driver), &driver_attr_gaugelog);
    if (0 != retval) {
        printk("failed to create sysfs entry(gaugelog): %d\n", retval);
        return -1;
    }

    retval = driver_create_file(&(bq27510_battery_driver.driver), &driver_attr_debug);
    if(0 != retval)
        printk("failed to create sysfs entry(debug): %d\n", retval);

    retval = driver_create_file(&(bq27510_battery_driver.driver), &driver_attr_firmware_version);
    if (0 != retval) {
        printk("failed to create sysfs entry(firmware_version): %d\n", retval);
        return -1;
    }

    retval = driver_create_file(&(bq27510_battery_driver.driver), &driver_attr_firmware_check);
    if (0 != retval) {
        printk("failed to create sysfs entry(firmware_check): %d\n", retval);
        return -1;
    }

    retval = driver_create_file(&(bq27510_battery_driver.driver), &driver_attr_qmax);
    if (0 != retval) {
        printk("failed to create sysfs entry(firmware_check): %d\n", retval);
        return -1;
    }

    retval = driver_create_file(&(bq27510_battery_driver.driver), &driver_attr_capacity);
    if (0 != retval) {
        printk("failed to create sysfs entry(capacity): %d\n", retval);
        return -1;
    }    

    /* Get new ID for the new battery device */
    retval = idr_pre_get(&bq27510_battery_id, GFP_KERNEL);
    if (retval == 0) {
        BQ27510_ERR("bq27510 idr_pre_get failed!!\n");
        retval = -ENOMEM;
        goto batt_failed_0;
    }

    mutex_lock(&bq27510_battery_mutex);
    retval = idr_get_new(&bq27510_battery_id, client, &num);
    mutex_unlock(&bq27510_battery_mutex);
    if (retval < 0) {
        BQ27510_ERR("bq27510 idr_get_new failed!!\n");
        goto batt_failed_0;
    }

    name = kasprintf(GFP_KERNEL, "bq27510-%d", num);
    if (!name) {
        dev_err(&client->dev, "failed to allocate device name\n");
        retval = -ENOMEM;
        goto batt_failed_1;
    }

    di = kzalloc(sizeof(*di), GFP_KERNEL);
    if (!di) {
        dev_err(&client->dev, "failed to allocate device info data\n");
        retval = -ENOMEM;
        goto batt_failed_2;
    }

    di->id = num;
    i2c_set_clientdata(client, di);
    di->dev = &client->dev;
    di->bat.name = name;
    di->client = client;

    di->timeout_jiffies = 0;//jiffies + msecs_to_jiffies(DISABLE_ACCESS_TIME);// Maybe lost interrupts in 2 seconds after power on,Zheng

	//bq27510_powersupply_init(di);

    INIT_DELAYED_WORK_DEFERRABLE(&di->notifier_work,interrupt_notifier_work);



    /* request battery_low interruption */
    ret = request_irq(client->irq, bq27510_abnormal_status_interrupt, IRQF_TRIGGER_FALLING,
                      "bq27510_irq_ctrl", di);

    if (ret) {
        pr_err("could not request irq %d, status %d\n", client->irq, ret);
        goto batt_failed_0;
    }else{
        enable_irq_wake(client->irq);
    }

    g_battery_measure_by_bq27510_i2c_client = client;
    g_battery_measure_by_bq27510_device = di;




    dev_info(&client->dev, "bq27510 support ver. %s enabled\n", DRIVER_VERSION);

    return 0;

batt_failed_2:
    kfree(name);
batt_failed_1:
    mutex_lock(&bq27510_battery_mutex);
    idr_remove(&bq27510_battery_id, num);
    mutex_unlock(&bq27510_battery_mutex);
batt_failed_0:

	//power_set_batt_measurement_type(BATT_MEASURE_UNKNOW);		//wxx_debug_for_battery

    return retval;

}

static int bq27510_battery_remove(struct i2c_client *client)
{
    struct bq27510_device_info *di = i2c_get_clientdata(client);

    free_irq(di->client->irq,di);
	//power_supply_unregister(&di->bat);
    kfree(di->bat.name);
    g_battery_measure_by_bq27510_i2c_client = NULL;
    cancel_delayed_work(&di->notifier_work);

    mutex_lock(&bq27510_battery_mutex);
    idr_remove(&bq27510_battery_id, di->id);
    mutex_unlock(&bq27510_battery_mutex);

    kfree(di);
    return 0;
}

/*
 * Module stuff
 */

static const struct i2c_device_id bq27510_id[] = {
    {"bq27510",0},
    {},
};

static struct i2c_driver bq27510_battery_driver = {
    .driver = {
        .name = "bq27510-battery",
    },
    .probe = bq27510_battery_probe,
    .remove = bq27510_battery_remove,
    .id_table = bq27510_id,
};

static int __init bq27510_battery_init(void)
{
    int ret = 0;

    ret = i2c_add_driver(&bq27510_battery_driver);
    if (ret)
    BQ27510_ERR("Unable to register BQ27510 driver\n");

    return ret;
}

module_init(bq27510_battery_init);

static void __exit bq27510_battery_exit(void)
{
    i2c_del_driver(&bq27510_battery_driver);
}
module_exit(bq27510_battery_exit);

MODULE_AUTHOR("Rodolfo Giometti <giometti@linux.it>");
MODULE_DESCRIPTION("BQ27510 battery monitor driver");
MODULE_LICENSE("GPL");

