/** @file
 *
 *  SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 **/

#ifndef __DEVICE_CONFIG_H__
#define __DEVICE_CONFIG_H__

#ifndef VFR_FILE_INCLUDE
VOID
EFIAPI
ApplyDeviceConfigVariables (
  VOID
  );

VOID
EFIAPI
SetupDeviceConfigVariables (
  VOID
  );
#endif

#endif
