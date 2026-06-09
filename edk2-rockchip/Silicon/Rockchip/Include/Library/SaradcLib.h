/** @file
 *
 *  Copyright (c) 2024, Mario Bălănică <mariobalanica02@gmail.com>
 *
 *  SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 **/

#ifndef SARADC_LIB_H__
#define SARADC_LIB_H__

typedef enum {
  SaradcKeyNone,
  SaradcKeyVolumeUp,
  SaradcKeyVolumeDown
} SARADC_KEY;

RETURN_STATUS
SaradcReadChannel (
  IN  UINT32  Channel,
  OUT UINT32  *Data
  );

RETURN_STATUS
SaradcReadKey (
  OUT SARADC_KEY  *Key,
  OUT UINT32      *Data OPTIONAL
  );

#endif /* SARADC_LIB_H__ */
