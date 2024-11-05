/** @file
  This file implements the Graphics Output protocol for Arm platforms

  Copyright (c) 2011 - 2020, Arm Limited. All rights reserved.<BR>
  Copyright (c) 2022 Rockchip Electronics Co. Ltd.
  Copyright (c) 2023, Mario Bălănică <mariobalanica02@gmail.com>

  SPDX-License-Identifier: BSD-2-Clause-Patent

**/

#include <PiDxe.h>
#include <Protocol/RockchipDsiPanel.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DevicePathLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/DrmModes.h>
#include <Library/MediaBusFormat.h>

#include <Guid/GlobalVariable.h>

#include "LcdGraphicsOutputDxe.h"


//
// Global variables
//
STATIC LIST_ENTRY mDisplayStateList;

//STATIC DISPLAY_PROTOCOL mDisplayProtocol;

STATIC EFI_CPU_ARCH_PROTOCOL *mCpu;

typedef enum {
  ROCKCHIP_VOP2_CLUSTER0 = 0,
  ROCKCHIP_VOP2_CLUSTER1,
  ROCKCHIP_VOP2_ESMART0,
  ROCKCHIP_VOP2_ESMART1,
  ROCKCHIP_VOP2_SMART0,
  ROCKCHIP_VOP2_SMART1,
  ROCKCHIP_VOP2_CLUSTER2,
  ROCKCHIP_VOP2_CLUSTER3,
  ROCKCHIP_VOP2_ESMART2,
  ROCKCHIP_VOP2_ESMART3,
  ROCKCHIP_VOP2_LAYER_MAX,
} VOP2_LAYER_PHY_ID;

STATIC OVER_SCAN mDefaultOverScanParas = {
  .LeftMargin = 100,
  .RightMargin = 100,
  .TopMargin = 100,
  .BottomMargin = 100
};

STATIC DISPLAY_MODE mDisplayModes[] = {
  {
    2,
    148500000,
    {1920, 32, 200, 48},
    {1080, 5, 37, 3},
    0,
    0,
    0,
    0,
    1
  },
};

STATIC CONST UINT32 mMaxMode = ARRAY_SIZE (mDisplayModes);

LCD_INSTANCE mLcdTemplate = {
  LCD_INSTANCE_SIGNATURE,
  NULL, // Handle
  { // ModeInfo
    0, // Version
    0, // HorizontalResolution
    0, // VerticalResolution
    PixelBltOnly, // PixelFormat
    { 0 }, // PixelInformation
    0, // PixelsPerScanLine
  },
  {
    0, // MaxMode;
    0, // Mode;
    NULL, // Info;
    0, // SizeOfInfo;
    0, // FrameBufferBase;
    0 // FrameBufferSize;
  },
  { // Gop
    LcdGraphicsQueryMode,  // QueryMode
    LcdGraphicsSetMode,    // SetMode
    LcdGraphicsBlt,        // Blt
    NULL                     // *Mode
  },
  { // DevicePath
    {
      {
        HARDWARE_DEVICE_PATH, HW_VENDOR_DP,
        {
          (UINT8)(sizeof (VENDOR_DEVICE_PATH)),
          (UINT8)((sizeof (VENDOR_DEVICE_PATH)) >> 8)
        },
      },
      // Hardware Device Path for Lcd
      EFI_CALLER_ID_GUID // Use the driver's GUID
    },

    {
      END_DEVICE_PATH_TYPE,
      END_ENTIRE_DEVICE_PATH_SUBTYPE,
      {
        sizeof (EFI_DEVICE_PATH_PROTOCOL),
        0
      }
    }
  },
  (EFI_EVENT)NULL, // ExitBootServicesEvent
};

EFI_STATUS
LcdInstanceContructor (
  OUT LCD_INSTANCE** NewInstance
  )
{
  LCD_INSTANCE* Instance;

  Instance = AllocateCopyPool (sizeof (LCD_INSTANCE), &mLcdTemplate);
  if (Instance == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  Instance->Gop.Mode          = &Instance->Mode;
  Instance->Gop.Mode->MaxMode = mMaxMode;
  Instance->Mode.Info         = &Instance->ModeInfo;

  *NewInstance = Instance;
  return EFI_SUCCESS;
}

//
// Function Definitions
//

EFIAPI
EFI_STATUS
DisplayGetTiming (
  OUT DISPLAY_STATE                        *DisplayState
  )
{
  DRM_DISPLAY_MODE *Mode = &DisplayState->ConnectorState.DisplayMode;
  UINT32 ModeNumber = DisplayState->ModeNumber;
  UINT32 HActive, VActive, PixelClock;
  UINT32 HFrontPorch, HBackPorch, HSyncLen;
  UINT32 VFrontPorch, VBackPorch, VSyncLen;
  UINT32 Flags = 0;
  UINT32 Val = 0;

  HActive = mDisplayModes[ModeNumber].Horizontal.Resolution;
  VActive = mDisplayModes[ModeNumber].Vertical.Resolution;
  PixelClock = mDisplayModes[ModeNumber].OscFreq;
  HSyncLen = mDisplayModes[ModeNumber].Horizontal.Sync;
  HFrontPorch = mDisplayModes[ModeNumber].Horizontal.FrontPorch;
  HBackPorch = mDisplayModes[ModeNumber].Horizontal.BackPorch;
  VSyncLen = mDisplayModes[ModeNumber].Vertical.Sync;
  VFrontPorch = mDisplayModes[ModeNumber].Vertical.FrontPorch;
  VBackPorch = mDisplayModes[ModeNumber].Vertical.BackPorch;

  Val = mDisplayModes[ModeNumber].HsyncActive ? DRM_MODE_FLAG_PHSYNC : DRM_MODE_FLAG_NHSYNC;
  Flags |= Val;
  Val = mDisplayModes[ModeNumber].VsyncActive ? DRM_MODE_FLAG_PVSYNC : DRM_MODE_FLAG_NVSYNC;
  Flags |= Val;
  Val = mDisplayModes[ModeNumber].ClkActive ? DRM_MODE_FLAG_PPIXDATA : 0;
  Flags |= Val;

  Mode->HDisplay = HActive;
  Mode->HSyncStart = Mode->HDisplay + HFrontPorch;
  Mode->HSyncEnd = Mode->HSyncStart + HSyncLen;
  Mode->HTotal = Mode->HSyncEnd + HBackPorch;

  Mode->VDisplay = VActive;
  Mode->VSyncStart = Mode->VDisplay + VFrontPorch;
  Mode->VSyncEnd = Mode->VSyncStart + VSyncLen;
  Mode->VTotal = Mode->VSyncEnd + VBackPorch;

  Mode->Clock = PixelClock / 1000;
  Mode->Flags = Flags;

  /* not to set fix --- todo */
  Mode->VScan = 0;

  return EFI_SUCCESS;
}

STATIC
UINT32
DisplayBppConvert (
  IN LCD_BPP             LcdBpp
  )
{
  UINT32                 DisplayBpp;

  switch (LcdBpp) {
  case LcdBitsPerPixel_24:
    DisplayBpp = 32;
    break;
  default:
    DisplayBpp = 32;
    break;
  }

  return DisplayBpp;
}

STATIC
EFI_STATUS
DisplayPreInit (
  IN LCD_INSTANCE* Instance
  )
{
  EFI_STATUS                  Status;
  DISPLAY_STATE               *StateInterate;
  ROCKCHIP_CRTC_PROTOCOL      *Crtc;
  ROCKCHIP_CONNECTOR_PROTOCOL *Connector;

  LIST_FOR_EACH_ENTRY(StateInterate, &mDisplayStateList, ListHead) {
    if (StateInterate->IsEnable) {
      CRTC_STATE *CrtcState = &StateInterate->CrtcState;
      CONNECTOR_STATE *ConnectorState = &StateInterate->ConnectorState;
      Crtc = (ROCKCHIP_CRTC_PROTOCOL*)StateInterate->CrtcState.Crtc;
      Connector = (ROCKCHIP_CONNECTOR_PROTOCOL *)StateInterate->ConnectorState.Connector;

      if (Connector && Connector->Preinit)
        Status = Connector->Preinit(Connector, StateInterate);

      Crtc->Vps[CrtcState->CrtcID].OutputType = ConnectorState->Type;

      if (Crtc && Crtc->Preinit) {
        Status = Crtc->Preinit(Crtc, StateInterate);
        if (EFI_ERROR (Status)) {
          goto EXIT;
        }
      }
    }
  }

EXIT:
  return Status;
}

STATIC
EFI_STATUS
EFIAPI
LcdGraphicsOutputInit (
  VOID
  )
{
  EFI_STATUS                  Status;
  LCD_INSTANCE                *Instance;
  DISPLAY_STATE               *DisplayState;
  DISPLAY_MODE                *Mode;
  UINTN                       ModeIndex;
  ROCKCHIP_CRTC_PROTOCOL      *Crtc;
  UINTN                       ConnectorCount;
  EFI_HANDLE                  *ConnectorHandles;
  UINTN                       Index;
  UINT32                      HorizontalResolution;
  UINT32                      VerticalResolution;

  Status = LcdInstanceContructor (&Instance);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = gBS->LocateProtocol (&gEfiCpuArchProtocolGuid, NULL,
                                (VOID**)&mCpu);
  ASSERT_EFI_ERROR (Status);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = gBS->LocateProtocol (&gRockchipCrtcProtocolGuid, NULL,
                                (VOID **) &Crtc);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "%a: Can not locate the RockchipCrtcProtocol. Status=%r\n",
            __func__, Status));
    return Status;
  }

  Status = gBS->LocateHandleBuffer (ByProtocol,
                                    &gRockchipConnectorProtocolGuid,
                                    NULL,
                                    &ConnectorCount,
                                    &ConnectorHandles);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "%a: Can not locate gRockchipConnectorProtocolGuid. Status=%r\n",
            __func__, Status));
    return Status;
  }

  //
  // TO-DO: When EDID is implemented.
  // If native resolution is preferred (rather than custom, via PCDs),
  // we should pick the maximum mode supported by all connected displays.
  //
  // Why? Because we provide a single GOP and framebuffer shared between
  // all outputs.
  //
  // It is possible to install a GOP for each display, but then OSes would
  // only draw on the primary (usually first) one. This needs additional
  // logic to let users choose the primary display through a setup option.
  //

  //
  // Assume mode #0 is the highest supported mode for now.
  //
  ModeIndex = 0;
  Mode = &mDisplayModes[ModeIndex];

  InitializeListHead (&mDisplayStateList);

  for (Index = 0; Index < ConnectorCount; Index++) {
    DisplayState = AllocateZeroPool (sizeof(DISPLAY_STATE));
    InitializeListHead (&DisplayState->ListHead);

    Status = gBS->HandleProtocol (ConnectorHandles[Index],
                                  &gRockchipConnectorProtocolGuid,
                                  (VOID **) &DisplayState->ConnectorState.Connector);
    if (EFI_ERROR (Status)) {
      DEBUG ((DEBUG_ERROR, "%a: HandleProtocol gRockchipConnectorProtocolGuid [%d] failed. Status=%r\n",
              __func__, Index, Status));
      return Status;
    }

    //
    // DSI panel has a hardcoded mode. Overwrite the current one.
    // This is all pretty ugly and prevents using multiple displays,
    // but whatever...
    //
    ROCKCHIP_DSI_PANEL_PROTOCOL *DsiPanel;

    Status = gBS->HandleProtocol (ConnectorHandles[Index],
                                  &gRockchipDsiPanelProtocolGuid,
                                  (VOID **) &DsiPanel);
    if (!EFI_ERROR (Status)) {
      CopyMem (Mode, &DsiPanel->NativeMode, sizeof (*Mode));
    }

    /* adapt to UEFI architecture */
    DisplayState->ModeNumber = ModeIndex;
    DisplayState->VpsConfigModeID = Mode->VpsConfigModeID;

    DisplayState->CrtcState.Crtc = (VOID *) Crtc;
    DisplayState->CrtcState.CrtcID = Mode->CrtcId;

    DisplayState->ConnectorState.OverScan.LeftMargin = mDefaultOverScanParas.LeftMargin;
    DisplayState->ConnectorState.OverScan.RightMargin = mDefaultOverScanParas.RightMargin;
    DisplayState->ConnectorState.OverScan.TopMargin = mDefaultOverScanParas.TopMargin;
    DisplayState->ConnectorState.OverScan.BottomMargin = mDefaultOverScanParas.BottomMargin;

    /* set MEDIA_BUS_FMT_RBG888_1X24 by default when using panel */
    /* move to panel driver later -- todo*/
    DisplayState->ConnectorState.BusFormat = MEDIA_BUS_FMT_RBG888_1X24;

    /* add BCSH data if needed --- todo */
    DisplayState->ConnectorState.DispInfo = NULL;

    Crtc->Vps[Mode->CrtcId].Enable = TRUE;
    DisplayState->IsEnable = TRUE;

    InsertTailList (&mDisplayStateList, &DisplayState->ListHead);
  }

  Status = DisplayPreInit (Instance);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "%a: DisplayPreInit fail. Exit Status=%r\n",
            __func__, Status));
    return Status;
  }

  // Register for an ExitBootServicesEvent
  // When ExitBootServices starts, this function will make sure that the
  // graphics driver shuts down properly, i.e. it will free up all
  // allocated memory and perform any necessary hardware re-configuration.
  Status = gBS->CreateEvent (
                  EVT_SIGNAL_EXIT_BOOT_SERVICES,
                  TPL_NOTIFY,
                  LcdGraphicsExitBootServicesEvent,
                  NULL,
                  &Instance->ExitBootServicesEvent
                  );
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "%a: Can not install the ExitBootServicesEvent handler. Exit Status=%r\n",
            __func__, Status));
    return Status;
  }

  // Install the Graphics Output Protocol and the Device Path
  Status = gBS->InstallMultipleProtocolInterfaces (
                  &Instance->Handle,
                  &gEfiGraphicsOutputProtocolGuid,
                  &Instance->Gop,
                  &gEfiDevicePathProtocolGuid,
                  &Instance->DevicePath,
                  NULL
                  );
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "%a: Can not install the protocol. Status=%r\n",
            __func__, Status));
    return Status;
  }

  return EFI_SUCCESS;
}

VOID
EFIAPI
LcdGraphicsOutputEndOfDxeEventHandler (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  )
{
  gBS->CloseEvent (Event);

  LcdGraphicsOutputInit ();
}

EFI_STATUS
EFIAPI
LcdGraphicsOutputDxeInitialize (
  IN EFI_HANDLE         ImageHandle,
  IN EFI_SYSTEM_TABLE   *SystemTable
  )
{
  EFI_STATUS               Status;
  EFI_EVENT                EndOfDxeEvent;

  Status = gBS->CreateEventEx (
                  EVT_NOTIFY_SIGNAL,
                  TPL_CALLBACK,
                  LcdGraphicsOutputEndOfDxeEventHandler,
                  NULL,
                  &gEfiEndOfDxeEventGroupGuid,
                  &EndOfDxeEvent
                  );
  return Status;
}

/** This function should be called
  on Event: ExitBootServices
  to free up memory, stop the driver
  and uninstall the protocols
**/
VOID
LcdGraphicsExitBootServicesEvent (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  )
{
  // By default, this PCD is FALSE. But if a platform starts a predefined OS
  // that does not use a framebuffer then we might want to disable the display
  // controller to avoid to display corrupted information on the screen.
  if (FeaturePcdGet (PcdGopDisableOnExitBootServices)) {
    // Turn-off the Display controller
  }
}

/** GraphicsOutput Protocol function, mapping to
  EFI_GRAPHICS_OUTPUT_PROTOCOL.QueryMode
**/
EFI_STATUS
EFIAPI
LcdGraphicsQueryMode (
  IN EFI_GRAPHICS_OUTPUT_PROTOCOL            *This,
  IN UINT32                                  ModeNumber,
  OUT UINTN                                  *SizeOfInfo,
  OUT EFI_GRAPHICS_OUTPUT_MODE_INFORMATION   **Info
  )
{
  EFI_STATUS Status;
  LCD_INSTANCE *Instance;

  Instance = LCD_INSTANCE_FROM_GOP_THIS (This);

  // Error checking
  if ((This == NULL) ||
      (Info == NULL) ||
      (SizeOfInfo == NULL) ||
      (ModeNumber >= This->Mode->MaxMode)) {
    DEBUG ((DEBUG_ERROR, "LcdGraphicsQueryMode: ERROR - For mode number %d : Invalid Parameter.\n", ModeNumber));
    Status = EFI_INVALID_PARAMETER;
    goto EXIT;
  }

  *Info = AllocatePool (sizeof (EFI_GRAPHICS_OUTPUT_MODE_INFORMATION));
  if (*Info == NULL) {
    Status = EFI_OUT_OF_RESOURCES;
    goto EXIT;
  }

  *SizeOfInfo = sizeof (EFI_GRAPHICS_OUTPUT_MODE_INFORMATION);

  (*Info)->Version = 0;
  (*Info)->HorizontalResolution = mDisplayModes[ModeNumber].Horizontal.Resolution;
  (*Info)->VerticalResolution = mDisplayModes[ModeNumber].Vertical.Resolution;
  (*Info)->PixelsPerScanLine = mDisplayModes[ModeNumber].Horizontal.Resolution;
  (*Info)->PixelFormat = FixedPcdGet32 (PcdLcdPixelFormat);

  return EFI_SUCCESS;

EXIT:
  return Status;
}

/** GraphicsOutput Protocol function, mapping to
  EFI_GRAPHICS_OUTPUT_PROTOCOL.SetMode
**/
EFI_STATUS
EFIAPI
LcdGraphicsSetMode (
  IN EFI_GRAPHICS_OUTPUT_PROTOCOL   *This,
  IN UINT32                         ModeNumber
  )
{
  EFI_STATUS                      Status;
  EFI_GRAPHICS_OUTPUT_BLT_PIXEL   FillColour;
  LCD_INSTANCE*                   Instance;
  LCD_BPP                         Bpp;
  EFI_PHYSICAL_ADDRESS            VramBaseAddress;
  UINTN                           VramSize;
  UINTN                           NumVramPages;
  UINTN                           NumPreviousVramPages;
  DISPLAY_MODE                    *Mode;
  DRM_DISPLAY_MODE                *DrmMode;
  DISPLAY_STATE                   *StateInterate;
  ROCKCHIP_CRTC_PROTOCOL          *Crtc;
  CRTC_STATE                      *CrtcState;
  ROCKCHIP_CONNECTOR_PROTOCOL     *Connector;
  CONNECTOR_STATE                 *ConnectorState;

  Instance = LCD_INSTANCE_FROM_GOP_THIS (This);

  // Check if this mode is supported
  if (ModeNumber >= This->Mode->MaxMode) {
    DEBUG ((DEBUG_ERROR, "%a: ERROR - Unsupported mode number %d .\n",
            __func__, ModeNumber));
    Status = EFI_UNSUPPORTED;
    goto EXIT;
  }

  Mode = &mDisplayModes[ModeNumber];

  Status = LcdGraphicsGetBpp (ModeNumber, &Bpp);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "%a: ERROR - Couldn't get bytes per pixel, status: %r\n",
            __func__, Status));
    goto EXIT;
  }

  VramBaseAddress = This->Mode->FrameBufferBase;

  VramSize = Mode->Horizontal.Resolution
             * Mode->Vertical.Resolution
             * GetBytesPerPixel (Bpp);

  NumVramPages = EFI_SIZE_TO_PAGES (VramSize);
  NumPreviousVramPages = EFI_SIZE_TO_PAGES (This->Mode->FrameBufferSize);

  if (NumPreviousVramPages < NumVramPages) {
    if (NumPreviousVramPages != 0) {
      gBS->FreePages (VramBaseAddress, NumPreviousVramPages);
      This->Mode->FrameBufferSize = 0;
    }

    VramBaseAddress = SIZE_4GB - 1; // VOP2 only supports 32-bit addresses
    Status = gBS->AllocatePages (AllocateMaxAddress, EfiRuntimeServicesData,
                                NumVramPages, &VramBaseAddress);
    if (EFI_ERROR (Status)) {
      DEBUG ((DEBUG_ERROR, "%a: Could not allocate %u pages for mode %u: %r\n",
              __func__, NumVramPages, ModeNumber, Status));
      return Status;
    }

    Status = mCpu->SetMemoryAttributes (mCpu, VramBaseAddress,
                                        ALIGN_VALUE (VramSize, EFI_PAGE_SIZE),
                                        EFI_MEMORY_WC);
    if (EFI_ERROR (Status)) {
      DEBUG ((DEBUG_ERROR, "%a: Couldn't set framebuffer attributes: %r\n",
              __func__, Status));
      goto EXIT;
    }
  }

  // Update the UEFI mode information
  This->Mode->Mode = ModeNumber;

  Instance->ModeInfo.Version = 0;
  Instance->ModeInfo.HorizontalResolution = mDisplayModes[ModeNumber].Horizontal.Resolution;
  Instance->ModeInfo.VerticalResolution = mDisplayModes[ModeNumber].Vertical.Resolution;
  Instance->ModeInfo.PixelsPerScanLine = mDisplayModes[ModeNumber].Horizontal.Resolution;
  Instance->ModeInfo.PixelFormat = FixedPcdGet32 (PcdLcdPixelFormat);
  Instance->Mode.SizeOfInfo = sizeof (EFI_GRAPHICS_OUTPUT_MODE_INFORMATION);

  This->Mode->FrameBufferBase = VramBaseAddress;
  This->Mode->FrameBufferSize = VramSize;

  // The UEFI spec requires that we now clear the visible portions of the
  // output display to black.

  // Set the fill colour to black
  SetMem (&FillColour, sizeof (EFI_GRAPHICS_OUTPUT_BLT_PIXEL), 0x0);

  // Fill the entire visible area with the same colour.
  Status = This->Blt (
      This,
      &FillColour,
      EfiBltVideoFill,
      0,
      0,
      0,
      0,
      This->Mode->Info->HorizontalResolution,
      This->Mode->Info->VerticalResolution,
      0
      );

  LIST_FOR_EACH_ENTRY(StateInterate, &mDisplayStateList, ListHead) {
    if (StateInterate->ModeNumber == ModeNumber && StateInterate->IsEnable) {
      Crtc = (ROCKCHIP_CRTC_PROTOCOL*)StateInterate->CrtcState.Crtc;
      Connector = (ROCKCHIP_CONNECTOR_PROTOCOL *)StateInterate->ConnectorState.Connector;
      CrtcState = &StateInterate->CrtcState;
      ConnectorState = &StateInterate->ConnectorState;
      DrmMode = &StateInterate->ConnectorState.DisplayMode;

      StateInterate->ModeNumber = ModeNumber;

      if (Connector && Connector->Init)
        Status = Connector->Init(Connector, StateInterate);

      /* move to panel protocol --- todo */
      DisplayGetTiming (StateInterate);

      DEBUG ((DEBUG_INIT, "[INIT]detailed mode clock %u kHz, flags[%x]\n"
                          "          H: %04d %04d %04d %04d\n"
                          "          V: %04d %04d %04d %04d\n"
                          "      bus_format: %x\n",
                          DrmMode->Clock, DrmMode->Flags,
                          DrmMode->HDisplay, DrmMode->HSyncStart,
                          DrmMode->HSyncEnd, DrmMode->HTotal,
                          DrmMode->VDisplay, DrmMode->VSyncStart,
                          DrmMode->VSyncEnd, DrmMode->VTotal,
                          ConnectorState->BusFormat));

      Status = DisplaySetCrtcInfo(DrmMode, CRTC_INTERLACE_HALVE_V);
      if (EFI_ERROR (Status)) {
        goto EXIT;
      }

      if (Crtc && Crtc->Init) {
        Status = Crtc->Init (Crtc, StateInterate);
        if (EFI_ERROR (Status)) {
          goto EXIT;
        }
      }

      /* adapt to uefi display architecture */
      CrtcState->Format = ROCKCHIP_FMT_ARGB8888;
      CrtcState->SrcW = ConnectorState->DisplayMode.HDisplay;
      CrtcState->SrcH = ConnectorState->DisplayMode.VDisplay;
      CrtcState->SrcX = 0;
      CrtcState->SrcY = 0;
      CrtcState->CrtcW = ConnectorState->DisplayMode.HDisplay;
      CrtcState->CrtcH = ConnectorState->DisplayMode.VDisplay;
      CrtcState->CrtcX = 0;
      CrtcState->CrtcY = 0;
      CrtcState->YMirror = 0;
      CrtcState->RBSwap = 0;

      CrtcState->XVirtual = ALIGN(CrtcState->SrcW * DisplayBppConvert (Bpp), 32) >> 5;
      CrtcState->DMAAddress = (UINT32)VramBaseAddress;

      if (Crtc && Crtc->SetPlane)
        Crtc->SetPlane (Crtc, StateInterate);

      if (Crtc && Crtc->Enable)
        Crtc->Enable (Crtc, StateInterate);

      if (Connector && Connector->Enable)
        Connector->Enable (Connector, StateInterate);
    }
  }

EXIT:
  return Status;
}

EFI_STATUS
EFIAPI
LcdGraphicsGetBpp (
  IN  UINT32                                ModeNumber,
  OUT LCD_BPP*                              Bpp
  )
{
  if (ModeNumber >= mMaxMode) {
    return EFI_INVALID_PARAMETER;
  }

  *Bpp = LcdBitsPerPixel_24;

  return EFI_SUCCESS;
}

UINTN
GetBytesPerPixel (
  IN  LCD_BPP       Bpp
  )
{
  switch (Bpp) {
  case LcdBitsPerPixel_24:
    return 4;

  case LcdBitsPerPixel_16_565:
  case LcdBitsPerPixel_16_555:
  case LcdBitsPerPixel_12_444:
    return 2;

  case LcdBitsPerPixel_8:
  case LcdBitsPerPixel_4:
  case LcdBitsPerPixel_2:
  case LcdBitsPerPixel_1:
    return 1;

  default:
    return 0;
  }
}
