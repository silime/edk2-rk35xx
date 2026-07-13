/** @file
 *
 *  RK3588 SDIO controller used by the on-board Wi-Fi module.
 *
 *  SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 **/

#include "AcpiTables.h"

Device (SDIO) {
  Name (_HID, "RKCPFE2D")
  Name (_UID, 0x0)
  Name (_CCA, 0x0)
  Name (_S1D, 0x1)
  Name (_S2D, 0x1)
  Name (_S3D, 0x1)
  Name (_S4D, 0x1)
  Name (_STA, 0xf)

  Method (_CRS, 0x0, Serialized) {
    Name (RBUF, ResourceTemplate () {
      Memory32Fixed (ReadWrite, 0xfe2d0000, 0x4000)
      Interrupt (ResourceConsumer, Level, ActiveHigh, Exclusive) { 236 }
    })
    Return (RBUF)
  }

  Name (_DSD, Package () {
    ToUUID ("daffd814-6eba-4d8c-8a91-bc9bbf4aa301"),
    Package () {
      Package () { "compatible", Package () { "rockchip,rk3588-dw-mshc", "rockchip,rk3288-dw-mshc" } },
      Package () { "fifo-depth", 0x100 },
      Package () { "max-frequency", 150000000 },
      Package () { "bus-width", 4 },
      Package () { "no-sd", 1 },
      Package () { "no-mmc", 1 },
      Package () { "disable-wp", 1 },
      Package () { "cap-sd-highspeed", 1 },
      Package () { "cap-sdio-irq", 1 },
      Package () { "keep-power-in-suspend", 1 },
      Package () { "non-removable", 1 },
      Package () { "sd-uhs-sdr104", 1 },
    }
  })

  Device (WLN0) {
    // SDIO _ADR: high word = slot 0, low word = function 1.
    // This lets Windows merge these sideband resources into the FN1 PDO.
    Name (_ADR, 0x00000001)
    Name (_UID, 0)
    Name (_CCA, 0)
    Name (_STA, 0x0F)
    Name (_RMV, 0)
    Name (_DEP, Package () { \_SB.GPI0 })

    Name (_CRS, ResourceTemplate () {
      // WL_REG_ON is a power-enable output. It must never be used as IRQ.
      // DTS: gpio_wl_reg_on = <&gpio0 RK_PC4 GPIO_ACTIVE_HIGH>.
      // Rockchip bank-local numbering is A0..A7=0..7, B0..B7=8..15, C0..C7=16..23.
      GpioIo (Exclusive, PullNone, 0x0000, 0x0000, IoRestrictionOutputOnly,
        "\\_SB.GPI0", 0x00, ResourceConsumer, ,)
        { GPIO_PIN_PC4 }

      // OOB host-wake interrupt used by CONFIG_BCMDHD_OOB_INTR=y.
      // DTS: gpio_wl_host_wake = <&gpio0 RK_PB7 GPIO_ACTIVE_HIGH>.
      // Windows cannot mask this GPIO at DIRQL while the SDIO worker drains the
      // level source. Detect the active-high transition and retain polling as a
      // missed-edge watchdog.
      GpioInt (Edge, ActiveHigh, ExclusiveAndWake, PullDown, 0x0000,
        "\\_SB.GPI0", 0x00, ResourceConsumer, ,)
        { GPIO_PIN_PB7 }
    })

    Name (_DSD, Package () {
      ToUUID ("daffd814-6eba-4d8c-8a91-bc9bbf4aa301"),
      Package () {
        Package () { "compatible", Package () { "android,bcmdhd_wlan", "brcm,bcm4329-fmac" } },
        Package () { "wifi-chip-type", "ap6398sv" },
        Package () { "interrupt-names", "host-wake" },
        Package () { "gpio_wl_reg_on", Package () { ^WLN0, 0, 0, 0 } },
        Package () { "gpio_wl_host_wake", Package () { ^WLN0, 1, 0, 0 } },
      }
    })
  }
}
