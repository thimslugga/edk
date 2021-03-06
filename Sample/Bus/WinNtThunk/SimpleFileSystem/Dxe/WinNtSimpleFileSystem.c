/*++

Copyright (c) 2004 - 2008, Intel Corporation                                                         
All rights reserved. This program and the accompanying materials                          
are licensed and made available under the terms and conditions of the BSD License         
which accompanies this distribution.  The full text of the license may be found at        
http://opensource.org/licenses/bsd-license.php                                            
                                                                                          
THE PROGRAM IS DISTRIBUTED UNDER THE BSD LICENSE ON AN "AS IS" BASIS,                     
WITHOUT WARRANTIES OR REPRESENTATIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED.             

Module Name:

  WinNtSimpleFileSystem.c

Abstract:

  Produce Simple File System abstractions for directories on your PC using Win32 APIs.
  The configuration of what devices to mount or emulate comes from NT 
  environment variables. The variables must be visible to the Microsoft* 
  Developer Studio for them to work.

  * Other names and brands may be claimed as the property of others.

--*/

#include "WinNtSimpleFileSystem.h"

EFI_DRIVER_BINDING_PROTOCOL gWinNtSimpleFileSystemDriverBinding = {
  WinNtSimpleFileSystemDriverBindingSupported,
  WinNtSimpleFileSystemDriverBindingStart,
  WinNtSimpleFileSystemDriverBindingStop,
  0xa,
  NULL,
  NULL
};

EFI_DRIVER_ENTRY_POINT (InitializeWinNtSimpleFileSystem)

CHAR16 *
EfiStrChr (
  IN CHAR16   *Str,
  IN CHAR16   Chr
  )
/*++

Routine Description:

  Locate the first occurance of a character in a string.

Arguments:

  Str - Pointer to NULL terminated unicode string.
  Chr - Character to locate.

Returns:

  If Str is NULL, then NULL is returned.
  If Chr is not contained in Str, then NULL is returned.
  If Chr is contained in Str, then a pointer to the first occurance of Chr in Str is returned.

--*/
{
  if (Str == NULL) {
    return Str;
  }

  while (*Str != '\0' && *Str != Chr) {
    ++Str;
  }

  return (*Str == Chr) ? Str : NULL;
}

BOOLEAN
IsZero (
  IN VOID   *Buffer,
  IN UINTN  Length
  )
/*++

Routine Description:

  TODO: Add function description

Arguments:

  Buffer  - TODO: add argument description
  Length  - TODO: add argument description

Returns:

  TODO: add return values

--*/
{
  if (Buffer == NULL || Length == 0) {
    return FALSE;
  }

  if (*(UINT8 *) Buffer != 0) {
    return FALSE;
  }

  if (Length > 1) {
    if (!EfiCompareMem (Buffer, (UINT8 *) Buffer + 1, Length - 1)) {
      return FALSE;
    }
  }

  return TRUE;
}

VOID
CutPrefix (
  IN  CHAR16  *Str,
  IN  UINTN   Count
  )
/*++

Routine Description:

  TODO: Add function description

Arguments:

  Str   - TODO: add argument description
  Count - TODO: add argument description

Returns:

  TODO: add return values

--*/
{
  CHAR16  *Pointer;

  if (EfiStrLen (Str) < Count) {
    ASSERT (0);
  }

  if (Count != 0) {
    for (Pointer = Str; *(Pointer + Count); Pointer++) {
      *Pointer = *(Pointer + Count);
    }
    *Pointer = *(Pointer + Count);
  }
}

EFI_STATUS
EFIAPI
InitializeWinNtSimpleFileSystem (
  IN EFI_HANDLE           ImageHandle,
  IN EFI_SYSTEM_TABLE     *SystemTable
  )
/*++

Routine Description:

  TODO: Add function description

Arguments:

  ImageHandle - TODO: add argument description
  SystemTable - TODO: add argument description

Returns:

  TODO: add return values

--*/
{
  return INSTALL_ALL_DRIVER_PROTOCOLS_OR_PROTOCOLS2 (
          ImageHandle,
          SystemTable,
          &gWinNtSimpleFileSystemDriverBinding,
          ImageHandle,
          &gWinNtSimpleFileSystemComponentName,
          NULL,
          NULL
          );
}

EFI_STATUS
EFIAPI
WinNtSimpleFileSystemDriverBindingSupported (
  IN  EFI_DRIVER_BINDING_PROTOCOL  *This,
  IN  EFI_HANDLE                   ControllerHandle,
  IN  EFI_DEVICE_PATH_PROTOCOL     *RemainingDevicePath
  )
/*++

Routine Description:

  Check to see if the driver supports a given controller.

Arguments:

  This                - A pointer to an instance of the EFI_DRIVER_BINDING_PROTOCOL.

  ControllerHandle    - EFI handle of the controller to test.

  RemainingDevicePath - Pointer to remaining portion of a device path.

Returns:

  EFI_SUCCESS         - The device specified by ControllerHandle and RemainingDevicePath is supported by the driver
                        specified by This.

  EFI_ALREADY_STARTED - The device specified by ControllerHandle and RemainingDevicePath is already being managed by
                        the driver specified by This.

  EFI_ACCESS_DENIED   - The device specified by ControllerHandle and RemainingDevicePath is already being managed by
                        a different driver or an application that requires exclusive access.

  EFI_UNSUPPORTED     - The device specified by ControllerHandle and RemainingDevicePath is not supported by the
                        driver specified by This.

--*/
{
  EFI_STATUS              Status;
  EFI_WIN_NT_IO_PROTOCOL  *WinNtIo;

  //
  // Open the IO Abstraction(s) needed to perform the supported test
  //
  Status = gBS->OpenProtocol (
                  ControllerHandle,
                  &gEfiWinNtIoProtocolGuid,
                  &WinNtIo,
                  This->DriverBindingHandle,
                  ControllerHandle,
                  EFI_OPEN_PROTOCOL_BY_DRIVER
                  );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  //
  // Make sure GUID is for a File System handle.
  //
  Status = EFI_UNSUPPORTED;
  if (EfiCompareGuid (WinNtIo->TypeGuid, &gEfiWinNtFileSystemGuid)) {
    Status = EFI_SUCCESS;
  }

  //
  // Close the I/O Abstraction(s) used to perform the supported test
  //
  gBS->CloseProtocol (
        ControllerHandle,
        &gEfiWinNtIoProtocolGuid,
        This->DriverBindingHandle,
        ControllerHandle
        );

  return Status;
}

EFI_STATUS
EFIAPI
WinNtSimpleFileSystemDriverBindingStart (
  IN  EFI_DRIVER_BINDING_PROTOCOL   *This,
  IN  EFI_HANDLE                    ControllerHandle,
  IN  EFI_DEVICE_PATH_PROTOCOL      *RemainingDevicePath
  )
/*++

Routine Description:

  Starts a device controller or a bus controller.

Arguments:

  This                - A pointer to an instance of the EFI_DRIVER_BINDING_PROTOCOL.

  ControllerHandle    - EFI handle of the controller to start.

  RemainingDevicePath - Pointer to remaining portion of a device path.

Returns:

  EFI_SUCCESS           - The device or bus controller has been started.

  EFI_DEVICE_ERROR      - The device could not be started due to a device failure.

  EFI_OUT_OF_RESOURCES  - The request could not be completed due to lack of resources.

--*/
{
  EFI_STATUS                        Status;
  EFI_WIN_NT_IO_PROTOCOL            *WinNtIo;
  WIN_NT_SIMPLE_FILE_SYSTEM_PRIVATE *Private;

  Private = NULL;

  //
  // Open the IO Abstraction(s) needed
  //
  Status = gBS->OpenProtocol (
                  ControllerHandle,
                  &gEfiWinNtIoProtocolGuid,
                  &WinNtIo,
                  This->DriverBindingHandle,
                  ControllerHandle,
                  EFI_OPEN_PROTOCOL_BY_DRIVER
                  );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  //
  // Validate GUID
  //
  if (!EfiCompareGuid (WinNtIo->TypeGuid, &gEfiWinNtFileSystemGuid)) {
    Status = EFI_UNSUPPORTED;
    goto Done;
  }

  Status = gBS->AllocatePool (
                  EfiBootServicesData,
                  sizeof (WIN_NT_SIMPLE_FILE_SYSTEM_PRIVATE),
                  &Private
                  );
  if (EFI_ERROR (Status)) {
    goto Done;
  }

  Private->Signature  = WIN_NT_SIMPLE_FILE_SYSTEM_PRIVATE_SIGNATURE;
  Private->WinNtThunk = WinNtIo->WinNtThunk;

  Private->FilePath = WinNtIo->EnvString;

  Private->VolumeLabel      = NULL;
  Status = gBS->AllocatePool (
                  EfiBootServicesData,
                  EfiStrSize (L"EFI_EMULATED"),
                  &Private->VolumeLabel
                  );

  if (EFI_ERROR (Status)) {
    goto Done;
  }

  EfiStrCpy (Private->VolumeLabel, L"EFI_EMULATED");

  Private->SimpleFileSystem.Revision    = EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_REVISION;
  Private->SimpleFileSystem.OpenVolume  = WinNtSimpleFileSystemOpenVolume;

  Private->WinNtThunk->SetErrorMode (SEM_FAILCRITICALERRORS);

  Private->ControllerNameTable = NULL;

  EfiLibAddUnicodeString (
    LANGUAGE_CODE_ENGLISH,
    gWinNtSimpleFileSystemComponentName.SupportedLanguages,
    &Private->ControllerNameTable,
    WinNtIo->EnvString
    );

  Status = gBS->InstallMultipleProtocolInterfaces (
                  &ControllerHandle,
                  &gEfiSimpleFileSystemProtocolGuid,
                  &Private->SimpleFileSystem,
                  NULL
                  );

Done:
  if (EFI_ERROR (Status)) {

    if (Private != NULL) {

      EfiLibFreeUnicodeStringTable (Private->ControllerNameTable);

      gBS->FreePool (Private);
    }

    gBS->CloseProtocol (
          ControllerHandle,
          &gEfiWinNtIoProtocolGuid,
          This->DriverBindingHandle,
          ControllerHandle
          );
  }

  return Status;
}

EFI_STATUS
EFIAPI
WinNtSimpleFileSystemDriverBindingStop (
  IN  EFI_DRIVER_BINDING_PROTOCOL  *This,
  IN  EFI_HANDLE                   ControllerHandle,
  IN  UINTN                        NumberOfChildren,
  IN  EFI_HANDLE                   *ChildHandleBuffer
  )
/*++

Routine Description:

  TODO: Add function description

Arguments:

  This              - A pointer to an instance of the EFI_DRIVER_BINDING_PROTOCOL.

  ControllerHandle  - A handle to the device to be stopped.

  NumberOfChildren  - The number of child device handles in ChildHandleBuffer.

  ChildHandleBuffer - An array of child device handles to be freed.

Returns:

  EFI_SUCCESS       - The device has been stopped.

  EFI_DEVICE_ERROR  - The device could not be stopped due to a device failure.

--*/
// TODO:    EFI_UNSUPPORTED - add return value to function comment
{
  EFI_STATUS                        Status;
  EFI_SIMPLE_FILE_SYSTEM_PROTOCOL   *SimpleFileSystem;
  WIN_NT_SIMPLE_FILE_SYSTEM_PRIVATE *Private;

  //
  // Get our context back
  //
  Status = gBS->OpenProtocol (
                  ControllerHandle,
                  &gEfiSimpleFileSystemProtocolGuid,
                  &SimpleFileSystem,
                  This->DriverBindingHandle,
                  ControllerHandle,
                  EFI_OPEN_PROTOCOL_GET_PROTOCOL
                  );
  if (EFI_ERROR (Status)) {
    return EFI_UNSUPPORTED;
  }

  Private = WIN_NT_SIMPLE_FILE_SYSTEM_PRIVATE_DATA_FROM_THIS (SimpleFileSystem);

  //
  // Uninstall the Simple File System Protocol from ControllerHandle
  //
  Status = gBS->UninstallMultipleProtocolInterfaces (
                  ControllerHandle,
                  &gEfiSimpleFileSystemProtocolGuid,
                  &Private->SimpleFileSystem,
                  NULL
                  );
  if (!EFI_ERROR (Status)) {
    Status = gBS->CloseProtocol (
                    ControllerHandle,
                    &gEfiWinNtIoProtocolGuid,
                    This->DriverBindingHandle,
                    ControllerHandle
                    );
  }

  if (!EFI_ERROR (Status)) {
    //
    // Free our instance data
    //
    EfiLibFreeUnicodeStringTable (Private->ControllerNameTable);

    gBS->FreePool (Private);
  }

  return Status;
}

EFI_STATUS
EFIAPI
WinNtSimpleFileSystemOpenVolume (
  IN EFI_SIMPLE_FILE_SYSTEM_PROTOCOL  *This,
  OUT EFI_FILE                        **Root
  )
/*++

Routine Description:

  Open the root directory on a volume.

Arguments:

  This  - A pointer to the volume to open.

  Root  - A pointer to storage for the returned opened file handle of the root directory.

Returns:

  EFI_SUCCESS           - The volume was opened.

  EFI_UNSUPPORTED       - The volume does not support the requested file system type.

  EFI_NO_MEDIA          - The device has no media.

  EFI_DEVICE_ERROR      - The device reported an error.

  EFI_VOLUME_CORRUPTED  - The file system structures are corrupted.

  EFI_ACCESS_DENIED     - The service denied access to the file.

  EFI_OUT_OF_RESOURCES  - The file volume could not be opened due to lack of resources.

  EFI_MEDIA_CHANGED     - The device has new media or the media is no longer supported.

--*/
// TODO:    EFI_INVALID_PARAMETER - add return value to function comment
{
  EFI_STATUS                        Status;
  WIN_NT_SIMPLE_FILE_SYSTEM_PRIVATE *Private;
  WIN_NT_EFI_FILE_PRIVATE           *PrivateFile;
  CHAR16                            *TempFileName;

  if (This == NULL || Root == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  Private     = WIN_NT_SIMPLE_FILE_SYSTEM_PRIVATE_DATA_FROM_THIS (This);

  PrivateFile = NULL;
  Status = gBS->AllocatePool (
                  EfiBootServicesData,
                  sizeof (WIN_NT_EFI_FILE_PRIVATE),
                  &PrivateFile
                  );
  if (EFI_ERROR (Status)) {
    goto Done;
  }

  PrivateFile->FileName = NULL;
  Status = gBS->AllocatePool (
                  EfiBootServicesData,
                  EfiStrSize (Private->FilePath),
                  &PrivateFile->FileName
                  );
  if (EFI_ERROR (Status)) {
    goto Done;
  }

  PrivateFile->FilePath = NULL;
  Status = gBS->AllocatePool (
                  EfiBootServicesData,
                  EfiStrSize (Private->FilePath),
                  &PrivateFile->FilePath
                  );
  if (EFI_ERROR (Status)) {
    goto Done;
  }

  EfiStrCpy (PrivateFile->FilePath, Private->FilePath);
  EfiStrCpy (PrivateFile->FileName, PrivateFile->FilePath);
  PrivateFile->Signature            = WIN_NT_EFI_FILE_PRIVATE_SIGNATURE;
  PrivateFile->WinNtThunk           = Private->WinNtThunk;
  PrivateFile->SimpleFileSystem     = This;
  PrivateFile->IsRootDirectory      = TRUE;
  PrivateFile->IsDirectoryPath      = TRUE;
  PrivateFile->IsOpenedByRead       = TRUE;
  PrivateFile->EfiFile.Revision     = EFI_FILE_HANDLE_REVISION;
  PrivateFile->EfiFile.Open         = WinNtSimpleFileSystemOpen;
  PrivateFile->EfiFile.Close        = WinNtSimpleFileSystemClose;
  PrivateFile->EfiFile.Delete       = WinNtSimpleFileSystemDelete;
  PrivateFile->EfiFile.Read         = WinNtSimpleFileSystemRead;
  PrivateFile->EfiFile.Write        = WinNtSimpleFileSystemWrite;
  PrivateFile->EfiFile.GetPosition  = WinNtSimpleFileSystemGetPosition;
  PrivateFile->EfiFile.SetPosition  = WinNtSimpleFileSystemSetPosition;
  PrivateFile->EfiFile.GetInfo      = WinNtSimpleFileSystemGetInfo;
  PrivateFile->EfiFile.SetInfo      = WinNtSimpleFileSystemSetInfo;
  PrivateFile->EfiFile.Flush        = WinNtSimpleFileSystemFlush;

  //
  // Set DirHandle
  //
  PrivateFile->DirHandle = PrivateFile->WinNtThunk->CreateFile (
                                                      PrivateFile->FilePath,
                                                      GENERIC_READ,
                                                      FILE_SHARE_READ | FILE_SHARE_WRITE,
                                                      NULL,
                                                      OPEN_EXISTING,
                                                      FILE_FLAG_BACKUP_SEMANTICS,
                                                      NULL
                                                      );

  if (PrivateFile->DirHandle == INVALID_HANDLE_VALUE) {
    Status = EFI_NOT_FOUND;
    goto Done;
  }

  //
  // Find the first file under it
  //
  Status = gBS->AllocatePool (
                  EfiBootServicesData,
                  EfiStrSize (PrivateFile->FilePath) + EfiStrSize (L"\\*"),
                  &TempFileName
                  );
  if (EFI_ERROR (Status)) {
    goto Done;
  }
  EfiStrCpy (TempFileName, PrivateFile->FilePath);
  EfiStrCat (TempFileName, L"\\*");

  PrivateFile->LHandle = PrivateFile->WinNtThunk->FindFirstFile (TempFileName, &PrivateFile->FindBuf);

  if (PrivateFile->LHandle == INVALID_HANDLE_VALUE) {
    PrivateFile->IsValidFindBuf = FALSE;
  } else {
    PrivateFile->IsValidFindBuf = TRUE;
  }

  *Root = &PrivateFile->EfiFile;

  Status = EFI_SUCCESS;

Done:
  if (EFI_ERROR (Status)) {
    if (PrivateFile) {
      if (PrivateFile->FileName) {
        gBS->FreePool (PrivateFile->FileName);
      }

      if (PrivateFile->FilePath) {
        gBS->FreePool (PrivateFile->FilePath);
      }

      gBS->FreePool (PrivateFile);
    }
  }

  return Status;
}

EFI_STATUS
EFIAPI
WinNtSimpleFileSystemOpen (
  IN  EFI_FILE  *This,
  OUT EFI_FILE  **NewHandle,
  IN  CHAR16    *FileName,
  IN  UINT64    OpenMode,
  IN  UINT64    Attributes
  )
/*++

Routine Description:

  Open a file relative to the source file location.

Arguments:

  This        - A pointer to the source file location.

  NewHandle   - Pointer to storage for the new file handle.

  FileName    - Pointer to the file name to be opened.

  OpenMode    - File open mode information.

  Attributes  - File creation attributes.

Returns:

  EFI_SUCCESS           - The file was opened.

  EFI_NOT_FOUND         - The file could not be found in the volume.

  EFI_NO_MEDIA          - The device has no media.

  EFI_MEDIA_CHANGED     - The device has new media or the media is no longer supported.

  EFI_DEVICE_ERROR      - The device reported an error.

  EFI_VOLUME_CORRUPTED  - The file system structures are corrupted.

  EFI_WRITE_PROTECTED   - The volume or file is write protected.

  EFI_ACCESS_DENIED     - The service denied access to the file.

  EFI_OUT_OF_RESOURCES  - Not enough resources were available to open the file.

  EFI_VOLUME_FULL       - There is not enough space left to create the new file.

--*/
// TODO:    EFI_INVALID_PARAMETER - add return value to function comment
// TODO:    EFI_INVALID_PARAMETER - add return value to function comment
// TODO:    EFI_INVALID_PARAMETER - add return value to function comment
// TODO:    EFI_INVALID_PARAMETER - add return value to function comment
{
  WIN_NT_EFI_FILE_PRIVATE           *PrivateFile;
  WIN_NT_EFI_FILE_PRIVATE           *NewPrivateFile;
  WIN_NT_SIMPLE_FILE_SYSTEM_PRIVATE *PrivateRoot;
  EFI_STATUS                        Status;
  CHAR16                            *RealFileName;
  CHAR16                            *TempFileName;
  CHAR16                            *ParseFileName;
  CHAR16                            *GuardPointer;
  CHAR16                            TempChar;
  DWORD                             LastError;
  UINTN                             Count;
  BOOLEAN                           LoopFinish;
  UINTN                             InfoSize;
  EFI_FILE_INFO                     *Info;

  //
  // Check for obvious invalid parameters.
  //
  if (This == NULL || NewHandle == NULL || FileName == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  switch (OpenMode) {
  case EFI_FILE_MODE_CREATE | EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE:
    if (Attributes &~EFI_FILE_VALID_ATTR) {
      return EFI_INVALID_PARAMETER;
    }

    if (Attributes & EFI_FILE_READ_ONLY) {
      return EFI_INVALID_PARAMETER;
    }

  //
  // fall through
  //
  case EFI_FILE_MODE_READ:
  case EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE:
    break;

  default:
    return EFI_INVALID_PARAMETER;
  }

  //
  // Init local variables
  //
  PrivateFile     = WIN_NT_EFI_FILE_PRIVATE_DATA_FROM_THIS (This);
  PrivateRoot     = WIN_NT_SIMPLE_FILE_SYSTEM_PRIVATE_DATA_FROM_THIS (PrivateFile->SimpleFileSystem);
  NewPrivateFile  = NULL;

  //
  // Allocate buffer for FileName as the passed in FileName may be read only
  //
  Status = gBS->AllocatePool (
                  EfiBootServicesData,
                  EfiStrSize (FileName),
                  &TempFileName
                  );
  if (EFI_ERROR (Status)) {
    return  Status;
  }
  EfiStrCpy (TempFileName, FileName);
  FileName = TempFileName;
 
  if (FileName[EfiStrLen (FileName) - 1] == L'\\') {
    FileName[EfiStrLen (FileName) - 1]  = 0;
  }
  //
  // If file name does not equal to "." or ".." and not trailed with "\..",
  // then we trim the leading/trailing blanks and trailing dots
  //
  if (EfiStrCmp (FileName, L".") != 0 && EfiStrCmp (FileName, L"..") != 0 &&
	  ((EfiStrLen (FileName) >= 3) ? (EfiStrCmp (&FileName[EfiStrLen (FileName) - 3], L"\\..") != 0) : TRUE)) {
    //
    // Trim leading blanks
    //
    Count = 0;
    for (TempFileName = FileName;
      *TempFileName != 0 && *TempFileName == L' ';
      TempFileName++) {
      Count++;
    }
    CutPrefix (FileName, Count);
    //
    // Trim trailing dots and blanks
    //
    for (TempFileName = FileName + EfiStrLen (FileName) - 1; 
      TempFileName >= FileName && (*TempFileName == L' ' || *TempFileName == L'.');
      TempFileName--) {
      ;
    }
    *(TempFileName + 1) = 0;
  }
  //
  // Attempt to open the file
  //
  Status = gBS->AllocatePool (
                  EfiBootServicesData,
                  sizeof (WIN_NT_EFI_FILE_PRIVATE),
                  &NewPrivateFile
                  );

  if (EFI_ERROR (Status)) {
    goto Done;
  }

  EfiCopyMem (NewPrivateFile, PrivateFile, sizeof (WIN_NT_EFI_FILE_PRIVATE));

  NewPrivateFile->FilePath = NULL;

  Status = gBS->AllocatePool (
                  EfiBootServicesData,
                  EfiStrSize (PrivateFile->FileName),
                  &NewPrivateFile->FilePath
                  );

  if (EFI_ERROR (Status)) {
    goto Done;
  }

  if (PrivateFile->IsDirectoryPath) {
    EfiStrCpy (NewPrivateFile->FilePath, PrivateFile->FileName);
  } else {
    EfiStrCpy (NewPrivateFile->FilePath, PrivateFile->FilePath);
  }

  NewPrivateFile->FileName = NULL;
  Status = gBS->AllocatePool (
                  EfiBootServicesData,
                  EfiStrSize (NewPrivateFile->FilePath) + EfiStrSize (L"\\") + EfiStrSize (FileName),
                  &NewPrivateFile->FileName
                  );

  if (EFI_ERROR (Status)) {
    goto Done;
  }

  if (*FileName == L'\\') {
    EfiStrCpy (NewPrivateFile->FileName, PrivateRoot->FilePath);
    EfiStrCat (NewPrivateFile->FileName, L"\\");
    EfiStrCat (NewPrivateFile->FileName, FileName + 1);
  } else {
    EfiStrCpy (NewPrivateFile->FileName, NewPrivateFile->FilePath);
    if (EfiStrCmp (FileName, L"") != 0) {
      //
      // In case the filename becomes empty, especially after trimming dots and blanks
      //
      EfiStrCat (NewPrivateFile->FileName, L"\\");
      EfiStrCat (NewPrivateFile->FileName, FileName);
    }
  }

  //
  // GuardPointer protect simplefilesystem root path not be destroyed
  //
  GuardPointer  = NewPrivateFile->FileName + EfiStrLen (PrivateRoot->FilePath);

  LoopFinish    = FALSE;

  while (!LoopFinish) {

    LoopFinish = TRUE;

    for (ParseFileName = GuardPointer; *ParseFileName; ParseFileName++) {
      if (*ParseFileName == L'.' &&
          (*(ParseFileName + 1) == 0 || *(ParseFileName + 1) == L'\\') &&
          *(ParseFileName - 1) == L'\\'
          ) {

        //
        // cut \.
        //
        CutPrefix (ParseFileName - 1, 2);
        LoopFinish = FALSE;
        break;
      }

      if (*ParseFileName == L'.' &&
          *(ParseFileName + 1) == L'.' &&
          (*(ParseFileName + 2) == 0 || *(ParseFileName + 2) == L'\\') &&
          *(ParseFileName - 1) == L'\\'
          ) {

        ParseFileName--;
        Count = 3;

        while (ParseFileName != GuardPointer) {
          ParseFileName--;
          Count++;
          if (*ParseFileName == L'\\') {
            break;
          }
        }

        //
        // cut \.. and its left directory
        //
        CutPrefix (ParseFileName, Count);
        LoopFinish = FALSE;
        break;
      }
    }
  }

  RealFileName = NewPrivateFile->FileName;
  while (EfiStrChr (RealFileName, L'\\') != NULL) {
    RealFileName = EfiStrChr (RealFileName, L'\\') + 1;
  }

  TempChar = 0;
  if (RealFileName != NewPrivateFile->FileName) {
    TempChar            = *(RealFileName - 1);
    *(RealFileName - 1) = 0;
  }

  gBS->FreePool (NewPrivateFile->FilePath);
  NewPrivateFile->FilePath = NULL;
  Status = gBS->AllocatePool (
                  EfiBootServicesData,
                  EfiStrSize (NewPrivateFile->FileName),
                  &NewPrivateFile->FilePath
                  );

  if (EFI_ERROR (Status)) {
    goto Done;
  }

  EfiStrCpy (NewPrivateFile->FilePath, NewPrivateFile->FileName);

  if (TempChar != 0) {
    *(RealFileName - 1)             = TempChar;
  }

  NewPrivateFile->IsRootDirectory = FALSE;

  //
  // Test whether file or directory
  //
  if (OpenMode & EFI_FILE_MODE_CREATE) {
    if (Attributes & EFI_FILE_DIRECTORY) {
      NewPrivateFile->IsDirectoryPath = TRUE;
    } else {
      NewPrivateFile->IsDirectoryPath = FALSE;
    }
  } else {
    NewPrivateFile->LHandle = INVALID_HANDLE_VALUE;
    NewPrivateFile->LHandle = NewPrivateFile->WinNtThunk->CreateFile (
                                                            NewPrivateFile->FileName,
                                                            GENERIC_READ,
                                                            FILE_SHARE_READ | FILE_SHARE_WRITE,
                                                            NULL,
                                                            OPEN_EXISTING,
                                                            0,
                                                            NULL
                                                            );

    if (NewPrivateFile->LHandle != INVALID_HANDLE_VALUE) {
      NewPrivateFile->IsDirectoryPath = FALSE;
      NewPrivateFile->WinNtThunk->CloseHandle (NewPrivateFile->LHandle);
    } else {
      NewPrivateFile->IsDirectoryPath = TRUE;
    }

    NewPrivateFile->LHandle = INVALID_HANDLE_VALUE;
  }

  if (OpenMode & EFI_FILE_MODE_WRITE) {
    NewPrivateFile->IsOpenedByRead = FALSE;
  } else {
    NewPrivateFile->IsOpenedByRead = TRUE;
  }

  Status = EFI_SUCCESS;

  //
  // deal with directory
  //
  if (NewPrivateFile->IsDirectoryPath) {

    Status = gBS->AllocatePool (
                    EfiBootServicesData,
                    EfiStrSize (NewPrivateFile->FileName) + EfiStrSize (L"\\*"),
                    &TempFileName
                    );

    if (EFI_ERROR (Status)) {
      goto Done;
    }

    EfiStrCpy (TempFileName, NewPrivateFile->FileName);

    if ((OpenMode & EFI_FILE_MODE_CREATE)) {
      //
      // Create a directory
      //
      if (!NewPrivateFile->WinNtThunk->CreateDirectory (TempFileName, NULL)) {

        LastError = PrivateFile->WinNtThunk->GetLastError ();
        if (LastError != ERROR_ALREADY_EXISTS) {
          gBS->FreePool (TempFileName);
          Status = EFI_ACCESS_DENIED;
          goto Done;
        }
      }
    }

    NewPrivateFile->DirHandle = NewPrivateFile->WinNtThunk->CreateFile (
                                                              TempFileName,
                                                              NewPrivateFile->IsOpenedByRead ? GENERIC_READ : (GENERIC_READ | GENERIC_WRITE),
                                                              FILE_SHARE_READ | FILE_SHARE_WRITE,
                                                              NULL,
                                                              OPEN_EXISTING,
                                                              FILE_FLAG_BACKUP_SEMANTICS,
                                                              NULL
                                                              );

    if (NewPrivateFile->DirHandle == INVALID_HANDLE_VALUE) {

      NewPrivateFile->DirHandle = NewPrivateFile->WinNtThunk->CreateFile (
                                                                TempFileName,
                                                                GENERIC_READ,
                                                                FILE_SHARE_READ | FILE_SHARE_WRITE,
                                                                NULL,
                                                                OPEN_EXISTING,
                                                                FILE_FLAG_BACKUP_SEMANTICS,
                                                                NULL
                                                                );

      if (NewPrivateFile->DirHandle != INVALID_HANDLE_VALUE) {
        NewPrivateFile->WinNtThunk->CloseHandle (NewPrivateFile->DirHandle);
        NewPrivateFile->DirHandle = INVALID_HANDLE_VALUE;
        Status                    = EFI_ACCESS_DENIED;
      } else {
        Status = EFI_NOT_FOUND;
      }

      goto Done;
    }

    //
    // Find the first file under it
    //
    EfiStrCat (TempFileName, L"\\*");
    NewPrivateFile->LHandle = NewPrivateFile->WinNtThunk->FindFirstFile (TempFileName, &NewPrivateFile->FindBuf);

    if (NewPrivateFile->LHandle == INVALID_HANDLE_VALUE) {
      NewPrivateFile->IsValidFindBuf = FALSE;
    } else {
      NewPrivateFile->IsValidFindBuf = TRUE;
    }
  } else {
    //
    // deal with file
    //
    if (!NewPrivateFile->IsOpenedByRead) {
      NewPrivateFile->LHandle = NewPrivateFile->WinNtThunk->CreateFile (
                                                              NewPrivateFile->FileName,
                                                              GENERIC_READ | GENERIC_WRITE,
                                                              FILE_SHARE_READ | FILE_SHARE_WRITE,
                                                              NULL,
                                                              (OpenMode & EFI_FILE_MODE_CREATE) ? OPEN_ALWAYS : OPEN_EXISTING,
                                                              0,
                                                              NULL
                                                              );

      if (NewPrivateFile->LHandle == INVALID_HANDLE_VALUE) {
        NewPrivateFile->LHandle = NewPrivateFile->WinNtThunk->CreateFile (
                                                                NewPrivateFile->FileName,
                                                                GENERIC_READ,
                                                                FILE_SHARE_READ | FILE_SHARE_WRITE,
                                                                NULL,
                                                                OPEN_EXISTING,
                                                                0,
                                                                NULL
                                                                );

        if (NewPrivateFile->LHandle == INVALID_HANDLE_VALUE) {
          Status = EFI_NOT_FOUND;
        } else {
          Status = EFI_ACCESS_DENIED;
          NewPrivateFile->WinNtThunk->CloseHandle (NewPrivateFile->LHandle);
          NewPrivateFile->LHandle = INVALID_HANDLE_VALUE;
        }
      }
    } else {
      NewPrivateFile->LHandle = NewPrivateFile->WinNtThunk->CreateFile (
                                                              NewPrivateFile->FileName,
                                                              GENERIC_READ,
                                                              FILE_SHARE_READ | FILE_SHARE_WRITE,
                                                              NULL,
                                                              OPEN_EXISTING,
                                                              0,
                                                              NULL
                                                              );

      if (NewPrivateFile->LHandle == INVALID_HANDLE_VALUE) {
        Status = EFI_NOT_FOUND;
      }
    }
  }

  if ((OpenMode & EFI_FILE_MODE_CREATE) && Status == EFI_SUCCESS) {
    //
    // Set the attribute
    //
    InfoSize  = 0;
    Info      = NULL;

    Status    = WinNtSimpleFileSystemGetInfo (&NewPrivateFile->EfiFile, &gEfiFileInfoGuid, &InfoSize, Info);

    if (Status != EFI_BUFFER_TOO_SMALL) {
      Status = EFI_DEVICE_ERROR;
      goto Done;
    }

    Status = gBS->AllocatePool (
                    EfiBootServicesData,
                    InfoSize,
                    &Info
                    );

    if (EFI_ERROR (Status)) {
      goto Done;
    }

    Status = WinNtSimpleFileSystemGetInfo (&NewPrivateFile->EfiFile, &gEfiFileInfoGuid, &InfoSize, Info);

    if (EFI_ERROR (Status)) {
      goto Done;
    }

    Info->Attribute = Attributes;

    WinNtSimpleFileSystemSetInfo (&NewPrivateFile->EfiFile, &gEfiFileInfoGuid, InfoSize, Info);
  }

Done: ;
  
  gBS->FreePool (FileName);

  if (EFI_ERROR (Status)) {
    if (NewPrivateFile) {
      if (NewPrivateFile->FileName) {
        gBS->FreePool (NewPrivateFile->FileName);
      }

      if (NewPrivateFile->FilePath) {
        gBS->FreePool (NewPrivateFile->FilePath);
      }

      gBS->FreePool (NewPrivateFile);
    }
  } else {
    *NewHandle = &NewPrivateFile->EfiFile;
    if (EfiStrCmp (NewPrivateFile->FileName, PrivateRoot->FilePath) == 0) {
      NewPrivateFile->IsRootDirectory = TRUE;
    }
  }

  return Status;
}

EFI_STATUS
EFIAPI
WinNtSimpleFileSystemClose (
  IN EFI_FILE  *This
  )
/*++

Routine Description:

  Close the specified file handle.

Arguments:

  This  - Pointer to a returned opened file handle.

Returns:

  EFI_SUCCESS - The file handle has been closed.

--*/
// TODO:    EFI_INVALID_PARAMETER - add return value to function comment
{
  WIN_NT_EFI_FILE_PRIVATE *PrivateFile;

  if (This == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  PrivateFile = WIN_NT_EFI_FILE_PRIVATE_DATA_FROM_THIS (This);

  if (PrivateFile->LHandle != INVALID_HANDLE_VALUE) {
    if (PrivateFile->IsDirectoryPath) {
      PrivateFile->WinNtThunk->FindClose (PrivateFile->LHandle);
    } else {
      PrivateFile->WinNtThunk->CloseHandle (PrivateFile->LHandle);
    }

    PrivateFile->LHandle = INVALID_HANDLE_VALUE;
  }

  if (PrivateFile->IsDirectoryPath && PrivateFile->DirHandle != INVALID_HANDLE_VALUE) {
    PrivateFile->WinNtThunk->CloseHandle (PrivateFile->DirHandle);
    PrivateFile->DirHandle = INVALID_HANDLE_VALUE;
  }

  if (PrivateFile->FileName) {
    gBS->FreePool (PrivateFile->FileName);
  }

  gBS->FreePool (PrivateFile);
  return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
WinNtSimpleFileSystemDelete (
  IN EFI_FILE  *This
  )
/*++

Routine Description:

  Close and delete a file.

Arguments:

  This  - Pointer to a returned opened file handle.

Returns:

  EFI_SUCCESS             - The file handle was closed and deleted.

  EFI_WARN_DELETE_FAILURE - The handle was closed but could not be deleted.

--*/
// TODO:    EFI_INVALID_PARAMETER - add return value to function comment
{
  EFI_STATUS              Status;
  WIN_NT_EFI_FILE_PRIVATE *PrivateFile;

  if (This == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  PrivateFile = WIN_NT_EFI_FILE_PRIVATE_DATA_FROM_THIS (This);

  Status      = EFI_WARN_DELETE_FAILURE;

  if (PrivateFile->IsDirectoryPath) {
    if (PrivateFile->LHandle != INVALID_HANDLE_VALUE) {
      PrivateFile->WinNtThunk->FindClose (PrivateFile->LHandle);
    }

    if (PrivateFile->DirHandle != INVALID_HANDLE_VALUE) {
      PrivateFile->WinNtThunk->CloseHandle (PrivateFile->DirHandle);
      PrivateFile->DirHandle = INVALID_HANDLE_VALUE;
    }

    if (PrivateFile->WinNtThunk->RemoveDirectory (PrivateFile->FileName)) {
      Status = EFI_SUCCESS;
    }
  } else {
    PrivateFile->WinNtThunk->CloseHandle (PrivateFile->LHandle);
    PrivateFile->LHandle = INVALID_HANDLE_VALUE;

    if (!PrivateFile->IsOpenedByRead) {
      if (PrivateFile->WinNtThunk->DeleteFile (PrivateFile->FileName)) {
        Status = EFI_SUCCESS;
      }
    }
  }

  gBS->FreePool (PrivateFile->FileName);
  gBS->FreePool (PrivateFile);

  return Status;
}

STATIC
VOID
WinNtSystemTimeToEfiTime (
  IN SYSTEMTIME             *SystemTime,
  IN TIME_ZONE_INFORMATION  *TimeZone,
  OUT EFI_TIME              *Time
  )
/*++

Routine Description:

  TODO: Add function description

Arguments:

  SystemTime  - TODO: add argument description
  TimeZone    - TODO: add argument description
  Time        - TODO: add argument description

Returns:

  TODO: add return values

--*/
{
  Time->Year        = (UINT16) SystemTime->wYear;
  Time->Month       = (UINT8) SystemTime->wMonth;
  Time->Day         = (UINT8) SystemTime->wDay;
  Time->Hour        = (UINT8) SystemTime->wHour;
  Time->Minute      = (UINT8) SystemTime->wMinute;
  Time->Second      = (UINT8) SystemTime->wSecond;
  Time->Nanosecond  = (UINT32) SystemTime->wMilliseconds * 1000000;
  Time->TimeZone    = (INT16) TimeZone->Bias;

  if (TimeZone->StandardDate.wMonth) {
    Time->Daylight = EFI_TIME_ADJUST_DAYLIGHT;
  }
}

EFI_STATUS
EFIAPI
WinNtSimpleFileSystemRead (
  IN     EFI_FILE  *This,
  IN OUT UINTN     *BufferSize,
  OUT    VOID      *Buffer
  )
/*++

Routine Description:

  Read data from a file.

Arguments:

  This        - Pointer to a returned open file handle.

  BufferSize  - On input, the size of the Buffer.  On output, the number of bytes stored in the Buffer.

  Buffer      - Pointer to the first byte of the read Buffer.

Returns:

  EFI_SUCCESS           - The data was read.

  EFI_NO_MEDIA          - The device has no media.

  EFI_DEVICE_ERROR      - The device reported an error.

  EFI_VOLUME_CORRUPTED  - The file system structures are corrupted.

  EFI_BUFFER_TOO_SMALL  - The supplied buffer size was too small to store the current directory entry.
                          *BufferSize has been updated with the size needed to complete the request.

--*/
// TODO:    EFI_INVALID_PARAMETER - add return value to function comment
{
  WIN_NT_EFI_FILE_PRIVATE *PrivateFile;
  EFI_STATUS              Status;
  UINTN                   Size;
  UINTN                   NameSize;
  UINTN                   ResultSize;
  UINTN                   Index;
  SYSTEMTIME              SystemTime;
  EFI_FILE_INFO           *Info;
  WCHAR                   *pw;
  TIME_ZONE_INFORMATION   TimeZone;
  EFI_FILE_INFO           *FileInfo;
  UINT64                  Pos;
  UINT64                  FileSize;
  UINTN                   FileInfoSize;

  if (This == NULL || BufferSize == NULL || Buffer == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  PrivateFile = WIN_NT_EFI_FILE_PRIVATE_DATA_FROM_THIS (This);

  if (PrivateFile->LHandle == INVALID_HANDLE_VALUE) {
    return EFI_DEVICE_ERROR;
  }

  if (!PrivateFile->IsDirectoryPath) {

    if (This->GetPosition (This, &Pos) != EFI_SUCCESS) {
      return EFI_DEVICE_ERROR;
    }

    FileInfoSize = SIZE_OF_EFI_FILE_SYSTEM_INFO;
    gBS->AllocatePool (
          EfiBootServicesData,
          FileInfoSize,
          &FileInfo
          );

    Status = This->GetInfo (
                    This,
                    &gEfiFileInfoGuid,
                    &FileInfoSize,
                    FileInfo
                    );

    if (Status == EFI_BUFFER_TOO_SMALL) {
      gBS->FreePool (FileInfo);
      gBS->AllocatePool (
            EfiBootServicesData,
            FileInfoSize,
            &FileInfo
            );
      Status = This->GetInfo (
                      This,
                      &gEfiFileInfoGuid,
                      &FileInfoSize,
                      FileInfo
                      );
    }

    if (EFI_ERROR (Status)) {
      return EFI_DEVICE_ERROR;
    }

    FileSize = FileInfo->FileSize;

    gBS->FreePool (FileInfo);

    if (Pos >= FileSize) {
      *BufferSize = 0;
      if (Pos == FileSize) {
        return EFI_SUCCESS;
      } else {
        return EFI_DEVICE_ERROR;
      }
    }

    return PrivateFile->WinNtThunk->ReadFile (
                                      PrivateFile->LHandle,
                                      Buffer,
                                      *BufferSize,
                                      BufferSize,
                                      NULL
                                      ) ? EFI_SUCCESS : EFI_DEVICE_ERROR;
  }

  //
  // Read on a directory.  Perform a find next
  //
  if (!PrivateFile->IsValidFindBuf) {
    *BufferSize = 0;
    return EFI_SUCCESS;
  }

  Size        = SIZE_OF_EFI_FILE_INFO;

  NameSize    = EfiStrSize (PrivateFile->FindBuf.cFileName);

  ResultSize  = Size + NameSize;

  Status      = EFI_BUFFER_TOO_SMALL;

  if (*BufferSize >= ResultSize) {
    Status  = EFI_SUCCESS;

    Info    = Buffer;
    EfiZeroMem (Info, ResultSize);

    Info->Size = ResultSize;

    PrivateFile->WinNtThunk->GetTimeZoneInformation (&TimeZone);

    PrivateFile->WinNtThunk->FileTimeToLocalFileTime (
                              &PrivateFile->FindBuf.ftCreationTime,
                              &PrivateFile->FindBuf.ftCreationTime
                              );

    PrivateFile->WinNtThunk->FileTimeToSystemTime (&PrivateFile->FindBuf.ftCreationTime, &SystemTime);

    WinNtSystemTimeToEfiTime (&SystemTime, &TimeZone, &Info->CreateTime);

    PrivateFile->WinNtThunk->FileTimeToLocalFileTime (
                              &PrivateFile->FindBuf.ftLastWriteTime,
                              &PrivateFile->FindBuf.ftLastWriteTime
                              );

    PrivateFile->WinNtThunk->FileTimeToSystemTime (&PrivateFile->FindBuf.ftLastWriteTime, &SystemTime);

    WinNtSystemTimeToEfiTime (&SystemTime, &TimeZone, &Info->ModificationTime);

    Info->FileSize      = PrivateFile->FindBuf.nFileSizeLow;

    Info->PhysicalSize  = PrivateFile->FindBuf.nFileSizeLow;

    if (PrivateFile->FindBuf.dwFileAttributes & FILE_ATTRIBUTE_ARCHIVE) {
      Info->Attribute |= EFI_FILE_ARCHIVE;
    }

    if (PrivateFile->FindBuf.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN) {
      Info->Attribute |= EFI_FILE_HIDDEN;
    }

    if (PrivateFile->FindBuf.dwFileAttributes & FILE_ATTRIBUTE_SYSTEM) {
      Info->Attribute |= EFI_FILE_SYSTEM;
    }

    if (PrivateFile->FindBuf.dwFileAttributes & FILE_ATTRIBUTE_READONLY) {
      Info->Attribute |= EFI_FILE_READ_ONLY;
    }

    if (PrivateFile->FindBuf.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
      Info->Attribute |= EFI_FILE_DIRECTORY;
    }

    NameSize  = NameSize / sizeof (WCHAR);

    pw        = (WCHAR *) (((CHAR8 *) Buffer) + Size);

    for (Index = 0; Index < NameSize; Index++) {
      pw[Index] = PrivateFile->FindBuf.cFileName[Index];
    }

    if (PrivateFile->WinNtThunk->FindNextFile (PrivateFile->LHandle, &PrivateFile->FindBuf)) {
      PrivateFile->IsValidFindBuf = TRUE;
    } else {
      PrivateFile->IsValidFindBuf = FALSE;
    }
  }

  *BufferSize = ResultSize;

  return Status;
}

EFI_STATUS
EFIAPI
WinNtSimpleFileSystemWrite (
  IN     EFI_FILE  *This,
  IN OUT UINTN     *BufferSize,
  IN     VOID      *Buffer
  )
/*++

Routine Description:

  Write data to a file.

Arguments:

  This        - Pointer to an opened file handle.

  BufferSize  - On input, the number of bytes in the Buffer to write to the file.  On output, the number of bytes
                of data written to the file.

  Buffer      - Pointer to the first by of data in the buffer to write to the file.

Returns:

  EFI_SUCCESS           - The data was written to the file.

  EFI_UNSUPPORTED       - Writes to an open directory are not supported.

  EFI_NO_MEDIA          - The device has no media.

  EFI_DEVICE_ERROR      - The device reported an error.

  EFI_VOLUME_CORRUPTED  - The file system structures are corrupt.

  EFI_WRITE_PROTECTED   - The file, directory, volume, or device is write protected.

  EFI_ACCESS_DENIED     - The file was opened read-only.

  EFI_VOLUME_FULL       - The volume is full.

--*/
// TODO:    EFI_INVALID_PARAMETER - add return value to function comment
{
  WIN_NT_EFI_FILE_PRIVATE *PrivateFile;

  if (This == NULL || BufferSize == NULL || Buffer == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  PrivateFile = WIN_NT_EFI_FILE_PRIVATE_DATA_FROM_THIS (This);

  if (PrivateFile->LHandle == INVALID_HANDLE_VALUE) {
    return EFI_DEVICE_ERROR;
  }

  if (PrivateFile->IsDirectoryPath) {
    return EFI_UNSUPPORTED;
  }

  if (PrivateFile->IsOpenedByRead) {
    return EFI_ACCESS_DENIED;
  }

  return PrivateFile->WinNtThunk->WriteFile (
                                    PrivateFile->LHandle,
                                    Buffer,
                                    *BufferSize,
                                    BufferSize,
                                    NULL
                                    ) ? EFI_SUCCESS : EFI_DEVICE_ERROR;

  //
  // bugbug: need to access windows error reporting
  //
}

EFI_STATUS
EFIAPI
WinNtSimpleFileSystemSetPosition (
  IN EFI_FILE  *This,
  IN UINT64    Position
  )
/*++

Routine Description:

  Set a file's current position.

Arguments:

  This      - Pointer to an opened file handle.

  Position  - The byte position from the start of the file to set.

Returns:

  EFI_SUCCESS     - The file position has been changed.

  EFI_UNSUPPORTED - The seek request for non-zero is not supported for directories.

--*/
// TODO:    EFI_INVALID_PARAMETER - add return value to function comment
{
  EFI_STATUS              Status;
  WIN_NT_EFI_FILE_PRIVATE *PrivateFile;
  UINT32                  PosLow;
  UINT32                  PosHigh;
  CHAR16                  *FileName;

  if (This == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  PrivateFile = WIN_NT_EFI_FILE_PRIVATE_DATA_FROM_THIS (This);

  if (PrivateFile->IsDirectoryPath) {
    if (Position != 0) {
      return EFI_UNSUPPORTED;
    }

    Status = gBS->AllocatePool (
                    EfiBootServicesData,
                    EfiStrSize (PrivateFile->FileName) + EfiStrSize (L"\\*"),
                    &FileName
                    );

    if (EFI_ERROR (Status)) {
      return Status;
    }

    EfiStrCpy (FileName, PrivateFile->FileName);
    EfiStrCat (FileName, L"\\*");

    if (PrivateFile->LHandle != INVALID_HANDLE_VALUE) {
      PrivateFile->WinNtThunk->FindClose (PrivateFile->LHandle);
    }

    PrivateFile->LHandle = PrivateFile->WinNtThunk->FindFirstFile (FileName, &PrivateFile->FindBuf);

    if (PrivateFile->LHandle == INVALID_HANDLE_VALUE) {
      PrivateFile->IsValidFindBuf = FALSE;
    } else {
      PrivateFile->IsValidFindBuf = TRUE;
    }

    gBS->FreePool (FileName);

    Status = (PrivateFile->LHandle == INVALID_HANDLE_VALUE) ? EFI_DEVICE_ERROR : EFI_SUCCESS;
  } else {
    if (Position == (UINT64) -1) {
      PosLow = PrivateFile->WinNtThunk->SetFilePointer (PrivateFile->LHandle, (ULONG) 0, NULL, FILE_END);
    } else {
      PosHigh = (UINT32) RShiftU64 (Position, 32);

      PosLow  = PrivateFile->WinNtThunk->SetFilePointer (PrivateFile->LHandle, (ULONG) Position, &PosHigh, FILE_BEGIN);
    }

    Status = (PosLow == 0xFFFFFFFF) ? EFI_DEVICE_ERROR : EFI_SUCCESS;
  }

  return Status;
}

EFI_STATUS
EFIAPI
WinNtSimpleFileSystemGetPosition (
  IN  EFI_FILE  *This,
  OUT UINT64    *Position
  )
/*++

Routine Description:

  Get a file's current position.

Arguments:

  This      - Pointer to an opened file handle.

  Position  - Pointer to storage for the current position.

Returns:

  EFI_SUCCESS     - The file position has been reported.

  EFI_UNSUPPORTED - Not valid for directories.

--*/
// TODO:    EFI_INVALID_PARAMETER - add return value to function comment
{
  EFI_STATUS              Status;
  WIN_NT_EFI_FILE_PRIVATE *PrivateFile;
  INT32                   PositionHigh;
  UINT64                  PosHigh64;

  if (This == NULL || Position == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  PrivateFile   = WIN_NT_EFI_FILE_PRIVATE_DATA_FROM_THIS (This);

  PositionHigh  = 0;
  PosHigh64     = 0;

  if (PrivateFile->IsDirectoryPath) {

    return EFI_UNSUPPORTED;

  } else {

    PositionHigh = 0;
    *Position = PrivateFile->WinNtThunk->SetFilePointer (
                                          PrivateFile->LHandle,
                                          0,
                                          &PositionHigh,
                                          FILE_CURRENT
                                          );

    Status = *Position == 0xffffffff ? EFI_DEVICE_ERROR : EFI_SUCCESS;
    if (EFI_ERROR (Status)) {
      goto Done;
    }

    PosHigh64 = PositionHigh;
    *Position += LShiftU64 (PosHigh64, 32);
  }

Done:
  return Status;
}

STATIC
EFI_STATUS
WinNtSimpleFileSystemFileInfo (
  IN     WIN_NT_EFI_FILE_PRIVATE  *PrivateFile,
  IN OUT UINTN                    *BufferSize,
  OUT    VOID                     *Buffer
  )
/*++

Routine Description:

  TODO: Add function description

Arguments:

  PrivateFile - TODO: add argument description
  BufferSize  - TODO: add argument description
  Buffer      - TODO: add argument description

Returns:

  TODO: add return values

--*/
{
  EFI_STATUS                  Status;
  UINTN                       Size;
  UINTN                       NameSize;
  UINTN                       ResultSize;
  EFI_FILE_INFO               *Info;
  BY_HANDLE_FILE_INFORMATION  FileInfo;
  SYSTEMTIME                  SystemTime;
  CHAR16                      *RealFileName;
  CHAR16                      *TempPointer;

  Size        = SIZE_OF_EFI_FILE_INFO;
  NameSize    = EfiStrSize (PrivateFile->FileName);
  ResultSize  = Size + NameSize;

  Status      = EFI_BUFFER_TOO_SMALL;
  if (*BufferSize >= ResultSize) {
    Status  = EFI_SUCCESS;

    Info    = Buffer;
    EfiZeroMem (Info, ResultSize);

    Info->Size = ResultSize;
    PrivateFile->WinNtThunk->GetFileInformationByHandle (
                              PrivateFile->IsDirectoryPath ? PrivateFile->DirHandle : PrivateFile->LHandle,
                              &FileInfo
                              );
    Info->FileSize      = FileInfo.nFileSizeLow;
    Info->PhysicalSize  = Info->FileSize;

    PrivateFile->WinNtThunk->FileTimeToSystemTime (&FileInfo.ftCreationTime, &SystemTime);
    Info->CreateTime.Year   = SystemTime.wYear;
    Info->CreateTime.Month  = (UINT8) SystemTime.wMonth;
    Info->CreateTime.Day    = (UINT8) SystemTime.wDay;
    Info->CreateTime.Hour   = (UINT8) SystemTime.wHour;
    Info->CreateTime.Minute = (UINT8) SystemTime.wMinute;
    Info->CreateTime.Second = (UINT8) SystemTime.wSecond;

    PrivateFile->WinNtThunk->FileTimeToSystemTime (&FileInfo.ftLastAccessTime, &SystemTime);
    Info->LastAccessTime.Year   = SystemTime.wYear;
    Info->LastAccessTime.Month  = (UINT8) SystemTime.wMonth;
    Info->LastAccessTime.Day    = (UINT8) SystemTime.wDay;
    Info->LastAccessTime.Hour   = (UINT8) SystemTime.wHour;
    Info->LastAccessTime.Minute = (UINT8) SystemTime.wMinute;
    Info->LastAccessTime.Second = (UINT8) SystemTime.wSecond;

    PrivateFile->WinNtThunk->FileTimeToSystemTime (&FileInfo.ftLastWriteTime, &SystemTime);
    Info->ModificationTime.Year   = SystemTime.wYear;
    Info->ModificationTime.Month  = (UINT8) SystemTime.wMonth;
    Info->ModificationTime.Day    = (UINT8) SystemTime.wDay;
    Info->ModificationTime.Hour   = (UINT8) SystemTime.wHour;
    Info->ModificationTime.Minute = (UINT8) SystemTime.wMinute;
    Info->ModificationTime.Second = (UINT8) SystemTime.wSecond;

    if (FileInfo.dwFileAttributes & FILE_ATTRIBUTE_ARCHIVE) {
      Info->Attribute |= EFI_FILE_ARCHIVE;
    }

    if (FileInfo.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN) {
      Info->Attribute |= EFI_FILE_HIDDEN;
    }

    if (FileInfo.dwFileAttributes & FILE_ATTRIBUTE_READONLY) {
      Info->Attribute |= EFI_FILE_READ_ONLY;
    }

    if (FileInfo.dwFileAttributes & FILE_ATTRIBUTE_SYSTEM) {
      Info->Attribute |= EFI_FILE_SYSTEM;
    }

    if (FileInfo.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
      Info->Attribute |= EFI_FILE_DIRECTORY;
    }

    if (PrivateFile->IsDirectoryPath) {
      Info->Attribute |= EFI_FILE_DIRECTORY;
    }

    RealFileName  = PrivateFile->FileName;
    TempPointer   = RealFileName;

    while (*TempPointer) {
      if (*TempPointer == '\\') {
        RealFileName = TempPointer + 1;
      }

      TempPointer++;
    }

    if (PrivateFile->IsRootDirectory) {
      *((CHAR8 *) Buffer + Size) = 0;
    } else {
      EfiCopyMem ((CHAR8 *) Buffer + Size, RealFileName, NameSize);
    }
  }

  *BufferSize = ResultSize;
  return Status;
}

EFI_STATUS
EFIAPI
WinNtSimpleFileSystemGetInfo (
  IN     EFI_FILE  *This,
  IN     EFI_GUID  *InformationType,
  IN OUT UINTN     *BufferSize,
  OUT    VOID      *Buffer
  )
/*++

Routine Description:

  Return information about a file or volume.

Arguments:

  This            - Pointer to an opened file handle.

  InformationType - GUID describing the type of information to be returned.

  BufferSize      - On input, the size of the information buffer.  On output, the number of bytes written to the
                    information buffer.

  Buffer          - Pointer to the first byte of the information buffer.

Returns:

  EFI_SUCCESS           - The requested information has been written into the buffer.

  EFI_UNSUPPORTED       - The InformationType is not known.

  EFI_NO_MEDIA          - The device has no media.

  EFI_DEVICE_ERROR      - The device reported an error.

  EFI_VOLUME_CORRUPTED  - The file system structures are corrupt.

  EFI_BUFFER_TOO_SMALL  - The buffer size was too small to contain the requested information.  The buffer size has
                          been updated with the size needed to complete the requested operation.

--*/
// TODO:    EFI_INVALID_PARAMETER - add return value to function comment
{
  EFI_STATUS                        Status;
  WIN_NT_EFI_FILE_PRIVATE           *PrivateFile;
  EFI_FILE_SYSTEM_INFO              *FileSystemInfoBuffer;
  UINT32                            SectorsPerCluster;
  UINT32                            BytesPerSector;
  UINT32                            FreeClusters;
  UINT32                            TotalClusters;
  UINT32                            BytesPerCluster;
  CHAR16                            *DriveName;
  BOOLEAN                           DriveNameFound;
  BOOL                              NtStatus;
  UINTN                             Index;
  WIN_NT_SIMPLE_FILE_SYSTEM_PRIVATE *PrivateRoot;

  if (This == NULL || InformationType == NULL || BufferSize == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  PrivateFile = WIN_NT_EFI_FILE_PRIVATE_DATA_FROM_THIS (This);
  PrivateRoot = WIN_NT_SIMPLE_FILE_SYSTEM_PRIVATE_DATA_FROM_THIS (PrivateFile->SimpleFileSystem);

  Status      = EFI_UNSUPPORTED;

  if (EfiCompareGuid (InformationType, &gEfiFileInfoGuid)) {
    Status = WinNtSimpleFileSystemFileInfo (PrivateFile, BufferSize, Buffer);
  }

  if (EfiCompareGuid (InformationType, &gEfiFileSystemInfoGuid)) {
    if (*BufferSize < SIZE_OF_EFI_FILE_SYSTEM_INFO + EfiStrSize (PrivateRoot->VolumeLabel)) {
      *BufferSize = SIZE_OF_EFI_FILE_SYSTEM_INFO + EfiStrSize (PrivateRoot->VolumeLabel);
      return EFI_BUFFER_TOO_SMALL;
    }

    FileSystemInfoBuffer            = (EFI_FILE_SYSTEM_INFO *) Buffer;
    FileSystemInfoBuffer->Size      = SIZE_OF_EFI_FILE_SYSTEM_INFO + EfiStrSize (PrivateRoot->VolumeLabel);
    FileSystemInfoBuffer->ReadOnly  = FALSE;

    //
    // Try to get the drive name
    //
    DriveName       = NULL;
    DriveNameFound  = FALSE;
    Status = gBS->AllocatePool (
                    EfiBootServicesData,
                    EfiStrSize (PrivateFile->FilePath) + 1,
                    &DriveName
                    );
    if (EFI_ERROR (Status)) {
      return Status;
    }

    EfiStrCpy (DriveName, PrivateFile->FilePath);
    for (Index = 0; DriveName[Index] != 0 && DriveName[Index] != ':'; Index++) {
      ;
    }

    if (DriveName[Index] == ':') {
      DriveName[Index + 1]  = '\\';
      DriveName[Index + 2]  = 0;
      DriveNameFound        = TRUE;
    } else if (DriveName[0] == '\\' && DriveName[1] == '\\') {
      for (Index = 2; DriveName[Index] != 0 && DriveName[Index] != '\\'; Index++) {
        ;
      }

      if (DriveName[Index] == '\\') {
        DriveNameFound = TRUE;
        for (Index++; DriveName[Index] != 0 && DriveName[Index] != '\\'; Index++) {
          ;
        }

        DriveName[Index]      = '\\';
        DriveName[Index + 1]  = 0;
      }
    }

    //
    // Try GetDiskFreeSpace first
    //
    NtStatus = PrivateFile->WinNtThunk->GetDiskFreeSpace (
                                          DriveNameFound ? DriveName : NULL,
                                          &SectorsPerCluster,
                                          &BytesPerSector,
                                          &FreeClusters,
                                          &TotalClusters
                                          );
    if (DriveName) {
      gBS->FreePool (DriveName);
    }

    if (NtStatus) {
      //
      // Succeeded
      //
      BytesPerCluster                   = BytesPerSector * SectorsPerCluster;
      FileSystemInfoBuffer->VolumeSize  = MultU64x32 (TotalClusters, BytesPerCluster);
      FileSystemInfoBuffer->FreeSpace   = MultU64x32 (FreeClusters, BytesPerCluster);
      FileSystemInfoBuffer->BlockSize   = BytesPerCluster;

    } else {
      //
      // try GetDiskFreeSpaceEx then
      //
      FileSystemInfoBuffer->BlockSize = 0;
      NtStatus = PrivateFile->WinNtThunk->GetDiskFreeSpaceEx (
                                            PrivateFile->FilePath,
                                            (PULARGE_INTEGER) (&FileSystemInfoBuffer->FreeSpace),
                                            (PULARGE_INTEGER) (&FileSystemInfoBuffer->VolumeSize),
                                            NULL
                                            );
      if (!NtStatus) {
        return EFI_DEVICE_ERROR;
      }
    }

    EfiStrCpy ((CHAR16 *) FileSystemInfoBuffer->VolumeLabel, PrivateRoot->VolumeLabel);
    *BufferSize = SIZE_OF_EFI_FILE_SYSTEM_INFO + EfiStrSize (PrivateRoot->VolumeLabel);
    Status      = EFI_SUCCESS;
  }

  if (EfiCompareGuid (InformationType, &gEfiFileSystemVolumeLabelInfoIdGuid)) {
    if (*BufferSize < EfiStrSize (PrivateRoot->VolumeLabel)) {
      *BufferSize = EfiStrSize (PrivateRoot->VolumeLabel);
      return EFI_BUFFER_TOO_SMALL;
    }

    EfiStrCpy ((CHAR16 *) Buffer, PrivateRoot->VolumeLabel);
    *BufferSize = EfiStrSize (PrivateRoot->VolumeLabel);
    Status      = EFI_SUCCESS;
  }

  return Status;
}

EFI_STATUS
EFIAPI
WinNtSimpleFileSystemSetInfo (
  IN EFI_FILE         *This,
  IN EFI_GUID         *InformationType,
  IN UINTN            BufferSize,
  IN VOID             *Buffer
  )
/*++

Routine Description:

  Set information about a file or volume.

Arguments:

  This            - Pointer to an opened file handle.

  InformationType - GUID identifying the type of information to set.

  BufferSize      - Number of bytes of data in the information buffer.

  Buffer          - Pointer to the first byte of data in the information buffer.

Returns:

  EFI_SUCCESS           - The file or volume information has been updated.

  EFI_UNSUPPORTED       - The information identifier is not recognised.

  EFI_NO_MEDIA          - The device has no media.

  EFI_DEVICE_ERROR      - The device reported an error.

  EFI_VOLUME_CORRUPTED  - The file system structures are corrupt.

  EFI_WRITE_PROTECTED   - The file, directory, volume, or device is write protected.

  EFI_ACCESS_DENIED     - The file was opened read-only.

  EFI_VOLUME_FULL       - The volume is full.

  EFI_BAD_BUFFER_SIZE   - The buffer size is smaller than the type indicated by InformationType.

--*/
// TODO:    EFI_INVALID_PARAMETER - add return value to function comment
// TODO:    EFI_INVALID_PARAMETER - add return value to function comment
{
  WIN_NT_SIMPLE_FILE_SYSTEM_PRIVATE *PrivateRoot;
  WIN_NT_EFI_FILE_PRIVATE           *PrivateFile;
  EFI_FILE_INFO                     *OldFileInfo;
  EFI_FILE_INFO                     *NewFileInfo;
  EFI_STATUS                        Status;
  UINTN                             OldInfoSize;
  INTN                              NtStatus;
  UINT32                            NewAttr;
  UINT32                            OldAttr;
  CHAR16                            *OldFileName;
  CHAR16                            *NewFileName;
  CHAR16                            *TempFileName;
  CHAR16                            *CharPointer;
  BOOLEAN                           AttrChangeFlag;
  BOOLEAN                           NameChangeFlag;
  BOOLEAN                           SizeChangeFlag;
  BOOLEAN                           TimeChangeFlag;
  UINT64                            CurPos;
  SYSTEMTIME                        NewCreationSystemTime;
  SYSTEMTIME                        NewLastAccessSystemTime;
  SYSTEMTIME                        NewLastWriteSystemTime;
  FILETIME                          NewCreationFileTime;
  FILETIME                          NewLastAccessFileTime;
  FILETIME                          NewLastWriteFileTime;
  WIN32_FIND_DATA                   FindBuf;
  EFI_FILE_SYSTEM_INFO              *NewFileSystemInfo;

  //
  // Check for invalid parameters.
  //
  if (This == NULL || InformationType == NULL || BufferSize == 0 || Buffer == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  //
  // Initialise locals.
  //
  PrivateFile               = WIN_NT_EFI_FILE_PRIVATE_DATA_FROM_THIS (This);
  PrivateRoot               = WIN_NT_SIMPLE_FILE_SYSTEM_PRIVATE_DATA_FROM_THIS (PrivateFile->SimpleFileSystem);

  Status                    = EFI_UNSUPPORTED;
  OldFileInfo               = NewFileInfo = NULL;
  OldFileName               = NewFileName = NULL;
  AttrChangeFlag = NameChangeFlag = SizeChangeFlag = TimeChangeFlag = FALSE;

  //
  // Set file system information.
  //
  if (EfiCompareGuid (InformationType, &gEfiFileSystemInfoGuid)) {
    if (BufferSize < SIZE_OF_EFI_FILE_SYSTEM_INFO + EfiStrSize (PrivateRoot->VolumeLabel)) {
      return EFI_BAD_BUFFER_SIZE;
    }

    NewFileSystemInfo = (EFI_FILE_SYSTEM_INFO *) Buffer;

    gBS->FreePool (PrivateRoot->VolumeLabel);

    PrivateRoot->VolumeLabel = NULL;
    Status = gBS->AllocatePool (
                    EfiBootServicesData,
                    EfiStrSize (NewFileSystemInfo->VolumeLabel),
                    &PrivateRoot->VolumeLabel
                    );

    if (EFI_ERROR (Status)) {
      goto Done;
    }

    EfiStrCpy (PrivateRoot->VolumeLabel, NewFileSystemInfo->VolumeLabel);

    return EFI_SUCCESS;
  }

  //
  // Set volume label information.
  //
  if (EfiCompareGuid (InformationType, &gEfiFileSystemVolumeLabelInfoIdGuid)) {
    if (BufferSize < EfiStrSize (PrivateRoot->VolumeLabel)) {
      return EFI_BAD_BUFFER_SIZE;
    }

    EfiStrCpy (PrivateRoot->VolumeLabel, (CHAR16 *) Buffer);

    return EFI_SUCCESS;
  }

  if (!EfiCompareGuid (InformationType, &gEfiFileInfoGuid)) {
    return EFI_UNSUPPORTED;
  }

  if (BufferSize < SIZE_OF_EFI_FILE_INFO) {
    return EFI_BAD_BUFFER_SIZE;
  }

  //
  // Set file/directory information.
  //

  //
  // Check for invalid set file information parameters.
  //
  NewFileInfo = (EFI_FILE_INFO *) Buffer;

  if (NewFileInfo->Size <= sizeof (EFI_FILE_INFO) ||
      (NewFileInfo->Attribute &~(EFI_FILE_VALID_ATTR)) ||
      (sizeof (UINTN) == 4 && NewFileInfo->Size > 0xFFFFFFFF)
      ) {
    return EFI_INVALID_PARAMETER;
  }

  //
  // bugbug: - This is not safe.  We need something like EfiStrMaxSize()
  // that would have an additional parameter that would be the size
  // of the string array just in case there are no NULL characters in
  // the string array.
  //
  //
  // Get current file information so we can determine what kind
  // of change request this is.
  //
  OldInfoSize = 0;
  Status      = WinNtSimpleFileSystemFileInfo (PrivateFile, &OldInfoSize, NULL);

  if (Status != EFI_BUFFER_TOO_SMALL) {
    Status = EFI_DEVICE_ERROR;
    goto Done;
  }

  Status = gBS->AllocatePool (EfiBootServicesData, OldInfoSize, &OldFileInfo);

  if (EFI_ERROR (Status)) {
    goto Done;
  }

  Status = WinNtSimpleFileSystemFileInfo (PrivateFile, &OldInfoSize, OldFileInfo);

  if (EFI_ERROR (Status)) {
    goto Done;
  }

  Status = gBS->AllocatePool (
                  EfiBootServicesData,
                  EfiStrSize (PrivateFile->FileName),
                  &OldFileName
                  );

  if (EFI_ERROR (Status)) {
    goto Done;
  }

  EfiStrCpy (OldFileName, PrivateFile->FileName);

  //
  // Make full pathname from new filename and rootpath.
  //
  if (NewFileInfo->FileName[0] == '\\') {
    Status = gBS->AllocatePool (
                    EfiBootServicesData,
                    EfiStrSize (PrivateRoot->FilePath) + EfiStrSize (L"\\") + EfiStrSize (NewFileInfo->FileName),
                    &NewFileName
                    );

    if (EFI_ERROR (Status)) {
      goto Done;
    }

    EfiStrCpy (NewFileName, PrivateRoot->FilePath);
    EfiStrCat (NewFileName, L"\\");
    EfiStrCat (NewFileName, NewFileInfo->FileName + 1);
  } else {
    Status = gBS->AllocatePool (
                    EfiBootServicesData,
                    EfiStrSize (PrivateFile->FilePath) + EfiStrSize (L"\\") + EfiStrSize (NewFileInfo->FileName),
                    &NewFileName
                    );

    if (EFI_ERROR (Status)) {
      goto Done;
    }

    EfiStrCpy (NewFileName, PrivateFile->FilePath);
    EfiStrCat (NewFileName, L"\\");
    EfiStrCat (NewFileName, NewFileInfo->FileName);
  }

  //
  // Is there an attribute change request?
  //
  if (NewFileInfo->Attribute != OldFileInfo->Attribute) {
    if ((NewFileInfo->Attribute & EFI_FILE_DIRECTORY) != (OldFileInfo->Attribute & EFI_FILE_DIRECTORY)) {
      Status = EFI_INVALID_PARAMETER;
      goto Done;
    }

    AttrChangeFlag = TRUE;
  }

  //
  // Is there a name change request?
  // bugbug: - Need EfiStrCaseCmp()
  //
  if (EfiStrCmp (NewFileInfo->FileName, OldFileInfo->FileName)) {
    NameChangeFlag = TRUE;
  }

  //
  // Is there a size change request?
  //
  if (NewFileInfo->FileSize != OldFileInfo->FileSize) {
    SizeChangeFlag = TRUE;
  }

  //
  // Is there a time stamp change request?
  //
  if (!IsZero (&NewFileInfo->CreateTime, sizeof (EFI_TIME)) &&
      EfiCompareMem (&NewFileInfo->CreateTime, &OldFileInfo->CreateTime, sizeof (EFI_TIME))
        ) {
    TimeChangeFlag = TRUE;
  } else if (!IsZero (&NewFileInfo->LastAccessTime, sizeof (EFI_TIME)) &&
           EfiCompareMem (&NewFileInfo->LastAccessTime, &OldFileInfo->LastAccessTime, sizeof (EFI_TIME))
            ) {
    TimeChangeFlag = TRUE;
  } else if (!IsZero (&NewFileInfo->ModificationTime, sizeof (EFI_TIME)) &&
           EfiCompareMem (&NewFileInfo->ModificationTime, &OldFileInfo->ModificationTime, sizeof (EFI_TIME))
            ) {
    TimeChangeFlag = TRUE;
  }

  //
  // All done if there are no change requests being made.
  //
  if (!(AttrChangeFlag || NameChangeFlag || SizeChangeFlag || TimeChangeFlag)) {
    Status = EFI_SUCCESS;
    goto Done;
  }

  //
  // Set file or directory information.
  //
  OldAttr = PrivateFile->WinNtThunk->GetFileAttributes (OldFileName);

  //
  // Name change.
  //
  if (NameChangeFlag) {
    //
    // Close the handles first
    //
    if (PrivateFile->IsOpenedByRead) {
      Status = EFI_ACCESS_DENIED;
      goto Done;
    }

    for (CharPointer = NewFileName; *CharPointer != 0 && *CharPointer != L'/'; CharPointer++) {
    }

    if (*CharPointer != 0) {
      Status = EFI_ACCESS_DENIED;
      goto Done;
    }

    if (PrivateFile->LHandle != INVALID_HANDLE_VALUE) {
      if (PrivateFile->IsDirectoryPath) {
        PrivateFile->WinNtThunk->FindClose (PrivateFile->LHandle);
      } else {
        PrivateFile->WinNtThunk->CloseHandle (PrivateFile->LHandle);
        PrivateFile->LHandle = INVALID_HANDLE_VALUE;
      }
    }

    if (PrivateFile->IsDirectoryPath && PrivateFile->DirHandle != INVALID_HANDLE_VALUE) {
      PrivateFile->WinNtThunk->CloseHandle (PrivateFile->DirHandle);
      PrivateFile->DirHandle = INVALID_HANDLE_VALUE;
    }

    NtStatus = PrivateFile->WinNtThunk->MoveFile (OldFileName, NewFileName);

    if (NtStatus) {
      //
      // modify file name
      //
      gBS->FreePool (PrivateFile->FileName);

      Status = gBS->AllocatePool (
                      EfiBootServicesData,
                      EfiStrSize (NewFileName),
                      &PrivateFile->FileName
                      );

      if (EFI_ERROR (Status)) {
        goto Done;
      }

      EfiStrCpy (PrivateFile->FileName, NewFileName);

      Status = gBS->AllocatePool (
                      EfiBootServicesData,
                      EfiStrSize (NewFileName) + EfiStrSize (L"\\*"),
                      &TempFileName
                      );

      EfiStrCpy (TempFileName, NewFileName);

      if (!PrivateFile->IsDirectoryPath) {
        PrivateFile->LHandle = PrivateFile->WinNtThunk->CreateFile (
                                                          TempFileName,
                                                          PrivateFile->IsOpenedByRead ? GENERIC_READ : GENERIC_READ | GENERIC_WRITE,
                                                          FILE_SHARE_READ | FILE_SHARE_WRITE,
                                                          NULL,
                                                          OPEN_EXISTING,
                                                          0,
                                                          NULL
                                                          );

        gBS->FreePool (TempFileName);

        //
        //  Flush buffers just in case
        //
        if (PrivateFile->WinNtThunk->FlushFileBuffers (PrivateFile->LHandle) == 0) {
          Status = EFI_DEVICE_ERROR;
          goto Done;
        }
      } else {
        PrivateFile->DirHandle = PrivateFile->WinNtThunk->CreateFile (
                                                            TempFileName,
                                                            PrivateFile->IsOpenedByRead ? GENERIC_READ : GENERIC_READ | GENERIC_WRITE,
                                                            FILE_SHARE_READ | FILE_SHARE_WRITE,
                                                            NULL,
                                                            OPEN_EXISTING,
                                                            FILE_FLAG_BACKUP_SEMANTICS,
                                                            NULL
                                                            );

        EfiStrCat (TempFileName, L"\\*");
        PrivateFile->LHandle = PrivateFile->WinNtThunk->FindFirstFile (TempFileName, &FindBuf);

        gBS->FreePool (TempFileName);
      }
    } else {
Reopen: ;
      Status    = EFI_DEVICE_ERROR;

      NtStatus  = PrivateFile->WinNtThunk->SetFileAttributes (OldFileName, OldAttr);

      if (!NtStatus) {
        goto Done;
      }

      Status = gBS->AllocatePool (
                      EfiBootServicesData,
                      EfiStrSize (OldFileName) + EfiStrSize (L"\\*"),
                      &TempFileName
                      );

      EfiStrCpy (TempFileName, OldFileName);

      if (!PrivateFile->IsDirectoryPath) {
        PrivateFile->LHandle = PrivateFile->WinNtThunk->CreateFile (
                                                          TempFileName,
                                                          PrivateFile->IsOpenedByRead ? GENERIC_READ : GENERIC_READ | GENERIC_WRITE,
                                                          FILE_SHARE_READ | FILE_SHARE_WRITE,
                                                          NULL,
                                                          OPEN_EXISTING,
                                                          0,
                                                          NULL
                                                          );
      } else {
        PrivateFile->DirHandle = PrivateFile->WinNtThunk->CreateFile (
                                                            TempFileName,
                                                            PrivateFile->IsOpenedByRead ? GENERIC_READ : GENERIC_READ | GENERIC_WRITE,
                                                            FILE_SHARE_READ | FILE_SHARE_WRITE,
                                                            NULL,
                                                            OPEN_EXISTING,
                                                            FILE_FLAG_BACKUP_SEMANTICS,
                                                            NULL
                                                            );

        EfiStrCat (TempFileName, L"\\*");
        PrivateFile->LHandle = PrivateFile->WinNtThunk->FindFirstFile (TempFileName, &FindBuf);
      }

      gBS->FreePool (TempFileName);

      goto Done;

    }
  }

  //
  //  Size change
  //
  if (SizeChangeFlag) {
    if (PrivateFile->IsDirectoryPath) {
      Status = EFI_UNSUPPORTED;
      goto Done;
    }

    if (PrivateFile->IsOpenedByRead || OldFileInfo->Attribute & EFI_FILE_READ_ONLY) {
      Status = EFI_ACCESS_DENIED;
      goto Done;
    }

    Status = This->GetPosition (This, &CurPos);
    if (EFI_ERROR (Status)) {
      goto Done;
    }

    Status = This->SetPosition (This, NewFileInfo->FileSize);
    if (EFI_ERROR (Status)) {
      goto Done;
    }

    if (PrivateFile->WinNtThunk->SetEndOfFile (PrivateFile->LHandle) == 0) {
      Status = EFI_DEVICE_ERROR;
      goto Done;
    }

    Status = This->SetPosition (This, CurPos);
    if (EFI_ERROR (Status)) {
      goto Done;
    }
  }

  //
  // Time change
  //
  if (TimeChangeFlag) {

    NewCreationSystemTime.wYear         = NewFileInfo->CreateTime.Year;
    NewCreationSystemTime.wMonth        = NewFileInfo->CreateTime.Month;
    NewCreationSystemTime.wDay          = NewFileInfo->CreateTime.Day;
    NewCreationSystemTime.wHour         = NewFileInfo->CreateTime.Hour;
    NewCreationSystemTime.wMinute       = NewFileInfo->CreateTime.Minute;
    NewCreationSystemTime.wSecond       = NewFileInfo->CreateTime.Second;
    NewCreationSystemTime.wMilliseconds = 0;

    if (!PrivateFile->WinNtThunk->SystemTimeToFileTime (
                                    &NewCreationSystemTime,
                                    &NewCreationFileTime
                                    )) {
      goto Done;
    }

    NewLastAccessSystemTime.wYear         = NewFileInfo->LastAccessTime.Year;
    NewLastAccessSystemTime.wMonth        = NewFileInfo->LastAccessTime.Month;
    NewLastAccessSystemTime.wDay          = NewFileInfo->LastAccessTime.Day;
    NewLastAccessSystemTime.wHour         = NewFileInfo->LastAccessTime.Hour;
    NewLastAccessSystemTime.wMinute       = NewFileInfo->LastAccessTime.Minute;
    NewLastAccessSystemTime.wSecond       = NewFileInfo->LastAccessTime.Second;
    NewLastAccessSystemTime.wMilliseconds = 0;

    if (!PrivateFile->WinNtThunk->SystemTimeToFileTime (
                                    &NewLastAccessSystemTime,
                                    &NewLastAccessFileTime
                                    )) {
      goto Done;
    }

    NewLastWriteSystemTime.wYear          = NewFileInfo->ModificationTime.Year;
    NewLastWriteSystemTime.wMonth         = NewFileInfo->ModificationTime.Month;
    NewLastWriteSystemTime.wDay           = NewFileInfo->ModificationTime.Day;
    NewLastWriteSystemTime.wHour          = NewFileInfo->ModificationTime.Hour;
    NewLastWriteSystemTime.wMinute        = NewFileInfo->ModificationTime.Minute;
    NewLastWriteSystemTime.wSecond        = NewFileInfo->ModificationTime.Second;
    NewLastWriteSystemTime.wMilliseconds  = 0;

    if (!PrivateFile->WinNtThunk->SystemTimeToFileTime (
                                    &NewLastWriteSystemTime,
                                    &NewLastWriteFileTime
                                    )) {
      goto Done;
    }

    if (!PrivateFile->WinNtThunk->SetFileTime (
                                    PrivateFile->IsDirectoryPath ? PrivateFile->DirHandle : PrivateFile->LHandle,
                                    &NewCreationFileTime,
                                    &NewLastAccessFileTime,
                                    &NewLastWriteFileTime
                                    )) {
      Status = EFI_DEVICE_ERROR;
      goto Done;
    }

  }

  //
  // No matter about AttrChangeFlag, Attribute must be set.
  // Because operation before may cause attribute change.
  //
  NewAttr = OldAttr;

  if (NewFileInfo->Attribute & EFI_FILE_ARCHIVE) {
    NewAttr |= FILE_ATTRIBUTE_ARCHIVE;
  } else {
    NewAttr &= ~FILE_ATTRIBUTE_ARCHIVE;
  }

  if (NewFileInfo->Attribute & EFI_FILE_HIDDEN) {
    NewAttr |= FILE_ATTRIBUTE_HIDDEN;
  } else {
    NewAttr &= ~FILE_ATTRIBUTE_HIDDEN;
  }

  if (NewFileInfo->Attribute & EFI_FILE_SYSTEM) {
    NewAttr |= FILE_ATTRIBUTE_SYSTEM;
  } else {
    NewAttr &= ~FILE_ATTRIBUTE_SYSTEM;
  }

  if (NewFileInfo->Attribute & EFI_FILE_READ_ONLY) {
    NewAttr |= FILE_ATTRIBUTE_READONLY;
  } else {
    NewAttr &= ~FILE_ATTRIBUTE_READONLY;
  }

  NtStatus = PrivateFile->WinNtThunk->SetFileAttributes (NewFileName, NewAttr);

  if (!NtStatus) {
    goto Reopen;
  }

Done:
  if (OldFileInfo != NULL) {
    gBS->FreePool (OldFileInfo);
  }

  if (OldFileName != NULL) {
    gBS->FreePool (OldFileName);
  }

  if (NewFileName != NULL) {
    gBS->FreePool (NewFileName);
  }

  return Status;
}

EFI_STATUS
EFIAPI
WinNtSimpleFileSystemFlush (
  IN EFI_FILE  *This
  )
/*++

Routine Description:

  Flush all modified data to the media.

Arguments:

  This  - Pointer to an opened file handle.

Returns:

  EFI_SUCCESS           - The data has been flushed.

  EFI_NO_MEDIA          - The device has no media.

  EFI_DEVICE_ERROR      - The device reported an error.

  EFI_VOLUME_CORRUPTED  - The file system structures have been corrupted.

  EFI_WRITE_PROTECTED   - The file, directory, volume, or device is write protected.

  EFI_ACCESS_DENIED     - The file was opened read-only.

  EFI_VOLUME_FULL       - The volume is full.

--*/
// TODO:    EFI_INVALID_PARAMETER - add return value to function comment
{
  BY_HANDLE_FILE_INFORMATION  FileInfo;
  WIN_NT_EFI_FILE_PRIVATE     *PrivateFile;

  if (This == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  PrivateFile = WIN_NT_EFI_FILE_PRIVATE_DATA_FROM_THIS (This);

  if (PrivateFile->LHandle == INVALID_HANDLE_VALUE) {
    return EFI_DEVICE_ERROR;
  }

  if (PrivateFile->IsDirectoryPath) {
    return EFI_SUCCESS;
  }

  if (PrivateFile->IsOpenedByRead) {
    return EFI_ACCESS_DENIED;
  }

  PrivateFile->WinNtThunk->GetFileInformationByHandle (PrivateFile->LHandle, &FileInfo);

  if (FileInfo.dwFileAttributes & FILE_ATTRIBUTE_READONLY) {
    return EFI_ACCESS_DENIED;
  }

  return PrivateFile->WinNtThunk->FlushFileBuffers (PrivateFile->LHandle) ? EFI_SUCCESS : EFI_DEVICE_ERROR;

  //
  // bugbug: - Use Windows error reporting.
  //
}

/* eof - WinNtSimpleFileSystem.c */
