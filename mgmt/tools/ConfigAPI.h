/** @file

  A brief file description

  @section license License

  Licensed to the Apache Software Foundation (ASF) under one
  or more contributor license agreements.  See the NOTICE file
  distributed with this work for additional information
  regarding copyright ownership.  The ASF licenses this file
  to you under the Apache License, Version 2.0 (the
  "License"); you may not use this file except in compliance
  with the License.  You may obtain a copy of the License at

      http://www.apache.org/licenses/LICENSE-2.0

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.
 */

#ifndef _CONFIG_API_H
#define _CONFIG_API_H

#include "../utils/XmlUtils.h"

#if defined(solaris)
#include "ink_bool.h"
#endif

class XmlObject
{
  XMLDom xmlDom;
public:
    XmlObject()
  {
  }
  int LoadFile(char *file);
  char *getXmlTagValue(const char *XmlTagName);
  char *getXmlTagValueAndAttribute(char *XmlAttribute, const char *XmlTagName);
};

extern int Config_GetHostname(char *hostname, size_t hostname_len);
extern int Config_SetHostname(char *hostname);
extern int Config_GetDefaultRouter(char *router, size_t len);
extern int Config_SetDefaultRouter(char *router);
extern int Config_GetDomain(char *domain, size_t domain_len);
extern int Config_SetDomain(const char *domain);
extern int Config_GetDNS_Servers(char *dns, size_t dns_len);
extern int Config_SetDNS_Servers(char *dns);
extern int Config_GetDNS_Server(char *server, size_t server_len, int no);
extern int Config_GetNetworkIntCount();
extern int Config_GetNetworkInt(int int_num, char *interface, size_t interface_len);
extern int Config_GetNIC_Status(char *intr, char *status, size_t status_len);
extern int Config_GetNIC_Start(char *intr, char *start, size_t start_len);
extern int Config_GetNIC_Protocol(char *intr, char *protocol, size_t protocol_len);
extern int Config_GetNIC_IP(char *intr, char *ip, size_t ip_len);
extern int Config_GetNIC_Netmask(char *intr, char *netmask, size_t netmask_len);
extern int Config_GetNIC_Gateway(char *intr, char *gateway, size_t gateway_len);
extern int Config_SetNIC_Down(char *interface);
extern int Config_SetNIC_StartOnBoot(char *interface, char *onboot);
extern int Config_SetNIC_BootProtocol(char *interface, char *nic_protocol);
extern int Config_SetNIC_IP(char *interface, char *nic_ip);
extern int Config_SetNIC_Netmask(char *interface, char *nic_netmask);
extern int Config_SetNIC_Gateway(char *interface, char *nic_gateway);
extern int Config_SetNIC_Up(char *interface, char *onboot, char *protocol, char *ip, char *netmask, char *gateway);
extern int Config_GetTime(char *hour, char *minute, char *second);
extern int Config_SetTime(bool restart, char *hour, char *minute, char *second);
extern int Config_GetDate(char *month, char *day, char *year);
extern int Config_SetDate(bool restart, char *month, char *day, char *year);
extern int Config_SortTimezone(void);
extern int Config_GetTimezone(char *timezone, size_t timezone_len);
extern int Config_SetTimezone(bool restart, char *timezone);
extern int Config_GetNTP_Servers(char *server, size_t server_len);
extern int Config_SetNTP_Servers(bool restart, char *server);
extern int Config_GetNTP_Server(char *server, size_t server_len, int no);
extern int Config_GetNTP_Status(char *status, size_t status_len);
extern int Config_SetNTP_Off(void);
extern int Config_SaveNetConfig(char *filename);
extern int Config_SaveVersion(char *file);
extern int Config_RestoreNetConfig(char *filename);
extern int Config_GetXmlTagValue(char *XmlTagName, char **XmlTagValue, char *XmlFile);
extern int Config_SetSMTP_Server(char *server);
extern int Config_GetSMTP_Server(char *server);
extern int Config_FloppyNetRestore();
extern int Config_DisableInterface(char *eth);
#endif // _CONFIG_API_H
