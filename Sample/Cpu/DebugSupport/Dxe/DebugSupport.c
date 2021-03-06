/*++

Copyright (c) 2004, Intel Corporation                                                         
All rights reserved. This program and the accompanying materials                          
are licensed and made available under the terms and conditions of the BSD License         
which accompanies this distribution.  The full text of the license may be found at        
http://opensource.org/licenses/bsd-license.php                                            
                                                                                          
THE PROGRAM IS DISTRIBUTED UNDER THE BSD LICENSE ON AN "AS IS" BASIS,                     
WITHOUT WARRANTIES OR REPRESENTATIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED.             

Module Name:

    DebugSupport.c

Abstract:

    Top level C file for debug support driver.  Contains initialization function.

Revision History

--*/

//
// Master EFI header file
//
#include "Tiano.h"

//
// Common library header files
//
#include "EfiDriverLib.h"

//
// Consumed protocols
//
#include EFI_PROTOCOL_DEFINITION (LoadedImage)

//
// Produced protocols
//
#include EFI_PROTOCOL_DEFINITION (DebugSupport)

//
// private header files
//
#include "plDebugSupport.h"

//
// This is a global that is the actual interface
//
EFI_DEBUG_SUPPORT_PROTOCOL  gDebugSupportProtocolInterface = {
  EFI_ISA,
  GetMaximumProcessorIndex,
  RegisterPeriodicCallback,
  RegisterExceptionCallback,
  InvalidateInstructionCache
};

//
// Driver Entry Point
//
EFI_STATUS
InitializeDebugSupportDriver (
  IN EFI_HANDLE               ImageHandle,
  IN EFI_SYSTEM_TABLE         *SystemTable
  )
/*++

Routine Description:
  Driver entry point.  Checks to see there's not already a DebugSupport protocol
  installed for the selected processor before installing protocol.

Arguments:
  IN EFI_HANDLE               ImageHandle,
  IN EFI_SYSTEM_TABLE         *SystemTable

Returns:

  EFI_STATUS

--*/
// TODO:    ImageHandle - add argument and description to function comment
// TODO:    SystemTable - add argument and description to function comment
{
  EFI_LOADED_IMAGE_PROTOCOL   *LoadedImageProtocolPtr;
  EFI_STATUS                  Status;
  EFI_HANDLE                  Handle;
  EFI_HANDLE                  *HandlePtr;
  UINTN                       NumHandles;
  EFI_DEBUG_SUPPORT_PROTOCOL  *DebugSupportProtocolPtr;

  //
  // Initialize libraries
  //
  EfiInitializeDriverLib (ImageHandle, SystemTable);

  //
  // Check to be sure we have an EFI 1.1 core
  //
  if (SystemTable->Hdr.Revision < EFI_1_10_SYSTEM_TABLE_REVISION) {
    Status = EFI_UNSUPPORTED;
    goto ErrExit;
  }

  //
  //  Install Protocol Interface...
  //
  // First check to see that the debug support protocol for this processor
  // type is not already installed
  //
  Status = gBS->LocateHandleBuffer (
                  ByProtocol,
                  &gEfiDebugSupportProtocolGuid,
                  NULL,
                  &NumHandles,
                  &HandlePtr
                  );

  if (Status != EFI_NOT_FOUND) {
    do {
      NumHandles--;
      Status = gBS->OpenProtocol (
                      HandlePtr[NumHandles],
                      &gEfiDebugSupportProtocolGuid,
                      (VOID **) &DebugSupportProtocolPtr,
                      ImageHandle,
                      NULL,
                      EFI_OPEN_PROTOCOL_GET_PROTOCOL
                      );
      if (Status == EFI_SUCCESS && DebugSupportProtocolPtr->Isa == EFI_ISA) {
        gBS->FreePool (HandlePtr);
        Status = EFI_ALREADY_STARTED;
        goto ErrExit;
      }
    } while (NumHandles > 0);
    gBS->FreePool (HandlePtr);
  }

  //
  // Get our image information and install platform specific unload handler
  //
  Status = gBS->OpenProtocol (
                  ImageHandle,
                  &gEfiLoadedImageProtocolGuid,
                  (VOID **) &LoadedImageProtocolPtr,
                  ImageHandle,
                  NULL,
                  EFI_OPEN_PROTOCOL_GET_PROTOCOL
                  );
  ASSERT (!EFI_ERROR (Status));
  if (Status != EFI_SUCCESS) {
    goto ErrExit;
  }

  LoadedImageProtocolPtr->Unload = plUnloadDebugSupportDriver;

  //
  // Call hook for platform specific initialization
  //
  Status = plInitializeDebugSupportDriver ();
  ASSERT (!EFI_ERROR (Status));
  if (Status != EFI_SUCCESS) {
    goto ErrExit;
  }

  //
  // Install DebugSupport protocol to new handle
  //
  Handle = NULL;
  Status = gBS->InstallProtocolInterface (
                  &Handle,
                  &gEfiDebugSupportProtocolGuid,
                  EFI_NATIVE_INTERFACE,
                  &gDebugSupportProtocolInterface
                  );
  ASSERT (!EFI_ERROR (Status));
  if (Status != EFI_SUCCESS) {
    goto ErrExit;
  }

ErrExit:
  return Status;
}
