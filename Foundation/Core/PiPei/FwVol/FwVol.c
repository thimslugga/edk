/*++

Copyright (c) 2004 - 2009, Intel Corporation                                                         
All rights reserved. This program and the accompanying materials                          
are licensed and made available under the terms and conditions of the BSD License         
which accompanies this distribution.  The full text of the license may be found at        
http://opensource.org/licenses/bsd-license.php                                            
                                                                                          
THE PROGRAM IS DISTRIBUTED UNDER THE BSD LICENSE ON AN "AS IS" BASIS,                     
WITHOUT WARRANTIES OR REPRESENTATIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED.             

Module Name:

  FwVol.c

Abstract:

  Pei Core Firmware File System service routines.

--*/

#include "Tiano.h"
#include "EfiImageFormat.h"
#include "PeiCore.h"
#include "PeiLib.h"

#define GET_OCCUPIED_SIZE(ActualSize, Alignment) \
  (ActualSize) + (((Alignment) - ((ActualSize) & ((Alignment) - 1))) & ((Alignment) - 1))


EFI_PEI_NOTIFY_DESCRIPTOR mNotifyList = {
  (EFI_PEI_PPI_DESCRIPTOR_NOTIFY_CALLBACK | EFI_PEI_PPI_DESCRIPTOR_TERMINATE_LIST),
  &gEfiFirmwareVolumeInfoPpiGuid,
  FirmwareVolmeInfoPpiNotifyCallback 
};




STATIC
EFI_FFS_FILE_STATE
GetFileState(
  IN UINT8                ErasePolarity,
  IN EFI_FFS_FILE_HEADER  *FfsHeader
  )
/*++

Routine Description:

  Returns the highest bit set of the State field

Arguments:

  ErasePolarity   - Erase Polarity  as defined by EFI_FVB_ERASE_POLARITY
                    in the Attributes field.
  FfsHeader       - Pointer to FFS File Header.

Returns:
  Returns the highest bit in the State field

--*/
{
  EFI_FFS_FILE_STATE  FileState;
  EFI_FFS_FILE_STATE  HighestBit;

  FileState = FfsHeader->State;

  if (ErasePolarity != 0) {
    FileState = (EFI_FFS_FILE_STATE)~FileState;
  }

  HighestBit = 0x80;
  while (HighestBit != 0 && (HighestBit & FileState) == 0) {
    HighestBit >>= 1;
  }

  return HighestBit;
} 

STATIC
UINT8
CalculateHeaderChecksum (
  IN EFI_FFS_FILE_HEADER  *FileHeader
  )
/*++

Routine Description:

  Calculates the checksum of the header of a file.

Arguments:

  FileHeader       - Pointer to FFS File Header.

Returns:
  Checksum of the header.
  
  The header is zero byte checksum.
    - Zero means the header is good.
    - Non-zero means the header is bad.
    
  
Bugbug: For PEI performance reason, we comments this code at this time.
--*/
{
  UINT8   *ptr;
  UINTN   Index;
  UINT8   Sum;
  
  Sum = 0;
  ptr = (UINT8 *)FileHeader;

  for (Index = 0; Index < sizeof(EFI_FFS_FILE_HEADER) - 3; Index += 4) {
    Sum = (UINT8)(Sum + ptr[Index]);
    Sum = (UINT8)(Sum + ptr[Index+1]);
    Sum = (UINT8)(Sum + ptr[Index+2]);
    Sum = (UINT8)(Sum + ptr[Index+3]);
  }

  for (; Index < sizeof(EFI_FFS_FILE_HEADER); Index++) {
    Sum = (UINT8)(Sum + ptr[Index]);
  }
  
  //
  // State field (since this indicates the different state of file). 
  //
  Sum = (UINT8)(Sum - FileHeader->State);
  //
  // Checksum field of the file is not part of the header checksum.
  //
  Sum = (UINT8)(Sum - FileHeader->IntegrityCheck.Checksum.File);

  return Sum;
}

STATIC
BOOLEAN
EFIAPI
PeiFileHandleToVolume (
  IN   EFI_PEI_FILE_HANDLE     FileHandle,
  OUT  EFI_PEI_FV_HANDLE       *VolumeHandle
  )
{
  UINTN                       Index;
  PEI_CORE_INSTANCE           *PrivateData;
  EFI_FIRMWARE_VOLUME_HEADER  *FwVolHeader;

  PrivateData = PEI_CORE_INSTANCE_FROM_PS_THIS (GetPeiServicesTablePointer ());
  for (Index = 0; Index < PrivateData->FvCount; Index++) {
    FwVolHeader = PrivateData->Fv[Index].FvHeader;
    if (((UINTN)FileHandle > (UINTN)FwVolHeader ) &&   \
        ((UINTN)FileHandle < ((UINTN)FwVolHeader + (UINTN)FwVolHeader->FvLength))) {
      *VolumeHandle = (EFI_PEI_FV_HANDLE)FwVolHeader;
      return TRUE;
    }
  }
  return FALSE;
}


EFI_STATUS
PeiFindFileEx (
  IN  CONST EFI_PEI_FV_HANDLE        FvHandle,
  IN  CONST EFI_GUID                 *FileName,   OPTIONAL
  IN        EFI_FV_FILETYPE          SearchType,
  IN OUT    EFI_PEI_FILE_HANDLE      *FileHandle,
  IN OUT    EFI_PEI_FILE_HANDLE      *AprioriFile  OPTIONAL
  )
/*++

Routine Description:
    Given the input file pointer, search for the next matching file in the
    FFS volume as defined by SearchType. The search starts from FileHeader inside
    the Firmware Volume defined by FwVolHeader.

Arguments:
    PeiServices - Pointer to the PEI Core Services Table.
    SearchType - Filter to find only files of this type.
      Type EFI_FV_FILETYPE_ALL causes no filtering to be done.
    FwVolHeader - Pointer to the FV header of the volume to search.
      This parameter must point to a valid FFS volume.
    FileHeader  - Pointer to the current file from which to begin searching.
      This pointer will be updated upon return to reflect the file found.
    Flag        - Indicator for if this is for PEI Dispath search 
    
Returns:
    EFI_NOT_FOUND - No files matching the search criteria were found
    EFI_SUCCESS

--*/
{
  EFI_FIRMWARE_VOLUME_HEADER           *FwVolHeader;
  EFI_FFS_FILE_HEADER                   **FileHeader;
  EFI_FFS_FILE_HEADER                   *FfsFileHeader;
  EFI_FIRMWARE_VOLUME_EXT_HEADER        *FwVolExHeaderInfo;
  UINT32                                FileLength;
  UINT32                                FileOccupiedSize;
  UINT32                                FileOffset;
  UINT64                                FvLength;
  UINT8                                 ErasePolarity;
  UINT8                                 FileState;
  EFI_PEI_SERVICES                      **PeiServices;

  PeiServices = GetPeiServicesTablePointer();
  FwVolHeader = (EFI_FIRMWARE_VOLUME_HEADER *)FvHandle;
  FileHeader  = (EFI_FFS_FILE_HEADER **)FileHandle;

  FvLength = FwVolHeader->FvLength;
  if (FwVolHeader->Attributes & EFI_FVB_ERASE_POLARITY) {
    ErasePolarity = 1;
  } else {
    ErasePolarity = 0;
  }
  
  //
  // If FileHeader is not specified (NULL) or FileName is not NULL,
  // start with the first file in the firmware volume.  Otherwise,
  // start from the FileHeader.
  //
  if ((*FileHeader == NULL) || (FileName != NULL)) {
    FfsFileHeader = (EFI_FFS_FILE_HEADER *)((UINT8 *)FwVolHeader + FwVolHeader->HeaderLength);
    if (FwVolHeader->ExtHeaderOffset != 0) {
      FwVolExHeaderInfo = (EFI_FIRMWARE_VOLUME_EXT_HEADER *)(((UINT8 *)FwVolHeader) + FwVolHeader->ExtHeaderOffset);
      FfsFileHeader = (EFI_FFS_FILE_HEADER *)(((UINT8 *)FwVolExHeaderInfo) + FwVolExHeaderInfo->ExtHeaderSize);
    }
  } else {
    //
    // Length is 24 bits wide so mask upper 8 bits
    // FileLength is adjusted to FileOccupiedSize as it is 8 byte aligned.
    //
    FileLength = *(UINT32 *)(*FileHeader)->Size & 0x00FFFFFF;
    FileOccupiedSize = GET_OCCUPIED_SIZE (FileLength, 8);
    FfsFileHeader = (EFI_FFS_FILE_HEADER *)((UINT8 *)*FileHeader + FileOccupiedSize);
  }

  FileOffset = (UINT32) ((UINT8 *)FfsFileHeader - (UINT8 *)FwVolHeader);
  PEI_ASSERT (PeiServices,(FileOffset <= 0xFFFFFFFF));
  
  while (FileOffset < (FvLength - sizeof (EFI_FFS_FILE_HEADER))) {
    //
    // Get FileState which is the highest bit of the State 
    //
    FileState = GetFileState (ErasePolarity, FfsFileHeader);
    switch (FileState) {

    case EFI_FILE_HEADER_INVALID:
      FileOffset += sizeof(EFI_FFS_FILE_HEADER);
      FfsFileHeader = (EFI_FFS_FILE_HEADER *)((UINT8 *)FfsFileHeader + sizeof(EFI_FFS_FILE_HEADER));
      break;
        
    case EFI_FILE_DATA_VALID:
    case EFI_FILE_MARKED_FOR_UPDATE:
      if (CalculateHeaderChecksum (FfsFileHeader) != 0) {
        PEI_ASSERT (PeiServices,FALSE);
        return EFI_NOT_FOUND;
      }

      FileLength = *(UINT32 *)(FfsFileHeader->Size) & 0x00FFFFFF;
      FileOccupiedSize = GET_OCCUPIED_SIZE(FileLength, 8);

      if (FileName != NULL) {
        if (CompareGuid (&FfsFileHeader->Name, (EFI_GUID*)FileName)) {
          *FileHeader = FfsFileHeader;
          return EFI_SUCCESS;
        }
      } else if (SearchType == PEI_CORE_INTERNAL_FFS_FILE_DISPATCH_TYPE) {
        if ((FfsFileHeader->Type == EFI_FV_FILETYPE_PEIM) || 
            (FfsFileHeader->Type == EFI_FV_FILETYPE_COMBINED_PEIM_DRIVER)) { 
          
          *FileHeader = FfsFileHeader;
          return EFI_SUCCESS;
        } else if (AprioriFile != NULL) {
          if (FfsFileHeader->Type == EFI_FV_FILETYPE_FREEFORM) {
            if (CompareGuid (&FfsFileHeader->Name, &gEfiPeiAprioriGuid)) {
              *AprioriFile = FfsFileHeader;
            }           
          } 
        }
      } else if ((SearchType == FfsFileHeader->Type) || (SearchType == EFI_FV_FILETYPE_ALL)) { 
        *FileHeader = FfsFileHeader;
        return EFI_SUCCESS;
      }

      FileOffset += FileOccupiedSize; 
      FfsFileHeader = (EFI_FFS_FILE_HEADER *)((UINT8 *)FfsFileHeader + FileOccupiedSize);
      break;
    
    case EFI_FILE_DELETED:
      FileLength = *(UINT32 *)(FfsFileHeader->Size) & 0x00FFFFFF;
      FileOccupiedSize = GET_OCCUPIED_SIZE(FileLength, 8);
      FileOffset += FileOccupiedSize;
      FfsFileHeader = (EFI_FFS_FILE_HEADER *)((UINT8 *)FfsFileHeader + FileOccupiedSize);
      break;

    default:
      return EFI_NOT_FOUND;

    } 
  }

  return EFI_NOT_FOUND;  
}


VOID 
PeiInitializeFv (
  IN  PEI_CORE_INSTANCE           *PrivateData,
  IN CONST EFI_SEC_PEI_HAND_OFF   *SecCoreData
  )
/*++

Routine Description:

  Initialize PeiCore Fv List.

Arguments:
  PrivateData     - Pointer to PEI_CORE_INSTANCE.
  SecCoreData     - Pointer to EFI_SEC_PEI_HAND_OFF.

Returns:
  NONE  
  
--*/  
{
  EFI_STATUS  Status;
  //
  // The BFV must be the first entry. The Core FV support is stateless 
  // The AllFV list has a single entry per FV in PEI. 
  // The Fv list only includes FV that PEIMs will be dispatched from and
  // its File System Format is PI 1.0 definition.
  //
  PrivateData->FvCount = 1;
  PrivateData->Fv[0].FvHeader = (EFI_FIRMWARE_VOLUME_HEADER *)SecCoreData->BootFirmwareVolumeBase;

  PrivateData->AllFvCount = 1;
  PrivateData->AllFv[0] = (EFI_PEI_FV_HANDLE)PrivateData->Fv[0].FvHeader;


  //
  // Post a call-back for the FvInfoPPI services to expose
  // additional Fvs to PeiCore.
  //
  Status = (PrivateData->PS)->NotifyPpi (&PrivateData->PS, &mNotifyList);
  ASSERT_PEI_ERROR (&PrivateData->PS, Status);

}



EFI_STATUS
EFIAPI
FirmwareVolmeInfoPpiNotifyCallback (
  IN EFI_PEI_SERVICES              **PeiServices,
  IN EFI_PEI_NOTIFY_DESCRIPTOR     *NotifyDescriptor,
  IN VOID                          *Ppi
  )
/*++

Routine Description:

  Process Firmware Volum Information once FvInfoPPI install.

Arguments:

  PeiServices - General purpose services available to every PEIM.
    
Returns:

  Status -  EFI_SUCCESS if the interface could be successfully
            installed

--*/
{
  UINT8                                 FvCount;
  EFI_PEI_FIRMWARE_VOLUME_INFO_PPI      *Fv;
  PEI_CORE_INSTANCE                     *PrivateData;
  
  PrivateData = PEI_CORE_INSTANCE_FROM_PS_THIS (PeiServices);

  if (PrivateData->FvCount >= PEI_CORE_MAX_FV_SUPPORTED) {
    PEI_ASSERT (GetPeiServicesTablePointer(),FALSE);
  }

  Fv = (EFI_PEI_FIRMWARE_VOLUME_INFO_PPI *)Ppi;

   if (CompareGuid (&Fv->FvFormat, &gEfiFirmwareFileSystem2Guid)) {
     for (FvCount = 0; FvCount < PrivateData->FvCount; FvCount ++) {
       if ((UINTN)PrivateData->Fv[FvCount].FvHeader == (UINTN)Fv->FvInfo) {
         return EFI_SUCCESS;
       }
     }
    PrivateData->Fv[PrivateData->FvCount++].FvHeader = (EFI_FIRMWARE_VOLUME_HEADER*)Fv->FvInfo;
  }

  //
  // Allways add to the All list
  //
  PrivateData->AllFv[PrivateData->AllFvCount++] = (EFI_PEI_FV_HANDLE)Fv->FvInfo;

  return EFI_SUCCESS;
}


EFI_STATUS
EFIAPI 
PeiFfsFindNextVolume (
  IN CONST EFI_PEI_SERVICES   **PeiServices,
  IN UINTN                    Instance,
  OUT EFI_PEI_FV_HANDLE       *VolumeHandle
  )
/*++

Routine Description:

  Search the next FV Volume.

Arguments:
  PeiServices  - Pointer to the PEI Core Services Table.
  Instance     - The Fv Volume Instance.
  VolumeHandle - Pointer to the current Fv Volume to search.

Returns:
  EFI_INVALID_PARAMETER  - VolumeHandle is NULL.
  EFI_NOT_FOUND          - No FV Volume is found.
  EFI_SUCCESS            - The next FV Volume is found.
  
--*/
  
{
  PEI_CORE_INSTANCE   *Private;

  Private = PEI_CORE_INSTANCE_FROM_PS_THIS (PeiServices);
  if (VolumeHandle == NULL) {
    return EFI_INVALID_PARAMETER;
  } 

  if (Instance >= Private->AllFvCount) {
    VolumeHandle = NULL;
    return EFI_NOT_FOUND;
  }

  *VolumeHandle = Private->AllFv[Instance];
  return EFI_SUCCESS;
}


EFI_STATUS
EFIAPI 
PeiFfsFindNextFile (
  IN CONST  EFI_PEI_SERVICES        **PeiServices,
  IN EFI_FV_FILETYPE                SearchType,
  IN CONST EFI_PEI_FV_HANDLE        FvHandle,
  IN OUT EFI_PEI_FILE_HANDLE        *FileHandle
  )
/*++

Routine Description:

  Given the input FvHandle, search for the next matching type file in the FV Volume.

Arguments:
  PeiServices  - Pointer to the PEI Core Services Table.
  SearchType   - Filter to find only file of this type.
  FvHandle     - Pointer to the current FV to search.
  FileHandle   - Pointer to the file matching SearchType in FwVolHeader.
                - NULL if file not found
Returns:
  EFI_STATUS
  
--*/
{
  return PeiFindFileEx (FvHandle, NULL, SearchType, FileHandle, NULL);
}

EFI_STATUS
PeiFfsProcessSection (
  IN CONST EFI_PEI_SERVICES     **PeiServices,
  IN EFI_SECTION_TYPE           SectionType,
  IN EFI_COMMON_SECTION_HEADER  *Section,
  IN UINTN                      SectionSize,
  OUT VOID                      **OutputBuffer,
  OUT UINTN                     *OutputSize,
  OUT UINT32                    *Authentication
  )
/*++

Routine Description:

  Go through the file to search SectionType section,
  when meeting an encapsuled section, search recursively. 
  
Arguments:
  PeiServices  - Pointer to the PEI Core Services Table.
  SearchType   - Filter to find only section of this type.
  Section      - From where to search.
  SectionSize  - The file size to search.
  OutputBuffer - Pointer to the section to search.
  OutputSize   - The size of the section to search.
  Authentication -  Authenticate the section.

Returns:
  EFI_STATUS
  
--*/
{
  EFI_STATUS                              Status;
  UINT32                                  SectionLength;
  UINT32                                  ParsedLength;
  EFI_GUID_DEFINED_SECTION                *GuidSection; 
  EFI_PEI_GUIDED_SECTION_EXTRACTION_PPI   *GuidSectionPpi;
  EFI_COMPRESSION_SECTION                 *CompressionSection;
  EFI_PEI_DECOMPRESS_PPI                  *DecompressPpi;
  VOID                                    *PpiOutput;
  UINTN                                   PpiOutputSize;

  *OutputBuffer = NULL;
  ParsedLength = 0;
  while (ParsedLength < SectionSize) {
    if (Section->Type == SectionType) {
      *OutputBuffer = (VOID *)(Section + 1);
      return EFI_SUCCESS;
    } else if (Section->Type == EFI_SECTION_GUID_DEFINED) {
      GuidSection = (EFI_GUID_DEFINED_SECTION *)Section;
      GuidSectionPpi = PeiReturnPpi (PeiServices, &GuidSection->SectionDefinitionGuid);
      if (GuidSectionPpi != NULL) {
        Status = GuidSectionPpi->ExtractSection (
                                  GuidSectionPpi,
                                  Section,
                                  &PpiOutput,
                                  &PpiOutputSize,
                                  Authentication
                                  );
        if (!EFI_ERROR (Status)) {
          return PeiFfsProcessSection (
                  PeiServices,
                  SectionType, 
                  PpiOutput, 
                  PpiOutputSize, 
                  OutputBuffer, 
                  OutputSize, 
                  Authentication
                  );
        }
      }
    } else if (Section->Type == EFI_SECTION_COMPRESSION) {
      CompressionSection = (EFI_COMPRESSION_SECTION *)Section;
      DecompressPpi = PeiReturnPpi (PeiServices, &gEfiPeiDecompressPpiGuid);
      if (DecompressPpi != NULL) {
        Status = DecompressPpi->Decompress (
                                  DecompressPpi,
                                  CompressionSection,
                                  &PpiOutput,
                                  &PpiOutputSize
                                  );
        if (!EFI_ERROR (Status)) {
          return PeiFfsProcessSection (
                  PeiServices, SectionType, PpiOutput, PpiOutputSize, OutputBuffer, OutputSize, Authentication
                  );
        }
      }
    }

    //
    // Size is 24 bits wide so mask upper 8 bits. 
    // SectionLength is adjusted it is 4 byte aligned.
    // Go to the next section
    //
    SectionLength = *(UINT32 *)Section->Size & 0x00FFFFFF;
    SectionLength = GET_OCCUPIED_SIZE (SectionLength, 4);
    PEI_ASSERT (PeiServices, (SectionLength != 0));
    ParsedLength += SectionLength;
    Section = (EFI_COMMON_SECTION_HEADER *)((UINT8 *)Section + SectionLength);
  }
  
  return EFI_NOT_FOUND;
}


EFI_STATUS
EFIAPI 
PeiFfsFindSectionData (
  IN CONST  EFI_PEI_SERVICES    **PeiServices,
  IN EFI_SECTION_TYPE           SectionType,
  IN EFI_PEI_FILE_HANDLE        FileHandle,
  OUT VOID                      **SectionData
  )
/*++

Routine Description:
    Given the input file pointer, search for the next matching section in the
    FFS volume.

Arguments:
    PeiServices     - Pointer to the PEI Core Services Table.
    SearchType      - Filter to find only sections of this type.
    FileHandle      - Pointer to the current file to search.
    SectionData     - Pointer to the Section matching SectionType in FfsFileHeader.
                    - NULL if section not found

Returns:
    EFI_NOT_FOUND - No files matching the search criteria were found
    EFI_SUCCESS
  
--*/
{
  EFI_FFS_FILE_HEADER                     *FfsFileHeader;
  UINT32                                  FileSize;
  EFI_COMMON_SECTION_HEADER               *Section;
  UINTN                                   OutputSize;
  UINT32                                  AuthenticationStatus;


  FfsFileHeader = (EFI_FFS_FILE_HEADER *)FileHandle;

  //
  // Size is 24 bits wide so mask upper 8 bits. 
  // Does not include FfsFileHeader header size
  // FileSize is adjusted to FileOccupiedSize as it is 8 byte aligned.
  //
  Section = (EFI_COMMON_SECTION_HEADER *)(FfsFileHeader + 1);
  FileSize = *(UINT32 *)(FfsFileHeader->Size) & 0x00FFFFFF;
  FileSize -= sizeof (EFI_FFS_FILE_HEADER);

  return PeiFfsProcessSection (
          PeiServices, 
          SectionType, 
          Section, 
          FileSize, 
          SectionData, 
          &OutputSize, 
          &AuthenticationStatus
          );
}


EFI_STATUS
EFIAPI 
PeiFfsFindFileByName (
  IN  CONST EFI_GUID        *FileName,
  IN  EFI_PEI_FV_HANDLE     VolumeHandle,
  OUT EFI_PEI_FILE_HANDLE   *FileHandle
  )
/*++

Routine Description:

  Given the input VolumeHandle, search for the next matching name file.

Arguments:

  FileName      - File name to search.
  VolumeHandle  - The current FV to search.
  FileHandle    - Pointer to the file matching name in VolumeHandle.
                - NULL if file not found
Returns:
  EFI_STATUS
  
--*/  
{
  EFI_STATUS  Status;
  if ((VolumeHandle == NULL) || (FileName == NULL) || (FileHandle == NULL)) {
    return EFI_INVALID_PARAMETER;
  }
  Status = PeiFindFileEx (VolumeHandle, FileName, 0, FileHandle, NULL);
  if (Status == EFI_NOT_FOUND) {
    *FileHandle = NULL;
  }
  return Status;
}


EFI_STATUS
EFIAPI 
PeiFfsGetFileInfo (
  IN EFI_PEI_FILE_HANDLE  FileHandle,
  OUT EFI_FV_FILE_INFO    *FileInfo
  )
/*++

Routine Description:

  Collect information of given file.

Arguments:
  FileHandle   - The handle to file.
  FileInfo     - Pointer to the file information.

Returns:
  EFI_STATUS
  
--*/    
{
  UINT8                       FileState;
  UINT8                       ErasePolarity;
  EFI_FFS_FILE_HEADER         *FileHeader;
  EFI_PEI_FV_HANDLE           VolumeHandle;

  if ((FileHandle == NULL) || (FileInfo == NULL)) {
    return EFI_INVALID_PARAMETER;
  }

  //
  // Retrieve the FirmwareVolume which the file resides in.
  //
  if (!PeiFileHandleToVolume(FileHandle, &VolumeHandle)) {
    return EFI_INVALID_PARAMETER;
  }

  if (((EFI_FIRMWARE_VOLUME_HEADER*)VolumeHandle)->Attributes & EFI_FVB_ERASE_POLARITY) {
    ErasePolarity = 1;
  } else {
    ErasePolarity = 0;
  }

  //
  // Get FileState which is the highest bit of the State 
  //
  FileState = GetFileState (ErasePolarity, (EFI_FFS_FILE_HEADER*)FileHandle);

  switch (FileState) {
    case EFI_FILE_DATA_VALID:
    case EFI_FILE_MARKED_FOR_UPDATE:
      break;  
    default:
      return EFI_INVALID_PARAMETER;
    }

  FileHeader = (EFI_FFS_FILE_HEADER *)FileHandle;
  CopyMem (&FileInfo->FileName, &FileHeader->Name, sizeof(EFI_GUID));
  FileInfo->FileType = FileHeader->Type;
  FileInfo->FileAttributes = FileHeader->Attributes;
  FileInfo->BufferSize = ((*(UINT32 *)FileHeader->Size) & 0x00FFFFFF) -  sizeof (EFI_FFS_FILE_HEADER);
  FileInfo->Buffer = (FileHeader + 1);
  return EFI_SUCCESS;
}


EFI_STATUS
EFIAPI 
PeiFfsGetVolumeInfo (
  IN EFI_PEI_FV_HANDLE  VolumeHandle,
  OUT EFI_FV_INFO       *VolumeInfo
  )
/*++

Routine Description:

  Collect information of given Fv Volume.

Arguments:
  VolumeHandle    - The handle to Fv Volume.
  VolumeInfo      - The pointer to volume information.
  
Returns:
  EFI_STATUS
  
--*/    
{
  EFI_FIRMWARE_VOLUME_HEADER             *FwVolHeader;
  EFI_FIRMWARE_VOLUME_EXT_HEADER         *FwVolExHeaderInfo;

  if (VolumeInfo == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  FwVolHeader = (EFI_FIRMWARE_VOLUME_HEADER *)VolumeHandle;
  VolumeInfo->FvAttributes = FwVolHeader->Attributes;
  VolumeInfo->FvStart = FwVolHeader;
  VolumeInfo->FvSize = FwVolHeader->FvLength;
  CopyMem (&VolumeInfo->FvFormat, &FwVolHeader->FileSystemGuid,sizeof(EFI_GUID));

  if (FwVolHeader->ExtHeaderOffset != 0) {
    FwVolExHeaderInfo = (EFI_FIRMWARE_VOLUME_EXT_HEADER*)(((UINT8 *)FwVolHeader) + FwVolHeader->ExtHeaderOffset);
    CopyMem (&VolumeInfo->FvName, &FwVolExHeaderInfo->FvName, sizeof(EFI_GUID));
  }
  return EFI_SUCCESS;
}

