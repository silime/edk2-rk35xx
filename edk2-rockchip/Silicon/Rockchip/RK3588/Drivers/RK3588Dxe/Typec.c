/** @file
 *
 *  SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 **/

#include <Library/DebugLib.h>
#include <Library/PcdLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>
#include <VarStoreData.h>

#include "RK3588DxeFormSetGuid.h"
#include "Typec.h"

VOID
EFIAPI
SetupTypecVariables (
  VOID
  )
{
  EFI_STATUS  Status;
  UINT32      Role;
  UINTN       Size;

  Size = sizeof (Role);
  Status = gRT->GetVariable (
                  L"TypecRole",
                  &gRK3588DxeFormSetGuid,
                  NULL,
                  &Size,
                  &Role
                  );
  if (EFI_ERROR (Status) || (Role > TYPEC_ROLE_SOURCE)) {
    Status = PcdSet32S (PcdTypecRole, FixedPcdGet32 (PcdTypecRoleDefault));
    ASSERT_EFI_ERROR (Status);
  }
}
