/** @file
 *
 *  Type-C power policy driver.
 *
 *  SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 **/

#include <Uefi.h>

#include <Library/BaseLib.h>
#include <Library/DebugLib.h>
#include <Library/GpioLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/PcdLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Protocol/Bq25890Control.h>
#include <Protocol/Fusb302State.h>
#include <VarStoreData.h>

#include "../../../RK3588/Drivers/RK3588Dxe/RK3588DxeFormSetGuid.h"
#include "TypecPowerPolicyDxe.h"

EFI_GUID  gRK3588DxeFormSetGuid = { 0x10f41c33, 0xa468, 0x42cd, { 0x85, 0xee, 0x70, 0x43, 0x21, 0x3f, 0x73, 0xa3 } };

#define TYPEC_POLICY_CONTEXT_SIGNATURE  SIGNATURE_32 ('T', 'P', 'P', 'D')
#define TYPEC_POLICY_POLL_INTERVAL_MS   200
#define TYPEC_FUSB302_IRQ_GROUP         0
#define TYPEC_FUSB302_IRQ_PIN           GPIO_PIN_PD3
#define TYPEC_BQ25890_IRQ_GROUP         4
#define TYPEC_BQ25890_IRQ_PIN           GPIO_PIN_PB0
#define TYPEC_FUSB302_TOGSS_SRC1        1
#define TYPEC_FUSB302_TOGSS_SRC2        2
#define TYPEC_FUSB302_TOGSS_SNK1        5
#define TYPEC_FUSB302_TOGSS_SNK2        6

typedef enum {
  TYPEC_POLICY_ROLE_UNKNOWN = 0,
  TYPEC_POLICY_ROLE_SINK    = 1,
  TYPEC_POLICY_ROLE_SOURCE   = 2
} TYPEC_POLICY_ROLE;

typedef struct {
  UINT32                   Signature;
  EFI_EVENT                RefreshEvent;
  BQ25890_CONTROL_PROTOCOL  *Bq25890;
  FUSB302_STATE_PROTOCOL    *Fusb302;
  TYPEC_POLICY_ROLE         AppliedRole;
  TYPEC_POLICY_ROLE         LastDetectedRole;
  BOOLEAN                   LastFusbIrqLow;
  BOOLEAN                   LastBqIrqLow;
} TYPEC_POLICY_CONTEXT;

STATIC
EFI_STATUS
TypecPolicyApplyRole (
  IN TYPEC_POLICY_CONTEXT  *Context,
  IN TYPEC_POLICY_ROLE     DesiredRole
  )
{
  EFI_STATUS  Status;

  if (Context->AppliedRole == DesiredRole) {
    return EFI_SUCCESS;
  }

  switch (DesiredRole) {
    case TYPEC_POLICY_ROLE_SOURCE:
      Status = Context->Bq25890->SetPowerRole (Context->Bq25890, TRUE);
      break;
    case TYPEC_POLICY_ROLE_SINK:
    default:
      Status = Context->Bq25890->SetPowerRole (Context->Bq25890, FALSE);
      if (!EFI_ERROR (Status)) {
        Status = Context->Bq25890->SetInputCurrentLimit (Context->Bq25890, 2000);
      }
      break;
  }

  if (!EFI_ERROR (Status)) {
    Context->AppliedRole = DesiredRole;
  }

  DEBUG ((DEBUG_INFO, "TypeC policy: applied role %u status %r\n", DesiredRole, Status));
  return Status;
}

STATIC
EFI_STATUS
TypecPolicyGetPreferredRole (
  OUT UINT32  *PreferredRole
  )
{
  EFI_STATUS  Status;
  UINTN       Size;
  UINT32      Role;

  if (PreferredRole == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  Size   = sizeof (Role);
  Status = gRT->GetVariable (
                  L"TypecRole",
                  &gRK3588DxeFormSetGuid,
                  NULL,
                  &Size,
                  &Role
                  );
  if (EFI_ERROR (Status) || (Role > TYPEC_ROLE_SOURCE)) {
    Role = PcdGet32 (PcdTypecRole);
  }

  *PreferredRole = Role;
  return EFI_SUCCESS;
}

STATIC
TYPEC_POLICY_ROLE
TypecPolicyResolveRole (
  IN TYPEC_POLICY_CONTEXT  *Context,
  OUT UINT32               *DetectedRoleOut
  )
{
  UINT32   DetectedRole;
  BOOLEAN  VbusPresent;
  EFI_STATUS Status;
  UINT32   Preferred;

  Status = TypecPolicyGetPreferredRole (&Preferred);
  if (EFI_ERROR (Status)) {
    Preferred = TYPEC_ROLE_DUAL;
  }

  if (Preferred == TYPEC_ROLE_SOURCE) {
    if (DetectedRoleOut != NULL) {
      *DetectedRoleOut = TYPEC_FUSB302_TOGSS_SRC1;
    }
    return TYPEC_POLICY_ROLE_SOURCE;
  }

  if (Preferred == TYPEC_ROLE_SINK) {
    if (DetectedRoleOut != NULL) {
      *DetectedRoleOut = TYPEC_FUSB302_TOGSS_SNK1;
    }
    return TYPEC_POLICY_ROLE_SINK;
  }

  Status = Context->Fusb302->GetStatus (Context->Fusb302, &DetectedRole, &VbusPresent);
  if (EFI_ERROR (Status)) {
    if (DetectedRoleOut != NULL) {
      *DetectedRoleOut = 0;
    }
    return TYPEC_POLICY_ROLE_SINK;
  }
  (VOID)VbusPresent;

  if (DetectedRoleOut != NULL) {
    *DetectedRoleOut = DetectedRole;
  }

  switch (DetectedRole) {
    case TYPEC_FUSB302_TOGSS_SRC1:
    case TYPEC_FUSB302_TOGSS_SRC2:
      return TYPEC_POLICY_ROLE_SOURCE;
    case TYPEC_FUSB302_TOGSS_SNK1:
    case TYPEC_FUSB302_TOGSS_SNK2:
      return TYPEC_POLICY_ROLE_SINK;
    default:
      return TYPEC_POLICY_ROLE_SINK;
  }
}

STATIC
VOID
EFIAPI
TypecPolicyRefresh (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  )
{
  (VOID)Event;

  TYPEC_POLICY_CONTEXT  *Policy;
  EFI_STATUS            Status;
  TYPEC_POLICY_ROLE     DesiredRole;
  UINT32                DetectedRole;
  BOOLEAN               FusbIrqLow;
  BOOLEAN               BqIrqLow;

  Policy = Context;
  if ((Policy->Bq25890 == NULL) || (Policy->Fusb302 == NULL)) {
    Status = gBS->LocateProtocol (&gBq25890ControlProtocolGuid, NULL, (VOID **)&Policy->Bq25890);
    if (EFI_ERROR (Status)) {
      return;
    }

    Status = gBS->LocateProtocol (&gFusb302StateProtocolGuid, NULL, (VOID **)&Policy->Fusb302);
    if (EFI_ERROR (Status)) {
      return;
    }
  }

  FusbIrqLow = !GpioPinReadActual (TYPEC_FUSB302_IRQ_GROUP, TYPEC_FUSB302_IRQ_PIN);
  BqIrqLow   = !GpioPinReadActual (TYPEC_BQ25890_IRQ_GROUP, TYPEC_BQ25890_IRQ_PIN);

  if (FusbIrqLow != Policy->LastFusbIrqLow) {
    DEBUG ((DEBUG_INFO, "TypeC policy: FUSB302 INT_N %a\n", FusbIrqLow ? "asserted" : "released"));
    Policy->LastFusbIrqLow = FusbIrqLow;
  }
  if (BqIrqLow != Policy->LastBqIrqLow) {
    DEBUG ((DEBUG_INFO, "TypeC policy: BQ25890 INT %a\n", BqIrqLow ? "asserted" : "released"));
    Policy->LastBqIrqLow = BqIrqLow;
  }

  if (FusbIrqLow) {
    Policy->Fusb302->RefreshStatus (Policy->Fusb302);
  }
  if (BqIrqLow) {
    Policy->Bq25890->RefreshStatus (Policy->Bq25890);
  }

  DesiredRole = TypecPolicyResolveRole (Policy, &DetectedRole);
  if (DetectedRole != Policy->LastDetectedRole) {
    DEBUG ((DEBUG_INFO, "TypeC policy: detected role raw=%u\n", DetectedRole));
    Policy->LastDetectedRole = DetectedRole;
  }
  if (DesiredRole != Policy->AppliedRole) {
    TypecPolicyApplyRole (Policy, DesiredRole);
  }
}

EFI_STATUS
EFIAPI
TypecPowerPolicyDxeInitialize (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  (VOID)ImageHandle;
  (VOID)SystemTable;

  TYPEC_POLICY_CONTEXT  *Policy;
  EFI_STATUS            Status;

  Policy = AllocateZeroPool (sizeof (*Policy));
  if (Policy == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  Policy->Signature = TYPEC_POLICY_CONTEXT_SIGNATURE;
  Policy->AppliedRole = TYPEC_POLICY_ROLE_UNKNOWN;
  Policy->LastDetectedRole = 0xFFFFFFFFU;
  Policy->LastFusbIrqLow = FALSE;
  Policy->LastBqIrqLow = FALSE;

  Status = gBS->CreateEvent (EVT_TIMER | EVT_NOTIFY_SIGNAL, TPL_CALLBACK, TypecPolicyRefresh, Policy, &Policy->RefreshEvent);
  if (EFI_ERROR (Status)) {
    FreePool (Policy);
    return Status;
  }

  TypecPolicyRefresh (Policy->RefreshEvent, Policy);
  Status = gBS->SetTimer (Policy->RefreshEvent, TimerPeriodic, TYPEC_POLICY_POLL_INTERVAL_MS * 10 * 1000);
  if (EFI_ERROR (Status)) {
    gBS->CloseEvent (Policy->RefreshEvent);
    FreePool (Policy);
    return Status;
  }

  return EFI_SUCCESS;
}
