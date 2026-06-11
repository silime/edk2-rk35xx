/** @file
 *
 *  FUSB302 state protocol.
 *
 *  SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 **/

#ifndef __FUSB302_STATE_H__
#define __FUSB302_STATE_H__

#include <Uefi.h>

typedef struct _FUSB302_STATE_PROTOCOL FUSB302_STATE_PROTOCOL;

typedef
EFI_STATUS
(EFIAPI *FUSB302_GET_STATUS)(
  IN  FUSB302_STATE_PROTOCOL  *This,
  OUT UINT32                  *DetectedRole,
  OUT BOOLEAN                 *VbusPresent
  );

typedef
EFI_STATUS
(EFIAPI *FUSB302_REFRESH_STATUS)(
  IN FUSB302_STATE_PROTOCOL  *This
  );

struct _FUSB302_STATE_PROTOCOL {
  FUSB302_GET_STATUS  GetStatus;
  FUSB302_REFRESH_STATUS  RefreshStatus;
};

extern EFI_GUID  gFusb302StateProtocolGuid;

#endif
