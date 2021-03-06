/*++

Copyright (c) 2007 - 2009, Intel Corporation                                                         
All rights reserved. This program and the accompanying materials                          
are licensed and made available under the terms and conditions of the BSD License         
which accompanies this distribution.  The full text of the license may be found at        
http://opensource.org/licenses/bsd-license.php                                            
                                                                                          
THE PROGRAM IS DISTRIBUTED UNDER THE BSD LICENSE ON AN "AS IS" BASIS,                     
WITHOUT WARRANTIES OR REPRESENTATIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED. 

Module Name:

  IScsiProto.c

Abstract:

--*/

#include "IScsiImpl.h"

static UINT32 mDataSegPad = 0;

VOID
IScsiAttatchConnection (
  IN ISCSI_SESSION     *Session,
  IN ISCSI_CONNECTION  *Conn
  )
/*++

Routine Description:

  Attach the iSCSI connection to the iSCSI session. 

Arguments:

  Session - The iSCSI session.
  Conn    - The iSCSI connection.

Returns:

  None.

--*/
{
  NetListInsertTail (&Session->Conns, &Conn->Link);
  Conn->Session = Session;
  Session->NumConns++;
}

VOID
IScsiDetatchConnection (
  IN ISCSI_CONNECTION  *Conn
  )
/*++

Routine Description:

  Detach the iSCSI connection from the session it belongs to. 

Arguments:

  Conn - The iSCSI connection.

Returns:

  None.

--*/
{
  NetListRemoveEntry (&Conn->Link);
  Conn->Session->NumConns--;
  Conn->Session = NULL;
}

EFI_STATUS
IScsiCheckSN (
  IN UINT32  *ExpSN,
  IN UINT32  NewSN
  )
/*++

Routine Description:

  Check the sequence number according to RFC3720. 

Arguments:

  ExpSN - The currently expected sequence number.
  NewSN - The sequence number to check.

Returns:

  EFI_SUCCESS - The check passed and the ExpSN is increased.

--*/
{
  if (!ISCSI_SEQ_EQ (NewSN, *ExpSN)) {
    if (ISCSI_SEQ_LT (NewSN, *ExpSN)) {
      //
      // Duplicate
      //
      return EFI_NOT_READY;
    } else {
      return EFI_PROTOCOL_ERROR;
    }
  } else {
    //
    // Advance the ExpSN
    //
    (*ExpSN)++;
    return EFI_SUCCESS;
  }
}

VOID
IScsiUpdateCmdSN (
  IN ISCSI_SESSION  *Session,
  IN UINT32         MaxCmdSN,
  IN UINT32         ExpCmdSN
  )
/*++

Routine Description:

  Update the sequence numbers for the iSCSI command.

Arguments:

  Session  - The iSCSI session.
  MaxCmdSN - Maximum CmdSN from the target.
  ExpCmdSN - Next expected CmdSN from the target.

Returns:

  None.

--*/
{
  if (ISCSI_SEQ_LT (MaxCmdSN, ExpCmdSN - 1)) {
    return ;
  }

  if (ISCSI_SEQ_GT (MaxCmdSN, Session->MaxCmdSN)) {
    Session->MaxCmdSN = MaxCmdSN;
  }

  if (ISCSI_SEQ_GT (ExpCmdSN, Session->ExpCmdSN)) {
    Session->ExpCmdSN = ExpCmdSN;
  }
}

EFI_STATUS
IScsiConnLogin (
  IN ISCSI_CONNECTION  *Conn
  )
/*++

Routine Description:

  This function does the iSCSI connection login.

Arguments:

  Conn - The iSCSI connection to login.

Returns:

  EFI_SUCCESS        - The iSCSI connection is logged into the iSCSI target.
  EFI_TIMEOUT        - Timeout happened during the login procedure.
  EFI_PROTOCOL_ERROR - Some kind of iSCSI protocol error happened.

--*/
{
  EFI_STATUS  Status;

  //
  // Start the timer, wait 16 seconds to establish the TCP connection.
  //
  Status = gBS->SetTimer (Conn->TimeoutEvent, TimerRelative, 16 * TICKS_PER_SECOND);
  if (EFI_ERROR (Status)) {
    return Status;
  }
  //
  // try to establish the tcp connection
  //
  Status = Tcp4IoConnect (&Conn->Tcp4Io, Conn->TimeoutEvent);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  gBS->SetTimer (Conn->TimeoutEvent, TimerCancel, 0);
  Conn->State = CONN_STATE_IN_LOGIN;

  //
  // connection is established, start the iSCSI Login
  //
  do {
    Status = IScsiSendLoginReq (Conn);
    if (EFI_ERROR (Status)) {
      break;
    }

    Status = IScsiReceiveLoginRsp (Conn);
    if (EFI_ERROR (Status)) {
      break;
    }
  } while (Conn->CurrentStage != ISCSI_FULL_FEATURE_PHASE);

  return Status;
}

VOID
IScsiConnReset (
  IN ISCSI_CONNECTION  *Conn
  )
/*++

Routine Description:

  Reset the iSCSI connection.

Arguments:

  Conn - The iSCSI connection to reset.

Returns:

  None.

--*/
{
  Tcp4IoReset (&Conn->Tcp4Io);
}

ISCSI_CONNECTION *
IScsiCreateConnection (
  IN ISCSI_DRIVER_DATA  *Private,
  IN ISCSI_SESSION      *Session
  )
/*++

Routine Description:

  Create a TCP connection for the iSCSI session.

Arguments:

  Private - The iSCSI driver data.
  Session - Maximum CmdSN from the target.

Returns:

  The newly created iSCSI connection.

--*/
{
  ISCSI_CONNECTION    *Conn;
  TCP4_IO_CONFIG_DATA Tcp4IoConfig;
  EFI_STATUS          Status;

  Conn = NetAllocatePool (sizeof (ISCSI_CONNECTION));
  if (Conn == NULL) {
    return NULL;
  }

  Conn->Signature       = ISCSI_CONNECTION_SIGNATURE;
  Conn->State           = CONN_STATE_FREE;
  Conn->CurrentStage    = ISCSI_SECURITY_NEGOTIATION;
  Conn->NextStage       = ISCSI_LOGIN_OPERATIONAL_NEGOTIATION;
  Conn->CHAPStep        = ISCSI_CHAP_INITIAL;
  Conn->ExpStatSN       = 0;
  Conn->PartialReqSent  = FALSE;
  Conn->PartialRspRcvd  = FALSE;
  Conn->CID             = Session->NextCID++;

  Status = gBS->CreateEvent (
                  EFI_EVENT_TIMER,
                  NET_TPL_TIMER,
                  NULL,
                  NULL,
                  &Conn->TimeoutEvent
                  );
  if (EFI_ERROR (Status)) {
    NetFreePool (Conn);
    return NULL;
  }

  NetbufQueInit (&Conn->RspQue);

  //
  // set the default connection-only parameters
  //
  Conn->MaxRecvDataSegmentLength  = DEFAULT_MAX_RECV_DATA_SEG_LEN;
  Conn->HeaderDigest              = ISCSI_DIGEST_NONE;
  Conn->DataDigest                = ISCSI_DIGEST_NONE;

  NetCopyMem (&Tcp4IoConfig.LocalIp, &Session->ConfigData.NvData.LocalIp, sizeof (EFI_IPv4_ADDRESS));
  NetCopyMem (&Tcp4IoConfig.SubnetMask, &Session->ConfigData.NvData.SubnetMask, sizeof (EFI_IPv4_ADDRESS));
  NetCopyMem (&Tcp4IoConfig.Gateway, &Session->ConfigData.NvData.Gateway, sizeof (EFI_IPv4_ADDRESS));
  NetCopyMem (&Tcp4IoConfig.RemoteIp, &Session->ConfigData.NvData.TargetIp, sizeof (EFI_IPv4_ADDRESS));

  Tcp4IoConfig.RemotePort = Session->ConfigData.NvData.TargetPort;

  //
  // Create the tcp4 IO for this connection
  //
  Status = Tcp4IoCreateSocket (
            Private->Image,
            Private->Controller,
            &Tcp4IoConfig,
            &Conn->Tcp4Io
            );
  if (EFI_ERROR (Status)) {
    gBS->CloseEvent (Conn->TimeoutEvent);
    NetFreePool (Conn);
    Conn = NULL;
  }

  return Conn;
}

VOID
IScsiDestroyConnection (
  IN ISCSI_CONNECTION  *Conn
  )
/*++

Routine Description:

  Destroy an iSCSI connection.

Arguments:

  Conn - The connection to destroy.

Returns:

  None.

--*/
{
  Tcp4IoDestroySocket (&Conn->Tcp4Io);
  NetbufQueFlush (&Conn->RspQue);
  gBS->CloseEvent (Conn->TimeoutEvent);
  NetFreePool (Conn);
}

EFI_STATUS
IScsiSessionLogin (
  IN ISCSI_DRIVER_DATA  *Private
  )
/*++

Routine Description:

  Login the iSCSI session.

Arguments:

  Private - The iSCSI driver data.

Returns:

  EFI_SUCCESS          - The iSCSI session login procedure finished.
  EFI_OUT_OF_RESOURCES - Failed to allocate memory.
  EFI_PROTOCOL_ERROR   - Some kind of iSCSI protocol error happened.

--*/
{
  EFI_STATUS        Status;
  ISCSI_SESSION     *Session;
  ISCSI_CONNECTION  *Conn;
  EFI_TCP4_PROTOCOL *Tcp4;

  Session = &Private->Session;

  //
  // Create a connection for the session.
  //
  Conn = IScsiCreateConnection (Private, Session);
  if (Conn == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  IScsiAttatchConnection (Session, Conn);

  //
  // Login througth the newly created connection.
  //
  Status = IScsiConnLogin (Conn);
  if (EFI_ERROR (Status)) {
    IScsiConnReset (Conn);
    IScsiDetatchConnection (Conn);
    IScsiDestroyConnection (Conn);
  } else {
    Session->State = SESSION_STATE_LOGGED_IN;

    gBS->OpenProtocol (
          Conn->Tcp4Io.Handle,
          &gEfiTcp4ProtocolGuid,
          &Tcp4,
          Private->Image,
          Private->ExtScsiPassThruHandle,
          EFI_OPEN_PROTOCOL_BY_CHILD_CONTROLLER
          );
  }

  return Status;
}

EFI_STATUS
IScsiSendLoginReq (
  IN ISCSI_CONNECTION  *Conn
  )
/*++

Routine Description:

  Build and send the iSCSI login request to the iSCSI target according to
  the current login stage.

Arguments:

  Conn - The connection in the iSCSI login phase.

Returns:

  EFI_SUCCESS          - The iSCSI login request PDU is built and sent on this
                         connection.
  EFI_OUT_OF_RESOURCES - Failed to allocate memory.
  EFI_PROTOCOL_ERROR   - Some kind of iSCSI protocol error happened.

--*/
{
  NET_BUF     *Pdu;
  EFI_STATUS  Status;

  //
  // build the Login Request PDU
  //
  Pdu = IScsiPrepareLoginReq (Conn);
  if (Pdu == NULL) {
    return EFI_DEVICE_ERROR;
  }
  //
  // Send it to the iSCSI target.
  //
  Status = Tcp4IoTransmit (&Conn->Tcp4Io, Pdu);

  NetbufFree (Pdu);

  return Status;
}

EFI_STATUS
IScsiReceiveLoginRsp (
  IN ISCSI_CONNECTION  *Conn
  )
/*++

Routine Description:

  Receive and process the iSCSI login response.

Arguments:

  Conn - The connection in the iSCSI login phase.

Returns:

  EFI_SUCCESS          - The iSCSI login response PDU is received and processed.
  EFI_OUT_OF_RESOURCES - Failed to allocate memory.
  EFI_PROTOCOL_ERROR   - Some kind of iSCSI protocol error happened.

--*/
{
  EFI_STATUS  Status;
  NET_BUF     *Pdu;

  //
  // Receive the iSCSI login response.
  //
  Status = IScsiReceivePdu (Conn, &Pdu, NULL, FALSE, FALSE, NULL);
  if (EFI_ERROR (Status)) {
    return Status;
  }
  //
  // A Login Response is received, process it.
  //
  Status = IScsiProcessLoginRsp (Conn, Pdu);

  NetbufFree (Pdu);

  return Status;
}

EFI_STATUS
IScsiAddKeyValuePair (
  IN NET_BUF          *Pdu,
  IN CHAR8            *Key,
  IN CHAR8            *Value
  )
/*++

Routine Description:

  Add an iSCSI key-value pair as a string into the data segment of the Login Request PDU.
  The DataSegmentLength and the actual size of the net buffer containing this PDU will be
  updated.

Arguments:

  Pdu   - The iSCSI PDU whose data segment the key-value pair will be added to.
  Key   - The key name string.
  Value - The value string.

Returns:

  EFI_SUCCESS          - The key-valu pair is added to the PDU's datasegment and
                         the correspondence length fields are updated.
  EFI_OUT_OF_RESOURCES - There is not enough space in the PDU to add the key-value
                         pair.

--*/
{
  UINT32              DataSegLen;
  UINT32              KeyLen;
  UINT32              ValueLen;
  UINT32              TotalLen;
  ISCSI_LOGIN_REQUEST *LoginReq;
  CHAR8               *Data;

  LoginReq    = (ISCSI_LOGIN_REQUEST *) NetbufGetByte (Pdu, 0, NULL);
  DataSegLen  = NTOH24 (LoginReq->DataSegmentLength);

  KeyLen      = (UINT32) EfiAsciiStrLen (Key);
  ValueLen    = (UINT32) EfiAsciiStrLen (Value);

  //
  // 1 byte for the key value separator '=' and 1 byte for the null
  // delimiter after the value.
  //
  TotalLen = KeyLen + 1 + ValueLen + 1;

  //
  // Allocate the space for the key-value pair.
  //
  Data = NetbufAllocSpace (Pdu, TotalLen, NET_BUF_TAIL);
  if (Data == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }
  //
  // Add the key.
  //
  NetCopyMem (Data, Key, KeyLen);
  Data += KeyLen;

  *Data = '=';
  Data++;

  //
  // Add the value.
  //
  NetCopyMem (Data, Value, ValueLen);
  Data += ValueLen;

  *Data = '\0';

  //
  // update the DataSegmentLength
  //
  ISCSI_SET_DATASEG_LEN (LoginReq, DataSegLen + TotalLen);

  return EFI_SUCCESS;
}

NET_BUF *
IScsiPrepareLoginReq (
  IN ISCSI_CONNECTION  *Conn
  )
/*++

Routine Description:

  Prepare the iSCSI login request to be sent according to the current login status.

Arguments:

  Conn - The connection in the iSCSI login phase.

Returns:

  The pointer to the net buffer containing the iSCSI login request built.

--*/
{
  ISCSI_SESSION       *Session;
  NET_BUF             *Nbuf;
  ISCSI_LOGIN_REQUEST *LoginReq;
  EFI_STATUS          Status;

  Session = Conn->Session;

  Nbuf    = NetbufAlloc (sizeof (ISCSI_LOGIN_REQUEST) + DEFAULT_MAX_RECV_DATA_SEG_LEN);
  if (Nbuf == NULL) {
    return NULL;
  }

  LoginReq = (ISCSI_LOGIN_REQUEST *) NetbufAllocSpace (Nbuf, sizeof (ISCSI_LOGIN_REQUEST), NET_BUF_TAIL);
  NetZeroMem (LoginReq, sizeof (ISCSI_LOGIN_REQUEST));

  //
  // Init the login request pdu
  //
  ISCSI_SET_OPCODE (LoginReq, ISCSI_OPCODE_LOGIN_REQ, ISCSI_REQ_IMMEDIATE);
  ISCSI_SET_STAGES (LoginReq, Conn->CurrentStage, Conn->NextStage);
  LoginReq->VersionMax        = ISCSI_VERSION_MAX;
  LoginReq->VersionMin        = ISCSI_VERSION_MIN;
  LoginReq->TSIH              = HTONS (Session->TSIH);
  LoginReq->InitiatorTaskTag  = HTONL (Session->InitiatorTaskTag);
  LoginReq->CID               = HTONS (Conn->CID);
  LoginReq->CmdSN             = HTONL (Session->CmdSN);

  //
  // For the first Login Request on a coonection this is ExpStatSN for the
  // old connection and this field is only valid if the Login Request restarts
  // a connection.
  // For subsequent Login Requests it is used to acknowledge the Login Responses
  // with their increasing StatSN values.
  //
  LoginReq->ExpStatSN = HTONL (Conn->ExpStatSN);
  NetCopyMem (LoginReq->ISID, Session->ISID, sizeof (LoginReq->ISID));

  if (Conn->PartialRspRcvd) {
    //
    // A partial response, initiator must send an empty Login Request.
    //
    return Nbuf;
  }

  switch (Conn->CurrentStage) {
  case ISCSI_SECURITY_NEGOTIATION:
    Status = IScsiCHAPToSendReq (Conn, Nbuf);
    break;

  case ISCSI_LOGIN_OPERATIONAL_NEGOTIATION:
    Status = IScsiFillOpParams (Conn, Nbuf);
    ISCSI_SET_FLAG (LoginReq, ISCSI_LOGIN_REQ_PDU_FLAG_TRANSIT);
    break;

  default:
    //
    // something error happens...
    //
    Status = EFI_DEVICE_ERROR;
    break;
  }

  if (EFI_ERROR (Status)) {
    NetbufFree (Nbuf);
    Nbuf = NULL;
  } else {
    //
    // Pad the data segment if needed.
    //
    IScsiPadSegment (Nbuf, ISCSI_GET_DATASEG_LEN (LoginReq));
    //
    // Check whether we will issue the stage transition signal?
    //
    Conn->TransitInitiated = ISCSI_FLAG_ON (LoginReq, ISCSI_LOGIN_REQ_PDU_FLAG_TRANSIT);
  }

  return Nbuf;
}

EFI_STATUS
IScsiProcessLoginRsp (
  IN ISCSI_CONNECTION  *Conn,
  IN NET_BUF           *Pdu
  )
/*++

Routine Description:

  Process the iSCSI Login Response.

Arguments:

  Conn - The connection on which the iSCSI login response is received.
  Pdu  - The iSCSI login response PDU.

Returns:

  EFI_SUCCESS        - The iSCSI login response PDU is processed and all check are passed.
  EFI_PROTOCOL_ERROR - Some kind of iSCSI protocol error happened.

--*/
{
  EFI_STATUS            Status;
  ISCSI_SESSION         *Session;
  ISCSI_LOGIN_RESPONSE  *LoginRsp;
  BOOLEAN               Transit;
  BOOLEAN               Continue;
  UINT8                 CurrentStage;
  UINT8                 NextStage;
  UINT8                 *DataSeg;
  UINT32                DataSegLen;

  Session   = Conn->Session;

  LoginRsp  = (ISCSI_LOGIN_RESPONSE *) NetbufGetByte (Pdu, 0, NULL);
  if (!ISCSI_CHECK_OPCODE (LoginRsp, ISCSI_OPCODE_LOGIN_RSP)) {
    //
    // It's not a Login Response
    //
    return EFI_PROTOCOL_ERROR;
  }
  //
  // Get the data segment if any.
  //
  DataSegLen = ISCSI_GET_DATASEG_LEN (LoginRsp);
  if (DataSegLen != 0) {
    DataSeg = NetbufGetByte (Pdu, sizeof (ISCSI_LOGIN_RESPONSE), NULL);
  } else {
    DataSeg = NULL;
  }
  //
  // Check the status class in the login response PDU.
  //
  switch (LoginRsp->StatusClass) {
  case ISCSI_LOGIN_STATUS_SUCCESS:
    //
    // Just break here, the response and the data segment will be processed later.
    //
    break;

  case ISCSI_LOGIN_STATUS_REDIRECTION:
    //
    // The target may be moved to a different address
    //
    if (DataSeg == NULL) {
      return EFI_PROTOCOL_ERROR;
    }
    //
    // Process the TargetAddress key-value strings in the data segment to update the
    // target address info.
    //
    Status = IScsiUpdateTargetAddress (Session, DataSeg, DataSegLen);
    if (EFI_ERROR (Status)) {
      return Status;
    }
    //
    // Session will be restarted on this error status because the Target is
    // redirected by this Login Response.
    //
    return EFI_MEDIA_CHANGED;

  default:
    //
    // Initiator Error, Target Error, or any other undefined error code.
    //
    return EFI_PROTOCOL_ERROR;
  }
  //
  // The status is sucess, extract the wanted fields from the header segment.
  //
  Transit                     = ISCSI_FLAG_ON (LoginRsp, ISCSI_LOGIN_RSP_PDU_FLAG_TRANSIT);
  Continue                    = ISCSI_FLAG_ON (LoginRsp, ISCSI_LOGIN_RSP_PDU_FLAG_CONTINUE);

  CurrentStage                = ISCSI_GET_CURRENT_STAGE (LoginRsp);
  NextStage                   = ISCSI_GET_NEXT_STAGE (LoginRsp);

  LoginRsp->InitiatorTaskTag  = NTOHL (LoginRsp->InitiatorTaskTag);

  if ((Transit && Continue) ||
      (CurrentStage != Conn->CurrentStage) ||
      (!Conn->TransitInitiated && Transit) ||
      (Transit && (NextStage != Conn->NextStage)) ||
      (NetCompareMem (Session->ISID, LoginRsp->ISID, sizeof (LoginRsp->ISID)) != 0) ||
      (LoginRsp->InitiatorTaskTag != Session->InitiatorTaskTag)
      ) {
    //
    // A Login Response with the C bit set to 1 MUST have the T bit set to 0;
    // The CSG in the Login Response MUST be the same with the I-end of this connection;
    // The T bit can't be 1 if the last Login Response sent by the initiator doesn't
    // initiate the transistion;
    // The NSG MUST be the same with the I-end of this connection if Transit is required.
    // The ISID in the Login Response MUST be the same with this session.
    //
    return EFI_PROTOCOL_ERROR;
  }

  LoginRsp->StatSN    = NTOHL (LoginRsp->StatSN);
  LoginRsp->ExpCmdSN  = NTOHL (LoginRsp->ExpCmdSN);
  LoginRsp->MaxCmdSN  = NTOHL (LoginRsp->MaxCmdSN);

  if ((Conn->CurrentStage == ISCSI_SECURITY_NEGOTIATION) && (Conn->CHAPStep == ISCSI_CHAP_INITIAL)) {
    //
    // It's the initial Login Response, initialize the local ExpStatSN, MaxCmdSN
    // and ExpCmdSN.
    //
    Conn->ExpStatSN   = LoginRsp->StatSN + 1;
    Session->MaxCmdSN = LoginRsp->MaxCmdSN;
    Session->ExpCmdSN = LoginRsp->ExpCmdSN;
  } else {
    //
    // Check the StatSN of this PDU
    //
    Status = IScsiCheckSN (&Conn->ExpStatSN, LoginRsp->StatSN);
    if (!EFI_ERROR (Status)) {
      //
      // Update the MaxCmdSN and ExpCmdSN
      //
      IScsiUpdateCmdSN (Session, LoginRsp->MaxCmdSN, LoginRsp->ExpCmdSN);
    } else {
      return Status;
    }
  }
  //
  // Trim off the header segment.
  //
  NetbufTrim (Pdu, sizeof (ISCSI_LOGIN_RESPONSE), NET_BUF_HEAD);

  //
  // Queue this login response first in case it's a partial response so that
  // later when the full response list is received we can combine these scattered
  // responses' data segment and then process it.
  //
  NET_GET_REF (Pdu);
  NetbufQueAppend (&Conn->RspQue, Pdu);

  Conn->PartialRspRcvd = Continue;
  if (Continue) {
    //
    // It's a partial response, have to wait for another or more Request/Response
    // conversations to get the full response.
    //
    return EFI_SUCCESS;
  }

  switch (CurrentStage) {
  case ISCSI_SECURITY_NEGOTIATION:
    //
    // In security negotiation stage, let CHAP module handle it.
    //
    Status = IScsiCHAPOnRspReceived (Conn, Transit);
    break;

  case ISCSI_LOGIN_OPERATIONAL_NEGOTIATION:
    //
    // Response received with negotiation resonse on iSCSI parameters, check them.
    //
    Status = IScsiCheckOpParams (Conn, Transit);
    break;

  default:
    //
    // Should never get here.
    //
    Status = EFI_PROTOCOL_ERROR;
    break;
  }

  if (Transit && (Status == EFI_SUCCESS)) {
    //
    // Do the state transition.
    //
    Conn->CurrentStage = Conn->NextStage;

    if (Conn->CurrentStage == ISCSI_LOGIN_OPERATIONAL_NEGOTIATION) {
      Conn->NextStage = ISCSI_FULL_FEATURE_PHASE;
    } else {
      //
      // CurrentStage is iSCSI Full Feature, it's the Login-Final Response,
      // get the TSIH from the Login Response.
      //
      Session->TSIH = NTOHS (LoginRsp->TSIH);
    }
  }
  //
  // Flush the response(s) received.
  //
  NetbufQueFlush (&Conn->RspQue);

  return Status;
}

EFI_STATUS
IScsiUpdateTargetAddress (
  IN ISCSI_SESSION  *Session,
  IN CHAR8          *Data,
  IN UINT32         Len
  )
/*++

Routine Description:

  Updated the target information according the data received in the iSCSI
  login response with an target redirection status.

Arguments:

  Session - The iSCSI session.
  Data    - The data segment which should contain the TargetAddress key-value list.
  Len     - Length of the data.

Returns:

  EFI_SUCCESS          - The target address is updated.
  EFI_OUT_OF_RESOURCES - Failed to allocate memory.
  EFI_NOT_FOUND        - The TargetAddress key is not found.

--*/
{
  NET_LIST_ENTRY  *KeyValueList;
  CHAR8           *TargetAddress;
  CHAR8           *IpStr;
  EFI_STATUS      Status;
  UINTN           Number;

  KeyValueList = IScsiBuildKeyValueList (Data, Len);
  if (KeyValueList == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  Status = EFI_NOT_FOUND;

  while (TRUE) {
    TargetAddress = IScsiGetValueByKeyFromList (KeyValueList, ISCSI_KEY_TARGET_ADDRESS);
    if (TargetAddress == NULL) {
      break;
    }

    if (!NET_IS_DIGIT (TargetAddress[0])) {
      //
      // The domainname of the target may be presented in three formats: a DNS host name,
      // a dotted-decimal IPv4 address, or a bracketed IPv6 address. Only accept dotted
      // IPv4 address.
      //
      continue;
    }

    IpStr = TargetAddress;

    while (*TargetAddress && (*TargetAddress != ':') && (*TargetAddress != ',')) {
      //
      // NULL, ':' or ',' ends the IPv4 string.
      //
      TargetAddress++;
    }

    if (*TargetAddress == ',') {
      //
      // Comma and the portal group tag MUST be ommitted if the TargetAddress is sent
      // as the result of a redirection.
      //
      continue;
    } else if (*TargetAddress == ':') {
      *TargetAddress = '\0';

      TargetAddress++;

      Number = NetAtoi (TargetAddress);
      if (Number > 0xFFFF) {
        continue;
      } else {
        Session->ConfigData.NvData.TargetPort = (UINT16) Number;
      }
    } else {
      //
      // The string only contains the IPv4 address. Use the well known port.
      //
      Session->ConfigData.NvData.TargetPort = ISCSI_WELL_KNOWN_PORT;
    }
    //
    // Update the target IP address.
    //
    Status = IScsiAsciiStrToIp (IpStr, &Session->ConfigData.NvData.TargetIp);
    if (EFI_ERROR (Status)) {
      continue;
    } else {
      break;
    }
  }

  IScsiFreeKeyValueList (KeyValueList);

  return Status;
}

VOID
IScsiFreeNbufList (
  VOID *Arg
  )
/*++

Routine Description:

  The callback function to free the net buffer list.

Arguments:

  Arg - The opaque parameter.

Returns:

  None.

--*/
{
  ASSERT (Arg != NULL);

  NetbufFreeList ((NET_LIST_ENTRY *) Arg);
  NetFreePool (Arg);
}

VOID
IScsiNbufExtFree (
  VOID *Arg
  )
/*++

Routine Description:

  The callback function called in NetBufFree, it does nothing.

Arguments:

  Arg - The opaque parameter.

Returns:

  None.

--*/
{
}

EFI_STATUS
IScsiReceivePdu (
  IN ISCSI_CONNECTION                      *Conn,
  OUT NET_BUF                              **Pdu,
  IN ISCSI_IN_BUFFER_CONTEXT               *Context, OPTIONAL
  IN BOOLEAN                               HeaderDigest,
  IN BOOLEAN                               DataDigest,
  IN EFI_EVENT                             TimeoutEvent OPTIONAL
  )
/*++

Routine Description:

  Receive an iSCSI response PDU. An iSCSI response PDU contains an iSCSI PDU header and
  an optional data segment. The two parts will be put into two blocks of buffers in the
  net buffer. The digest check will be conducted in this function if needed and the digests
  will be trimmed from the PDU buffer.

Arguments:

  Conn         - The iSCSI connection to receive data from.
  Pdu          - The received iSCSI pdu.
  Context      - The context used to describe information on the caller provided
                 buffer to receive data segment of the iSCSI pdu, it's optional.
  HeaderDigest - Whether there will be header digest received.
  DataDigest   - Whether there will be data digest.
  TimeoutEvent - The timeout event, it's optional.

Returns:

  EFI_SUCCESS - An iSCSI pdu is received.
  EFI_TIMEOUT - Timeout happenend.

--*/
{
  NET_LIST_ENTRY  *NbufList;
  UINT32          Len;
  NET_BUF         *PduHdr;
  UINT8           *Header;
  EFI_STATUS      Status;
  UINT32          PadLen;
  UINT32          InDataOffset;
  NET_FRAGMENT    Fragment[2];
  UINT32          FragmentCount;
  NET_BUF         *DataSeg;
  UINT32          PadAndCRC32[2];

  NbufList = NetAllocatePool (sizeof (NET_LIST_ENTRY));
  if (NbufList == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  NetListInit (NbufList);

  //
  // The header digest will be received together with the PDU header if exists.
  //
  Len     = sizeof (ISCSI_BASIC_HEADER) + (HeaderDigest ? sizeof (UINT32) : 0);
  PduHdr  = NetbufAlloc (Len);
  if (PduHdr == NULL) {
    Status = EFI_OUT_OF_RESOURCES;
    goto ON_EXIT;
  }

  Header = NetbufAllocSpace (PduHdr, Len, NET_BUF_TAIL);
  NetListInsertTail (NbufList, &PduHdr->List);

  //
  // First step, receive the BHS of the PDU.
  //
  Status = Tcp4IoReceive (&Conn->Tcp4Io, PduHdr, FALSE, TimeoutEvent);
  if (EFI_ERROR (Status)) {
    goto ON_EXIT;
  }

  if (HeaderDigest) {
    //
    // TODO: check the header-digest.
    //
    //
    // Trim off the digest.
    //
    NetbufTrim (PduHdr, sizeof (UINT32), NET_BUF_TAIL);
  }

  Len = ISCSI_GET_DATASEG_LEN (Header);
  if (Len == 0) {
    //
    // No data segment.
    //
    goto FORM_PDU;
  }
  //
  // Get the length of the padding bytes of the data segment.
  //
  PadLen = ISCSI_GET_PAD_LEN (Len);

  switch (ISCSI_GET_OPCODE (Header)) {
  case ISCSI_OPCODE_SCSI_DATA_IN:
    //
    // Try to use the buffer described by Context if the PDU is an
    // iSCSI SCSI data in pdu so as to reduce memory copy overhead.
    //
    InDataOffset = ISCSI_GET_BUFFER_OFFSET (Header);
    if ((Context == NULL) || ((InDataOffset + Len) > Context->InDataLen)) {
      Status = EFI_PROTOCOL_ERROR;
      goto ON_EXIT;
    }

    Fragment[0].Len   = Len;
    Fragment[0].Bulk  = Context->InData + InDataOffset;

    if (DataDigest || (PadLen != 0)) {
      //
      // The data segment is padded, use two fragments to receive it.
      // The first to receive the useful data. The second to receive the padding.
      //
      Fragment[1].Len   = PadLen + (DataDigest ? sizeof (UINT32) : 0);
      Fragment[1].Bulk  = (UINT8 *) ((UINTN) &PadAndCRC32[1] - PadLen);

      FragmentCount     = 2;
    } else {
      FragmentCount = 1;
    }

    DataSeg = NetbufFromExt (&Fragment[0], FragmentCount, 0, 0, IScsiNbufExtFree, NULL);
    if (DataSeg == NULL) {
      Status = EFI_OUT_OF_RESOURCES;
      goto ON_EXIT;
    }

    break;

  case ISCSI_OPCODE_SCSI_RSP:
  case ISCSI_OPCODE_NOP_IN:
  case ISCSI_OPCODE_LOGIN_RSP:
  case ISCSI_OPCODE_TEXT_RSP:
  case ISCSI_OPCODE_ASYNC_MSG:
  case ISCSI_OPCODE_REJECT:
  case ISCSI_OPCODE_VENDOR_T0:
  case ISCSI_OPCODE_VENDOR_T1:
  case ISCSI_OPCODE_VENDOR_T2:
    //
    // Allocate buffer to receive the data segment.
    //
    Len += PadLen + (DataDigest ? sizeof (UINT32) : 0);
    DataSeg = NetbufAlloc (Len);
    if (DataSeg == NULL) {
      Status = EFI_OUT_OF_RESOURCES;
      goto ON_EXIT;
    }

    NetbufAllocSpace (DataSeg, Len, NET_BUF_TAIL);
    break;

  default:
    Status = EFI_PROTOCOL_ERROR;
    goto ON_EXIT;
  }

  NetListInsertTail (NbufList, &DataSeg->List);

  //
  // Receive the data segment with the data digest if any.
  //
  Status = Tcp4IoReceive (&Conn->Tcp4Io, DataSeg, FALSE, TimeoutEvent);
  if (EFI_ERROR (Status)) {
    goto ON_EXIT;
  }

  if (DataDigest) {
    //
    // TODO: Check the data digest.
    //
    NetbufTrim (DataSeg, sizeof (UINT32), NET_BUF_TAIL);
  }

  if (PadLen != 0) {
    //
    // Trim off the padding bytes in the data segment.
    //
    NetbufTrim (DataSeg, PadLen, NET_BUF_TAIL);
  }

FORM_PDU:
  //
  // Form the pdu from a list of pdu segments.
  //
  *Pdu = NetbufFromBufList (NbufList, 0, 0, IScsiFreeNbufList, NbufList);
  if (*Pdu == NULL) {
    Status = EFI_OUT_OF_RESOURCES;
  }

ON_EXIT:

  if (EFI_ERROR (Status)) {
    //
    // Free the Nbufs in this NbufList and the NbufList itself.
    //
    IScsiFreeNbufList (NbufList);
  }

  return Status;
}

EFI_STATUS
IScsiCheckOpParams (
  IN ISCSI_CONNECTION  *Conn,
  IN BOOLEAN           Transit
  )
/*++

Routine Description:

  Check and get the result of the prameter negotiation.

Arguments:

  Conn - The connection in iSCSI login.
  Pdu  - The iSCSI response PDU containing the parameter list.

Returns:

  EFI_SUCCESS        - The parmeter check is passed and negotiation is finished.
  EFI_PROTOCOL_ERROR - Some kind of iSCSI protocol error happened.

--*/
{
  EFI_STATUS      Status;
  NET_LIST_ENTRY  *KeyValueList;
  CHAR8           *Data;
  UINT32          Len;
  ISCSI_SESSION   *Session;
  CHAR8           *Value;
  UINTN           NumericValue;

  ASSERT (Conn->RspQue.BufNum != 0);

  Session = Conn->Session;

  Len     = Conn->RspQue.BufSize;
  Data    = NetAllocatePool (Len);
  if (Data == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  NetbufQueCopy (&Conn->RspQue, 0, Len, Data);

  Status = EFI_PROTOCOL_ERROR;

  //
  // Extract the Key-Value pairs into a list.
  //
  KeyValueList = IScsiBuildKeyValueList (Data, Len);
  if (KeyValueList == NULL) {
    NetFreePool (Data);
    return Status;
  }
  //
  // HeaderDigest
  //
  Value = IScsiGetValueByKeyFromList (KeyValueList, ISCSI_KEY_HEADER_DIGEST);
  if (Value == NULL) {
    goto ON_ERROR;
  }

  if (EfiAsciiStrCmp (Value, "CRC32") == 0) {
    if (Conn->HeaderDigest != ISCSI_DIGEST_CRC32) {
      goto ON_ERROR;
    }
  } else if (EfiAsciiStrCmp (Value, ISCSI_KEY_VALUE_NONE) == 0) {
    Conn->HeaderDigest = ISCSI_DIGEST_NONE;
  } else {
    goto ON_ERROR;
  }
  //
  // DataDigest
  //
  Value = IScsiGetValueByKeyFromList (KeyValueList, ISCSI_KEY_DATA_DIGEST);
  if (Value == NULL) {
    goto ON_ERROR;
  }

  if (EfiAsciiStrCmp (Value, "CRC32") == 0) {
    if (Conn->DataDigest != ISCSI_DIGEST_CRC32) {
      goto ON_ERROR;
    }
  } else if (EfiAsciiStrCmp (Value, ISCSI_KEY_VALUE_NONE) == 0) {
    Conn->DataDigest = ISCSI_DIGEST_NONE;
  } else {
    goto ON_ERROR;
  }
  //
  // ErrorRecoveryLevel, result fuction is Minimum.
  //
  Value = IScsiGetValueByKeyFromList (KeyValueList, ISCSI_KEY_ERROR_RECOVERY_LEVEL);
  if (Value == NULL) {
    goto ON_ERROR;
  }

  NumericValue = NetAtoi (Value);
  if (NumericValue > 2) {
    goto ON_ERROR;
  }

  Session->ErrorRecoveryLevel = (UINT8) NET_MIN (Session->ErrorRecoveryLevel, NumericValue);

  //
  // InitialR2T, result function is OR.
  //
  Value = IScsiGetValueByKeyFromList (KeyValueList, ISCSI_KEY_INITIAL_R2T);
  if (Value == NULL) {
    goto ON_ERROR;
  }

  Session->InitialR2T = Session->InitialR2T || (BOOLEAN) (EfiAsciiStrCmp (Value, "Yes") == 0);

  //
  // ImmediateData, result function is AND.
  //
  Value = IScsiGetValueByKeyFromList (KeyValueList, ISCSI_KEY_IMMEDIATE_DATA);
  if (Value == NULL) {
    goto ON_ERROR;
  }

  Session->ImmediateData = Session->ImmediateData && (BOOLEAN) (EfiAsciiStrCmp (Value, "Yes") == 0);

  //
  // MaxRecvDataSegmentLength is declarative.
  //
  Value = IScsiGetValueByKeyFromList (KeyValueList, ISCSI_KEY_MAX_RECV_DATA_SEGMENT_LENGTH);
  if (Value != NULL) {
      Conn->MaxRecvDataSegmentLength = (UINT32) NetAtoi (Value);
  }
  //
  // MaxBurstLength, result funtion is Mininum.
  //
  Value = IScsiGetValueByKeyFromList (KeyValueList, ISCSI_KEY_MAX_BURST_LENGTH);
  if (Value == NULL) {
    goto ON_ERROR;
  }

  NumericValue            = NetAtoi (Value);
  Session->MaxBurstLength = (UINT32) NET_MIN (Session->MaxBurstLength, NumericValue);

  //
  // FirstBurstLength, result function is Minimum. Irrelevant when InitialR2T=Yes and
  // ImmediateData=No.
  //
  Value = IScsiGetValueByKeyFromList (KeyValueList, ISCSI_KEY_FIRST_BURST_LENGTH);
  if ((Value == NULL) && !(Session->InitialR2T && !Session->ImmediateData)) {
    goto ON_ERROR;
  }

  NumericValue              = NetAtoi (Value);
  Session->FirstBurstLength = (UINT32) NET_MIN (Session->FirstBurstLength, NumericValue);

  //
  // MaxConnections, result function is Minimum.
  //
  Value = IScsiGetValueByKeyFromList (KeyValueList, ISCSI_KEY_MAX_CONNECTIONS);
  if (Value == NULL) {
    goto ON_ERROR;
  }

  NumericValue = NetAtoi (Value);
  if ((NumericValue == 0) || (NumericValue > 65535)) {
    goto ON_ERROR;
  }

  Session->MaxConnections = (UINT32) NET_MIN (Session->MaxConnections, NumericValue);

  //
  // DataPDUInOrder, result function is OR.
  //
  Value = IScsiGetValueByKeyFromList (KeyValueList, ISCSI_KEY_DATA_PDU_IN_ORDER);
  if (Value == NULL) {
    goto ON_ERROR;
  }

  Session->DataPDUInOrder = Session->DataPDUInOrder || (BOOLEAN) (EfiAsciiStrCmp (Value, "Yes") == 0);

  //
  // DataSequenceInorder, result function is OR.
  //
  Value = IScsiGetValueByKeyFromList (KeyValueList, ISCSI_KEY_DATA_SEQUENCE_IN_ORDER);
  if (Value == NULL) {
    goto ON_ERROR;
  }

  Session->DataSequenceInOrder = Session->DataSequenceInOrder || (BOOLEAN) (EfiAsciiStrCmp (Value, "Yes") == 0);

  //
  // DefaultTime2Wait, result function is Maximum.
  //
  Value = IScsiGetValueByKeyFromList (KeyValueList, ISCSI_KEY_DEFAULT_TIME2WAIT);
  if (Value == NULL) {
    goto ON_ERROR;
  }

  NumericValue = NetAtoi (Value);
  if (NumericValue == 0) {
    Session->DefaultTime2Wait = 0;
  } else if (NumericValue > 3600) {
    goto ON_ERROR;
  } else {
    Session->DefaultTime2Wait = (UINT32) NET_MAX (Session->DefaultTime2Wait, NumericValue);
  }
  //
  // DefaultTime2Retain, result function is Minimum.
  //
  Value = IScsiGetValueByKeyFromList (KeyValueList, ISCSI_KEY_DEFAULT_TIME2RETAIN);
  if (Value == NULL) {
    goto ON_ERROR;
  }

  NumericValue = NetAtoi (Value);
  if (NumericValue == 0) {
    Session->DefaultTime2Retain = 0;
  } else if (NumericValue > 3600) {
    goto ON_ERROR;
  } else {
    Session->DefaultTime2Retain = (UINT32) NET_MIN (Session->DefaultTime2Retain, NumericValue);
  }
  //
  // MaxOutstandingR2T, result function is Minimum.
  //
  Value = IScsiGetValueByKeyFromList (KeyValueList, ISCSI_KEY_MAX_OUTSTANDING_R2T);
  if (Value == NULL) {
    goto ON_ERROR;
  }

  NumericValue = NetAtoi (Value);
  if ((NumericValue == 0) || (NumericValue > 65535)) {
    goto ON_ERROR;
  }

  Session->MaxOutstandingR2T = (UINT16) NET_MIN (Session->MaxOutstandingR2T, NumericValue);

  //
  // Remove declarative key-value paris if any.
  //
  IScsiGetValueByKeyFromList (KeyValueList, ISCSI_KEY_SESSION_TYPE);
  IScsiGetValueByKeyFromList (KeyValueList, ISCSI_KEY_TARGET_ALIAS);
  IScsiGetValueByKeyFromList (KeyValueList, ISCSI_KEY_TARGET_PORTAL_GROUP_TAG);

  if (NetListIsEmpty (KeyValueList)) {
    //
    // Succeed if no more keys in the list.
    //
    Status = EFI_SUCCESS;
  }

ON_ERROR:

  IScsiFreeKeyValueList (KeyValueList);

  NetFreePool (Data);

  return Status;
}

EFI_STATUS
IScsiFillOpParams (
  IN ISCSI_CONNECTION  *Conn,
  IN NET_BUF           *Pdu
  )
/*++

Routine Description:

  Fill the oprational prameters.

Arguments:

  Conn - The connection in iSCSI login.
  Pdu  - The iSCSI login request PDU to fill the parameters.

Returns:

  EFI_SUCCESS          - The parmeters are filled into the iSCSI login request PDU.
  EFI_OUT_OF_RESOURCES - There is not enough space in the PDU to hold the parameters.

--*/
{
  ISCSI_SESSION *Session;
  CHAR8         Value[256];

  Session = Conn->Session;

  ASPrint (Value, sizeof (Value), "%a", (Conn->HeaderDigest == ISCSI_DIGEST_CRC32) ? "None,CRC32" : "None");
  IScsiAddKeyValuePair (Pdu, ISCSI_KEY_HEADER_DIGEST, Value);

  ASPrint (Value, sizeof (Value), "%a", (Conn->DataDigest == ISCSI_DIGEST_CRC32) ? "None,CRC32" : "None");
  IScsiAddKeyValuePair (Pdu, ISCSI_KEY_DATA_DIGEST, Value);

  ASPrint (Value, sizeof (Value), "%d", Session->ErrorRecoveryLevel);
  IScsiAddKeyValuePair (Pdu, ISCSI_KEY_ERROR_RECOVERY_LEVEL, Value);

  ASPrint (Value, sizeof (Value), "%a", Session->InitialR2T ? "Yes" : "No");
  IScsiAddKeyValuePair (Pdu, ISCSI_KEY_INITIAL_R2T, Value);

  ASPrint (Value, sizeof (Value), "%a", Session->ImmediateData ? "Yes" : "No");
  IScsiAddKeyValuePair (Pdu, ISCSI_KEY_IMMEDIATE_DATA, Value);

  ASPrint (Value, sizeof (Value), "%d", MAX_RECV_DATA_SEG_LEN_IN_FFP);
  IScsiAddKeyValuePair (Pdu, ISCSI_KEY_MAX_RECV_DATA_SEGMENT_LENGTH, Value);

  ASPrint (Value, sizeof (Value), "%d", Session->MaxBurstLength);
  IScsiAddKeyValuePair (Pdu, ISCSI_KEY_MAX_BURST_LENGTH, Value);

  ASPrint (Value, sizeof (Value), "%d", Session->FirstBurstLength);
  IScsiAddKeyValuePair (Pdu, ISCSI_KEY_FIRST_BURST_LENGTH, Value);

  ASPrint (Value, sizeof (Value), "%d", Session->MaxConnections);
  IScsiAddKeyValuePair (Pdu, ISCSI_KEY_MAX_CONNECTIONS, Value);

  ASPrint (Value, sizeof (Value), "%a", Session->DataPDUInOrder ? "Yes" : "No");
  IScsiAddKeyValuePair (Pdu, ISCSI_KEY_DATA_PDU_IN_ORDER, Value);

  ASPrint (Value, sizeof (Value), "%a", Session->DataSequenceInOrder ? "Yes" : "No");
  IScsiAddKeyValuePair (Pdu, ISCSI_KEY_DATA_SEQUENCE_IN_ORDER, Value);

  ASPrint (Value, sizeof (Value), "%d", Session->DefaultTime2Wait);
  IScsiAddKeyValuePair (Pdu, ISCSI_KEY_DEFAULT_TIME2WAIT, Value);

  ASPrint (Value, sizeof (Value), "%d", Session->DefaultTime2Retain);
  IScsiAddKeyValuePair (Pdu, ISCSI_KEY_DEFAULT_TIME2RETAIN, Value);

  ASPrint (Value, sizeof (Value), "%d", Session->MaxOutstandingR2T);
  IScsiAddKeyValuePair (Pdu, ISCSI_KEY_MAX_OUTSTANDING_R2T, Value);

  return EFI_SUCCESS;
}

EFI_STATUS
IScsiPadSegment (
  IN NET_BUF  *Pdu,
  IN UINT32   Len
  )
/*++

Routine Description:

  Pad the iSCSI AHS or data segment to an integer number of 4 byte words.

Arguments:

  Pdu - The iSCSI pdu which contains segments to pad.
  Len - The length of the last semgnet in the PDU.

Returns:

  EFI_SUCCESS          - The segment is padded or no need to pad it.
  EFI_OUT_OF_RESOURCES - There is not enough remaining free space to add the
                         padding bytes.

--*/
{
  UINT32  PadLen;
  UINT8   *Data;

  PadLen = ISCSI_GET_PAD_LEN (Len);

  if (PadLen != 0) {
    Data = NetbufAllocSpace (Pdu, PadLen, NET_BUF_TAIL);
    if (Data == NULL) {
      return EFI_OUT_OF_RESOURCES;
    }

    NetZeroMem (Data, PadLen);
  }

  return EFI_SUCCESS;
}

NET_LIST_ENTRY *
IScsiBuildKeyValueList (
  IN CHAR8  *Data,
  IN UINT32 Len
  )
/*++

Routine Description:

  Build a key-value list from the data segment.

Arguments:

  Data - The data segment containing the key-value pairs.
  Len  - Length of the data segment.

Returns:

  The key-value list.

--*/
{
  NET_LIST_ENTRY        *ListHead;
  ISCSI_KEY_VALUE_PAIR  *KeyValuePair;

  ListHead = NetAllocatePool (sizeof (NET_LIST_ENTRY));
  if (ListHead == NULL) {
    return NULL;
  }

  NetListInit (ListHead);

  while (Len > 0) {
    KeyValuePair = NetAllocatePool (sizeof (ISCSI_KEY_VALUE_PAIR));
    if (KeyValuePair == NULL) {
      goto ON_ERROR;
    }

    NetListInit (&KeyValuePair->List);

    KeyValuePair->Key = Data;

    while ((Len > 0) && (*Data != '=')) {
      Len--;
      Data++;
    }

    if (*Data == '=') {
      *Data = '\0';

      Data++;
      Len--;
    } else {
      NetFreePool (KeyValuePair);
      goto ON_ERROR;
    }

    KeyValuePair->Value = Data;

    NetListInsertTail (ListHead, &KeyValuePair->List);;

    Data += EfiAsciiStrLen (KeyValuePair->Value) + 1;
    Len -= (UINT32) EfiAsciiStrLen (KeyValuePair->Value) + 1;
  }

  return ListHead;

ON_ERROR:

  IScsiFreeKeyValueList (ListHead);

  return NULL;
}

CHAR8 *
IScsiGetValueByKeyFromList (
  IN NET_LIST_ENTRY  *KeyValueList,
  IN CHAR8           *Key
  )
/*++

Routine Description:

  Get the value string by the key name from the key-value list. If found,
  the key-value entry will be removed from the list.

Arguments:

  KeyValueList - The key-value list.
  Key          - The key name to find.

Returns:

  The value string.

--*/
{
  NET_LIST_ENTRY        *Entry;
  ISCSI_KEY_VALUE_PAIR  *KeyValuePair;
  CHAR8                 *Value;

  Value = NULL;

  NET_LIST_FOR_EACH (Entry, KeyValueList) {
    KeyValuePair = NET_LIST_USER_STRUCT (Entry, ISCSI_KEY_VALUE_PAIR, List);

    if (EfiAsciiStrCmp (KeyValuePair->Key, Key) == 0) {
      Value = KeyValuePair->Value;

      NetListRemoveEntry (&KeyValuePair->List);
      NetFreePool (KeyValuePair);
      break;
    }
  }

  return Value;
}

VOID
IScsiFreeKeyValueList (
  IN NET_LIST_ENTRY  *KeyValueList
  )
/*++

Routine Description:

  Free the key-value list.

Arguments:

  KeyValueList - The key-value list.

Returns:

  None.

--*/
{
  NET_LIST_ENTRY        *Entry;
  ISCSI_KEY_VALUE_PAIR  *KeyValuePair;

  while (!NetListIsEmpty (KeyValueList)) {
    Entry         = NetListRemoveHead (KeyValueList);
    KeyValuePair  = NET_LIST_USER_STRUCT (Entry, ISCSI_KEY_VALUE_PAIR, List);

    NetFreePool (KeyValuePair);
  }

  NetFreePool (KeyValueList);
}

EFI_STATUS
IScsiNormalizeName (
  IN CHAR8  *Name,
  IN UINTN  Len
  )
/*++

Routine Description:

  Normalize the iSCSI name according to RFC.

Arguments:

  Name - The iSCSI name.
  Len  - length of the iSCSI name.

Returns:

  EFI_SUCCESS        - The iSCSI name is valid and normalized.
  EFI_PROTOCOL_ERROR - The iSCSI name is mal-formatted or not in the IQN format.

--*/
{
  UINTN Index;

  for (Index = 0; Index < Len; Index++) {
    if (NET_IS_UPPER_CASE_CHAR (Name[Index])) {
      //
      // Convert the upper-case characters to lower-case ones
      //
      Name[Index] = Name[Index] - 'A' + 'a';
    }

    if (!NET_IS_LOWER_CASE_CHAR (Name[Index]) &&
        !NET_IS_DIGIT (Name[Index]) &&
        (Name[Index] != '-') &&
        (Name[Index] != '.') &&
        (Name[Index] != ':')
        ) {
      //
      // ASCII dash, dot, colon lower-case characters and digit characters
      // are allowed.
      //
      return EFI_PROTOCOL_ERROR;
    }
  }

  if ((Len < 4) || (NetCompareMem (Name, "iqn.", 4) != 0)) {
    //
    // Only IQN format is accepted now.
    //
    return EFI_PROTOCOL_ERROR;
  }

  return EFI_SUCCESS;
}

EFI_STATUS
IScsiNewTcb (
  IN  ISCSI_CONNECTION  *Conn,
  OUT ISCSI_TCB         **Tcb
  )
/*++

Routine Description:

  Create an iSCSI task control block.

Arguments:

  Conn - The connection on which the task control block will be created.
  Tcb  - The newly created task control block.

Returns:

  EFI_SUCCESS          - The task control block is created.
  EFI_OUT_OF_RESOURCES - Failed to allocate memory.

--*/
{
  ISCSI_SESSION *Session;
  ISCSI_TCB     *NewTcb;

  ASSERT (Tcb != NULL);

  Session = Conn->Session;

  if (ISCSI_SEQ_GT (Session->CmdSN, Session->MaxCmdSN)) {
    return EFI_NOT_READY;
  }

  NewTcb = NetAllocateZeroPool (sizeof (ISCSI_TCB));
  if (NewTcb == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  NetListInit (&NewTcb->Link);

  NewTcb->SoFarInOrder      = TRUE;
  NewTcb->InitiatorTaskTag  = Session->InitiatorTaskTag;
  NewTcb->CmdSN             = Session->CmdSN;
  NewTcb->Conn              = Conn;

  NetListInsertTail (&Session->TcbList, &NewTcb->Link);

  //
  // Advance the initiator task tag.
  //
  Session->InitiatorTaskTag++;
  Session->CmdSN++;

  *Tcb = NewTcb;

  return EFI_SUCCESS;
}

VOID
IScsiDelTcb (
  IN ISCSI_TCB  *Tcb
  )
/*++

Routine Description:

  Delete the tcb from the connection and destroy it.

Arguments:

  Tcb - The tcb to delete.

Returns:

  None.

--*/
{
  NetListRemoveEntry (&Tcb->Link);

  NetFreePool (Tcb);
}

ISCSI_TCB *
IScsiFindTcbByITT (
  IN NET_LIST_ENTRY  *TcbList,
  IN UINT32          InitiatorTaskTag
  )
/*++

Routine Description:

  Find the task control block by the initator task tag.

Arguments:

  TcbList          - The tcb list.
  InitiatorTaskTag - The initiator task tag.

Returns:

  The task control block found.

--*/
{
  ISCSI_TCB       *Tcb;
  NET_LIST_ENTRY  *Entry;

  Tcb = NULL;

  NET_LIST_FOR_EACH (Entry, TcbList) {
    Tcb = NET_LIST_USER_STRUCT (Entry, ISCSI_TCB, Link);

    if (Tcb->InitiatorTaskTag == InitiatorTaskTag) {
      break;
    }

    Tcb = NULL;
  }

  return Tcb;
}

NET_BUF *
IScsiNewDataSegment (
  IN UINT8    *Data,
  IN UINT32   Len,
  IN BOOLEAN  DataDigest
  )
/*++

Routine Description:

  Create a data segment, pad it and calculate the CRC if needed.

Arguments:

  Data       - The data to fill into the data segment.
  Len        - Length of the data.
  DataDigest - Whether to calculate CRC for this data segment.

Returns:

  The net buffer wrapping the data segment.

--*/
{
  NET_FRAGMENT  Fragment[2];
  UINT32        FragmentCount;
  UINT32        PadLen;
  NET_BUF       *DataSeg;

  Fragment[0].Len   = Len;
  Fragment[0].Bulk  = Data;

  PadLen            = ISCSI_GET_PAD_LEN (Len);
  if (PadLen != 0) {
    Fragment[1].Len   = PadLen;
    Fragment[1].Bulk  = (UINT8 *) &mDataSegPad;

    FragmentCount     = 2;
  } else {
    FragmentCount = 1;
  }

  DataSeg = NetbufFromExt (&Fragment[0], FragmentCount, 0, 0, IScsiNbufExtFree, NULL);

  return DataSeg;
}

NET_BUF *
IScsiNewScsiCmdPdu (
  IN EFI_EXT_SCSI_PASS_THRU_SCSI_REQUEST_PACKET *Packet,
  IN UINT64                                     Lun,
  IN ISCSI_TCB                                  *Tcb
  )
/*++

Routine Description:

  Create a iSCSI SCSI command PDU to encapsulate the command issued
  by SCSI through the EXT SCSI PASS THRU Protocol.

Arguments:

  Packet - The EXT SCSI PASS THRU request packet containing the SCSI command.
  Lun    - The LUN.
  Tcb    - The tcb assocated with this SCSI command.

Returns:

  The created iSCSI SCSI command PDU.

--*/
{
  NET_LIST_ENTRY                  *NbufList;
  NET_BUF                         *Pdu;
  NET_BUF                         *PduHeader;
  NET_BUF                         *DataSeg;
  SCSI_COMMAND                    *ScsiCmd;
  UINT8                           AHSLength;
  UINT32                          Length;
  ISCSI_ADDITIONAL_HEADER         *Header;
  ISCSI_BI_EXP_READ_DATA_LEN_AHS  *BiExpReadDataLenAHS;
  ISCSI_SESSION                   *Session;
  UINT32                          ImmediateDataLen;

  AHSLength = 0;

  if (Packet->DataDirection == DataBi) {
    //
    // Bi directional Read/Write command, the bidirectional expected
    // read data length AHS is required.
    //
    AHSLength += sizeof (ISCSI_BI_EXP_READ_DATA_LEN_AHS);
  }

  if (Packet->CdbLength > 16) {
    //
    // The CDB exceeds 16 bytes, an extended CDB AHS is required.
    //
    AHSLength += ISCSI_ROUNDUP (Packet->CdbLength - 16) + sizeof (ISCSI_ADDITIONAL_HEADER);
  }

  Length    = sizeof (SCSI_COMMAND) + AHSLength;
  PduHeader = NetbufAlloc (Length);
  if (PduHeader == NULL) {
    return NULL;
  }

  ScsiCmd = (SCSI_COMMAND *) NetbufAllocSpace (PduHeader, Length, NET_BUF_TAIL);
  Header  = (ISCSI_ADDITIONAL_HEADER *) (ScsiCmd + 1);

  NetZeroMem (ScsiCmd, Length);

  ISCSI_SET_OPCODE (ScsiCmd, ISCSI_OPCODE_SCSI_CMD, 0);
  ISCSI_SET_FLAG (ScsiCmd, ISCSI_TASK_ATTR_SIMPLE);

  //
  // Set the READ/WRITE flags according to the IO type of this request.
  //
  switch (Packet->DataDirection) {
  case DataIn:
    ISCSI_SET_FLAG (ScsiCmd, SCSI_CMD_PDU_FLAG_READ);
    ScsiCmd->ExpDataXferLength = NTOHL (Packet->InTransferLength);
    break;

  case DataOut:
    ISCSI_SET_FLAG (ScsiCmd, SCSI_CMD_PDU_FLAG_WRITE);
    ScsiCmd->ExpDataXferLength = NTOHL (Packet->OutTransferLength);
    break;

  case DataBi:
    ISCSI_SET_FLAG (ScsiCmd, SCSI_CMD_PDU_FLAG_READ | SCSI_CMD_PDU_FLAG_WRITE);
    ScsiCmd->ExpDataXferLength = NTOHL (Packet->OutTransferLength);

    //
    // Fill the bidirectional expected read data length AHS.
    //
    BiExpReadDataLenAHS                     = (ISCSI_BI_EXP_READ_DATA_LEN_AHS *) Header;
    Header = (ISCSI_ADDITIONAL_HEADER *) (BiExpReadDataLenAHS + 1);

    BiExpReadDataLenAHS->Length = NTOHS (5);
    BiExpReadDataLenAHS->Type = ISCSI_AHS_TYPE_BI_EXP_READ_DATA_LEN;
    BiExpReadDataLenAHS->ExpReadDataLength = NTOHL (Packet->InTransferLength);

    break;
  }

  ScsiCmd->TotalAHSLength = AHSLength;
  NetCopyMem (ScsiCmd->Lun, &Lun, sizeof (ScsiCmd->Lun));
  ScsiCmd->InitiatorTaskTag = NTOHL (Tcb->InitiatorTaskTag);
  ScsiCmd->CmdSN            = NTOHL (Tcb->CmdSN);
  ScsiCmd->ExpStatSN        = NTOHL (Tcb->Conn->ExpStatSN);

  NetCopyMem (ScsiCmd->CDB, Packet->Cdb, sizeof (ScsiCmd->CDB));

  if (Packet->CdbLength > 16) {
    Header->Length  = NTOHS (Packet->CdbLength - 15);
    Header->Type    = ISCSI_AHS_TYPE_EXT_CDB;

    NetCopyMem (Header + 1, (UINT8 *) Packet->Cdb + 16, Packet->CdbLength - 16);
  }

  Pdu               = PduHeader;
  Session           = Tcb->Conn->Session;
  ImmediateDataLen  = 0;

  if (Session->ImmediateData && (Packet->OutTransferLength != 0)) {
    //
    // Send immediate data in this SCSI Command PDU. The length of the immeidate
    // data is the minimum of FirstBurstLength, the data length to be xfered and
    // the MaxRecvdataSegmentLength on this connection.
    //
    ImmediateDataLen  = NET_MIN (Session->FirstBurstLength, Packet->OutTransferLength);
    ImmediateDataLen  = NET_MIN (ImmediateDataLen, Tcb->Conn->MaxRecvDataSegmentLength);

    //
    // Update the data segment length in the PDU header.
    //
    ISCSI_SET_DATASEG_LEN (ScsiCmd, ImmediateDataLen);

    //
    // Create the data segment.
    //
    DataSeg = IScsiNewDataSegment ((UINT8 *) Packet->OutDataBuffer, ImmediateDataLen, FALSE);
    if (DataSeg == NULL) {
      NetbufFree (PduHeader);
      Pdu = NULL;
      goto ON_EXIT;
    }

    NbufList = NetAllocatePool (sizeof (NET_LIST_ENTRY));
    if (NbufList == NULL) {
      NetbufFree (PduHeader);
      NetbufFree (DataSeg);

      Pdu = NULL;
      goto ON_EXIT;
    }

    NetListInit (NbufList);
    NetListInsertTail (NbufList, &PduHeader->List);
    NetListInsertTail (NbufList, &DataSeg->List);

    Pdu = NetbufFromBufList (NbufList, 0, 0, IScsiFreeNbufList, NbufList);
    if (Pdu == NULL) {
      IScsiFreeNbufList (NbufList);
    }
  }

  if (Session->InitialR2T ||
      (ImmediateDataLen == Session->FirstBurstLength) ||
      (ImmediateDataLen == Packet->OutTransferLength)
      ) {
    //
    // Unsolicited data out sequence is not allowed,
    // or FirstBustLength data is already sent out by immediate data
    // or all the OUT data accompany this SCSI packet is sent as
    // immediate data, the final flag should be set on this SCSI Command
    // PDU.
    //
    ISCSI_SET_FLAG (ScsiCmd, ISCSI_BHS_FLAG_FINAL);
  }

ON_EXIT:

  return Pdu;
}

NET_BUF *
IScsiNewDataOutPdu (
  IN UINT8      *Data,
  IN UINT32     Len,
  IN UINT32     DataSN,
  IN ISCSI_TCB  *Tcb,
  IN UINT64     Lun
  )
/*++

Routine Description:

  Create a new iSCSI SCSI Data Out PDU.

Arguments:

  Data   - The data to put into the Data Out PDU.
  Len    - Length of the data.
  DataSN - The DataSN of the Data Out PDU.
  Tcb    - The task control block of this Data Out PDU.
  Lun    - The LUN.

Returns:

  The net buffer wrapping the Data Out PDU.

--*/
{
  NET_LIST_ENTRY      *NbufList;
  NET_BUF             *PduHdr;
  NET_BUF             *DataSeg;
  NET_BUF             *Pdu;
  ISCSI_SCSI_DATA_OUT *DataOutHdr;
  ISCSI_XFER_CONTEXT  *XferContext;

  NbufList = NetAllocatePool (sizeof (NET_LIST_ENTRY));
  if (NbufList == NULL) {
    return NULL;
  }

  NetListInit (NbufList);

  //
  // Allocate memory for the BHS.
  //
  PduHdr = NetbufAlloc (sizeof (ISCSI_SCSI_DATA_OUT));
  if (PduHdr == NULL) {
    NetFreePool (NbufList);
    return NULL;
  }
  //
  // Insert the BHS into the buffer list.
  //
  NetListInsertTail (NbufList, &PduHdr->List);

  DataOutHdr  = (ISCSI_SCSI_DATA_OUT *) NetbufAllocSpace (PduHdr, sizeof (ISCSI_SCSI_DATA_OUT), NET_BUF_TAIL);
  XferContext = &Tcb->XferContext;

  NetZeroMem (DataOutHdr, sizeof (ISCSI_SCSI_DATA_OUT));

  //
  // Set the flags and fields of the Data Out PDU BHS.
  //
  ISCSI_SET_OPCODE (DataOutHdr, ISCSI_OPCODE_SCSI_DATA_OUT, 0);
  ISCSI_SET_DATASEG_LEN (DataOutHdr, Len);

  DataOutHdr->InitiatorTaskTag  = HTONL (Tcb->InitiatorTaskTag);
  DataOutHdr->TargetTransferTag = HTONL (XferContext->TargetTransferTag);
  DataOutHdr->ExpStatSN         = HTONL (Tcb->Conn->ExpStatSN);
  DataOutHdr->DataSN            = HTONL (DataSN);
  DataOutHdr->BufferOffset      = HTONL (XferContext->Offset);

  if (XferContext->TargetTransferTag != ISCSI_RESERVED_TAG) {
    NetCopyMem (&DataOutHdr->Lun, &Lun, sizeof (DataOutHdr->Lun));
  }
  //
  // Build the data segment for this Data Out PDU.
  //
  DataSeg = IScsiNewDataSegment (Data, Len, FALSE);
  if (DataSeg == NULL) {
    IScsiFreeNbufList (NbufList);
    return NULL;
  }
  //
  // Put the data segment into the buffer list and combine it with the BHS
  // into a full Data Out PDU.
  //
  NetListInsertTail (NbufList, &DataSeg->List);
  Pdu = NetbufFromBufList (NbufList, 0, 0, IScsiFreeNbufList, NbufList);
  if (Pdu == NULL) {
    IScsiFreeNbufList (NbufList);
  }

  return Pdu;
}

NET_LIST_ENTRY *
IScsiGenerateDataOutPduSequence (
  IN UINT8      *Data,
  IN ISCSI_TCB  *Tcb,
  IN UINT64     Lun
  )
/*++

Routine Description:

  Generate a consecutive sequence of iSCSI SCSI Data Out PDUs.

Arguments:

  Data - The data  which will be carried by the sequence of iSCSI SCSI Data Out PDUs.
  Tcb  - The task control block of the data to send out.
  Lun  - The LUN the data will be sent to.

Returns:

  A list of net buffers with each of them wraps an iSCSI SCSI Data Out PDU.

--*/
{
  NET_LIST_ENTRY      *PduList;
  UINT32              DataSN;
  UINT32              DataLen;
  NET_BUF             *DataOutPdu;
  ISCSI_CONNECTION    *Conn;
  ISCSI_XFER_CONTEXT  *XferContext;

  PduList = NetAllocatePool (sizeof (EFI_LIST_ENTRY));
  if (PduList == NULL) {
    return NULL;
  }

  NetListInit (PduList);

  DataSN      = 0;
  Conn        = Tcb->Conn;
  DataOutPdu  = NULL;
  XferContext = &Tcb->XferContext;

  while (XferContext->DesiredLength > 0) {
    //
    // Determine the length of data this Data Out PDU can carry.
    //
    DataLen = NET_MIN (XferContext->DesiredLength, Conn->MaxRecvDataSegmentLength);

    //
    // Create a Data Out PDU.
    //
    DataOutPdu = IScsiNewDataOutPdu (Data, DataLen, DataSN, Tcb, Lun);
    if (DataOutPdu == NULL) {
      IScsiFreeNbufList (PduList);
      PduList = NULL;

      goto ON_EXIT;
    }

    NetListInsertTail (PduList, &DataOutPdu->List);

    //
    // Update the context and DataSN.
    //
    Data += DataLen;
    XferContext->Offset += DataLen;
    XferContext->DesiredLength -= DataLen;
    DataSN++;
  }
  //
  // Set the F bit for the last data out PDU in this sequence.
  //
  ISCSI_SET_FLAG (NetbufGetByte (DataOutPdu, 0, NULL), ISCSI_BHS_FLAG_FINAL);

ON_EXIT:

  return PduList;
}

EFI_STATUS
IScsiSendDataOutPduSequence (
  IN UINT8      *Data,
  IN UINT64     Lun,
  IN ISCSI_TCB  *Tcb
  )
/*++

Routine Description:

  Send the Data in a sequence of Data Out PDUs one by one.

Arguments:

  Data - The data to carry by Data Out PDUs.
  Lun  - The LUN the data will be sent to.
  Tcb  - The task control block.

Returns:

  EFI_SUCCES           - The data is sent out to the LUN.
  EFI_OUT_OF_RESOURCES - Failed to allocate memory.

--*/
{
  NET_LIST_ENTRY  *DataOutPduList;
  NET_LIST_ENTRY  *Entry;
  NET_BUF         *Pdu;
  EFI_STATUS      Status;

  //
  // Generate the Data Out PDU sequence.
  //
  DataOutPduList = IScsiGenerateDataOutPduSequence (Data, Tcb, Lun);
  if (DataOutPduList == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  Status = EFI_SUCCESS;

  //
  // Send the Data Out PDU's one by one.
  //
  NET_LIST_FOR_EACH (Entry, DataOutPduList) {
    Pdu     = NET_LIST_USER_STRUCT (Entry, NET_BUF, List);

    Status  = Tcp4IoTransmit (&Tcb->Conn->Tcp4Io, Pdu);
    if (EFI_ERROR (Status)) {
      break;
    }
  }

  IScsiFreeNbufList (DataOutPduList);

  return Status;
}

EFI_STATUS
IScsiOnDataInRcvd (
  IN NET_BUF                                         *Pdu,
  IN ISCSI_TCB                                       *Tcb,
  IN OUT EFI_EXT_SCSI_PASS_THRU_SCSI_REQUEST_PACKET  *Packet
  )
/*++

Routine Description:

  Process the received iSCSI SCSI Data In PDU.

Arguments:

  Pdu    - The Data In PDU received.
  Tcb    - The task control block.
  Packet - The EXT SCSI PASS THRU request packet.

Returns:

  EFI_SUCCES          - The check on the Data IN PDU is passed and some update
                        actions are taken.
  EFI_PROTOCOL_ERROR  - Some kind of iSCSI protocol errror happened.

--*/
{
  ISCSI_SCSI_DATA_IN  *DataInHdr;
  EFI_STATUS          Status;

  DataInHdr                   = (ISCSI_SCSI_DATA_IN *) NetbufGetByte (Pdu, 0, NULL);

  DataInHdr->InitiatorTaskTag = NTOHL (DataInHdr->InitiatorTaskTag);
  DataInHdr->ExpCmdSN         = NTOHL (DataInHdr->ExpCmdSN);
  DataInHdr->MaxCmdSN         = NTOHL (DataInHdr->MaxCmdSN);
  DataInHdr->DataSN           = NTOHL (DataInHdr->DataSN);

  //
  // Check the DataSN.
  //
  Status = IScsiCheckSN (&Tcb->ExpDataSN, DataInHdr->DataSN);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  if (DataInHdr->InitiatorTaskTag != Tcb->InitiatorTaskTag) {
    return EFI_PROTOCOL_ERROR;
  }
  //
  // Update the command related sequence numbers.
  //
  IScsiUpdateCmdSN (Tcb->Conn->Session, DataInHdr->MaxCmdSN, DataInHdr->ExpCmdSN);

  if (ISCSI_FLAG_ON (DataInHdr, SCSI_DATA_IN_PDU_FLAG_STATUS_VALID)) {
    if (!ISCSI_FLAG_ON (DataInHdr, ISCSI_BHS_FLAG_FINAL)) {
      //
      // The S bit is on but the F bit is off.
      //
      return EFI_PROTOCOL_ERROR;
    }

    Tcb->StatusXferd = TRUE;

    if (ISCSI_FLAG_ON (DataInHdr, SCSI_DATA_IN_PDU_FLAG_OVERFLOW | SCSI_DATA_IN_PDU_FLAG_UNDERFLOW)) {
      //
      // Underflow and Overflow are mutual flags.
      //
      return EFI_PROTOCOL_ERROR;
    }
    //
    // S bit is on, the StatSN is valid.
    //
    Status = IScsiCheckSN (&Tcb->Conn->ExpStatSN, NTOHL (DataInHdr->StatSN));
    if (EFI_ERROR (Status)) {
      return Status;
    }

    Packet->HostAdapterStatus = 0;
    Packet->TargetStatus      = DataInHdr->Status;

    if (ISCSI_FLAG_ON (DataInHdr, SCSI_RSP_PDU_FLAG_OVERFLOW)) {
      Packet->InTransferLength += NTOHL (DataInHdr->ResidualCount);
      Status = EFI_BAD_BUFFER_SIZE;
    }

    if (ISCSI_FLAG_ON (DataInHdr, SCSI_RSP_PDU_FLAG_UNDERFLOW)) {
      Packet->InTransferLength -= NTOHL (DataInHdr->ResidualCount);
    }
  }

  return Status;
}

EFI_STATUS
IScsiOnR2TRcvd (
  IN NET_BUF                                         *Pdu,
  IN ISCSI_TCB                                       *Tcb,
  IN UINT64                                          Lun,
  IN OUT EFI_EXT_SCSI_PASS_THRU_SCSI_REQUEST_PACKET  *Packet
  )
/*++

Routine Description:

  Process the received iSCSI R2T PDU.

Arguments:

  Pdu    - The R2T PDU received.
  Tcb    - The task control block.
  Lun    - The Lun.
  Packet - The EXT SCSI PASS THRU request packet.

Returns:

  EFI_SUCCES          - The R2T PDU is valid and the solicited data is sent out.
  EFI_PROTOCOL_ERROR  - Some kind of iSCSI protocol errror happened.

--*/
{
  ISCSI_READY_TO_TRANSFER *R2THdr;
  EFI_STATUS              Status;
  ISCSI_XFER_CONTEXT      *XferContext;
  UINT8                   *Data;

  R2THdr = (ISCSI_READY_TO_TRANSFER *) NetbufGetByte (Pdu, 0, NULL);

  R2THdr->InitiatorTaskTag = NTOHL (R2THdr->InitiatorTaskTag);
  R2THdr->TargetTransferTag = NTOHL (R2THdr->TargetTransferTag);
  R2THdr->StatSN = NTOHL (R2THdr->StatSN);
  R2THdr->R2TSN = NTOHL (R2THdr->R2TSN);
  R2THdr->BufferOffset = NTOHL (R2THdr->BufferOffset);
  R2THdr->DesiredDataTransferLength = NTOHL (R2THdr->DesiredDataTransferLength);

  if ((R2THdr->InitiatorTaskTag != Tcb->InitiatorTaskTag) || !ISCSI_SEQ_EQ (R2THdr->StatSN, Tcb->Conn->ExpStatSN)) {
    return EFI_PROTOCOL_ERROR;;
  }
  //
  // Check the sequence number.
  //
  Status = IScsiCheckSN (&Tcb->ExpDataSN, R2THdr->R2TSN);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  XferContext                     = &Tcb->XferContext;
  XferContext->TargetTransferTag  = R2THdr->TargetTransferTag;
  XferContext->Offset             = R2THdr->BufferOffset;
  XferContext->DesiredLength      = R2THdr->DesiredDataTransferLength;

  if (((XferContext->Offset + XferContext->DesiredLength) > Packet->OutTransferLength) ||
      (XferContext->DesiredLength > Tcb->Conn->Session->MaxBurstLength)
      ) {
    return EFI_PROTOCOL_ERROR;
  }
  //
  // Send the data solicited by this R2T.
  //
  Data    = (UINT8 *) Packet->OutDataBuffer + XferContext->Offset;
  Status  = IScsiSendDataOutPduSequence (Data, Lun, Tcb);

  return Status;
}

EFI_STATUS
IScsiOnScsiRspRcvd (
  IN NET_BUF                                         *Pdu,
  IN ISCSI_TCB                                       *Tcb,
  IN OUT EFI_EXT_SCSI_PASS_THRU_SCSI_REQUEST_PACKET  *Packet
  )
/*++

Routine Description:

  Process the received iSCSI SCSI Response PDU.

Arguments:

  Pdu    - The Response PDU received.
  Tcb    - The task control block.
  Packet - The EXT SCSI PASS THRU request packet.

Returns:

  EFI_SUCCES         - The Response PDU is processed.
  EFI_PROTOCOL_ERROR - Some kind of iSCSI protocol errror happened.

--*/
{
  SCSI_RESPONSE     *ScsiRspHdr;
  ISCSI_SENSE_DATA  *SenseData;
  EFI_STATUS        Status;
  UINT32            DataSegLen;

  ScsiRspHdr                    = (SCSI_RESPONSE *) NetbufGetByte (Pdu, 0, NULL);

  ScsiRspHdr->InitiatorTaskTag  = NTOHL (ScsiRspHdr->InitiatorTaskTag);
  if (ScsiRspHdr->InitiatorTaskTag != Tcb->InitiatorTaskTag) {
    return EFI_PROTOCOL_ERROR;
  }

  ScsiRspHdr->StatSN  = NTOHL (ScsiRspHdr->StatSN);

  Status              = IScsiCheckSN (&Tcb->Conn->ExpStatSN, ScsiRspHdr->StatSN);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  ScsiRspHdr->MaxCmdSN  = NTOHL (ScsiRspHdr->MaxCmdSN);
  ScsiRspHdr->ExpCmdSN  = NTOHL (ScsiRspHdr->ExpCmdSN);
  IScsiUpdateCmdSN (Tcb->Conn->Session, ScsiRspHdr->MaxCmdSN, ScsiRspHdr->ExpCmdSN);

  Tcb->StatusXferd          = TRUE;

  Packet->HostAdapterStatus = ScsiRspHdr->Response;
  if (Packet->HostAdapterStatus != ISCSI_SERVICE_RSP_COMMAND_COMPLETE_AT_TARGET) {
    return EFI_SUCCESS;
  }

  Packet->TargetStatus = ScsiRspHdr->Status;

  if (ISCSI_FLAG_ON (ScsiRspHdr, SCSI_RSP_PDU_FLAG_BI_READ_OVERFLOW | SCSI_RSP_PDU_FLAG_BI_READ_UNDERFLOW) ||
      ISCSI_FLAG_ON (ScsiRspHdr, SCSI_RSP_PDU_FLAG_OVERFLOW | SCSI_RSP_PDU_FLAG_UNDERFLOW)
        ) {
    return EFI_PROTOCOL_ERROR;
  }

  if (ISCSI_FLAG_ON (ScsiRspHdr, SCSI_RSP_PDU_FLAG_BI_READ_OVERFLOW)) {
    Packet->InTransferLength += NTOHL (ScsiRspHdr->BiReadResidualCount);
    Status = EFI_BAD_BUFFER_SIZE;
  }

  if (ISCSI_FLAG_ON (ScsiRspHdr, SCSI_RSP_PDU_FLAG_BI_READ_UNDERFLOW)) {
    Packet->InTransferLength -= NTOHL (ScsiRspHdr->BiReadResidualCount);
  }

  if (ISCSI_FLAG_ON (ScsiRspHdr, SCSI_RSP_PDU_FLAG_OVERFLOW)) {
    if (Packet->DataDirection == DataIn) {
      Packet->InTransferLength += NTOHL (ScsiRspHdr->ResidualCount);
    } else {
      Packet->OutTransferLength += NTOHL (ScsiRspHdr->ResidualCount);
    }

    Status = EFI_BAD_BUFFER_SIZE;
  }

  if (ISCSI_FLAG_ON (ScsiRspHdr, SCSI_RSP_PDU_FLAG_UNDERFLOW)) {
    if (Packet->DataDirection == DataIn) {
      Packet->InTransferLength -= NTOHL (ScsiRspHdr->ResidualCount);
    } else {
      Packet->OutTransferLength -= NTOHL (ScsiRspHdr->ResidualCount);
    }
  }

  DataSegLen = ISCSI_GET_DATASEG_LEN (ScsiRspHdr);
  if (DataSegLen != 0) {
    SenseData               = (ISCSI_SENSE_DATA *) NetbufGetByte (Pdu, sizeof (SCSI_RESPONSE), NULL);

    SenseData->Length       = NTOHS (SenseData->Length);

    Packet->SenseDataLength = (UINT8) NET_MIN (SenseData->Length, Packet->SenseDataLength);
    if (Packet->SenseDataLength != 0) {
      NetCopyMem (Packet->SenseData, &SenseData->Data[0], Packet->SenseDataLength);
    }
  } else {
    Packet->SenseDataLength = 0;
  }

  return Status;
}

EFI_STATUS
IScsiOnNopInRcvd (
  IN NET_BUF    *Pdu,
  IN ISCSI_TCB  *Tcb
  )
/*++

Routine Description:

  Process the received NOP In PDU.

Arguments:

  Pdu    - The NOP In PDU received.
  Tcb    - The task control block.

Returns:

  EFI_SUCCES          - The NOP In PDU is processed and the related sequence
                        numbers are updated.
  EFI_PROTOCOL_ERROR  - Some kind of iSCSI protocol errror happened.

--*/
{
  ISCSI_NOP_IN  *NopInHdr;
  EFI_STATUS    Status;

  NopInHdr            = (ISCSI_NOP_IN *) NetbufGetByte (Pdu, 0, NULL);

  NopInHdr->StatSN    = NTOHL (NopInHdr->StatSN);
  NopInHdr->ExpCmdSN  = NTOHL (NopInHdr->ExpCmdSN);
  NopInHdr->MaxCmdSN  = NTOHL (NopInHdr->MaxCmdSN);

  if (NopInHdr->InitiatorTaskTag == ISCSI_RESERVED_TAG) {
    if (NopInHdr->StatSN != Tcb->Conn->ExpStatSN) {
      return EFI_PROTOCOL_ERROR;
    }
  } else {
    Status = IScsiCheckSN (&Tcb->Conn->ExpStatSN, NopInHdr->StatSN);
    if (EFI_ERROR (Status)) {
      return Status;
    }
  }

  IScsiUpdateCmdSN (Tcb->Conn->Session, NopInHdr->MaxCmdSN, NopInHdr->ExpCmdSN);

  return EFI_SUCCESS;
}

EFI_STATUS
IScsiExecuteScsiCommand (
  IN EFI_EXT_SCSI_PASS_THRU_PROTOCOL                 *PassThru,
  IN UINT8                                           *Target,
  IN UINT64                                          Lun,
  IN OUT EFI_EXT_SCSI_PASS_THRU_SCSI_REQUEST_PACKET  *Packet
  )
/*++

Routine Description:

  Execute the SCSI command issued through the EXT SCSI PASS THRU protocol.

Arguments:

  PassThru - The EXT SCSI PASS THRU protocol.
  Target   - The target ID.
  Lun      - The LUN.
  Packet   - The request packet containing IO request, SCSI command buffer and
             buffers to read/write.

Returns:

  EFI_SUCCES       - The SCSI command is executed and the result is updated to 
                     the Packet.
  EFI_DEVICE_ERROR - Some unexpected error happened.

--*/
{
  EFI_STATUS              Status;
  ISCSI_DRIVER_DATA       *Private;
  ISCSI_SESSION           *Session;
  EFI_EVENT               TimeoutEvent;
  ISCSI_CONNECTION        *Conn;
  ISCSI_TCB               *Tcb;
  NET_BUF                 *Pdu;
  ISCSI_XFER_CONTEXT      *XferContext;
  UINT8                   *Data;
  ISCSI_IN_BUFFER_CONTEXT InBufferContext;
  UINT64                  Timeout;

  Private       = ISCSI_DRIVER_DATA_FROM_EXT_SCSI_PASS_THRU (PassThru);
  Session       = &Private->Session;
  Status        = EFI_SUCCESS;
  Tcb           = NULL;
  TimeoutEvent  = NULL;
  Timeout       = 0;

  if (Session->State != SESSION_STATE_LOGGED_IN) {
    return EFI_DEVICE_ERROR;
  }

  Conn = NET_LIST_USER_STRUCT_S (
          Session->Conns.ForwardLink,
          ISCSI_CONNECTION,
          Link,
          ISCSI_CONNECTION_SIGNATURE
          );

  if (Packet->Timeout != 0) {
    Timeout = MultU64x32 (Packet->Timeout, 2);
  }

  Status = IScsiNewTcb (Conn, &Tcb);
  if (EFI_ERROR (Status)) {
    goto ON_EXIT;
  }
  //
  // Encapsulate the SCSI request packet into an iSCSI SCSI Command PDU.
  //
  Pdu = IScsiNewScsiCmdPdu (Packet, Lun, Tcb);
  if (Pdu == NULL) {
    Status = EFI_OUT_OF_RESOURCES;
    goto ON_EXIT;
  }

  XferContext         = &Tcb->XferContext;
  XferContext->Offset = ISCSI_GET_DATASEG_LEN (NetbufGetByte (Pdu, 0, NULL));

  //
  // Transmit the SCSI Command PDU.
  //
  Status = Tcp4IoTransmit (&Conn->Tcp4Io, Pdu);

  NetbufFree (Pdu);

  if (EFI_ERROR (Status)) {
    goto ON_EXIT;
  }

  if (!Session->InitialR2T &&
      (XferContext->Offset < Session->FirstBurstLength) &&
      (XferContext->Offset < Packet->OutTransferLength)
      ) {
    //
    // Unsolicited Data-Out sequence is allowed, there is remaining SCSI
    // OUT data and the limit of FirstBurstLength is not reached.
    //
    XferContext->TargetTransferTag = ISCSI_RESERVED_TAG;
    XferContext->DesiredLength = NET_MIN (
                                  Session->FirstBurstLength,
                                  Packet->OutTransferLength - XferContext->Offset
                                  );

    Data    = (UINT8 *) Packet->OutDataBuffer + XferContext->Offset;
    Status  = IScsiSendDataOutPduSequence (Data, Lun, Tcb);
    if (EFI_ERROR (Status)) {
      goto ON_EXIT;
    }
  }

  InBufferContext.InData    = (UINT8 *) Packet->InDataBuffer;
  InBufferContext.InDataLen = Packet->InTransferLength;

  while (!Tcb->StatusXferd) {
    //
    // Start the timeout timer.
    //
    if (Timeout != 0) {
      Status = gBS->SetTimer (Conn->TimeoutEvent, TimerRelative, Timeout);
      if (EFI_ERROR (Status)) {
        goto ON_EXIT;
      }
      TimeoutEvent = Conn->TimeoutEvent;
    }

   
    //
    // try to receive PDU from target.
    //
    Status = IScsiReceivePdu (Conn, &Pdu, &InBufferContext, FALSE, FALSE, TimeoutEvent);
    if (EFI_ERROR (Status)) {
      goto ON_EXIT;
    }

    switch (ISCSI_GET_OPCODE (NetbufGetByte (Pdu, 0, NULL))) {
    case ISCSI_OPCODE_SCSI_DATA_IN:
      Status = IScsiOnDataInRcvd (Pdu, Tcb, Packet);
      break;

    case ISCSI_OPCODE_R2T:
      Status = IScsiOnR2TRcvd (Pdu, Tcb, Lun, Packet);
      break;

    case ISCSI_OPCODE_SCSI_RSP:
      Status = IScsiOnScsiRspRcvd (Pdu, Tcb, Packet);
      break;

    case ISCSI_OPCODE_NOP_IN:
      Status = IScsiOnNopInRcvd (Pdu, Tcb);
      break;

    case ISCSI_OPCODE_VENDOR_T0:
    case ISCSI_OPCODE_VENDOR_T1:
    case ISCSI_OPCODE_VENDOR_T2:
      //
      // These messages are vendor specific, skip them.
      //
      break;

    default:
      Status = EFI_PROTOCOL_ERROR;
      break;
    }

    NetbufFree (Pdu);

    if (EFI_ERROR (Status)) {
      break;
    }
  }

ON_EXIT:

  if (TimeoutEvent != NULL) {
    gBS->SetTimer (TimeoutEvent, TimerCancel, 0);
  }

  if (Tcb != NULL) {
    IScsiDelTcb (Tcb);
  }

  if ((Status != EFI_SUCCESS) && (Status != EFI_NOT_READY)) {
    //
    // Reinstate the session.
    //
    if (EFI_ERROR (IScsiSessionReinstatement (Private))) {
      Status = EFI_DEVICE_ERROR;
    }
  }

  return Status;
}

EFI_STATUS
IScsiSessionReinstatement (
  IN ISCSI_DRIVER_DATA  *Private
  )
/*++

Routine Description:

  Reinstate the session on some error.

Arguments:

  Private - The iSCSI driver data.

Returns:

  EFI_SUCCES - The session is reinstated from some error.
  other      - Reinstatement failed.

--*/
{
  ISCSI_SESSION *Session;
  EFI_STATUS    Status;

  Session = &Private->Session;
  ASSERT (Session->State == SESSION_STATE_LOGGED_IN);

  //
  // Abort the session and re-init it.
  //
  IScsiSessionAbort (Session);
  IScsiSessionInit (Session, TRUE);

  //
  // Login again.
  //
  Status = IScsiSessionLogin (Private);

  return Status;
}

VOID
IScsiSessionInit (
  IN ISCSI_SESSION  *Session,
  IN BOOLEAN        Recovery
  )
/*++

Routine Description:

  Initialize some session parameters before login.

Arguments:

  Session  - The iSCSI session.
  Recovery - Whether the request is from a fresh new start or recovery.

Returns:

  None.

--*/
{
  UINT32  Random;

  if (!Recovery) {
    Session->Signature  = ISCSI_SESSION_SIGNATURE;
    Session->State      = SESSION_STATE_FREE;

    Random              = NET_RANDOM (NetRandomInitSeed ());

    Session->ISID[0]    = ISID_BYTE_0;
    Session->ISID[1]    = ISID_BYTE_1;
    Session->ISID[2]    = ISID_BYTE_2;
    Session->ISID[3]    = ISID_BYTE_3;
    Session->ISID[4]    = (UINT8) Random;
    Session->ISID[5]    = (UINT8) (Random >> 8);

    NetListInit (&Session->Conns);
    NetListInit (&Session->TcbList);
  }

  Session->TSIH                 = 0;

  Session->CmdSN                = 1;
  Session->InitiatorTaskTag     = 1;
  Session->NextCID              = 1;

  Session->TargetPortalGroupTag = 0;
  Session->MaxConnections       = ISCSI_MAX_CONNS_PER_SESSION;
  Session->InitialR2T           = FALSE;
  Session->ImmediateData        = TRUE;
  Session->MaxBurstLength       = 262144;
  Session->FirstBurstLength     = MAX_RECV_DATA_SEG_LEN_IN_FFP;
  Session->DefaultTime2Wait     = 2;
  Session->DefaultTime2Retain   = 20;
  Session->MaxOutstandingR2T    = DEFAULT_MAX_OUTSTANDING_R2T;
  Session->DataPDUInOrder       = TRUE;
  Session->DataSequenceInOrder  = TRUE;
  Session->ErrorRecoveryLevel   = 0;
}

EFI_STATUS
IScsiSessionAbort (
  IN ISCSI_SESSION  *Session
  )
/*++

Routine Description:

  Abort the iSCSI session, that is, reset all the connection and free the
  resources.

Arguments:

  Session - The iSCSI session.

Returns:

  EFI_SUCCES - The session is aborted.

--*/
{
  ISCSI_DRIVER_DATA *Private;
  ISCSI_CONNECTION  *Conn;

  if (Session->State != SESSION_STATE_LOGGED_IN) {
    return EFI_SUCCESS;
  }

  ASSERT (!NetListIsEmpty (&Session->Conns));

  Private = ISCSI_DRIVER_DATA_FROM_SESSION (Session);

  while (!NetListIsEmpty (&Session->Conns)) {
    Conn = NET_LIST_USER_STRUCT_S (
            Session->Conns.ForwardLink,
            ISCSI_CONNECTION,
            Link,
            ISCSI_CONNECTION_SIGNATURE
            );

    gBS->CloseProtocol (
          Conn->Tcp4Io.Handle,
          &gEfiTcp4ProtocolGuid,
          Private->Image,
          Private->ExtScsiPassThruHandle
          );

    IScsiConnReset (Conn);

    IScsiDetatchConnection (Conn);
    IScsiDestroyConnection (Conn);
  }

  Session->State = SESSION_STATE_FAILED;

  return EFI_SUCCESS;
}
