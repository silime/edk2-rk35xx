/** @file
 *
 *  Copyright (c) 2021, ARM Limited. All rights reserved.
 *
 *  SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 **/
#include "AcpiTables.h"

  Device (BAT1) {
    Name (_HID, BOARD_BAT_HID)
    Name (_UID, 0)
    Name (_CCA, 0)

    Method (_CRS, 0x0, Serialized) {
      Name (RBUF, ResourceTemplate() {
        I2cSerialBusV2(BOARD_BAT_I2C_ADDR, ControllerInitiated, 0x000186A0,
          AddressingMode7Bit, BOARD_BAT_I2C,
          0x00, ResourceConsumer, , Exclusive)
      })
      Return (RBUF)
    }
  }
