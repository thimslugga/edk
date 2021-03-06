/*++

Copyright (c) 2006 - 2007, Intel Corporation                                                         
All rights reserved. This program and the accompanying materials                          
are licensed and made available under the terms and conditions of the BSD License         
which accompanies this distribution.  The full text of the license may be found at        
http://opensource.org/licenses/bsd-license.php                                            
                                                                                          
THE PROGRAM IS DISTRIBUTED UNDER THE BSD LICENSE ON AN "AS IS" BASIS,                     
WITHOUT WARRANTIES OR REPRESENTATIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED. 

Module Name:

  Ip4Config.h

Abstract:

  Header file for IP4Config driver.
  
--*/

#ifndef __EFI_IP4CONFIG_H__
#define __EFI_IP4CONFIG_H__

#include "Tiano.h"
#include "EfiDriverLib.h"
#include "NetLib.h"
#include "EfiCommonLib.h"
#include "EfiPrintLib.h"

#include EFI_PROTOCOL_CONSUMER (Dhcp4)
#include EFI_PROTOCOL_CONSUMER (NicIp4Config)
#include EFI_PROTOCOL_DEFINITION (Ip4Config)

#include "NicIp4Variable.h"

typedef struct _IP4_CONFIG_INSTANCE IP4_CONFIG_INSTANCE;

enum {
  IP4_CONFIG_STATE_IDLE         = 0,
  IP4_CONFIG_STATE_STARTED,
  IP4_CONFIG_STATE_CONFIGURED,

  IP4_PROTO_ICMP                = 0x01,
  IP4_CONFIG_INSTANCE_SIGNATURE = EFI_SIGNATURE_32 ('I', 'P', '4', 'C'),

  DHCP_TAG_PARA_LIST            = 55,
  DHCP_TAG_NETMASK              = 1,
  DHCP_TAG_ROUTER               = 3,
};

//
// Configure the DHCP to request the routers and netmask
// from server. The DHCP_TAG_NETMASK is included in Head.
//
#pragma pack(1)
typedef struct {
  EFI_DHCP4_PACKET_OPTION Head;
  UINT8                   Route;
} IP4_CONFIG_DHCP4_OPTION;
#pragma pack()

typedef struct _IP4_CONFIG_INSTANCE {
  UINT32                        Signature;
  EFI_HANDLE                    Controller;
  EFI_HANDLE                    Image;

  EFI_IP4_CONFIG_PROTOCOL       Ip4ConfigProtocol;
  EFI_NIC_IP4_CONFIG_PROTOCOL   NicIp4Protocol;

  //
  // NicConfig's state, such as IP4_CONFIG_STATE_IDLE
  //
  INTN                          State;

  //
  // Mnp child to keep the connection with MNP.
  //
  EFI_MANAGED_NETWORK_PROTOCOL  *Mnp;
  EFI_HANDLE                    MnpHandle;

  //
  // User's requests data
  //
  EFI_EVENT                     DoneEvent;
  EFI_EVENT                     ReconfigEvent;
  EFI_STATUS                    Result;

  //
  // Identity of this interface and some configuration info.
  //
  NIC_ADDR                      NicAddr;
  UINT16                        NicName[IP4_NIC_NAME_LENGTH];
  UINT32                        NicIndex;
  NIC_IP4_CONFIG_INFO           *NicConfig;

  //
  // DHCP handles to access DHCP
  //
  EFI_DHCP4_PROTOCOL            *Dhcp4;
  EFI_HANDLE                    Dhcp4Handle;
  EFI_EVENT                     Dhcp4Event;
};

#define IP4_CONFIG_INSTANCE_FROM_IP4CONFIG(this) \
  CR (this, IP4_CONFIG_INSTANCE, Ip4ConfigProtocol, IP4_CONFIG_INSTANCE_SIGNATURE)

#define IP4_CONFIG_INSTANCE_FROM_NIC_IP4CONFIG(this) \
  CR (this, IP4_CONFIG_INSTANCE, NicIp4Protocol, IP4_CONFIG_INSTANCE_SIGNATURE)

extern EFI_DRIVER_BINDING_PROTOCOL  gIp4ConfigDriverBinding;
#if (EFI_SPECIFICATION_VERSION >= 0x00020000)
extern EFI_COMPONENT_NAME2_PROTOCOL gIp4ConfigComponentName;
#else
extern EFI_COMPONENT_NAME_PROTOCOL  gIp4ConfigComponentName;
#endif
extern IP4_CONFIG_INSTANCE          *mIp4ConfigNicList[MAX_IP4_CONFIG_IN_VARIABLE];
extern EFI_IP4_CONFIG_PROTOCOL      mIp4ConfigProtocolTemplate;
extern EFI_NIC_IP4_CONFIG_PROTOCOL  mNicIp4ConfigProtocolTemplate;

VOID
Ip4ConfigCleanDhcp4 (
  IN IP4_CONFIG_INSTANCE        *This
  );

VOID
Ip4ConfigCleanConfig (
  IN IP4_CONFIG_INSTANCE        *Instance
  );
#endif
