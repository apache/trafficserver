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

#ifndef _SYS_API_H
#define _SYS_API_H

#if defined(solaris)
#include "ink_bool.h"
#endif

#include <sys/types.h>

extern int Net_GetHostname(char *hostname, size_t hostname_len);
extern int Net_SetHostname(char *hostname);
extern int Net_GetDefaultRouter(char *router, size_t len);
extern int Net_SetDefaultRouter(char *router);
extern int Net_GetDomain(char *domain, size_t domain_len);
extern int Net_SetDomain(const char *domain);
extern int Net_GetDNS_Servers(char *dns, size_t dns_len);
extern int Net_SetDNS_Servers(char *dns);
extern int Net_GetDNS_Server(char *server, size_t server_len, int no);
extern int Net_GetNetworkIntCount();
extern int Net_GetNetworkInt(int int_num, char *interface, size_t interface_len);
extern int Net_GetNIC_Status(char *intr, char *status, size_t status_len);
extern int Net_GetNIC_Start(char *intr, char *start, size_t start_len);
extern int Net_GetNIC_Protocol(char *intr, char *protocol, size_t protocol_len);
extern int Net_GetNIC_IP(char *intr, char *nic_ip, size_t nic_ip_len);
extern int Net_GetNIC_Netmask(char *intr, char *netmask, size_t netmask_len);
extern int Net_GetNIC_Gateway(char *intr, char *gateway, size_t gateway_len);
extern int Net_SetNIC_Down(char *interface);
extern int Net_SetNIC_StartOnBoot(char *interface, char *onboot);
extern int Net_SetNIC_BootProtocol(char *interface, char *nic_protocol);
extern int Net_SetNIC_IP(char *interface, char *nic_ip);
extern int Net_SetNIC_Netmask(char *interface, char *nic_netmask);
extern int Net_SetNIC_Gateway(char *interface, char *nic_gateway);
extern int Net_SetNIC_Up(char *interface, char *onboot, char *protocol, char *ip, char *netmask, const char *gateway);
extern int Net_IsValid_Interface(char *interface);
extern int Net_IsValid_Hostname(char *hostname);
extern int Net_IsValid_IP(char *ip);
extern int Time_GetTimezone(char *timezone, size_t timezone_len);
extern int Time_SetTimezone(bool restart, char *timezone);
//extern int Net_SaveNetConfig(char *file);
extern int Time_SortTimezone(void);
extern int Time_GetTime(char *hour, const size_t hourSize, char *minute, const size_t minuteSize, char *second,
                        const size_t secondSize);
extern int Time_SetTime(bool restart, char *hour, char *minute, char *second);
extern int Time_GetDate(char *month, const size_t monthSize, char *day, const size_t daySize, char *year,
                        const size_t yearSize);
extern int Time_SetDate(bool restart, char *month, char *day, char *year);
extern int Time_GetNTP_Servers(char *server, size_t server_len);
extern int Time_SetNTP_Servers(bool restart, char *server);
extern int Time_GetNTP_Server(char *server, size_t server_len, int no);
extern int Time_GetNTP_Status(char *status, size_t status_len);
extern int Time_SetNTP_Off(void);
extern int Net_GetSMTP_Server(char *server);
extern int Net_SetSMTP_Server(char *server);
extern int Net_DisableInterface(char *interface);

#endif // _SYS_API_H
