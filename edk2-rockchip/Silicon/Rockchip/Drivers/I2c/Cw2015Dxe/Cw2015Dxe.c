/** @file
 *
 *  CW2015 gas gauge driver.
 *
 *  SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 **/

#include <Uefi.h>

#include <Guid/MdeModuleHii.h>
#include <Library/BaseMemoryLib.h>
#include <Library/BaseLib.h>
#include <Library/DebugLib.h>
#include <Library/HiiLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/PcdLib.h>
#include <Library/PrintLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include <Pi/PiI2c.h>
#include <Protocol/DriverBinding.h>
#include <Protocol/I2c.h>
#include <Protocol/I2cIo.h>
#include <BatteryCharger.h>

#include "../../../RK3588/Drivers/RK3588Dxe/RK3588DxeFormSetGuid.h"
#include "Cw2015Dxe.h"

#define CW2015_REG_VERSION    0x00
#define CW2015_REG_VCELL      0x02
#define CW2015_REG_SOC        0x04
#define CW2015_REG_RRT_ALERT  0x06
#define CW2015_REG_CONFIG     0x08
#define CW2015_REG_MODE       0x0A
#define CW2015_REG_BATINFO    0x10

#define CW2015_BATINFO_SIZE        64
#define CW2015_MODE_SLEEP_MASK     (BIT7 | BIT6)
#define CW2015_MODE_SLEEP          (BIT7 | BIT6)
#define CW2015_MODE_NORMAL         0
#define CW2015_MODE_RESTART        0x0F
#define CW2015_CONFIG_UPDATE_FLAG  BIT1
#define CW2015_CONFIG_ATHD_MASK    0xF8
#define CW2015_POLL_INTERVAL_MS    5000

#define CW2015_CONTEXT_SIGNATURE  SIGNATURE_32 ('C', 'W', '2', '0')

typedef struct {
  UINT32                 Signature;
  EFI_HANDLE             Controller;
  EFI_I2C_IO_PROTOCOL    *I2cIo;
  EFI_EVENT              RefreshEvent;
  EFI_HII_HANDLE         HiiHandle;
  BOOLEAN                FormInstalled;
  UINT8                  Version;
  UINT8                  Soc;
  UINT16                 VoltageMv;
  UINT16                 RemainingMinutes;
  EFI_STRING_ID          FormStrings[22];
} CW2015_CONTEXT;

STATIC CONST EFI_GUID  mI2cGuid = ROCKCHIP_I2C_DEVICE_GUID;

STATIC CONST UINT8  mBatteryProfile[CW2015_BATINFO_SIZE] = {
  0x18, 0x0A, 0x68, 0x68, 0x6B, 0x6B, 0x69, 0x67,
  0x63, 0x61, 0x5E, 0x61, 0x5E, 0x53, 0x46, 0x3F,
  0x36, 0x30, 0x29, 0x26, 0x2C, 0x37, 0x44, 0x4E,
  0x1E, 0x6F, 0x0A, 0x3E, 0x18, 0x31, 0x52, 0x60,
  0x6F, 0x6E, 0x6E, 0x70, 0x3E, 0x1B, 0x6E, 0x5F,
  0x0B, 0x2F, 0x21, 0x4F, 0x89, 0x92, 0x96, 0x1B,
  0x49, 0x69, 0x9D, 0xBC, 0x80, 0x5C, 0x7F, 0xCB,
  0x2F, 0x00, 0x64, 0xA5, 0xB5, 0xC1, 0x46, 0xAE
};

STATIC
EFI_STATUS
Cw2015Read (
  IN  CW2015_CONTEXT  *Context,
  IN  UINT8           Register,
  OUT UINT8           *Data,
  IN  UINTN           Length
  )
{
  EFI_I2C_REQUEST_PACKET  *Request;
  EFI_STATUS              Status;
  UINTN                   RequestSize;

  RequestSize = sizeof (UINTN) + (2 * sizeof (EFI_I2C_OPERATION));
  Request     = AllocateZeroPool (RequestSize);
  if (Request == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  Request->OperationCount = 2;
  Request->Operation[0].LengthInBytes = sizeof (Register);
  Request->Operation[0].Buffer        = &Register;
  Request->Operation[1].Flags         = I2C_FLAG_READ;
  Request->Operation[1].LengthInBytes = Length;
  Request->Operation[1].Buffer        = Data;

  Status = Context->I2cIo->QueueRequest (Context->I2cIo, 0, NULL, Request, NULL);
  FreePool (Request);
  return Status;
}

STATIC
EFI_STATUS
Cw2015Write (
  IN CW2015_CONTEXT  *Context,
  IN UINT8           Register,
  IN CONST UINT8     *Data,
  IN UINTN           Length
  )
{
  EFI_I2C_REQUEST_PACKET  *Request;
  EFI_STATUS              Status;
  UINT8                   *Buffer;

  Request = AllocateZeroPool (sizeof (UINTN) + sizeof (EFI_I2C_OPERATION));
  Buffer  = AllocatePool (Length + 1);
  if ((Request == NULL) || (Buffer == NULL)) {
    Status = EFI_OUT_OF_RESOURCES;
    goto Exit;
  }

  Buffer[0] = Register;
  CopyMem (&Buffer[1], Data, Length);

  Request->OperationCount = 1;
  Request->Operation[0].LengthInBytes = Length + 1;
  Request->Operation[0].Buffer        = Buffer;
  Status = Context->I2cIo->QueueRequest (Context->I2cIo, 0, NULL, Request, NULL);

Exit:
  if (Buffer != NULL) {
    FreePool (Buffer);
  }
  if (Request != NULL) {
    FreePool (Request);
  }
  return Status;
}

STATIC
EFI_STATUS
Cw2015WriteByte (
  IN CW2015_CONTEXT  *Context,
  IN UINT8           Register,
  IN UINT8           Value
  )
{
  return Cw2015Write (Context, Register, &Value, sizeof (Value));
}

STATIC
EFI_STATUS
Cw2015UpdateProfile (
  IN CW2015_CONTEXT  *Context,
  IN UINT8           Mode,
  IN UINT8           Config
  )
{
  EFI_STATUS  Status;
  UINT8       Soc;
  UINTN       Retry;

  Status = Cw2015Write (Context, CW2015_REG_BATINFO, mBatteryProfile, sizeof (mBatteryProfile));
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Config &= (UINT8)~CW2015_CONFIG_ATHD_MASK;
  Config |= CW2015_CONFIG_UPDATE_FLAG;
  Status  = Cw2015WriteByte (Context, CW2015_REG_CONFIG, Config);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Mode  &= (UINT8)~CW2015_MODE_RESTART;
  Status = Cw2015WriteByte (Context, CW2015_REG_MODE, Mode | CW2015_MODE_RESTART);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  gBS->Stall (20 * 1000);
  Status = Cw2015WriteByte (Context, CW2015_REG_MODE, Mode);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  for (Retry = 0; Retry < 1000; Retry++) {
    Status = Cw2015Read (Context, CW2015_REG_SOC, &Soc, 1);
    if (!EFI_ERROR (Status) && (Soc <= 100)) {
      DEBUG ((DEBUG_INFO, "CW2015: battery profile updated\n"));
      return EFI_SUCCESS;
    }
    gBS->Stall (10 * 1000);
  }

  return EFI_TIMEOUT;
}

STATIC
EFI_STATUS
Cw2015InitializeGauge (
  IN CW2015_CONTEXT  *Context
  )
{
  EFI_STATUS  Status;
  UINT8       Config;
  UINT8       Mode;
  UINT8       StoredProfile[CW2015_BATINFO_SIZE];

  Status = Cw2015Read (Context, CW2015_REG_MODE, &Mode, 1);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  if ((Mode & CW2015_MODE_SLEEP_MASK) == CW2015_MODE_SLEEP) {
    Mode   = CW2015_MODE_NORMAL;
    Status = Cw2015WriteByte (Context, CW2015_REG_MODE, Mode);
    if (EFI_ERROR (Status)) {
      return Status;
    }
  }

  Status = Cw2015Read (Context, CW2015_REG_CONFIG, &Config, 1);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  if ((Config & CW2015_CONFIG_UPDATE_FLAG) != 0) {
    Status = Cw2015Read (Context, CW2015_REG_BATINFO, StoredProfile, sizeof (StoredProfile));
    if (EFI_ERROR (Status)) {
      return Status;
    }

    if (CompareMem (StoredProfile, mBatteryProfile, sizeof (mBatteryProfile)) == 0) {
      return EFI_SUCCESS;
    }
  }

  DEBUG ((DEBUG_INFO, "CW2015: uploading battery profile\n"));
  return Cw2015UpdateProfile (Context, Mode, Config);
}

STATIC
EFI_STATUS
Cw2015Refresh (
  IN OUT CW2015_CONTEXT  *Context
  )
{
  EFI_STATUS  Status;
  UINT8       Data[2];
  UINT32      Raw;
  UINTN       Sample;

  Status = Cw2015Read (Context, CW2015_REG_VERSION, &Context->Version, 1);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = Cw2015Read (Context, CW2015_REG_SOC, Data, sizeof (Data));
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Context->Soc = MIN (Data[0], 100);

  Raw = 0;
  for (Sample = 0; Sample < 3; Sample++) {
    Status = Cw2015Read (Context, CW2015_REG_VCELL, Data, sizeof (Data));
    if (EFI_ERROR (Status)) {
      return Status;
    }
    Raw += (Data[0] << 8) | Data[1];
  }

  Raw                /= 3;
  Context->VoltageMv  = (UINT16)((Raw * 312) / 1024);

  Status = Cw2015Read (Context, CW2015_REG_RRT_ALERT, Data, sizeof (Data));
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Context->RemainingMinutes = ((Data[0] << 8) | Data[1]) & 0x1FFF;
  return EFI_SUCCESS;
}

STATIC
CONST CHAR16 *
ChargeStatusString (
  IN UINT32  Status
  )
{
  switch (Status) {
    case BATTERY_CHARGE_STATUS_NOT_CHARGING:
      return L"Not charging";
    case BATTERY_CHARGE_STATUS_PRECHARGE:
      return L"Pre-charge";
    case BATTERY_CHARGE_STATUS_FAST_CHARGE:
      return L"Fast charging";
    case BATTERY_CHARGE_STATUS_DONE:
      return L"Charge complete";
    case BATTERY_CHARGE_STATUS_DISCHARGING:
      return L"Discharging";
    case BATTERY_CHARGE_STATUS_OTG:
      return L"Discharging (OTG source)";
    case BATTERY_CHARGE_STATUS_FAULT:
      return L"Charger fault";
    case BATTERY_CHARGE_STATUS_UNKNOWN:
    default:
      return L"Unknown";
  }
}

STATIC
CONST CHAR16 *
InputSourceString (
  IN UINT32  Source
  )
{
  STATIC CONST CHAR16  *Sources[] = {
    L"No input", L"USB SDP", L"USB CDP", L"USB DCP",
    L"High-voltage DCP", L"Unknown adapter", L"Non-standard adapter", L"OTG"
  };

  return Source < ARRAY_SIZE (Sources) ? Sources[Source] : L"Unknown";
}

STATIC
CONST CHAR16 *
ChargerFaultString (
  IN UINT32  Fault
  )
{
  if ((Fault & BIT7) != 0) {
    return L"Watchdog expired";
  }
  if ((Fault & BIT6) != 0) {
    return L"Boost/OTG fault";
  }

  switch ((Fault >> 4) & 0x3) {
    case 1:
      return L"Input fault";
    case 2:
      return L"Thermal shutdown";
    case 3:
      return L"Charge safety timer expired";
    default:
      break;
  }

  if ((Fault & BIT3) != 0) {
    return L"Battery overvoltage";
  }

  switch (Fault & 0x7) {
    case 2:
      return L"Battery warm";
    case 3:
      return L"Battery cool";
    case 5:
      return L"Battery cold";
    case 6:
      return L"Battery hot";
    default:
      return L"None";
  }
}

STATIC
EFI_STRING_ID
SetFormString (
  IN     EFI_HII_HANDLE  HiiHandle,
  IN OUT EFI_STRING_ID   *StringId,
  IN     CHAR16          *Format,
  ...
  )
{
  VA_LIST  Marker;
  CHAR16   String[64];

  VA_START (Marker, Format);
  UnicodeVSPrint (String, sizeof (String), Format, Marker);
  VA_END (Marker);
  *StringId = HiiSetString (HiiHandle, *StringId, String, NULL);
  return *StringId;
}

STATIC
VOID
UpdateBatteryForm (
  IN OUT CW2015_CONTEXT  *Context
  )
{
  EFI_HII_HANDLE      *Handles;
  VOID                *StartHandle;
  VOID                *EndHandle;
  EFI_IFR_GUID_LABEL  *Label;
  UINT32              ChargeStatus;

  if (Context->HiiHandle == NULL) {
    Handles = HiiGetHiiHandles (&gRK3588DxeFormSetGuid);
    if ((Handles == NULL) || (Handles[0] == NULL)) {
      if (Handles != NULL) {
        FreePool (Handles);
      }
      return;
    }

    Context->HiiHandle = Handles[0];
    FreePool (Handles);
  }

  StartHandle = HiiAllocateOpCodeHandle ();
  EndHandle   = HiiAllocateOpCodeHandle ();
  if ((StartHandle == NULL) || (EndHandle == NULL)) {
    goto Exit;
  }

  Label = (EFI_IFR_GUID_LABEL *)HiiCreateGuidOpCode (
                                  StartHandle,
                                  &gEfiIfrTianoGuid,
                                  NULL,
                                  sizeof (EFI_IFR_GUID_LABEL)
                                  );
  Label->ExtendOpCode = EFI_IFR_EXTEND_OP_LABEL;
  Label->Number       = BATTERY_LABEL_UPDATE;

  Label = (EFI_IFR_GUID_LABEL *)HiiCreateGuidOpCode (
                                  EndHandle,
                                  &gEfiIfrTianoGuid,
                                  NULL,
                                  sizeof (EFI_IFR_GUID_LABEL)
                                  );
  Label->ExtendOpCode = EFI_IFR_EXTEND_OP_LABEL;
  Label->Number       = BATTERY_LABEL_END;

  HiiCreateTextOpCode (StartHandle, SetFormString (Context->HiiHandle, &Context->FormStrings[0], L"Gauge"), 0, SetFormString (Context->HiiHandle, &Context->FormStrings[1], L"CW2015 (version 0x%02x)", Context->Version));
  HiiCreateTextOpCode (StartHandle, SetFormString (Context->HiiHandle, &Context->FormStrings[2], L"State of Charge"), 0, SetFormString (Context->HiiHandle, &Context->FormStrings[3], L"%u%%", Context->Soc));
  HiiCreateTextOpCode (StartHandle, SetFormString (Context->HiiHandle, &Context->FormStrings[4], L"Voltage"), 0, SetFormString (Context->HiiHandle, &Context->FormStrings[5], L"%u mV", Context->VoltageMv));
  HiiCreateTextOpCode (
    StartHandle,
    SetFormString (Context->HiiHandle, &Context->FormStrings[6], L"Remaining Time"),
    0,
    SetFormString (
      Context->HiiHandle,
      &Context->FormStrings[7],
      L"%u hours %u minutes",
      Context->RemainingMinutes / 60,
      Context->RemainingMinutes % 60
      )
    );
  if (!PcdGetBool (PcdBq25890StatusValid)) {
    HiiCreateTextOpCode (StartHandle, SetFormString (Context->HiiHandle, &Context->FormStrings[8], L"Charging Status"), 0, SetFormString (Context->HiiHandle, &Context->FormStrings[9], L"Unavailable"));
  } else {
    ChargeStatus = PcdGet32 (PcdBq25890ChargeStatus);
    HiiCreateTextOpCode (StartHandle, SetFormString (Context->HiiHandle, &Context->FormStrings[8], L"Charging Status"), 0, SetFormString (Context->HiiHandle, &Context->FormStrings[9], L"%s", ChargeStatusString (ChargeStatus)));
    HiiCreateTextOpCode (StartHandle, SetFormString (Context->HiiHandle, &Context->FormStrings[10], L"Charger Input"), 0, SetFormString (Context->HiiHandle, &Context->FormStrings[11], L"%s (%u mV)", InputSourceString (PcdGet32 (PcdBq25890InputSource)), PcdGet32 (PcdBq25890VbusVoltageMv)));
    HiiCreateTextOpCode (StartHandle, SetFormString (Context->HiiHandle, &Context->FormStrings[12], L"Charge Current"), 0, SetFormString (Context->HiiHandle, &Context->FormStrings[13], L"%u mA", PcdGet32 (PcdBq25890ChargeCurrentMa)));
    if ((ChargeStatus == BATTERY_CHARGE_STATUS_PRECHARGE) ||
        (ChargeStatus == BATTERY_CHARGE_STATUS_FAST_CHARGE))
    {
      HiiCreateTextOpCode (StartHandle, SetFormString (Context->HiiHandle, &Context->FormStrings[14], L"Charge/Discharge Power"), 0, SetFormString (Context->HiiHandle, &Context->FormStrings[15], L"%u mW charging", PcdGet32 (PcdBq25890ChargePowerMw)));
    } else if ((ChargeStatus == BATTERY_CHARGE_STATUS_DISCHARGING) ||
               (ChargeStatus == BATTERY_CHARGE_STATUS_OTG))
    {
      HiiCreateTextOpCode (StartHandle, SetFormString (Context->HiiHandle, &Context->FormStrings[14], L"Charge/Discharge Power"), 0, SetFormString (Context->HiiHandle, &Context->FormStrings[15], L"Unavailable while discharging"));
    } else {
      HiiCreateTextOpCode (StartHandle, SetFormString (Context->HiiHandle, &Context->FormStrings[14], L"Charge/Discharge Power"), 0, SetFormString (Context->HiiHandle, &Context->FormStrings[15], L"0 mW"));
    }
    HiiCreateTextOpCode (StartHandle, SetFormString (Context->HiiHandle, &Context->FormStrings[16], L"Charger Battery ADC"), 0, SetFormString (Context->HiiHandle, &Context->FormStrings[17], L"%u mV", PcdGet32 (PcdBq25890BatteryVoltageMv)));
    HiiCreateTextOpCode (StartHandle, SetFormString (Context->HiiHandle, &Context->FormStrings[18], L"Charger Fault"), 0, SetFormString (Context->HiiHandle, &Context->FormStrings[19], L"%s (0x%02x)", ChargerFaultString (PcdGet32 (PcdBq25890Fault)), PcdGet32 (PcdBq25890Fault)));
  }

  HiiUpdateForm (Context->HiiHandle, &gRK3588DxeFormSetGuid, BATTERY_FORM_ID, StartHandle, EndHandle);
  Context->FormInstalled = TRUE;

Exit:
  if (StartHandle != NULL) {
    HiiFreeOpCodeHandle (StartHandle);
  }
  if (EndHandle != NULL) {
    HiiFreeOpCodeHandle (EndHandle);
  }
}

STATIC
VOID
EFIAPI
RefreshNotify (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  )
{
  CW2015_CONTEXT  *Cw2015;

  Cw2015 = Context;
  if (EFI_ERROR (Cw2015Refresh (Cw2015))) {
    return;
  }

  UpdateBatteryForm (Cw2015);
}

STATIC
EFI_STATUS
EFIAPI
Cw2015Supported (
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
            (I2cIo->DeviceIndex == I2C_DEVICE_INDEX (PcdGet8 (PcdBatteryI2cBus), PcdGet8 (PcdBatteryI2cAddress)))) ?
           EFI_SUCCESS : EFI_UNSUPPORTED;
  gBS->CloseProtocol (Controller, &gEfiI2cIoProtocolGuid, gImageHandle, Controller);
  return Status;
}

STATIC
EFI_STATUS
EFIAPI
Cw2015Start (
  IN EFI_DRIVER_BINDING_PROTOCOL  *This,
  IN EFI_HANDLE                   Controller,
  IN EFI_DEVICE_PATH_PROTOCOL     *RemainingDevicePath OPTIONAL
  )
{
  CW2015_CONTEXT  *Context;
  EFI_STATUS      Status;

  Context = AllocateZeroPool (sizeof (*Context));
  if (Context == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  Context->Signature  = CW2015_CONTEXT_SIGNATURE;
  Context->Controller = Controller;
  Status = gBS->OpenProtocol (Controller, &gEfiI2cIoProtocolGuid, (VOID **)&Context->I2cIo, gImageHandle, Controller, EFI_OPEN_PROTOCOL_BY_DRIVER);
  if (EFI_ERROR (Status)) {
    FreePool (Context);
    return Status;
  }

  Status = Cw2015InitializeGauge (Context);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "CW2015: gauge initialization failed: %r\n", Status));
    gBS->CloseProtocol (Controller, &gEfiI2cIoProtocolGuid, gImageHandle, Controller);
    FreePool (Context);
    return Status;
  }

  Status = gBS->CreateEvent (EVT_TIMER | EVT_NOTIFY_SIGNAL, TPL_CALLBACK, RefreshNotify, Context, &Context->RefreshEvent);
  if (EFI_ERROR (Status)) {
    gBS->CloseProtocol (Controller, &gEfiI2cIoProtocolGuid, gImageHandle, Controller);
    FreePool (Context);
    return Status;
  }

  RefreshNotify (Context->RefreshEvent, Context);
  return gBS->SetTimer (
                Context->RefreshEvent,
                TimerPeriodic,
                CW2015_POLL_INTERVAL_MS * 10 * 1000
                );
}

STATIC
EFI_STATUS
EFIAPI
Cw2015Stop (
  IN EFI_DRIVER_BINDING_PROTOCOL  *This,
  IN EFI_HANDLE                   Controller,
  IN UINTN                        NumberOfChildren,
  IN EFI_HANDLE                   *ChildHandleBuffer OPTIONAL
  )
{
  return EFI_UNSUPPORTED;
}

STATIC EFI_DRIVER_BINDING_PROTOCOL  mDriverBinding = {
  Cw2015Supported,
  Cw2015Start,
  Cw2015Stop
};

EFI_STATUS
EFIAPI
Cw2015DxeInitialize (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  return gBS->InstallMultipleProtocolInterfaces (
                &ImageHandle,
                &gEfiDriverBindingProtocolGuid,
                &mDriverBinding,
                NULL
                );
}
