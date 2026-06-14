/** @file
 *
 *  SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 **/

#include <Library/CruLib.h>
#include <Library/DebugLib.h>
#include <Library/GpioLib.h>
#include <Library/PcdLib.h>
#include <Library/TimerLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>
#include <Soc.h>
#include <VarStoreData.h>

#include "DeviceConfig.h"
#include "RK3588DxeFormSetGuid.h"

STATIC
VOID
SetupStateVariable (
  IN CHAR16   *Name,
  IN UINT32   DefaultState,
  IN UINTN    TokenNumber
  )
{
  EFI_STATUS  Status;
  UINT32      State;
  UINTN       Size;

  Size   = sizeof (State);
  Status = gRT->GetVariable (
                  Name,
                  &gRK3588DxeFormSetGuid,
                  NULL,
                  &Size,
                  &State
                  );
  if (EFI_ERROR (Status) || (State > DEVICE_STATE_ENABLED)) {
    Status = LibPcdSet32S (TokenNumber, DefaultState);
    ASSERT_EFI_ERROR (Status);
  }
}

VOID
EFIAPI
SetupDeviceConfigVariables (
  VOID
  )
{
  SetupStateVariable (
    L"BluetoothState",
    FixedPcdGet32 (PcdBluetoothStateDefault),
    _PCD_TOKEN_PcdBluetoothState
    );
  SetupStateVariable (
    L"WifiState",
    FixedPcdGet32 (PcdWifiStateDefault),
    _PCD_TOKEN_PcdWifiState
    );
}

STATIC
VOID
ApplyBluetoothState (
  IN BOOLEAN  Enable
  )
{
  GpioPinSetFunction (0, GPIO_PIN_PC5, 0);
  GpioPinSetPull (0, GPIO_PIN_PC5, GPIO_PIN_PULL_UP);
  GpioPinWrite (0, GPIO_PIN_PC5, FALSE);
  GpioPinSetDirection (0, GPIO_PIN_PC5, GPIO_PIN_OUTPUT);

  GpioPinSetFunction (0, GPIO_PIN_PC6, 0);
  GpioPinSetPull (0, GPIO_PIN_PC6, GPIO_PIN_PULL_NONE);
  GpioPinWrite (0, GPIO_PIN_PC6, FALSE);
  GpioPinSetDirection (0, GPIO_PIN_PC6, GPIO_PIN_OUTPUT);

  GpioPinSetFunction (0, GPIO_PIN_PA0, 0);
  GpioPinSetPull (0, GPIO_PIN_PA0, GPIO_PIN_PULL_DOWN);
  GpioPinSetDirection (0, GPIO_PIN_PA0, GPIO_PIN_INPUT);

  if (!Enable) {
    return;
  }

  HAL_CRU_ClkEnable (PCLK_UART9_GATE);
  HAL_CRU_ClkEnable (SCLK_UART9_GATE);
  HAL_CRU_RstDeassert (SRST_P_UART9);
  HAL_CRU_RstDeassert (SRST_S_UART9);

  GpioPinSetFunction (2, GPIO_PIN_PC4, 10); // uart9_rx_m0
  GpioPinSetPull (2, GPIO_PIN_PC4, GPIO_PIN_PULL_UP);
  GpioPinSetFunction (2, GPIO_PIN_PC2, 10); // uart9_tx_m0
  GpioPinSetPull (2, GPIO_PIN_PC2, GPIO_PIN_PULL_UP);
  GpioPinSetFunction (4, GPIO_PIN_PC5, 10); // uart9m0_ctsn
  GpioPinSetPull (4, GPIO_PIN_PC5, GPIO_PIN_PULL_NONE);
  GpioPinSetFunction (4, GPIO_PIN_PC4, 10); // uart9m0_rtsn
  GpioPinSetPull (4, GPIO_PIN_PC4, GPIO_PIN_PULL_NONE);

  GpioPinWrite (0, GPIO_PIN_PC6, TRUE);
  MicroSecondDelay (200 * 1000);
}

STATIC
VOID
ApplyWifiState (
  IN BOOLEAN  Enable
  )
{
  GpioPinSetFunction (0, GPIO_PIN_PC4, 0);
  GpioPinSetPull (0, GPIO_PIN_PC4, GPIO_PIN_PULL_UP);
  GpioPinWrite (0, GPIO_PIN_PC4, FALSE);
  GpioPinSetDirection (0, GPIO_PIN_PC4, GPIO_PIN_OUTPUT);

  GpioPinSetFunction (0, GPIO_PIN_PB7, 0);
  GpioPinSetPull (0, GPIO_PIN_PB7, GPIO_PIN_PULL_DOWN);
  GpioPinSetDirection (0, GPIO_PIN_PB7, GPIO_PIN_INPUT);

  if (!Enable) {
    return;
  }

  HAL_CRU_ClkEnable (HCLK_SDIO_ROOT_GATE);
  HAL_CRU_ClkEnable (HCLK_SDIO_NIU_GATE);
  HAL_CRU_ClkEnable (HCLK_SDIO_GATE);
  HAL_CRU_ClkEnable (CCLK_SRC_SDIO_GATE);
  HAL_CRU_ClkSetFreq (CCLK_SRC_SDIO, 150000000);
  HAL_CRU_RstDeassert (SRST_H_SDIO_NIU);
  HAL_CRU_RstDeassert (SRST_H_SDIO);
  HAL_CRU_RstDeassert (SRST_SDIO);

  GpioPinSetFunction (2, GPIO_PIN_PB3, 2); // sdio_clk_m0
  GpioPinSetPull (2, GPIO_PIN_PB3, GPIO_PIN_PULL_NONE);
  GpioPinSetFunction (2, GPIO_PIN_PB2, 2); // sdio_cmd_m0
  GpioPinSetPull (2, GPIO_PIN_PB2, GPIO_PIN_PULL_NONE);
  GpioPinSetFunction (2, GPIO_PIN_PA6, 2); // sdio_d0_m0
  GpioPinSetPull (2, GPIO_PIN_PA6, GPIO_PIN_PULL_NONE);
  GpioPinSetFunction (2, GPIO_PIN_PA7, 2); // sdio_d1_m0
  GpioPinSetPull (2, GPIO_PIN_PA7, GPIO_PIN_PULL_NONE);
  GpioPinSetFunction (2, GPIO_PIN_PB0, 2); // sdio_d2_m0
  GpioPinSetPull (2, GPIO_PIN_PB0, GPIO_PIN_PULL_NONE);
  GpioPinSetFunction (2, GPIO_PIN_PB1, 2); // sdio_d3_m0
  GpioPinSetPull (2, GPIO_PIN_PB1, GPIO_PIN_PULL_NONE);

  GpioPinWrite (0, GPIO_PIN_PC4, TRUE);
  MicroSecondDelay (200 * 1000);
}

VOID
EFIAPI
ApplyDeviceConfigVariables (
  VOID
  )
{
  if (FixedPcdGetBool (PcdBluetoothSupported)) {
    ApplyBluetoothState (PcdGet32 (PcdBluetoothState) == DEVICE_STATE_ENABLED);
  }

  if (FixedPcdGetBool (PcdWifiSupported)) {
    ApplyWifiState (PcdGet32 (PcdWifiState) == DEVICE_STATE_ENABLED);
  }
}
