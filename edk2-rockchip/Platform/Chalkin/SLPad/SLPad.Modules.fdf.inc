## @file
#
#  Copyright (c) 2023-2024, Mario Bălănică <mariobalanica02@gmail.com>
#
#  SPDX-License-Identifier: BSD-2-Clause-Patent
#
##

  # ACPI Support
  INF RuleOverride = ACPITABLE $(PLATFORM_DIRECTORY)/AcpiTables/AcpiTables.inf

  # Device Tree Support
  INF RuleOverride = DTB $(PLATFORM_DIRECTORY)/DeviceTree/Vendor.inf

  # Splash screen logo
  INF $(PLATFORM_DIRECTORY)/Drivers/LogoDxe/LogoDxe.inf

  # Power and volume buttons
  INF Silicon/Rockchip/RK3588/Drivers/ButtonDxe/ButtonDxe.inf
  INF Silicon/Rockchip/Drivers/I2c/Cw2015Dxe/Cw2015Dxe.inf
  INF Silicon/Rockchip/Drivers/I2c/Bq25890Dxe/Bq25890Dxe.inf
  INF Silicon/Rockchip/Drivers/I2c/Fusb302Dxe/Fusb302Dxe.inf
  INF Silicon/Rockchip/Drivers/I2c/TypecPowerPolicyDxe/TypecPowerPolicyDxe.inf
