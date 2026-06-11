/** @file
 *
 *  TI BQ25890 charger driver.
 *
 *  SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 **/

#include <Uefi.h>

#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/GpioLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/PcdLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Pi/PiI2c.h>
#include <Protocol/DriverBinding.h>
#include <Protocol/I2c.h>
#include <Protocol/I2cIo.h>
#include <Protocol/Bq25890Control.h>
#include <BatteryCharger.h>
#include <VarStoreData.h>

#include "Bq25890Dxe.h"

#define BQ25890_REG_INPUT_CONTROL   0x00
#define BQ25890_REG_ADC_CONTROL     0x02
#define BQ25890_REG_CHARGE_CONTROL  0x03
#define BQ25890_REG_CHARGE_CURRENT  0x04
#define BQ25890_REG_PRE_TERM        0x05
#define BQ25890_REG_CHARGE_VOLTAGE  0x06
#define BQ25890_REG_TIMER_CONTROL   0x07
#define BQ25890_REG_BOOST_CONTROL   0x0A
#define BQ25890_REG_STATUS          0x0B
#define BQ25890_REG_FAULT           0x0C
#define BQ25890_REG_BATTERY_ADC     0x0E
#define BQ25890_REG_VBUS_ADC        0x11
#define BQ25890_REG_CHARGE_ADC      0x12
#define BQ25890_REG_PART_INFO       0x14

#define BQ25890_PART_NUMBER_MASK  0x38
#define BQ25890_PART_NUMBER       0x18
#define BQ25890_RESET             BIT7
#define BQ25890_WATCHDOG_MASK     (BIT5 | BIT4)
#define BQ25890_CHARGE_ENABLE     BIT4
#define BQ25890_OTG_ENABLE        BIT5
#define BQ25890_ADC_CONTINUOUS    BIT6
#define BQ25890_VBUS_GOOD         BIT7
#define BQ25890_PG_STAT           BIT2
#define BQ25890_VBUS_STAT_MASK    0xE0
#define BQ25890_VBUS_STAT_SHIFT   5
#define BQ25890_CHRG_STAT_MASK    0x18
#define BQ25890_CHRG_STAT_SHIFT   3
#define BQ25890_VBUS_STAT_OTG     7

#define BQ25890_CONTEXT_SIGNATURE  SIGNATURE_32 ('B', 'Q', '9', '0')
#define BQ25890_POLL_INTERVAL_MS   5000

typedef struct {
  UINT32                 Signature;
  EFI_HANDLE             Controller;
  EFI_I2C_IO_PROTOCOL    *I2cIo;
  EFI_EVENT              RefreshEvent;
  UINT8                  PartInfo;
  UINT8                  Status;
  UINT8                  Fault;
  UINT8                  BatteryAdc;
  UINT8                  VbusAdc;
  UINT8                  ChargeAdc;
  BOOLEAN                SourceMode;
  UINT32                 InputCurrentMa;
} BQ25890_CONTEXT;

STATIC CONST EFI_GUID  mI2cGuid = ROCKCHIP_I2C_DEVICE_GUID;
STATIC BQ25890_CONTEXT *mBq25890Context;
STATIC BQ25890_CONTROL_PROTOCOL  mBq25890ControlProtocol;

STATIC
EFI_STATUS
Bq25890Read (
  IN  BQ25890_CONTEXT  *Context,
  IN  UINT8            Register,
  OUT UINT8            *Value
  )
{
  EFI_I2C_REQUEST_PACKET  *Request;
  EFI_STATUS              Status;

  Request = AllocateZeroPool (sizeof (UINTN) + (2 * sizeof (EFI_I2C_OPERATION)));
  if (Request == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  Request->OperationCount = 2;
  Request->Operation[0].LengthInBytes = 1;
  Request->Operation[0].Buffer        = &Register;
  Request->Operation[1].Flags         = I2C_FLAG_READ;
  Request->Operation[1].LengthInBytes = 1;
  Request->Operation[1].Buffer        = Value;
  Status = Context->I2cIo->QueueRequest (Context->I2cIo, 0, NULL, Request, NULL);
  FreePool (Request);
  return Status;
}

STATIC
EFI_STATUS
Bq25890Write (
  IN BQ25890_CONTEXT  *Context,
  IN UINT8            Register,
  IN UINT8            Value
  )
{
  EFI_I2C_REQUEST_PACKET  *Request;
  EFI_STATUS              Status;
  UINT8                   Buffer[2];

  Request = AllocateZeroPool (sizeof (UINTN) + sizeof (EFI_I2C_OPERATION));
  if (Request == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  Buffer[0] = Register;
  Buffer[1] = Value;
  Request->OperationCount = 1;
  Request->Operation[0].LengthInBytes = sizeof (Buffer);
  Request->Operation[0].Buffer        = Buffer;
  Status = Context->I2cIo->QueueRequest (Context->I2cIo, 0, NULL, Request, NULL);
  FreePool (Request);
  return Status;
}

STATIC
EFI_STATUS
Bq25890UpdateBits (
  IN BQ25890_CONTEXT  *Context,
  IN UINT8            Register,
  IN UINT8            Mask,
  IN UINT8            Value
  )
{
  EFI_STATUS  Status;
  UINT8       Data;

  Status = Bq25890Read (Context, Register, &Data);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  return Bq25890Write (Context, Register, (Data & ~Mask) | (Value & Mask));
}

STATIC
EFI_STATUS
Bq25890SetInputCurrentLimit (
  IN BQ25890_CONTEXT  *Context,
  IN UINT32           MilliAmps
  )
{
  UINT32  Code;

  if (MilliAmps <= 100) {
    Code = 0;
  } else if (MilliAmps >= 3250) {
    Code = 0x3F;
  } else {
    Code = (MilliAmps - 100) / 50;
  }

Context->InputCurrentMa = 100 + (Code * 50);
  return Bq25890UpdateBits (Context, BQ25890_REG_INPUT_CONTROL, 0x3F, (UINT8)Code);
}

STATIC
VOID
EFIAPI
Bq25890Refresh (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  );

STATIC
EFI_STATUS
Bq25890SetPowerRole (
  IN BQ25890_CONTEXT  *Context,
  IN BOOLEAN          Source
  )
{
  EFI_STATUS  Status;

  Context->SourceMode = Source;

  GpioPinSetFunction (4, GPIO_PIN_PB1, 0);
  GpioPinWrite (4, GPIO_PIN_PB1, FALSE);
  GpioPinSetDirection (4, GPIO_PIN_PB1, GPIO_PIN_OUTPUT);

  Status = Bq25890UpdateBits (
             Context,
             BQ25890_REG_CHARGE_CONTROL,
             BQ25890_OTG_ENABLE | BQ25890_CHARGE_ENABLE,
             Source ? BQ25890_OTG_ENABLE : BQ25890_CHARGE_ENABLE
             );
  if (!EFI_ERROR (Status) && Source) {
    GpioPinWrite (4, GPIO_PIN_PB1, TRUE);
  } else if (!EFI_ERROR (Status)) {
    Status = Bq25890SetInputCurrentLimit (Context, 2000);
  }

  DEBUG ((DEBUG_INFO, "BQ25890: power role %a, VBUS %a: %r\n",
    Source ? "source" : "sink", Source ? "enabled" : "disabled", Status));
  return Status;
}

STATIC
EFI_STATUS
Bq25890InitializeCharger (
  IN BQ25890_CONTEXT  *Context
  )
{
  EFI_STATUS  Status;
  UINT8       Retry;

  Status = Bq25890Read (Context, BQ25890_REG_PART_INFO, &Context->PartInfo);
  if (EFI_ERROR (Status) ||
      ((Context->PartInfo & BQ25890_PART_NUMBER_MASK) != BQ25890_PART_NUMBER))
  {
    return EFI_NOT_FOUND;
  }

  Status = Bq25890Write (Context, BQ25890_REG_PART_INFO, Context->PartInfo | BQ25890_RESET);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  for (Retry = 0; Retry < 20; Retry++) {
    gBS->Stall (1000);
    Status = Bq25890Read (Context, BQ25890_REG_PART_INFO, &Context->PartInfo);
    if (!EFI_ERROR (Status) && ((Context->PartInfo & BQ25890_RESET) == 0)) {
      break;
    }
  }

  if (Retry == 20) {
    return EFI_TIMEOUT;
  }

  //
  // Values match rk3588-slpad-switch.dts and the Linux BQ25890 driver:
  // 5 A charge, 4.4 V regulation, 66 mA termination, 130 mA precharge,
  // 3.7 V (hardware maximum) SYSVMIN, 5 V boost and 1.8 A boost limit.
  //
  Status = Bq25890UpdateBits (Context, BQ25890_REG_TIMER_CONTROL, BQ25890_WATCHDOG_MASK, 0);
  if (!EFI_ERROR (Status)) {
    Status = Bq25890UpdateBits (Context, BQ25890_REG_CHARGE_CURRENT, 0x7F, 78);
  }
  if (!EFI_ERROR (Status)) {
    Status = Bq25890Write (Context, BQ25890_REG_PRE_TERM, 0x10);
  }
  if (!EFI_ERROR (Status)) {
    Status = Bq25890UpdateBits (Context, BQ25890_REG_CHARGE_VOLTAGE, 0xFC, (35 << 2));
  }
  if (!EFI_ERROR (Status)) {
    Status = Bq25890UpdateBits (Context, BQ25890_REG_CHARGE_CONTROL, 0x0E, (7 << 1));
  }
  if (!EFI_ERROR (Status)) {
    Status = Bq25890UpdateBits (Context, BQ25890_REG_BOOST_CONTROL, 0xF7, (7 << 4) | 5);
  }
  if (!EFI_ERROR (Status)) {
    Status = Bq25890UpdateBits (Context, BQ25890_REG_INPUT_CONTROL, BIT7, 0);
  }
  if (!EFI_ERROR (Status)) {
    Status = Bq25890UpdateBits (Context, BQ25890_REG_ADC_CONTROL, BQ25890_ADC_CONTINUOUS, BQ25890_ADC_CONTINUOUS);
  }
  if (!EFI_ERROR (Status)) {
    GpioPinSetFunction (4, GPIO_PIN_PB0, 0);
    GpioPinSetDirection (4, GPIO_PIN_PB0, GPIO_PIN_INPUT);
    GpioPinSetPull (4, GPIO_PIN_PB0, GPIO_PIN_PULL_UP);
    GpioPinSetInput (4, GPIO_PIN_PB0, GPIO_PIN_INPUT_SCHMITT);
  }
  if (!EFI_ERROR (Status)) {
    Status = Bq25890SetPowerRole (Context, PcdGet32 (PcdTypecRole) == TYPEC_ROLE_SOURCE);
  }

  DEBUG ((DEBUG_INFO, "BQ25890: part 0x%02x initialized: %r\n", Context->PartInfo, Status));
  return Status;
}

STATIC
EFI_STATUS
EFIAPI
Bq25890ProtocolSetPowerRole (
  IN BQ25890_CONTROL_PROTOCOL  *This,
  IN BOOLEAN                   Source
  )
{
  (VOID)This;

  if (mBq25890Context == NULL) {
    return EFI_NOT_READY;
  }

  return Bq25890SetPowerRole (mBq25890Context, Source);
}

STATIC
EFI_STATUS
EFIAPI
Bq25890ProtocolSetInputCurrentLimit (
  IN BQ25890_CONTROL_PROTOCOL  *This,
  IN UINT32                    MilliAmps
  )
{
  (VOID)This;

  if (mBq25890Context == NULL) {
    return EFI_NOT_READY;
  }

  return Bq25890SetInputCurrentLimit (mBq25890Context, MilliAmps);
}

STATIC
EFI_STATUS
EFIAPI
Bq25890RefreshStatus (
  IN BQ25890_CONTROL_PROTOCOL  *This
  )
{
  (VOID)This;

  if (mBq25890Context == NULL) {
    return EFI_NOT_READY;
  }

  Bq25890Refresh (mBq25890Context->RefreshEvent, mBq25890Context);
  return EFI_SUCCESS;
}

STATIC
VOID
EFIAPI
Bq25890Refresh (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  )
{
  BQ25890_CONTEXT  *Bq25890;
  UINT32           BatteryVoltageMv;
  UINT32           ChargeCurrentMa;
  UINT32           ChargePowerMw;
  UINT32           ChargeStatus;
  UINT32           InputSource;
  UINT32           VbusVoltageMv;
  UINT8            FaultSnapshot;

  Bq25890 = Context;
  if (EFI_ERROR (Bq25890Read (Bq25890, BQ25890_REG_STATUS, &Bq25890->Status)) ||
      EFI_ERROR (Bq25890Read (Bq25890, BQ25890_REG_BATTERY_ADC, &Bq25890->BatteryAdc)) ||
      EFI_ERROR (Bq25890Read (Bq25890, BQ25890_REG_VBUS_ADC, &Bq25890->VbusAdc)) ||
      EFI_ERROR (Bq25890Read (Bq25890, BQ25890_REG_CHARGE_ADC, &Bq25890->ChargeAdc)))
  {
    PcdSetBoolS (PcdBq25890StatusValid, FALSE);
    return;
  }

  if (EFI_ERROR (Bq25890Read (Bq25890, BQ25890_REG_FAULT, &FaultSnapshot)) ||
      EFI_ERROR (Bq25890Read (Bq25890, BQ25890_REG_FAULT, &Bq25890->Fault)))
  {
    PcdSetBoolS (PcdBq25890StatusValid, FALSE);
    return;
  }

  (VOID)FaultSnapshot;

  InputSource      = (Bq25890->Status & BQ25890_VBUS_STAT_MASK) >> BQ25890_VBUS_STAT_SHIFT;
  BatteryVoltageMv = 2304 + ((Bq25890->BatteryAdc & 0x7F) * 20);
  VbusVoltageMv    = (Bq25890->VbusAdc & BQ25890_VBUS_GOOD) != 0 ?
                     2600 + ((Bq25890->VbusAdc & 0x7F) * 100) : 0;
  ChargeCurrentMa  = (Bq25890->ChargeAdc & 0x7F) * 50;
  ChargePowerMw    = (BatteryVoltageMv * ChargeCurrentMa) / 1000;

  if (Bq25890->Fault != 0) {
    ChargeStatus = BATTERY_CHARGE_STATUS_FAULT;
  } else if (InputSource == BQ25890_VBUS_STAT_OTG) {
    ChargeStatus = BATTERY_CHARGE_STATUS_OTG;
  } else {
    switch ((Bq25890->Status & BQ25890_CHRG_STAT_MASK) >> BQ25890_CHRG_STAT_SHIFT) {
      case 1:
        ChargeStatus = BATTERY_CHARGE_STATUS_PRECHARGE;
        break;
      case 2:
        ChargeStatus = BATTERY_CHARGE_STATUS_FAST_CHARGE;
        break;
      case 3:
        ChargeStatus = BATTERY_CHARGE_STATUS_DONE;
        break;
      case 0:
      default:
        ChargeStatus = ((Bq25890->Status & BQ25890_PG_STAT) != 0) ?
                       BATTERY_CHARGE_STATUS_NOT_CHARGING :
                       BATTERY_CHARGE_STATUS_DISCHARGING;
        break;
    }
  }

  PcdSet32S (PcdBq25890ChargeStatus, ChargeStatus);
  PcdSet32S (PcdBq25890InputSource, InputSource);
  PcdSet32S (PcdBq25890Fault, Bq25890->Fault);
  PcdSet32S (PcdBq25890BatteryVoltageMv, BatteryVoltageMv);
  PcdSet32S (PcdBq25890ChargeCurrentMa, ChargeCurrentMa);
  PcdSet32S (PcdBq25890ChargePowerMw, ChargePowerMw);
  PcdSet32S (PcdBq25890VbusVoltageMv, VbusVoltageMv);
  PcdSetBoolS (PcdBq25890StatusValid, TRUE);

  DEBUG ((
    DEBUG_VERBOSE,
    "BQ25890: source=%u charge-state=%u fault=0x%02x battery=%u mV current=%u mA power=%u mW vbus=%u mV\n",
    InputSource,
    ChargeStatus,
    Bq25890->Fault,
    BatteryVoltageMv,
    ChargeCurrentMa,
    ChargePowerMw,
    VbusVoltageMv
    ));
}

STATIC
EFI_STATUS
EFIAPI
Bq25890Supported (
  IN EFI_DRIVER_BINDING_PROTOCOL  *This,
  IN EFI_HANDLE                   Controller,
  IN EFI_DEVICE_PATH_PROTOCOL     *RemainingDevicePath OPTIONAL
  )
{
  EFI_I2C_IO_PROTOCOL  *I2cIo;
  EFI_STATUS           Status;

  Status = gBS->OpenProtocol (Controller, &gEfiI2cIoProtocolGuid, (VOID **)&I2cIo, gImageHandle, Controller, EFI_OPEN_PROTOCOL_BY_DRIVER);
  if (EFI_ERROR (Status)) {
    return EFI_UNSUPPORTED;
  }

  Status = (CompareGuid (I2cIo->DeviceGuid, &mI2cGuid) &&
            (I2cIo->DeviceIndex == I2C_DEVICE_INDEX (PcdGet8 (PcdBq25890I2cBus), PcdGet8 (PcdBq25890I2cAddress)))) ?
           EFI_SUCCESS : EFI_UNSUPPORTED;
  gBS->CloseProtocol (Controller, &gEfiI2cIoProtocolGuid, gImageHandle, Controller);
  return Status;
}

STATIC
EFI_STATUS
EFIAPI
Bq25890Start (
  IN EFI_DRIVER_BINDING_PROTOCOL  *This,
  IN EFI_HANDLE                   Controller,
  IN EFI_DEVICE_PATH_PROTOCOL     *RemainingDevicePath OPTIONAL
  )
{
  BQ25890_CONTEXT  *Context;
  EFI_STATUS       Status;

  Context = AllocateZeroPool (sizeof (*Context));
  if (Context == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  Context->Signature  = BQ25890_CONTEXT_SIGNATURE;
  Context->Controller = Controller;
  Status = gBS->OpenProtocol (Controller, &gEfiI2cIoProtocolGuid, (VOID **)&Context->I2cIo, gImageHandle, Controller, EFI_OPEN_PROTOCOL_BY_DRIVER);
  if (!EFI_ERROR (Status)) {
    Status = Bq25890InitializeCharger (Context);
  }
  if (!EFI_ERROR (Status)) {
    mBq25890Context = Context;
    Status = gBS->CreateEvent (EVT_TIMER | EVT_NOTIFY_SIGNAL, TPL_CALLBACK, Bq25890Refresh, Context, &Context->RefreshEvent);
  }
  if (!EFI_ERROR (Status)) {
    Bq25890Refresh (Context->RefreshEvent, Context);
    Status = gBS->SetTimer (Context->RefreshEvent, TimerPeriodic, BQ25890_POLL_INTERVAL_MS * 10 * 1000);
  }
  if (EFI_ERROR (Status)) {
    if (mBq25890Context == Context) {
      mBq25890Context = NULL;
    }
    gBS->CloseProtocol (Controller, &gEfiI2cIoProtocolGuid, gImageHandle, Controller);
    FreePool (Context);
  }

  return Status;
}

STATIC
EFI_STATUS
EFIAPI
Bq25890Stop (
  IN EFI_DRIVER_BINDING_PROTOCOL  *This,
  IN EFI_HANDLE                   Controller,
  IN UINTN                        NumberOfChildren,
  IN EFI_HANDLE                   *ChildHandleBuffer OPTIONAL
  )
{
  return EFI_UNSUPPORTED;
}

STATIC EFI_DRIVER_BINDING_PROTOCOL  mDriverBinding = {
  Bq25890Supported,
  Bq25890Start,
  Bq25890Stop
};

EFI_STATUS
EFIAPI
Bq25890DxeInitialize (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  mBq25890ControlProtocol.SetPowerRole        = Bq25890ProtocolSetPowerRole;
  mBq25890ControlProtocol.SetInputCurrentLimit = Bq25890ProtocolSetInputCurrentLimit;
  mBq25890ControlProtocol.RefreshStatus        = Bq25890RefreshStatus;

  return gBS->InstallMultipleProtocolInterfaces (
                &ImageHandle,
                &gEfiDriverBindingProtocolGuid,
                &mDriverBinding,
                &gBq25890ControlProtocolGuid,
                &mBq25890ControlProtocol,
                NULL
                );
}
