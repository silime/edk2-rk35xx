/** @file
 *
 *  FUSB302 USB Type-C controller driver.
 *
 *  SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 **/

#include <Uefi.h>

#include <Guid/MdeModuleHii.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/GpioLib.h>
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
#include <Protocol/Fusb302State.h>
#include <VarStoreData.h>

#include "../../../RK3588/Drivers/RK3588Dxe/RK3588DxeFormSetGuid.h"
#include "Fusb302Dxe.h"

#define FUSB302_REG_DEVICE_ID  0x01
#define FUSB302_REG_CONTROL0   0x06
#define FUSB302_REG_CONTROL2   0x08
#define FUSB302_REG_CONTROL3   0x09
#define FUSB302_REG_MASK       0x0A
#define FUSB302_REG_POWER      0x0B
#define FUSB302_REG_RESET      0x0C
#define FUSB302_REG_MASKA      0x0E
#define FUSB302_REG_MASKB      0x0F
#define FUSB302_REG_INTERRUPTA 0x3E
#define FUSB302_REG_INTERRUPTB 0x3F
#define FUSB302_REG_STATUS1A   0x3D
#define FUSB302_REG_STATUS0    0x40

#define FUSB302_CONTROL0_INT_MASK       BIT5
#define FUSB302_CONTROL2_MODE_MASK      (BIT2 | BIT1)
#define FUSB302_CONTROL2_MODE_DRP       BIT1
#define FUSB302_CONTROL2_MODE_UFP       BIT2
#define FUSB302_CONTROL2_MODE_DFP       (BIT2 | BIT1)
#define FUSB302_CONTROL2_TOGGLE         BIT0
#define FUSB302_CONTROL3_AUTO_RETRY     BIT0
#define FUSB302_CONTROL3_RETRIES_3      (BIT2 | BIT1)
#define FUSB302_MASK_VBUSOK             BIT7
#define FUSB302_MASKA_TOGDONE           BIT6
#define FUSB302_POWER_ALL               0x0F
#define FUSB302_RESET_SW                BIT0
#define FUSB302_STATUS0_VBUSOK          BIT7
#define FUSB302_STATUS1A_TOGSS_SHIFT    3
#define FUSB302_STATUS1A_TOGSS_MASK     0x07
#define FUSB302_TOGSS_RUNNING           0
#define FUSB302_TOGSS_SRC1              1
#define FUSB302_TOGSS_SRC2              2
#define FUSB302_TOGSS_SNK1              5
#define FUSB302_TOGSS_SNK2              6
#define FUSB302_TOGSS_AUDIO_ACCESSORY   7

#define FUSB302_CONTEXT_SIGNATURE  SIGNATURE_32 ('F', 'U', 'S', 'B')
#define FUSB302_POLL_INTERVAL_MS   1000

typedef struct {
  UINT32                 Signature;
  EFI_HANDLE             Controller;
  EFI_I2C_IO_PROTOCOL    *I2cIo;
  EFI_EVENT              RefreshEvent;
  EFI_HII_HANDLE         HiiHandle;
  UINT8                  DeviceId;
  UINT8                  InterruptA;
  UINT8                  InterruptB;
  UINT8                  Status0;
  UINT8                  Status1A;
  UINT8                  ToggleState;
  BOOLEAN                VbusPresent;
  UINT8                  LastLoggedToggleState;
  BOOLEAN                LastLoggedVbusPresent;
  UINT8                  LastLoggedInterruptA;
  UINT8                  LastLoggedInterruptB;
  UINT8                  LastLoggedStatus0;
  UINT8                  LastLoggedStatus1A;
  EFI_STRING_ID          FormStrings[8];
} FUSB302_CONTEXT;

STATIC CONST EFI_GUID  mI2cGuid = ROCKCHIP_I2C_DEVICE_GUID;
STATIC FUSB302_CONTEXT        *mFusb302Context;
STATIC FUSB302_STATE_PROTOCOL  mFusb302StateProtocol;

STATIC
CONST CHAR16 *
Fusb302ConfiguredRoleString (
  VOID
  )
{
  switch (PcdGet32 (PcdTypecRole)) {
    case TYPEC_ROLE_SOURCE:
      return L"Source";
    case TYPEC_ROLE_SINK:
      return L"Sink";
    case TYPEC_ROLE_DUAL:
    default:
      return L"Dual Role";
  }
}

STATIC
EFI_STATUS
EFIAPI
Fusb302GetStatus (
  IN  FUSB302_STATE_PROTOCOL  *This,
  OUT UINT32                  *DetectedRole,
  OUT BOOLEAN                 *VbusPresent
  )
{
  (VOID)This;

  if ((mFusb302Context == NULL) || (DetectedRole == NULL) || (VbusPresent == NULL)) {
    return EFI_NOT_READY;
  }

  *DetectedRole = mFusb302Context->ToggleState;
  *VbusPresent  = mFusb302Context->VbusPresent;
  return EFI_SUCCESS;
}

STATIC
VOID
EFIAPI
Fusb302Refresh (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  );

STATIC
EFI_STATUS
EFIAPI
Fusb302RefreshStatus (
  IN FUSB302_STATE_PROTOCOL  *This
  )
{
  (VOID)This;

  if (mFusb302Context == NULL) {
    return EFI_NOT_READY;
  }

  Fusb302Refresh (mFusb302Context->RefreshEvent, mFusb302Context);
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
Fusb302Read (
  IN  FUSB302_CONTEXT  *Context,
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
Fusb302Write (
  IN FUSB302_CONTEXT  *Context,
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
Fusb302UpdateBits (
  IN FUSB302_CONTEXT  *Context,
  IN UINT8            Register,
  IN UINT8            Mask,
  IN UINT8            Value
  )
{
  EFI_STATUS  Status;
  UINT8       Data;

  Status = Fusb302Read (Context, Register, &Data);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  return Fusb302Write (Context, Register, (Data & ~Mask) | (Value & Mask));
}

STATIC
EFI_STATUS
Fusb302InitializeController (
  IN FUSB302_CONTEXT  *Context
  )
{
  EFI_STATUS  Status;
  UINT8       Mode;

  switch (PcdGet32 (PcdTypecRole)) {
    case TYPEC_ROLE_SOURCE:
      Mode = FUSB302_CONTROL2_MODE_DFP;
      break;
    case TYPEC_ROLE_SINK:
      Mode = FUSB302_CONTROL2_MODE_UFP;
      break;
    case TYPEC_ROLE_DUAL:
    default:
      Mode = FUSB302_CONTROL2_MODE_DRP;
      break;
  }

  Status = Fusb302Read (Context, FUSB302_REG_DEVICE_ID, &Context->DeviceId);
  if (EFI_ERROR (Status) || (Context->DeviceId == 0) || (Context->DeviceId == 0xFF)) {
    return EFI_NOT_FOUND;
  }

  Status = Fusb302Write (Context, FUSB302_REG_RESET, FUSB302_RESET_SW);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  gBS->Stall (1000);
  Status = Fusb302Write (Context, FUSB302_REG_POWER, FUSB302_POWER_ALL);
  if (!EFI_ERROR (Status)) {
    Status = Fusb302UpdateBits (
               Context,
               FUSB302_REG_CONTROL3,
               FUSB302_CONTROL3_RETRIES_3 | FUSB302_CONTROL3_AUTO_RETRY,
               FUSB302_CONTROL3_RETRIES_3 | FUSB302_CONTROL3_AUTO_RETRY
               );
  }
  if (!EFI_ERROR (Status)) {
    Status = Fusb302Write (Context, FUSB302_REG_MASK, 0xFF & ~FUSB302_MASK_VBUSOK);
  }
  if (!EFI_ERROR (Status)) {
    Status = Fusb302Write (Context, FUSB302_REG_MASKA, 0xFF & ~FUSB302_MASKA_TOGDONE);
  }
  if (!EFI_ERROR (Status)) {
    Status = Fusb302Write (Context, FUSB302_REG_MASKB, 0xFF);
  }
  if (!EFI_ERROR (Status)) {
    Status = Fusb302UpdateBits (Context, FUSB302_REG_CONTROL0, FUSB302_CONTROL0_INT_MASK, 0);
  }
  if (!EFI_ERROR (Status)) {
    GpioPinSetFunction (0, GPIO_PIN_PD3, 0);
    GpioPinSetDirection (0, GPIO_PIN_PD3, GPIO_PIN_INPUT);
    GpioPinSetPull (0, GPIO_PIN_PD3, GPIO_PIN_PULL_UP);
    GpioPinSetInput (0, GPIO_PIN_PD3, GPIO_PIN_INPUT_SCHMITT);
  }
  if (!EFI_ERROR (Status)) {
    Status = Fusb302UpdateBits (
               Context,
               FUSB302_REG_CONTROL2,
               FUSB302_CONTROL2_MODE_MASK | FUSB302_CONTROL2_TOGGLE,
               Mode | FUSB302_CONTROL2_TOGGLE
               );
  }

  DEBUG ((DEBUG_INFO, "FUSB302: device ID 0x%02x initialized: %r\n", Context->DeviceId, Status));
  return Status;
}

STATIC
CONST CHAR16 *
Fusb302RoleString (
  IN UINT8  ToggleState
  )
{
  switch (ToggleState) {
    case FUSB302_TOGSS_SRC1:
    case FUSB302_TOGSS_SRC2:
      return L"Source";
    case FUSB302_TOGSS_SNK1:
    case FUSB302_TOGSS_SNK2:
      return L"Sink";
    case FUSB302_TOGSS_AUDIO_ACCESSORY:
      return L"Audio accessory";
    case FUSB302_TOGSS_RUNNING:
    default:
      return L"Dual-role detection";
  }
}

STATIC
EFI_STRING_ID
SetFormString (
  IN EFI_HII_HANDLE  HiiHandle,
  IN EFI_STRING_ID   *StringId,
  IN CONST CHAR16    *Format,
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
UpdateTypecForm (
  IN OUT FUSB302_CONTEXT  *Context
  )
{
  EFI_HII_HANDLE      *Handles;
  VOID                *StartHandle;
  VOID                *EndHandle;
  EFI_IFR_GUID_LABEL  *Label;

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

  Label = (EFI_IFR_GUID_LABEL *)HiiCreateGuidOpCode (StartHandle, &gEfiIfrTianoGuid, NULL, sizeof (EFI_IFR_GUID_LABEL));
  Label->ExtendOpCode = EFI_IFR_EXTEND_OP_LABEL;
  Label->Number       = TYPEC_LABEL_UPDATE;
  Label = (EFI_IFR_GUID_LABEL *)HiiCreateGuidOpCode (EndHandle, &gEfiIfrTianoGuid, NULL, sizeof (EFI_IFR_GUID_LABEL));
  Label->ExtendOpCode = EFI_IFR_EXTEND_OP_LABEL;
  Label->Number       = TYPEC_LABEL_END;

  HiiCreateTextOpCode (StartHandle, SetFormString (Context->HiiHandle, &Context->FormStrings[0], L"Controller"), 0, SetFormString (Context->HiiHandle, &Context->FormStrings[1], L"FUSB302 (ID 0x%02x)", Context->DeviceId));
  HiiCreateTextOpCode (StartHandle, SetFormString (Context->HiiHandle, &Context->FormStrings[2], L"Configured Role"), 0, SetFormString (Context->HiiHandle, &Context->FormStrings[3], L"%s", Fusb302ConfiguredRoleString ()));
  HiiCreateTextOpCode (StartHandle, SetFormString (Context->HiiHandle, &Context->FormStrings[4], L"Detected Role"), 0, SetFormString (Context->HiiHandle, &Context->FormStrings[5], L"%s", Fusb302RoleString (Context->ToggleState)));
  HiiCreateTextOpCode (StartHandle, SetFormString (Context->HiiHandle, &Context->FormStrings[6], L"VBUS Status"), 0, SetFormString (Context->HiiHandle, &Context->FormStrings[7], Context->VbusPresent ? L"Present" : L"Not present"));
  HiiUpdateForm (Context->HiiHandle, &gRK3588DxeFormSetGuid, TYPEC_FORM_ID, StartHandle, EndHandle);

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
Fusb302Refresh (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  )
{
  FUSB302_CONTEXT  *Fusb302;
  UINT8            Status0;
  UINT8            Status1A;
  UINT8            InterruptA;
  UINT8            InterruptB;

  Fusb302 = Context;
  if (EFI_ERROR (Fusb302Read (Fusb302, FUSB302_REG_INTERRUPTA, &InterruptA)) ||
      EFI_ERROR (Fusb302Read (Fusb302, FUSB302_REG_INTERRUPTB, &InterruptB)) ||
      EFI_ERROR (Fusb302Read (Fusb302, FUSB302_REG_STATUS0, &Status0)) ||
      EFI_ERROR (Fusb302Read (Fusb302, FUSB302_REG_STATUS1A, &Status1A)))
  {
    return;
  }

  Fusb302->InterruptA  = InterruptA;
  Fusb302->InterruptB  = InterruptB;
  Fusb302->Status0     = Status0;
  Fusb302->Status1A    = Status1A;
  Fusb302->VbusPresent = (Status0 & FUSB302_STATUS0_VBUSOK) != 0;
  Fusb302->ToggleState = (Status1A >> FUSB302_STATUS1A_TOGSS_SHIFT) & FUSB302_STATUS1A_TOGSS_MASK;

  if ((Fusb302->InterruptA != Fusb302->LastLoggedInterruptA) ||
      (Fusb302->InterruptB != Fusb302->LastLoggedInterruptB) ||
      (Fusb302->Status0 != Fusb302->LastLoggedStatus0) ||
      (Fusb302->Status1A != Fusb302->LastLoggedStatus1A) ||
      (Fusb302->ToggleState != Fusb302->LastLoggedToggleState) ||
      (Fusb302->VbusPresent != Fusb302->LastLoggedVbusPresent))
  {
    DEBUG ((
      DEBUG_INFO,
      "FUSB302: A=0x%02x B=0x%02x S0=0x%02x S1A=0x%02x state=%u vbus=%a\n",
      Fusb302->InterruptA,
      Fusb302->InterruptB,
      Fusb302->Status0,
      Fusb302->Status1A,
      Fusb302->ToggleState,
      Fusb302->VbusPresent ? "present" : "absent"
      ));
    Fusb302->LastLoggedInterruptA  = Fusb302->InterruptA;
    Fusb302->LastLoggedInterruptB  = Fusb302->InterruptB;
    Fusb302->LastLoggedStatus0     = Fusb302->Status0;
    Fusb302->LastLoggedStatus1A    = Fusb302->Status1A;
    Fusb302->LastLoggedToggleState = Fusb302->ToggleState;
    Fusb302->LastLoggedVbusPresent  = Fusb302->VbusPresent;
  }

  UpdateTypecForm (Fusb302);
}

STATIC
EFI_STATUS
EFIAPI
Fusb302Supported (
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
            (I2cIo->DeviceIndex == I2C_DEVICE_INDEX (PcdGet8 (PcdFusb302I2cBus), PcdGet8 (PcdFusb302I2cAddress)))) ?
           EFI_SUCCESS : EFI_UNSUPPORTED;
  gBS->CloseProtocol (Controller, &gEfiI2cIoProtocolGuid, gImageHandle, Controller);
  return Status;
}

STATIC
EFI_STATUS
EFIAPI
Fusb302Start (
  IN EFI_DRIVER_BINDING_PROTOCOL  *This,
  IN EFI_HANDLE                   Controller,
  IN EFI_DEVICE_PATH_PROTOCOL     *RemainingDevicePath OPTIONAL
  )
{
  FUSB302_CONTEXT  *Context;
  EFI_STATUS       Status;

  Context = AllocateZeroPool (sizeof (*Context));
  if (Context == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  Context->Signature  = FUSB302_CONTEXT_SIGNATURE;
  Context->Controller = Controller;
  Context->LastLoggedToggleState = 0xFF;
  Context->LastLoggedVbusPresent  = FALSE;
  Context->LastLoggedInterruptA   = 0xFF;
  Context->LastLoggedInterruptB   = 0xFF;
  Context->LastLoggedStatus0      = 0xFF;
  Context->LastLoggedStatus1A     = 0xFF;
  Status = gBS->OpenProtocol (Controller, &gEfiI2cIoProtocolGuid, (VOID **)&Context->I2cIo, gImageHandle, Controller, EFI_OPEN_PROTOCOL_BY_DRIVER);
  if (!EFI_ERROR (Status)) {
    Status = Fusb302InitializeController (Context);
  }
  if (!EFI_ERROR (Status)) {
    mFusb302Context = Context;
    Status = gBS->CreateEvent (EVT_TIMER | EVT_NOTIFY_SIGNAL, TPL_CALLBACK, Fusb302Refresh, Context, &Context->RefreshEvent);
  }
  if (!EFI_ERROR (Status)) {
    Fusb302Refresh (Context->RefreshEvent, Context);
    Status = gBS->SetTimer (Context->RefreshEvent, TimerPeriodic, FUSB302_POLL_INTERVAL_MS * 10 * 1000);
  }
  if (EFI_ERROR (Status)) {
    if (mFusb302Context == Context) {
      mFusb302Context = NULL;
    }
    gBS->CloseProtocol (Controller, &gEfiI2cIoProtocolGuid, gImageHandle, Controller);
    FreePool (Context);
  }

  return Status;
}

STATIC
EFI_STATUS
EFIAPI
Fusb302Stop (
  IN EFI_DRIVER_BINDING_PROTOCOL  *This,
  IN EFI_HANDLE                   Controller,
  IN UINTN                        NumberOfChildren,
  IN EFI_HANDLE                   *ChildHandleBuffer OPTIONAL
  )
{
  return EFI_UNSUPPORTED;
}

STATIC EFI_DRIVER_BINDING_PROTOCOL  mDriverBinding = {
  Fusb302Supported,
  Fusb302Start,
  Fusb302Stop
};

EFI_STATUS
EFIAPI
Fusb302DxeInitialize (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  mFusb302StateProtocol.GetStatus = Fusb302GetStatus;
  mFusb302StateProtocol.RefreshStatus = Fusb302RefreshStatus;
  return gBS->InstallMultipleProtocolInterfaces (
                &ImageHandle,
                &gEfiDriverBindingProtocolGuid,
                &mDriverBinding,
                &gFusb302StateProtocolGuid,
                &mFusb302StateProtocol,
                NULL
                );
}
