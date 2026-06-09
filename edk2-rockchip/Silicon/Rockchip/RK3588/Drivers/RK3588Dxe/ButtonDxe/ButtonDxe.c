/** @file
  RK3588 power and volume button input driver.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <Uefi.h>

#include <Guid/ConsoleInDevice.h>
#include <Protocol/DevicePath.h>
#include <Protocol/SimpleTextIn.h>

#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/RK806.h>
#include <Library/SaradcLib.h>
#include <Library/UefiBootServicesTableLib.h>

#define BUTTON_POLL_INTERVAL  500000
#define BUTTON_QUEUE_SIZE     8
#define BUTTON_ADC_LOG_DELTA     20

typedef struct {
  VENDOR_DEVICE_PATH          Vendor;
  EFI_DEVICE_PATH_PROTOCOL    End;
} BUTTON_DEVICE_PATH;

typedef struct {
  EFI_SIMPLE_TEXT_INPUT_PROTOCOL    SimpleTextIn;
  EFI_HANDLE                        Handle;
  EFI_EVENT                         PollEvent;
  EFI_INPUT_KEY                     Queue[BUTTON_QUEUE_SIZE];
  UINTN                             QueueHead;
  UINTN                             QueueTail;
  SARADC_KEY                        LastSaradcKey;
  UINT32                            LastAdcData;
} BUTTON_DEVICE;

STATIC BUTTON_DEVICE  mButtonDevice;

STATIC BUTTON_DEVICE_PATH  mButtonDevicePath = {
  {
    {
      HARDWARE_DEVICE_PATH,
      HW_VENDOR_DP,
      {
        (UINT8)sizeof (VENDOR_DEVICE_PATH),
        (UINT8)(sizeof (VENDOR_DEVICE_PATH) >> 8)
      }
    },
    { 0x93f50d0b, 0x37e5, 0x44ba, { 0xa8, 0x9d, 0x83, 0x65, 0x73, 0xb9, 0xe8, 0xd2 } }
  },
  {
    END_DEVICE_PATH_TYPE,
    END_ENTIRE_DEVICE_PATH_SUBTYPE,
    {
      (UINT8)sizeof (EFI_DEVICE_PATH_PROTOCOL),
      0
    }
  }
};

STATIC
BOOLEAN
ButtonQueueIsEmpty (
  VOID
  )
{
  return mButtonDevice.QueueHead == mButtonDevice.QueueTail;
}

STATIC
VOID
ButtonQueueAdd (
  IN UINT16  ScanCode,
  IN CHAR16  UnicodeChar
  )
{
  UINTN  NextTail;

  NextTail = (mButtonDevice.QueueTail + 1) % BUTTON_QUEUE_SIZE;
  if (NextTail == mButtonDevice.QueueHead) {
    DEBUG ((DEBUG_WARN, "ButtonDxe: input queue full\n"));
    return;
  }

  mButtonDevice.Queue[mButtonDevice.QueueTail].ScanCode    = ScanCode;
  mButtonDevice.Queue[mButtonDevice.QueueTail].UnicodeChar = UnicodeChar;
  mButtonDevice.QueueTail                                  = NextTail;

  DEBUG ((
    DEBUG_INFO,
    "ButtonDxe: queued ScanCode=0x%x UnicodeChar=0x%x\n",
    ScanCode,
    UnicodeChar
    ));
}

STATIC
VOID
ButtonPoll (
  VOID
  )
{
  BOOLEAN        PowerPressed;
  RETURN_STATUS  ReturnStatus;
  SARADC_KEY     SaradcKey;
  UINT32         AdcData;
  UINT32         AdcDelta;

  ReturnStatus = RK806ReadPowerKeyEvent (&PowerPressed);
  if (RETURN_ERROR (ReturnStatus)) {
    DEBUG ((DEBUG_ERROR, "ButtonDxe: RK806 power key read failed: %r\n", ReturnStatus));
  } else if (PowerPressed) {
    DEBUG ((DEBUG_INFO, "ButtonDxe: power key pressed\n"));
    ButtonQueueAdd (SCAN_NULL, CHAR_CARRIAGE_RETURN);
  }

  ReturnStatus = SaradcReadKey (&SaradcKey, &AdcData);
  if (RETURN_ERROR (ReturnStatus)) {
    DEBUG ((DEBUG_ERROR, "ButtonDxe: SARADC read failed: %r\n", ReturnStatus));
    return;
  }

  AdcDelta = (AdcData > mButtonDevice.LastAdcData) ?
             (AdcData - mButtonDevice.LastAdcData) :
             (mButtonDevice.LastAdcData - AdcData);
  if (AdcDelta >= BUTTON_ADC_LOG_DELTA) {
    DEBUG ((
      DEBUG_INFO,
      "ButtonDxe: SARADC1 raw=%u key=%u\n",
      AdcData,
      SaradcKey
      ));
    mButtonDevice.LastAdcData = AdcData;
  }

  if (SaradcKey != mButtonDevice.LastSaradcKey) {
    DEBUG ((
      DEBUG_INFO,
      "ButtonDxe: SARADC key changed %u -> %u, raw=%u\n",
      mButtonDevice.LastSaradcKey,
      SaradcKey,
      AdcData
      ));

    if (SaradcKey == SaradcKeyVolumeUp) {
      ButtonQueueAdd (SCAN_DOWN, CHAR_NULL);
    } else if (SaradcKey == SaradcKeyVolumeDown) {
      ButtonQueueAdd (SCAN_UP, CHAR_NULL);
    }

    mButtonDevice.LastSaradcKey = SaradcKey;
  }
}

STATIC
VOID
EFIAPI
ButtonPollHandler (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  )
{
  ButtonPoll ();
}

STATIC
VOID
EFIAPI
ButtonWaitForKey (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  )
{
  ButtonPoll ();
  if (!ButtonQueueIsEmpty ()) {
    gBS->SignalEvent (Event);
  }
}

STATIC
EFI_STATUS
EFIAPI
ButtonReset (
  IN EFI_SIMPLE_TEXT_INPUT_PROTOCOL  *This,
  IN BOOLEAN                         ExtendedVerification
  )
{
  EFI_TPL  OldTpl;

  OldTpl = gBS->RaiseTPL (TPL_NOTIFY);
  mButtonDevice.QueueHead      = 0;
  mButtonDevice.QueueTail      = 0;
  mButtonDevice.LastSaradcKey  = SaradcKeyNone;
  mButtonDevice.LastAdcData    = 0;
  gBS->RestoreTPL (OldTpl);

  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
ButtonReadKeyStroke (
  IN EFI_SIMPLE_TEXT_INPUT_PROTOCOL  *This,
  OUT EFI_INPUT_KEY                  *Key
  )
{
  EFI_TPL  OldTpl;

  if (Key == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  OldTpl = gBS->RaiseTPL (TPL_NOTIFY);
  if (ButtonQueueIsEmpty ()) {
    gBS->RestoreTPL (OldTpl);
    return EFI_NOT_READY;
  }

  CopyMem (Key, &mButtonDevice.Queue[mButtonDevice.QueueHead], sizeof (*Key));
  mButtonDevice.QueueHead = (mButtonDevice.QueueHead + 1) % BUTTON_QUEUE_SIZE;
  gBS->RestoreTPL (OldTpl);

  return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
ButtonDxeEntryPoint (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS     Status;
  RETURN_STATUS  ReturnStatus;

  ReturnStatus = RK806Init ();
  if (RETURN_ERROR (ReturnStatus)) {
    DEBUG ((DEBUG_ERROR, "ButtonDxe: RK806 init failed: %r\n", ReturnStatus));
    return EFI_DEVICE_ERROR;
  }

  ReturnStatus = RK806InitPowerKey ();
  if (RETURN_ERROR (ReturnStatus)) {
    DEBUG ((DEBUG_ERROR, "ButtonDxe: RK806 power key init failed: %r\n", ReturnStatus));
    return EFI_DEVICE_ERROR;
  }

  DEBUG ((DEBUG_INFO, "ButtonDxe: initialized\n"));

  mButtonDevice.SimpleTextIn.Reset         = ButtonReset;
  mButtonDevice.SimpleTextIn.ReadKeyStroke = ButtonReadKeyStroke;

  Status = gBS->CreateEvent (
                  EVT_NOTIFY_WAIT,
                  TPL_NOTIFY,
                  ButtonWaitForKey,
                  NULL,
                  &mButtonDevice.SimpleTextIn.WaitForKey
                  );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = gBS->CreateEvent (
                  EVT_TIMER | EVT_NOTIFY_SIGNAL,
                  TPL_NOTIFY,
                  ButtonPollHandler,
                  NULL,
                  &mButtonDevice.PollEvent
                  );
  if (EFI_ERROR (Status)) {
    goto Error;
  }

  Status = gBS->SetTimer (
                  mButtonDevice.PollEvent,
                  TimerPeriodic,
                  BUTTON_POLL_INTERVAL
                  );
  if (EFI_ERROR (Status)) {
    goto Error;
  }

  Status = gBS->InstallMultipleProtocolInterfaces (
                  &mButtonDevice.Handle,
                  &gEfiDevicePathProtocolGuid,
                  &mButtonDevicePath,
                  &gEfiSimpleTextInProtocolGuid,
                  &mButtonDevice.SimpleTextIn,
                  &gEfiConsoleInDeviceGuid,
                  NULL,
                  NULL
                  );
  if (!EFI_ERROR (Status)) {
    DEBUG ((DEBUG_INFO, "ButtonDxe: console input protocols installed\n"));
    return EFI_SUCCESS;
  }

Error:
  DEBUG ((DEBUG_ERROR, "ButtonDxe: initialization failed: %r\n", Status));
  if (mButtonDevice.PollEvent != NULL) {
    gBS->CloseEvent (mButtonDevice.PollEvent);
  }

  gBS->CloseEvent (mButtonDevice.SimpleTextIn.WaitForKey);
  return Status;
}
