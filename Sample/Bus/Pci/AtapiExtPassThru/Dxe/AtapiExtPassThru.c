/*++

Copyright (c) 2004 - 2008, Intel Corporation                                                         
All rights reserved. This program and the accompanying materials                          
are licensed and made available under the terms and conditions of the BSD License         
which accompanies this distribution.  The full text of the license may be found at        
http://opensource.org/licenses/bsd-license.php                                            
                                                                                          
THE PROGRAM IS DISTRIBUTED UNDER THE BSD LICENSE ON AN "AS IS" BASIS,                     
WITHOUT WARRANTIES OR REPRESENTATIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED.             

Module Name:

    AtapiPassThru.c
    
Abstract: 
    

Revision History
--*/

#include "AtapiExtPassThru.h"

EFI_STATUS
EFIAPI
AtapiExtScsiPassThruDriverBindingSupported (
  IN EFI_DRIVER_BINDING_PROTOCOL  *This,
  IN EFI_HANDLE                   Controller,
  IN EFI_DEVICE_PATH_PROTOCOL     *RemainingDevicePath
  );

EFI_STATUS
EFIAPI
AtapiExtScsiPassThruDriverBindingStart (
  IN EFI_DRIVER_BINDING_PROTOCOL  *This,
  IN EFI_HANDLE                   Controller,
  IN EFI_DEVICE_PATH_PROTOCOL     *RemainingDevicePath
  );

EFI_STATUS
EFIAPI
AtapiExtScsiPassThruDriverBindingStop (
  IN  EFI_DRIVER_BINDING_PROTOCOL     *This,
  IN  EFI_HANDLE                      Controller,
  IN  UINTN                           NumberOfChildren,
  IN  EFI_HANDLE                      *ChildHandleBuffer
  );

STATIC  
EFI_STATUS
CheckSCSIRequestPacket (
  EFI_EXT_SCSI_PASS_THRU_SCSI_REQUEST_PACKET      *Packet
  );

STATIC  
BOOLEAN
IsCommandValid (
  EFI_EXT_SCSI_PASS_THRU_SCSI_REQUEST_PACKET   *Packet
  );

STATIC  
EFI_STATUS
SubmitBlockingIoCommand (
  ATAPI_EXT_SCSI_PASS_THRU_DEV                  *AtapiScsiPrivate,
  UINT8                                         Target,
  EFI_EXT_SCSI_PASS_THRU_SCSI_REQUEST_PACKET    *Packet
  );

STATIC  
EFI_STATUS
RequestSenseCommand (
  ATAPI_EXT_SCSI_PASS_THRU_DEV    *AtapiScsiPrivate,
  UINT8                           Target,
  UINT64                          Timeout,
  VOID                            *SenseData,
  UINT8                           *SenseDataLength
  );

STATIC  
EFI_STATUS
AtapiPacketCommand (
  ATAPI_EXT_SCSI_PASS_THRU_DEV    *AtapiScsiPrivate,
  UINT8                           Target,
  UINT8                           *PacketCommand,
  VOID                            *Buffer,
  UINT32                          *ByteCount,
  DATA_DIRECTION                  Direction,
  UINT64                          TimeoutInMicroSeconds
  );

STATIC  
EFI_STATUS
AtapiPassThruPioReadWriteData (
  ATAPI_EXT_SCSI_PASS_THRU_DEV  *AtapiScsiPrivate,
  UINT16                        *Buffer,
  UINT32                        *ByteCount,
  DATA_DIRECTION                Direction,
  UINT64                        TimeoutInMicroSeconds
  );

STATIC
UINT8
ReadPortB (
  IN  EFI_PCI_IO_PROTOCOL   *PciIo,
  IN  UINT16                Port
  );

STATIC
UINT16
ReadPortW (
  IN  EFI_PCI_IO_PROTOCOL   *PciIo,
  IN  UINT16                Port
  );

STATIC
VOID
WritePortB (
  IN  EFI_PCI_IO_PROTOCOL   *PciIo,
  IN  UINT16                Port,
  IN  UINT8                 Data
  );

STATIC
VOID
WritePortW (
  IN  EFI_PCI_IO_PROTOCOL   *PciIo,
  IN  UINT16                Port,
  IN  UINT16                Data
  );

STATIC
EFI_STATUS
StatusDRQClear (
  ATAPI_EXT_SCSI_PASS_THRU_DEV        *AtapiScsiPrivate,
  UINT64                              TimeoutInMicroSeconds
  );

STATIC  
EFI_STATUS
AltStatusDRQClear (
  ATAPI_EXT_SCSI_PASS_THRU_DEV        *AtapiScsiPrivate,
  UINT64                              TimeoutInMicroSeconds
  );

STATIC  
EFI_STATUS
StatusDRQReady (
  ATAPI_EXT_SCSI_PASS_THRU_DEV        *AtapiScsiPrivate,
  UINT64                              TimeoutInMicroSeconds
  );

STATIC  
EFI_STATUS
StatusWaitForBSYClear (
  ATAPI_EXT_SCSI_PASS_THRU_DEV    *AtapiScsiPrivate,
  UINT64                          TimeoutInMicroSeconds
  );


STATIC  
EFI_STATUS
AltStatusWaitForBSYClear (
  ATAPI_EXT_SCSI_PASS_THRU_DEV    *AtapiScsiPrivate,
  UINT64                          TimeoutInMicroSeconds
  );

STATIC  
EFI_STATUS
StatusDRDYReady (
  ATAPI_EXT_SCSI_PASS_THRU_DEV     *AtapiScsiPrivate,
  UINT64                           TimeoutInMicroSeconds
  );

STATIC  
EFI_STATUS
AltStatusDRDYReady (
  ATAPI_EXT_SCSI_PASS_THRU_DEV     *AtapiScsiPrivate,
  UINT64                           TimeoutInMicroSeconds
  );

STATIC  
EFI_STATUS
AtapiPassThruCheckErrorStatus (
  ATAPI_EXT_SCSI_PASS_THRU_DEV        *AtapiScsiPrivate
  );

STATIC
VOID
InitAtapiIoPortRegisters (
  IN  ATAPI_EXT_SCSI_PASS_THRU_DEV      *AtapiScsiPrivate,
  IN  IDE_REGISTERS_BASE_ADDR           *IdeRegsBaseAddr
  )
;

STATIC
EFI_STATUS
GetIdeRegistersBaseAddr (
  IN  EFI_PCI_IO_PROTOCOL         *PciIo,
  OUT IDE_REGISTERS_BASE_ADDR     *IdeRegsBaseAddr
  )
;

static SCSI_COMMAND_SET     EndTable = { 0xff, 0xff };

//
// This table contains all the supported ATAPI commands.
//
static SCSI_COMMAND_SET     SupportedATAPICommands[] = {
    OP_INQUIRY,                     DataIn,
    OP_LOAD_UNLOAD_CD,              NoData,
    OP_MECHANISM_STATUS,            DataIn,
    OP_MODE_SELECT_10,              DataOut,
    OP_MODE_SENSE_10,               DataIn,
    OP_PAUSE_RESUME,                NoData,
    OP_PLAY_AUDIO_10,               DataIn,
    OP_PLAY_AUDIO_MSF,              DataIn,
    OP_PLAY_CD,                     DataIn,
    OP_PLAY_CD_MSF,                 DataIn,
    OP_PREVENT_ALLOW_MEDIUM_REMOVAL,NoData,
    OP_READ_10,                     DataIn,
    OP_READ_12,                     DataIn,
    OP_READ_CAPACITY,               DataIn,
    OP_READ_CD,                     DataIn,
    OP_READ_CD_MSF,                 DataIn,
    OP_READ_HEADER,                 DataIn,
    OP_READ_SUB_CHANNEL,            DataIn,
    OP_READ_TOC,                    DataIn,
    OP_REQUEST_SENSE,               DataIn,
    OP_SCAN,                        NoData,
    OP_SEEK_10,                     NoData,
    OP_SET_CD_SPEED,                DataOut,
    OP_STOPPLAY_SCAN,               NoData,
    OP_START_STOP_UNIT,             NoData,
    OP_TEST_UNIT_READY,             NoData,
    OP_FORMAT_UNIT,                 DataOut,
    OP_READ_FORMAT_CAPACITIES,      DataIn,
    OP_VERIFY,                      DataOut,
    OP_WRITE_10,                    DataOut,
    OP_WRITE_12,                    DataOut,
    OP_WRITE_AND_VERIFY,            DataOut,
    0xff,                           0xff
};

static CHAR16               ControllerNameString[]  = L"ATAPI Controller";
static CHAR16               AtapiChannelString[]    = L"ATAPI Channel";

EFI_DRIVER_BINDING_PROTOCOL gAtapiExtScsiPassThruDriverBinding = {
  AtapiExtScsiPassThruDriverBindingSupported,
  AtapiExtScsiPassThruDriverBindingStart,
  AtapiExtScsiPassThruDriverBindingStop,
  0xa,
  NULL,
  NULL
};


EFI_DRIVER_ENTRY_POINT (AtapiExtScsiPassThruDriverEntryPoint)

EFI_STATUS
EFIAPI
AtapiExtScsiPassThruDriverEntryPoint (
  IN EFI_HANDLE         ImageHandle,
  IN EFI_SYSTEM_TABLE   *SystemTable
  )
/*++

Routine Description:

  Entry point for EFI drivers.

Arguments:

  ImageHandle - EFI_HANDLE
  SystemTable - EFI_SYSTEM_TABLE

Returns:

  EFI_SUCCESS
  Others 
  
--*/
{
  return INSTALL_ALL_DRIVER_PROTOCOLS_OR_PROTOCOLS2 (
          ImageHandle,
          SystemTable,
          &gAtapiExtScsiPassThruDriverBinding,
          ImageHandle,
          &gAtapiScsiPassThruComponentName,
          NULL,
          NULL
          );
}

EFI_STATUS
EFIAPI
AtapiExtScsiPassThruDriverBindingSupported (
  IN EFI_DRIVER_BINDING_PROTOCOL  *This,
  IN EFI_HANDLE                   Controller,
  IN EFI_DEVICE_PATH_PROTOCOL     *RemainingDevicePath
  )
/*++
  
Routine Description:
  Test to see if this driver supports ControllerHandle. Any ControllerHandle
  that has gEfiPciIoProtocolGuid installed and is IDE Controller it will be supported.
    
Arguments:

  This                - Protocol instance pointer.
  Controller          - Handle of device to test
  RemainingDevicePath - Not used
    
Returns:
    EFI_STATUS
  
--*/
{
  EFI_STATUS          Status;
  EFI_PCI_IO_PROTOCOL *PciIo;
  PCI_TYPE00          Pci;

  //
  // Open the IO Abstraction(s) needed to perform the supported test
  //
  Status = gBS->OpenProtocol (
                  Controller,
                  &gEfiPciIoProtocolGuid,
                  (VOID **) &PciIo,
                  This->DriverBindingHandle,
                  Controller,
                  EFI_OPEN_PROTOCOL_BY_DRIVER
                  );
  if (EFI_ERROR (Status)) {
    return Status;
  }
  //
  // Use the PCI I/O Protocol to see if Controller is a IDE Controller that
  // can be managed by this driver.  Read the PCI Configuration Header
  // for this device.
  //
  Status = PciIo->Pci.Read (
                    PciIo,
                    EfiPciIoWidthUint32,
                    0,
                    sizeof (Pci) / sizeof (UINT32),
                    &Pci
                    );
  if (EFI_ERROR (Status)) {
    gBS->CloseProtocol (
           Controller,
           &gEfiPciIoProtocolGuid,
           This->DriverBindingHandle,
           Controller
           );
    return EFI_UNSUPPORTED;
  }

  if (Pci.Hdr.ClassCode[2] != PCI_CLASS_MASS_STORAGE || Pci.Hdr.ClassCode[1] != PCI_CLASS_IDE) {

    Status = EFI_UNSUPPORTED;
  }

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
AtapiExtScsiPassThruDriverBindingStart (
  IN EFI_DRIVER_BINDING_PROTOCOL  *This,
  IN EFI_HANDLE                   Controller,
  IN EFI_DEVICE_PATH_PROTOCOL     *RemainingDevicePath
  )
/*++
  
Routine Description:
  Create handles for IDE channels specified by RemainingDevicePath.
  Install Extended SCSI Pass Thru Protocol onto each created handle.
  
Arguments:

  This                - Protocol instance pointer.
  Controller          - Handle of device to test
  RemainingDevicePath - Not used
    
Returns:
  EFI_STATUS
    
--*/
{
  EFI_STATUS          Status;
  EFI_STATUS          DisableStatus;
  EFI_PCI_IO_PROTOCOL *PciIo;
  UINT64              Supports;

  PciIo = NULL;
  Status = gBS->OpenProtocol (
                  Controller,
                  &gEfiPciIoProtocolGuid,
                  (VOID **) &PciIo,
                  This->DriverBindingHandle,
                  Controller,
                  EFI_OPEN_PROTOCOL_BY_DRIVER
                  );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = PciIo->Attributes (
                    PciIo,
                    EfiPciIoAttributeOperationSupported,
                    0,
                    &Supports
                    );
  if (!EFI_ERROR (Status)) {
    Supports &= (EFI_PCI_DEVICE_ENABLE               |
                 EFI_PCI_IO_ATTRIBUTE_IDE_PRIMARY_IO |
                 EFI_PCI_IO_ATTRIBUTE_IDE_SECONDARY_IO);
    Status = PciIo->Attributes (
                      PciIo,
                      EfiPciIoAttributeOperationEnable,
                      Supports,
                      NULL
                      );
  }

  if (EFI_ERROR (Status)) {
    goto Done;
  }

  //
  // Create Extended SCSI Pass Thru instance for the IDE channel.
  //
  Status = RegisterAtapiExtScsiPassThru (This, Controller, PciIo);

Done:
  if (EFI_ERROR (Status)) {
    if (PciIo) {
      DisableStatus = PciIo->Attributes (
                               PciIo,
                               EfiPciIoAttributeOperationSupported,
                               0,
                               &Supports
                               );
      if (!EFI_ERROR (DisableStatus)) {
        Supports &= (EFI_PCI_DEVICE_ENABLE               |
                     EFI_PCI_IO_ATTRIBUTE_IDE_PRIMARY_IO |
                     EFI_PCI_IO_ATTRIBUTE_IDE_SECONDARY_IO);
        DisableStatus = PciIo->Attributes (
                                 PciIo,
                                 EfiPciIoAttributeOperationDisable,
                                 Supports,
                                 NULL
                                 );
      }
    }

    gBS->CloseProtocol (
           Controller,
           &gEfiPciIoProtocolGuid,
           This->DriverBindingHandle,
           Controller
           );
  }

  return Status;
}

EFI_STATUS
EFIAPI
AtapiExtScsiPassThruDriverBindingStop (
  IN  EFI_DRIVER_BINDING_PROTOCOL     *This,
  IN  EFI_HANDLE                      Controller,
  IN  UINTN                           NumberOfChildren,
  IN  EFI_HANDLE                      *ChildHandleBuffer
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

  EFI_STATUS
  
--*/
{
  EFI_STATUS                       Status;
  EFI_EXT_SCSI_PASS_THRU_PROTOCOL  *ScsiPassThru;
  ATAPI_EXT_SCSI_PASS_THRU_DEV     *AtapiScsiPrivate;
  UINT64                           Supports;

  Status = gBS->OpenProtocol (
                  Controller,
                  &gEfiExtScsiPassThruProtocolGuid,
                  (VOID **) &ScsiPassThru,
                  This->DriverBindingHandle,
                  Controller,
                  EFI_OPEN_PROTOCOL_GET_PROTOCOL
                  );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  AtapiScsiPrivate = ATAPI_EXT_SCSI_PASS_THRU_DEV_FROM_THIS (ScsiPassThru);

  Status = gBS->UninstallProtocolInterface (
                  Controller,
                  &gEfiExtScsiPassThruProtocolGuid,
                  &AtapiScsiPrivate->ScsiPassThru
                  );
  if (EFI_ERROR (Status)) {
    return Status;
  }
  //
  // Release Pci Io protocol on the controller handle.
  //
  Status = AtapiScsiPrivate->PciIo->Attributes (
                                      AtapiScsiPrivate->PciIo,
                                      EfiPciIoAttributeOperationSupported,
                                      0,
                                      &Supports
                                      );
  if (!EFI_ERROR (Status)) {
    Supports &= (EFI_PCI_DEVICE_ENABLE               |
                 EFI_PCI_IO_ATTRIBUTE_IDE_PRIMARY_IO |
                 EFI_PCI_IO_ATTRIBUTE_IDE_SECONDARY_IO);
    Status = AtapiScsiPrivate->PciIo->Attributes (
                                        AtapiScsiPrivate->PciIo,
                                        EfiPciIoAttributeOperationDisable,
                                        Supports,
                                        NULL
                                        );
  }

  gBS->CloseProtocol (
         Controller,
         &gEfiPciIoProtocolGuid,
         This->DriverBindingHandle,
         Controller
         );

  gBS->FreePool (AtapiScsiPrivate);

  return EFI_SUCCESS;
}

EFI_STATUS
RegisterAtapiExtScsiPassThru (
  IN  EFI_DRIVER_BINDING_PROTOCOL  *This,
  IN  EFI_HANDLE                   Controller,
  IN  EFI_PCI_IO_PROTOCOL          *PciIo
  )
/*++
  
Routine Description:
  Attaches SCSI Pass Thru Protocol for specified IDE channel.
    
Arguments:
  This              - Protocol instance pointer.
  Controller        - Parent device handle to the IDE channel.    
  PciIo             - PCI I/O protocol attached on the "Controller".                        
  
Returns:

  Always return EFI_SUCCESS unless installing SCSI Pass Thru Protocol failed.

--*/
{
  EFI_STATUS                    Status;
  UINT64                        Supports;
  ATAPI_EXT_SCSI_PASS_THRU_DEV  *AtapiScsiPrivate;
  IDE_REGISTERS_BASE_ADDR       IdeRegsBaseAddr[ATAPI_MAX_CHANNEL];
  

  AtapiScsiPrivate = EfiLibAllocateZeroPool (sizeof (ATAPI_EXT_SCSI_PASS_THRU_DEV));
  if (AtapiScsiPrivate == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  EfiCopyMem (AtapiScsiPrivate->ChannelName, AtapiChannelString, sizeof (AtapiChannelString));
  EfiCopyMem (AtapiScsiPrivate->ControllerName, ControllerNameString, sizeof (ControllerNameString));

  //
  // Enable channel
  //
  Status = PciIo->Attributes (
                    PciIo,
                    EfiPciIoAttributeOperationSupported,
                    0,
                    &Supports
                    );
  if (!EFI_ERROR (Status)) {
    Supports &= (EFI_PCI_DEVICE_ENABLE               |
                 EFI_PCI_IO_ATTRIBUTE_IDE_PRIMARY_IO |
                 EFI_PCI_IO_ATTRIBUTE_IDE_SECONDARY_IO);
    Status = PciIo->Attributes (
                      PciIo,
                      EfiPciIoAttributeOperationEnable,
                      Supports,
                      NULL
                      );
  }

  AtapiScsiPrivate->Signature = ATAPI_SCSI_PASS_THRU_DEV_SIGNATURE;
  AtapiScsiPrivate->Handle    = Controller;

  //
  // will reset the IoPort inside each API function.
  //
  AtapiScsiPrivate->IoPort  = NULL;
  AtapiScsiPrivate->PciIo   = PciIo;

  //
  // Obtain IDE IO port registers' base addresses
  //
  Status = GetIdeRegistersBaseAddr (PciIo, IdeRegsBaseAddr);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  InitAtapiIoPortRegisters(AtapiScsiPrivate, IdeRegsBaseAddr);

  //
  // initialize SCSI Pass Thru Protocol interface
  //
  AtapiScsiPrivate->ScsiPassThru.Mode             = &AtapiScsiPrivate->ScsiPassThruMode;
  AtapiScsiPrivate->ScsiPassThru.PassThru         = AtapiExtScsiPassThruFunction;
  AtapiScsiPrivate->ScsiPassThru.GetNextTargetLun = AtapiExtScsiPassThruGetNextTargetLun;
  AtapiScsiPrivate->ScsiPassThru.BuildDevicePath  = AtapiExtScsiPassThruBuildDevicePath;
  AtapiScsiPrivate->ScsiPassThru.GetTargetLun     = AtapiExtScsiPassThruGetTargetLun;
  AtapiScsiPrivate->ScsiPassThru.ResetChannel     = AtapiExtScsiPassThruResetChannel;
  AtapiScsiPrivate->ScsiPassThru.ResetTargetLun   = AtapiExtScsiPassThruResetTarget;
  AtapiScsiPrivate->ScsiPassThru.GetNextTarget    = AtapiExtScsiPassThruGetNextTarget;

  //
  // Set Mode
  //
  AtapiScsiPrivate->ScsiPassThruMode.AdapterId      = 4;

  //
  // non-RAID SCSI controllers should set both physical and logical attributes
  //
  AtapiScsiPrivate->ScsiPassThruMode.Attributes = EFI_SCSI_PASS_THRU_ATTRIBUTES_PHYSICAL | 
                                                  EFI_SCSI_PASS_THRU_ATTRIBUTES_LOGICAL;
  AtapiScsiPrivate->ScsiPassThruMode.IoAlign = 0;

  //
  // Initialize the LatestTargetId to MAX_TARGET_ID.
  //
  AtapiScsiPrivate->LatestTargetId  = MAX_TARGET_ID;
  AtapiScsiPrivate->LatestLun       = 0;

  Status = gBS->InstallProtocolInterface (
                  &Controller,
                  &gEfiExtScsiPassThruProtocolGuid,
                  EFI_NATIVE_INTERFACE,
                  &AtapiScsiPrivate->ScsiPassThru
                  );
  return Status;
}

EFI_STATUS
EFIAPI
AtapiExtScsiPassThruFunction (
  IN EFI_EXT_SCSI_PASS_THRU_PROTOCOL                    *This,
  IN UINT8                                              *Target,
  IN UINT64                                             Lun,
  IN OUT EFI_EXT_SCSI_PASS_THRU_SCSI_REQUEST_PACKET     *Packet,
  IN EFI_EVENT                                          Event OPTIONAL
  )
/*++

Routine Description:

  Implements EFI_EXT_SCSI_PASS_THRU_PROTOCOL.PassThru() function.

Arguments:

  This:     The EFI_EXT_SCSI_PASS_THRU_PROTOCOL instance.
  Target:   The Target ID of the ATAPI device to send the SCSI 
            Request Packet. To ATAPI devices attached on an IDE
            Channel, Target ID 0 indicates Master device;Target
            ID 1 indicates Slave device.
  Lun:      The LUN of the ATAPI device to send the SCSI Request
            Packet. To the ATAPI device, Lun is always 0.
  Packet:   The SCSI Request Packet to send to the ATAPI device 
            specified by Target and Lun.
  Event:    If non-blocking I/O is not supported then Event is ignored, 
            and blocking I/O is performed.
            If Event is NULL, then blocking I/O is performed.
            If Event is not NULL and non blocking I/O is supported, 
            then non-blocking I/O is performed, and Event will be signaled 
            when the SCSI Request Packet completes.      

Returns:  

   EFI_STATUS
   
--*/
{
  EFI_STATUS                          Status;
  ATAPI_EXT_SCSI_PASS_THRU_DEV        *AtapiScsiPrivate;
  UINT8                                TargetId;
  
  AtapiScsiPrivate = ATAPI_EXT_SCSI_PASS_THRU_DEV_FROM_THIS (This);

  //
  // For ATAPI device, UINT8 is enough to represent the SCSI ID on channel.
  //
  TargetId = Target[0];
  
  //
  // Target is not allowed beyond MAX_TARGET_ID
  //
  if ((TargetId > MAX_TARGET_ID) || (Lun != 0)) {
    return EFI_INVALID_PARAMETER;
  }
  
  //
  // check the data fields in Packet parameter.
  //
  Status = CheckSCSIRequestPacket (Packet);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  //
  // If Request Packet targets at the IDE channel itself,
  // do nothing.
  //
  if (TargetId == (UINT8)This->Mode->AdapterId) {
    Packet->InTransferLength = Packet->OutTransferLength = 0;
    return EFI_SUCCESS;
  }
  
  //
  // According to Target ID, reset the Atapi I/O Register mapping
  // (Target Id in [0,1] area, using AtapiIoPortRegisters[0],
  //  Target Id in [2,3] area, using AtapiIoPortRegisters[1]
  //
  if ((TargetId / 2) == 0) {
    TargetId = TargetId % 2;
    AtapiScsiPrivate->IoPort = &AtapiScsiPrivate->AtapiIoPortRegisters[0];
  } else {
    TargetId = TargetId % 2;
    AtapiScsiPrivate->IoPort = &AtapiScsiPrivate->AtapiIoPortRegisters[1];
  }
  
  //
  // the ATAPI SCSI interface does not support non-blocking I/O
  // ignore the Event parameter
  //
  // Performs blocking I/O.
  //
  Status = SubmitBlockingIoCommand (AtapiScsiPrivate, TargetId, Packet);
  return Status;
}

EFI_STATUS
EFIAPI
AtapiExtScsiPassThruGetNextTargetLun (
  IN  EFI_EXT_SCSI_PASS_THRU_PROTOCOL    *This,
  IN OUT UINT8                           **Target,
  IN OUT UINT64                          *Lun
  )
/*++

Routine Description:

  Used to retrieve the list of legal Target IDs for SCSI devices 
  on a SCSI channel.

Arguments:

  This                  - Protocol instance pointer.
  Target                - On input, a pointer to the Target ID of a SCSI 
                          device present on the SCSI channel.  On output, 
                          a pointer to the Target ID of the next SCSI device
                          present on a SCSI channel.  An input value of 
                          0xFFFFFFFF retrieves the Target ID of the first 
                          SCSI device present on a SCSI channel.
  Lun                   - On input, a pointer to the LUN of a SCSI device
                          present on the SCSI channel. On output, a pointer
                          to the LUN of the next SCSI device present on 
                          a SCSI channel.
Returns:

  EFI_SUCCESS           - The Target ID and Lun of the next SCSI device 
                          on the SCSI channel was returned in Target and Lun.
  EFI_NOT_FOUND         - There are no more SCSI devices on this SCSI channel.
  EFI_INVALID_PARAMETER - Target is not 0xFFFFFFFF,and Target and Lun were not
                           returned on a previous call to GetNextDevice().
--*/
{
  UINT8                          ByteIndex;
  UINT8                          TargetId;
  UINT8                          ScsiId[TARGET_MAX_BYTES];
  ATAPI_EXT_SCSI_PASS_THRU_DEV   *AtapiScsiPrivate; 

  //
  // Retrieve Device Private Data Structure.
  //
  AtapiScsiPrivate = ATAPI_EXT_SCSI_PASS_THRU_DEV_FROM_THIS (This);

  //
  // Check whether Target is valid.
  //
  if (*Target == NULL || Lun == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  EfiSetMem (ScsiId, TARGET_MAX_BYTES,0xFF);

  TargetId = (*Target)[0];

  //
  // For ATAPI device, we use UINT8 to represent the SCSI ID on channel.
  //
  if (EfiCompareMem(*Target, ScsiId, TARGET_MAX_BYTES) != 0) {
    for (ByteIndex = 1; ByteIndex < TARGET_MAX_BYTES; ByteIndex++) {
      if ((*Target)[ByteIndex] != 0) {
        return EFI_INVALID_PARAMETER;
      }
    } 
  }
  
  if ((EfiCompareMem(*Target, ScsiId, TARGET_MAX_BYTES) != 0) &&
      ((TargetId != AtapiScsiPrivate->LatestTargetId) ||
      (*Lun != AtapiScsiPrivate->LatestLun))) {
    return EFI_INVALID_PARAMETER;
  }

  if (TargetId == MAX_TARGET_ID) {
    return EFI_NOT_FOUND;
  }

  if (EfiCompareMem(*Target, ScsiId, TARGET_MAX_BYTES) == 0) {
    EfiSetMem (*Target, TARGET_MAX_BYTES,0);
  } else {
    (*Target)[0] = AtapiScsiPrivate->LatestTargetId + 1;
  }

  *Lun = 0;

  //
  // Update the LatestTargetId.
  //
  AtapiScsiPrivate->LatestTargetId  = (*Target)[0];
  AtapiScsiPrivate->LatestLun       = *Lun;

  return EFI_SUCCESS;

}

EFI_STATUS
EFIAPI
AtapiExtScsiPassThruBuildDevicePath (
  IN     EFI_EXT_SCSI_PASS_THRU_PROTOCOL    *This,
  IN     UINT8                              *Target,
  IN     UINT64                             Lun,
  IN OUT EFI_DEVICE_PATH_PROTOCOL           **DevicePath
  )
/*++

Routine Description:

  Used to allocate and build a device path node for a SCSI device 
  on a SCSI channel. Would not build device path for a SCSI Host Controller.

Arguments:

  This                  - Protocol instance pointer.
  Target                - The Target ID of the SCSI device for which
                          a device path node is to be allocated and built.
  Lun                   - The LUN of the SCSI device for which a device 
                          path node is to be allocated and built.
  DevicePath            - A pointer to a single device path node that 
                          describes the SCSI device specified by 
                          Target and Lun. This function is responsible 
                          for allocating the buffer DevicePath with the boot
                          service AllocatePool().  It is the caller's 
                          responsibility to free DevicePath when the caller
                          is finished with DevicePath.    
  Returns:
  EFI_SUCCESS           - The device path node that describes the SCSI device
                          specified by Target and Lun was allocated and 
                          returned in DevicePath.
  EFI_NOT_FOUND         - The SCSI devices specified by Target and Lun does
                          not exist on the SCSI channel.
  EFI_INVALID_PARAMETER - DevicePath is NULL.
  EFI_OUT_OF_RESOURCES  - There are not enough resources to allocate 
                          DevicePath.
--*/
{
  EFI_DEV_PATH                   *Node;
  UINT8                          TargetId;
  ATAPI_EXT_SCSI_PASS_THRU_DEV   *AtapiScsiPrivate;

  //
  // Retrieve Device Private Data Structure.
  //
  AtapiScsiPrivate = ATAPI_EXT_SCSI_PASS_THRU_DEV_FROM_THIS (This);
  TargetId = Target[0];  

  //
  // Validate parameters passed in.
  //
  
  if (DevicePath == NULL) {
    return EFI_INVALID_PARAMETER;
  }
  
  //
  // can not build device path for the SCSI Host Controller.
  //
  if ((TargetId > (MAX_TARGET_ID - 1)) || (Lun != 0)) {
    return EFI_NOT_FOUND;
  }

  Node = EfiLibAllocateZeroPool (sizeof (EFI_DEV_PATH));
  if (Node == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  Node->DevPath.Type    = MESSAGING_DEVICE_PATH;
  Node->DevPath.SubType = MSG_ATAPI_DP;
  SetDevicePathNodeLength (&Node->DevPath, sizeof (ATAPI_DEVICE_PATH));

  Node->Atapi.PrimarySecondary  = (UINT8) (TargetId / 2);
  Node->Atapi.SlaveMaster       = (UINT8) (TargetId % 2);
  Node->Atapi.Lun               = (UINT16) Lun;

  *DevicePath                   = (EFI_DEVICE_PATH_PROTOCOL *) Node;

  return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
AtapiExtScsiPassThruGetTargetLun (
  IN  EFI_EXT_SCSI_PASS_THRU_PROTOCOL    *This,
  IN  EFI_DEVICE_PATH_PROTOCOL           *DevicePath,
  OUT UINT8                              **Target,
  OUT UINT64                             *Lun
  )
/*++

Routine Description:

  Used to translate a device path node to a Target ID and LUN.

Arguments:

  This                  - Protocol instance pointer.
  DevicePath            - A pointer to the device path node that 
                          describes a SCSI device on the SCSI channel.
  Target                - A pointer to the Target ID of a SCSI device 
                          on the SCSI channel. 
  Lun                   - A pointer to the LUN of a SCSI device on 
                          the SCSI channel.    
Returns:

  EFI_SUCCESS           - DevicePath was successfully translated to a 
                          Target ID and LUN, and they were returned 
                          in Target and Lun.
  EFI_INVALID_PARAMETER - DevicePath/Target/Lun is NULL.
  EFI_UNSUPPORTED       - This driver does not support the device path 
                          node type in DevicePath.
  EFI_NOT_FOUND         - A valid translation from DevicePath to a 
                          Target ID and LUN does not exist.
--*/
{
  EFI_DEV_PATH  *Node;

  //
  // Validate parameters passed in.
  //
  if (DevicePath == NULL || Target == NULL || Lun == NULL) {
    return EFI_INVALID_PARAMETER;
  }
  
  //
  // Check whether the DevicePath belongs to SCSI_DEVICE_PATH
  //
  if ((DevicePath->Type != MESSAGING_DEVICE_PATH) ||
      (DevicePath->SubType != MSG_ATAPI_DP) ||
      (DevicePathNodeLength(DevicePath) != sizeof(ATAPI_DEVICE_PATH))) {
    return EFI_UNSUPPORTED;
  }

  EfiZeroMem (*Target, TARGET_MAX_BYTES);

  Node    = (EFI_DEV_PATH *) DevicePath;

  (*Target)[0] = Node->Atapi.PrimarySecondary * 2 + Node->Atapi.SlaveMaster;
  *Lun    = Node->Atapi.Lun;

  if ((*Target)[0] > (MAX_TARGET_ID - 1) || *Lun != 0) {
    return EFI_NOT_FOUND;
  }

  return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
AtapiExtScsiPassThruResetChannel (
  IN  EFI_EXT_SCSI_PASS_THRU_PROTOCOL   *This
  )
/*++

Routine Description:

  Resets a SCSI channel.This operation resets all the 
  SCSI devices connected to the SCSI channel.

Arguments:

  This                  - Protocol instance pointer.

Returns:

  EFI_SUCCESS           - The SCSI channel was reset.
  EFI_UNSUPPORTED       - The SCSI channel does not support 
                          a channel reset operation.
  EFI_DEVICE_ERROR      - A device error occurred while 
                          attempting to reset the SCSI channel.
  EFI_TIMEOUT           - A timeout occurred while attempting 
                          to reset the SCSI channel.
--*/
{
  UINT8                         DeviceControlValue;
  UINT8                         Index;
  ATAPI_EXT_SCSI_PASS_THRU_DEV  *AtapiScsiPrivate;
  BOOLEAN                        ResetFlag;

  AtapiScsiPrivate = ATAPI_EXT_SCSI_PASS_THRU_DEV_FROM_THIS (This);
  ResetFlag = FALSE;
  //
  // Reset both Primary channel and Secondary channel.
  // so, the IoPort pointer must point to the right I/O Register group
  // And if there is a channel reset successfully, return EFI_SUCCESS.
  //
  for (Index = 0; Index < 2; Index++) {
    //
    // Reset
    //
    AtapiScsiPrivate->IoPort  = &AtapiScsiPrivate->AtapiIoPortRegisters[Index];

    DeviceControlValue        = 0;
    //
    // set SRST bit to initiate soft reset
    //
    DeviceControlValue |= SRST;
    //
    // disable Interrupt
    //
    DeviceControlValue |= bit (1);
    WritePortB (
      AtapiScsiPrivate->PciIo,
      AtapiScsiPrivate->IoPort->Alt.DeviceControl,
      DeviceControlValue
      );

    //
    // Wait 10us
    //
    gBS->Stall (10);

    //
    // Clear SRST bit
    // 0xfb:1111,1011
    //
    DeviceControlValue &= 0xfb;
    
    WritePortB (AtapiScsiPrivate->PciIo, AtapiScsiPrivate->IoPort->Alt.DeviceControl, DeviceControlValue);

    //
    // slave device needs at most 31s to clear BSY
    //
    if (StatusWaitForBSYClear (AtapiScsiPrivate, 31000000) != EFI_TIMEOUT) {
      ResetFlag = TRUE;
    }
  }

  if (ResetFlag) {
    return EFI_SUCCESS;
  }
  
  return EFI_TIMEOUT;
}

EFI_STATUS
EFIAPI
AtapiExtScsiPassThruResetTarget (
  IN EFI_EXT_SCSI_PASS_THRU_PROTOCOL    *This,
  IN UINT8                              *Target,
  IN UINT64                             Lun
  )
/*++

Routine Description:

  Resets a SCSI device that is connected to a SCSI channel.

Arguments:

  This                  - Protocol instance pointer.
  Target                - The Target ID of the SCSI device to reset. 
  Lun                   - The LUN of the SCSI device to reset.
    
Returns:

  EFI_SUCCESS           - The SCSI device specified by Target and 
                          Lun was reset.
  EFI_UNSUPPORTED       - The SCSI channel does not support a target
                          reset operation.
  EFI_INVALID_PARAMETER - Target or Lun are invalid.
  EFI_DEVICE_ERROR      - A device error occurred while attempting 
                          to reset the SCSI device specified by Target 
                          and Lun.
  EFI_TIMEOUT           - A timeout occurred while attempting to reset 
                          the SCSI device specified by Target and Lun.
--*/
{
  UINT8                         Command;
  UINT8                         DeviceSelect;
  UINT8                         TargetId;
  ATAPI_EXT_SCSI_PASS_THRU_DEV  *AtapiScsiPrivate;
  
  AtapiScsiPrivate = ATAPI_EXT_SCSI_PASS_THRU_DEV_FROM_THIS (This);
  TargetId = Target[0];
  
  if ((TargetId > MAX_TARGET_ID) || (Lun != 0)) {
    return EFI_INVALID_PARAMETER;
  }
  //
  // Directly return EFI_SUCCESS if want to reset the host controller
  //
  if (TargetId == This->Mode->AdapterId) {
    return EFI_SUCCESS;
  }
  
  //
  // According to Target ID, reset the Atapi I/O Register mapping
  // (Target Id in [0,1] area, using AtapiIoPortRegisters[0],
  //  Target Id in [2,3] area, using AtapiIoPortRegisters[1]
  //
  if ((TargetId / 2) == 0) {
    AtapiScsiPrivate->IoPort = &AtapiScsiPrivate->AtapiIoPortRegisters[0];
  } else {
    AtapiScsiPrivate->IoPort = &AtapiScsiPrivate->AtapiIoPortRegisters[1];
  }
  
  //
  // for ATAPI device, no need to wait DRDY ready after device selecting.
  //
  // bit7 and bit5 are both set to 1 for backward compatibility
  //
  DeviceSelect = (UINT8) (((bit (7) | bit (5)) | (TargetId << 4)));
  WritePortB (AtapiScsiPrivate->PciIo, AtapiScsiPrivate->IoPort->Head, DeviceSelect);

  Command = ATAPI_SOFT_RESET_CMD;
  WritePortB (AtapiScsiPrivate->PciIo, AtapiScsiPrivate->IoPort->Reg.Command, Command);

  //
  // BSY clear is the only status return to the host by the device
  // when reset is complete.
  // slave device needs at most 31s to clear BSY
  //
  if (EFI_ERROR (StatusWaitForBSYClear (AtapiScsiPrivate, 31000000))) {
    return EFI_TIMEOUT;
  }
  
  //
  // stall 5 seconds to make the device status stable
  //
  gBS->Stall (5000000);

  return EFI_SUCCESS;
}


EFI_STATUS
EFIAPI
AtapiExtScsiPassThruGetNextTarget (
  IN  EFI_EXT_SCSI_PASS_THRU_PROTOCOL    *This,
  IN OUT UINT8                           **Target
  )
/*++

Routine Description:
  Used to retrieve the list of legal Target IDs for SCSI devices 
  on a SCSI channel.

Arguments:
  This                  - Protocol instance pointer.
  Target                - On input, a pointer to the Target ID of a SCSI 
                          device present on the SCSI channel.  On output, 
                          a pointer to the Target ID of the next SCSI device
                           present on a SCSI channel.  An input value of 
                           0xFFFFFFFF retrieves the Target ID of the first 
                           SCSI device present on a SCSI channel.
  Lun                   - On input, a pointer to the LUN of a SCSI device
                          present on the SCSI channel. On output, a pointer
                          to the LUN of the next SCSI device present on 
                          a SCSI channel.
    
Returns:
  EFI_SUCCESS           - The Target ID and Lun of the next SCSI device 
                          on the SCSI channel was returned in Target and Lun.
  EFI_NOT_FOUND         - There are no more SCSI devices on this SCSI channel.
  EFI_INVALID_PARAMETER - Target is not 0xFFFFFFFF,and Target and Lun were not
                          returned on a previous call to GetNextDevice().
--*/
{
  UINT8                         TargetId;
  UINT8                         ScsiId[TARGET_MAX_BYTES];
  ATAPI_EXT_SCSI_PASS_THRU_DEV  *AtapiScsiPrivate;  
  UINT8                         ByteIndex;

  //
  // Retrieve Device Private Data Structure.
  //
  AtapiScsiPrivate = ATAPI_EXT_SCSI_PASS_THRU_DEV_FROM_THIS (This);

  //
  // Check whether Target is valid.
  //
  if (*Target == NULL ) {
    return EFI_INVALID_PARAMETER;
  }

  TargetId = (*Target)[0];
  EfiSetMem (ScsiId, TARGET_MAX_BYTES,0xFF);

  //
  // For ATAPI device, we use UINT8 to represent the SCSI ID on channel.
  //
  if (EfiCompareMem(*Target, ScsiId, TARGET_MAX_BYTES) != 0) {
    for (ByteIndex = 1; ByteIndex < TARGET_MAX_BYTES; ByteIndex++) {
      if ((*Target)[ByteIndex] != 0) {
        return EFI_INVALID_PARAMETER;
      }
    } 
  }

  if ((EfiCompareMem(*Target, ScsiId, TARGET_MAX_BYTES) != 0) &&(TargetId != AtapiScsiPrivate->LatestTargetId)) {
    return EFI_INVALID_PARAMETER;
  }

  if (TargetId == MAX_TARGET_ID) {
    return EFI_NOT_FOUND;
  }

  if ((EfiCompareMem(*Target, ScsiId, TARGET_MAX_BYTES) == 0)) {
    EfiSetMem (*Target, TARGET_MAX_BYTES,0);
  } else {
    (*Target)[0] = AtapiScsiPrivate->LatestTargetId + 1;
  }

  //
  // Update the LatestTargetId.
  //
  AtapiScsiPrivate->LatestTargetId  = (*Target)[0];
  AtapiScsiPrivate->LatestLun       = 0;

  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
GetIdeRegistersBaseAddr (
  IN  EFI_PCI_IO_PROTOCOL         *PciIo,
  OUT IDE_REGISTERS_BASE_ADDR     *IdeRegsBaseAddr
  )
/*++

Routine Description:
  Get IDE IO port registers' base addresses by mode. In 'Compatibility' mode,
  use fixed addresses. In Native-PCI mode, get base addresses from BARs in
  the PCI IDE controller's Configuration Space.

Arguments:
  PciIo             - Pointer to the EFI_PCI_IO_PROTOCOL instance
  IdeRegsBaseAddr   - Pointer to IDE_REGISTERS_BASE_ADDR to 
                      receive IDE IO port registers' base addresses
                      
Returns:

  EFI_STATUS
    
--*/
{
  EFI_STATUS  Status;
  PCI_TYPE00  PciData;

  Status = PciIo->Pci.Read (
                        PciIo,
                        EfiPciIoWidthUint8,
                        0,
                        sizeof (PciData),
                        &PciData
                        );

  if (EFI_ERROR (Status)) {
    return Status;
  }

  if ((PciData.Hdr.ClassCode[0] & IDE_PRIMARY_OPERATING_MODE) == 0) {
    IdeRegsBaseAddr[IdePrimary].CommandBlockBaseAddr  = 0x1f0;
    IdeRegsBaseAddr[IdePrimary].ControlBlockBaseAddr  = 0x3f6;
  } else {
    //
    // The BARs should be of IO type
    //
    if ((PciData.Device.Bar[0] & bit0) == 0 || 
        (PciData.Device.Bar[1] & bit0) == 0) {
      return EFI_UNSUPPORTED;
    }

    IdeRegsBaseAddr[IdePrimary].CommandBlockBaseAddr  =
    (UINT16) (PciData.Device.Bar[0] & 0x0000fff8);
    IdeRegsBaseAddr[IdePrimary].ControlBlockBaseAddr  =
    (UINT16) ((PciData.Device.Bar[1] & 0x0000fffc) + 2);
  }

  if ((PciData.Hdr.ClassCode[0] & IDE_SECONDARY_OPERATING_MODE) == 0) {
    IdeRegsBaseAddr[IdeSecondary].CommandBlockBaseAddr  = 0x170;
    IdeRegsBaseAddr[IdeSecondary].ControlBlockBaseAddr  = 0x376;
  } else {
    //
    // The BARs should be of IO type
    //
    if ((PciData.Device.Bar[2] & bit0) == 0 ||
        (PciData.Device.Bar[3] & bit0) == 0) {
      return EFI_UNSUPPORTED;
    }

    IdeRegsBaseAddr[IdeSecondary].CommandBlockBaseAddr  =
    (UINT16) (PciData.Device.Bar[2] & 0x0000fff8);
    IdeRegsBaseAddr[IdeSecondary].ControlBlockBaseAddr  =
    (UINT16) ((PciData.Device.Bar[3] & 0x0000fffc) + 2);
  }

  return EFI_SUCCESS;
}


STATIC
VOID
InitAtapiIoPortRegisters (
  IN  ATAPI_EXT_SCSI_PASS_THRU_DEV *AtapiScsiPrivate,
  IN  IDE_REGISTERS_BASE_ADDR      *IdeRegsBaseAddr
  )
/*++

Routine Description:

  Initialize each Channel's Base Address of CommandBlock and ControlBlock.

Arguments:
    
  AtapiScsiPrivate            - The pointer of ATAPI_SCSI_PASS_THRU_DEV
  IdeRegsBaseAddr             - The pointer of IDE_REGISTERS_BASE_ADDR
  
Returns:
  
  None

--*/  
{
  
  UINT8               IdeChannel;
  UINT16              CommandBlockBaseAddr;
  UINT16              ControlBlockBaseAddr;
  IDE_BASE_REGISTERS  *RegisterPointer;

  
  for (IdeChannel = 0; IdeChannel < ATAPI_MAX_CHANNEL; IdeChannel++) {

    RegisterPointer =  &AtapiScsiPrivate->AtapiIoPortRegisters[IdeChannel];

    //
    // Initialize IDE IO port addresses, including Command Block registers
    // and Control Block registers
    //
    CommandBlockBaseAddr = IdeRegsBaseAddr[IdeChannel].CommandBlockBaseAddr;
    ControlBlockBaseAddr = IdeRegsBaseAddr[IdeChannel].ControlBlockBaseAddr;
  
    RegisterPointer->Data = CommandBlockBaseAddr;
    (*(UINT16 *) &RegisterPointer->Reg1) = (UINT16) (CommandBlockBaseAddr + 0x01);
    RegisterPointer->SectorCount = (UINT16) (CommandBlockBaseAddr + 0x02);
    RegisterPointer->SectorNumber = (UINT16) (CommandBlockBaseAddr + 0x03);
    RegisterPointer->CylinderLsb = (UINT16) (CommandBlockBaseAddr + 0x04);
    RegisterPointer->CylinderMsb = (UINT16) (CommandBlockBaseAddr + 0x05);
    RegisterPointer->Head = (UINT16) (CommandBlockBaseAddr + 0x06);
    (*(UINT16 *) &RegisterPointer->Reg) = (UINT16) (CommandBlockBaseAddr + 0x07);
  
    (*(UINT16 *) &RegisterPointer->Alt) = ControlBlockBaseAddr;
    RegisterPointer->DriveAddress = (UINT16) (ControlBlockBaseAddr + 0x01);
  }

}

STATIC  
EFI_STATUS
CheckSCSIRequestPacket (
  EFI_EXT_SCSI_PASS_THRU_SCSI_REQUEST_PACKET      *Packet
  )
/*++

Routine Description:

  Checks the parameters in the SCSI Request Packet to make sure
  they are valid for a SCSI Pass Thru request.

Arguments:

  Packet       - The pointer of EFI_EXT_SCSI_PASS_THRU_SCSI_REQUEST_PACKET
  
Returns:
  
  EFI_STATUS

--*/
{
  if (Packet == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  if (!ValidCdbLength (Packet->CdbLength)) {
    return EFI_INVALID_PARAMETER;
  }

  if (Packet->Cdb == NULL) {
    return EFI_INVALID_PARAMETER;
  }
  
  //
  // Checks whether the request command is supported.
  //
  if (!IsCommandValid (Packet)) {
    return EFI_UNSUPPORTED;
  }

  return EFI_SUCCESS;
}


STATIC  
BOOLEAN
IsCommandValid (
  EFI_EXT_SCSI_PASS_THRU_SCSI_REQUEST_PACKET   *Packet
  )
/*++
  
Routine Description:

  Checks the requested SCSI command: 
  Is it supported by this driver?
  Is the Data transfer direction reasonable?

Arguments:

  Packet         -  The pointer of EFI_SCSI_PASS_THRU_SCSI_REQUEST_PACKET   

Returns:

  EFI_STATUS

--*/
{
  UINT8 Index;
  UINT8 *OpCode;

  OpCode = (UINT8 *) (Packet->Cdb);

  for (Index = 0; EfiCompareMem (&SupportedATAPICommands[Index], &EndTable, sizeof (SCSI_COMMAND_SET)); Index++) {

    if (*OpCode == SupportedATAPICommands[Index].OpCode) {
      //
      // Check whether the requested Command is supported by this driver
      //
      if (Packet->DataDirection == DataIn) {
        //
        // Check whether the requested data direction conforms to
        // what it should be.
        //
        if (SupportedATAPICommands[Index].Direction == DataOut) {
          return FALSE;
        }
      }

      if (Packet->DataDirection == DataOut) {
        //
        // Check whether the requested data direction conforms to
        // what it should be.
        //
        if (SupportedATAPICommands[Index].Direction == DataIn) {
          return FALSE;
        }
      }

      return TRUE;
    }
  }

  return FALSE;
}

STATIC  
EFI_STATUS
SubmitBlockingIoCommand (
  ATAPI_EXT_SCSI_PASS_THRU_DEV                  *AtapiScsiPrivate,
  UINT8                                         Target,
  EFI_EXT_SCSI_PASS_THRU_SCSI_REQUEST_PACKET    *Packet
  )
/*++

Routine Description:

  Performs blocking I/O request.
    
Arguments:

  AtapiScsiPrivate:   Private data structure for the specified channel.
  Target:             The Target ID of the ATAPI device to send the SCSI 
                      Request Packet. To ATAPI devices attached on an IDE
                      Channel, Target ID 0 indicates Master device;Target
                      ID 1 indicates Slave device.
  Packet:             The SCSI Request Packet to send to the ATAPI device 
                      specified by Target.
  
  Returns:            EFI_STATUS  
  
--*/
{
  UINT8       PacketCommand[12];
  UINT64      TimeoutInMicroSeconds;
  EFI_STATUS  PacketCommandStatus;
  UINTN       Remainder;

  //
  // Fill ATAPI Command Packet according to CDB
  //
  EfiZeroMem (&PacketCommand, 12);
  EfiCopyMem (&PacketCommand, Packet->Cdb, Packet->CdbLength);

  //
  // Timeout is 100ns unit, convert it to 1000ns (1us) unit.
  //
  TimeoutInMicroSeconds = DivU64x32 (Packet->Timeout, (UINT32) 10, &Remainder);

  //
  // Submit ATAPI Command Packet
  //
  if (Packet->DataDirection == DataIn) {
    PacketCommandStatus = AtapiPacketCommand (
                              AtapiScsiPrivate,
                              Target,
                              PacketCommand,
                              Packet->InDataBuffer,
                              &(Packet->InTransferLength),
                              DataIn,
                              TimeoutInMicroSeconds
                              );
  } else {

  PacketCommandStatus = AtapiPacketCommand (
                          AtapiScsiPrivate,
                          Target,
                          PacketCommand,
                          Packet->OutDataBuffer,
                          &(Packet->OutTransferLength),
                          DataOut,
                          TimeoutInMicroSeconds
                          );
  }
  
  if (!EFI_ERROR (PacketCommandStatus) || (Packet->SenseData == NULL)) {
    Packet->SenseDataLength = 0;
    return PacketCommandStatus;
  }
  
  //
  // Check if SenseData meets the alignment requirement.
  //
  if ((AtapiScsiPrivate->ScsiPassThru.Mode->IoAlign != 0)     \
    && (AtapiScsiPrivate->ScsiPassThru.Mode->IoAlign != 1)) {
    if (((UINTN)Packet->SenseData % AtapiScsiPrivate->ScsiPassThru.Mode->IoAlign) != 0) {
      return EFI_INVALID_PARAMETER;
    }
  }
  
  //
  // Return SenseData if PacketCommandStatus matches
  // the following return codes.
  //
  if ((PacketCommandStatus ==  EFI_BAD_BUFFER_SIZE) ||
      (PacketCommandStatus == EFI_DEVICE_ERROR) ||
      (PacketCommandStatus == EFI_TIMEOUT)) {

    //
    // avoid submit request sense command continuously.
    //
    if (PacketCommand[0] == OP_REQUEST_SENSE) {
      Packet->SenseDataLength = 0;
      return PacketCommandStatus;
    }

    RequestSenseCommand (
      AtapiScsiPrivate,
      Target,
      Packet->Timeout,
      Packet->SenseData,
      &Packet->SenseDataLength
      );
  }

  return PacketCommandStatus;
}

STATIC  
EFI_STATUS
RequestSenseCommand (
  ATAPI_EXT_SCSI_PASS_THRU_DEV    *AtapiScsiPrivate,
  UINT8                           Target,
  UINT64                          Timeout,
  VOID                            *SenseData,
  UINT8                           *SenseDataLength
  )
/*++

Routine Description:

  Sumbit request sense command

Arguments:

  AtapiScsiPrivate  - The pionter of ATAPI_SCSI_PASS_THRU_DEV
  Target            - The target ID
  Timeout           - The time to complete the command
  SenseData         - The buffer to fill in sense data
  SenseDataLength   - The length of buffer

Returns:

  EFI_STATUS

--*/
{
  EFI_EXT_SCSI_PASS_THRU_SCSI_REQUEST_PACKET  Packet;
  UINT8                                       Cdb[12];
  EFI_STATUS                                  Status;

  EfiZeroMem (&Packet, sizeof (EFI_EXT_SCSI_PASS_THRU_SCSI_REQUEST_PACKET));
  EfiZeroMem (Cdb, 12);

  Cdb[0]                = OP_REQUEST_SENSE;
  Cdb[4]                = (UINT8) (*SenseDataLength);

  Packet.Timeout        = Timeout;
  Packet.InDataBuffer     = SenseData;
  Packet.SenseData      = NULL;
  Packet.Cdb            = Cdb;
  Packet.InTransferLength = *SenseDataLength;
  Packet.CdbLength      = 12;
  Packet.DataDirection  = DataIn;

  Status                = SubmitBlockingIoCommand (AtapiScsiPrivate, Target, &Packet);
  *SenseDataLength      = (UINT8) (Packet.InTransferLength);
  return Status;
}

STATIC  
EFI_STATUS
AtapiPacketCommand (
  ATAPI_EXT_SCSI_PASS_THRU_DEV    *AtapiScsiPrivate,
  UINT8                           Target,
  UINT8                           *PacketCommand,
  VOID                            *Buffer,
  UINT32                          *ByteCount,
  DATA_DIRECTION                  Direction,
  UINT64                          TimeoutInMicroSeconds
  )
/*++

Routine Description:
  Submits ATAPI command packet to the specified ATAPI device.
    
Arguments:
  AtapiScsiPrivate:   Private data structure for the specified channel.
  Target:             The Target ID of the ATAPI device to send the SCSI 
                      Request Packet. To ATAPI devices attached on an IDE
                      Channel, Target ID 0 indicates Master device;Target
                      ID 1 indicates Slave device.
  PacketCommand:      Points to the ATAPI command packet.
  Buffer:             Points to the transferred data.
  ByteCount:          When input,indicates the buffer size; when output,
                      indicates the actually transferred data size.
  Direction:          Indicates the data transfer direction. 
  TimeoutInMicroSeconds:
                      The timeout, in micro second units, to use for the 
                      execution of this ATAPI command.
                      A TimeoutInMicroSeconds value of 0 means that 
                      this function will wait indefinitely for the ATAPI 
                      command to execute.
                      If TimeoutInMicroSeconds is greater than zero, then 
                      this function will return EFI_TIMEOUT if the time 
                      required to execute the ATAPI command is greater 
                      than TimeoutInMicroSeconds.
  
Returns:

  EFI_STATUS
  
--*/
{

  UINT16      *CommandIndex;
  UINT8       Count;
  EFI_STATUS  Status;

  //
  // Check if the buffer meets the alignment requirement.
  //
  if ((AtapiScsiPrivate->ScsiPassThru.Mode->IoAlign != 0)     \
    && (AtapiScsiPrivate->ScsiPassThru.Mode->IoAlign != 1)) {
    if (((UINTN)Buffer % AtapiScsiPrivate->ScsiPassThru.Mode->IoAlign) != 0) {
      return EFI_INVALID_PARAMETER;
    }
  }

  //
  // Set all the command parameters by fill related registers.
  // Before write to all the following registers, BSY must be 0.
  //
  Status = StatusWaitForBSYClear (AtapiScsiPrivate, TimeoutInMicroSeconds);
  if (EFI_ERROR (Status)) {
    return EFI_DEVICE_ERROR;
  }


  //
  // Select device via Device/Head Register.
  // "Target = 0" indicates device 0; "Target = 1" indicates device 1
  //
  WritePortB (
    AtapiScsiPrivate->PciIo,
    AtapiScsiPrivate->IoPort->Head,
    (UINT8) ((Target << 4) | DEFAULT_CMD) // DEFAULT_CMD: 0xa0 (1010,0000)
    );

  //
  // Set all the command parameters by fill related registers.
  // Before write to all the following registers, BSY DRQ must be 0.
  //
   Status =  StatusDRQClear(AtapiScsiPrivate,  TimeoutInMicroSeconds);

  if (EFI_ERROR (Status)) {
    if (Status == EFI_ABORTED) {
      Status = EFI_DEVICE_ERROR;
    }
    *ByteCount = 0;
    return Status;
  }

  //
  // No OVL; No DMA (by setting feature register)
  //
  WritePortB (
    AtapiScsiPrivate->PciIo,
    AtapiScsiPrivate->IoPort->Reg1.Feature,
    0x00
    );

  //
  // set the transfersize to MAX_ATAPI_BYTE_COUNT to let the device
  // determine how much data should be transfered.
  //
  WritePortB (
    AtapiScsiPrivate->PciIo,
    AtapiScsiPrivate->IoPort->CylinderLsb,
    (UINT8) (MAX_ATAPI_BYTE_COUNT & 0x00ff)
    );
  WritePortB (
    AtapiScsiPrivate->PciIo,
    AtapiScsiPrivate->IoPort->CylinderMsb,
    (UINT8) (MAX_ATAPI_BYTE_COUNT >> 8)
    );

  //
  //  DEFAULT_CTL:0x0a (0000,1010)
  //  Disable interrupt
  //
  WritePortB (
    AtapiScsiPrivate->PciIo,
    AtapiScsiPrivate->IoPort->Alt.DeviceControl,
    DEFAULT_CTL
    );

  //
  // Send Packet command to inform device
  // that the following data bytes are command packet.
  //
  WritePortB (
    AtapiScsiPrivate->PciIo,
    AtapiScsiPrivate->IoPort->Reg.Command,
    PACKET_CMD
    );

  //
  // Before data transfer, BSY should be 0 and DRQ should be 1.
  // if they are not in specified time frame,
  // retrieve Sense Key from Error Register before return.
  //
  Status = StatusDRQReady (AtapiScsiPrivate, TimeoutInMicroSeconds);
  if (EFI_ERROR (Status)) {
    if (Status == EFI_ABORTED) {
      Status = EFI_DEVICE_ERROR;
    }

    *ByteCount = 0;
    return Status;
  }

  //
  // Send out command packet
  //
  CommandIndex = (UINT16 *) PacketCommand;
  for (Count = 0; Count < 6; Count++, CommandIndex++) {
    WritePortW (AtapiScsiPrivate->PciIo, AtapiScsiPrivate->IoPort->Data, *CommandIndex);
  }

  //
  // call AtapiPassThruPioReadWriteData() function to get
  // requested transfer data form device.
  //
  return AtapiPassThruPioReadWriteData (
          AtapiScsiPrivate,
          Buffer,
          ByteCount,
          Direction,
          TimeoutInMicroSeconds
          );
}

STATIC  
EFI_STATUS
AtapiPassThruPioReadWriteData (
  ATAPI_EXT_SCSI_PASS_THRU_DEV  *AtapiScsiPrivate,
  UINT16                        *Buffer,
  UINT32                        *ByteCount,
  DATA_DIRECTION                Direction,
  UINT64                        TimeoutInMicroSeconds
  )
/*++

Routine Description:

  Performs data transfer between ATAPI device and host after the
  ATAPI command packet is sent.
    
Arguments:

  AtapiScsiPrivate:   Private data structure for the specified channel.    
  Buffer:             Points to the transferred data.
  ByteCount:          When input,indicates the buffer size; when output,
                      indicates the actually transferred data size.
  Direction:          Indicates the data transfer direction. 
  TimeoutInMicroSeconds:
                      The timeout, in micro second units, to use for the 
                      execution of this ATAPI command.
                      A TimeoutInMicroSeconds value of 0 means that 
                      this function will wait indefinitely for the ATAPI 
                      command to execute.
                      If TimeoutInMicroSeconds is greater than zero, then 
                      this function will return EFI_TIMEOUT if the time 
                      required to execute the ATAPI command is greater 
                      than TimeoutInMicroSeconds.
 Returns:

  EFI_STATUS
  
--*/
{
  UINT32      Index;
  UINT32      RequiredWordCount;
  UINT32      ActualWordCount;
  UINT32      WordCount;
  EFI_STATUS  Status;
  UINT16      *ptrBuffer;

  Status = EFI_SUCCESS;

  //
  // Non Data transfer request is also supported.
  //
  if (*ByteCount == 0 || Buffer == NULL) {
    *ByteCount = 0;
    if (EFI_ERROR (StatusWaitForBSYClear (AtapiScsiPrivate, TimeoutInMicroSeconds))) {
      return EFI_DEVICE_ERROR;
    }
  }

  ptrBuffer  = Buffer;
  RequiredWordCount = *ByteCount / 2;

  //
  // ActuralWordCount means the word count of data really transfered.
  //
  ActualWordCount = 0;

  while (ActualWordCount < RequiredWordCount) {
    //
    // before each data transfer stream, the host should poll DRQ bit ready,
    // which indicates device's ready for data transfer .
    //
    Status = StatusDRQReady (AtapiScsiPrivate, TimeoutInMicroSeconds);
    if (EFI_ERROR (Status)) {
      *ByteCount = ActualWordCount * 2;

      AtapiPassThruCheckErrorStatus (AtapiScsiPrivate);

      if (ActualWordCount == 0) {
        return EFI_DEVICE_ERROR;
      }
      //
      // ActualWordCount > 0
      //
      if (ActualWordCount < RequiredWordCount) {
        return EFI_BAD_BUFFER_SIZE;
      }
    }
    //
    // get current data transfer size from Cylinder Registers.
    //
    WordCount =
      (
        (ReadPortB (AtapiScsiPrivate->PciIo, AtapiScsiPrivate->IoPort->CylinderMsb) << 8) |
        ReadPortB (AtapiScsiPrivate->PciIo, AtapiScsiPrivate->IoPort->CylinderLsb)
      ) & 0xffff;
    WordCount /= 2;

    //
    // perform a series data In/Out.
    //
    for (Index = 0; (Index < WordCount) && (ActualWordCount < RequiredWordCount); Index++, ActualWordCount++) {

      if (Direction == DataIn) {

        *ptrBuffer = ReadPortW (AtapiScsiPrivate->PciIo, AtapiScsiPrivate->IoPort->Data);
      } else {

        WritePortW (AtapiScsiPrivate->PciIo, AtapiScsiPrivate->IoPort->Data, *ptrBuffer);
      }

      ptrBuffer++;

    }
  }
  //
  // After data transfer is completed, normally, DRQ bit should clear.
  //
  StatusDRQClear (AtapiScsiPrivate, TimeoutInMicroSeconds);

  //
  // read status register to check whether error happens.
  //
  Status      = AtapiPassThruCheckErrorStatus (AtapiScsiPrivate);

  *ByteCount  = ActualWordCount * 2;

  return Status;
}

STATIC
UINT8
ReadPortB (
  IN  EFI_PCI_IO_PROTOCOL   *PciIo,
  IN  UINT16                Port
  )
/*++

Routine Description:

  Read one byte from a specified I/O port.

Arguments:

  PciIo      - The pointer of EFI_PCI_IO_PROTOCOL
  Port       - IO port
  
Returns:


--*/
{
  UINT8 Data;

  Data = 0;
  PciIo->Io.Read (
              PciIo,
              EfiPciIoWidthUint8,
              EFI_PCI_IO_PASS_THROUGH_BAR,
              (UINT64) Port,
              1,
              &Data
              );
  return Data;
}

STATIC
UINT16
ReadPortW (
  IN  EFI_PCI_IO_PROTOCOL   *PciIo,
  IN  UINT16                Port
  )
/*++

Routine Description:

  Read one word from a specified I/O port.

Arguments:

  PciIo      - The pointer of EFI_PCI_IO_PROTOCOL
  Port       - IO port
  
Returns:

--*/
{
  UINT16  Data;

  Data = 0;
  PciIo->Io.Read (
              PciIo,
              EfiPciIoWidthUint16,
              EFI_PCI_IO_PASS_THROUGH_BAR,
              (UINT64) Port,
              1,
              &Data
              );
  return Data;
}

STATIC
VOID
WritePortB (
  IN  EFI_PCI_IO_PROTOCOL   *PciIo,
  IN  UINT16                Port,
  IN  UINT8                 Data
  )
/*++

Routine Description:

  Write one byte to a specified I/O port.

Arguments:

  PciIo      - The pointer of EFI_PCI_IO_PROTOCOL
  Port       - IO port
  Data       - The data to write
  
Returns:

--*/
{
  PciIo->Io.Write (
              PciIo,
              EfiPciIoWidthUint8,
              EFI_PCI_IO_PASS_THROUGH_BAR,
              (UINT64) Port,
              1,
              &Data
              );
}

STATIC
VOID
WritePortW (
  IN  EFI_PCI_IO_PROTOCOL   *PciIo,
  IN  UINT16                Port,
  IN  UINT16                Data
  )
/*++

Routine Description:

  Write one word to a specified I/O port.

Arguments:

  PciIo      - The pointer of EFI_PCI_IO_PROTOCOL
  Port       - IO port
  Data       - The data to write
  
Returns:

--*/
{
  PciIo->Io.Write (
              PciIo,
              EfiPciIoWidthUint16,
              EFI_PCI_IO_PASS_THROUGH_BAR,
              (UINT64) Port,
              1,
              &Data
              );
}

STATIC
EFI_STATUS
StatusDRQClear (
  ATAPI_EXT_SCSI_PASS_THRU_DEV        *AtapiScsiPrivate,
  UINT64                              TimeoutInMicroSeconds
  )
/*++

Routine Description:

  Check whether DRQ is clear in the Status Register. (BSY must also be cleared)
  If TimeoutInMicroSeconds is zero, this routine should wait infinitely for
  DRQ clear. Otherwise, it will return EFI_TIMEOUT when specified time is 
  elapsed.

Arguments:

  AtapiScsiPrivate            - The pointer of ATAPI_SCSI_PASS_THRU_DEV
  TimeoutInMicroSeconds       - The time to wait for
   
Returns:

  EFI_STATUS
  
--*/
{
  UINT64  Delay;
  UINT8   StatusRegister;
  UINT8   ErrRegister;
  UINTN   Remainder;

  if (TimeoutInMicroSeconds == 0) {
    Delay = 2;
  } else {
    Delay = DivU64x32 (TimeoutInMicroSeconds, (UINT32) 30, &Remainder) + 1;
  }

  do {

    StatusRegister = ReadPortB (
                      AtapiScsiPrivate->PciIo,
                      AtapiScsiPrivate->IoPort->Reg.Status
                      );

    //
    // wait for BSY == 0 and DRQ == 0
    //
    if ((StatusRegister & (DRQ | BSY)) == 0) {
      break;
    }
    //
    // check whether the command is aborted by the device
    //
    if ((StatusRegister & (BSY | ERR)) == ERR) {

      ErrRegister = ReadPortB (
                      AtapiScsiPrivate->PciIo,
                      AtapiScsiPrivate->IoPort->Reg1.Error
                      );
      if ((ErrRegister & ABRT_ERR) == ABRT_ERR) {

        return EFI_ABORTED;
      }
    }
    //
    //  Stall for 30 us
    //
    gBS->Stall (30);

    //
    // Loop infinitely if not meeting expected condition
    //
    if (TimeoutInMicroSeconds == 0) {
      Delay = 2;
    }

    Delay--;
  } while (Delay);

  if (Delay == 0) {
    return EFI_TIMEOUT;
  }

  return EFI_SUCCESS;
}

STATIC  
EFI_STATUS
AltStatusDRQClear (
  ATAPI_EXT_SCSI_PASS_THRU_DEV        *AtapiScsiPrivate,
  UINT64                              TimeoutInMicroSeconds
  )
/*++

Routine Description:

  Check whether DRQ is clear in the Alternate Status Register. 
  (BSY must also be cleared).If TimeoutInMicroSeconds is zero, this routine should 
  wait infinitely for DRQ clear. Otherwise, it will return EFI_TIMEOUT when specified time is 
  elapsed.

Arguments:

  AtapiScsiPrivate            - The pointer of ATAPI_SCSI_PASS_THRU_DEV
  TimeoutInMicroSeconds       - The time to wait for
   
Returns:

  EFI_STATUS

--*/
{
  UINT64  Delay;
  UINT8   AltStatusRegister;
  UINT8   ErrRegister;
  UINTN   Remainder;

  if (TimeoutInMicroSeconds == 0) {
    Delay = 2;
  } else {
    Delay = DivU64x32 (TimeoutInMicroSeconds, (UINT32) 30, &Remainder) + 1;
  }

  do {

    AltStatusRegister = ReadPortB (
                          AtapiScsiPrivate->PciIo,
                          AtapiScsiPrivate->IoPort->Alt.AltStatus
                          );

    //
    // wait for BSY == 0 and DRQ == 0
    //
    if ((AltStatusRegister & (DRQ | BSY)) == 0) {
      break;
    }

    if ((AltStatusRegister & (BSY | ERR)) == ERR) {

      ErrRegister = ReadPortB (
                      AtapiScsiPrivate->PciIo,
                      AtapiScsiPrivate->IoPort->Reg1.Error
                      );
      if ((ErrRegister & ABRT_ERR) == ABRT_ERR) {

        return EFI_ABORTED;
      }
    }
    //
    //  Stall for 30 us
    //
    gBS->Stall (30);

    //
    // Loop infinitely if not meeting expected condition
    //
    if (TimeoutInMicroSeconds == 0) {
      Delay = 2;
    }

    Delay--;
  } while (Delay);

  if (Delay == 0) {
    return EFI_TIMEOUT;
  }

  return EFI_SUCCESS;
}

STATIC  
EFI_STATUS
StatusDRQReady (
  ATAPI_EXT_SCSI_PASS_THRU_DEV        *AtapiScsiPrivate,
  UINT64                              TimeoutInMicroSeconds
  )
/*++

Routine Description:

  Check whether DRQ is ready in the Status Register. (BSY must also be cleared)
  If TimeoutInMicroSeconds is zero, this routine should wait infinitely for
  DRQ ready. Otherwise, it will return EFI_TIMEOUT when specified time is 
  elapsed.

Arguments:

  AtapiScsiPrivate            - The pointer of ATAPI_SCSI_PASS_THRU_DEV
  TimeoutInMicroSeconds       - The time to wait for
   
Returns:

  EFI_STATUS

--*/
{
  UINT64  Delay;
  UINT8   StatusRegister;
  UINT8   ErrRegister;
  UINTN   Remainder;

  if (TimeoutInMicroSeconds == 0) {
    Delay = 2;
  } else {
    Delay = DivU64x32 (TimeoutInMicroSeconds, (UINT32) 30, &Remainder) + 1;
  }

  do {
    //
    //  read Status Register will clear interrupt
    //
    StatusRegister = ReadPortB (
                      AtapiScsiPrivate->PciIo,
                      AtapiScsiPrivate->IoPort->Reg.Status
                      );

    //
    //  BSY==0,DRQ==1
    //
    if ((StatusRegister & (BSY | DRQ)) == DRQ) {
      break;
    }

    if ((StatusRegister & (BSY | ERR)) == ERR) {

      ErrRegister = ReadPortB (
                      AtapiScsiPrivate->PciIo,
                      AtapiScsiPrivate->IoPort->Reg1.Error
                      );
      if ((ErrRegister & ABRT_ERR) == ABRT_ERR) {
        return EFI_ABORTED;
      }
    }

    //
    // Stall for 30 us
    //
    gBS->Stall (30);

    //
    // Loop infinitely if not meeting expected condition
    //
    if (TimeoutInMicroSeconds == 0) {
      Delay = 2;
    }

    Delay--;
  } while (Delay);

  if (Delay == 0) {
    return EFI_TIMEOUT;
  }

  return EFI_SUCCESS;
}

STATIC  
EFI_STATUS
AltStatusDRQReady (
  ATAPI_EXT_SCSI_PASS_THRU_DEV        *AtapiScsiPrivate,
  UINT64                              TimeoutInMicroSeconds
  )
/*++

Routine Description:

  Check whether DRQ is ready in the Alternate Status Register. 
  (BSY must also be cleared)
  If TimeoutInMicroSeconds is zero, this routine should wait infinitely for
  DRQ ready. Otherwise, it will return EFI_TIMEOUT when specified time is 
  elapsed.

Arguments:

  AtapiScsiPrivate            - The pointer of ATAPI_SCSI_PASS_THRU_DEV
  TimeoutInMicroSeconds       - The time to wait for
   
Returns:

  EFI_STATUS

--*/
{
  UINT64  Delay;
  UINT8   AltStatusRegister;
  UINT8   ErrRegister;
  UINTN   Remainder;

  if (TimeoutInMicroSeconds == 0) {
    Delay = 2;
  } else {
    Delay = DivU64x32 (TimeoutInMicroSeconds, (UINT32) 30, &Remainder) + 1;
  }

  do {
    //
    //  read Status Register will clear interrupt
    //
    AltStatusRegister = ReadPortB (
                          AtapiScsiPrivate->PciIo,
                          AtapiScsiPrivate->IoPort->Alt.AltStatus
                          );
    //
    //  BSY==0,DRQ==1
    //
    if ((AltStatusRegister & (BSY | DRQ)) == DRQ) {
      break;
    }

    if ((AltStatusRegister & (BSY | ERR)) == ERR) {

      ErrRegister = ReadPortB (
                      AtapiScsiPrivate->PciIo,
                      AtapiScsiPrivate->IoPort->Reg1.Error
                      );
      if ((ErrRegister & ABRT_ERR) == ABRT_ERR) {
        return EFI_ABORTED;
      }
    }

    //
    // Stall for 30 us
    //
    gBS->Stall (30);

    //
    // Loop infinitely if not meeting expected condition
    //
    if (TimeoutInMicroSeconds == 0) {
      Delay = 2;
    }

    Delay--;
  } while (Delay);

  if (Delay == 0) {
    return EFI_TIMEOUT;
  }

  return EFI_SUCCESS;
}

STATIC  
EFI_STATUS
StatusWaitForBSYClear (
  ATAPI_EXT_SCSI_PASS_THRU_DEV    *AtapiScsiPrivate,
  UINT64                          TimeoutInMicroSeconds
  )
/*++

Routine Description:

  Check whether BSY is clear in the Status Register.
  If TimeoutInMicroSeconds is zero, this routine should wait infinitely for
  BSY clear. Otherwise, it will return EFI_TIMEOUT when specified time is 
  elapsed.

Arguments:

  AtapiScsiPrivate            - The pointer of ATAPI_SCSI_PASS_THRU_DEV
  TimeoutInMicroSeconds       - The time to wait for
   
Returns:

  EFI_STATUS

--*/
{
  UINT64  Delay;
  UINT8   StatusRegister;
  UINTN   Remainder;

  if (TimeoutInMicroSeconds == 0) {
    Delay = 2;
  } else {
    Delay = DivU64x32 (TimeoutInMicroSeconds, (UINT32) 30, &Remainder) + 1;
  }

  do {

    StatusRegister = ReadPortB (
                      AtapiScsiPrivate->PciIo,
                      AtapiScsiPrivate->IoPort->Reg.Status
                      );
    if ((StatusRegister & BSY) == 0x00) {
      break;
    }

    //
    // Stall for 30 us
    //
    gBS->Stall (30);

    //
    // Loop infinitely if not meeting expected condition
    //
    if (TimeoutInMicroSeconds == 0) {
      Delay = 2;
    }

    Delay--;
  } while (Delay);

  if (Delay == 0) {
    return EFI_TIMEOUT;
  }

  return EFI_SUCCESS;
}

STATIC  
EFI_STATUS
AltStatusWaitForBSYClear (
  ATAPI_EXT_SCSI_PASS_THRU_DEV    *AtapiScsiPrivate,
  UINT64                          TimeoutInMicroSeconds
  )
/*++

Routine Description:

  Check whether BSY is clear in the Alternate Status Register.
  If TimeoutInMicroSeconds is zero, this routine should wait infinitely for
  BSY clear. Otherwise, it will return EFI_TIMEOUT when specified time is 
  elapsed.

Arguments:

  AtapiScsiPrivate            - The pointer of ATAPI_SCSI_PASS_THRU_DEV
  TimeoutInMicroSeconds       - The time to wait for
   
Returns:

  EFI_STATUS

--*/
{
  UINT64  Delay;
  UINT8   AltStatusRegister;
  UINTN   Remainder;

  if (TimeoutInMicroSeconds == 0) {
    Delay = 2;
  } else {
    Delay = DivU64x32 (TimeoutInMicroSeconds, (UINT32) 30, &Remainder) + 1;
  }

  do {

    AltStatusRegister = ReadPortB (
                          AtapiScsiPrivate->PciIo,
                          AtapiScsiPrivate->IoPort->Alt.AltStatus
                          );
    if ((AltStatusRegister & BSY) == 0x00) {
      break;
    }

    //
    // Stall for 30 us
    //
    gBS->Stall (30);
    //
    // Loop infinitely if not meeting expected condition
    //
    if (TimeoutInMicroSeconds == 0) {
      Delay = 2;
    }

    Delay--;
  } while (Delay);

  if (Delay == 0) {
    return EFI_TIMEOUT;
  }

  return EFI_SUCCESS;
}

STATIC  
EFI_STATUS
StatusDRDYReady (
  ATAPI_EXT_SCSI_PASS_THRU_DEV     *AtapiScsiPrivate,
  UINT64                           TimeoutInMicroSeconds
  )
/*++

Routine Description:

  Check whether DRDY is ready in the Status Register. 
  (BSY must also be cleared)
  If TimeoutInMicroSeconds is zero, this routine should wait infinitely for
  DRDY ready. Otherwise, it will return EFI_TIMEOUT when specified time is 
  elapsed.

Arguments:

  AtapiScsiPrivate            - The pointer of ATAPI_SCSI_PASS_THRU_DEV
  TimeoutInMicroSeconds       - The time to wait for
   
Returns:

  EFI_STATUS

--*/
{
  UINT64  Delay;
  UINT8   StatusRegister;
  UINT8   ErrRegister;
  UINTN   Remainder;

  if (TimeoutInMicroSeconds == 0) {
    Delay = 2;
  } else {
    Delay = DivU64x32 (TimeoutInMicroSeconds, (UINT32) 30, &Remainder) + 1;
  }

  do {
    StatusRegister = ReadPortB (
                      AtapiScsiPrivate->PciIo,
                      AtapiScsiPrivate->IoPort->Reg.Status
                      );
    //
    //  BSY == 0 , DRDY == 1
    //
    if ((StatusRegister & (DRDY | BSY)) == DRDY) {
      break;
    }

    if ((StatusRegister & (BSY | ERR)) == ERR) {

      ErrRegister = ReadPortB (
                      AtapiScsiPrivate->PciIo,
                      AtapiScsiPrivate->IoPort->Reg1.Error
                      );
      if ((ErrRegister & ABRT_ERR) == ABRT_ERR) {
        return EFI_ABORTED;
      }
    }
    
    //
    // Stall for 30 us
    //
    gBS->Stall (30);
    //
    // Loop infinitely if not meeting expected condition
    //
    if (TimeoutInMicroSeconds == 0) {
      Delay = 2;
    }

    Delay--;
  } while (Delay);

  if (Delay == 0) {
    return EFI_TIMEOUT;
  }

  return EFI_SUCCESS;
}


STATIC  
EFI_STATUS
AltStatusDRDYReady (
  ATAPI_EXT_SCSI_PASS_THRU_DEV     *AtapiScsiPrivate,
  UINT64                           TimeoutInMicroSeconds
  )
/*++

Routine Description:

  Check whether DRDY is ready in the Alternate Status Register. 
  (BSY must also be cleared)
  If TimeoutInMicroSeconds is zero, this routine should wait infinitely for
  DRDY ready. Otherwise, it will return EFI_TIMEOUT when specified time is 
  elapsed.

Arguments:

  AtapiScsiPrivate            - The pointer of ATAPI_SCSI_PASS_THRU_DEV
  TimeoutInMicroSeconds       - The time to wait for
   
Returns:

  EFI_STATUS

--*/
{
  UINT64  Delay;
  UINT8   AltStatusRegister;
  UINT8   ErrRegister;
  UINTN   Remainder;

  if (TimeoutInMicroSeconds == 0) {
    Delay = 2;
  } else {
    Delay = DivU64x32 (TimeoutInMicroSeconds, (UINT32) 30, &Remainder) + 1;
  }

  do {
    AltStatusRegister = ReadPortB (
                          AtapiScsiPrivate->PciIo,
                          AtapiScsiPrivate->IoPort->Alt.AltStatus
                          );
    //
    //  BSY == 0 , DRDY == 1
    //
    if ((AltStatusRegister & (DRDY | BSY)) == DRDY) {
      break;
    }

    if ((AltStatusRegister & (BSY | ERR)) == ERR) {

      ErrRegister = ReadPortB (
                      AtapiScsiPrivate->PciIo,
                      AtapiScsiPrivate->IoPort->Reg1.Error
                      );
      if ((ErrRegister & ABRT_ERR) == ABRT_ERR) {
        return EFI_ABORTED;
      }
    }

    //
    // Stall for 30 us
    //
    gBS->Stall (30);
    //
    // Loop infinitely if not meeting expected condition
    //
    if (TimeoutInMicroSeconds == 0) {
      Delay = 2;
    }

    Delay--;
  } while (Delay);

  if (Delay == 0) {
    return EFI_TIMEOUT;
  }

  return EFI_SUCCESS;
}

STATIC  
EFI_STATUS
AtapiPassThruCheckErrorStatus (
  ATAPI_EXT_SCSI_PASS_THRU_DEV        *AtapiScsiPrivate
  )
/*++  

Routine Description:

  Check Error Register for Error Information. 
  
Arguments:

  AtapiScsiPrivate            - The pointer of ATAPI_SCSI_PASS_THRU_DEV
   
Returns:

  EFI_STATUS

--*/
{
  UINT8 StatusRegister;

#ifdef EFI_DEBUG

  UINT8 ErrorRegister;

#endif

  StatusRegister = ReadPortB (
                    AtapiScsiPrivate->PciIo,
                    AtapiScsiPrivate->IoPort->Reg.Status
                    );
  DEBUG_CODE (

    if (StatusRegister & DWF) {
    DEBUG (
      (EFI_D_BLKIO,
      "AtapiPassThruCheckErrorStatus()-- %02x : Error : Write Fault\n",
      StatusRegister)
      );
  }

  if (StatusRegister & CORR) {
    DEBUG (
      (EFI_D_BLKIO,
      "AtapiPassThruCheckErrorStatus()-- %02x : Error : Corrected Data\n",
      StatusRegister)
      );
  }

  if (StatusRegister & ERR) {
    ErrorRegister = ReadPortB (AtapiScsiPrivate->PciIo, AtapiScsiPrivate->IoPort->Reg1.Error);

    if (ErrorRegister & BBK_ERR) {
    DEBUG (
      (EFI_D_BLKIO,
      "AtapiPassThruCheckErrorStatus()-- %02x : Error : Bad Block Detected\n",
      ErrorRegister)
      );
  }

  if (ErrorRegister & UNC_ERR) {
    DEBUG (
      (EFI_D_BLKIO,
      "AtapiPassThruCheckErrorStatus()-- %02x : Error : Uncorrectable Data\n",
      ErrorRegister)
      );
  }

  if (ErrorRegister & MC_ERR) {
    DEBUG (
      (EFI_D_BLKIO,
      "AtapiPassThruCheckErrorStatus()-- %02x : Error : Media Change\n",
      ErrorRegister)
      );
  }

  if (ErrorRegister & ABRT_ERR) {
    DEBUG (
      (EFI_D_BLKIO,
      "AtapiPassThruCheckErrorStatus()-- %02x : Error : Abort\n",
      ErrorRegister)
      );
  }

  if (ErrorRegister & TK0NF_ERR) {
    DEBUG (
      (EFI_D_BLKIO,
      "AtapiPassThruCheckErrorStatus()-- %02x : Error : Track 0 Not Found\n",
      ErrorRegister)
      );
  }

  if (ErrorRegister & AMNF_ERR) {
    DEBUG (
      (EFI_D_BLKIO,
      "AtapiPassThruCheckErrorStatus()-- %02x : Error : Address Mark Not Found\n",
      ErrorRegister)
      );
  }

  }
  )

  if ((StatusRegister & (ERR | DWF | CORR)) == 0) {

    return EFI_SUCCESS;
  }
  
  return EFI_DEVICE_ERROR;
}

