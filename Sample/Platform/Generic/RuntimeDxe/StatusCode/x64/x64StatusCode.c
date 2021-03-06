/*++

Copyright (c) 2005 - 2006, Intel Corporation                                                         
All rights reserved. This program and the accompanying materials                          
are licensed and made available under the terms and conditions of the BSD License         
which accompanies this distribution.  The full text of the license may be found at        
http://opensource.org/licenses/bsd-license.php                                            
                                                                                          
THE PROGRAM IS DISTRIBUTED UNDER THE BSD LICENSE ON AN "AS IS" BASIS,                     
WITHOUT WARRANTIES OR REPRESENTATIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED.             

Module Name:
  
  x64StatusCode.c

Abstract:

  Installs the ReportStatusCode runtime service.

--*/

#include "StatusCode.h"

//
//
//
EFI_HANDLE  gStatusCodeHandle = NULL;

const EFI_STATUS_CODE_PROTOCOL gStatusCodeInstance = {
  StatusCodeReportStatusCode
};
//
// Define the driver entry point
//
EFI_DRIVER_ENTRY_POINT (InstallStatusCode)

EFI_STATUS
InstallStatusCode (
  IN EFI_HANDLE         ImageHandle,
  IN EFI_SYSTEM_TABLE   *SystemTable
  )
/*++

Routine Description:

  Install the ReportStatusCode runtime service.

Arguments:

  ImageHandle     Image handle of the loaded driver
  SystemTable     Pointer to the System Table

Returns:

  EFI_SUCCESS     The function always returns success.

--*/
{
  EFI_STATUS  Status;

  //
  // Initialize RT status code
  //
  InitializeStatusCode (ImageHandle, SystemTable);

  //
  // Update runtime service table.
  //
#if (EFI_SPECIFICATION_VERSION < 0x00020000)
  //
  // Update runtime service table.
  //
  SystemTable->RuntimeServices->ReportStatusCode = StatusCodeReportStatusCode;
#endif

  Status = gBS->InstallMultipleProtocolInterfaces (
                  &gStatusCodeHandle,
                  &gEfiStatusCodeRuntimeProtocolGuid,
                  &gStatusCodeInstance,
                  NULL
                  );
  ASSERT_EFI_ERROR (Status);

  return Status;
}
