/** @file
 *
 *  SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 **/
#include "AcpiTables.h"

  Device (FUSB) {
    Name (_HID, BOARD_FUSB302_HID)
    Name (_UID, 1)
    Name (_CCA, 0)
    Name (_DEP, Package () { \_SB.GPI0, \_SB.I2C6, \_SB.I2C6.CHG0 })

    Method (_CRS, 0x0, Serialized) {
      Name (RBUF, ResourceTemplate() {
        I2cSerialBusV2(BOARD_FUSB302_I2C_ADDR, ControllerInitiated, 0x000186A0,
          AddressingMode7Bit, BOARD_FUSB302_I2C,
          0x00, ResourceConsumer, , Exclusive)
        GpioInt (Level, ActiveLow, ExclusiveAndWake, PullUp, 0x0000,
          BOARD_FUSB302_INT_GPIO, 0x00, ResourceConsumer, ,)
          { BOARD_FUSB302_INT_GPIO_PIN }
      })
      Return (RBUF)
    }
  }

  Device (CHG0) {
    Name (_HID, BOARD_BQ25890_HID)
    Name (_UID, 1)
    Name (_CCA, 0)
    Name (_DEP, Package () { \_SB.GPI4, \_SB.I2C6 })

    Method (_CRS, 0x0, Serialized) {
      Name (RBUF, ResourceTemplate() {
        I2cSerialBusV2(BOARD_BQ25890_I2C_ADDR, ControllerInitiated, 0x000186A0,
          AddressingMode7Bit, BOARD_BQ25890_I2C,
          0x00, ResourceConsumer, , Exclusive)
        GpioInt (Edge, ActiveLow, ExclusiveAndWake, PullUp, 0x0000,
          BOARD_BQ25890_INT_GPIO, 0x00, ResourceConsumer, ,)
          { BOARD_BQ25890_INT_GPIO_PIN }
        GpioIo (Exclusive, PullUp, 0x0000, 0x0000, IoRestrictionOutputOnly,
          BOARD_BQ25890_OTG_GPIO, 0x00, ResourceConsumer, ,)
          { BOARD_BQ25890_OTG_GPIO_PIN }
      })
      Return (RBUF)
    }
  }
