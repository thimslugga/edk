/*++

Copyright (c) 2004 - 2007, Intel Corporation                                                         
All rights reserved. This program and the accompanying materials                          
are licensed and made available under the terms and conditions of the BSD License         
which accompanies this distribution.  The full text of the license may be found at        
http://opensource.org/licenses/bsd-license.php                                            
                                                                                          
THE PROGRAM IS DISTRIBUTED UNDER THE BSD LICENSE ON AN "AS IS" BASIS,                     
WITHOUT WARRANTIES OR REPRESENTATIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED.             

Module Name:

  DxeLoad.c

Abstract:

  Last PEIM.
  Responsibility of this module is to load the DXE Core from a Firmware Volume.

--*/

#include "Tiano.h"
#include "Pei.h"
#include "DxeIpl.h"
#include EFI_GUID_DEFINITION(MemoryAllocationHob)

EFI_PEI_SERVICES                  **gPeiServices;

//
// Interface and GUID for the Decompression APIs shared between PEI and DXE
//
EFI_GUID                          mPeiEfiDecompressProtocolGuid = EFI_DECOMPRESS_PROTOCOL_GUID;

//
// Interface and GUID for the Tiano Decompression APIs shared between PEI and DXE
//
EFI_GUID                          mPeiEfiTianoDecompressProtocolGuid = EFI_TIANO_DECOMPRESS_PROTOCOL_GUID;

//
// Interface and GUID for the user customized Decompression APIs shared between PEI and DXE
//
EFI_GUID                          mPeiEfiCustomizedDecompressProtocolGuid = EFI_CUSTOMIZED_DECOMPRESS_PROTOCOL_GUID;

//
// Interface and GUID for the Instruction Cache Flushing APIs shared between PEI and DXE
//
EFI_GUID                          mPeiEfiPeiFlushInstructionCacheGuid = EFI_PEI_FLUSH_INSTRUCTION_CACHE_GUID;

//
// Interface and GUID for the PE/COFF Loader APIs shared between PEI and DXE
//
EFI_GUID                          mPeiEfiPeiPeCoffLoaderGuid = EFI_PEI_PE_COFF_LOADER_GUID;

//
// Interface and GUID for the setjump()/longjump() APIs shared between PEI and DXE
//
EFI_GUID                          mPeiEfiPeiTransferControlGuid = EFI_PEI_TRANSFER_CONTROL_GUID;

//
// GUID for the Firmware Volume type that PEI supports
//
// EFI_GUID  mPeiFwFileSysTypeGuid = EFI_FIRMWARE_FILE_SYSTEM_GUID;
//
// Module Globals used in the DXE to PEI handoff
// These must be module globals, so the stack can be switched
//
EFI_STATUS
EFIAPI
DxeIplLoadFile (
  IN EFI_PEI_FV_FILE_LOADER_PPI                 *This,
  IN  EFI_FFS_FILE_HEADER                       *FfsHeader,
  OUT EFI_PHYSICAL_ADDRESS                      *ImageAddress,
  OUT UINT64                                    *ImageSize,
  OUT EFI_PHYSICAL_ADDRESS                      *EntryPoint
  );

EFI_STATUS
ShadowDxeIpl (
  IN EFI_PEI_SERVICES                          **PeiServices,
  IN EFI_FFS_FILE_HEADER                       *DxeIpl,
  IN EFI_PEI_PE_COFF_LOADER_PROTOCOL           *PeiEfiPeiPeCoffLoader,
  IN EFI_PEI_FLUSH_INSTRUCTION_CACHE_PROTOCOL  *PeiEfiPeiFlushInstructionCache
  );

VOID
EfiCommonLibCopyMem (
  IN VOID   *Destination,
  IN VOID   *Source,
  IN UINTN  Count
  );

EFI_STATUS
EFIAPI
DxeLoadCore (
  IN EFI_DXE_IPL_PPI       *This,
  IN EFI_PEI_SERVICES      **PeiServices,
  IN EFI_PEI_HOB_POINTERS  HobList
  );

EFI_STATUS
PeiProcessFile (
  IN     EFI_PEI_SERVICES       **PeiServices,
  IN     UINT16                 SectionType,
  IN     EFI_FFS_FILE_HEADER    *FfsFileHeader,
  OUT    VOID                   **Pe32Data,
  IN     EFI_PEI_HOB_POINTERS   *OrigHob
  );


STATIC
EFI_STATUS
GetFvAlignment (
  IN    EFI_FIRMWARE_VOLUME_HEADER   *FvHeader,
  OUT   UINT32                      *FvAlignment
  );

STATIC
VOID *
EFIAPI
AllocateAlignedPages (
  IN EFI_PEI_SERVICES       **PeiServices,
  IN UINTN                  Pages,
  IN UINTN                  Alignment
  );

STATIC
VOID
UpdateStackHob (
  IN EFI_PEI_SERVICES            **PeiServices,
  IN EFI_PHYSICAL_ADDRESS        BaseAddress,
  IN UINT64                      Length
  );

//
// Module Globals used in the DXE to PEI handoff
// These must be module globals, so the stack can be switched
//
static EFI_DXE_IPL_PPI mDxeIplPpi = {
  DxeLoadCore
};

static EFI_PEI_FV_FILE_LOADER_PPI mLoadFilePpi = {
  DxeIplLoadFile
};

static EFI_PEI_PPI_DESCRIPTOR     mPpiLoadFile = {
  (EFI_PEI_PPI_DESCRIPTOR_PPI | EFI_PEI_PPI_DESCRIPTOR_TERMINATE_LIST),
  &gPeiFvFileLoaderPpiGuid,
  &mLoadFilePpi
};

static EFI_PEI_PPI_DESCRIPTOR     mPpiList = {
  (EFI_PEI_PPI_DESCRIPTOR_PPI | EFI_PEI_PPI_DESCRIPTOR_TERMINATE_LIST),
  &gEfiDxeIplPpiGuid,
  &mDxeIplPpi
};

static EFI_PEI_PPI_DESCRIPTOR     mPpiPeiInMemory = {
  (EFI_PEI_PPI_DESCRIPTOR_PPI | EFI_PEI_PPI_DESCRIPTOR_TERMINATE_LIST),
  &gPeiInMemoryGuid,
  NULL
};

static EFI_PEI_PPI_DESCRIPTOR     mPpiSignal = {
  (EFI_PEI_PPI_DESCRIPTOR_PPI | EFI_PEI_PPI_DESCRIPTOR_TERMINATE_LIST),
  &gEndOfPeiSignalPpiGuid,
  NULL
};

STATIC
UINTN
GetOccupiedSize (
  IN UINTN   ActualSize,
  IN UINTN   Alignment
  )
{
  UINTN OccupiedSize;

  OccupiedSize = ActualSize;
  while ((OccupiedSize & (Alignment - 1)) != 0) {
    OccupiedSize++;
  }

  return OccupiedSize;
}

EFI_STATUS
EFIAPI
PeimInitializeDxeIpl (
  IN EFI_FFS_FILE_HEADER       *FfsHeader,
  IN EFI_PEI_SERVICES          **PeiServices
  );

EFI_PEIM_ENTRY_POINT (PeimInitializeDxeIpl)

typedef
EFI_STATUS
(EFIAPI *DXE_IPL_ENTRYPOINT) (
  IN EFI_FFS_FILE_HEADER       * FfsHeader,
  IN EFI_PEI_SERVICES          **PeiServices
  );

EFI_STATUS
EFIAPI
PeimInitializeDxeIpl (
  IN EFI_FFS_FILE_HEADER       *FfsHeader,
  IN EFI_PEI_SERVICES          **PeiServices
  )
/*++

Routine Description:

  Initializes the Dxe Ipl PPI

Arguments:

  FfsHeader   - Pointer to FFS file header
  PeiServices - General purpose services available to every PEIM.
    
Returns:

  EFI_SUCCESS

--*/
{
  EFI_STATUS                                Status;
  EFI_PEI_PE_COFF_LOADER_PROTOCOL           *PeiEfiPeiPeCoffLoader;
  EFI_PEI_FLUSH_INSTRUCTION_CACHE_PROTOCOL  *PeiEfiPeiFlushInstructionCache;
  EFI_BOOT_MODE                             BootMode;
 
  Status = (*PeiServices)->GetBootMode (PeiServices, &BootMode);
  ASSERT_PEI_ERROR (PeiServices, Status);

  Status = (*PeiServices)->LocatePpi (
                            PeiServices,
                            &gPeiInMemoryGuid,
                            0,
                            NULL,
                            NULL
                            );

  if (EFI_ERROR (Status) && (BootMode != BOOT_ON_S3_RESUME)) {   
    //
    // The DxeIpl has not yet been shadowed
    //
    InstallEfiPeiFlushInstructionCache (&PeiEfiPeiFlushInstructionCache);
    InstallEfiPeiPeCoffLoader (PeiServices, &PeiEfiPeiPeCoffLoader, NULL);

    //
    // Shadow DxeIpl and then re-run its entry point
    //
    Status = ShadowDxeIpl (
              PeiServices,
              FfsHeader,
              PeiEfiPeiPeCoffLoader,
              PeiEfiPeiFlushInstructionCache
              );

    if (EFI_ERROR (Status)) {
      return Status;
    }

  } else {  
    if (BootMode != BOOT_ON_S3_RESUME) {
      //
      // The DxeIpl has been shadowed
      //
      gPeiServices = PeiServices;

      //
      // Install LoadFile PPI
      //
      Status = (*PeiServices)->InstallPpi (PeiServices, &mPpiLoadFile);

      if (EFI_ERROR (Status)) {
        return Status;
      }
    }
    
    //
    // Install DxeIpl PPI
    //
    (*PeiServices)->InstallPpi (PeiServices, &mPpiList);

    if (EFI_ERROR (Status)) {
      return Status;
    }

  }

  return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
DxeLoadCore (
  IN EFI_DXE_IPL_PPI       *This,
  IN EFI_PEI_SERVICES      **PeiServices,
  IN EFI_PEI_HOB_POINTERS  HobList
  )
/*++

Routine Description:

  Main entry point to last PEIM

Arguments:

  This         - Entry point for DXE IPL PPI
  PeiServices  - General purpose services available to every PEIM.
  HobList      - Address to the Pei HOB list

Returns:

  EFI_SUCCESS          - DEX core was successfully loaded.
  EFI_OUT_OF_RESOURCES - There are not enough resources to load DXE core.

--*/
{
  EFI_STATUS                                Status;
  EFI_PHYSICAL_ADDRESS                      TopOfStack;
  EFI_PHYSICAL_ADDRESS                      BaseOfStack;
  EFI_PHYSICAL_ADDRESS                      BspStore;
  EFI_GUID                                  DxeCoreFileName;
  VOID                                      *Pe32Data;
  EFI_GUID                                  FirmwareFileName;
  VOID                                      *FvImageData;
  VOID                                      *Interface;
  EFI_PHYSICAL_ADDRESS                      DxeCoreAddress;
  UINT64                                    DxeCoreSize;
  EFI_PHYSICAL_ADDRESS                      DxeCoreEntryPoint;
  EFI_DECOMPRESS_PROTOCOL                   *PeiEfiDecompress;
  EFI_TIANO_DECOMPRESS_PROTOCOL             *PeiEfiTianoDecompress;
  EFI_CUSTOMIZED_DECOMPRESS_PROTOCOL        *PeiEfiCustomizedDecompress;
  EFI_PEI_FLUSH_INSTRUCTION_CACHE_PROTOCOL  *PeiEfiPeiFlushInstructionCache;
  EFI_PEI_PE_COFF_LOADER_PROTOCOL           *PeiEfiPeiPeCoffLoader;
  EFI_PEI_TRANSFER_CONTROL_PROTOCOL         *PeiEfiPeiTransferControl;
  EFI_BOOT_MODE                             BootMode;
  PEI_RECOVERY_MODULE_INTERFACE             *PeiRecovery;
  PEI_S3_RESUME_PPI                         *S3Resume;

  PEI_PERF_START (PeiServices, L"DxeIpl", NULL, 0);

  TopOfStack  = 0;
  BaseOfStack = 0;
  BspStore    = 0;

  //
  // if in S3 Resume, restore configure
  //
  Status = (*PeiServices)->GetBootMode (PeiServices, &BootMode);

  if (!EFI_ERROR (Status) && (BootMode == BOOT_ON_S3_RESUME)) {
    Status = (*PeiServices)->LocatePpi (
                              PeiServices,
                              &gPeiS3ResumePpiGuid,
                              0,
                              NULL,
                              &S3Resume
                              );

    ASSERT_PEI_ERROR (PeiServices, Status);

    Status = S3Resume->S3RestoreConfig (PeiServices);

    ASSERT_PEI_ERROR (PeiServices, Status);
  }

  Status = EFI_SUCCESS;

  //
  // Get the EFI decompress functions for possible usage
  //
  Status = InstallEfiDecompress (&PeiEfiDecompress);

  ASSERT_PEI_ERROR (PeiServices, Status);

  //
  // Get the Tiano decompress functions for possible usage
  //
  Status = InstallTianoDecompress (&PeiEfiTianoDecompress);

  ASSERT_PEI_ERROR (PeiServices, Status);

  //
  // Get the user Customized decompress functions for possible usage
  //
  Status = InstallCustomizedDecompress (&PeiEfiCustomizedDecompress);

  ASSERT_PEI_ERROR (PeiServices, Status);

  //
  // Install the PEI Protocols that are shared between PEI and DXE
  //
  PeiEfiPeiPeCoffLoader = NULL;
  Status                = InstallEfiPeiFlushInstructionCache (&PeiEfiPeiFlushInstructionCache);

  ASSERT_PEI_ERROR (PeiServices, Status);

  Status = InstallEfiPeiTransferControl (&PeiEfiPeiTransferControl);

  ASSERT_PEI_ERROR (PeiServices, Status);

  Status = InstallEfiPeiPeCoffLoader (PeiServices, &PeiEfiPeiPeCoffLoader, NULL);

  ASSERT_PEI_ERROR (PeiServices, Status);

  //
  // Allocate 128KB for the Stack
  //
  Status = (*PeiServices)->AllocatePages (
                            PeiServices,
                            EfiBootServicesData,
                            EFI_SIZE_TO_PAGES (EFI_STACK_SIZE),
                            &BaseOfStack
                            );

  ASSERT_PEI_ERROR (PeiServices, Status);

  //
  // Update the contents of BSP stack HOB to reflect the real stack info passed to DxeCore.
  //    
  UpdateStackHob (PeiServices, BaseOfStack, EFI_STACK_SIZE);
  
  
  //
  // Compute the top of the stack we were allocated. Pre-allocate a UINTN
  // for safety.
  //
  TopOfStack = BaseOfStack + EFI_SIZE_TO_PAGES (EFI_STACK_SIZE) * EFI_PAGE_SIZE - sizeof (UINTN);
  //
  // Add architecture-specifc HOBs (including the BspStore HOB)
  //
  Status = CreateArchSpecificHobs (
            PeiServices,
            &BspStore
            );

  ASSERT_PEI_ERROR (PeiServices, Status);

  //
  // Add HOB for the EFI Decompress Protocol
  //
  Interface = (VOID *) PeiEfiDecompress;

  Status = PeiBuildHobGuidData (
            PeiServices,
            &mPeiEfiDecompressProtocolGuid,
            &Interface,
            sizeof (VOID *)
            );

  ASSERT_PEI_ERROR (PeiServices, Status);

  //
  // Add HOB for the Tiano Decompress Protocol
  //
  Interface = (VOID *) PeiEfiTianoDecompress;

  Status = PeiBuildHobGuidData (
            PeiServices,
            &mPeiEfiTianoDecompressProtocolGuid,
            &Interface,
            sizeof (VOID *)
            );

  ASSERT_PEI_ERROR (PeiServices, Status);

  //
  // Add HOB for the user customized Decompress Protocol
  //
  Interface = (VOID *) PeiEfiCustomizedDecompress;

  Status = PeiBuildHobGuidData (
            PeiServices,
            &mPeiEfiCustomizedDecompressProtocolGuid,
            &Interface,
            sizeof (VOID *)
            );

  ASSERT_PEI_ERROR (PeiServices, Status);

  //
  // Add HOB for the Flush Instruction Cache Protocol
  //
  Interface = (VOID *) PeiEfiPeiFlushInstructionCache;

  Status = PeiBuildHobGuidData (
            PeiServices,
            &mPeiEfiPeiFlushInstructionCacheGuid,
            &Interface,
            sizeof (VOID *)
            );

  ASSERT_PEI_ERROR (PeiServices, Status);

  //
  // Add HOB for the PE/COFF Loader Protocol
  //
  Interface = (VOID *) PeiEfiPeiPeCoffLoader;

  Status = PeiBuildHobGuidData (
            PeiServices,
            &mPeiEfiPeiPeCoffLoaderGuid,
            &Interface,
            sizeof (VOID *)
            );

  ASSERT_PEI_ERROR (PeiServices, Status);

  //
  // Add HOB for the Transfer Control Protocol
  //
  Interface = (VOID *) PeiEfiPeiTransferControl;

  Status = PeiBuildHobGuidData (
            PeiServices,
            &mPeiEfiPeiTransferControlGuid,
            &Interface,
            sizeof (VOID *)
            );

  ASSERT_PEI_ERROR (PeiServices, Status);

  //
  // See if we are in crisis recovery
  //
  Status = (*PeiServices)->GetBootMode (PeiServices, &BootMode);

  if (!EFI_ERROR (Status) && (BootMode == BOOT_IN_RECOVERY_MODE)) {

    Status = (*PeiServices)->LocatePpi (
                              PeiServices,
                              &gPeiRecoveryModulePpiGuid,
                              0,
                              NULL,
                              &PeiRecovery
                              );

    ASSERT_PEI_ERROR (PeiServices, Status);
    Status = PeiRecovery->LoadRecoveryCapsule (PeiServices, PeiRecovery);
    ASSERT_PEI_ERROR (PeiServices, Status);

    //
    // Now should have a HOB with the DXE core w/ the old HOB destroyed
    //
  }

  //
  // Find the EFI_FV_FILETYPE_FIRMWARE_VOLUME_IMAGE type compressed Firmware Volume file
  // The file found will be processed by PeiProcessFile: It will first be decompressed to
  // a normal FV, then a corresponding FV type hob will be built. 
  //
  Status = PeiFindFile (
             PeiServices,
             EFI_FV_FILETYPE_FIRMWARE_VOLUME_IMAGE,
             EFI_SECTION_FIRMWARE_VOLUME_IMAGE,
             &FirmwareFileName,
             &FvImageData
             );

  //
  // Find the DXE Core in a Firmware Volume
  //
  Status = PeiFindFile (
            PeiServices,
            EFI_FV_FILETYPE_DXE_CORE,
            EFI_SECTION_PE32,
            &DxeCoreFileName,
            &Pe32Data
            );

  ASSERT_PEI_ERROR (PeiServices, Status);

  //
  // Load the DXE Core from a Firmware Volume
  //
  Status = PeiLoadFile (
            PeiServices,
            PeiEfiPeiPeCoffLoader,
            PeiEfiPeiFlushInstructionCache,
            Pe32Data,
            &DxeCoreAddress,
            &DxeCoreSize,
            &DxeCoreEntryPoint
            );

  ASSERT_PEI_ERROR (PeiServices, Status);

  //
  // Transfer control to the DXE Core
  // The handoff state is simply a pointer to the HOB list
  //
  PEI_PERF_END (PeiServices, L"DxeIpl", NULL, 0);

  Status = (*PeiServices)->InstallPpi (PeiServices, &mPpiSignal);

  ASSERT_PEI_ERROR (PeiServices, Status);

  //
  // Add HOB for the DXE Core
  //
  Status = PeiBuildHobModule (
            PeiServices,
            &DxeCoreFileName,
            DxeCoreAddress,
            DxeCoreSize,
            DxeCoreEntryPoint
            );

  ASSERT_PEI_ERROR (PeiServices, Status);

  //
  // Report Status Code EFI_SW_PEI_PC_HANDOFF_TO_NEXT
  //
  PEI_REPORT_STATUS_CODE (
    PeiServices,
    EFI_PROGRESS_CODE,
    EFI_SOFTWARE_PEI_MODULE | EFI_SW_PEI_CORE_PC_HANDOFF_TO_NEXT,
    0,
    NULL,
    NULL
    );

  PEI_DEBUG ((PeiServices, EFI_D_INFO, "DXE Core Entry\n"));
  SwitchStacks (
    (VOID *) (UINTN) DxeCoreEntryPoint,
    (UINTN) (HobList.Raw),
    (VOID *) (UINTN) TopOfStack,
    (VOID *) (UINTN) BspStore
    );

  //
  // If we get here, then the DXE Core returned.  This is an error
  //
  ASSERT_PEI_ERROR (PeiServices, Status);

  return EFI_OUT_OF_RESOURCES;
}

EFI_STATUS
PeiFindFile (
  IN  EFI_PEI_SERVICES       **PeiServices,
  IN  UINT8                  Type,
  IN  UINT16                 SectionType,
  OUT EFI_GUID               *FileName,
  OUT VOID                   **Pe32Data
  )
/*++

Routine Description:

  Finds a PE/COFF of a specific Type and SectionType in the Firmware Volumes
  described in the HOB list. Able to search in a compression set in a FFS file.
  But only one level of compression is supported, that is, not able to search
  in a compression set that is within another compression set.

Arguments:

  PeiServices - General purpose services available to every PEIM.
  Type        - The Type of file to retrieve
  SectionType - The type of section to retrieve from a file
  FileName    - The name of the file found in the Firmware Volume
  Pe32Data    - Pointer to the beginning of the PE/COFF file found in the Firmware Volume

Returns:

  EFI_SUCCESS   - The file was found, and the name is returned in FileName, and a pointer to
                  the PE/COFF image is returned in Pe32Data
  EFI_NOT_FOUND - The file was not found in the Firmware Volumes present in the HOB List

--*/
{
  EFI_FIRMWARE_VOLUME_HEADER  *FwVolHeader;
  EFI_FFS_FILE_HEADER         *FfsFileHeader;
  VOID                        *SectionData;
  EFI_STATUS                  Status;
  BOOLEAN                     Found;
  EFI_PEI_HOB_POINTERS        Hob;

  Status = (*PeiServices)->GetHobList (PeiServices, &Hob.Raw);

  if (EFI_ERROR (Status)) {
    return Status;
  }

  Found         = FALSE;

  Status        = EFI_SUCCESS;

  FwVolHeader   = NULL;
  FfsFileHeader = NULL;
  SectionData   = NULL;

  //
  // Foreach Firmware Volume, look for a specified type
  // of file and break out when one is found
  //
  Hob.Raw = GetHob (EFI_HOB_TYPE_FV, Hob.Raw);
  if (Hob.Header->HobType != EFI_HOB_TYPE_FV) {
    return EFI_NOT_FOUND;
  }

  while (!END_OF_HOB_LIST (Hob)) {

    FwVolHeader = (EFI_FIRMWARE_VOLUME_HEADER *) (UINTN) (Hob.FirmwareVolume->BaseAddress);

    Status = (*PeiServices)->FfsFindNextFile (
                              PeiServices,
                              Type,
                              FwVolHeader,
                              &FfsFileHeader
                              );

    if (EFI_ERROR (Status)) {
      Hob.Raw = GET_NEXT_HOB (Hob);
      Hob.Raw = GetHob (EFI_HOB_TYPE_FV, Hob.Raw);
      if (Hob.Header->HobType != EFI_HOB_TYPE_FV) {
        break;
      }

      continue;
    } else {

      Status = PeiProcessFile (
                PeiServices,
                SectionType,
                FfsFileHeader,
                Pe32Data,
                &Hob
                );

      //
      // Copy the File GUID from the FFS file that Pe32Data is found
      //
      (*PeiServices)->CopyMem (
                        FileName,
                        &FfsFileHeader->Name,
                        sizeof (EFI_GUID)
                        );

      //
      // Find all Fv type ffs to get all FvImage and add them into FvHob
      //
      if (!EFI_ERROR (Status) && (Type != EFI_FV_FILETYPE_FIRMWARE_VOLUME_IMAGE)) {
        return EFI_SUCCESS;
      }
    }
  }

  return EFI_NOT_FOUND;
}

EFI_STATUS
PeiLoadFile (
  IN  EFI_PEI_SERVICES                          **PeiServices,
  IN  EFI_PEI_PE_COFF_LOADER_PROTOCOL           *PeiEfiPeiPeCoffLoader,
  IN  EFI_PEI_FLUSH_INSTRUCTION_CACHE_PROTOCOL  *PeiEfiPeiFlushInstructionCache,
  IN  VOID                                      *Pe32Data,
  OUT EFI_PHYSICAL_ADDRESS                      *ImageAddress,
  OUT UINT64                                    *ImageSize,
  OUT EFI_PHYSICAL_ADDRESS                      *EntryPoint
  )
/*++

Routine Description:

  Loads and relocates a PE/COFF image into memory.

Arguments:

  PeiService         - General purpose services available to every PEIM.
  PeiEfiPeiPeCoffLoader - Pointer to a PE COFF loader protocol
  PeiEfiPeiFlushInstructionCache  - Pointer to a flush-instruction-cache protocol so
                                    we can flush the cache after loading
  Pe32Data         - The base address of the PE/COFF file that is to be loaded and relocated
  ImageAddress     - The base address of the relocated PE/COFF image
  ImageSize        - The size of the relocated PE/COFF image
  EntryPoint       - The entry point of the relocated PE/COFF image

Returns:

  EFI_SUCCESS   - The file was loaded and relocated
  EFI_OUT_OF_RESOURCES - There was not enough memory to load and relocate the PE/COFF file

--*/
{
  EFI_STATUS                            Status;
  EFI_PEI_PE_COFF_LOADER_IMAGE_CONTEXT  ImageContext;
  EFI_PHYSICAL_ADDRESS                  MemoryBuffer;

  (*PeiServices)->SetMem (
                    &ImageContext,
                    sizeof (ImageContext),
                    0
                    );
  ImageContext.Handle = Pe32Data;
  Status              = GetImageReadFunction (PeiServices, &ImageContext);

  ASSERT_PEI_ERROR (PeiServices, Status);

  Status = PeiEfiPeiPeCoffLoader->GetImageInfo (PeiEfiPeiPeCoffLoader, &ImageContext);
  if (EFI_ERROR (Status)) {
    return Status;
  }
  //
  // Allocate Memory for the image
  //
  Status = (*PeiServices)->AllocatePages (
                            PeiServices,
                            EfiBootServicesData,
                            EFI_SIZE_TO_PAGES ((UINT32) ImageContext.ImageSize),
                            &MemoryBuffer
                            );

  ASSERT_PEI_ERROR (PeiServices, Status);

  ImageContext.ImageAddress = MemoryBuffer;

  if (ImageContext.IsTeImage &&
      ((EFI_TE_IMAGE_HEADER *) Pe32Data)->Machine == EFI_IMAGE_MACHINE_IA64) {
    ImageContext.ImageAddress = ImageContext.ImageAddress + 
                                  ((EFI_TE_IMAGE_HEADER *) Pe32Data)->StrippedSize -
                                  sizeof (EFI_TE_IMAGE_HEADER);
  }
  
  //
  // Load the image to our new buffer
  //
  Status = PeiEfiPeiPeCoffLoader->LoadImage (PeiEfiPeiPeCoffLoader, &ImageContext);
  if (EFI_ERROR (Status)) {
    return Status;
  }
  //
  // Relocate the image in our new buffer
  //
  Status = PeiEfiPeiPeCoffLoader->RelocateImage (PeiEfiPeiPeCoffLoader, &ImageContext);
  if (EFI_ERROR (Status)) {
    return Status;
  }
  //
  // Flush the instruction cache so the image data is written before we execute it
  //
  Status = PeiEfiPeiFlushInstructionCache->Flush (
                                            PeiEfiPeiFlushInstructionCache,
                                            ImageContext.ImageAddress,
                                            ImageContext.ImageSize
                                            );

  if (EFI_ERROR (Status)) {
    return Status;
  }

  *ImageAddress = ImageContext.ImageAddress;
  *ImageSize    = ImageContext.ImageSize;
  *EntryPoint   = ImageContext.EntryPoint;

  return EFI_SUCCESS;
}

EFI_STATUS
ShadowDxeIpl (
  IN EFI_PEI_SERVICES                          **PeiServices,
  IN EFI_FFS_FILE_HEADER                       *DxeIpl,
  IN EFI_PEI_PE_COFF_LOADER_PROTOCOL           *PeiEfiPeiPeCoffLoader,
  IN EFI_PEI_FLUSH_INSTRUCTION_CACHE_PROTOCOL  *PeiEfiPeiFlushInstructionCache
  )
/*++

Routine Description:

  Shadow the DXE IPL to a different memory location. This occurs after permanent
  memory has been discovered.

Arguments:

  PeiService        - General purpose services available to every PEIM.
  DxeIpl            - Pointer to the FFS file header of the DXE IPL driver
  PeiEfiPeiPeCoffLoader - Pointer to a PE COFF loader protocol
  PeiEfiPeiFlushInstructionCache  - Pointer to a flush-instruction-cache protocol so
                                    we can flush the cache after shadowing the image

Returns:

  EFI_SUCCESS   - DXE IPL was successfully shadowed to a different memory location.
  EFI_ ERROR    - The shadow was unsuccessful.
  
--*/
{
  UINTN                     SectionLength;
  UINTN                     OccupiedSectionLength;
  EFI_PHYSICAL_ADDRESS      DxeIplAddress;
  UINT64                    DxeIplSize;
  EFI_PHYSICAL_ADDRESS      DxeIplEntryPoint;
  EFI_STATUS                Status;
  EFI_COMMON_SECTION_HEADER *Section;

  Section = (EFI_COMMON_SECTION_HEADER *) (DxeIpl + 1);

  while ((Section->Type != EFI_SECTION_PE32) && (Section->Type != EFI_SECTION_TE)) {
    SectionLength         = *(UINT32 *) (Section->Size) & 0x00ffffff;
    OccupiedSectionLength = GetOccupiedSize (SectionLength, 4);
    Section               = (EFI_COMMON_SECTION_HEADER *) ((UINT8 *) Section + OccupiedSectionLength);
  }
  
  //
  // Relocate DxeIpl into memory by using loadfile service
  //
  Status = PeiLoadFile (
            PeiServices,
            PeiEfiPeiPeCoffLoader,
            PeiEfiPeiFlushInstructionCache,
            (VOID *) (Section + 1),
            &DxeIplAddress,
            &DxeIplSize,
            &DxeIplEntryPoint
            );

  if (Status == EFI_SUCCESS) {
    //
    // Install PeiInMemory to indicate the Dxeipl is shadowed
    //
    Status = (*PeiServices)->InstallPpi (PeiServices, &mPpiPeiInMemory);

    if (EFI_ERROR (Status)) {
      return Status;
    }

    Status = ((DXE_IPL_ENTRYPOINT) (UINTN) DxeIplEntryPoint) (DxeIpl, PeiServices);
  }

  return Status;
}

EFI_STATUS
EFIAPI
DxeIplLoadFile (
  IN EFI_PEI_FV_FILE_LOADER_PPI                 *This,
  IN  EFI_FFS_FILE_HEADER                       *FfsHeader,
  OUT EFI_PHYSICAL_ADDRESS                      *ImageAddress,
  OUT UINT64                                    *ImageSize,
  OUT EFI_PHYSICAL_ADDRESS                      *EntryPoint
  )
/*++

Routine Description:

  Given a pointer to an FFS file containing a PE32 image, get the
  information on the PE32 image, and then "load" it so that it
  can be executed.

Arguments:

  This  - pointer to our file loader protocol
  FfsHeader - pointer to the FFS file header of the FFS file that
              contains the PE32 image we want to load
  ImageAddress  - returned address where the PE32 image is loaded
  ImageSize     - returned size of the loaded PE32 image
  EntryPoint    - entry point to the loaded PE32 image

Returns:
 
  EFI_SUCCESS  - The FFS file was successfully loaded.
  EFI_ERROR    - Unable to load the FFS file.

--*/
{
  EFI_PEI_PE_COFF_LOADER_PROTOCOL           *PeiEfiPeiPeCoffLoader;
  EFI_PEI_FLUSH_INSTRUCTION_CACHE_PROTOCOL  *PeiEfiPeiFlushInstructionCache;
  EFI_STATUS                                Status;
  VOID                                      *Pe32Data;

  Pe32Data = NULL;
  InstallEfiPeiFlushInstructionCache (&PeiEfiPeiFlushInstructionCache);
  InstallEfiPeiPeCoffLoader (gPeiServices, &PeiEfiPeiPeCoffLoader, NULL);
  //
  // Preprocess the FFS file to get a pointer to the PE32 information
  // in the enclosed PE32 image.
  //
  Status = PeiProcessFile (
            gPeiServices,
            EFI_SECTION_PE32,
            FfsHeader,
            &Pe32Data,
            NULL
            );

  if (EFI_ERROR (Status)) {
    return Status;
  }
  //
  // Load the PE image from the FFS file
  //
  Status = PeiLoadFile (
            gPeiServices,
            PeiEfiPeiPeCoffLoader,
            PeiEfiPeiFlushInstructionCache,
            Pe32Data,
            ImageAddress,
            ImageSize,
            EntryPoint
            );

  return Status;
}

EFI_STATUS
PeiProcessFile (
  IN     EFI_PEI_SERVICES       **PeiServices,
  IN     UINT16                 SectionType,
  IN     EFI_FFS_FILE_HEADER    *FfsFileHeader,
  OUT    VOID                   **Pe32Data,
  IN     EFI_PEI_HOB_POINTERS   *OrigHob
  )
/*++

Routine Description:
  
Arguments:

  PeiServices        - General purpose services available to every PEIM.

  SectionType        - The type of section in the FFS file to process.

  FfsFileHeader      - Pointer to the FFS file to process, looking for the
                       specified SectionType and return pointer to FFS file that PE32 image is found

  Pe32Data           - returned pointer to the start of the PE32 image found
                       in the FFS file.
  OrigHob            - Point to the FV Hob that contains the FV type ffs file.                      

Returns:

  EFI_SUCCESS       - found the PE32 section in the FFS file

--*/
{
  EFI_STATUS                      Status;
  VOID                            *SectionData;
  EFI_TIANO_DECOMPRESS_PROTOCOL   *DecompressProtocol;
  EFI_PHYSICAL_ADDRESS            OldTopOfMemory;
  UINT8                           *DstBuffer;
  UINT8                           *ScratchBuffer;
  UINT32                          DstBufferSize;
  UINTN                           ScratchBufferSize;
  EFI_COMMON_SECTION_HEADER       *CmpSection;
  UINTN                           CmpSectionLength;
  UINTN                           OccupiedCmpSectionLength;
  VOID                            *CmpFileData;
  UINTN                           CmpFileSize;
  EFI_COMMON_SECTION_HEADER       *Section;
  UINTN                           SectionLength;
  UINTN                           OccupiedSectionLength;
  UINT64                          FileSize;
  EFI_GUID_DEFINED_SECTION        *GuidedSectionHeader;
  UINT32                          AuthenticationStatus;
  EFI_PEI_SECTION_EXTRACTION_PPI  *SectionExtract;
  UINT32                          BufferSize;
  UINT8                           *Buffer;
  PEI_SECURITY_PPI                *Security;
  BOOLEAN                         StartCrisisRecovery;
  EFI_GUID                        TempGuid;
  EFI_COMPRESSION_SECTION         *CompressionSection;
  EFI_FIRMWARE_VOLUME_HEADER      *FvHeader;
  UINT32                          FvAlignment;
  VOID                            *SectionInMemory;


  FvAlignment = 0;
  Status = (*PeiServices)->FfsFindSectionData (
                            PeiServices,
                            EFI_SECTION_COMPRESSION,
                            FfsFileHeader,
                            &SectionData
                            );

  //
  // Upon finding a DXE Core file, see if there is first a compression section
  //
  if (!EFI_ERROR (Status)) {
    //
    // Yes, there is a compression section, so extract the contents
    // Decompress the image here
    //
    Section = (EFI_COMMON_SECTION_HEADER *) (UINTN) (VOID *) ((UINT8 *) (FfsFileHeader) + (UINTN) sizeof (EFI_FFS_FILE_HEADER));

    do {
      SectionLength         = *(UINT32 *) (Section->Size) & 0x00ffffff;
      OccupiedSectionLength = GetOccupiedSize (SectionLength, 4);

      //
      // Was the DXE Core file encapsulated in a GUID'd section?
      //
      if (Section->Type == EFI_SECTION_GUID_DEFINED) {
        //
        // Locate the GUID'd Section Extractor
        //
        GuidedSectionHeader = (VOID *) (Section + 1);

        //
        // This following code constitutes the addition of the security model
        // to the DXE IPL.
        //
        //
        // Set a default authenticatino state
        //
        AuthenticationStatus = 0;

        Status = (*PeiServices)->LocatePpi (
                                  PeiServices,
                                  &gPeiSectionExtractionPpiGuid,
                                  0,
                                  NULL,
                                  &SectionExtract
                                  );

        if (EFI_ERROR (Status)) {
          return Status;
        }
        //
        // Verify Authentication State
        //
        (*PeiServices)->CopyMem (&TempGuid, Section + 1, sizeof (EFI_GUID));

        Status = SectionExtract->PeiGetSection (
                                  PeiServices,
                                  SectionExtract,
                                  (EFI_SECTION_TYPE *) &SectionType,
                                  &TempGuid,
                                  0,
                                  (VOID **) &Buffer,
                                  &BufferSize,
                                  &AuthenticationStatus
                                  );

        if (EFI_ERROR (Status)) {
          return Status;
        }
        //
        // If not ask the Security PPI, if exists, for disposition
        //
        //
        Status = (*PeiServices)->LocatePpi (
                                  PeiServices,
                                  &gPeiSecurityPpiGuid,
                                  0,
                                  NULL,
                                  &Security
                                  );
        if (EFI_ERROR (Status)) {
          return Status;
        }

        Status = Security->AuthenticationState (
                            PeiServices,
                            (struct _PEI_SECURITY_PPI *) Security,
                            AuthenticationStatus,
                            FfsFileHeader,
                            &StartCrisisRecovery
                            );

        if (EFI_ERROR (Status)) {
          return Status;
        }
        //
        // If there is a security violation, report to caller and have
        // the upper-level logic possible engender a crisis recovery
        //
        if (StartCrisisRecovery) {
          return EFI_SECURITY_VIOLATION;
        }
      }

      if (Section->Type == EFI_SECTION_PE32) {
        //
        // This is what we want
        //
        *Pe32Data = (VOID *) (Section + 1);
        return EFI_SUCCESS;
      } else if (Section->Type == EFI_SECTION_COMPRESSION) {
        //
        // This is a compression set, expand it
        //
        CompressionSection  = (EFI_COMPRESSION_SECTION *) Section;
        DecompressProtocol  = NULL;

        switch (CompressionSection->CompressionType) {
        case EFI_STANDARD_COMPRESSION:
          Status = InstallTianoDecompress (&DecompressProtocol);
          break;

        case EFI_CUSTOMIZED_COMPRESSION:
          //
          // Load user customized compression protocol.
          //
          Status = InstallCustomizedDecompress ((EFI_CUSTOMIZED_DECOMPRESS_PROTOCOL **) &DecompressProtocol);
          break;

        case EFI_NOT_COMPRESSED:
          //
          // Need to support not compressed file
          //
          Status = EFI_UNSUPPORTED;
          break;

        default:
          Status = EFI_UNSUPPORTED;
        }
        //
        // Unsupport compression type
        //
        if (EFI_ERROR (Status)) {
          ASSERT_PEI_ERROR (PeiServices, Status);
          return EFI_NOT_FOUND;
        }

        Status = (*PeiServices)->AllocatePages (
                                  PeiServices,
                                  EfiBootServicesData,
                                  1,  // EFI_PAGE_SIZE,
                                  &OldTopOfMemory
                                  );

        if (EFI_ERROR (Status)) {
          return EFI_OUT_OF_RESOURCES;
        }

        DstBufferSize     = 0;
        ScratchBufferSize = 0;
        DstBuffer         = (UINT8 *) (UINTN) (OldTopOfMemory);
        ScratchBuffer     = (UINT8 *) (UINTN) (OldTopOfMemory);
        //
        // Copy compressed section to memory before decompess
        //
        Status = (*PeiServices)->AllocatePages (
                                   PeiServices,
                                   EfiBootServicesData,
                                   EFI_SIZE_TO_PAGES(SectionLength),
                                   &OldTopOfMemory);
        if (EFI_ERROR (Status)) {
          return EFI_OUT_OF_RESOURCES;
        }
        SectionInMemory = (VOID*) (UINTN) OldTopOfMemory;
        //
        // If PEI core is shadowed, (*PeiServices)->CopyMem is preferred to save size
        //
        EfiCommonLibCopyMem (SectionInMemory, Section, SectionLength);

        Status = DecompressProtocol->GetInfo (
                                      DecompressProtocol,
                                      (UINT8 *) ((EFI_COMPRESSION_SECTION *) SectionInMemory + 1),
                                      (UINT32) SectionLength - sizeof (EFI_COMPRESSION_SECTION),
                                      (UINT32 *) &DstBufferSize,
                                      (UINT32 *) &ScratchBufferSize
                                      );

        if (EFI_SUCCESS == Status) {
          //
          // This is a compression set, expand it
          //
          Status = (*PeiServices)->AllocatePages (
                                    PeiServices,
                                    EfiBootServicesData,
                                    EFI_SIZE_TO_PAGES (ScratchBufferSize),
                                    &OldTopOfMemory
                                    );

          if (EFI_ERROR (Status)) {
            return EFI_OUT_OF_RESOURCES;
          }

          ScratchBuffer = (UINT8 *) (UINTN) (OldTopOfMemory);

          //
          // Allocate destination buffer
          //
          Status = (*PeiServices)->AllocatePages (
                                    PeiServices,
                                    EfiBootServicesData,
                                    EFI_SIZE_TO_PAGES (DstBufferSize),
                                    &OldTopOfMemory
                                    );

          if (EFI_ERROR (Status)) {
            return EFI_OUT_OF_RESOURCES;
          }

          DstBuffer = (UINT8 *) (UINTN) (OldTopOfMemory);

          //
          // Call decompress function
          //
          Status = DecompressProtocol->Decompress (
                                        DecompressProtocol,
                                        (CHAR8 *) ((EFI_COMPRESSION_SECTION *) SectionInMemory + 1),
                                        (UINT32) SectionLength - sizeof (EFI_COMPRESSION_SECTION),
                                        DstBuffer,
                                        (UINT32) DstBufferSize,
                                        ScratchBuffer,
                                        (UINT32) ScratchBufferSize
                                        );
        }

        if (EFI_ERROR (Status)) {
          //
          // Decompress failed
          //
          PEI_DEBUG ((PeiServices, EFI_D_ERROR, "Decompress - %r\n", Status));
          return EFI_NOT_FOUND;
        }

        //
        // Decompress successfully.
        // Loop the decompressed data searching for expected section.
        //
        CmpSection = (EFI_COMMON_SECTION_HEADER *) DstBuffer;
        CmpFileData = (VOID *) DstBuffer;
        CmpFileSize = DstBufferSize;
        do {
          CmpSectionLength = *(UINT32 *) (CmpSection->Size) & 0x00ffffff;
          if (CmpSection->Type == SectionType) {
            //
            // This is what we want
            //
            if (SectionType == EFI_SECTION_PE32) {
              *Pe32Data = (VOID *) (CmpSection + 1);
              return EFI_SUCCESS;
            } else if (SectionType == EFI_SECTION_FIRMWARE_VOLUME_IMAGE) {
              // 
              // Firmware Volume Image in this Section
              // Skip the section header to get FvHeader
              //
              FvHeader = (EFI_FIRMWARE_VOLUME_HEADER *) (CmpSection + 1);
    
              if (FvHeader->Signature == EFI_FVH_SIGNATURE) {
                //
                // Because FvLength in FvHeader is UINT64 type, 
                // so FvHeader must meed at least 8 bytes alignment.
                // If current FvImage base address doesn't meet its alignment,
                // we need to reload this FvImage to another correct memory address.
                //
                Status = GetFvAlignment(FvHeader, &FvAlignment); 
                if (EFI_ERROR(Status)) {
                  return Status;
                }
                if (((UINTN) FvHeader % FvAlignment) != 0) {
                  DstBuffer = AllocateAlignedPages (PeiServices, EFI_SIZE_TO_PAGES ((UINTN) CmpSectionLength - sizeof (EFI_COMMON_SECTION_HEADER)), FvAlignment);
                  if (DstBuffer == NULL) {
                    return EFI_OUT_OF_RESOURCES;
                  }
                  (*PeiServices)->CopyMem (DstBuffer, FvHeader, (UINTN) CmpSectionLength - sizeof (EFI_COMMON_SECTION_HEADER));
                  FvHeader = (EFI_FIRMWARE_VOLUME_HEADER *) DstBuffer;  
                }

                //
                // Build new FvHob for new decompressed Fv image.
                //
                PeiBuildHobFv (PeiServices, (EFI_PHYSICAL_ADDRESS) (UINTN) FvHeader, FvHeader->FvLength);
                
                //
                // Set the original FvHob to unused.
                //
                if (OrigHob != NULL) {
                  OrigHob->Header->HobType = EFI_HOB_TYPE_UNUSED;
                  //
                  // Reserved Flash MemoryIo resource
                  //
                  Status = PeiBuildHobMemoryAllocation (PeiServices,
                                                        OrigHob->FirmwareVolume->BaseAddress,
                                                        OrigHob->FirmwareVolume->Length,
                                                        NULL,
                                                        EfiMemoryMappedIO
                                                        );
                  ASSERT_PEI_ERROR (PeiServices, Status);
                }
                
                //
                // return found FvImage data.
                //
                *Pe32Data = (VOID *) FvHeader;
                return EFI_SUCCESS;
              }
            }
          }
          OccupiedCmpSectionLength  = GetOccupiedSize (CmpSectionLength, 4);
          CmpSection                = (EFI_COMMON_SECTION_HEADER *) ((UINT8 *) CmpSection + OccupiedCmpSectionLength);
        } while (CmpSection->Type != 0 && (UINTN) ((UINT8 *) CmpSection - (UINT8 *) CmpFileData) < CmpFileSize);
      }

      Section   = (EFI_COMMON_SECTION_HEADER *) ((UINT8 *) Section + OccupiedSectionLength);
      FileSize  = FfsFileHeader->Size[0] & 0xFF;
      FileSize += (FfsFileHeader->Size[1] << 8) & 0xFF00;
      FileSize += (FfsFileHeader->Size[2] << 16) & 0xFF0000;
      FileSize &= 0x00FFFFFF;
    } while (Section->Type != 0 && (UINTN) ((UINT8 *) Section - (UINT8 *) FfsFileHeader) < FileSize);

    //
    // End of the decompression activity
    //
  } else {

    Status = (*PeiServices)->FfsFindSectionData (
                              PeiServices,
                              EFI_SECTION_PE32,
                              FfsFileHeader,
                              &SectionData
                              );

    if (EFI_ERROR (Status)) {
      Status = (*PeiServices)->FfsFindSectionData (
                                PeiServices,
                                EFI_SECTION_TE,
                                FfsFileHeader,
                                &SectionData
                                );
      if (EFI_ERROR (Status)) {
        return Status;
      }
    }
  }

  *Pe32Data = SectionData;

  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
GetFvAlignment (
  IN    EFI_FIRMWARE_VOLUME_HEADER   *FvHeader,
  OUT   UINT32                      *FvAlignment
  )
{
  UINT8 Index;
  //
  // Because FvLength in FvHeader is UINT64 type, 
  // so FvHeader must meed at least 8 bytes alignment.
  // we scan the Fv alignment attribute from 8 bytes to 64K.
  // And get the least alignment.
  //
  if (FvHeader->Attributes & EFI_FVB_ALIGNMENT_CAP) {
    for (Index = 3; Index <= 16; Index ++) {
      if ((FvHeader->Attributes & ((1 << Index) * EFI_FVB_ALIGNMENT_CAP)) != 0 ) {
        *FvAlignment = (1 << Index);
        return EFI_SUCCESS;
      }
    }
  }
  return EFI_UNSUPPORTED;
}


STATIC
VOID *
EFIAPI
AllocateAlignedPages (
  IN EFI_PEI_SERVICES       **PeiServices,
  IN UINTN                  Pages,
  IN UINTN                  Alignment
  )
{
  VOID                  *Memory;
  UINTN                 AlignmentMask;
  EFI_STATUS            Status;
  EFI_PHYSICAL_ADDRESS  BufferAddress; 

  //
  // Alignment must be a power of two or zero.
  //
  PEI_ASSERT (PeiServices, (Alignment & (Alignment - 1)) == 0);

  if (Pages == 0) {
    return NULL;
  }
  //
  // Make sure that Pages plus EFI_SIZE_TO_PAGES (Alignment) does not overflow.
  //
  PEI_ASSERT (PeiServices,Pages <= (EFI_MAX_ADDRESS - EFI_SIZE_TO_PAGES (Alignment)));
  //
  // We would rather waste some memory to save PEI code size.
  //
  if ((Pages + EFI_SIZE_TO_PAGES (Alignment)) == 0) {
    return NULL;
  }

  Status = ((*PeiServices)->AllocatePages) (PeiServices, EfiBootServicesData, (Pages + EFI_SIZE_TO_PAGES (Alignment)), &BufferAddress);

  if (EFI_ERROR (Status)) {
    BufferAddress = 0;
  }
  Memory = (VOID *) (UINTN) BufferAddress;

  if (Alignment == 0) {
    AlignmentMask = Alignment;
  } else {
    AlignmentMask = Alignment - 1;  
  }
  return (VOID *) (UINTN) (((UINTN) Memory + AlignmentMask) & ~AlignmentMask);
}

STATIC
VOID
UpdateStackHob (
  IN EFI_PEI_SERVICES            **PeiServices,
  IN EFI_PHYSICAL_ADDRESS        BaseAddress,
  IN UINT64                      Length
  )
{
  EFI_STATUS                    Status;
  EFI_PEI_HOB_POINTERS          Hob;
  
  Status = (*PeiServices)->GetHobList (PeiServices, &Hob.Raw);

  while ((Hob.Raw = GetHob (EFI_HOB_TYPE_MEMORY_ALLOCATION, Hob.Raw)) != NULL) {
    if (CompareGuid (&gEfiHobMemeryAllocStackGuid, &(Hob.MemoryAllocationStack->AllocDescriptor.Name))) {
      //
      // Build a new memory allocation HOB with old stack info with EfiConventionalMemory type
      // to be reclaimed by DXE core.
      //
      PeiBuildHobMemoryAllocation (
        PeiServices,
        Hob.MemoryAllocationStack->AllocDescriptor.MemoryBaseAddress,
        Hob.MemoryAllocationStack->AllocDescriptor.MemoryLength,
        NULL,
        EfiConventionalMemory
        );
      //
      // Update the BSP Stack Hob to reflect the new stack info.
      //
      Hob.MemoryAllocationStack->AllocDescriptor.MemoryBaseAddress = BaseAddress;
      Hob.MemoryAllocationStack->AllocDescriptor.MemoryLength = Length;
      break;
    }
    Hob.Raw = GET_NEXT_HOB (Hob);
  }
}
