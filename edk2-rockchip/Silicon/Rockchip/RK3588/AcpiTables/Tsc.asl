/** @file
 *
 *  Copyright (c) 2021, ARM Limited. All rights reserved.
 *
 *  SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 **/
#include "AcpiTables.h"

  Device (TSC1) {
    Name (_HID, BOARD_TOUCH_HID)
    Name (_UID, 0)
    Name (_CCA, 0)

    Method (_CRS, 0x0, Serialized) {
      Name (RBUF, ResourceTemplate() {
        I2cSerialBusV2(BOARD_TOUCH_I2C_ADDR, ControllerInitiated, 0x000186A0,
          AddressingMode7Bit, BOARD_TOUCH_I2C,
          0x00, ResourceConsumer, , Exclusive)
        GpioInt (Edge, ActiveHigh, Exclusive, PullNone, 0x0000,
          BOARD_TOUCH_INT_GPIO, 0x00, ResourceConsumer)
          {
            BOARD_TOUCH_INT_GPIO_PIN
          }
        // GpioIo (Exclusive, PullNone, 0x0000, 0x0000, IoRestrictionNone,
        //   BOARD_TOUCH_RST_GPIO, 0x00, ResourceConsumer, ,
        //   )
        //   {   // Pin list
        //     BOARD_TOUCH_RST_GPIO_PIN
        //   }
      })
      Return (RBUF)
    }
  }