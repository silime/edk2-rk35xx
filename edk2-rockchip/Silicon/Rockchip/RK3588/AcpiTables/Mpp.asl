/** @file
 *
 *  Rockchip Media Process Platform (MPP) - hardware video codecs.
 *
 *  Exposes RKVDEC (H.264/H.265/VP9), RKVENC (H.264/H.265),
 *  legacy VPU (VEPU/VDPU), JPEG encoder/decoder, IEP and AV1
 *  decoder cores along with their per-engine IOMMUs to OS drivers
 *  via ACPI, mirroring the rk3588s.dtsi device tree topology.
 *
 *  Each device has a Rockchip-assigned _HID for Windows driver
 *  matching, with _CID "PRP0001" + _DSD "compatible" for Linux.
 *
 *  HID allocation (RKCP35xx range, RK3588 MPP):
 *    RKCP3500  rockchip,mpp-service
 *    RKCP3501  rockchip,vpu-jpege-ccu
 *    RKCP3502  rockchip,rkv-encoder-v2-ccu
 *    RKCP3503  rockchip,rkv-decoder-v2-ccu
 *    RKCP3510  rockchip,vpu-encoder-v2 (legacy VEPU)
 *    RKCP3511  rockchip,vpu-decoder-v2 (legacy VDPU)
 *    RKCP3512  rockchip,avs-plus-decoder
 *    RKCP3520  rockchip,rkv-jpeg-decoder-v1
 *    RKCP3521  rockchip,vpu-jpege-core
 *    RKCP3530  rockchip,iep-v2
 *    RKCP3540  rockchip,rkv-encoder-v2-core
 *    RKCP3550  rockchip,rkv-decoder-v2
 *    RKCP3560  rockchip,av1-decoder
 *    RKCP3570  rockchip,iommu-v2 (generic MPP IOMMU)
 *    RKCP3571  rockchip,iommu-av1d
 *
 *  Copyright (c) 2026, Vibe Coder
 *
 *  SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 **/

#include "AcpiTables.h"

Scope (\_SB_) {

  //
  // MPP service node - taskqueue/reset group coordinator referenced
  // by all codec cores via the rockchip,srv property.
  //
  Device (MPSV) {
    Name (_HID, "RKCP3500")
    Name (_CID, "PRP0001")
    Name (_UID, 0)
    Name (_CCA, 0)
    Name (_STA, 0xF)

    Name (_DSD, Package () {
      ToUUID("daffd814-6eba-4d8c-8a91-bc9bbf4aa301"),
      Package () {
        Package () { "compatible", "rockchip,mpp-service" },
        Package () { "rockchip,taskqueue-count", 12 },
        Package () { "rockchip,resetgroup-count", 1 },
      }
    })
  }

  //
  // CCU nodes (multi-core coordination)
  //
  Device (JPCC) {
    Name (_HID, "RKCP3501")
    Name (_CID, "PRP0001")
    Name (_UID, 0)
    Name (_CCA, 0)
    Name (_STA, 0xF)

    Name (_DSD, Package () {
      ToUUID("daffd814-6eba-4d8c-8a91-bc9bbf4aa301"),
      Package () {
        Package () { "compatible", "rockchip,vpu-jpege-ccu" },
      }
    })
  }

  Device (RECC) {
    Name (_HID, "RKCP3502")
    Name (_CID, "PRP0001")
    Name (_UID, 0)
    Name (_CCA, 0)
    Name (_STA, 0xF)

    Name (_DSD, Package () {
      ToUUID("daffd814-6eba-4d8c-8a91-bc9bbf4aa301"),
      Package () {
        Package () { "compatible", "rockchip,rkv-encoder-v2-ccu" },
      }
    })
  }

  Device (RDCC) {
    Name (_HID, "RKCP3503")
    Name (_CID, "PRP0001")
    Name (_UID, 0)
    Name (_CCA, 0)
    Name (_STA, 0xF)

    Method (_CRS, 0x0, Serialized) {
      Name (RBUF, ResourceTemplate() {
        Memory32Fixed (ReadWrite, 0xFDC30000, 0x100)
      })
      Return (RBUF)
    }

    Name (_DSD, Package () {
      ToUUID("daffd814-6eba-4d8c-8a91-bc9bbf4aa301"),
      Package () {
        Package () { "compatible", "rockchip,rkv-decoder-v2-ccu" },
        Package () { "reg-names", Package () { "ccu" } },
        Package () { "rockchip,ccu-mode", 1 },
      }
    })
  }

  //
  // Legacy VPU (VEPU + VDPU share IOMMU at 0xfdb50800)
  //
  Device (VEPU) {
    Name (_HID, "RKCP3510")
    Name (_CID, "PRP0001")
    Name (_UID, 0)
    Name (_CCA, 0)
    Name (_DEP, Package () { \_SB.VPMU })
    Name (_STA, 0xF)

    Method (_CRS, 0x0, Serialized) {
      Name (RBUF, ResourceTemplate() {
        Memory32Fixed (ReadWrite, 0xFDB50000, 0x400)
        Interrupt (ResourceConsumer, Level, ActiveHigh, Exclusive) { 152 }
      })
      Return (RBUF)
    }

    Name (_DSD, Package () {
      ToUUID("daffd814-6eba-4d8c-8a91-bc9bbf4aa301"),
      Package () {
        Package () { "compatible", "rockchip,vpu-encoder-v2" },
        Package () { "interrupt-names", Package () { "irq_vepu" } },
      }
    })
  }

  Device (VDPU) {
    Name (_HID, "RKCP3511")
    Name (_CID, "PRP0001")
    Name (_UID, 0)
    Name (_CCA, 0)
    Name (_DEP, Package () { \_SB.VPMU })
    Name (_STA, 0xF)

    Method (_CRS, 0x0, Serialized) {
      Name (RBUF, ResourceTemplate() {
        Memory32Fixed (ReadWrite, 0xFDB50400, 0x400)
        Interrupt (ResourceConsumer, Level, ActiveHigh, Shared) { 151 }
      })
      Return (RBUF)
    }

    Name (_DSD, Package () {
      ToUUID("daffd814-6eba-4d8c-8a91-bc9bbf4aa301"),
      Package () {
        Package () { "compatible", "rockchip,vpu-decoder-v2" },
        Package () { "interrupt-names", Package () { "irq_vdpu" } },
      }
    })
  }

  Device (VPMU) {
    Name (_HID, "RKCP3570")
    Name (_CID, "PRP0001")
    Name (_UID, 0)
    Name (_CCA, 0)
    Name (_STA, 0xF)

    Method (_CRS, 0x0, Serialized) {
      Name (RBUF, ResourceTemplate() {
        Memory32Fixed (ReadWrite, 0xFDB50800, 0x40)
        Interrupt (ResourceConsumer, Level, ActiveHigh, Exclusive) { 150 }
      })
      Return (RBUF)
    }

    Name (_DSD, Package () {
      ToUUID("daffd814-6eba-4d8c-8a91-bc9bbf4aa301"),
      Package () {
        Package () { "compatible", "rockchip,iommu-v2" },
        Package () { "interrupt-names", Package () { "irq_vdpu_mmu" } },
      }
    })
  }

  Device (AVSD) {
    Name (_HID, "RKCP3512")
    Name (_CID, "PRP0001")
    Name (_UID, 0)
    Name (_CCA, 0)
    Name (_DEP, Package () { \_SB.VPMU })
    Name (_STA, 0xF)

    Method (_CRS, 0x0, Serialized) {
      Name (RBUF, ResourceTemplate() {
        Memory32Fixed (ReadWrite, 0xFDB51000, 0x200)
        Interrupt (ResourceConsumer, Level, ActiveHigh, Shared) { 151 }
      })
      Return (RBUF)
    }

    Name (_DSD, Package () {
      ToUUID("daffd814-6eba-4d8c-8a91-bc9bbf4aa301"),
      Package () {
        Package () { "compatible", "rockchip,avs-plus-decoder" },
        Package () { "interrupt-names", Package () { "irq_avsd" } },
      }
    })
  }

  //
  // JPEG decoder
  //
  Device (JPGD) {
    Name (_HID, "RKCP3520")
    Name (_CID, "PRP0001")
    Name (_UID, 0)
    Name (_CCA, 0)
    Name (_DEP, Package () { \_SB.JDMU })
    Name (_STA, 0xF)

    Method (_CRS, 0x0, Serialized) {
      Name (RBUF, ResourceTemplate() {
        Memory32Fixed (ReadWrite, 0xFDB90000, 0x400)
        Interrupt (ResourceConsumer, Level, ActiveHigh, Exclusive) { 161 }
      })
      Return (RBUF)
    }

    Name (_DSD, Package () {
      ToUUID("daffd814-6eba-4d8c-8a91-bc9bbf4aa301"),
      Package () {
        Package () { "compatible", "rockchip,rkv-jpeg-decoder-v1" },
        Package () { "interrupt-names", Package () { "irq_jpegd" } },
      }
    })
  }

  Device (JDMU) {
    Name (_HID, "RKCP3570")
    Name (_CID, "PRP0001")
    Name (_UID, 1)
    Name (_CCA, 0)
    Name (_STA, 0xF)

    Method (_CRS, 0x0, Serialized) {
      Name (RBUF, ResourceTemplate() {
        Memory32Fixed (ReadWrite, 0xFDB90480, 0x40)
        Interrupt (ResourceConsumer, Level, ActiveHigh, Exclusive) { 162 }
      })
      Return (RBUF)
    }

    Name (_DSD, Package () {
      ToUUID("daffd814-6eba-4d8c-8a91-bc9bbf4aa301"),
      Package () {
        Package () { "compatible", "rockchip,iommu-v2" },
        Package () { "interrupt-names", Package () { "irq_jpegd_mmu" } },
      }
    })
  }

  //
  // JPEG encoder cores 0..3 (each has its own IOMMU, share JPCC)
  //
  Device (JPE0) {
    Name (_HID, "RKCP3521")
    Name (_CID, "PRP0001")
    Name (_UID, 0)
    Name (_CCA, 0)
    Name (_DEP, Package () { \_SB.JPCC, \_SB.J0MU })
    Name (_STA, 0xF)

    Method (_CRS, 0x0, Serialized) {
      Name (RBUF, ResourceTemplate() {
        Memory32Fixed (ReadWrite, 0xFDBA0000, 0x400)
        Interrupt (ResourceConsumer, Level, ActiveHigh, Exclusive) { 154 }
      })
      Return (RBUF)
    }

    Name (_DSD, Package () {
      ToUUID("daffd814-6eba-4d8c-8a91-bc9bbf4aa301"),
      Package () {
        Package () { "compatible", "rockchip,vpu-jpege-core" },
        Package () { "interrupt-names", Package () { "irq_jpege0" } },
      }
    })
  }

  Device (J0MU) {
    Name (_HID, "RKCP3570")
    Name (_CID, "PRP0001")
    Name (_UID, 2)
    Name (_CCA, 0)
    Name (_STA, 0xF)

    Method (_CRS, 0x0, Serialized) {
      Name (RBUF, ResourceTemplate() {
        Memory32Fixed (ReadWrite, 0xFDBA0800, 0x40)
        Interrupt (ResourceConsumer, Level, ActiveHigh, Exclusive) { 153 }
      })
      Return (RBUF)
    }

    Name (_DSD, Package () {
      ToUUID("daffd814-6eba-4d8c-8a91-bc9bbf4aa301"),
      Package () {
        Package () { "compatible", "rockchip,iommu-v2" },
        Package () { "interrupt-names", Package () { "irq_jpege0_mmu" } },
      }
    })
  }

  Device (JPE1) {
    Name (_HID, "RKCP3521")
    Name (_CID, "PRP0001")
    Name (_UID, 1)
    Name (_CCA, 0)
    Name (_DEP, Package () { \_SB.JPCC, \_SB.J1MU })
    Name (_STA, 0xF)

    Method (_CRS, 0x0, Serialized) {
      Name (RBUF, ResourceTemplate() {
        Memory32Fixed (ReadWrite, 0xFDBA4000, 0x400)
        Interrupt (ResourceConsumer, Level, ActiveHigh, Exclusive) { 156 }
      })
      Return (RBUF)
    }

    Name (_DSD, Package () {
      ToUUID("daffd814-6eba-4d8c-8a91-bc9bbf4aa301"),
      Package () {
        Package () { "compatible", "rockchip,vpu-jpege-core" },
        Package () { "interrupt-names", Package () { "irq_jpege1" } },
      }
    })
  }

  Device (J1MU) {
    Name (_HID, "RKCP3570")
    Name (_CID, "PRP0001")
    Name (_UID, 3)
    Name (_CCA, 0)
    Name (_STA, 0xF)

    Method (_CRS, 0x0, Serialized) {
      Name (RBUF, ResourceTemplate() {
        Memory32Fixed (ReadWrite, 0xFDBA4800, 0x40)
        Interrupt (ResourceConsumer, Level, ActiveHigh, Exclusive) { 155 }
      })
      Return (RBUF)
    }

    Name (_DSD, Package () {
      ToUUID("daffd814-6eba-4d8c-8a91-bc9bbf4aa301"),
      Package () {
        Package () { "compatible", "rockchip,iommu-v2" },
        Package () { "interrupt-names", Package () { "irq_jpege1_mmu" } },
      }
    })
  }

  Device (JPE2) {
    Name (_HID, "RKCP3521")
    Name (_CID, "PRP0001")
    Name (_UID, 2)
    Name (_CCA, 0)
    Name (_DEP, Package () { \_SB.JPCC, \_SB.J2MU })
    Name (_STA, 0xF)

    Method (_CRS, 0x0, Serialized) {
      Name (RBUF, ResourceTemplate() {
        Memory32Fixed (ReadWrite, 0xFDBA8000, 0x400)
        Interrupt (ResourceConsumer, Level, ActiveHigh, Exclusive) { 158 }
      })
      Return (RBUF)
    }

    Name (_DSD, Package () {
      ToUUID("daffd814-6eba-4d8c-8a91-bc9bbf4aa301"),
      Package () {
        Package () { "compatible", "rockchip,vpu-jpege-core" },
        Package () { "interrupt-names", Package () { "irq_jpege2" } },
      }
    })
  }

  Device (J2MU) {
    Name (_HID, "RKCP3570")
    Name (_CID, "PRP0001")
    Name (_UID, 4)
    Name (_CCA, 0)
    Name (_STA, 0xF)

    Method (_CRS, 0x0, Serialized) {
      Name (RBUF, ResourceTemplate() {
        Memory32Fixed (ReadWrite, 0xFDBA8800, 0x40)
        Interrupt (ResourceConsumer, Level, ActiveHigh, Exclusive) { 157 }
      })
      Return (RBUF)
    }

    Name (_DSD, Package () {
      ToUUID("daffd814-6eba-4d8c-8a91-bc9bbf4aa301"),
      Package () {
        Package () { "compatible", "rockchip,iommu-v2" },
        Package () { "interrupt-names", Package () { "irq_jpege2_mmu" } },
      }
    })
  }

  Device (JPE3) {
    Name (_HID, "RKCP3521")
    Name (_CID, "PRP0001")
    Name (_UID, 3)
    Name (_CCA, 0)
    Name (_DEP, Package () { \_SB.JPCC, \_SB.J3MU })
    Name (_STA, 0xF)

    Method (_CRS, 0x0, Serialized) {
      Name (RBUF, ResourceTemplate() {
        Memory32Fixed (ReadWrite, 0xFDBAC000, 0x400)
        Interrupt (ResourceConsumer, Level, ActiveHigh, Exclusive) { 160 }
      })
      Return (RBUF)
    }

    Name (_DSD, Package () {
      ToUUID("daffd814-6eba-4d8c-8a91-bc9bbf4aa301"),
      Package () {
        Package () { "compatible", "rockchip,vpu-jpege-core" },
        Package () { "interrupt-names", Package () { "irq_jpege3" } },
      }
    })
  }

  Device (J3MU) {
    Name (_HID, "RKCP3570")
    Name (_CID, "PRP0001")
    Name (_UID, 5)
    Name (_CCA, 0)
    Name (_STA, 0xF)

    Method (_CRS, 0x0, Serialized) {
      Name (RBUF, ResourceTemplate() {
        Memory32Fixed (ReadWrite, 0xFDBAC800, 0x40)
        Interrupt (ResourceConsumer, Level, ActiveHigh, Exclusive) { 159 }
      })
      Return (RBUF)
    }

    Name (_DSD, Package () {
      ToUUID("daffd814-6eba-4d8c-8a91-bc9bbf4aa301"),
      Package () {
        Package () { "compatible", "rockchip,iommu-v2" },
        Package () { "interrupt-names", Package () { "irq_jpege3_mmu" } },
      }
    })
  }

  //
  // Image Enhancement Processor
  //
  Device (IEP0) {
    Name (_HID, "RKCP3530")
    Name (_CID, "PRP0001")
    Name (_UID, 0)
    Name (_CCA, 0)
    Name (_DEP, Package () { \_SB.IEMU })
    Name (_STA, 0xF)

    Method (_CRS, 0x0, Serialized) {
      Name (RBUF, ResourceTemplate() {
        Memory32Fixed (ReadWrite, 0xFDBB0000, 0x500)
        Interrupt (ResourceConsumer, Level, ActiveHigh, Shared) { 149 }
      })
      Return (RBUF)
    }

    Name (_DSD, Package () {
      ToUUID("daffd814-6eba-4d8c-8a91-bc9bbf4aa301"),
      Package () {
        Package () { "compatible", "rockchip,iep-v2" },
        Package () { "interrupt-names", Package () { "irq_iep" } },
      }
    })
  }

  Device (IEMU) {
    Name (_HID, "RKCP3570")
    Name (_CID, "PRP0001")
    Name (_UID, 6)
    Name (_CCA, 0)
    Name (_STA, 0xF)

    Method (_CRS, 0x0, Serialized) {
      Name (RBUF, ResourceTemplate() {
        Memory32Fixed (ReadWrite, 0xFDBB0800, 0x100)
        Interrupt (ResourceConsumer, Level, ActiveHigh, Shared) { 149 }
      })
      Return (RBUF)
    }

    Name (_DSD, Package () {
      ToUUID("daffd814-6eba-4d8c-8a91-bc9bbf4aa301"),
      Package () {
        Package () { "compatible", "rockchip,iommu-v2" },
        Package () { "interrupt-names", Package () { "irq_iep_mmu" } },
      }
    })
  }

  //
  // RKVENC0/RKVENC1 H.264/H.265 encoder cores (CCU coordinated)
  //
  Device (RVE0) {
    Name (_HID, "RKCP3540")
    Name (_CID, "PRP0001")
    Name (_UID, 0)
    Name (_CCA, 0)
    Name (_DEP, Package () { \_SB.RECC, \_SB.RE0M })
    Name (_STA, 0xF)

    Method (_CRS, 0x0, Serialized) {
      Name (RBUF, ResourceTemplate() {
        Memory32Fixed (ReadWrite, 0xFDBD0000, 0x6000)
        Interrupt (ResourceConsumer, Level, ActiveHigh, Exclusive) { 133 }
      })
      Return (RBUF)
    }

    Name (_DSD, Package () {
      ToUUID("daffd814-6eba-4d8c-8a91-bc9bbf4aa301"),
      Package () {
        Package () { "compatible", "rockchip,rkv-encoder-v2-core" },
        Package () { "interrupt-names", Package () { "irq_rkvenc0" } },
        Package () { "rockchip,task-capacity", 8 },
      }
    })
  }

  Device (RE0M) {
    Name (_HID, "RKCP3570")
    Name (_CID, "PRP0001")
    Name (_UID, 7)
    Name (_CCA, 0)
    Name (_STA, 0xF)

    Method (_CRS, 0x0, Serialized) {
      Name (RBUF, ResourceTemplate() {
        Memory32Fixed (ReadWrite, 0xFDBDF000, 0x40)
        Memory32Fixed (ReadWrite, 0xFDBDF040, 0x40)
        Interrupt (ResourceConsumer, Level, ActiveHigh, Exclusive) { 131 }
        Interrupt (ResourceConsumer, Level, ActiveHigh, Exclusive) { 132 }
      })
      Return (RBUF)
    }

    Name (_DSD, Package () {
      ToUUID("daffd814-6eba-4d8c-8a91-bc9bbf4aa301"),
      Package () {
        Package () { "compatible", "rockchip,iommu-v2" },
        Package () { "interrupt-names", Package () { "irq_rkvenc0_mmu0", "irq_rkvenc0_mmu1" } },
        Package () { "rockchip,disable-mmu-reset", 1 },
        Package () { "rockchip,enable-cmd-retry", 1 },
        Package () { "rockchip,shootdown-entire", 1 },
      }
    })
  }

  Device (RVE1) {
    Name (_HID, "RKCP3540")
    Name (_CID, "PRP0001")
    Name (_UID, 1)
    Name (_CCA, 0)
    Name (_DEP, Package () { \_SB.RECC, \_SB.RE1M })
    Name (_STA, 0xF)

    Method (_CRS, 0x0, Serialized) {
      Name (RBUF, ResourceTemplate() {
        Memory32Fixed (ReadWrite, 0xFDBE0000, 0x6000)
        Interrupt (ResourceConsumer, Level, ActiveHigh, Exclusive) { 136 }
      })
      Return (RBUF)
    }

    Name (_DSD, Package () {
      ToUUID("daffd814-6eba-4d8c-8a91-bc9bbf4aa301"),
      Package () {
        Package () { "compatible", "rockchip,rkv-encoder-v2-core" },
        Package () { "interrupt-names", Package () { "irq_rkvenc1" } },
        Package () { "rockchip,task-capacity", 8 },
      }
    })
  }

  Device (RE1M) {
    Name (_HID, "RKCP3570")
    Name (_CID, "PRP0001")
    Name (_UID, 8)
    Name (_CCA, 0)
    Name (_STA, 0xF)

    Method (_CRS, 0x0, Serialized) {
      Name (RBUF, ResourceTemplate() {
        Memory32Fixed (ReadWrite, 0xFDBEF000, 0x40)
        Memory32Fixed (ReadWrite, 0xFDBEF040, 0x40)
        Interrupt (ResourceConsumer, Level, ActiveHigh, Exclusive) { 134 }
        Interrupt (ResourceConsumer, Level, ActiveHigh, Exclusive) { 135 }
      })
      Return (RBUF)
    }

    Name (_DSD, Package () {
      ToUUID("daffd814-6eba-4d8c-8a91-bc9bbf4aa301"),
      Package () {
        Package () { "compatible", "rockchip,iommu-v2" },
        Package () { "interrupt-names", Package () { "irq_rkvenc1_mmu0", "irq_rkvenc1_mmu1" } },
        Package () { "rockchip,disable-mmu-reset", 1 },
        Package () { "rockchip,enable-cmd-retry", 1 },
        Package () { "rockchip,shootdown-entire", 1 },
      }
    })
  }

  //
  // RKVDEC0/RKVDEC1 H.264/H.265/VP9 decoder cores (CCU coordinated)
  //
  Device (RVD0) {
    Name (_HID, "RKCP3550")
    Name (_CID, "PRP0001")
    Name (_UID, 0)
    Name (_CCA, 0)
    Name (_DEP, Package () { \_SB.RDCC, \_SB.RD0M })
    Name (_STA, 0xF)

    Method (_CRS, 0x0, Serialized) {
      Name (RBUF, ResourceTemplate() {
        Memory32Fixed (ReadWrite, 0xFDC38000, 0x800)
        Interrupt (ResourceConsumer, Level, ActiveHigh, Exclusive) { 127 }
      })
      Return (RBUF)
    }

    Name (_DSD, Package () {
      ToUUID("daffd814-6eba-4d8c-8a91-bc9bbf4aa301"),
      Package () {
        Package () { "compatible", "rockchip,rkv-decoder-v2" },
        Package () { "interrupt-names", Package () { "irq_rkvdec0" } },
        Package () { "rockchip,core-mask", 0x00010001 },
        Package () { "rockchip,task-capacity", 16 },
        Package () { "rockchip,rcb-min-width", 512 },
      }
    })
  }

  Device (RD0M) {
    Name (_HID, "RKCP3570")
    Name (_CID, "PRP0001")
    Name (_UID, 9)
    Name (_CCA, 0)
    Name (_STA, 0xF)

    Method (_CRS, 0x0, Serialized) {
      Name (RBUF, ResourceTemplate() {
        Memory32Fixed (ReadWrite, 0xFDC38700, 0x40)
        Memory32Fixed (ReadWrite, 0xFDC38740, 0x40)
        Interrupt (ResourceConsumer, Level, ActiveHigh, Exclusive) { 128 }
      })
      Return (RBUF)
    }

    Name (_DSD, Package () {
      ToUUID("daffd814-6eba-4d8c-8a91-bc9bbf4aa301"),
      Package () {
        Package () { "compatible", "rockchip,iommu-v2" },
        Package () { "interrupt-names", Package () { "irq_rkvdec0_mmu" } },
        Package () { "rockchip,disable-mmu-reset", 1 },
        Package () { "rockchip,enable-cmd-retry", 1 },
        Package () { "rockchip,shootdown-entire", 1 },
        Package () { "rockchip,master-handle-irq", 1 },
      }
    })
  }

  Device (RVD1) {
    Name (_HID, "RKCP3550")
    Name (_CID, "PRP0001")
    Name (_UID, 1)
    Name (_CCA, 0)
    Name (_DEP, Package () { \_SB.RDCC, \_SB.RD1M })
    Name (_STA, 0xF)

    Method (_CRS, 0x0, Serialized) {
      Name (RBUF, ResourceTemplate() {
        Memory32Fixed (ReadWrite, 0xFDC48000, 0x800)
        Interrupt (ResourceConsumer, Level, ActiveHigh, Exclusive) { 129 }
      })
      Return (RBUF)
    }

    Name (_DSD, Package () {
      ToUUID("daffd814-6eba-4d8c-8a91-bc9bbf4aa301"),
      Package () {
        Package () { "compatible", "rockchip,rkv-decoder-v2" },
        Package () { "interrupt-names", Package () { "irq_rkvdec1" } },
        Package () { "rockchip,core-mask", 0x00020002 },
        Package () { "rockchip,task-capacity", 16 },
        Package () { "rockchip,rcb-min-width", 512 },
      }
    })
  }

  Device (RD1M) {
    Name (_HID, "RKCP3570")
    Name (_CID, "PRP0001")
    Name (_UID, 10)
    Name (_CCA, 0)
    Name (_STA, 0xF)

    Method (_CRS, 0x0, Serialized) {
      Name (RBUF, ResourceTemplate() {
        Memory32Fixed (ReadWrite, 0xFDC48700, 0x40)
        Memory32Fixed (ReadWrite, 0xFDC48740, 0x40)
        Interrupt (ResourceConsumer, Level, ActiveHigh, Exclusive) { 130 }
      })
      Return (RBUF)
    }

    Name (_DSD, Package () {
      ToUUID("daffd814-6eba-4d8c-8a91-bc9bbf4aa301"),
      Package () {
        Package () { "compatible", "rockchip,iommu-v2" },
        Package () { "interrupt-names", Package () { "irq_rkvdec1_mmu" } },
        Package () { "rockchip,disable-mmu-reset", 1 },
        Package () { "rockchip,enable-cmd-retry", 1 },
        Package () { "rockchip,shootdown-entire", 1 },
        Package () { "rockchip,master-handle-irq", 1 },
      }
    })
  }

  //
  // AV1 decoder
  //
  Device (AV1D) {
    Name (_HID, "RKCP3560")
    Name (_CID, "PRP0001")
    Name (_UID, 0)
    Name (_CCA, 0)
    Name (_DEP, Package () { \_SB.A1MU })
    Name (_STA, 0xF)

    Method (_CRS, 0x0, Serialized) {
      Name (RBUF, ResourceTemplate() {
        Memory32Fixed (ReadWrite, 0xFDC70000, 0x800)
        Memory32Fixed (ReadWrite, 0xFDC80000, 0x400)
        Memory32Fixed (ReadWrite, 0xFDC90000, 0x400)
        Interrupt (ResourceConsumer, Level, ActiveHigh, Exclusive) { 140 }
        Interrupt (ResourceConsumer, Level, ActiveHigh, Exclusive) { 139 }
        Interrupt (ResourceConsumer, Level, ActiveHigh, Exclusive) { 138 }
      })
      Return (RBUF)
    }

    Name (_DSD, Package () {
      ToUUID("daffd814-6eba-4d8c-8a91-bc9bbf4aa301"),
      Package () {
        Package () { "compatible", "rockchip,av1-decoder" },
        Package () { "reg-names", Package () { "vcd", "cache", "afbc" } },
        Package () { "interrupt-names", Package () { "irq_av1d", "irq_cache", "irq_afbc" } },
      }
    })
  }

  Device (A1MU) {
    Name (_HID, "RKCP3571")
    Name (_CID, "PRP0001")
    Name (_UID, 0)
    Name (_CCA, 0)
    Name (_STA, 0xF)

    Method (_CRS, 0x0, Serialized) {
      Name (RBUF, ResourceTemplate() {
        Memory32Fixed (ReadWrite, 0xFDCA0000, 0x600)
        Interrupt (ResourceConsumer, Level, ActiveHigh, Exclusive) { 141 }
      })
      Return (RBUF)
    }

    Name (_DSD, Package () {
      ToUUID("daffd814-6eba-4d8c-8a91-bc9bbf4aa301"),
      Package () {
        Package () { "compatible", "rockchip,iommu-av1d" },
        Package () { "interrupt-names", Package () { "irq_av1d_mmu" } },
      }
    })
  }
}
