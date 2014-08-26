/*
 * Copyright (C) 2010 Texas Instruments
 * Author: Balaji T K
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef _LINUX_BQ2416X_I2C_H
#define _LINUX_BQ2416X_I2C_H

#define I2C_ADDR_BQ2416x        (0x6B)

#define BQ2416x_IC_THRESHOLD_VOLTAGE    (4000)
#define BQ2416x_NO_CHARGER_SOURCE       (0x00)
#define BQ2416x_NOT_CHARGING            (0x10)
#define BQ2416x_START_CHARGING          (0x20)
#define BQ2416x_START_AC_CHARGING       (0x30)
#define BQ2416x_START_USB_CHARGING      (0x40)
#define BQ2416x_CHARGE_DONE             (0x50)
#define BQ2416x_STOP_CHARGING           (0x60)
#define POWER_SUPPLY_STATE_FAULT        (0x70)
#define POWER_SUPPLY_OVERVOLTAGE        (0x80)
#define POWER_SUPPLY_WEAKSOURCE         (0x90)
#define BATTERY_LOW_WARNING             (0x51)
#define BATTERY_LOW_SHUTDOWN            (0x52)

/* Status/Control Register */
#define REG_STATUS_CONTROL_REG00    0x00

#define TMR_RST                     (1 << 7)
#define BQ2416x_POWER_NONE          (0x00)
#define BQ2416x_IN_READY            (1 << 4)
#define BQ2416x_USB_READY           (2 << 4)
#define BQ2416x_CHARGING_FROM_IN    (3 << 4)
#define BQ2416x_CHARGING_FROM_USB   (4 << 4)
#define BQ2416x_STATUS_CHARGE_DONE  (5 << 4)
#define BQ2416x_CHARGE_FAULT        (7 << 4)
#define SUPPLY_SEL_IN               (0x0)
#define SUPPLY_SEL_USB              (1 << 3)
#define BQ2416x_FAULT_THERMAL_SHUTDOWN     (0x01)
#define BQ2416x_FAULT_BATTERY_TEMPERATURE  (0x02)
#define BQ2416x_FAULT_WATCHDOG_TIMER       (0x03)
#define BQ2416x_FAULT_SAFETY_TIMER         (0x04)
#define BQ2416x_FAULT_AC_SUPPLY            (0x05)
#define BQ2416x_FAULT_USB_SUPPLY           (0x06)
#define BQ2416x_FAULT_BATTERY              (0x07)

/* Battery/ Supply Status Register */
#define REG_BATTERY_AND_SUPPLY_STATUS_REG01    0x01

#define BQ2416x_FAULT_VAC_OVP       (1 << 6)
#define BQ2416x_FAULT_VAC_WEAK		(2 << 6)
#define BQ2416x_FAULT_VAC_VUVLO		(3 << 6)
#define BQ2416x_FAULT_VBUS_OVP		(1 << 4)
#define BQ2416x_FAULT_VBUS_WEAK		(2 << 4)
#define BQ2416x_FAULT_VBUS_VUVLO    (3 << 4)
#define OTG_LOCK_EN                 (1 << 3)
#define OTG_LOCK_DIS                 (0x00)
#define BQ2416x_FAULT_BAT_OVP		(1 << 1)
#define BQ2416x_FAULT_NO_BATTERY	(2 << 1)
#define EN_NOBATOP                  (1 << 0)
#define DIS_NOBATOP                 (0)
 
/* Control Register */
#define REG_CONTROL_REGISTER_REG02    0x02

#define RESET          (1 << 7)
#define BQ2416x_INPUT_CURRENT_LIMIT_SHIFT   4
#define IINLIM_100                    (100)
#define IINLIM_150                    (150)
#define IINLIM_500                    (500)
#define IINLIM_800                    (800)
#define IINLIM_900                    (900)
#define IINLIM_1000                   (1000)
#define IINLIM_1200                   (1200)
#define IINLIM_1500                   (1500)
#define ENABLE_STAT                   (1 << 3)
#define DISABLE_STAT                  (0x00)
#define BQ2416x_EN_ITERM_SHIFT          2
#define ENABLE_ITERM                (1)
#define DISABLE_ITERM               (0)
#define BQ2416x_EN_CE_SHIFT              1
#define ENABLE_CHARGER             (0)
#define DISABLE_CHARGER            (1)
#define EN_HIZ                     (1)
#define DIS_HIZ                    (0)
//#define CURRENT_AC_LIMIT_IN     (1000)
//#define CURRENT_USB_LIMIT_IN     (500)

/* Control/Battery Voltage Register */
#define REG_BATTERY_VOLTAGE_REG03    0x03

#define BQ2416x_VCHARGE_SHIFT     2
#define VCHARGE_MIN_3500         (3500)
#define VCHARGE_4200         (4200) 
#define VCHARGE_4300         (4300) 
#define VCHARGE_4350         (4340)
#define VCHARGE_MAX_4440         (4440)
#define VCHARGE_STEP_20      (20)
#define ILIMIT_IN_1500       (0)
#define ILIMIT_IN_2500       (1 << 1)
#define DPDM_EN              (1)
#define DPDM_DIS             (0)

/* Vender/Part/Revision Register */
#define REG_PART_REVISION_REG04    0x04


/* Battery Termination/Fast Charge Current Register */
#define REG_BATTERY_CURRENT_REG05   0x05

#define BQ2416x_CURRENT_SHIFT       3
#define ICHG_MIN_550     (550)
#define ICHG_800         (800)
#define ICHG_900         (900)
#define ICHG_1000       (1000)
#define ICHG_1250       (1250)
#define ICHG_1500       (1500)
#define ICHG_2000       (2000)
#define ICHG_2500       (2500)
#define ICHG_MAX_2825   (2825)
#define ICHG_STEP_75      (75)

#define ITERM_MIN_50       (50)
#define ITERM_100          (100)
#define ITERM_150          (150)
#define ITERM_MAX_400      (400)
#define ITERM_STEP_50      (50)

/* VIN-DPM Voltage/ DPPM Status Register */
#define REG_DPPM_VOLTAGE_REG06   0x06

#define MINSYS_STATUS_ACTIVE    (1 << 7)
#define DPM_STATUS_ACTIVE       (1 << 6)
#define BQ2416x_USB_INPUT_DPPM_SHIFT   3   /* USB input VIN-DPM voltage shift bit */
#define VINDPM_MIN_4200    (4200)
#define VINDPM_4360        (4360)
#define VINDPM_4520        (4520)
#define VINDPM_MAX_4760    (4760)
#define VINDPM_STEP_80     (80)

/* Safety Timer/NTC Monitor Register */
#define REG_SAFETY_TIMER_REG07	0x07

#define TMR_EN                          (1 << 7)  /* Timer slowed by 2x when in thermal regulation*/
#define	TMR_X_0		                    (0 << 5) /* 27 min fast charge*/
#define	TMR_X_6		                    (1 << 5) /* 6 hour fast charge*/
#define	TMR_X_9		                    (2 << 5) /* 9 hour fast charge*/
#define DIS_SAFETY_TIMER                (3 << 5)
#define EN_TS_PIN                       (1 << 3) /* TS function enabled*/
#define DIS_TS_PIN                      (0x00) /* TS function disable*/
#define BQ2416x_FAULT_TS_COLD_OR_HOT	(1 << 1)
#define BQ2416x_FAULT_TS_COLD_AND_COOL  (2 << 1)
#define BQ2416x_FAULT_TS_WARM_AND_HOT	(3 << 1)
#define ENABLE_LOW_CHG	                (1)      /* preconditioning current is 100mA below 3.0V*/
#define DISABLE_LOW_CHG	                (0)


#define BQ2416x_WATCHDOG_TIMEOUT    (20000)

/*set gpio_174 to control CD pin to disable/enable bq2416x IC*/
#define ENABLE_BQ2416x_CHARGER        (174)
 
/*battery temperature is -10 degree*/
#define BQ2416x_COLD_BATTERY_THRESHOLD     (-10)
 /*battery temperature is 0 degree*/
#define BQ2416x_COOL_BATTERY_THRESHOLD      (0)
 /*battery temperature is 40 degree*/
#define BQ2416x_WARM_BATTERY_THRESHOLD     (40)
 /*battery temperature is 50 degree*/
#define BQ2416x_HOT_BATTERY_THRESHOLD      (50)
 /*battery temperature offset is 3 degree*/
#define BQ2416x_TEMPERATURE_OFFSET          (3)
 /*battery preconditioning voltage is 3.0V*/

#define TEMPERATURE_MULTIPLE      10 
#define VOLTAGE_MULTIPLE         1000 
 /*low temperature charge termination voltage*/
#define BQ2416x_LOW_TEMP_TERM_VOLTAGE    (4000)

#define BQ2416x_PRECHG_ICHRG_VOLTAGE     (3000)


#define BQ24161 (1 << 1)


/* not a bq generated event,we use this to reset the
 * the timer from the twl driver.
 */
#define BQ2416x_RESET_TIMER		0x38

struct bq2416x_platform_data {
	int max_charger_currentmA;
	int max_charger_voltagemV;
	int termination_currentmA;
};

int bq2416x_register_notifier(struct notifier_block *nb, unsigned int events);
int bq2416x_unregister_notifier(struct notifier_block *nb, unsigned int events);

#endif

