/** @file
 *
 *  Battery charger status shared by charger and gas gauge drivers.
 *
 *  SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 **/

#ifndef __BATTERY_CHARGER_H__
#define __BATTERY_CHARGER_H__

#define BATTERY_CHARGE_STATUS_UNKNOWN       0
#define BATTERY_CHARGE_STATUS_NOT_CHARGING  1
#define BATTERY_CHARGE_STATUS_PRECHARGE     2
#define BATTERY_CHARGE_STATUS_FAST_CHARGE   3
#define BATTERY_CHARGE_STATUS_DONE          4
#define BATTERY_CHARGE_STATUS_DISCHARGING   5
#define BATTERY_CHARGE_STATUS_OTG           6
#define BATTERY_CHARGE_STATUS_FAULT         7

#endif
