/*++

Copyright (c) 2006 - 2008, Intel Corporation                                                         
All rights reserved. This program and the accompanying materials                          
are licensed and made available under the terms and conditions of the BSD License         
which accompanies this distribution.  The full text of the license may be found at        
http://opensource.org/licenses/bsd-license.php                                            
                                                                                          
THE PROGRAM IS DISTRIBUTED UNDER THE BSD LICENSE ON AN "AS IS" BASIS,                     
WITHOUT WARRANTIES OR REPRESENTATIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED.


Module Name:

  Ps2Keyboard.c

Abstract:

  PS/2 Keyboard driver. Routines that interacts with callers,
  conforming to EFI driver model

--*/

#include "Ps2Keyboard.h"

//
// Function prototypes
//
EFI_STATUS
EFIAPI
KbdControllerDriverSupported (
  IN EFI_DRIVER_BINDING_PROTOCOL    *This,
  IN EFI_HANDLE                     Controller,
  IN EFI_DEVICE_PATH_PROTOCOL       *RemainingDevicePath
  );

EFI_STATUS
EFIAPI
KbdControllerDriverStart (
  IN EFI_DRIVER_BINDING_PROTOCOL    *This,
  IN EFI_HANDLE                     Controller,
  IN EFI_DEVICE_PATH_PROTOCOL       *RemainingDevicePath
  );

EFI_STATUS
EFIAPI
KbdControllerDriverStop (
  IN  EFI_DRIVER_BINDING_PROTOCOL    *This,
  IN  EFI_HANDLE                     Controller,
  IN  UINTN                          NumberOfChildren,
  IN  EFI_HANDLE                     *ChildHandleBuffer
  );

#if (EFI_SPECIFICATION_VERSION >= 0x0002000A)
#ifndef DISABLE_CONSOLE_EX
STATIC
EFI_STATUS
KbdFreeNotifyList (
  IN OUT EFI_LIST_ENTRY       *ListHead
  );  

EFI_GUID gSimpleTextInExNotifyGuid = SIMPLE_TEXTIN_EX_NOTIFY_GUID;
#endif  // DISABLE_CONSOLE_EX
#endif
//
// Global variables
//
//
// DriverBinding Protocol Instance
//
EFI_DRIVER_BINDING_PROTOCOL gKeyboardControllerDriver = {
  KbdControllerDriverSupported,
  KbdControllerDriverStart,
  KbdControllerDriverStop,
  0xa,
  NULL,
  NULL
};

EFI_STATUS
EFIAPI
KbdControllerDriverSupported (
  IN EFI_DRIVER_BINDING_PROTOCOL    *This,
  IN EFI_HANDLE                     Controller,
  IN EFI_DEVICE_PATH_PROTOCOL       *RemainingDevicePath
  )
/*++

Routine Description:

  ControllerDriver Protocol Method

Arguments:

Returns:

--*/
// GC_TODO:    This - add argument and description to function comment
// GC_TODO:    Controller - add argument and description to function comment
// GC_TODO:    RemainingDevicePath - add argument and description to function comment
{
  EFI_STATUS                          Status;
  EFI_INTERFACE_DEFINITION_FOR_ISA_IO *IsaIo;

  //
  // Open the IO Abstraction(s) needed to perform the supported test
  //
  Status = gBS->OpenProtocol (
                  Controller,
                  EFI_ISA_IO_PROTOCOL_VERSION,
                  (VOID **) &IsaIo,
                  This->DriverBindingHandle,
                  Controller,
                  EFI_OPEN_PROTOCOL_BY_DRIVER
                  );
  if (EFI_ERROR (Status)) {
    return Status;
  }
  //
  // Use the ISA I/O Protocol to see if Controller is the Keyboard controller
  //
  if (IsaIo->ResourceList->Device.HID != EISA_PNP_ID (0x303) || IsaIo->ResourceList->Device.UID != 0) {
    Status = EFI_UNSUPPORTED;
  }
  //
  // Close the I/O Abstraction(s) used to perform the supported test
  //
  gBS->CloseProtocol (
         Controller,
         EFI_ISA_IO_PROTOCOL_VERSION,
         This->DriverBindingHandle,
         Controller
         );

  return Status;
}

EFI_STATUS
EFIAPI
KbdControllerDriverStart (
  IN EFI_DRIVER_BINDING_PROTOCOL    *This,
  IN EFI_HANDLE                     Controller,
  IN EFI_DEVICE_PATH_PROTOCOL       *RemainingDevicePath
  )
/*++

Routine Description:

Arguments:

Returns:

--*/
// GC_TODO:    This - add argument and description to function comment
// GC_TODO:    Controller - add argument and description to function comment
// GC_TODO:    RemainingDevicePath - add argument and description to function comment
// GC_TODO:    EFI_INVALID_PARAMETER - add return value to function comment
{
  EFI_STATUS                                Status;
  EFI_STATUS                                Status1;
  EFI_INTERFACE_DEFINITION_FOR_ISA_IO       *IsaIo;
  KEYBOARD_CONSOLE_IN_DEV                   *ConsoleIn;
  UINT8                                     Data;
  EFI_STATUS_CODE_VALUE                     StatusCode;
  EFI_DEVICE_PATH_PROTOCOL                  *ParentDevicePath;

  StatusCode = 0;

  Status = gBS->OpenProtocol (
                  Controller,
                  &gEfiDevicePathProtocolGuid,
                  (VOID **) &ParentDevicePath,
                  This->DriverBindingHandle,
                  Controller,
                  EFI_OPEN_PROTOCOL_BY_DRIVER
                  );
  if (EFI_ERROR (Status)) {
    return Status;
  }
  //
  // Report that the keyboard is being enabled
  //
  ReportStatusCodeWithDevicePath (
    EFI_PROGRESS_CODE,
    EFI_PERIPHERAL_KEYBOARD | EFI_P_PC_ENABLE,
    0,
    &gEfiCallerIdGuid,
    ParentDevicePath
    );

  //
  // Get the ISA I/O Protocol on Controller's handle
  //
  Status = gBS->OpenProtocol (
                  Controller,
                  EFI_ISA_IO_PROTOCOL_VERSION,
                  (VOID **) &IsaIo,
                  This->DriverBindingHandle,
                  Controller,
                  EFI_OPEN_PROTOCOL_BY_DRIVER
                  );
  if (EFI_ERROR (Status)) {
    gBS->CloseProtocol (
           Controller,
           &gEfiDevicePathProtocolGuid,
           This->DriverBindingHandle,
           Controller
           );
    return EFI_INVALID_PARAMETER;
  }
  //
  // Allocate private data
  //
  ConsoleIn = EfiLibAllocateZeroPool (sizeof (KEYBOARD_CONSOLE_IN_DEV));
  if (ConsoleIn == NULL) {
    Status      = EFI_OUT_OF_RESOURCES;
    StatusCode  = EFI_PERIPHERAL_KEYBOARD | EFI_P_EC_CONTROLLER_ERROR;
    goto ErrorExit;
  }
  //
  // Setup the device instance
  //
  ConsoleIn->Signature              = KEYBOARD_CONSOLE_IN_DEV_SIGNATURE;
  ConsoleIn->Handle                 = Controller;
  (ConsoleIn->ConIn).Reset          = KeyboardEfiReset;
  (ConsoleIn->ConIn).ReadKeyStroke  = KeyboardReadKeyStroke;
  ConsoleIn->DataRegisterAddress    = KEYBOARD_8042_DATA_REGISTER;
  ConsoleIn->StatusRegisterAddress  = KEYBOARD_8042_STATUS_REGISTER;
  ConsoleIn->CommandRegisterAddress = KEYBOARD_8042_COMMAND_REGISTER;
  ConsoleIn->IsaIo                  = IsaIo;
  ConsoleIn->ScancodeBufStartPos    = 0;
  ConsoleIn->ScancodeBufEndPos      = KEYBOARD_BUFFER_MAX_COUNT - 1;
  ConsoleIn->ScancodeBufCount       = 0;
  ConsoleIn->Ctrled                 = FALSE;
  ConsoleIn->Alted                  = FALSE;
  ConsoleIn->DevicePath             = ParentDevicePath;

#if (EFI_SPECIFICATION_VERSION >= 0x0002000A)
#ifndef DISABLE_CONSOLE_EX
  ConsoleIn->ConInEx.Reset               = KeyboardEfiResetEx;
  ConsoleIn->ConInEx.ReadKeyStrokeEx     = KeyboardReadKeyStrokeEx;
  ConsoleIn->ConInEx.SetState            = KeyboardSetState;
  ConsoleIn->ConInEx.RegisterKeyNotify   = KeyboardRegisterKeyNotify;
  ConsoleIn->ConInEx.UnregisterKeyNotify = KeyboardUnregisterKeyNotify;  
  
  InitializeListHead (&ConsoleIn->NotifyList);
#endif  // DISABLE_CONSOLE_EX
#endif

  //
  // Fix for random hangs in System waiting for the Key if no KBC is present in BIOS.
  //
  KeyboardRead (ConsoleIn, &Data);
  if ((KeyReadStatusRegister (ConsoleIn) & (KBC_PARE | KBC_TIM)) == (KBC_PARE | KBC_TIM)) {
    //
    // If nobody decodes KBC I/O port, it will read back as 0xFF.
    // Check the Time-Out and Parity bit to see if it has an active KBC in system
    //
    Status      = EFI_DEVICE_ERROR;
    StatusCode  = EFI_PERIPHERAL_KEYBOARD | EFI_P_EC_NOT_DETECTED;
    goto ErrorExit;
  }

  //
  // Setup the WaitForKey event
  //
  Status = gBS->CreateEvent (
                  EFI_EVENT_NOTIFY_WAIT,
                  EFI_TPL_NOTIFY,
                  KeyboardWaitForKey,
                  &(ConsoleIn->ConIn),
                  &((ConsoleIn->ConIn).WaitForKey)
                  );
  if (EFI_ERROR (Status)) {
    Status      = EFI_OUT_OF_RESOURCES;
    StatusCode  = EFI_PERIPHERAL_KEYBOARD | EFI_P_EC_CONTROLLER_ERROR;
    goto ErrorExit;
  }
#if (EFI_SPECIFICATION_VERSION >= 0x0002000A)
#ifndef DISABLE_CONSOLE_EX  
  //
  // Setup the WaitForKeyEx event
  //  
  Status = gBS->CreateEvent (
                  EFI_EVENT_NOTIFY_WAIT,
                  EFI_TPL_NOTIFY,
                  KeyboardWaitForKeyEx,
                  &(ConsoleIn->ConInEx),
                  &(ConsoleIn->ConInEx.WaitForKeyEx)
                  );
  if (EFI_ERROR (Status)) {
    Status      = EFI_OUT_OF_RESOURCES;
    StatusCode  = EFI_PERIPHERAL_KEYBOARD | EFI_P_EC_CONTROLLER_ERROR;
    goto ErrorExit;
  }
#endif  // DISABLE_CONSOLE_EX
#endif
  //
  // Setup a periodic timer, used for reading keystrokes at a fixed interval
  //
  Status = gBS->CreateEvent (
                  EFI_EVENT_TIMER | EFI_EVENT_NOTIFY_SIGNAL,
                  EFI_TPL_NOTIFY,
                  KeyboardTimerHandler,
                  ConsoleIn,
                  &ConsoleIn->TimerEvent
                  );
  if (EFI_ERROR (Status)) {
    Status      = EFI_OUT_OF_RESOURCES;
    StatusCode  = EFI_PERIPHERAL_KEYBOARD | EFI_P_EC_CONTROLLER_ERROR;
    goto ErrorExit;
  }

  Status = gBS->SetTimer (
                  ConsoleIn->TimerEvent,
                  TimerPeriodic,
                  KEYBOARD_TIMER_INTERVAL
                  );
  if (EFI_ERROR (Status)) {
    Status      = EFI_OUT_OF_RESOURCES;
    StatusCode  = EFI_PERIPHERAL_KEYBOARD | EFI_P_EC_CONTROLLER_ERROR;
    goto ErrorExit;
  }

  ReportStatusCodeWithDevicePath (
    EFI_PROGRESS_CODE,
    EFI_PERIPHERAL_KEYBOARD | EFI_P_PC_PRESENCE_DETECT,
    0,
    &gEfiCallerIdGuid,
    ParentDevicePath
    );

  //
  // Reset the keyboard device
  //
  Status = ConsoleIn->ConIn.Reset (&ConsoleIn->ConIn, TRUE);
  if (EFI_ERROR (Status)) {
    Status      = EFI_DEVICE_ERROR;
    StatusCode  = EFI_PERIPHERAL_KEYBOARD | EFI_P_EC_NOT_DETECTED;
    goto ErrorExit;
  }

  ConsoleIn->ControllerNameTable = NULL;
  EfiLibAddUnicodeString (
    LANGUAGE_CODE_ENGLISH,
    gPs2KeyboardComponentName.SupportedLanguages,
    &ConsoleIn->ControllerNameTable,
    L"PS/2 Keyboard Device"
    );

  //
  // Install protocol interfaces for the keyboard device.
  //
  Status = gBS->InstallMultipleProtocolInterfaces (
                  &Controller,
                  &gEfiSimpleTextInProtocolGuid,
                  &ConsoleIn->ConIn,
#if (EFI_SPECIFICATION_VERSION >= 0x0002000A)
#ifndef DISABLE_CONSOLE_EX  
                  &gEfiSimpleTextInputExProtocolGuid,
                  &ConsoleIn->ConInEx,
#endif  // DISABLE_CONSOLE_EX
#endif
                  NULL
                  );
  if (EFI_ERROR (Status)) {
    StatusCode = EFI_PERIPHERAL_KEYBOARD | EFI_P_EC_CONTROLLER_ERROR;
    goto ErrorExit;
  }

  return Status;

ErrorExit:
  //
  // Report error code
  //
  if (StatusCode != 0) {
    ReportStatusCodeWithDevicePath (
      EFI_ERROR_CODE | EFI_ERROR_MINOR,
      StatusCode,
      0,
      &gEfiCallerIdGuid,
      ParentDevicePath
      );
  }

  if ((ConsoleIn != NULL) && (ConsoleIn->ConIn.WaitForKey != NULL)) {
    gBS->CloseEvent (ConsoleIn->ConIn.WaitForKey);
  }

  if ((ConsoleIn != NULL) && (ConsoleIn->TimerEvent != NULL)) {
    gBS->CloseEvent (ConsoleIn->TimerEvent);
  }
#if (EFI_SPECIFICATION_VERSION >= 0x0002000A)
#ifndef DISABLE_CONSOLE_EX  
  if ((ConsoleIn != NULL) && (ConsoleIn->ConInEx.WaitForKeyEx != NULL)) {
    gBS->CloseEvent (ConsoleIn->ConInEx.WaitForKeyEx);
  }
  KbdFreeNotifyList (&ConsoleIn->NotifyList);
#endif  // DISABLE_CONSOLE_EX
#endif
  if ((ConsoleIn != NULL) && (ConsoleIn->ControllerNameTable != NULL)) {
    EfiLibFreeUnicodeStringTable (ConsoleIn->ControllerNameTable);
  }
  //
  // Since there will be no timer handler for keyboard input any more,
  // exhaust input data just in case there is still keyboard data left
  //
  Status1 = EFI_SUCCESS;
  while (!EFI_ERROR (Status1) && (Status != EFI_DEVICE_ERROR)) {
    Status1 = KeyboardRead (ConsoleIn, &Data);;
  }

  if (ConsoleIn != NULL) {
    gBS->FreePool (ConsoleIn);
  }

  gBS->CloseProtocol (
         Controller,
         &gEfiDevicePathProtocolGuid,
         This->DriverBindingHandle,
         Controller
         );

  gBS->CloseProtocol (
         Controller,
         EFI_ISA_IO_PROTOCOL_VERSION,
         This->DriverBindingHandle,
         Controller
         );

  return Status;
}

EFI_STATUS
EFIAPI
KbdControllerDriverStop (
  IN  EFI_DRIVER_BINDING_PROTOCOL    *This,
  IN  EFI_HANDLE                     Controller,
  IN  UINTN                          NumberOfChildren,
  IN  EFI_HANDLE                     *ChildHandleBuffer
  )
/*++

  Routine Description:

  Arguments:

  Returns:

--*/
// GC_TODO:    This - add argument and description to function comment
// GC_TODO:    Controller - add argument and description to function comment
// GC_TODO:    NumberOfChildren - add argument and description to function comment
// GC_TODO:    ChildHandleBuffer - add argument and description to function comment
// GC_TODO:    EFI_SUCCESS - add return value to function comment
{
  EFI_STATUS                  Status;
  EFI_SIMPLE_TEXT_IN_PROTOCOL *ConIn;
  KEYBOARD_CONSOLE_IN_DEV     *ConsoleIn;
  UINT8                       Data;

  //
  // Disable Keyboard
  //
  Status = gBS->OpenProtocol (
                  Controller,
                  &gEfiSimpleTextInProtocolGuid,
                  (VOID **) &ConIn,
                  This->DriverBindingHandle,
                  Controller,
                  EFI_OPEN_PROTOCOL_GET_PROTOCOL
                  );
  if (EFI_ERROR (Status)) {
    return Status;
  }
#if (EFI_SPECIFICATION_VERSION >= 0x0002000A)
#ifndef DISABLE_CONSOLE_EX
  Status = gBS->OpenProtocol (
                  Controller,
                  &gEfiSimpleTextInputExProtocolGuid,
                  NULL,
                  This->DriverBindingHandle,
                  Controller,
                  EFI_OPEN_PROTOCOL_TEST_PROTOCOL
                  );
  if (EFI_ERROR (Status)) {
    return Status;
  }
  
#endif  // DISABLE_CONSOLE_EX
#endif
  ConsoleIn = KEYBOARD_CONSOLE_IN_DEV_FROM_THIS (ConIn);

  //
  // Report that the keyboard is being disabled
  //
  ReportStatusCodeWithDevicePath (
    EFI_PROGRESS_CODE,
    EFI_PERIPHERAL_KEYBOARD | EFI_P_PC_DISABLE,
    0,
    &gEfiCallerIdGuid,
    ConsoleIn->DevicePath
    );

  if (ConsoleIn->TimerEvent) {
    gBS->CloseEvent (ConsoleIn->TimerEvent);
    ConsoleIn->TimerEvent = NULL;
  }
  //
  // Disable the keyboard interface
  //
  Status = DisableKeyboard (ConsoleIn);

  //
  // Since there will be no timer handler for keyboard input any more,
  // exhaust input data just in case there is still keyboard data left
  //
  Status = EFI_SUCCESS;
  while (!EFI_ERROR (Status)) {
    Status = KeyboardRead (ConsoleIn, &Data);;
  }

  //
  // Uninstall the SimpleTextIn and SimpleTextInEx protocols
  //
  Status = gBS->UninstallMultipleProtocolInterfaces (
                  Controller,
                  &gEfiSimpleTextInProtocolGuid,
                  &ConsoleIn->ConIn,
#if (EFI_SPECIFICATION_VERSION >= 0x0002000A)
#ifndef DISABLE_CONSOLE_EX
                  &gEfiSimpleTextInputExProtocolGuid,
                  &ConsoleIn->ConInEx,
#endif  // DISABLE_CONSOLE_EX
#endif
                  NULL
                  );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  gBS->CloseProtocol (
         Controller,
         &gEfiDevicePathProtocolGuid,
         This->DriverBindingHandle,
         Controller
         );

  gBS->CloseProtocol (
         Controller,
         EFI_ISA_IO_PROTOCOL_VERSION,
         This->DriverBindingHandle,
         Controller
         );

  //
  // Free other resources
  //
  if ((ConsoleIn->ConIn).WaitForKey) {
    gBS->CloseEvent ((ConsoleIn->ConIn).WaitForKey);
    (ConsoleIn->ConIn).WaitForKey = NULL;
  }
#if (EFI_SPECIFICATION_VERSION >= 0x0002000A)
#ifndef DISABLE_CONSOLE_EX
  if (ConsoleIn->ConInEx.WaitForKeyEx != NULL) {
    gBS->CloseEvent (ConsoleIn->ConInEx.WaitForKeyEx);
    ConsoleIn->ConInEx.WaitForKeyEx = NULL;
  }
  KbdFreeNotifyList (&ConsoleIn->NotifyList);
#endif  // DISABLE_CONSOLE_EX
#endif
  EfiLibFreeUnicodeStringTable (ConsoleIn->ControllerNameTable);
  gBS->FreePool (ConsoleIn);

  return EFI_SUCCESS;
}

#if (EFI_SPECIFICATION_VERSION >= 0x0002000A)
#ifndef DISABLE_CONSOLE_EX
STATIC
EFI_STATUS
KbdFreeNotifyList (
  IN OUT EFI_LIST_ENTRY       *ListHead
  )
/*++

Routine Description:

Arguments:

  ListHead   - The list head

Returns:

  EFI_SUCCESS           - Free the notify list successfully
  EFI_INVALID_PARAMETER - ListHead is invalid.

--*/
{
  KEYBOARD_CONSOLE_IN_EX_NOTIFY *NotifyNode;

  if (ListHead == NULL) {
    return EFI_INVALID_PARAMETER;
  }
  while (!IsListEmpty (ListHead)) {
    NotifyNode = CR (
                   ListHead->ForwardLink, 
                   KEYBOARD_CONSOLE_IN_EX_NOTIFY, 
                   NotifyEntry, 
                   KEYBOARD_CONSOLE_IN_EX_NOTIFY_SIGNATURE
                   );
    RemoveEntryList (ListHead->ForwardLink);
    EfiLibSafeFreePool (NotifyNode);
  }
  
  return EFI_SUCCESS;
}
#endif  // DISABLE_CONSOLE_EX
#endif

