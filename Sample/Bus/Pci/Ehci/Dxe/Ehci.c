/*++

Copyright (c) 2006 - 2009, Intel Corporation
All rights reserved. This program and the accompanying materials
are licensed and made available under the terms and conditions of the BSD License
which accompanies this distribution.  The full text of the license may be found at
http://opensource.org/licenses/bsd-license.php

THE PROGRAM IS DISTRIBUTED UNDER THE BSD LICENSE ON AN "AS IS" BASIS,
WITHOUT WARRANTIES OR REPRESENTATIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED.

Module Name:

    Ehci.c

Abstract:


Revision History
--*/

#include "Ehci.h"

//
// Two arrays used to translate the EHCI port state (change)
// to the UEFI protocol's port state (change).
//
USB_PORT_STATE_MAP  mUsbPortStateMap[] = {
  {PORTSC_CONN,     USB_PORT_STAT_CONNECTION},
  {PORTSC_ENABLED,  USB_PORT_STAT_ENABLE},
  {PORTSC_SUSPEND,  USB_PORT_STAT_SUSPEND},
  {PORTSC_OVERCUR,  USB_PORT_STAT_OVERCURRENT},
  {PORTSC_RESET,    USB_PORT_STAT_RESET},
  {PORTSC_POWER,    USB_PORT_STAT_POWER},
  {PORTSC_OWNER,    USB_PORT_STAT_OWNER}
};

USB_PORT_STATE_MAP  mUsbPortChangeMap[] = {
  {PORTSC_CONN_CHANGE,    USB_PORT_STAT_C_CONNECTION},
  {PORTSC_ENABLE_CHANGE,  USB_PORT_STAT_C_ENABLE},
  {PORTSC_OVERCUR_CHANGE, USB_PORT_STAT_C_OVERCURRENT}
};

STATIC
EFI_STATUS
EFIAPI
EhcGetCapability (
  IN  EFI_USB2_HC_PROTOCOL  *This,
  OUT UINT8                 *MaxSpeed,
  OUT UINT8                 *PortNumber,
  OUT UINT8                 *Is64BitCapable
  )
/*++

  Routine Description:
    Retrieves the capablility of root hub ports.

  Arguments:
    This            - This EFI_USB_HC_PROTOCOL instance.
    MaxSpeed        - Max speed supported by the controller
    PortNumber      - Number of the root hub ports.
    Is64BitCapable  - Whether the controller supports 64-bit memory addressing.
    
  Returns:
    EFI_SUCCESS           : host controller capability were retrieved successfully.
    EFI_INVALID_PARAMETER : Either of the three capability pointer is NULL

--*/
{
  USB2_HC_DEV             *Ehc;
  EFI_TPL                 OldTpl;

  if ((MaxSpeed == NULL) || (PortNumber == NULL) || (Is64BitCapable == NULL)) {
    return EFI_INVALID_PARAMETER;
  }

  OldTpl          = gBS->RaiseTPL (EHC_TPL);
  Ehc             = EHC_FROM_THIS (This);

  *MaxSpeed       = EFI_USB_SPEED_HIGH;
  *PortNumber     = (UINT8) (Ehc->HcStructParams & HCSP_NPORTS);
  *Is64BitCapable = (UINT8) (Ehc->HcCapParams & HCCP_64BIT);

  EHC_DEBUG (("EhcGetCapability: %d ports, 64 bit %d\n", *PortNumber, *Is64BitCapable));

  gBS->RestoreTPL (OldTpl);
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
EhcReset (
  IN EFI_USB2_HC_PROTOCOL *This,
  IN UINT16               Attributes
  )
/*++

  Routine Description:
    Provides software reset for the USB host controller.

  Arguments:
  
    This        - This EFI_USB2_HC_PROTOCOL instance.
    Attributes  - A bit mask of the reset operation to perform.
                
  Returns:
    EFI_SUCCESS           : The reset operation succeeded.
    EFI_INVALID_PARAMETER : Attributes is not valid.
    EFI_UNSUPPOURTED      : The type of reset specified by Attributes is 
                            not currently supported by the host controller.                          
    EFI_DEVICE_ERROR      : Host controller isn't halted to reset.

--*/
{
  USB2_HC_DEV             *Ehc;
  EFI_TPL                 OldTpl;
  EFI_STATUS              Status;

  OldTpl  = gBS->RaiseTPL (EHC_TPL);
  Ehc     = EHC_FROM_THIS (This);

  switch (Attributes) {
  case EFI_USB_HC_RESET_GLOBAL:
  //
  // Flow through, same behavior as Host Controller Reset
  //
  case EFI_USB_HC_RESET_HOST_CONTROLLER:
    //
    // Host Controller must be Halt when Reset it
    //
    if (!EhcIsHalt (Ehc)) {
      Status = EhcHaltHC (Ehc, EHC_GENERIC_TIMEOUT);

      if (EFI_ERROR (Status)) {
        Status = EFI_DEVICE_ERROR;
        goto ON_EXIT;
      }
    }
    
    //
    // Clean up the asynchronous transfers, currently only
    // interrupt supports asynchronous operation.
    //
    EhciDelAllAsyncIntTransfers (Ehc);
    EhcAckAllInterrupt (Ehc);
    EhcFreeSched (Ehc);

    Status = EhcResetHC (Ehc, EHC_RESET_TIMEOUT);

    if (EFI_ERROR (Status)) {
      goto ON_EXIT;
    }

    Status = EhcInitHC (Ehc);
    break;

  case EFI_USB_HC_RESET_GLOBAL_WITH_DEBUG:
  case EFI_USB_HC_RESET_HOST_WITH_DEBUG:
    Status = EFI_UNSUPPORTED;
    break;

  default:
    Status = EFI_INVALID_PARAMETER;
  }

ON_EXIT:
  EHC_DEBUG (("EhcReset: exit status %r\n", Status));
  gBS->RestoreTPL (OldTpl);
  return Status;
}

STATIC
EFI_STATUS
EFIAPI
EhcGetState (
  IN  EFI_USB2_HC_PROTOCOL  *This,
  OUT EFI_USB_HC_STATE      *State
  )
/*++

  Routine Description:
    Retrieve the current state of the USB host controller.

  Arguments:
    This    - This EFI_USB2_HC_PROTOCOL instance.
    State   - Variable to return the current host controller state.

  Returns:
    EFI_SUCCESS           : Host controller state was returned in State.
    EFI_INVALID_PARAMETER : State is NULL.
    EFI_DEVICE_ERROR      : An error was encountered while attempting to 
                            retrieve the host controller's current state.
--*/
{
  EFI_TPL                 OldTpl;
  USB2_HC_DEV             *Ehc;

  if (State == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  OldTpl  = gBS->RaiseTPL (EHC_TPL);
  Ehc     = EHC_FROM_THIS (This);

  if (EHC_REG_BIT_IS_SET (Ehc, EHC_USBSTS_OFFSET, USBSTS_HALT)) {
    *State = EfiUsbHcStateHalt;
  } else {
    *State = EfiUsbHcStateOperational;
  }

  gBS->RestoreTPL (OldTpl);

  EHC_DEBUG (("EhcGetState: current state %d\n", *State));
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
EhcSetState (
  IN EFI_USB2_HC_PROTOCOL *This,
  IN EFI_USB_HC_STATE     State
  )
/*++

  Routine Description:

    Sets the USB host controller to a specific state.

  Arguments:

    This     - This EFI_USB2_HC_PROTOCOL instance.
    State    - The state of the host controller that will be set.

  Returns:

    EFI_SUCCESS           : The USB host controller was successfully placed
                            in the state specified by State.
    EFI_INVALID_PARAMETER : State is invalid.
    EFI_DEVICE_ERROR      : Failed to set the state due to device error.

--*/
{
  USB2_HC_DEV             *Ehc;
  EFI_TPL                 OldTpl;
  EFI_STATUS              Status;
  EFI_USB_HC_STATE        CurState;

  Status = EhcGetState (This, &CurState);

  if (EFI_ERROR (Status)) {
    return EFI_DEVICE_ERROR;
  }

  if (CurState == State) {
    return EFI_SUCCESS;
  }

  OldTpl  = gBS->RaiseTPL (EHC_TPL);
  Ehc     = EHC_FROM_THIS (This);

  switch (State) {
  case EfiUsbHcStateHalt:
    Status = EhcHaltHC (Ehc, EHC_GENERIC_TIMEOUT);
    break;

  case EfiUsbHcStateOperational:
    if (EHC_REG_BIT_IS_SET (Ehc, EHC_USBSTS_OFFSET, USBSTS_SYS_ERROR)) {
      Status = EFI_DEVICE_ERROR;
      break;
    }

    //
    // Software must not write a one to this field unless the host controller
    // is in the Halted state. Doing so will yield undefined results. 
    // refers to Spec[EHCI1.0-2.3.1]
    // 
    if (!EHC_REG_BIT_IS_SET (Ehc, EHC_USBSTS_OFFSET, USBSTS_HALT)) {
      Status = EFI_DEVICE_ERROR;
      break;
    }

    Status = EhcRunHC (Ehc, EHC_GENERIC_TIMEOUT);
    break;

  case EfiUsbHcStateSuspend:
    Status = EFI_UNSUPPORTED;
    break;

  default:
    Status = EFI_INVALID_PARAMETER;
  }

  EHC_DEBUG (("EhcSetState: exit status %r\n", Status));
  gBS->RestoreTPL (OldTpl);
  return Status;
}

STATIC
EFI_STATUS
EFIAPI
EhcGetRootHubPortStatus (
  IN  EFI_USB2_HC_PROTOCOL  *This,
  IN  UINT8                 PortNumber,
  OUT EFI_USB_PORT_STATUS   *PortStatus
  )
/*++

  Routine Description:
    Retrieves the current status of a USB root hub port.

  Arguments:
    This        - This EFI_USB2_HC_PROTOCOL instance.
    PortNumber  - The root hub port to retrieve the state from.  
                  This value is zero-based. 
    PortStatus  - Variable to receive the port state

  Returns:
    EFI_SUCCESS           : The status of the USB root hub port specified
                            by PortNumber was returned in PortStatus.
    EFI_INVALID_PARAMETER : PortNumber is invalid.
    EFI_DEVICE_ERROR      : Can't read register

--*/
{
  USB2_HC_DEV             *Ehc;
  EFI_TPL                 OldTpl;
  UINT32                  Offset;
  UINT32                  State;
  UINT32                  TotalPort;
  UINTN                   Index;
  UINTN                   MapSize;
  EFI_STATUS              Status;

  if (PortStatus == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  OldTpl    = gBS->RaiseTPL (EHC_TPL);

  Ehc       = EHC_FROM_THIS (This);
  Status    = EFI_SUCCESS;

  TotalPort = (Ehc->HcStructParams & HCSP_NPORTS);

  if (PortNumber >= TotalPort) {
    Status = EFI_INVALID_PARAMETER;
    goto ON_EXIT;
  }

  Offset                        = (UINT32) (EHC_PORT_STAT_OFFSET + (4 * PortNumber));
  PortStatus->PortStatus        = 0;
  PortStatus->PortChangeStatus  = 0;

  State                         = EhcReadOpReg (Ehc, Offset);

  //
  // Identify device speed. If in K state, it is low speed.
  // If the port is enabled after reset, the device is of 
  // high speed. The USB bus driver should retrieve the actual
  // port speed after reset. 
  //
  if (EHC_BIT_IS_SET (State, PORTSC_LINESTATE_K)) {
    PortStatus->PortStatus |= USB_PORT_STAT_LOW_SPEED;

  } else if (EHC_BIT_IS_SET (State, PORTSC_ENABLED)) {
    PortStatus->PortStatus |= USB_PORT_STAT_HIGH_SPEED;
  }
  
  //
  // Convert the EHCI port/port change state to UEFI status
  //
  MapSize = sizeof (mUsbPortStateMap) / sizeof (USB_PORT_STATE_MAP);

  for (Index = 0; Index < MapSize; Index++) {
    if (EHC_BIT_IS_SET (State, mUsbPortStateMap[Index].HwState)) {
      PortStatus->PortStatus |= mUsbPortStateMap[Index].UefiState;
    }
  }

  MapSize = sizeof (mUsbPortChangeMap) / sizeof (USB_PORT_STATE_MAP);

  for (Index = 0; Index < MapSize; Index++) {
    if (EHC_BIT_IS_SET (State, mUsbPortChangeMap[Index].HwState)) {
      PortStatus->PortChangeStatus |= mUsbPortChangeMap[Index].UefiState;
    }
  }

ON_EXIT:
  gBS->RestoreTPL (OldTpl);
  return Status;
}

STATIC
EFI_STATUS
EFIAPI
EhcSetRootHubPortFeature (
  IN  EFI_USB2_HC_PROTOCOL  *This,
  IN  UINT8                 PortNumber,
  IN  EFI_USB_PORT_FEATURE  PortFeature
  )
/*++

  Routine Description:

    Sets a feature for the specified root hub port.

  Arguments:

    This        - This EFI_USB2_HC_PROTOCOL instance.
    PortNumber  - Root hub port to set.
    PortFeature - Feature to set

  Returns:

    EFI_SUCCESS           : The feature specified by PortFeature was set 
    EFI_INVALID_PARAMETER : PortNumber is invalid or PortFeature is invalid.
    EFI_DEVICE_ERROR      : Can't read register
--*/
{
  USB2_HC_DEV             *Ehc;
  EFI_TPL                 OldTpl;
  UINT32                  Offset;
  UINT32                  State;
  UINT32                  TotalPort;
  EFI_STATUS              Status;

  OldTpl    = gBS->RaiseTPL (EHC_TPL);
  Ehc       = EHC_FROM_THIS (This);
  Status    = EFI_SUCCESS;

  TotalPort = (Ehc->HcStructParams & HCSP_NPORTS);

  if (PortNumber >= TotalPort) {
    Status = EFI_INVALID_PARAMETER;
    goto ON_EXIT;
  }

  Offset  = (UINT32) (EHC_PORT_STAT_OFFSET + (4 * PortNumber));
  State   = EhcReadOpReg (Ehc, Offset);

  //
  // Mask off the port status change bits, these bits are
  // write clean bit
  //
  State &= ~PORTSC_CHANGE_MASK;

  switch (PortFeature) {
  case EfiUsbPortEnable:
    //
    // Sofeware can't set this bit, Port can only be enable by
    // EHCI as a part of the reset and enable
    //
    State |= PORTSC_ENABLED;
    EhcWriteOpReg (Ehc, Offset, State);
    break;

  case EfiUsbPortSuspend:
    State |= PORTSC_SUSPEND;
    EhcWriteOpReg (Ehc, Offset, State);
    break;

  case EfiUsbPortReset:
    //
    // Make sure Host Controller not halt before reset it
    //
    if (EhcIsHalt (Ehc)) {
      Status = EhcRunHC (Ehc, EHC_GENERIC_TIMEOUT);

      if (EFI_ERROR (Status)) {
        EHC_DEBUG (("EhcSetRootHubPortFeature :failed to start HC - %r\n", Status));
        break;
      }
    }
    
    //
    // Set one to PortReset bit must also set zero to PortEnable bit
    //
    State |= PORTSC_RESET;
    State &= ~PORTSC_ENABLED;
    EhcWriteOpReg (Ehc, Offset, State);
    break;

  case EfiUsbPortPower:
    //
    // Not supported, ignore the operation
    //
    Status = EFI_SUCCESS;
    break;

  case EfiUsbPortOwner:
    State |= PORTSC_OWNER;
    EhcWriteOpReg (Ehc, Offset, State);
    break;

  default:
    Status = EFI_INVALID_PARAMETER;
  }

ON_EXIT:
  EHC_DEBUG (("EhcSetRootHubPortFeature: exit status %r\n", Status));

  gBS->RestoreTPL (OldTpl);
  return Status;
}

STATIC
EFI_STATUS
EFIAPI
EhcClearRootHubPortFeature (
  IN  EFI_USB2_HC_PROTOCOL  *This,
  IN  UINT8                 PortNumber,
  IN  EFI_USB_PORT_FEATURE  PortFeature
  )
/*++

  Routine Description:

    Clears a feature for the specified root hub port.

  Arguments:

    This        - A pointer to the EFI_USB2_HC_PROTOCOL instance.
    PortNumber  - Specifies the root hub port whose feature
                  is requested to be cleared.
    PortFeature - Indicates the feature selector associated with the
                  feature clear request.

  Returns:

    EFI_SUCCESS           : The feature specified by PortFeature was cleared 
                            for the USB root hub port specified by PortNumber.
    EFI_INVALID_PARAMETER : PortNumber is invalid or PortFeature is invalid.
    EFI_DEVICE_ERROR      : Can't read register

--*/
{
  USB2_HC_DEV             *Ehc;
  EFI_TPL                 OldTpl;
  UINT32                  Offset;
  UINT32                  State;
  UINT32                  TotalPort;
  EFI_STATUS              Status;

  OldTpl    = gBS->RaiseTPL (EHC_TPL);
  Ehc       = EHC_FROM_THIS (This);
  Status    = EFI_SUCCESS;

  TotalPort = (Ehc->HcStructParams & HCSP_NPORTS);

  if (PortNumber >= TotalPort) {
    Status = EFI_INVALID_PARAMETER;
    goto ON_EXIT;
  }

  Offset  = EHC_PORT_STAT_OFFSET + (4 * PortNumber);
  State   = EhcReadOpReg (Ehc, Offset);
  State &= ~PORTSC_CHANGE_MASK;

  switch (PortFeature) {
  case EfiUsbPortEnable:
    //
    // Clear PORT_ENABLE feature means disable port.
    //
    State &= ~PORTSC_ENABLED;
    EhcWriteOpReg (Ehc, Offset, State);
    break;

  case EfiUsbPortSuspend:
    //
    // A write of zero to this bit is ignored by the host
    // controller. The host controller will unconditionally
    // set this bit to a zero when:
    //   1. software sets the Forct Port Resume bit to a zero from a one.
    //   2. software sets the Port Reset bit to a one frome a zero.
    //
    State &= ~PORSTSC_RESUME;
    EhcWriteOpReg (Ehc, Offset, State);
    break;

  case EfiUsbPortReset:
    //
    // Clear PORT_RESET means clear the reset signal.
    //
    State &= ~PORTSC_RESET;
    EhcWriteOpReg (Ehc, Offset, State);
    break;

  case EfiUsbPortOwner:
    //
    // Clear port owner means this port owned by EHC
    //
    State &= ~PORTSC_OWNER;
    EhcWriteOpReg (Ehc, Offset, State);
    break;

  case EfiUsbPortConnectChange:
    //
    // Clear connect status change
    //
    State |= PORTSC_CONN_CHANGE;
    EhcWriteOpReg (Ehc, Offset, State);
    break;

  case EfiUsbPortEnableChange:
    //
    // Clear enable status change
    //
    State |= PORTSC_ENABLE_CHANGE;
    EhcWriteOpReg (Ehc, Offset, State);
    break;

  case EfiUsbPortOverCurrentChange:
    //
    // Clear PortOverCurrent change
    //
    State |= PORTSC_OVERCUR_CHANGE;
    EhcWriteOpReg (Ehc, Offset, State);
    break;

  case EfiUsbPortPower:
  case EfiUsbPortSuspendChange:
  case EfiUsbPortResetChange:
    //
    // Not supported or not related operation
    //
    break;

  default:
    Status = EFI_INVALID_PARAMETER;
    break;
  }

ON_EXIT:
  EHC_DEBUG (("EhcClearRootHubPortFeature: exit status %r\n", Status));
  gBS->RestoreTPL (OldTpl);
  return Status;
}

STATIC
EFI_STATUS
EFIAPI
EhcControlTransfer (
  IN  EFI_USB2_HC_PROTOCOL                *This,
  IN  UINT8                               DeviceAddress,
  IN  UINT8                               DeviceSpeed,
  IN  UINTN                               MaximumPacketLength,
  IN  EFI_USB_DEVICE_REQUEST              *Request,
  IN  EFI_USB_DATA_DIRECTION              TransferDirection,
  IN  OUT VOID                            *Data,
  IN  OUT UINTN                           *DataLength,
  IN  UINTN                               TimeOut,
  IN  EFI_USB2_HC_TRANSACTION_TRANSLATOR  *Translator,
  OUT UINT32                              *TransferResult
  )
/*++

  Routine Description:

    Submits control transfer to a target USB device.

  Arguments:

    This                - This EFI_USB2_HC_PROTOCOL instance.
    DeviceAddress       - The target device address
    DeviceSpeed         - Target device speed.
    MaximumPacketLength - Maximum packet size the default control transfer 
                          endpoint is capable of sending or receiving.
    Request             - USB device request to send
    TransferDirection   - Specifies the data direction for the data stage
    Data                - Data buffer to be transmitted or received from USB device.
    DataLength          - The size (in bytes) of the data buffer
    TimeOut             - Indicates the maximum timeout, in millisecond,
    Translator          - Transaction translator to be used by this device.
    TransferResult      - Return the result of this control transfer.

  Returns:

    EFI_SUCCESS           : Transfer was completed successfully.
    EFI_OUT_OF_RESOURCES  : The transfer failed due to lack of resources.
    EFI_INVALID_PARAMETER : Some parameters are invalid.
    EFI_TIMEOUT           : Transfer failed due to timeout.
    EFI_DEVICE_ERROR      : Transfer failed due to host controller or device error.

--*/
{
  USB2_HC_DEV             *Ehc;
  URB                     *Urb;
  EFI_TPL                 OldTpl;
  UINT8                   Endpoint;
  EFI_STATUS              Status;

  //
  // Validate parameters
  //
  if ((Request == NULL) || (TransferResult == NULL)) {
    return EFI_INVALID_PARAMETER;
  }

  if ((TransferDirection != EfiUsbDataIn) &&
      (TransferDirection != EfiUsbDataOut) &&
      (TransferDirection != EfiUsbNoData)) {
    return EFI_INVALID_PARAMETER;
  }

  if ((TransferDirection == EfiUsbNoData) && 
      ((Data != NULL) || (*DataLength != 0))) {
    return EFI_INVALID_PARAMETER;
  }

  if ((TransferDirection != EfiUsbNoData) && 
     ((Data == NULL) || (*DataLength == 0))) {
    return EFI_INVALID_PARAMETER;
  }

  if ((MaximumPacketLength != 8)  && (MaximumPacketLength != 16) &&
      (MaximumPacketLength != 32) && (MaximumPacketLength != 64)) {
    return EFI_INVALID_PARAMETER;
  }

  if ((DeviceSpeed == EFI_USB_SPEED_LOW) && (MaximumPacketLength != 8)) {
    return EFI_INVALID_PARAMETER;
  }

  OldTpl          = gBS->RaiseTPL (EHC_TPL);
  Ehc             = EHC_FROM_THIS (This);

  Status          = EFI_DEVICE_ERROR;
  *TransferResult = EFI_USB_ERR_SYSTEM;

  if (EhcIsHalt (Ehc) || EhcIsSysError (Ehc)) {
    EHC_ERROR (("EhcControlTransfer: HC halted at entrance\n"));

    EhcAckAllInterrupt (Ehc);
    goto ON_EXIT;
  }

  EhcAckAllInterrupt (Ehc);

  //
  // Create a new URB, insert it into the asynchronous
  // schedule list, then poll the execution status.
  //
  //
  // Encode the direction in address, although default control
  // endpoint is bidirectional. EhcCreateUrb expects this
  // combination of Ep addr and its direction.
  //
  Endpoint = 0 | ((TransferDirection == EfiUsbDataIn) ? 0x80 : 0);
  Urb = EhcCreateUrb (
          Ehc,
          DeviceAddress,
          Endpoint,
          DeviceSpeed,
          0,
          MaximumPacketLength,
          Translator,
          EHC_CTRL_TRANSFER,
          Request,
          Data,
          *DataLength,
          NULL,
          NULL,
          1
          );

  if (Urb == NULL) {
    EHC_ERROR (("EhcControlTransfer: failed to create URB"));

    Status = EFI_OUT_OF_RESOURCES;
    goto ON_EXIT;
  }

  EhcLinkQhToAsync (Ehc, Urb->Qh);
  Status = EhcExecTransfer (Ehc, Urb, TimeOut);
  EhcUnlinkQhFromAsync (Ehc, Urb->Qh);

  //
  // Get the status from URB. The result is updated in EhcCheckUrbResult
  // which is called by EhcExecTransfer
  //
  *TransferResult = Urb->Result;
  *DataLength     = Urb->Completed;

  if (*TransferResult == EFI_USB_NOERROR) {
    Status = EFI_SUCCESS;
  }

  EhcAckAllInterrupt (Ehc);
  EhcFreeUrb (Ehc, Urb);

ON_EXIT:
  Ehc->PciIo->Flush (Ehc->PciIo);
  gBS->RestoreTPL (OldTpl);

  if (EFI_ERROR (Status)) {
    EHC_ERROR (("EhcControlTransfer: error - %r, transfer - %x\n", Status, *TransferResult));
  }

  return Status;
}

STATIC
EFI_STATUS
EFIAPI
EhcBulkTransfer (
  IN  EFI_USB2_HC_PROTOCOL                *This,
  IN  UINT8                               DeviceAddress,
  IN  UINT8                               EndPointAddress,
  IN  UINT8                               DeviceSpeed,
  IN  UINTN                               MaximumPacketLength,
  IN  UINT8                               DataBuffersNumber,
  IN  OUT VOID                            *Data[EFI_USB_MAX_BULK_BUFFER_NUM],
  IN  OUT UINTN                           *DataLength,
  IN  OUT UINT8                           *DataToggle,
  IN  UINTN                               TimeOut,
  IN  EFI_USB2_HC_TRANSACTION_TRANSLATOR  *Translator,
  OUT UINT32                              *TransferResult
  )
/*++

  Routine Description:

    Submits bulk transfer to a bulk endpoint of a USB device.

  Arguments:

    This                - This EFI_USB2_HC_PROTOCOL instance.
    DeviceAddress       - Target device address
    EndPointAddress     - Endpoint number and its direction in bit 7. .
    DeviceSpeed         - Device speed, Low speed device doesn't support 
                          bulk transfer.
    MaximumPacketLength - Maximum packet size the endpoint is capable of 
                          sending or receiving.
    DataBuffersNumber   - Number of data buffers prepared for the transfer.
    Data                - Array of pointers to the buffers of data to transmit 
                          from or receive into.
    DataLength          - The lenght of the data buffer
    DataToggle          - On input, the initial data toggle for the transfer;
                          On output, it is updated to to next data toggle to use                        of the subsequent bulk transfer.
    Translator          - A pointr to the transaction translator data.
    TimeOut             - Indicates the maximum time, in millisecond, which the
                          transfer is allowed to complete.
    TransferResult      - A pointer to the detailed result information of the
                          bulk transfer.

  Returns:

    EFI_SUCCESS           : The transfer was completed successfully.
    EFI_OUT_OF_RESOURCES  : The transfer failed due to lack of resource.
    EFI_INVALID_PARAMETER : Some parameters are invalid.
    EFI_TIMEOUT           : The transfer failed due to timeout.
    EFI_DEVICE_ERROR      : The transfer failed due to host controller error.

--*/
{
  USB2_HC_DEV             *Ehc;
  URB                     *Urb;
  EFI_TPL                 OldTpl;
  EFI_STATUS              Status;

  //
  // Validate the parameters
  //
  if ((DataLength == NULL) || (*DataLength == 0) || 
      (Data == NULL) || (Data[0] == NULL) || (TransferResult == NULL)) {
    return EFI_INVALID_PARAMETER;
  }

  if ((*DataToggle != 0) && (*DataToggle != 1)) {
    return EFI_INVALID_PARAMETER;
  }

  if ((DeviceSpeed == EFI_USB_SPEED_LOW) ||
      ((DeviceSpeed == EFI_USB_SPEED_FULL) && (MaximumPacketLength > 64)) ||
      ((EFI_USB_SPEED_HIGH == DeviceSpeed) && (MaximumPacketLength > 512))) {
    return EFI_INVALID_PARAMETER;
  }

  OldTpl          = gBS->RaiseTPL (EHC_TPL);
  Ehc             = EHC_FROM_THIS (This);

  *TransferResult = EFI_USB_ERR_SYSTEM;
  Status          = EFI_DEVICE_ERROR;

  if (EhcIsHalt (Ehc) || EhcIsSysError (Ehc)) {
    EHC_ERROR (("EhcBulkTransfer: HC is halted\n"));

    EhcAckAllInterrupt (Ehc);
    goto ON_EXIT;
  }

  EhcAckAllInterrupt (Ehc);

  //
  // Create a new URB, insert it into the asynchronous
  // schedule list, then poll the execution status.
  //
  Urb = EhcCreateUrb (
          Ehc,
          DeviceAddress,
          EndPointAddress,
          DeviceSpeed,
          *DataToggle,
          MaximumPacketLength,
          Translator,
          EHC_BULK_TRANSFER,
          NULL,
          Data[0],
          *DataLength,
          NULL,
          NULL,
          1
          );

  if (Urb == NULL) {
    EHC_ERROR (("EhcBulkTransfer: failed to create URB\n"));

    Status = EFI_OUT_OF_RESOURCES;
    goto ON_EXIT;
  }

  EhcLinkQhToAsync (Ehc, Urb->Qh);
  Status = EhcExecTransfer (Ehc, Urb, TimeOut);
  EhcUnlinkQhFromAsync (Ehc, Urb->Qh);

  *TransferResult = Urb->Result;
  *DataLength     = Urb->Completed;
  *DataToggle     = Urb->DataToggle;

  if (*TransferResult == EFI_USB_NOERROR) {
    Status = EFI_SUCCESS;
  }

  EhcAckAllInterrupt (Ehc);
  EhcFreeUrb (Ehc, Urb);

ON_EXIT:
  Ehc->PciIo->Flush (Ehc->PciIo);
  gBS->RestoreTPL (OldTpl);

  if (EFI_ERROR (Status)) {
    EHC_ERROR (("EhcBulkTransfer: error - %r, transfer - %x\n", Status, *TransferResult));
  }

  return Status;
}

STATIC
EFI_STATUS
EFIAPI
EhcAsyncInterruptTransfer (
  IN  EFI_USB2_HC_PROTOCOL                  * This,
  IN  UINT8                                 DeviceAddress,
  IN  UINT8                                 EndPointAddress,
  IN  UINT8                                 DeviceSpeed,
  IN  UINTN                                 MaximumPacketLength,
  IN  BOOLEAN                               IsNewTransfer,
  IN  OUT UINT8                             *DataToggle,
  IN  UINTN                                 PollingInterval,
  IN  UINTN                                 DataLength,
  IN  EFI_USB2_HC_TRANSACTION_TRANSLATOR    * Translator,
  IN  EFI_ASYNC_USB_TRANSFER_CALLBACK       CallBackFunction,
  IN  VOID                                  *Context OPTIONAL
  )
/*++

  Routine Description:

    Submits an asynchronous interrupt transfer to an
    interrupt endpoint of a USB device.

  Arguments:
    This                - This EFI_USB2_HC_PROTOCOL instance.
    DeviceAddress       - Target device address
    EndPointAddress     - Endpoint number and its direction encoded in bit 7
    DeviceSpeed         - Indicates device speed.
    MaximumPacketLength - Maximum packet size the target endpoint is capable 
    IsNewTransfer       - If TRUE, to submit an new asynchronous interrupt transfer
                          If FALSE, to remove the specified asynchronous interrupt
    DataToggle          - On input, the initial data toggle to use;
                          on output, it is updated to indicate the next data toggle 
    PollingInterval     - The he interval, in milliseconds, that the transfer is polled.                        
    DataLength          - The length of data to receive at the rate specified by 
                          PollingInterval.
    Translator          - Transaction translator to use.
    CallBackFunction    - Function to call at the rate specified by PollingInterval
    Context             - Context to CallBackFunction.

  Returns:

    EFI_SUCCESS           : The request has been successfully submitted or canceled.
    EFI_INVALID_PARAMETER : Some parameters are invalid.
    EFI_OUT_OF_RESOURCES  : The request failed due to a lack of resources.
    EFI_DEVICE_ERROR      : The transfer failed due to host controller error.

--*/
{
  USB2_HC_DEV             *Ehc;
  URB                     *Urb;
  EFI_TPL                 OldTpl;
  EFI_STATUS              Status;
  UINT8                   *Data;

  //
  // Validate parameters
  //
  if (!EHCI_IS_DATAIN (EndPointAddress)) {
    return EFI_INVALID_PARAMETER;
  }

  if (IsNewTransfer) {
    if (DataLength == 0) {
      return EFI_INVALID_PARAMETER;
    }

    if ((*DataToggle != 1) && (*DataToggle != 0)) {
      return EFI_INVALID_PARAMETER;
    }

    if ((PollingInterval > 255) || (PollingInterval < 1)) {
      return EFI_INVALID_PARAMETER;
    }
  }

  OldTpl  = gBS->RaiseTPL (EHC_TPL);
  Ehc     = EHC_FROM_THIS (This);

  //
  // Delete Async interrupt transfer request. DataToggle will return
  // the next data toggle to use.
  //
  if (!IsNewTransfer) {
    Status = EhciDelAsyncIntTransfer (Ehc, DeviceAddress, EndPointAddress, DataToggle);

    EHC_DEBUG (("EhcAsyncInterruptTransfer: remove old transfer - %r\n", Status));
    goto ON_EXIT;
  }

  Status = EFI_SUCCESS;

  if (EhcIsHalt (Ehc) || EhcIsSysError (Ehc)) {
    EHC_ERROR (("EhcAsyncInterruptTransfer: HC is halt\n"));
    EhcAckAllInterrupt (Ehc);

    Status = EFI_DEVICE_ERROR;
    goto ON_EXIT;
  }

  EhcAckAllInterrupt (Ehc);

  Data = EfiLibAllocatePool (DataLength);
  
  if (Data == NULL) {
    EHC_ERROR (("EhcAsyncInterruptTransfer: failed to allocate buffer\n"));

    Status = EFI_OUT_OF_RESOURCES;
    goto ON_EXIT;
  }

  Urb = EhcCreateUrb (
          Ehc,
          DeviceAddress,
          EndPointAddress,
          DeviceSpeed,
          *DataToggle,
          MaximumPacketLength,
          Translator,
          EHC_INT_TRANSFER_ASYNC,
          NULL,
          Data,
          DataLength,
          CallBackFunction,
          Context,
          PollingInterval
          );

  if (Urb == NULL) {
    EHC_ERROR (("EhcAsyncInterruptTransfer: failed to create URB\n"));

    gBS->FreePool (Data);
    Status = EFI_OUT_OF_RESOURCES;
    goto ON_EXIT;
  }
  
  //
  // New asynchronous transfer must inserted to the head.
  // Check the comments in EhcMoniteAsyncRequests
  //
  EhcLinkQhToPeriod (Ehc, Urb->Qh);
  InsertHeadList (&Ehc->AsyncIntTransfers, &Urb->UrbList);

ON_EXIT:
  Ehc->PciIo->Flush (Ehc->PciIo);
  gBS->RestoreTPL (OldTpl);

  return Status;
}

STATIC
EFI_STATUS
EFIAPI
EhcSyncInterruptTransfer (
  IN  EFI_USB2_HC_PROTOCOL                *This,
  IN  UINT8                               DeviceAddress,
  IN  UINT8                               EndPointAddress,
  IN  UINT8                               DeviceSpeed,
  IN  UINTN                               MaximumPacketLength,
  IN  OUT VOID                            *Data,
  IN  OUT UINTN                           *DataLength,
  IN  OUT UINT8                           *DataToggle,
  IN  UINTN                               TimeOut,
  IN  EFI_USB2_HC_TRANSACTION_TRANSLATOR  *Translator,
  OUT UINT32                              *TransferResult
  )
/*++

  Routine Description:

    Submits synchronous interrupt transfer to an interrupt endpoint
    of a USB device.

  Arguments:

    This                - This EFI_USB2_HC_PROTOCOL instance.
    DeviceAddress       - Target device address
    EndPointAddress     - Endpoint number and its direction encoded in bit 7
    DeviceSpeed         - Indicates device speed.
    MaximumPacketLength - Maximum packet size the target endpoint is capable 
                          of sending or receiving.
    Data                - Buffer of data that will be transmitted to 
                          USB device or received from USB device.
    DataLength          - On input, the size, in bytes, of the data buffer;
                          On output, the number of bytes transferred.
    DataToggle          - On input, the initial data toggle to use;
                          on output, it is updated to indicate the next data toggle 
    TimeOut             - Maximum time, in second, to complete
    Translator          - Transaction translator to use.
    TransferResult      - Variable to receive the transfer result
    
  Returns:

    EFI_SUCCESS           : The transfer was completed successfully.
    EFI_OUT_OF_RESOURCES  : The transfer failed due to lack of resource.
    EFI_INVALID_PARAMETER : Some parameters are invalid.
    EFI_TIMEOUT           : The transfer failed due to timeout.
    EFI_DEVICE_ERROR      : The failed due to host controller or device error

--*/
{
  USB2_HC_DEV             *Ehc;
  EFI_TPL                 OldTpl;
  URB                     *Urb;
  EFI_STATUS              Status;

  //
  // Validates parameters
  //
  if ((DataLength == NULL) || (*DataLength == 0) || 
      (Data == NULL) || (TransferResult == NULL)) {
    return EFI_INVALID_PARAMETER;
  }

  if (!EHCI_IS_DATAIN (EndPointAddress)) {
    return EFI_INVALID_PARAMETER;
  }

  if ((*DataToggle != 1) && (*DataToggle != 0)) {
    return EFI_INVALID_PARAMETER;
  }

  if (((DeviceSpeed == EFI_USB_SPEED_LOW) && (MaximumPacketLength != 8))  ||
      ((DeviceSpeed == EFI_USB_SPEED_FULL) && (MaximumPacketLength > 64)) || 
      ((DeviceSpeed == EFI_USB_SPEED_HIGH) && (MaximumPacketLength > 3072))) {
    return EFI_INVALID_PARAMETER;
  }

  OldTpl          = gBS->RaiseTPL (EHC_TPL);
  Ehc             = EHC_FROM_THIS (This);

  *TransferResult = EFI_USB_ERR_SYSTEM;
  Status          = EFI_DEVICE_ERROR;

  if (EhcIsHalt (Ehc) || EhcIsSysError (Ehc)) {
    EHC_ERROR (("EhcSyncInterruptTransfer: HC is halt\n"));

    EhcAckAllInterrupt (Ehc);
    goto ON_EXIT;
  }

  EhcAckAllInterrupt (Ehc);

  Urb = EhcCreateUrb (
          Ehc,
          DeviceAddress,
          EndPointAddress,
          DeviceSpeed,
          *DataToggle,
          MaximumPacketLength,
          Translator,
          EHC_INT_TRANSFER_SYNC,
          NULL,
          Data,
          *DataLength,
          NULL,
          NULL,
          1
          );

  if (Urb == NULL) {
    EHC_ERROR (("EhcSyncInterruptTransfer: failed to create URB\n"));

    Status = EFI_OUT_OF_RESOURCES;
    goto ON_EXIT;
  }

  EhcLinkQhToPeriod (Ehc, Urb->Qh);
  Status = EhcExecTransfer (Ehc, Urb, TimeOut);
  EhcUnlinkQhFromPeriod (Ehc, Urb->Qh);

  *TransferResult = Urb->Result;
  *DataLength     = Urb->Completed;
  *DataToggle     = Urb->DataToggle;

  if (*TransferResult == EFI_USB_NOERROR) {
    Status = EFI_SUCCESS;
  }

ON_EXIT:
  Ehc->PciIo->Flush (Ehc->PciIo);
  gBS->RestoreTPL (OldTpl);

  if (EFI_ERROR (Status)) {
    EHC_ERROR (("EhcSyncInterruptTransfer: error - %r, transfer - %x\n", Status, *TransferResult));
  }

  return Status;
}

STATIC
EFI_STATUS
EFIAPI
EhcIsochronousTransfer (
  IN  EFI_USB2_HC_PROTOCOL                *This,
  IN  UINT8                               DeviceAddress,
  IN  UINT8                               EndPointAddress,
  IN  UINT8                               DeviceSpeed,
  IN  UINTN                               MaximumPacketLength,
  IN  UINT8                               DataBuffersNumber,
  IN  OUT VOID                            *Data[EFI_USB_MAX_ISO_BUFFER_NUM],
  IN  UINTN                               DataLength,
  IN  EFI_USB2_HC_TRANSACTION_TRANSLATOR  *Translator,
  OUT UINT32                              *TransferResult
  )
/*++

  Routine Description:

    Submits isochronous transfer to a target USB device.

  Arguments:

    This                - This EFI_USB2_HC_PROTOCOL instance.
    DeviceAddress       - Target device address
    EndPointAddress     - End point address with its direction
    DeviceSpeed         - Device speed, Low speed device doesn't support this type.
    MaximumPacketLength - Maximum packet size that the endpoint is capable of
                          sending or receiving.
    DataBuffersNumber   - Number of data buffers prepared for the transfer.
    Data                - Array of pointers to the buffers of data that will be
                          transmitted to USB device or received from USB device.
    DataLength          - The size, in bytes, of the data buffer
    Translator          - Transaction translator to use.
    TransferResult      - Variable to receive the transfer result

  Returns:

    EFI_UNSUPPORTED : Isochronous transfer is unsupported.

--*/
{
  return EFI_UNSUPPORTED;
}

STATIC
EFI_STATUS
EFIAPI
EhcAsyncIsochronousTransfer (
  IN  EFI_USB2_HC_PROTOCOL                *This,
  IN  UINT8                               DeviceAddress,
  IN  UINT8                               EndPointAddress,
  IN  UINT8                               DeviceSpeed,
  IN  UINTN                               MaximumPacketLength,
  IN  UINT8                               DataBuffersNumber,
  IN  OUT VOID                            *Data[EFI_USB_MAX_ISO_BUFFER_NUM],
  IN  UINTN                               DataLength,
  IN  EFI_USB2_HC_TRANSACTION_TRANSLATOR  *Translator,
  IN  EFI_ASYNC_USB_TRANSFER_CALLBACK     IsochronousCallBack,
  IN  VOID                                *Context
  )
/*++

  Routine Description:

    Submits Async isochronous transfer to a target USB device.

  Arguments:
    This                - This EFI_USB2_HC_PROTOCOL instance.
    DeviceAddress       - Target device address
    EndPointAddress     - End point address with its direction
    DeviceSpeed         - Device speed, Low speed device doesn't support this type.
    MaximumPacketLength - Maximum packet size that the endpoint is capable of
                          sending or receiving.
    DataBuffersNumber   - Number of data buffers prepared for the transfer.
    Data                - Array of pointers to the buffers of data that will be
                          transmitted to USB device or received from USB device.
    DataLength          - The size, in bytes, of the data buffer
    Translator          - Transaction translator to use.
    IsochronousCallBack - Function to be called when the transfer complete
    Context             - Context passed to the call back function as parameter

  Returns:

    EFI_UNSUPPORTED : Isochronous transfer isn't supported

--*/
{
  return EFI_UNSUPPORTED;
}

EFI_DRIVER_ENTRY_POINT (EhcDriverEntryPoint)

EFI_STATUS
EFIAPI
EhcDriverEntryPoint (
  IN EFI_HANDLE           ImageHandle,
  IN EFI_SYSTEM_TABLE     *SystemTable
  )
/*++

Routine Description:

  Entry point for EFI drivers.

Arguments:

  ImageHandle - EFI_HANDLE
  SystemTable - EFI_SYSTEM_TABLE

Returns:

  EFI_SUCCESS         Success
  EFI_DEVICE_ERROR    Fail

--*/
{
  return INSTALL_ALL_DRIVER_PROTOCOLS_OR_PROTOCOLS2 (
           ImageHandle,
           SystemTable,
           &gEhciDriverBinding,
           ImageHandle,
           &gEhciComponentName,
           NULL,
           NULL
           );
}

EFI_STATUS
EFIAPI
EhcDriverBindingSupported (
  IN EFI_DRIVER_BINDING_PROTOCOL *This,
  IN EFI_HANDLE                  Controller,
  IN EFI_DEVICE_PATH_PROTOCOL    *RemainingDevicePath
  )
/*++

  Routine Description:

    Test to see if this driver supports ControllerHandle. Any 
    ControllerHandle that has Usb2HcProtocol installed will 
    be supported.

  Arguments:
    This                - Protocol instance pointer.
    Controlle           - Handle of device to test
    RemainingDevicePath - Not used

  Returns:
    EFI_SUCCESS         : This driver supports this device.
    EFI_UNSUPPORTED     : This driver does not support this device.

--*/
{
  EFI_STATUS              Status;
  EFI_PCI_IO_PROTOCOL     *PciIo;
  USB_CLASSC              UsbClassCReg;

  //
  // Test whether there is PCI IO Protocol attached on the controller handle.
  //
  Status = gBS->OpenProtocol (
                  Controller,
                  &gEfiPciIoProtocolGuid,
                  &PciIo,
                  This->DriverBindingHandle,
                  Controller,
                  EFI_OPEN_PROTOCOL_BY_DRIVER
                  );

  if (EFI_ERROR (Status)) {
    return EFI_UNSUPPORTED;
  }

  Status = PciIo->Pci.Read (
                        PciIo,
                        EfiPciIoWidthUint8,
                        EHC_PCI_CLASSC,
                        sizeof (USB_CLASSC) / sizeof (UINT8),
                        &UsbClassCReg
                        );

  if (EFI_ERROR (Status)) {
    Status = EFI_UNSUPPORTED;
    goto ON_EXIT;
  }
  
  //
  // Test whether the controller belongs to Ehci type
  //
  if ((UsbClassCReg.BaseCode     != PCI_CLASS_SERIAL) ||
      (UsbClassCReg.SubClassCode != PCI_CLASS_SERIAL_USB) ||
      (UsbClassCReg.PI           != EHC_PCI_CLASSC_PI)) {

    Status = EFI_UNSUPPORTED;
  }

ON_EXIT:
  gBS->CloseProtocol (
         Controller,
         &gEfiPciIoProtocolGuid,
         This->DriverBindingHandle,
         Controller
         );

  return Status;
}

STATIC
USB2_HC_DEV *
EhcCreateUsb2Hc (
  IN EFI_PCI_IO_PROTOCOL  *PciIo
  )
/*++
  Routine Description:
    Create and initialize a USB2_HC_DEV

  Arguments:
    PciIo    - The PciIo on this device

  Returns:
    The allocated and initialized USB2_HC_DEV structure
    if created, otherwise NULL.
    
--*/
{
  USB2_HC_DEV             *Ehc;
  EFI_STATUS              Status;

  Ehc = EfiLibAllocateZeroPool (sizeof (USB2_HC_DEV));

  if (Ehc == NULL) {
    return NULL;
  }
  
  //
  // Init EFI_USB2_HC_PROTOCOL interface and private data structure
  //
  Ehc->Signature                        = USB2_HC_DEV_SIGNATURE;

  Ehc->Usb2Hc.GetCapability             = EhcGetCapability;
  Ehc->Usb2Hc.Reset                     = EhcReset;
  Ehc->Usb2Hc.GetState                  = EhcGetState;
  Ehc->Usb2Hc.SetState                  = EhcSetState;
  Ehc->Usb2Hc.ControlTransfer           = EhcControlTransfer;
  Ehc->Usb2Hc.BulkTransfer              = EhcBulkTransfer;
  Ehc->Usb2Hc.AsyncInterruptTransfer    = EhcAsyncInterruptTransfer;
  Ehc->Usb2Hc.SyncInterruptTransfer     = EhcSyncInterruptTransfer;
  Ehc->Usb2Hc.IsochronousTransfer       = EhcIsochronousTransfer;
  Ehc->Usb2Hc.AsyncIsochronousTransfer  = EhcAsyncIsochronousTransfer;
  Ehc->Usb2Hc.GetRootHubPortStatus      = EhcGetRootHubPortStatus;
  Ehc->Usb2Hc.SetRootHubPortFeature     = EhcSetRootHubPortFeature;
  Ehc->Usb2Hc.ClearRootHubPortFeature   = EhcClearRootHubPortFeature;
  Ehc->Usb2Hc.MajorRevision             = 0x2;
  Ehc->Usb2Hc.MinorRevision             = 0x0;

  Ehc->PciIo = PciIo;

  InitializeListHead (&Ehc->AsyncIntTransfers);

  Ehc->HcStructParams = EhcReadCapRegister (Ehc, EHC_HCSPARAMS_OFFSET);
  Ehc->HcCapParams    = EhcReadCapRegister (Ehc, EHC_HCCPARAMS_OFFSET);
  Ehc->CapLen         = EhcReadCapRegister (Ehc, EHC_CAPLENGTH_OFFSET) & 0x0FF;
  
  EHC_DEBUG (("EhcCreateUsb2Hc: capability length %d\n", Ehc->CapLen));

  //
  // Create AsyncRequest Polling Timer
  //
  Status = gBS->CreateEvent (
                  EFI_EVENT_TIMER | EFI_EVENT_NOTIFY_SIGNAL,
                  EFI_TPL_CALLBACK,
                  EhcMoniteAsyncRequests,
                  Ehc,
                  &Ehc->PollTimer
                  );

  if (EFI_ERROR (Status)) {
    gBS->FreePool (Ehc);
    return NULL;
  }

  return Ehc;
}

VOID
EFIAPI
EhcExitBootService (
  EFI_EVENT                      Event,
  VOID                           *Context
  )
/*++

Routine Description:

  Callback function for exit boot service event

Arguments:

  Event   - EFI_EVENT structure
  Context - Event context

Returns:

  None

--*/
{
  USB2_HC_DEV   *Ehc;

  Ehc = (USB2_HC_DEV *) Context;

  //
  // Stop the Host Controller
  //
  EhcHaltHC (Ehc, EHC_GENERIC_TIMEOUT);

  return;
}

EFI_STATUS
EFIAPI
EhcDriverBindingStart (
  IN EFI_DRIVER_BINDING_PROTOCOL *This,
  IN EFI_HANDLE                  Controller,
  IN EFI_DEVICE_PATH_PROTOCOL    *RemainingDevicePath
  )
/*++
  Routine Description:
    Starting the Usb EHCI Driver

  Arguments:
    This                - Protocol instance pointer.
    Controller          - Handle of device to test
    RemainingDevicePath - Not used

  Returns:
    EFI_SUCCESS          : supports this device.
    EFI_UNSUPPORTED      : do not support this device.
    EFI_DEVICE_ERROR     : cannot be started due to device Error
    EFI_OUT_OF_RESOURCES : cannot allocate resources
--*/
{
  EFI_STATUS              Status;
  USB2_HC_DEV             *Ehc;
  EFI_PCI_IO_PROTOCOL     *PciIo;

  //
  // Open the PciIo Protocol, then enable the USB host controller
  //
  Status = gBS->OpenProtocol (
                  Controller,
                  &gEfiPciIoProtocolGuid,
                  &PciIo,
                  This->DriverBindingHandle,
                  Controller,
                  EFI_OPEN_PROTOCOL_BY_DRIVER
                  );

  if (EFI_ERROR (Status)) {
    EHC_ERROR (("EhcDriverBindingStart: failed to open PCI_IO\n"));
    return EFI_DEVICE_ERROR;
  }

  Status = PciIo->Attributes (
                    PciIo,
                    EfiPciIoAttributeOperationEnable,
                    EFI_PCI_DEVICE_ENABLE,
                    NULL
                    );

  if (EFI_ERROR (Status)) {
    EHC_ERROR (("EhcDriverBindingStart: failed to enable controller\n"));
    goto CLOSE_PCIIO;
  }
  
  //
  // Create then install USB2_HC_PROTOCOL
  //
  Ehc = EhcCreateUsb2Hc (PciIo);

  if (Ehc == NULL) {
    EHC_ERROR (("EhcDriverBindingStart: failed to create USB2_HC\n"));

    Status = EFI_OUT_OF_RESOURCES;
    goto CLOSE_PCIIO;
  }

  Status = gBS->InstallProtocolInterface (
                  &Controller,
                  &gEfiUsb2HcProtocolGuid,
                  EFI_NATIVE_INTERFACE,
                  &Ehc->Usb2Hc
                  );

  if (EFI_ERROR (Status)) {
    EHC_ERROR (("EhcDriverBindingStart: failed to install USB2_HC Protocol\n"));
    goto FREE_POOL;
  }
  
#ifdef USB_LEGACY_OFF 
  //
  // the USB_LEGACY_OFF can be defined at PlatformTools.env 
  //
  EhcClearLegacySupport (Ehc);
#endif

  EhcResetHC (Ehc, EHC_RESET_TIMEOUT);

  Status = EhcInitHC (Ehc);

  if (EFI_ERROR (Status)) {
    EHC_ERROR (("EhcDriverBindingStart: failed to init host controller\n"));
    goto UNINSTALL_USBHC;
  }
  
  //
  // Start the asynchronous interrupt monitor
  //
  Status = gBS->SetTimer (Ehc->PollTimer, TimerPeriodic, EHC_ASYNC_POLL_INTERVAL);

  if (EFI_ERROR (Status)) {
    EHC_ERROR (("EhcDriverBindingStart: failed to start async interrupt monitor\n"));
    
    EhcHaltHC (Ehc, EHC_GENERIC_TIMEOUT);
    goto UNINSTALL_USBHC;
  }

  //
  // Create event to stop the HC when exit boot service.
  //
  Status = gBS->CreateEvent (
                  EFI_EVENT_SIGNAL_EXIT_BOOT_SERVICES,
                  EFI_TPL_NOTIFY,
                  EhcExitBootService,
                  Ehc,
                  &Ehc->ExitBootServiceEvent
                  );
  if (EFI_ERROR (Status)) {
    goto UNINSTALL_USBHC;
  }
  
  //
  // Install the component name protocol, don't fail the start
  // because of something for display.
  //
  EfiLibAddUnicodeString (
    LANGUAGE_CODE_ENGLISH,
    gEhciComponentName.SupportedLanguages,
    &Ehc->ControllerNameTable,
    L"Enhanced Host Controller (USB 2.0)"
    );

  EHC_DEBUG (("EhcDriverBindingStart: EHCI started for controller @ %x\n", Controller));
  return EFI_SUCCESS;

UNINSTALL_USBHC:
  gBS->UninstallProtocolInterface (
         Controller,
         &gEfiUsb2HcProtocolGuid,
         &Ehc->Usb2Hc
         );

FREE_POOL:
  EhcFreeSched (Ehc);
  gBS->CloseEvent (Ehc->PollTimer);
  gBS->FreePool (Ehc);

CLOSE_PCIIO:
  gBS->CloseProtocol (
         Controller,
         &gEfiPciIoProtocolGuid,
         This->DriverBindingHandle,
         Controller
         );

  return Status;
}

EFI_STATUS
EFIAPI
EhcDriverBindingStop (
  IN EFI_DRIVER_BINDING_PROTOCOL *This,
  IN EFI_HANDLE                  Controller,
  IN UINTN                       NumberOfChildren,
  IN EFI_HANDLE                  *ChildHandleBuffer
  )
/*++

  Routine Description:

    Stop this driver on ControllerHandle. Support stoping any child handles
    created by this driver.

  Arguments:

    This              - Protocol instance pointer.
    Controller        - Handle of device to stop driver on
    NumberOfChildren  - Number of Children in the ChildHandleBuffer
    ChildHandleBuffer - List of handles for the children we need to stop.

  Returns:

    EFI_SUCCESS         Success
    EFI_DEVICE_ERROR    Fail
--*/
{
  EFI_STATUS            Status;
  EFI_USB2_HC_PROTOCOL  *Usb2Hc;
  EFI_PCI_IO_PROTOCOL   *PciIo;
  USB2_HC_DEV           *Ehc;

  //
  // Test whether the Controller handler passed in is a valid
  // Usb controller handle that should be supported, if not,
  // return the error status directly
  //
  Status = gBS->OpenProtocol (
                  Controller,
                  &gEfiUsb2HcProtocolGuid,
                  &Usb2Hc,
                  This->DriverBindingHandle,
                  Controller,
                  EFI_OPEN_PROTOCOL_GET_PROTOCOL
                  );

  if (EFI_ERROR (Status)) {
    return Status;
  }

  Ehc   = EHC_FROM_THIS (Usb2Hc);
  PciIo = Ehc->PciIo;

  //
  // Stop AsyncRequest Polling timer then stop the EHCI driver
  // and uninstall the EHCI protocl.
  //
  gBS->SetTimer (Ehc->PollTimer, TimerCancel, EHC_ASYNC_POLL_INTERVAL);
  EhcHaltHC (Ehc, EHC_GENERIC_TIMEOUT);

  Status = gBS->UninstallProtocolInterface (
                  Controller,
                  &gEfiUsb2HcProtocolGuid,
                  Usb2Hc
                  );

  if (EFI_ERROR (Status)) {
    return Status;
  }

  if (Ehc->PollTimer != NULL) {
    gBS->CloseEvent (Ehc->PollTimer);
  }

  if (Ehc->ExitBootServiceEvent != NULL) {
    gBS->CloseEvent (Ehc->ExitBootServiceEvent);
  }

  EhcFreeSched (Ehc);

  if (Ehc->ControllerNameTable) {
    EfiLibFreeUnicodeStringTable (Ehc->ControllerNameTable);
  }
  
  //
  // Disable the USB Host Controller
  //
  PciIo->Attributes (
           PciIo,
           EfiPciIoAttributeOperationDisable,
           EFI_PCI_DEVICE_ENABLE,
           NULL
           );

  gBS->CloseProtocol (
         Controller,
         &gEfiPciIoProtocolGuid,
         This->DriverBindingHandle,
         Controller
         );

  gBS->FreePool (Ehc);
  return Status;
}

EFI_DRIVER_BINDING_PROTOCOL 
gEhciDriverBinding = {
  EhcDriverBindingSupported,
  EhcDriverBindingStart,
  EhcDriverBindingStop,
  0x10,
  NULL,
  NULL
};
