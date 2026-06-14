/** @file
 *
 *  SLPad UART9 and Broadcom Bluetooth devices.
 *
 *  SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 **/

Device (UAR9) {
  Name (_HID, "RKCP3009")
  Name (_UID, 9)
  Name (_CCA, 0)

  Method (_CRS, 0x0, Serialized) {
    Name (RBUF, ResourceTemplate () {
      Memory32Fixed (ReadWrite, 0xFEBC0000, 0x100)
      // DTS GIC_SPI 340 maps to ACPI GSI/INTID 372 (SPI base + 32).
      Interrupt (ResourceConsumer, Level, ActiveHigh, Exclusive) { 372 }
    })
    Return (RBUF)
  }

  Name (_DSD, Package () {
    ToUUID ("daffd814-6eba-4d8c-8a91-bc9bbf4aa301"),
    Package () {
      Package () { "rockchip,dma", "DMA2" },
      Package () { "rockchip,tx", 11 },
      Package () { "rockchip,rx", 12 },
      Package () { "compatible", "rockchip,rk3588-uart" },
      Package () { "reg-shift", 2 },
      Package () { "reg-io-width", 4 },
      Package () { "clock-frequency", 24000000 },
      Package () { "uart-has-rtscts", 1 },
    }
  })
  Name (_DEP, Package () { \_SB.DMA2 })

  Name (_STA, 0x0F)
}

Device (BTH0) {
  Name (_HID, "BCM2EA6")
  Name (_CID, "PRP0001")
  Name (_UID, 0)
  Name (_CCA, 0)
  Name (_DEP, Package () { \_SB.GPI0, \_SB.UAR9 })

  Name (_CRS, ResourceTemplate () {
    UARTSerialBus (
      115200,
      DataBitsEight,
      StopBitsOne,
      0xC0,
      LittleEndian,
      ParityTypeNone,
      FlowControlHardware,
      64,
      64,
      "\\_SB.UAR9",
      0,
      ResourceConsumer,
      BTUR
    )
    GpioIo (Exclusive, PullUp, 0x0000, 0x0000, IoRestrictionOutputOnly,
      "\\_SB.GPI0", 0x00, ResourceConsumer, ,)
      { GPIO_PIN_PC5 }
    GpioIo (Exclusive, PullNone, 0x0000, 0x0000, IoRestrictionOutputOnly,
      "\\_SB.GPI0", 0x00, ResourceConsumer, ,)
      { GPIO_PIN_PC6 }
    GpioInt (Edge, ActiveLow, ExclusiveAndWake, PullDown, 0x0000,
      "\\_SB.GPI0", 0x00, ResourceConsumer, ,)
      { GPIO_PIN_PA0 }
  })

  Name (_DSD, Package () {
    ToUUID ("daffd814-6eba-4d8c-8a91-bc9bbf4aa301"),
    Package () {
      Package () { "compatible", "brcm,bcm4345c5" },
      Package () { "max-speed", 1500000 },
    }
  })

  Name (_STA, 0x0F)
}
