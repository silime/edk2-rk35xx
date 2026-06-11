/** @file
 *
 *  BQ25890 control protocol.
 *
 *  SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 **/

#ifndef __BQ25890_CONTROL_H__
#define __BQ25890_CONTROL_H__

#include <Uefi.h>

typedef struct _BQ25890_CONTROL_PROTOCOL BQ25890_CONTROL_PROTOCOL;

typedef
EFI_STATUS
(EFIAPI *BQ25890_SET_POWER_ROLE)(
  IN BQ25890_CONTROL_PROTOCOL  *This,
  IN BOOLEAN                   Source
  );

typedef
EFI_STATUS
(EFIAPI *BQ25890_SET_INPUT_CURRENT_LIMIT)(
  IN BQ25890_CONTROL_PROTOCOL  *This,
  IN UINT32                    MilliAmps
  );

typedef
EFI_STATUS
(EFIAPI *BQ25890_REFRESH_STATUS)(
  IN BQ25890_CONTROL_PROTOCOL  *This
  );

struct _BQ25890_CONTROL_PROTOCOL {
  BQ25890_SET_POWER_ROLE             SetPowerRole;
  BQ25890_SET_INPUT_CURRENT_LIMIT    SetInputCurrentLimit;
  BQ25890_REFRESH_STATUS             RefreshStatus;
};

extern EFI_GUID  gBq25890ControlProtocolGuid;

#endif
