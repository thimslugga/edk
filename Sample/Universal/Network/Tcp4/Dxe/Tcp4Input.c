/*++

Copyright (c) 2005 - 2007, Intel Corporation                                                         
All rights reserved. This program and the accompanying materials                          
are licensed and made available under the terms and conditions of the BSD License         
which accompanies this distribution.  The full text of the license may be found at        
http://opensource.org/licenses/bsd-license.php                                            
                                                                                          
THE PROGRAM IS DISTRIBUTED UNDER THE BSD LICENSE ON AN "AS IS" BASIS,                     
WITHOUT WARRANTIES OR REPRESENTATIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED. 

Module Name:

  Tcp4Input.c

Abstract:

  TCP input process routines.

--*/

#include "Tcp4Main.h"

INTN
TcpSeqAcceptable (
  IN TCP_CB  *Tcb,
  IN TCP_SEG *Seg
  )
/*++

Routine Description:

  Check whether the sequence number of the incoming segment
  is acceptable.

Arguments:

  Tcb - Pointer to the TCP_CB of this TCP instance.
  Seg - Pointer to the incoming segment.

Returns:

  1 if the sequence number is acceptable, otherwise 0.

--*/
{
  return (TCP_SEQ_LEQ (Tcb->RcvWl2, Seg->End) &&
          TCP_SEQ_LEQ (Seg->Seq, Tcb->RcvWl2 + Tcb->RcvWnd));
}

VOID
TcpFastRecover (
  IN TCP_CB  *Tcb,
  IN TCP_SEG *Seg
  )
/*++

Routine Description:

  NewReno fast recovery, RFC3782.

Arguments:

  Tcb - Pointer to the TCP_CB of this TCP instance.
  Seg - Segment that triggers the fast recovery.

Returns:

  None.

--*/
{
  UINT32  FlightSize;
  UINT32  Acked;

  //
  // Step 1: Three duplicate ACKs and not in fast recovery
  //
  if (Tcb->CongestState != TCP_CONGEST_RECOVER) {

    //
    // Step 1A: Invoking fast retransmission.
    //
    FlightSize = TCP_SUB_SEQ (Tcb->SndNxt, Tcb->SndUna);

    Tcb->Ssthresh     = NET_MAX (FlightSize >> 1, (UINT32) (2 * Tcb->SndMss));
    Tcb->Recover      = Tcb->SndNxt;

    Tcb->CongestState = TCP_CONGEST_RECOVER;
    TCP_CLEAR_FLG (Tcb->CtrlFlag, TCP_CTRL_RTT_ON);

    //
    // Step 2: Entering fast retransmission
    //
    TcpRetransmit (Tcb, Tcb->SndUna);
    Tcb->CWnd = Tcb->Ssthresh + 3 * Tcb->SndMss;

    TCP4_DEBUG_TRACE (("TcpFastRecover: enter fast retransmission"
      " for TCB %x, recover point is %d\n", Tcb, Tcb->Recover));
    return;
  }

  //
  // During fast recovery, execute Step 3, 4, 5 of RFC3782
  //
  if (Seg->Ack == Tcb->SndUna) {

    //
    // Step 3: Fast Recovery,
    // If this is a duplicated ACK, increse Cwnd by SMSS.
    //

    // Step 4 is skipped here only to be executed later
    // by TcpToSendData
    //
    Tcb->CWnd += Tcb->SndMss;
    TCP4_DEBUG_TRACE (("TcpFastRecover: received another"
      " duplicated ACK (%d) for TCB %x\n", Seg->Ack, Tcb));

  } else {

    //
    // New data is ACKed, check whether it is a
    // full ACK or partial ACK
    //
    if (TCP_SEQ_GEQ (Seg->Ack, Tcb->Recover)) {

      //
      // Step 5 - Full ACK:
      // deflate the congestion window, and exit fast recovery
      //
      FlightSize = TCP_SUB_SEQ (Tcb->SndNxt, Tcb->SndUna);

      Tcb->CWnd         = NET_MIN (Tcb->Ssthresh, FlightSize + Tcb->SndMss);

      Tcb->CongestState = TCP_CONGEST_OPEN;
      TCP4_DEBUG_TRACE (("TcpFastRecover: received a full ACK(%d)"
        " for TCB %x, exit fast recovery\n", Seg->Ack, Tcb));

    } else {

      //
      // Step 5 - Partial ACK:
      // fast retransmit the first unacknowledge field
      // , then deflate the CWnd
      //
      TcpRetransmit (Tcb, Seg->Ack);
      Acked = TCP_SUB_SEQ (Seg->Ack, Tcb->SndUna);

      //
      // Deflate the CWnd by the amount of new data
      // ACKed by SEG.ACK. If more than one SMSS data
      // is ACKed, add back SMSS byte to CWnd after
      //
      if (Acked >= Tcb->SndMss) {
        Acked -= Tcb->SndMss;

      }

      Tcb->CWnd -= Acked;

      TCP4_DEBUG_TRACE (("TcpFastRecover: received a partial"
        " ACK(%d) for TCB %x\n", Seg->Ack, Tcb));

    }
  }
}

VOID
TcpFastLossRecover (
  IN TCP_CB  *Tcb,
  IN TCP_SEG *Seg
  )
/*++

Routine Description:

  NewReno fast loss recovery, RFC3792.

Arguments:

  Tcb - Pointer to the TCP_CB of this TCP instance.
  Seg - Segment that triggers the fast loss recovery.

Returns:

  None.

--*/
{
  if (TCP_SEQ_GT (Seg->Ack, Tcb->SndUna)) {

    //
    // New data is ACKed, check whether it is a
    // full ACK or partial ACK
    //
    if (TCP_SEQ_GEQ (Seg->Ack, Tcb->LossRecover)) {

      //
      // Full ACK: exit the loss recovery.
      //
      Tcb->LossTimes    = 0;
      Tcb->CongestState = TCP_CONGEST_OPEN;

      TCP4_DEBUG_TRACE (("TcpFastLossRecover: received a "
        "full ACK(%d) for TCB %x\n", Seg->Ack, Tcb));

    } else {

      //
      // Partial ACK:
      // fast retransmit the first unacknowledge field.
      //
      TcpRetransmit (Tcb, Seg->Ack);
      TCP4_DEBUG_TRACE (("TcpFastLossRecover: received a "
        "partial ACK(%d) for TCB %x\n", Seg->Ack, Tcb));
    }
  }
}

VOID
TcpComputeRtt (
  IN TCP_CB *Tcb,
  IN UINT32 Measure
  )
/*++

Routine Description:

  Compute the RTT as specified in RFC2988

Arguments:

  Tcb     - Pointer to the TCP_CB of this TCP instance.
  Measure - Currently measured RTT in heart beats.

Returns:

  None.

--*/
{
  INT32 Var;

  //
  // Step 2.3: Compute the RTO for subsequent RTT measurement. 
  //
  if (Tcb->SRtt != 0) {

    Var = Tcb->SRtt - (Measure << TCP_RTT_SHIFT);

    if (Var < 0) {
      Var = -Var;
    }

    Tcb->RttVar = (3 * Tcb->RttVar + Var) >> 2;
    Tcb->SRtt   = 7 * (Tcb->SRtt >> 3) + Measure;

  } else {
    //
    // Step 2.2: compute the first RTT measure
    //
    Tcb->SRtt   = Measure << TCP_RTT_SHIFT;
    Tcb->RttVar = Measure << (TCP_RTT_SHIFT - 1);
  }

  Tcb->Rto = (Tcb->SRtt + NET_MAX (8, 4 * Tcb->RttVar)) >> TCP_RTT_SHIFT;

  //
  // Step 2.4: Limit the RTO to at least 1 second
  // Step 2.5: Limit the RTO to a maxium value that
  // is at least 60 second
  //
  if (Tcb->Rto < TCP_RTO_MIN) {
    Tcb->Rto = TCP_RTO_MIN;

  } else if (Tcb->Rto > TCP_RTO_MAX) {
    Tcb->Rto = TCP_RTO_MAX;

  }

  TCP4_DEBUG_TRACE (("TcpComputeRtt: new RTT for TCB %x"
    " computed SRTT: %d RTTVAR: %d RTO: %d\n",
    Tcb, Tcb->SRtt, Tcb->RttVar, Tcb->Rto));

}

STATIC
INTN
TcpTrimSegment (
  IN NET_BUF   *Nbuf,
  IN TCP_SEQNO Left,
  IN TCP_SEQNO Right
  )
/*++

Routine Description:

  Trim the data, SYN and FIN to fit into the window defined by
  Left and Right.

Arguments:

  Nbuf  - Buffer that contains received TCP segment without IP header.
  Left  - The sequence number of the window's left edge.
  Right - The sequence number of the window's right edge.

Returns:

  0, the data is successfully trimmed.

--*/
{
  TCP_SEG   *Seg;
  TCP_SEQNO Urg;
  UINT32    Drop;

  Seg = TCPSEG_NETBUF (Nbuf);

  //
  // If the segment is completely out of window,
  // truncate every thing, include SYN and FIN.
  //
  if (TCP_SEQ_LEQ (Seg->End, Left) || TCP_SEQ_LEQ (Right, Seg->Seq)) {

    TCP_CLEAR_FLG (Seg->Flag, TCP_FLG_SYN);
    TCP_CLEAR_FLG (Seg->Flag, TCP_FLG_FIN);

    Seg->Seq = Seg->End;
    NetbufTrim (Nbuf, Nbuf->TotalSize, NET_BUF_HEAD);
    return 0;
  }

  //
  // Adjust the buffer header
  //
  if (TCP_SEQ_LT (Seg->Seq, Left)) {

    Drop      = TCP_SUB_SEQ (Left, Seg->Seq);
    Urg       = Seg->Seq + Seg->Urg;
    Seg->Seq  = Left;

    if (TCP_FLG_ON (Seg->Flag, TCP_FLG_SYN)) {
      TCP_CLEAR_FLG (Seg->Flag, TCP_FLG_SYN);
      Drop--;
    }

    //
    // Adjust the urgent point
    //
    if (TCP_FLG_ON (Seg->Flag, TCP_FLG_URG)) {

      if (TCP_SEQ_LT (Urg, Seg->Seq)) {

        TCP_CLEAR_FLG (Seg->Flag, TCP_FLG_URG);
      } else {
        Seg->Urg = (UINT16) TCP_SUB_SEQ (Urg, Seg->Seq);
      }
    }

    if (Drop) {
      NetbufTrim (Nbuf, Drop, NET_BUF_HEAD);
    }
  }

  //
  // Adjust the buffer tail
  //
  if (TCP_SEQ_GT (Seg->End, Right)) {

    Drop      = TCP_SUB_SEQ (Seg->End, Right);
    Seg->End  = Right;

    if (TCP_FLG_ON (Seg->Flag, TCP_FLG_FIN)) {
      TCP_CLEAR_FLG (Seg->Flag, TCP_FLG_FIN);
      Drop--;
    }

    if (Drop) {
      NetbufTrim (Nbuf, Drop, NET_BUF_TAIL);
    }
  }

  ASSERT (TcpVerifySegment (Nbuf));
  return 0;
}

INTN
TcpTrimInWnd (
  IN TCP_CB  *Tcb,
  IN NET_BUF *Nbuf
  )
/*++

Routine Description:

  Trim off the data outside the tcb's receive window.

Arguments:

  Tcb  - Pointer to the TCP_CB of this TCP instance.
  Nbuf - Pointer to the NET_BUF containing the received tcp segment.

Returns:

  0, the data is trimmed.

--*/
{
  return TcpTrimSegment (Nbuf, Tcb->RcvNxt, Tcb->RcvWl2 + Tcb->RcvWnd);
}

INTN
TcpDeliverData (
  IN TCP_CB *Tcb
  )
/*++

Routine Description:

  Process the data and FIN flag, check whether to deliver
  data to the socket layer.

Arguments:

  Tcb - Pointer to the TCP_CB of this TCP instance.

Returns:

  0   - No error occurred to deliver data.
  -1  - Error condition occurred. Proper response is to reset
        the connection.

--*/
{
  NET_LIST_ENTRY  *Entry;
  NET_BUF         *Nbuf;
  TCP_SEQNO       Seq;
  TCP_SEG         *Seg;
  UINT32          Urgent;

  ASSERT (Tcb && Tcb->Sk);

  //
  // make sure there is some data queued,
  // and TCP is in a proper state
  //
  if (NetListIsEmpty (&Tcb->RcvQue) || !TCP_CONNECTED (Tcb->State)) {

    return 0;
  }

  //
  // Deliver data to the socket layer
  //
  Entry = Tcb->RcvQue.ForwardLink;
  Seq   = Tcb->RcvNxt;

  while (Entry != &Tcb->RcvQue) {
    Nbuf  = NET_LIST_USER_STRUCT (Entry, NET_BUF, List);
    Seg   = TCPSEG_NETBUF (Nbuf);

    ASSERT (TcpVerifySegment (Nbuf));
    ASSERT (Nbuf->Tcp == NULL);

    if (TCP_SEQ_GT (Seg->Seq, Seq)) {
      break;
    }

    Entry       = Entry->ForwardLink;
    Seq         = Seg->End;
    Tcb->RcvNxt = Seq;

    NetListRemoveEntry (&Nbuf->List);

    //
    // RFC793 Eighth step: process FIN in sequence
    //
    if (TCP_FLG_ON (Seg->Flag, TCP_FLG_FIN)) {

      //
      // The peer sends to us junky data after FIN,
      // reset the connection.
      //
      if (!NetListIsEmpty (&Tcb->RcvQue)) {
        TCP4_DEBUG_ERROR (("TcpDeliverData: data received after"
          " FIN from peer of TCB %x, reset connection\n", Tcb));

        NetbufFree (Nbuf);
        return -1;
      }

      TCP4_DEBUG_TRACE (("TcpDeliverData: processing FIN "
        "from peer of TCB %x\n", Tcb));

      switch (Tcb->State) {
      case TCP_SYN_RCVD:
      case TCP_ESTABLISHED:

        TcpSetState (Tcb, TCP_CLOSE_WAIT);
        break;

      case TCP_FIN_WAIT_1:

        if (!TCP_FLG_ON (Tcb->CtrlFlag, TCP_CTRL_FIN_ACKED)) {

          TcpSetState (Tcb, TCP_CLOSING);
          break;
        }

      //
      // fall through
      //
      case TCP_FIN_WAIT_2:

        TcpSetState (Tcb, TCP_TIME_WAIT);
        TcpClearAllTimer (Tcb);

        if (Tcb->TimeWaitTimeout != 0) {

          TcpSetTimer (Tcb, TCP_TIMER_2MSL, Tcb->TimeWaitTimeout);
        } else {

          TCP4_DEBUG_WARN (("Connection closed immediately "
            "because app disables TIME_WAIT timer for %x\n", Tcb));

          TcpSendAck (Tcb);
          TcpClose (Tcb);
        }
        break;

      case TCP_CLOSE_WAIT:
      case TCP_CLOSING:
      case TCP_LAST_ACK:
      case TCP_TIME_WAIT:
        //
        // The peer sends to us junk FIN byte. Discard 
        // the buffer then reset the connection
        //
        NetbufFree (Nbuf);
        return -1;
        break;
      }

      TCP_SET_FLG (Tcb->CtrlFlag, TCP_CTRL_ACK_NOW);

      Seg->End--;
    }

    //
    // Don't delay the ack if PUSH flag is on.
    //
    if (TCP_FLG_ON (Seg->Flag, TCP_FLG_PSH)) {

      TCP_SET_FLG (Tcb->CtrlFlag, TCP_CTRL_ACK_NOW);
    }

    if (Nbuf->TotalSize) {
      Urgent = 0;

      if (TCP_FLG_ON (Tcb->CtrlFlag, TCP_CTRL_RCVD_URG) &&
          TCP_SEQ_LEQ (Seg->Seq, Tcb->RcvUp)) {

        if (TCP_SEQ_LEQ (Seg->End, Tcb->RcvUp)) {
          Urgent = Nbuf->TotalSize;
        } else {
          Urgent = TCP_SUB_SEQ (Tcb->RcvUp, Seg->Seq) + 1;
        }
      }

      SockDataRcvd (Tcb->Sk, Nbuf, Urgent);
    }

    if (TCP_FIN_RCVD (Tcb->State)) {

      SockNoMoreData (Tcb->Sk);
    }

    NetbufFree (Nbuf);
  }

  return 0;
}

VOID
TcpQueueData (
  IN TCP_CB  *Tcb,
  IN NET_BUF *Nbuf
  )
/*++

Routine Description:

  Store the data into the reassemble queue.

Arguments:

  Tcb   - Pointer to the TCP_CB of this TCP instance.
  Nbuf  - Pointer to the buffer containing the data to be queued.

Returns:

  None.

--*/
{
  TCP_SEG         *Seg;
  NET_LIST_ENTRY  *Head;
  NET_LIST_ENTRY  *Prev;
  NET_LIST_ENTRY  *Cur;
  NET_BUF         *Node;

  ASSERT (Tcb && Nbuf && (Nbuf->Tcp == NULL));

  NET_GET_REF (Nbuf);

  Seg   = TCPSEG_NETBUF (Nbuf);
  Head  = &Tcb->RcvQue;

  //
  // Fast path to process normal case. That is,
  // no out-of-order segments are received.
  //
  if (NetListIsEmpty (Head)) {

    NetListInsertTail (Head, &Nbuf->List);
    return ;
  }

  //
  // Find the point to insert the buffer
  //
  for (Prev = Head, Cur = Head->ForwardLink;
      Cur != Head;
      Prev = Cur, Cur = Cur->ForwardLink) {

    Node = NET_LIST_USER_STRUCT (Cur, NET_BUF, List);

    if (TCP_SEQ_LT (Seg->Seq, TCPSEG_NETBUF (Node)->Seq)) {
      break;
    }
  }

  //
  // Check whether the current segment overlaps with the
  // previous segment.
  //
  if (Prev != Head) {
    Node = NET_LIST_USER_STRUCT (Prev, NET_BUF, List);

    if (TCP_SEQ_LT (Seg->Seq, TCPSEG_NETBUF (Node)->End)) {

      if (TCP_SEQ_LEQ (Seg->End, TCPSEG_NETBUF (Node)->End)) {

        NetbufFree (Nbuf);
        return ;
      }

      TcpTrimSegment (Nbuf, TCPSEG_NETBUF (Node)->End, Seg->End);
    }
  }

  NetListInsertHead (Prev, &Nbuf->List);

  TCP_SET_FLG (Tcb->CtrlFlag, TCP_CTRL_ACK_NOW);

  //
  // Check the segments after the insert point.
  //
  while (Cur != Head) {
    Node = NET_LIST_USER_STRUCT (Cur, NET_BUF, List);

    if (TCP_SEQ_LEQ (TCPSEG_NETBUF (Node)->End, Seg->End)) {

      Cur = Cur->ForwardLink;

      NetListRemoveEntry (&Node->List);
      NetbufFree (Node);
      continue;
    }

    if (TCP_SEQ_LT (TCPSEG_NETBUF (Node)->Seq, Seg->End)) {

      if (TCP_SEQ_LEQ (TCPSEG_NETBUF (Node)->Seq, Seg->Seq)) {

        NetListRemoveEntry (&Nbuf->List);
        NetbufFree (Nbuf);
        return ;
      }

      TcpTrimSegment (Nbuf, Seg->Seq, TCPSEG_NETBUF (Node)->Seq);
      break;
    }

    Cur = Cur->ForwardLink;
  }
}

VOID
TcpAdjustSndQue (
  IN TCP_CB    *Tcb,
  IN TCP_SEQNO Ack
  )
/*++

Routine Description:

  Ajust the send queue or the retransmit queue.

Arguments:

  Tcb - Pointer to the TCP_CB of this TCP instance.
  Ack - The acknowledge seuqence number of the received segment.

Returns:

  None.

--*/
{
  NET_LIST_ENTRY  *Head;
  NET_LIST_ENTRY  *Cur;
  NET_BUF         *Node;
  TCP_SEG         *Seg;

  Head  = &Tcb->SndQue;
  Cur   = Head->ForwardLink;

  while (Cur != Head) {
    Node  = NET_LIST_USER_STRUCT (Cur, NET_BUF, List);
    Seg   = TCPSEG_NETBUF (Node);

    if (TCP_SEQ_GEQ (Seg->Seq, Ack)) {
      break;
    }

    //
    // Remove completely ACKed segments
    //
    if (TCP_SEQ_LEQ (Seg->End, Ack)) {
      Cur = Cur->ForwardLink;

      NetListRemoveEntry (&Node->List);
      NetbufFree (Node);
      continue;
    }

    TcpTrimSegment (Node, Ack, Seg->End);
    break;
  }
}

INTN
TcpInput (
  IN NET_BUF *Nbuf,
  IN UINT32  Src,
  IN UINT32  Dst
  )
/*++

Routine Description:

  Process the received TCP segments.

Arguments:

  Nbuf  - Buffer that contains received TCP segment without IP header.
  Src   - Source address of the segment, or the peer's IP address.
  Dst   - Destination address of the segment, or the local end's IP address.

Returns:

  0     - Segment is processed successfully. It is either accepted or discarded.
          But no connection is reset by the segment.
  -1    - A connection is reset by the segment.

--*/
{
  TCP_CB      *Tcb;
  TCP_CB      *Parent;
  TCP_OPTION  Option;
  TCP_HEAD    *Head;
  INT32       Len;
  TCP_SEG     *Seg;
  TCP_SEQNO   Right;
  TCP_SEQNO   Urg;

  NET_CHECK_SIGNATURE (Nbuf, NET_BUF_SIGNATURE);

  Parent  = NULL;
  Tcb     = NULL;

  Head    = (TCP_HEAD *) NetbufGetByte (Nbuf, 0, NULL);
  Len     = Nbuf->TotalSize - (Head->HeadLen << 2);

  if ((Head->HeadLen < 5) || (Len < 0) ||
      TcpChecksum (Nbuf, NetPseudoHeadChecksum (Src, Dst, 6, 0))) {

    TCP4_DEBUG_TRACE (("TcpInput: received an mal-formated packet\n"));
    goto DISCARD;
  }

  if (TCP_FLG_ON (Head->Flag, TCP_FLG_SYN)) {
    Len++;
  }

  if (TCP_FLG_ON (Head->Flag, TCP_FLG_FIN)) {
    Len++;
  }

  Tcb = TcpLocateTcb (
          Head->DstPort,
          Dst,
          Head->SrcPort,
          Src,
          (BOOLEAN) TCP_FLG_ON (Head->Flag, TCP_FLG_SYN)
          );

  if ((Tcb == NULL) || (Tcb->State == TCP_CLOSED)) {
    TCP4_DEBUG_TRACE (("TcpInput: send reset because no TCB find\n"));

    Tcb = NULL;
    goto SEND_RESET;
  }

  Seg = TcpFormatNetbuf (Tcb, Nbuf);

  //
  // RFC1122 recommended reaction to illegal option
  // (in fact, an illegal option length) is reset.
  //
  if (TcpParseOption (Nbuf->Tcp, &Option) == -1) {
    TCP4_DEBUG_ERROR (("TcpInput: reset the peer because"
      " of mal-format option for Tcb %x\n", Tcb));

    goto SEND_RESET;
  }

  //
  // From now on, the segment is headless
  //
  NetbufTrim (Nbuf, (Head->HeadLen << 2), NET_BUF_HEAD);
  Nbuf->Tcp = NULL;

  //
  // TODO: add fast path process here
  //

  //
  // Process the segment in LISTEN state.
  //
  if (Tcb->State == TCP_LISTEN) {
    //
    // First step: Check RST
    //
    if (TCP_FLG_ON (Seg->Flag, TCP_FLG_RST)) {
      TCP4_DEBUG_WARN (("TcpInput: discard a reset segment "
        "for TCB %x in listening\n", Tcb));

      goto DISCARD;
    }

    //
    // Second step: Check ACK.
    // Any ACK sent to TCP in LISTEN is reseted.
    //
    if (TCP_FLG_ON (Seg->Flag, TCP_FLG_ACK)) {
      TCP4_DEBUG_WARN (("TcpInput: send reset because of"
        " segment with ACK for TCB %x in listening\n", Tcb));

      goto SEND_RESET;
    }

    //
    // Third step: Check SYN
    //
    if (TCP_FLG_ON (Seg->Flag, TCP_FLG_SYN)) {
      //
      // create a child TCB to handle the data
      //
      Parent  = Tcb;

      Tcb     = TcpCloneTcb (Parent);
      if (Tcb == NULL) {
        TCP4_DEBUG_ERROR (("TcpInput: discard a segment because"
          "failed to clone a child for TCB%x\n", Tcb));

        goto DISCARD;
      }

      TCP4_DEBUG_TRACE (("TcpInput: create a child for TCB %x"
        " in listening\n", Tcb));

      //
      // init the TCB structure
      //
      Tcb->LocalEnd.Ip    = Dst;
      Tcb->LocalEnd.Port  = Head->DstPort;
      Tcb->RemoteEnd.Ip   = Src;
      Tcb->RemoteEnd.Port = Head->SrcPort;

      TcpInitTcbLocal (Tcb);
      TcpInitTcbPeer (Tcb, Seg, &Option);

      TcpSetState (Tcb, TCP_SYN_RCVD);
      TcpSetTimer (Tcb, TCP_TIMER_CONNECT, Tcb->ConnectTimeout);
      TcpTrimInWnd (Tcb, Nbuf);

      goto StepSix;
    }

    goto DISCARD;

  } else if (Tcb->State == TCP_SYN_SENT) {
    //
    // First step: Check ACK bit
    //
    if (TCP_FLG_ON (Seg->Flag, TCP_FLG_ACK) && (Seg->Ack != Tcb->Iss + 1)) {
 
      TCP4_DEBUG_WARN (("TcpInput: send reset because of "
        "wrong ACK received for TCB %x in SYN_SENT\n", Tcb));

      goto SEND_RESET;
    }

    //
    // Second step: Check RST bit
    //
    if (TCP_FLG_ON (Seg->Flag, TCP_FLG_RST)) {

      if (TCP_FLG_ON (Seg->Flag, TCP_FLG_ACK)) {

        TCP4_DEBUG_WARN (("TcpInput: connection reset by"
          " peer for TCB%x in SYN_SENT\n", Tcb));

        SOCK_ERROR (Tcb->Sk, EFI_CONNECTION_RESET);
        goto DROP_CONNECTION;
      } else {

        TCP4_DEBUG_WARN (("TcpInput: discard a reset segment "
          "because of no ACK for TCB%x in SYN_SENT\n", Tcb));

        goto DISCARD;
      }
    }

    //
    // Third step: Check security and precedence. Skipped
    //

    //
    // Fourth step: Check SYN. Pay attention to sitimulatous open
    //
    if (TCP_FLG_ON (Seg->Flag, TCP_FLG_SYN)) {

      TcpInitTcbPeer (Tcb, Seg, &Option);

      if (TCP_FLG_ON (Seg->Flag, TCP_FLG_ACK)) {

        Tcb->SndUna = Seg->Ack;
      }

      TcpClearTimer (Tcb, TCP_TIMER_REXMIT);

      if (TCP_SEQ_GT (Tcb->SndUna, Tcb->Iss)) {

        TcpSetState (Tcb, TCP_ESTABLISHED);

        TcpClearTimer (Tcb, TCP_TIMER_CONNECT);
        TcpDeliverData (Tcb);

        if ((Tcb->CongestState == TCP_CONGEST_OPEN) && 
            TCP_FLG_ON (Tcb->CtrlFlag, TCP_CTRL_RTT_ON)) {

          TcpComputeRtt (Tcb, Tcb->RttMeasure);
          TCP_CLEAR_FLG (Tcb->CtrlFlag, TCP_CTRL_RTT_ON);
        }

        TcpTrimInWnd (Tcb, Nbuf);

        TCP_SET_FLG (Tcb->CtrlFlag, TCP_CTRL_ACK_NOW);

        TCP4_DEBUG_TRACE (("TcpInput: connection established"
          " for TCB %x in SYN_SENT\n", Tcb));

        goto StepSix;
      } else {
        //
        // Received a SYN segment without ACK, simultanous open. 
        //
        TcpSetState (Tcb, TCP_SYN_RCVD);

        ASSERT (Tcb->SndNxt == Tcb->Iss + 1);
        TcpAdjustSndQue (Tcb, Tcb->SndNxt);

        TcpTrimInWnd (Tcb, Nbuf);

        TCP4_DEBUG_WARN (("TcpInput: simultanous open "
          "for TCB %x in SYN_SENT\n", Tcb));

        goto StepSix;
      }
    }

    goto DISCARD;
  }

  //
  // Process segment in SYN_RCVD or TCP_CONNECTED states
  //

  //
  // First step: Check whether SEG.SEQ is acceptable
  //
  if (!TcpSeqAcceptable (Tcb, Seg)) {
    TCP4_DEBUG_WARN (("TcpInput: sequence acceptance"
      " test failed for segment of TCB %x\n", Tcb));

    if (!TCP_FLG_ON (Seg->Flag, TCP_FLG_RST)) {
      TcpSendAck (Tcb);
    }

    goto DISCARD;
  }

  if ((TCP_SEQ_LT (Seg->Seq, Tcb->RcvWl2)) &&
      (Tcb->RcvWl2 == Seg->End) &&
      !TCP_FLG_ON (Seg->Flag, TCP_FLG_SYN | TCP_FLG_FIN)) {

    TCP_SET_FLG (Tcb->CtrlFlag, TCP_CTRL_ACK_NOW);
  }

  //
  // Second step: Check the RST
  //
  if (TCP_FLG_ON (Seg->Flag, TCP_FLG_RST)) {

    TCP4_DEBUG_WARN (("TcpInput: connection reset for TCB %x\n", Tcb));

    if (Tcb->State == TCP_SYN_RCVD) {

      SOCK_ERROR (Tcb->Sk, EFI_CONNECTION_REFUSED);

      //
      // This TCB comes from either a LISTEN TCB,
      // or active open TCB with simultanous open.
      // Do NOT signal user CONNECTION refused
      // if it comes from a LISTEN TCB.
      //
    } else if ((Tcb->State == TCP_ESTABLISHED) ||
             (Tcb->State == TCP_FIN_WAIT_1) ||
             (Tcb->State == TCP_FIN_WAIT_2) ||
             (Tcb->State == TCP_CLOSE_WAIT)
            ) {

      SOCK_ERROR (Tcb->Sk, EFI_CONNECTION_RESET);

    } else {
      //
      // TODO: set socket error to CLOSED
      //
    }

    goto DROP_CONNECTION;
  }

  //
  // Trim the data and flags.
  //
  TcpTrimInWnd (Tcb, Nbuf);

  //
  // Third step: Check security and precedence, Ignored
  //

  //
  // Fourth step: Check the SYN bit.
  //
  if (TCP_FLG_ON (Seg->Flag, TCP_FLG_SYN)) {

    TCP4_DEBUG_WARN (("TcpInput: connection reset "
      "because received extra SYN for TCB %x\n", Tcb));

    SOCK_ERROR (Tcb->Sk, EFI_CONNECTION_RESET);
    goto RESET_THEN_DROP;
  }

  //
  // Fifth step: Check the ACK
  //
  if (!TCP_FLG_ON (Seg->Flag, TCP_FLG_ACK)) {
    TCP4_DEBUG_WARN (("TcpInput: segment discard because"
      " of no ACK for connected TCB %x\n", Tcb));

    goto DISCARD;

  }

  if (Tcb->State == TCP_SYN_RCVD) {

    if (TCP_SEQ_LT (Tcb->SndUna, Seg->Ack) &&
        TCP_SEQ_LEQ (Seg->Ack, Tcb->SndNxt)) {

      Tcb->SndWnd     = Seg->Wnd;
      Tcb->SndWndMax  = NET_MAX (Tcb->SndWnd, Tcb->SndWndMax);
      Tcb->SndWl1     = Seg->Seq;
      Tcb->SndWl2     = Seg->Ack;
      TcpSetState (Tcb, TCP_ESTABLISHED);

      TcpClearTimer (Tcb, TCP_TIMER_CONNECT);
      TcpDeliverData (Tcb);

      TCP4_DEBUG_TRACE (("TcpInput: connection established "
        " for TCB %x in SYN_RCVD\n", Tcb));

      //
      // Continue the process as ESTABLISHED state
      //
    } else {
      TCP4_DEBUG_WARN (("TcpInput: send reset because of"
        " wrong ACK for TCB %x in SYN_RCVD\n", Tcb));

      goto SEND_RESET;
    }
  }

  if (TCP_SEQ_LT (Seg->Ack, Tcb->SndUna)) {

    TCP4_DEBUG_WARN (("TcpInput: ignore the out-of-data"
      " ACK for connected TCB %x\n", Tcb));

    goto StepSix;

  } else if (TCP_SEQ_GT (Seg->Ack, Tcb->SndNxt)) {

    TCP4_DEBUG_WARN (("TcpInput: discard segment for "
      "future ACK for connected TCB %x\n", Tcb));

    TcpSendAck (Tcb);
    goto DISCARD;
  }

  //
  // From now on: SND.UNA <= SEG.ACK <= SND.NXT.
  //
  if (TCP_FLG_ON (Option.Flag, TCP_OPTION_RCVD_TS)) {
    //
    // update TsRecent as specified in page 16 RFC1323.
    // RcvWl2 equals to the variable "LastAckSent"
    // defined there.
    //
    if (TCP_SEQ_LEQ (Seg->Seq, Tcb->RcvWl2) &&
        TCP_SEQ_LT (Tcb->RcvWl2, Seg->End)) {

      Tcb->TsRecent     = Option.TSVal;
      Tcb->TsRecentAge  = mTcpTick;
    }

    TcpComputeRtt (Tcb, TCP_SUB_TIME (mTcpTick, Option.TSEcr));

  } else if (TCP_FLG_ON (Tcb->CtrlFlag, TCP_CTRL_RTT_ON)) {

    ASSERT (Tcb->CongestState == TCP_CONGEST_OPEN);

    TcpComputeRtt (Tcb, Tcb->RttMeasure);
    TCP_CLEAR_FLG (Tcb->CtrlFlag, TCP_CTRL_RTT_ON);
  }

  if (Seg->Ack == Tcb->SndNxt) {

    TcpClearTimer (Tcb, TCP_TIMER_REXMIT);
  } else {

    TcpSetTimer (Tcb, TCP_TIMER_REXMIT, Tcb->Rto);
  }

  //
  // Count duplicate acks.
  //
  if ((Seg->Ack == Tcb->SndUna) &&
      (Tcb->SndUna != Tcb->SndNxt) &&
      (Seg->Wnd == Tcb->SndWnd) &&
      (0 == Len)) {

    Tcb->DupAck++;
  } else {

    Tcb->DupAck = 0;
  }

  //
  // Congestion avoidance, fast recovery and fast retransmission. 
  //
  if (((Tcb->CongestState == TCP_CONGEST_OPEN) && (Tcb->DupAck < 3)) ||
      (Tcb->CongestState == TCP_CONGEST_LOSS)) {

    if (TCP_SEQ_GT (Seg->Ack, Tcb->SndUna)) {

      if (Tcb->CWnd < Tcb->Ssthresh) {

        Tcb->CWnd += Tcb->SndMss;
      } else {

        Tcb->CWnd += NET_MAX (Tcb->SndMss * Tcb->SndMss / Tcb->CWnd, 1);
      }

      Tcb->CWnd = NET_MIN (Tcb->CWnd, TCP_MAX_WIN << Tcb->SndWndScale);
    }

    if (Tcb->CongestState == TCP_CONGEST_LOSS) {
      TcpFastLossRecover (Tcb, Seg);
    }
  } else {

    TcpFastRecover (Tcb, Seg);
  }

  if (TCP_SEQ_GT (Seg->Ack, Tcb->SndUna)) {

    TcpAdjustSndQue (Tcb, Seg->Ack);
    Tcb->SndUna = Seg->Ack;

    if (TCP_FLG_ON (Tcb->CtrlFlag, TCP_CTRL_SND_URG) &&
        (TCP_SEQ_LT (Tcb->SndUp, Seg->Ack))) {

      TCP_CLEAR_FLG (Tcb->CtrlFlag, TCP_CTRL_SND_URG);
    }
  }

  //
  // Update window info
  //
  if (TCP_SEQ_LT (Tcb->SndWl1, Seg->Seq) || 
      ((Tcb->SndWl1 == Seg->Seq) && TCP_SEQ_LEQ (Tcb->SndWl2, Seg->Ack))) {

    Right = Seg->Ack + Seg->Wnd;

    if (TCP_SEQ_LT (Right, Tcb->SndWl2 + Tcb->SndWnd)) {

      if ((Tcb->SndWl1 == Seg->Seq) &&
          (Tcb->SndWl2 == Seg->Ack) &&
          (Len == 0)) {

        goto NO_UPDATE;
      }

      TCP4_DEBUG_WARN (("TcpInput: peer shrinks the"
        " window  for connected TCB %x\n", Tcb));

      if ((Tcb->CongestState == TCP_CONGEST_RECOVER) &&
          (TCP_SEQ_LT (Right, Tcb->Recover))) {

        Tcb->Recover = Right;
      }

      if ((Tcb->CongestState == TCP_CONGEST_LOSS) &&
          (TCP_SEQ_LT (Right, Tcb->LossRecover))) {

        Tcb->LossRecover = Right;
      }

      if (TCP_SEQ_LT (Right, Tcb->SndNxt)) {

        Tcb->SndNxt = Right;

        if (Right == Tcb->SndUna) {

          TcpClearTimer (Tcb, TCP_TIMER_REXMIT);
          TcpSetProbeTimer (Tcb);
        }
      }
    }

    Tcb->SndWnd     = Seg->Wnd;
    Tcb->SndWndMax  = NET_MAX (Tcb->SndWnd, Tcb->SndWndMax);
    Tcb->SndWl1     = Seg->Seq;
    Tcb->SndWl2     = Seg->Ack;
  }

NO_UPDATE:

  if (TCP_FLG_ON (Tcb->CtrlFlag, TCP_CTRL_FIN_SENT) &&
      (Tcb->SndUna == Tcb->SndNxt)) {

    TCP4_DEBUG_TRACE (("TcpInput: local FIN is ACKed by"
      " peer for connected TCB %x\n", Tcb));

    TCP_SET_FLG (Tcb->CtrlFlag, TCP_CTRL_FIN_ACKED);
  }

  //
  // Transit the state if proper.
  //
  switch (Tcb->State) {
  case TCP_FIN_WAIT_1:

    if (TCP_FLG_ON (Tcb->CtrlFlag, TCP_CTRL_FIN_ACKED)) {

      TcpSetState (Tcb, TCP_FIN_WAIT_2);

      TcpClearAllTimer (Tcb);
      TcpSetTimer (Tcb, TCP_TIMER_FINWAIT2, Tcb->FinWait2Timeout);
    }

  case TCP_FIN_WAIT_2:

    break;

  case TCP_CLOSE_WAIT:
    break;

  case TCP_CLOSING:

    if (TCP_FLG_ON (Tcb->CtrlFlag, TCP_CTRL_FIN_ACKED)) {

      TcpSetState (Tcb, TCP_TIME_WAIT);

      TcpClearAllTimer (Tcb);

      if (Tcb->TimeWaitTimeout != 0) {

        TcpSetTimer (Tcb, TCP_TIMER_2MSL, Tcb->TimeWaitTimeout);
      } else {

        TCP4_DEBUG_WARN (("Connection closed immediately "
          "because app disables TIME_WAIT timer for %x\n", Tcb));

        TcpClose (Tcb);
      }
    }
    break;

  case TCP_LAST_ACK:

    if (TCP_FLG_ON (Tcb->CtrlFlag, TCP_CTRL_FIN_ACKED)) {

      TcpSetState (Tcb, TCP_CLOSED);
    }

    break;

  case TCP_TIME_WAIT:

    TcpSendAck (Tcb);

    if (Tcb->TimeWaitTimeout != 0) {

      TcpSetTimer (Tcb, TCP_TIMER_2MSL, Tcb->TimeWaitTimeout);
    } else {

      TCP4_DEBUG_WARN (("Connection closed immediately "
        "because app disables TIME_WAIT timer for %x\n", Tcb));

      TcpClose (Tcb);
    }
    break;
  }

  //
  // Sixth step: Check the URG bit.update the Urg point
  // if in TCP_CAN_RECV, otherwise, leave the RcvUp intact.
  //
StepSix:

  Tcb->Idle = 0;
  TcpSetKeepaliveTimer (Tcb);

  if (TCP_TIMER_ON (Tcb->EnabledTimer, TCP_TIMER_PROBE)) {

    TcpClearTimer (Tcb, TCP_TIMER_PROBE);
  }

  if (TCP_FLG_ON (Seg->Flag, TCP_FLG_URG) &&
      !TCP_FIN_RCVD (Tcb->State)) {

    TCP4_DEBUG_TRACE (("TcpInput: received urgent data "
      "from peer for connected TCB %x\n", Tcb));

    Urg = Seg->Seq + Seg->Urg;

    if (TCP_FLG_ON (Tcb->CtrlFlag, TCP_CTRL_RCVD_URG) &&
        TCP_SEQ_GT (Urg, Tcb->RcvUp)) {

      Tcb->RcvUp = Urg;
    } else {

      Tcb->RcvUp = Urg;
      TCP_SET_FLG (Tcb->CtrlFlag, TCP_CTRL_RCVD_URG);
    }
  }

  //
  // Seventh step: Process the segment data
  //
  if (Seg->End != Seg->Seq) {

    if (TCP_FIN_RCVD (Tcb->State)) {

      TCP4_DEBUG_WARN (("TcpInput: connection reset because"
        " data is lost for connected TCB %x\n", Tcb));

      goto RESET_THEN_DROP;
    }

    if (TCP_LOCAL_CLOSED (Tcb->State) && (Nbuf->TotalSize != 0)) {
      TCP4_DEBUG_WARN (("TcpInput: connection reset because"
        " data is lost for connected TCB %x\n", Tcb));

      goto RESET_THEN_DROP;
    }

    TcpQueueData (Tcb, Nbuf);
    if (TcpDeliverData (Tcb) == -1) {
      goto RESET_THEN_DROP;
    }

    if (!NetListIsEmpty (&Tcb->RcvQue)) {
      TCP_SET_FLG (Tcb->CtrlFlag, TCP_CTRL_ACK_NOW);
    }
  }

  //
  // Eighth step: check the FIN.
  // This step is moved to TcpDeliverData. FIN will be
  // processed in sequence there. Check the comments in
  // the beginning of the file header for information.
  //

  //
  // Tcb is a new child of the listening Parent,
  // commit it.
  //
  if (Parent) {
    Tcb->Parent = Parent;
    TcpInsertTcb (Tcb);
  }

  if ((Tcb->State != TCP_CLOSED) &&
      (!TcpToSendData (Tcb, 0)) &&
      (TCP_FLG_ON (Tcb->CtrlFlag, TCP_CTRL_ACK_NOW) || Nbuf->TotalSize)) {

    TcpToSendAck (Tcb);
  }

  NetbufFree (Nbuf);
  return 0;

RESET_THEN_DROP:
  TcpSendReset (Tcb, Head, Len, Dst, Src);

DROP_CONNECTION:
  ASSERT (Tcb && Tcb->Sk);

  NetbufFree (Nbuf);
  TcpClose (Tcb);

  return -1;

SEND_RESET:

  TcpSendReset (Tcb, Head, Len, Dst, Src);

DISCARD:

  //
  // Tcb is a child of Parent, and it doesn't survive
  //
  TCP4_DEBUG_WARN (("Tcp4Input: Discard a packet\n"));
  NetbufFree (Nbuf);

  if (Parent && Tcb) {

    ASSERT (Tcb->Sk);
    TcpClose (Tcb);
  }

  return 0;
}

VOID
TcpIcmpInput (
  IN NET_BUF     *Nbuf,
  IN ICMP_ERROR  IcmpErr,
  IN UINT32      Src,
  IN UINT32      Dst
  )
/*++

Routine Description:

  Process the received ICMP error messages for TCP.

Arguments:

  Nbuf    - Buffer that contains part of the TCP segment without IP header
            truncated from the ICMP error packet.
  IcmpErr - The ICMP error code interpreted from ICMP error packet.
  Src     - Source address of the ICMP error message.
  Dst     - Destination address of the ICMP error message.

Returns:

  None.

--*/
{
  TCP_HEAD         *Head;
  TCP_CB           *Tcb;
  TCP_SEQNO        Seq;
  EFI_STATUS       IcmpErrStatus;
  BOOLEAN          IcmpErrIsHard;
  BOOLEAN          IcmpErrNotify;

  Head = (TCP_HEAD *) NetbufGetByte (Nbuf, 0, NULL);
  Tcb = TcpLocateTcb (
          Head->DstPort,
          Dst,
          Head->SrcPort,
          Src,
          FALSE
          );
  if (Tcb == NULL || Tcb->State == TCP_CLOSED) {

    goto CLEAN_EXIT;
  }

  //
  // Validate the sequence number.
  //
  Seq = NTOHL (Head->Seq);
  if (!(TCP_SEQ_LEQ (Tcb->SndUna, Seq) && TCP_SEQ_LT (Seq, Tcb->SndNxt))) {

    goto CLEAN_EXIT;
  }

  IcmpErrStatus = IpIoGetIcmpErrStatus (IcmpErr, &IcmpErrIsHard, &IcmpErrNotify);

  if (IcmpErrNotify) {

    SOCK_ERROR (Tcb->Sk, IcmpErrStatus);
  }

  if (IcmpErrIsHard) {

    TcpClose (Tcb);
  }

CLEAN_EXIT:
  NetbufFree (Nbuf);
}
