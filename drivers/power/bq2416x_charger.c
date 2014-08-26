/*
 * drivers/power/bq2416x_battery.c
 *
 * BQ24160/2 / BQ24161 battery charging driver
 *
 * Copyright (C) 2010 Texas Instruments, Inc.
 * Author: Texas Instruments, Inc.
 *
 * This package is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * THIS PACKAGE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED
 * WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/delay.h>
#include <linux/jiffies.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/i2c/twl.h>

#include <linux/i2c/bq27510_battery.h>
#include <linux/i2c/bq2416x.h>
#include <linux/usb/otg.h>
#include <linux/io.h>
#include <linux/gpio.h>
#include <hsad/config_interface.h>

#define BQ2416X_USE_WAKE_LOCK  1

#define DRIVER_NAME			"bq2416x"

struct charge_params {
    unsigned long       currentmA;
    unsigned long       voltagemV;
    unsigned long       term_currentmA;
    unsigned long       enable_iterm;
    bool                enable;
};

struct bq2416x_device_info {
    struct device        *dev;
    struct i2c_client    *client;
    struct charge_params  params;
    struct delayed_work   bq2416x_charger_work;
    struct work_struct    usb_work;

	unsigned short		status_reg00;
	unsigned short		battery_supply_status_reg01;
	unsigned short		control_reg02;
	unsigned short		voltage_reg03;
	unsigned short		bqchip_version;
	unsigned short		current_reg05;
	unsigned short	    dppm_reg06;
    unsigned short		safety_timer_reg07;

	unsigned int		cin_limit;
	unsigned int		currentmA;
	unsigned int		voltagemV;
	unsigned int		max_currentmA;
	unsigned int		max_voltagemV;
    unsigned int        max_cin_currentmA;
	unsigned int		term_currentmA;
	unsigned int        dppm_voltagemV;
    unsigned int        battery_temp_status;
    unsigned int		supply_sel;
    unsigned int		safety_timer;
    bool                en_nobatop;
	bool			    enable_iterm;
	bool                enable_ce;  
	bool                hz_mode;
    bool                enable_low_chg;
    bool                factory_flag;
    bool                calling_limit;
    bool                battery_present;
    bool                wakelock_enabled;   
	bool                cd_active;
	bool			    cfg_params;

	int			        charge_status;
	int			        charger_source;
    int                 timer_fault;
   
	unsigned long		event;
	struct otg_transceiver	*otg;
	struct notifier_block	nb_otg;

	struct wake_lock charger_wake_lock;
};

/*exterm a bq27510 instance for reading battery info*/
extern struct bq27510_device_info *g_battery_measure_by_bq27510_device;

/*extern a notifier list for charging notification*/
extern struct blocking_notifier_head notifier_list;

extern u32 wakeup_timer_seconds;

enum{
    BATTERY_HEALTH_TEMPERATURE_NORMAL = 0,
    BATTERY_HEALTH_TEMPERATURE_OVERLOW,
    BATTERY_HEALTH_TEMPERATURE_LOW,
    BATTERY_HEALTH_TEMPERATURE_NORMAL_HIGH,
    BATTERY_HEALTH_TEMPERATURE_HIGH,
    BATTERY_HEALTH_TEMPERATURE_OVERHIGH,
};


/**
 * bq2416x_write_block:
 * returns 0 if write successfully
 * This is API to check whether OMAP is waking up from device OFF mode.
 * There is no other status bit available for SW to read whether last state
 * entered was device OFF. To work around this, CORE PD, RFF context state
 * is used which is lost only when we hit device OFF state
 */
static int bq2416x_write_block(struct bq2416x_device_info *di, u8 *value,
						u8 reg, unsigned num_bytes)
{
	struct i2c_msg msg[1];
	int ret;

	*value		= reg;

	msg[0].addr	= di->client->addr;
	msg[0].flags	= 0;
	msg[0].buf	= value;
	msg[0].len	= num_bytes + 1;
	
	ret = i2c_transfer(di->client->adapter, msg, 1);

	/* i2c_transfer returns number of messages transferred */
	if (ret != 1) {
		dev_err(di->dev,
			"i2c_write failed to transfer all messages\n");
		if (ret < 0)
			return ret;
		else
			return -EIO;
	} else {
		return 0;
	}
}

static int bq2416x_read_block(struct bq2416x_device_info *di, u8 *value,
						u8 reg, unsigned num_bytes)
{
	struct i2c_msg msg[2];
	u8 buf;
	int ret;

	buf		= reg;

	msg[0].addr	= di->client->addr;
	msg[0].flags	= 0;
	msg[0].buf	= &buf;
	msg[0].len	= 1;

	msg[1].addr	= di->client->addr;
	msg[1].flags	= I2C_M_RD;
	msg[1].buf	= value;
	msg[1].len	= num_bytes;

	ret = i2c_transfer(di->client->adapter, msg, 2);

	/* i2c_transfer returns number of messages transferred */
	if (ret != 2) {
		dev_err(di->dev,
			"i2c_write failed to transfer all messages\n");
		if (ret < 0)
			return ret;
		else
			return -EIO;
	} else {
		return 0;
	}
}

static int bq2416x_write_byte(struct bq2416x_device_info *di, u8 value, u8 reg)
{
	/* 2 bytes offset 1 contains the data offset 0 is used by i2c_write */
	u8 temp_buffer[2] = { 0 };

	/* offset 1 contains the data */
	temp_buffer[1] = value;
	return bq2416x_write_block(di, temp_buffer, reg, 1);
}

static int bq2416x_read_byte(struct bq2416x_device_info *di, u8 *value, u8 reg)
{
	return bq2416x_read_block(di, value, reg, 1);
}

/*
 *config TMR_RST function to reset the watchdog
 * 
 */
static void bq2416x_config_watchdog_reg(struct bq2416x_device_info *di) 
{
    di->status_reg00 = (TMR_RST) | di->supply_sel; 
	bq2416x_write_byte(di, di->status_reg00, REG_STATUS_CONTROL_REG00);
	return;
}

/*
 *Enable STAT pin output to show charge status
 * 
 */
static void bq2416x_config_status_reg(struct bq2416x_device_info *di)
{
	bq2416x_config_watchdog_reg(di);
	bq2416x_write_byte(di, di->control_reg02, REG_CONTROL_REGISTER_REG02);
	return;
}

/*
 *di->cin_limit:      set usb input limit current(100,150,500,800,900,1500)
 *di->enable_iterm:   Enable charge current termination
 *ENABLE_STAT_PIN: Enable STAT pin output to show charge status
 * di->enable_ce=0 : Charger enabled
 */
static void bq2416x_config_control_reg(struct bq2416x_device_info *di)
{
	u8 Iin_limit;

	if (di->cin_limit <= IINLIM_100)
		Iin_limit = 0;
	else if (di->cin_limit > IINLIM_100 && di->cin_limit <= IINLIM_150)
		Iin_limit = 1;
	else if (di->cin_limit > IINLIM_150 && di->cin_limit <= IINLIM_500)
		Iin_limit = 2;
	else if (di->cin_limit > IINLIM_500 && di->cin_limit <= IINLIM_800)
		Iin_limit = 3;
	else if (di->cin_limit > IINLIM_800 && di->cin_limit <= IINLIM_900)
		Iin_limit = 4;	
	else if (di->cin_limit > IINLIM_900 && di->cin_limit <= IINLIM_1500)
		Iin_limit = 5;
	else
		Iin_limit = 6;

	di->control_reg02 = ((Iin_limit << BQ2416x_INPUT_CURRENT_LIMIT_SHIFT)
				| (di->enable_iterm << BQ2416x_EN_ITERM_SHIFT) | (ENABLE_STAT) 
				|( di->enable_ce << BQ2416x_EN_CE_SHIFT) |di->hz_mode);
	bq2416x_write_byte(di, di->control_reg02, REG_CONTROL_REGISTER_REG02);
	return;
}

/*
 * set Battery Regulation Voltage between 3.5V and 4.44V
 *
 */
static void bq2416x_config_voltage_reg(struct bq2416x_device_info *di)
{
	unsigned int voltagemV;
	u8 Voreg;

	voltagemV = di->voltagemV;
	if (voltagemV < VCHARGE_MIN_3500)
		voltagemV = VCHARGE_MIN_3500;
	else if (voltagemV > VCHARGE_MAX_4440)
		voltagemV = VCHARGE_MAX_4440;

	Voreg = (voltagemV - VCHARGE_MIN_3500)/VCHARGE_STEP_20;

	di->voltage_reg03 = (Voreg << BQ2416x_VCHARGE_SHIFT);
	bq2416x_write_byte(di, di->voltage_reg03, REG_BATTERY_VOLTAGE_REG03);
	return;
}

/*
 * set Battery charger current(550 ~1500mA) and Termination current(50~350mA)
 *
 */
static void bq2416x_config_current_reg(struct bq2416x_device_info *di)
{
	unsigned int currentmA = 0;
	unsigned int term_currentmA = 0;
	u8 Vichrg = 0;
	u8 shift = 0;
	u8 Viterm = 0;

	currentmA = di->currentmA;
	term_currentmA = di->term_currentmA;

	if (currentmA < ICHG_MIN_550)
		currentmA = ICHG_MIN_550;
  
	if ((di->bqchip_version & BQ24161)) {
		shift = BQ2416x_CURRENT_SHIFT;
		if (currentmA > ICHG_1500)
			currentmA = ICHG_1500;
	}

	if (term_currentmA < ITERM_MIN_50)
		term_currentmA = ITERM_MIN_50;

	if (term_currentmA > ITERM_MAX_400)
		term_currentmA = ITERM_MAX_400;

	Vichrg = (currentmA - ICHG_MIN_550)/ICHG_STEP_75;

	Viterm = (term_currentmA - ITERM_MIN_50)/ITERM_STEP_50;

	di->current_reg05 = (Vichrg << shift | Viterm);
	bq2416x_write_byte(di, di->current_reg05, REG_BATTERY_CURRENT_REG05);
	
	return;
}

/*
 * set USB input dppm voltage between 4.2V and 4.76V 
 *
 */
static void bq2416x_config_dppm_voltage_reg(struct bq2416x_device_info *di,
	                             unsigned int dppm_voltagemV) 
{
      u8 Vmreg;

	if (dppm_voltagemV < VINDPM_MIN_4200)
		dppm_voltagemV = VINDPM_MIN_4200;
	else if (dppm_voltagemV > VINDPM_MAX_4760)
		dppm_voltagemV = VINDPM_MAX_4760;
	
	Vmreg = (dppm_voltagemV - VINDPM_MIN_4200)/VINDPM_STEP_80;
	
	di->dppm_reg06 =(Vmreg << BQ2416x_USB_INPUT_DPPM_SHIFT);
	bq2416x_write_byte(di, di->dppm_reg06, REG_DPPM_VOLTAGE_REG06);
	return;
}

/*
 * enable TMR_PIN and set Safety Timer Time Limit = 9h
 *
 */
static void bq2416x_config_safety_reg(struct bq2416x_device_info *di)
{

	di->safety_timer_reg07 =  TMR_EN | di->safety_timer | di->enable_low_chg; 

	bq2416x_write_byte(di, di->safety_timer_reg07, REG_SAFETY_TIMER_REG07);
	return;
}

static void bq2416x_open_inner_fet(struct bq2416x_device_info *di)
{
    u8 en_nobatop = 0;
		
    bq2416x_read_byte(di, &en_nobatop, REG_BATTERY_AND_SUPPLY_STATUS_REG01);

    if(di->battery_present){
        di->enable_iterm = ENABLE_ITERM;
        en_nobatop = en_nobatop & (~EN_NOBATOP);
    }else {
        di->enable_iterm = DISABLE_ITERM;
        en_nobatop = en_nobatop | EN_NOBATOP;
    }

    bq2416x_config_control_reg(di);
    bq2416x_write_byte(di, en_nobatop, REG_BATTERY_AND_SUPPLY_STATUS_REG01);
}

static int bq2416x_check_battery_temperature_threshold(void)
{
    int battery_temperature = 0;

    battery_temperature = bq27510_battery_temperature(g_battery_measure_by_bq27510_device);
    //battery_temperature = battery_temperature/TEMPERATURE_MULTIPLE;
    if (battery_temperature < BQ2416x_COLD_BATTERY_THRESHOLD) {
        return BATTERY_HEALTH_TEMPERATURE_OVERLOW;

    } else if((battery_temperature >= BQ2416x_COLD_BATTERY_THRESHOLD)
        && (battery_temperature <  BQ2416x_COOL_BATTERY_THRESHOLD)){
        return BATTERY_HEALTH_TEMPERATURE_LOW;

    } else if ((battery_temperature >= BQ2416x_COOL_BATTERY_THRESHOLD)
        && (battery_temperature < (BQ2416x_WARM_BATTERY_THRESHOLD - BQ2416x_TEMPERATURE_OFFSET))){
       return BATTERY_HEALTH_TEMPERATURE_NORMAL;

    } else if ((battery_temperature >= (BQ2416x_WARM_BATTERY_THRESHOLD - BQ2416x_TEMPERATURE_OFFSET))
        && (battery_temperature < BQ2416x_WARM_BATTERY_THRESHOLD)){
        return BATTERY_HEALTH_TEMPERATURE_NORMAL_HIGH;

    } else if ((battery_temperature >= BQ2416x_WARM_BATTERY_THRESHOLD)
        && (battery_temperature < BQ2416x_HOT_BATTERY_THRESHOLD)){
        return BATTERY_HEALTH_TEMPERATURE_HIGH;

    } else if (battery_temperature >= BQ2416x_HOT_BATTERY_THRESHOLD){
       return BATTERY_HEALTH_TEMPERATURE_OVERHIGH;

    } else {
       return BATTERY_HEALTH_TEMPERATURE_NORMAL;
    }
}

/*0 = temperature less than 37,1 = temperature more than 40 */
static void bq2416x_calling_limit_ac_input_current(struct bq2416x_device_info *di,int flag)
{
    if (di->charger_source == POWER_SUPPLY_TYPE_MAINS)
    {
       switch(flag){
       case 0:
           if(di->calling_limit){
               di->cin_limit = IINLIM_800;
           } else {
               di->cin_limit = di->max_cin_currentmA;
           }
           break;
       case 1:
           di->cin_limit = IINLIM_800;
           break;
        }
    }
   return;
}
static void bq2416x_monitor_battery_ntc_charging(struct bq2416x_device_info *di)
{
    int battery_voltage = 0;
    int battery_status = 0;
    long int events = BQ2416x_START_CHARGING;
    if(!di->battery_present){
        blocking_notifier_call_chain(&notifier_list, BQ2416x_NOT_CHARGING, NULL);
        return;
    }
    battery_voltage = bq27510_battery_voltage(g_battery_measure_by_bq27510_device);
    //battery_voltage = battery_voltage/VOLTAGE_MULTIPLE;
    battery_status = bq2416x_check_battery_temperature_threshold();

    switch (battery_status) {
    case BATTERY_HEALTH_TEMPERATURE_OVERLOW:
         di->enable_ce = DISABLE_CHARGER;
        break;
    case BATTERY_HEALTH_TEMPERATURE_LOW:
         if(battery_voltage > BQ2416x_LOW_TEMP_TERM_VOLTAGE){
              di->enable_ce = DISABLE_CHARGER;
         }else{
             di->enable_ce = ENABLE_CHARGER;
         }
         di->enable_low_chg = ENABLE_LOW_CHG;
         
        break;
    case BATTERY_HEALTH_TEMPERATURE_NORMAL:
        di->enable_ce = ENABLE_CHARGER;
        di->enable_low_chg = DISABLE_LOW_CHG;
        if (di->charger_source == POWER_SUPPLY_TYPE_MAINS){
            bq2416x_calling_limit_ac_input_current(di,0);
        }
        break;
    case BATTERY_HEALTH_TEMPERATURE_NORMAL_HIGH:
        di->enable_ce = ENABLE_CHARGER;
        di->enable_low_chg = DISABLE_LOW_CHG;
        if (di->charger_source == POWER_SUPPLY_TYPE_MAINS){
            if(di->battery_temp_status == BATTERY_HEALTH_TEMPERATURE_NORMAL){
                bq2416x_calling_limit_ac_input_current(di,0);
            }else{    
                di->cin_limit = di->cin_limit;
            }
        }
        break;
    case BATTERY_HEALTH_TEMPERATURE_HIGH:
        di->enable_ce = ENABLE_CHARGER;
        di->enable_low_chg = DISABLE_LOW_CHG;
        if (di->charger_source == POWER_SUPPLY_TYPE_MAINS){
            di->cin_limit = IINLIM_800;
        }
        break;
    case BATTERY_HEALTH_TEMPERATURE_OVERHIGH:
        di->enable_ce = DISABLE_CHARGER;
        di->enable_low_chg = DISABLE_LOW_CHG;
        if (di->charger_source == POWER_SUPPLY_TYPE_MAINS){
            di->cin_limit = IINLIM_800;
        }
        break;
    default:
        break;
    }
    di->enable_ce = di->enable_ce | di->factory_flag;
    if(!di->enable_ce){
        events = BQ2416x_START_CHARGING;
    }else{
        events = BQ2416x_NOT_CHARGING;
    }
    bq2416x_config_control_reg(di);
    bq2416x_config_safety_reg(di); 
    di->battery_temp_status = battery_status;
    blocking_notifier_call_chain(&notifier_list, events, NULL);
    return;
}

static void bq2416x_start_usb_charger(struct bq2416x_device_info *di)
{
    long int  events = BQ2416x_START_USB_CHARGING;

      /*set gpio_174 low level for CD pin to enable bq24161 IC*/
	gpio_set_value(ENABLE_BQ2416x_CHARGER, 0);

	blocking_notifier_call_chain(&notifier_list, events, NULL);   
	di->charger_source = POWER_SUPPLY_TYPE_USB;
	di->charge_status = POWER_SUPPLY_STATUS_CHARGING;
	di->calling_limit = 0;
    di->factory_flag = 0;

    di->battery_temp_status = -1;

	di->dppm_voltagemV = VINDPM_4520;
	di->cin_limit = IINLIM_500;
	di->currentmA = ICHG_MIN_550;
    di->voltagemV = di->max_voltagemV;
	di->enable_ce = ENABLE_CHARGER;  /*enable charger*/
	di->enable_iterm = ENABLE_ITERM; /*enable charge current termination*/
    di->hz_mode = DIS_HIZ;
    di->safety_timer = TMR_X_9;
    di->enable_low_chg = DISABLE_LOW_CHG;

	bq2416x_config_control_reg(di);
	bq2416x_config_voltage_reg(di);
	bq2416x_config_current_reg(di);
    bq2416x_config_dppm_voltage_reg(di,di->dppm_voltagemV);
    bq2416x_config_watchdog_reg(di);
		
	schedule_delayed_work(&di->bq2416x_charger_work,msecs_to_jiffies(0));

	dev_info(di->dev,"%s, ---->START USB CHARGING, \n"
	                  "battery current = %d mA\n"
	                  "battery voltage = %d mV\n"
	                  , __func__, di->currentmA, di->voltagemV);

    di->battery_present = is_bq27510_battery_exist(g_battery_measure_by_bq27510_device);
	if (!di->battery_present){
		dev_info(di->dev, "BATTERY NOT DETECTED!\n");
		events = BQ2416x_NOT_CHARGING;
		blocking_notifier_call_chain(&notifier_list, events, NULL);
		di->charge_status = POWER_SUPPLY_STATUS_NOT_CHARGING;
	}
    di->wakelock_enabled = 0;
    if (di->wakelock_enabled)
        wake_lock(&di->charger_wake_lock);
	return;
}

static void bq2416x_start_ac_charger(struct bq2416x_device_info *di)
{
   long int  events = BQ2416x_START_AC_CHARGING;
	   
   /*set gpio_174 low level for CD pin to enable bq24161 IC*/
	gpio_set_value(ENABLE_BQ2416x_CHARGER, 0);

	blocking_notifier_call_chain(&notifier_list, events, NULL);

	di->charger_source = POWER_SUPPLY_TYPE_MAINS;
	di->charge_status = POWER_SUPPLY_STATUS_CHARGING;
	di->calling_limit = 0;
    di->factory_flag = 0;

    di->battery_temp_status = -1;

	di->dppm_voltagemV = VINDPM_MIN_4200;
	di->cin_limit = di->max_cin_currentmA;
	di->currentmA = di->max_currentmA ;
    di->voltagemV = di->max_voltagemV;
    di->term_currentmA = ITERM_MIN_50;
	di->enable_ce = ENABLE_CHARGER;     /*enable charger*/
	di->enable_iterm = ENABLE_ITERM;    /*enable charge current termination*/
    di->hz_mode = DIS_HIZ;
    di->safety_timer = TMR_X_9;
    di->enable_low_chg = DISABLE_LOW_CHG;

	bq2416x_config_control_reg(di);
	bq2416x_config_voltage_reg(di);
	bq2416x_config_current_reg(di);
    bq2416x_config_dppm_voltage_reg(di,di->dppm_voltagemV);
    bq2416x_config_safety_reg(di);
    bq2416x_config_watchdog_reg(di);

	schedule_delayed_work(&di->bq2416x_charger_work,
						msecs_to_jiffies(0));

	dev_info(di->dev,"%s, ---->START AC CHARGING, \n"
	                  "battery current = %d mA\n"
	                  "battery voltage = %d mV\n"
	                  , __func__, di->currentmA, di->voltagemV);
    di->battery_present = is_bq27510_battery_exist(g_battery_measure_by_bq27510_device);
	if (!di->battery_present){
		dev_info(di->dev, "BATTERY NOT DETECTED!\n");
		events = BQ2416x_NOT_CHARGING;
		blocking_notifier_call_chain(&notifier_list, events, NULL);
		di->charge_status = POWER_SUPPLY_STATUS_NOT_CHARGING;
	}
    di->wakelock_enabled = 1;
    if (di->wakelock_enabled)
        wake_lock(&di->charger_wake_lock);
	return;
}

static void bq2416x_stop_charger(struct bq2416x_device_info *di)
{
       long int  events = BQ2416x_STOP_CHARGING;

	dev_info(di->dev,"%s,---->STOP CHARGING\n", __func__);

	di->calling_limit = 0;
	di->factory_flag = 0;

	di->charger_source = POWER_SUPPLY_TYPE_BATTERY;
	di->charge_status = POWER_SUPPLY_STATUS_DISCHARGING;

    wakeup_timer_seconds = 0;

	cancel_delayed_work_sync(&di->bq2416x_charger_work);

	blocking_notifier_call_chain(&notifier_list, events, NULL);

	/*set gpio_174 high level for CD pin to disable bq24161 IC */
	gpio_set_value(ENABLE_BQ2416x_CHARGER, 1);

    //di->wakelock_enabled = 1;
    if (di->wakelock_enabled)
        wake_unlock(&di->charger_wake_lock);
    di->wakelock_enabled = 0;
    return;
}



static void
bq2416x_charger_update_status(struct bq2416x_device_info *di)
{
	u8 read_reg[8] = {0};
    long int events=BQ2416x_START_CHARGING;
    static int battery_capacity = 0;

	di->timer_fault = 0;
	bq2416x_read_block(di, &read_reg[0], 0, 8); 

	if((read_reg[1] & BQ2416x_FAULT_VBUS_VUVLO) == BQ2416x_FAULT_VBUS_OVP){
		dev_err(di->dev, "bq2416x charger over voltage = %x\n",read_reg[1]);
		events = POWER_SUPPLY_OVERVOLTAGE;
		blocking_notifier_call_chain(&notifier_list, events, NULL);
	}
   
	if ((read_reg[0] & BQ2416x_CHARGE_FAULT) == BQ2416x_STATUS_CHARGE_DONE){   
		dev_dbg(di->dev, "CHARGE DONE\n");
        battery_capacity = bq27510_battery_capacity(g_battery_measure_by_bq27510_device);
        if (((!is_bq27510_battery_full(g_battery_measure_by_bq27510_device))||(battery_capacity!=100))&& (di->battery_present)){
            dev_info(di->dev, "charge_done_battery_capacity=%d\n",battery_capacity);
            di->hz_mode = EN_HIZ;
			bq2416x_write_byte(di, di->control_reg02 | di->hz_mode, REG_CONTROL_REGISTER_REG02);
            msleep(500);
			di->hz_mode = DIS_HIZ;
            bq2416x_config_control_reg(di);
			events = BQ2416x_START_CHARGING;           
        }else{
            events = BQ2416x_CHARGE_DONE;
        }
        blocking_notifier_call_chain(&notifier_list, events, NULL);
    }

	if ((read_reg[0] & BQ2416x_FAULT_BATTERY) == BQ2416x_FAULT_WATCHDOG_TIMER){ 
		di->timer_fault = 1;
     }

	if ((read_reg[0] & BQ2416x_FAULT_BATTERY) == BQ2416x_FAULT_SAFETY_TIMER) 
		di->timer_fault = 1;
     
	if (read_reg[0] & BQ2416x_FAULT_BATTERY) {
		di->cfg_params = 1;
		dev_err(di->dev, "CHARGER STATUS %x\n", read_reg[0]);
	}

	if ((read_reg[1] & 0x6) == BQ2416x_FAULT_BAT_OVP) {
        gpio_set_value(ENABLE_BQ2416x_CHARGER, 1);
		dev_err(di->dev, "battery ovp = %x,%x\n", read_reg[1],read_reg[3]);
        msleep(1000);
        gpio_set_value(ENABLE_BQ2416x_CHARGER, 0);
	}

	if ((di->timer_fault == 1) || (di->cfg_params == 1)) {
		bq2416x_write_byte(di, di->control_reg02, REG_CONTROL_REGISTER_REG02);
		bq2416x_write_byte(di, di->voltage_reg03, REG_BATTERY_VOLTAGE_REG03);
		bq2416x_write_byte(di, di->current_reg05, REG_BATTERY_CURRENT_REG05);
		bq2416x_write_byte(di, di->dppm_reg06, REG_DPPM_VOLTAGE_REG06);
		bq2416x_config_safety_reg(di); 
		di->cfg_params = 0;
	}

	/* reset 32 second timer */
	bq2416x_config_status_reg(di);

	return;
}

static void bq2416x_charger_work(struct work_struct *work)
{
	struct bq2416x_device_info *di = container_of(work,
		struct bq2416x_device_info, bq2416x_charger_work.work);

    di->battery_present = is_bq27510_battery_exist(g_battery_measure_by_bq27510_device);

	bq2416x_open_inner_fet(di);

    bq2416x_monitor_battery_ntc_charging(di);

	bq2416x_charger_update_status(di);

	schedule_delayed_work(&di->bq2416x_charger_work,
						msecs_to_jiffies(BQ2416x_WATCHDOG_TIMEOUT));

}

static ssize_t bq2416x_set_enable_itermination(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t count)
{
	long val;
	int status = count;
	struct bq2416x_device_info *di = dev_get_drvdata(dev);

	if ((strict_strtol(buf, 10, &val) < 0) || (val < 0) || (val > 1))
		return -EINVAL;
	di->enable_iterm = val;
	bq2416x_config_control_reg(di);

	return status;
}

static ssize_t bq2416x_show_enable_itermination(struct device *dev,
				  struct device_attribute *attr,
				  char *buf)
{
	unsigned long val;
	struct bq2416x_device_info *di = dev_get_drvdata(dev);

	val = di->enable_iterm;
	return sprintf(buf, "%lu\n", val);
}

static ssize_t bq2416x_set_cin_limit(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t count)
{
	long val;
	int status = count;
	struct bq2416x_device_info *di = dev_get_drvdata(dev);

	if ((strict_strtol(buf, 10, &val) < 0) || (val < IINLIM_100)
					|| (val > IINLIM_1500))
		return -EINVAL;
	di->cin_limit = val;
	bq2416x_config_control_reg(di);

	return status;
}

static ssize_t bq2416x_show_cin_limit(struct device *dev,
				  struct device_attribute *attr,
				  char *buf)
{
	unsigned long val;
	struct bq2416x_device_info *di = dev_get_drvdata(dev);

	val = di->cin_limit;
	return sprintf(buf, "%lu\n", val);
}

static ssize_t bq2416x_set_regulation_voltage(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t count)
{
	long val;
	int status = count;
	struct bq2416x_device_info *di = dev_get_drvdata(dev);

	if ((strict_strtol(buf, 10, &val) < 0) || (val < VCHARGE_MIN_3500)
					|| (val > VCHARGE_MAX_4440))
		return -EINVAL;
	di->voltagemV = val;
	bq2416x_config_voltage_reg(di);

	return status;
}

static ssize_t bq2416x_show_regulation_voltage(struct device *dev,
				  struct device_attribute *attr,
				  char *buf)
{
	unsigned long val;
	struct bq2416x_device_info *di = dev_get_drvdata(dev);

	val = di->voltagemV;
	return sprintf(buf, "%lu\n", val);
}

static ssize_t bq2416x_set_charge_current(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t count)
{
	long val;
	int status = count;
	struct bq2416x_device_info *di = dev_get_drvdata(dev);

	if ((strict_strtol(buf, 10, &val) < 0) || (val < ICHG_MIN_550)
					|| (val > ICHG_2500))
		return -EINVAL;
	di->currentmA = val;
	bq2416x_config_current_reg(di);

	return status;
}

static ssize_t bq2416x_show_charge_current(struct device *dev,
				  struct device_attribute *attr,
				  char *buf)
{
	unsigned long val;
	struct bq2416x_device_info *di = dev_get_drvdata(dev);

	val = di->currentmA;
	return sprintf(buf, "%lu\n", val);
}

static ssize_t bq2416x_set_termination_current(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t count)
{
	long val;
	int status = count;
	struct bq2416x_device_info *di = dev_get_drvdata(dev);

	if ((strict_strtol(buf, 10, &val) < 0) || (val < ITERM_MIN_50) || (val > ITERM_MAX_400))
		return -EINVAL;
	di->term_currentmA = val;
	bq2416x_config_current_reg(di);

	return status;
}

static ssize_t bq2416x_show_termination_current(struct device *dev,
				  struct device_attribute *attr,
				  char *buf)
{
	unsigned long val;
	struct bq2416x_device_info *di = dev_get_drvdata(dev);

	val = di->term_currentmA;
	return sprintf(buf, "%lu\n", val);
}

static ssize_t bq2416x_set_dppm_voltage(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t count)
{
	long val;
	int status = count;
	struct bq2416x_device_info *di = dev_get_drvdata(dev);

	if ((strict_strtol(buf, 10, &val) < 0) || (val < VINDPM_MIN_4200) || (val > VINDPM_MAX_4760))
		return -EINVAL;

	di->dppm_voltagemV = val;
	bq2416x_config_dppm_voltage_reg(di,di->dppm_voltagemV);

	return status;
}

static ssize_t bq2416x_show_dppm_voltage(struct device *dev,
				  struct device_attribute *attr,
				  char *buf)
{
	unsigned long val;
	struct bq2416x_device_info *di = dev_get_drvdata(dev);

	val = di->dppm_voltagemV;
	return sprintf(buf, "%lu\n", val);
}
/*
* set 1 --- enable_charger; 0 --- disable charger
*
*/
static ssize_t bq2416x_set_enable_charger(struct device *dev,	
				  struct device_attribute *attr,
				  const char *buf, size_t count)
{
	long val;
	int status = count;
    long int events = BQ2416x_START_CHARGING;
	struct bq2416x_device_info *di = dev_get_drvdata(dev);

	if ((strict_strtol(buf, 10, &val) < 0) || (val < 0) || (val > 1))
		return -EINVAL;

	di->enable_ce = val ^ 0x1;

	di->factory_flag = val ^ 0x1;
    bq2416x_config_control_reg(di);

    if(!di->factory_flag){
        events = BQ2416x_START_CHARGING;
    } else {
        events = BQ2416x_NOT_CHARGING;
    }

    blocking_notifier_call_chain(&notifier_list, events, NULL);
	return status;
}

static ssize_t bq2416x_show_enable_charger(struct device *dev,
				  struct device_attribute *attr,
				  char *buf)
{
	unsigned long val;
	struct bq2416x_device_info *di = dev_get_drvdata(dev);

	val = di->enable_ce ^ 0x1;
	return sprintf(buf, "%lu\n", val);
}

/*
* set 1 --- hz_mode ; 0 --- not hz_mode 
*
*/
static ssize_t bq2416x_set_enable_hz_mode(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t count)
{
	long val;
	int status = count;
	struct bq2416x_device_info *di = dev_get_drvdata(dev);

	if ((strict_strtol(buf, 10, &val) < 0) || (val < 0) || (val > 1))
		return -EINVAL;
	di->hz_mode= val;
	bq2416x_config_control_reg(di);

	return status;
}
static ssize_t bq2416x_show_enable_hz_mode(struct device *dev,
				  struct device_attribute *attr,
				  char *buf)
{
	unsigned long val;
	struct bq2416x_device_info *di = dev_get_drvdata(dev);

	val = di->hz_mode;
	return sprintf(buf, "%lu\n", val);	
}

/*
* set 1 --- enable bq24161 IC; 0 --- disable bq24161 IC
*
*/
static ssize_t bq2416x_set_enable_cd(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t count)
{
	long val;
	int status = count;
	struct bq2416x_device_info *di = dev_get_drvdata(dev);

	if ((strict_strtol(buf, 10, &val) < 0) || (val < 0) || (val > 1))
		return -EINVAL;
	di->cd_active =val ^ 0x1;
	gpio_set_value(ENABLE_BQ2416x_CHARGER, di->cd_active);
	return status;
}
static ssize_t bq2416x_show_enable_cd(struct device *dev,
				  struct device_attribute *attr,
				  char *buf)
{
	unsigned long val;
	struct bq2416x_device_info *di = dev_get_drvdata(dev);

	val = di->cd_active ^ 0x1;
	return sprintf(buf, "%lu\n", val);	
}

static ssize_t bq2416x_show_chargelog(struct device *dev,
				  struct device_attribute *attr,
				  char *buf)
{
        int i = 0;
        u8   read_reg[8] = {0};
        u8   buf_temp[20] = {0};
        struct bq2416x_device_info *di = dev_get_drvdata(dev);
        bq2416x_read_block(di, &read_reg[0], 0, 8);
        for(i=0;i<8;i++)
        {
            sprintf(buf_temp,"0x%-8.2x",read_reg[i]);
            strcat(buf,buf_temp);
        }
        strcat(buf,"\n");
	return strlen(buf);
}


static ssize_t bq2416x_set_calling_limit(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t count)
{
	long val;
	int status = count;
	struct bq2416x_device_info *di = dev_get_drvdata(dev);
	if ((strict_strtol(buf, 10, &val) < 0) || (val < 0) || (val > 1))
		return -EINVAL;

	di->calling_limit = val;
	if (di->charger_source == POWER_SUPPLY_TYPE_MAINS){
		if(di->calling_limit){
			di->cin_limit = IINLIM_800;
			dev_info(di->dev,"calling_limit_current = %d\n", di->cin_limit);
		}else{
            di->battery_temp_status = -1;
            di->cin_limit = di->max_cin_currentmA;
        }
        bq2416x_config_control_reg(di);
	}
	else{
		di->calling_limit = 0;
	}

	return status;
}

static ssize_t bq2416x_show_calling_limit(struct device *dev,
				  struct device_attribute *attr,
				  char *buf)
{
	unsigned long val;
	struct bq2416x_device_info *di = dev_get_drvdata(dev);

	val = di->calling_limit;

	return sprintf(buf, "%lu\n", val);
}

static ssize_t bq2416x_set_charging(struct device *dev,
                  struct device_attribute *attr,
                  const char *buf, size_t count)
{
    int status = count;
    struct bq2416x_device_info *di = dev_get_drvdata(dev);

    if (strncmp(buf, "startac", 7) == 0) {
        if (di->charger_source == POWER_SUPPLY_TYPE_USB)
            bq2416x_stop_charger(di);
        bq2416x_start_ac_charger(di);
    } else if (strncmp(buf, "startusb", 8) == 0) {
        if (di->charger_source == POWER_SUPPLY_TYPE_MAINS)
            bq2416x_stop_charger(di);
        bq2416x_start_usb_charger(di);
    } else if (strncmp(buf, "stop" , 4) == 0) {
        bq2416x_stop_charger(di);
    }else
        return -EINVAL;

    return status;
}

static ssize_t bq2416x_set_wakelock_enable(struct device *dev,
                  struct device_attribute *attr,
                  const char *buf, size_t count)
{
    long val;
    int status = count;
    struct bq2416x_device_info *di = dev_get_drvdata(dev);

    if ((strict_strtol(buf, 10, &val) < 0) || (val < 0) || (val > 1))
        return -EINVAL;

    if ((val) && (di->charger_source != POWER_SUPPLY_TYPE_BATTERY))
        wake_lock(&di->charger_wake_lock);
    else
        wake_unlock(&di->charger_wake_lock);

    di->wakelock_enabled = val;
    return status;
}

static ssize_t bq2416x_show_wakelock_enable(struct device *dev,
                  struct device_attribute *attr,
                  char *buf)
{
    unsigned int val;
    struct bq2416x_device_info *di = dev_get_drvdata(dev);

    val = di->wakelock_enabled;
    return sprintf(buf, "%u\n", val);
}

static DEVICE_ATTR(enable_cd, S_IWUSR | S_IRUGO,
				bq2416x_show_enable_cd,
				bq2416x_set_enable_cd);

static DEVICE_ATTR(enable_itermination, S_IWUSR | S_IRUGO,
				bq2416x_show_enable_itermination,
				bq2416x_set_enable_itermination);
static DEVICE_ATTR(cin_limit, S_IWUSR | S_IRUGO,
				bq2416x_show_cin_limit,
				bq2416x_set_cin_limit);
static DEVICE_ATTR(regulation_voltage, S_IWUSR | S_IRUGO,
				bq2416x_show_regulation_voltage,
				bq2416x_set_regulation_voltage);
static DEVICE_ATTR(charge_current, S_IWUSR | S_IRUGO,
				bq2416x_show_charge_current,
				bq2416x_set_charge_current);
static DEVICE_ATTR(termination_current, S_IWUSR | S_IRUGO,
				bq2416x_show_termination_current,
				bq2416x_set_termination_current);				
static DEVICE_ATTR(enable_charger, S_IWUSR | S_IRUGO,
				bq2416x_show_enable_charger,
				bq2416x_set_enable_charger);
static DEVICE_ATTR(enable_hz_mode, S_IWUSR | S_IRUGO,
				bq2416x_show_enable_hz_mode,
				bq2416x_set_enable_hz_mode);
static DEVICE_ATTR(dppm_voltage, S_IWUSR | S_IRUGO,
				bq2416x_show_dppm_voltage,
				bq2416x_set_dppm_voltage);				
static DEVICE_ATTR(chargelog, S_IWUSR | S_IRUGO,
				bq2416x_show_chargelog,
				NULL);
static DEVICE_ATTR(calling_limit, S_IWUSR | S_IRUGO,
				bq2416x_show_calling_limit,
				bq2416x_set_calling_limit);
static DEVICE_ATTR(charging, S_IWUSR | S_IRUGO,
                NULL,
                bq2416x_set_charging);
static DEVICE_ATTR(wakelock_enable, S_IWUSR | S_IRUGO,
                bq2416x_show_wakelock_enable,
                bq2416x_set_wakelock_enable);

static struct attribute *bq2416x_attributes[] = {
	&dev_attr_enable_itermination.attr,
	&dev_attr_cin_limit.attr,
	&dev_attr_regulation_voltage.attr,
	&dev_attr_charge_current.attr,
	&dev_attr_termination_current.attr,
	&dev_attr_dppm_voltage.attr,    
	&dev_attr_enable_charger.attr,  
	&dev_attr_enable_hz_mode.attr, 
	&dev_attr_enable_cd.attr,
	&dev_attr_chargelog.attr,
	&dev_attr_calling_limit.attr,
    &dev_attr_charging.attr,
    &dev_attr_wakelock_enable.attr,
	NULL,
};

static const struct attribute_group bq2416x_attr_group = {
	.attrs = bq2416x_attributes,
};

static void bq2416x_usb_charger_work(struct work_struct *work)
{
	struct bq2416x_device_info	*di =
		container_of(work, struct bq2416x_device_info, usb_work);
	switch (di->event) {
	case USB_EVENT_CHARGER:
		bq2416x_start_ac_charger(di);
		break;
	case USB_EVENT_VBUS:
		bq2416x_start_usb_charger(di);
		break;
	case USB_EVENT_NONE:
		bq2416x_stop_charger(di);
		break;
	case USB_EVENT_ENUMERATED:
		break;
	default:
		return;
	}
}

static int bq2416x_usb_notifier_call(struct notifier_block *nb_otg,
		unsigned long event, void *data)
{
	struct bq2416x_device_info *di = 
		container_of(nb_otg, struct bq2416x_device_info, nb_otg);

	di->event = event;
	switch (event) {	
	case USB_EVENT_VBUS:
		break;
	case USB_EVENT_ENUMERATED:
		break;
	case USB_EVENT_CHARGER:
	    	break;
	case USB_EVENT_NONE:
	    	break;
	case USB_EVENT_ID:
	    	break;
	default:
		return NOTIFY_OK;
	}
	schedule_work(&di->usb_work);
	return NOTIFY_OK;
}
static int bq2416x_get_max_charge_voltage(struct bq2416x_device_info *di)
{
    bool ret = 0;

    ret = get_hw_config_int("gas_gauge/charge_voltage", &di->max_voltagemV , NULL);
    if(ret){
        if(di->max_voltagemV < 4200){
            di->max_voltagemV = 4200;
        }
        return true;
    }
    else{
        dev_err(di->dev, " bq2416x_get_max_charge_voltage from boardid fail \n");
        return false;
    }
}

static int bq2416x_get_max_charge_current(struct bq2416x_device_info *di)
{
    bool ret = 0;

    ret = get_hw_config_int("gas_gauge/charge_current", &di->max_currentmA , NULL);
    if(ret){
        if(di->max_currentmA < 1000){
             di->max_currentmA = 1000;
        }
        return true;
    }
    else{
        dev_err(di->dev, " bq2416x_get_max_charge_current from boardid fail \n");
        return false;
    }
}

static int __devinit bq2416x_charger_probe(struct i2c_client *client,
				 const struct i2c_device_id *id)
{
	struct bq2416x_device_info *di;
	struct bq2416x_platform_data *pdata = client->dev.platform_data;
	int ret;
	u8 read_reg = 0;
	enum plugin_status plugin_stat;

	di = kzalloc(sizeof(*di), GFP_KERNEL);
	if (!di)
		return -ENOMEM;

	di->dev = &client->dev;
	di->client = client;

	i2c_set_clientdata(client, di);

	ret = bq2416x_read_byte(di, &read_reg, REG_PART_REVISION_REG04);

	if (ret < 0) {
		dev_err(&client->dev, "chip not present at address %x\n",
								client->addr);
		ret = -EINVAL;
		goto err_kfree;
	} 
	
	if ((read_reg & 0x18) == 0x00 && (client->addr == 0x6b))
		di->bqchip_version = BQ24161;

	if (di->bqchip_version == 0) {
		dev_dbg(&client->dev, "unknown bq chip\n");
		dev_dbg(&client->dev, "Chip address %x", client->addr);
		dev_dbg(&client->dev, "bq chip version reg value %x", read_reg);
		ret = -EINVAL;
		goto err_kfree;
	}
	//	di->nb.notifier_call = bq2416x_charger_event;

    /*set gpio_174 to control CD pin to disable/enable bq24161 IC*/
	 gpio_request(ENABLE_BQ2416x_CHARGER, "gpio_174_cd");
	 /* set charger CD pin to low level and enable it to supply power normally*/
	 gpio_direction_output(ENABLE_BQ2416x_CHARGER, 0);

     ret = bq2416x_get_max_charge_voltage(di);
	 if(!ret){
		di->max_voltagemV = pdata->max_charger_voltagemV;
	 }

	 ret = bq2416x_get_max_charge_current(di);
	 if(!ret){
		di->max_currentmA = pdata->max_charger_currentmA;
	 }

     di->max_cin_currentmA = IINLIM_1000;
	 di->voltagemV = di->max_voltagemV;
	 di->currentmA = ICHG_MIN_550 ;
     di->term_currentmA = ITERM_MIN_50;
	 di->dppm_voltagemV = VINDPM_MIN_4200;
	 di->cin_limit = IINLIM_500; 
    di->safety_timer = TMR_X_9;
	di->enable_low_chg = DISABLE_LOW_CHG;/*set normally charge mode*/
	di->enable_iterm = ENABLE_ITERM; /*enable charge current termination*/
    di->supply_sel = SUPPLY_SEL_IN;
	di->factory_flag = 0;
	di->enable_ce = ENABLE_CHARGER;
	di->hz_mode = DIS_HIZ;
	di->cd_active = 0;
	 
	INIT_DELAYED_WORK_DEFERRABLE(&di->bq2416x_charger_work,
				bq2416x_charger_work);


	wake_lock_init(&di->charger_wake_lock, WAKE_LOCK_SUSPEND, "charger_wake_lock");


	//BLOCKING_INIT_NOTIFIER_HEAD(&notifier_list);

	di->params.enable = 1;
	di->cfg_params = 1;
	
	 bq2416x_config_control_reg(di);
	 bq2416x_config_voltage_reg(di);
	 bq2416x_config_current_reg(di);
	 bq2416x_config_dppm_voltage_reg(di,di->dppm_voltagemV);	
	 bq2416x_config_safety_reg(di);

	ret = sysfs_create_group(&client->dev.kobj, &bq2416x_attr_group);
	if (ret)
		dev_dbg(&client->dev, "could not create sysfs files\n");

	//twl6030_register_notifier(&di->nb, 1);

	INIT_WORK(&di->usb_work, bq2416x_usb_charger_work);

	di->nb_otg.notifier_call = bq2416x_usb_notifier_call;
	di->otg = otg_get_transceiver();
	ret = otg_register_notifier(di->otg, &di->nb_otg);
	if (ret)
		dev_err(&client->dev, "otg register notifier failed %d\n", ret);
 
    plugin_stat = get_plugin_device_status();
    if( PLUGIN_USB_CHARGER == plugin_stat){
		di->event = USB_EVENT_VBUS;
    }else if (PLUGIN_AC_CHARGER == plugin_stat){
         di->event = USB_EVENT_CHARGER;
    }else{
		di->event = USB_EVENT_NONE;
	}
	schedule_work(&di->usb_work);
	return 0;

err_kfree:
	kfree(di);	
	
	return ret;
}

static int __devexit bq2416x_charger_remove(struct i2c_client *client)
{
	struct bq2416x_device_info *di = i2c_get_clientdata(client);

	sysfs_remove_group(&client->dev.kobj, &bq2416x_attr_group);

	wake_lock_destroy(&di->charger_wake_lock);


	cancel_delayed_work_sync(&di->bq2416x_charger_work);
	//flush_scheduled_work();

	//twl6030_unregister_notifier(&di->nb, 1);
	otg_unregister_notifier(di->otg, &di->nb_otg);
	kfree(di);

	return 0;
}

static const struct i2c_device_id bq2416x_id[] = {
	{ "bq24161", 0 },
	{},
};

#ifdef CONFIG_PM
static int bq2416x_charger_suspend(struct i2c_client *client,
	pm_message_t state)
{

	return 0;
}

static int bq2416x_charger_resume(struct i2c_client *client)
{
	return 0;
}
#else
#define bq2416x_charger_suspend	NULL
#define bq2416x_charger_resume	NULL
#endif /* CONFIG_PM */

static struct i2c_driver bq2416x_charger_driver = {
	.probe		= bq2416x_charger_probe,
	.remove		= __devexit_p(bq2416x_charger_remove),
	.suspend	= bq2416x_charger_suspend,
	.resume		= bq2416x_charger_resume,
	.id_table	= bq2416x_id,
	.driver		= {
		.name	= "bq2416x_charger",
	},
};

static int __init bq2416x_charger_init(void)
{
	return i2c_add_driver(&bq2416x_charger_driver);
}
module_init(bq2416x_charger_init);

static void __exit bq2416x_charger_exit(void)
{
	i2c_del_driver(&bq2416x_charger_driver);
}
module_exit(bq2416x_charger_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Texas Instruments Inc");

