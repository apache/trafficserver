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

#if defined(linux) || defined(solaris) || defined(freebsd) || defined(darwin) \
 || defined(openbsd)

#include "libts.h"
#include "I_Layout.h"
#include "ConfigAPI.h"
#include "SysAPI.h"
#include "CoreAPI.h"
#include "SimpleTokenizer.h"
#include "XmlUtils.h"
#include "mgmtapi.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <stdarg.h>
#include <string.h>

#define NETCONFIG_HOSTNAME  0
#define NETCONFIG_GATEWAY   1
#define NETCONFIG_DOMAIN    2
#define NETCONFIG_DNS       3
#define NETCONFIG_INTF_UP   4
#define NETCONFIG_INTF_DOWN 5

#define XML_MEMORY_ERROR 	1
#define XML_FILE_ERROR 		3
#define ERROR			-1

#ifdef DEBUG_SYSAPI
#define DPRINTF(x)  printf x
#else
#define DPRINTF(x)
#endif

////////////////////////////////////////////////////////////////
// the following "Config" functions are os independant. They rely on SysAPI to carry out
// OS settings handling and on INKMmgmtAPI to carry out TS seetings. Later on other SysAPIs
// which support other OSs can be added.
//

int
Config_GetHostname(char *hostname, size_t hostname_len)
{
  // TODO: Use #if defined(linux) instead
#if !defined(freebsd) && !defined(darwin) && !defined(solaris)
  return (Net_GetHostname(hostname, hostname_len));
#else
  return -1;
#endif
}

int
Config_SetHostname(char *hostname)
{
  int status = -1;
#if !defined(freebsd) && !defined(darwin) && !defined(solaris)
  char old_hostname[256];

  //printf("Inside Config_SetHostname(), hostname = %s\n", hostname);

  //validate
  ink_strlcpy(old_hostname, "", sizeof(old_hostname));
  if (hostname == NULL)
    return -1;

  //System call first
  status = Net_SetHostname(hostname);
  if (status) {
    return status;
  }
  //MgmtAPI call
  status = TSSetHostname(hostname);
  if (status) {
    // If this fails, we need to restore old machine hostname
    Net_GetHostname(old_hostname, sizeof(old_hostname));
    if (!strlen(old_hostname)) {
      DPRINTF(("Config_SetHostname: FATAL: recovery failed - failed to get old_hostname\n"));
      return -1;
    }
    DPRINTF(("Config_SetHostname: new hostname setup failed - reverting to  old hostname\n"));
    status = Net_SetHostname(old_hostname);
    if (status) {
      DPRINTF(("Config_SetHostname: FATAL: failed reverting to old hostname\n"));
      return status;
    }

    return -1;
  }

#endif /* !freebsd && !darwin */
  return status;
}

int
Config_GetDefaultRouter(char *router, size_t len)
{
#if !defined(freebsd) && !defined(darwin)
  return (Net_GetDefaultRouter(router, len));
#else
  return -1;
#endif
}

int
Config_SetDefaultRouter(char *router)
{
  int status = -1;
#if !defined(freebsd) && !defined(darwin)
  char old_router[80];

  //validate
  if (router == NULL) {
    return -1;
  }
  status = Config_GetDefaultRouter(old_router, sizeof(old_router));
  if (status) {
    DPRINTF(("Config_SetDefaultRouter: Couldn't read old router name\n"));
    ink_strlcpy(old_router, "", sizeof(old_router));
  }

  DPRINTF(("Config_SetDefaultRouter: router %s\n", router));
  status = Net_SetDefaultRouter(router);
  DPRINTF(("Config_SetDefaultRouter: Net_SetDefaultRouter returned %d\n", status));
  if (status) {
    return status;
  }

  status = TSSetGateway(router);
  DPRINTF(("Config_SetDefaultRouter: INKSetGateway returned %d\n", status));
  if (status) {
    if (old_router == NULL) {
      DPRINTF(("Config_SetDefaultRouter: FATAL: Couldn't revert to old router - no old router name%s\n", old_router));
      return -1;
    }
    //try to revert to old router
    status = Net_SetDefaultRouter(old_router);
    if (status) {
      DPRINTF(("Config_SetDefaultRouter: FATAL: Couldn't revert to old router %s\n", old_router));
    }
    return -1;
  }
#endif /* !freebsd && !darwin */
  return status;
}

int
Config_GetDomain(char *domain, size_t domain_len)
{
#if !defined(freebsd) && !defined(darwin) && !defined(solaris)
  return (Net_GetDomain(domain, domain_len));
#else
  return -1;
#endif
}

int
Config_SetDomain(const char *domain)
{
  int status = -1;
#if !defined(freebsd) && !defined(darwin)
  char old_domain[80];

  status = Config_GetDomain(old_domain, sizeof(old_domain));
  if (status) {
    DPRINTF(("Config_SetDomain: Couldn't retrieve old domain\n"));
    ink_strlcpy(old_domain, "", sizeof(old_domain));
  }
  status = Net_SetDomain(domain);
  if (status) {
    return status;
  }
  status = TSSetSearchDomain(domain);
  if (status) {
    //rollback
    if (old_domain == NULL) {
      DPRINTF(("Config_SetDomain: FATAL: no domain to revert to\n"));
      return status;
    }
    status = Net_SetDomain(old_domain);
    if (status) {
      DPRINTF(("Config_SetDomain: FATAL: couldn't revert to old domain\n"));
      return status;
    }
    return -1;
  }
#endif /* !freebsd && !darwin */
  return status;
}

int
Config_GetDNS_Servers(char *dns, size_t dns_len)
{
#if !defined(freebsd) && !defined(darwin) && !defined(solaris)
  return (Net_GetDNS_Servers(dns, dns_len));
#else
  return -1;
#endif
}

int
Config_SetDNS_Servers(char *dns)
{
  int status = -1;
#if !defined(freebsd) && !defined(darwin)
  char old_dns[80];

  DPRINTF(("Config_SetDNS_Servers: dns %s\n", dns));
  status = Config_GetDNS_Servers(old_dns, sizeof(old_dns));
  if (status) {
    DPRINTF(("Config_SetDNS_Servers: falied to retrieve old dns name\n"));
    ink_strlcpy(old_dns, "", sizeof(old_dns));
  }
  status = Net_SetDNS_Servers(dns);
  if (status) {
    return status;
  }
  status = TSSetDNSServers(dns);
  if (status) {
    //if we fail we try to revert to the old dns name??
    if (old_dns == NULL) {
      DPRINTF(("Config_SetDNS_Servers: FATAL: falied to retrieve old dns name\n"));
      return -1;
    }
    status = Net_SetDNS_Servers(old_dns);
    if (status) {
      DPRINTF(("Config_SetDNS_Servers: FATAL: falied to revert to old dns name\n"));
      return status;
    }
    return -1;
  }
#endif /* !freebsd && !darwin */
  return status;
}

int
Config_GetDNS_Server(char *server, size_t server_len, int no)
{
#if !defined(freebsd) && !defined(darwin) && !defined(solaris)
  return (Net_GetDNS_Server(server, server_len, no));
#else
  return -1;
#endif
}

int
Config_GetNetworkIntCount()
{
#if !defined(freebsd) && !defined(darwin)
  return (Net_GetNetworkIntCount());
#else
  return -1;
#endif
}

int
Config_GetNetworkInt(int int_num, char *interface, size_t interface_len)
{
#if !defined(freebsd) && !defined(darwin)
  return (Net_GetNetworkInt(int_num, interface, interface_len));
#else
  return -1;
#endif
}

int
Config_GetNIC_Status(char *interface, char *status, size_t status_len)
{
#if !defined(freebsd) && !defined(darwin)
  return (Net_GetNIC_Status(interface, status, status_len));
#else
  return -1;
#endif
}

int
Config_GetNIC_Start(char *interface, char *start, size_t start_len)
{
#if !defined(freebsd) && !defined(darwin) && !defined(solaris)
  return (Net_GetNIC_Start(interface, start, start_len));
#else
  return -1;
#endif
}

int
Config_GetNIC_Protocol(char *interface, char *protocol, size_t protocol_len)
{
#if !defined(freebsd) && !defined(darwin) && !defined(solaris)
  return (Net_GetNIC_Protocol(interface, protocol, protocol_len));
#else
  return -1;
#endif
}

int
Config_GetNIC_IP(char *interface, char *ip, size_t ip_len)
{
#if !defined(freebsd) && !defined(darwin)
  return (Net_GetNIC_IP(interface, ip, ip_len));
#else
  return -1;
#endif
}

int
Config_GetNIC_Netmask(char *interface, char *netmask, size_t netmask_len)
{
#if !defined(freebsd) && !defined(darwin)
  return (Net_GetNIC_Netmask(interface, netmask, netmask_len));
#else
  return -1;
#endif
}

int
Config_GetNIC_Gateway(char *interface, char *gateway, size_t gateway_len)
{
#if !defined(freebsd) && !defined(darwin) && !defined(solaris)
  return (Net_GetNIC_Gateway(interface, gateway, gateway_len));
#else
  return -1;
#endif
}

int
Config_SetNIC_Down(char *interface)
{
  int status = -1;
#if !defined(freebsd) && !defined(darwin)
  char ip[80];

  //validate
  if (interface == NULL) {
    return -1;
  }
  status = Net_SetNIC_Down(interface);
  if (status) {
    return status;
  }

  Config_GetNIC_IP(interface, ip, sizeof(ip));

  status = TSSetNICDown(interface, ip);
  //do we have anything to roll back to?
  if (status) {
    DPRINTF(("Config_SetNIC_down: falied to config TS for SetNIC_Down\n"));
  }
#endif /* !freebsd && !darwin */
  return status;
}

int
Config_SetNIC_StartOnBoot(char *interface, char *onboot)
{

  //validate
  if ((interface == NULL) || (onboot == NULL)) {
    return -1;
  }
#if !defined(freebsd) && !defined(darwin)
  return (Net_SetNIC_StartOnBoot(interface, onboot));
#else
  return -1;
#endif
}

int
Config_SetNIC_BootProtocol(char *interface, char *nic_protocol)
{
  //validate
  if ((interface == NULL) || (nic_protocol == NULL)) {
    return -1;
  }
#if !defined(freebsd) && !defined(darwin)
  return (Net_SetNIC_BootProtocol(interface, nic_protocol));
#else
  return -1;
#endif
}

int
Config_SetNIC_IP(char *interface, char *nic_ip)
{
  int status = -1;
  //validate
  if ((interface == NULL) || (nic_ip == NULL)) {
    return -1;
  }
#if !defined(freebsd) && !defined(darwin)
  status = (Net_SetNIC_IP(interface, nic_ip));
#endif
  return status;
}

int
Config_SetNIC_Netmask(char *interface, char *nic_netmask)
{
  int status = -1;
  //validate
  if ((interface == NULL) || (nic_netmask == NULL)) {
    return -1;
  }
#if !defined(freebsd) && !defined(darwin)
  status = (Net_SetNIC_Netmask(interface, nic_netmask));
#endif
  return status;
}

int
Config_SetNIC_Gateway(char *interface, char *nic_gateway)
{
  int status = -1;
  //validate
  if ((interface == NULL) || (nic_gateway == NULL)) {
    return -1;
  }

  DPRINTF(("Config_SetNIC_gateway:: interface %s gateway %s\n", interface, nic_gateway));
#if !defined(freebsd) && !defined(darwin)
  status = (Net_SetNIC_Gateway(interface, nic_gateway));
#endif
  return status;
}

int
Config_SetNIC_Up(char *interface, char *onboot, char *protocol, char *ip, char *netmask, char *gateway)
{
  int status = -1;
#if !defined(freebsd) && !defined(darwin)
  char old_ip[80];

  Config_GetNIC_IP(interface, old_ip, sizeof(old_ip));

  if (onboot == NULL || ip == NULL || netmask == NULL) {
    return -1;
  }
  status = Net_SetNIC_Up(interface, onboot, protocol, ip, netmask, gateway);
  if (status) {
    DPRINTF(("Config_SetNIC_Up: Failed to set NIC up\n"));
    return status;
  }

  DPRINTF(("Config_SetNIC_Up: calling INKSetNICUp \n"));
  //Rollback to keep consistent with CLI and snapshot
  status = TSSetNICUp(interface, strcmp(protocol, "dhcp") != 0, ip, old_ip, netmask, strcmp(onboot, "onboot") == 0, gateway);

  if (status) {
    DPRINTF(("Config_SetNIC_Up: INKSetNICUp returned %d\n", status));
    //roll back??
    return status;
  }
#endif /* !freebsd && !darwin */
  return status;
}

int
Config_GetTime(char *hour, const size_t hourSize, char *minute, const size_t minuteSize, char *second,
               const size_t secondSize)
{
  int status = -1;
  status = Time_GetTime(hour, hourSize, minute, minuteSize, second, secondSize);
  return status;
}

int
Config_SetTime(bool restart, char *hour, char *minute, char *second)
{
  int status = -1;

  if (hour == NULL || minute == NULL || second == NULL) {
    return -1;
  }
  status = Time_SetTime(restart, hour, minute, second);
  return status;
}

int
Config_GetDate(char *month, const size_t monthSize, char *day, const size_t daySize, char *year, const size_t yearSize)
{
  int status = -1;
  status = Time_GetDate(month, monthSize, day, daySize, year, yearSize);
  return status;
}

int
Config_SetDate(bool restart, char *month, char *day, char *year)
{
  int status = -1;

  if (month == NULL || day == NULL || year == NULL) {
    return -1;
  }
  status = Time_SetDate(restart, month, day, year);
  return status;
}

int
Config_SortTimezone(void)
{
  int status = -1;
#if !defined(freebsd) && !defined(darwin) && !defined(solaris)
  status = Time_SortTimezone();
#endif
  return status;
}

int
Config_GetTimezone(char *timezone, size_t timezone_len)
{
  int status = -1;
#if !defined(freebsd) && !defined(darwin) && !defined(solaris)
  status = Time_GetTimezone(timezone, timezone_len);
#endif
  return status;
}

int
Config_SetTimezone(bool restart, char *timezone)
{
  int status = -1;

  if (timezone == NULL) {
    return -1;
  }
#if !defined(freebsd) && !defined(darwin)
  status = Time_SetTimezone(restart, timezone);
#endif
  return status;
}

int
Config_GetNTP_Servers(char *server, size_t server_len)
{
  int status = -1;
#if !defined(freebsd) && !defined(darwin) && !defined(solaris)
  status = Time_GetNTP_Servers(server, server_len);
#endif
  return status;
}

int
Config_SetNTP_Servers(bool restart, char *server)
{
  int status = -1;

  if (server == NULL) {
    return -1;
  }
#if !defined(freebsd) && !defined(darwin)
  status = Time_SetNTP_Servers(restart, server);
#endif
  return status;
}

int
Config_GetNTP_Server(char *server, size_t server_len, int no)
{
  int status = -1;
#if !defined(freebsd) && !defined(darwin) && !defined(solaris)
  status = Time_GetNTP_Server(server, server_len, no);
#endif
  return status;
}

int
Config_SaveVersion(char *file)
{
  XMLDom netConfigXML2;

  netConfigXML2.setNodeName("APPLIANCE_CONFIG");

  XMLNode *child2 = new XMLNode;
  char *parentAttributes3[3];
  size_t len;
  len = sizeof("type") + 1;
  parentAttributes3[0] = new char[len];
  snprintf(parentAttributes3[0], len, "%s", "type");
  len = sizeof("Version") + 1;
  parentAttributes3[1] = new char[len];
  snprintf(parentAttributes3[1], len, "%s", "Version");
  parentAttributes3[2] = NULL;
  child2->setAttributes(parentAttributes3);
  child2->setNodeName("CONFIG_TYPE");

  TSString TMVersion = NULL;
  if (TSRecordGetString("proxy.node.version.manager.short", &TMVersion) == TS_ERR_OKAY) {
    XMLNode *VersionString = new XMLNode;
    if (TMVersion != NULL) {
      VersionString->setNodeName("VersionString");
      VersionString->setNodeValue(TMVersion);
      child2->AppendChild(VersionString);
    } else {
      delete VersionString;
    }
  }
  netConfigXML2.AppendChild(child2);
  netConfigXML2.SaveToFile(file);
  return 0;
}

int
Config_GetNTP_Status(char *status, size_t status_len)
{
  return Time_GetNTP_Status(status, status_len);
}

int
Config_SetNTP_Off(void)
{
  return Time_SetNTP_Off();
}

#if defined(linux)
int
Config_DisableInterface(char *eth)
{
  return Net_DisableInterface(eth);
}
#endif

int
Config_RestoreNetConfig(char *file)
{
  // None of these are used it seems.
  //char ethX[6];
  //char ethXIP[24];
  //char ethXNM[24];
  //char ethXGW[24];
  int ret = 0;
  char *TagValue = NULL;
  bool isFloppyConfig = false;

  // TODO: Why is this only used / needed on Linux??
#if defined(linux)
  int activeInterface[] = { 0, 0, 0, 0, 0 };
#endif

  //this is the only way to know whether this is a floppy restore or not

  if (strstr(file, "net_config.xml") != NULL) {
    isFloppyConfig = true;
  }

  int old_euid = getuid();
  if (ret == 0) {
    if (seteuid(0))
      perror("Config_RestoreNetConfig setuid failed: ");
    if (setreuid(0, 0))
      perror("Config_RestoreNetConfig setreuid failed: ");

    XmlObject netXml;
    ret = netXml.LoadFile(file);
    if (ret == XML_FILE_ERROR) {
      printf("File %s error. Check the file path\n", file);
      return ERROR;
    } else if (ret == XML_MEMORY_ERROR) {
      printf("Could not allocate memory for parsing the xml file %s\n", file);
      return ERROR;
    }


    TagValue = netXml.getXmlTagValue("HostName");
    if (TagValue != NULL) {
      Config_SetHostname(TagValue);
      ats_free(TagValue);
    }


    TagValue = netXml.getXmlTagValue("DNSSearch");
    if (TagValue != NULL) {
      Config_SetDomain(TagValue);
      ats_free(TagValue);
    }

    // Check that we always have eth0. If eth0 is missing, exit.
    // Check for all others if eth0 is found
    char eth[5];
    snprintf(eth, sizeof(eth), "eth0");
    int count = 0;
    while (count < 5) {
      TagValue = netXml.getXmlTagValueAndAttribute(eth, "PerNICDefaultGateway");
      if (TagValue != NULL) {
        Config_SetNIC_Gateway(eth, TagValue);
        ats_free(TagValue);
      } else if (count == 0)
        break;
      snprintf(eth, sizeof(eth), "eth%d", ++count);
    }

    // Check that we always have eth0. If eth0 is missing, exit.
    // Check for all others if eth0 is found
    snprintf(eth, sizeof(eth), "eth0");
    count = 0;
    while (count < 5) {
      TagValue = netXml.getXmlTagValueAndAttribute(eth, "InterfaceIPAddress");
      if (TagValue != NULL) {
        Config_SetNIC_IP(eth, TagValue);
#if defined(linux)
        activeInterface[count] = 1;
#endif
        ats_free(TagValue);
      } else if (count == 0)
        break;
      snprintf(eth, sizeof(eth), "eth%d", ++count);
    }

#if defined(linux)
    // Clear all disabled interfaces
    snprintf(eth, sizeof(eth), "eth0");
    count = 0;
    while (count < 5) {
      if (!activeInterface[count]) {
        Config_DisableInterface(eth);
      }
      snprintf(eth, sizeof(eth), "eth%d", ++count);
    }
#endif /* linux */

    // Check that we always have eth0. If eth0 is missing, exit.
    // Check for all others if eth0 is found
    snprintf(eth, sizeof(eth), "eth0");
    count = 0;
    while (count < 5) {
      TagValue = netXml.getXmlTagValueAndAttribute(eth, "InterfaceNetmask");
      if (TagValue != NULL) {
        Config_SetNIC_Netmask(eth, TagValue);
        ats_free(TagValue);
      } else if (count == 0)
        break;
      snprintf(eth, sizeof(eth), "eth%d", ++count);
    }

    TagValue = netXml.getXmlTagValue("DefaultGateway");
    if (TagValue != NULL) {
      Config_SetDefaultRouter(TagValue);
      ats_free(TagValue);
    }


    TagValue = netXml.getXmlTagValue("DNSServer");
    if (TagValue != NULL) {
      Config_SetDNS_Servers(TagValue);
      ats_free(TagValue);
    }

    TagValue = netXml.getXmlTagValue("NTPServers");
    if (TagValue != NULL) {
      Config_SetNTP_Servers(0, TagValue);
      ats_free(TagValue);
    }

    // Get Admin GUI encrypted password.
    TSActionNeedT action_need = TS_ACTION_UNDEFINED;
    char *mail_address = netXml.getXmlTagValue("MailAddress");
    if (mail_address != NULL) {
      if (MgmtRecordSet("proxy.config.alarm_email", mail_address, &action_need) != TS_ERR_OKAY) {
        DPRINTF(("Config_FloppyNetRestore: failed to set new mail_address %s!\n", mail_address));
      } else {
        DPRINTF(("Config_FloppyNetRestore: set new mail_address %s!\n", mail_address));
      }
      ats_free(mail_address);
    }

    // Make sure this is the last entry in these series. We restart traffic server here and hence
    // should be done at the very end.
    TagValue = netXml.getXmlTagValue("TimeZone");
    if (TagValue != NULL) {
      //This is the last one - here we restart TM if it is not floppy configuration
      if (!isFloppyConfig) {
#if !defined(freebsd) && !defined(darwin)
        Time_SetTimezone(true, TagValue);
#endif
      } else {
#if !defined(freebsd) && !defined(darwin)
        Time_SetTimezone(false, TagValue);
#endif
      }
    }
    ats_free(TagValue);
  }

  if(setreuid(old_euid, old_euid) != 0)
    perror("Config_RestoreNetConfig set old uid failed: "); //happens only for floppy config
  return 0;
}


int
Config_SaveNetConfig(char *file)
{
  // None of these are used it seems ...
  //char ethX[6];
  //char ethXIP[24];
  //char ethXNM[24];
  //char ethXGW[24];
  XMLDom netConfigXML;
#if !defined(freebsd) && !defined(darwin) && !defined(solaris)
  netConfigXML.setNodeName("APPLIANCE_CONFIG");

  XMLNode *child2 = new XMLNode;
  char *parentAttributes3[3];
  size_t len;
  len = sizeof("type") + 1;
  parentAttributes3[0] = new char[len];
  snprintf(parentAttributes3[0], len, "%s", "type");
  len = sizeof("Version") + 1;
  parentAttributes3[1] = new char[len];
  snprintf(parentAttributes3[1], len, "%s", "Version");
  parentAttributes3[2] = NULL;
  child2->setAttributes(parentAttributes3);
  child2->setNodeName("CONFIG_TYPE");

  TSString TMVersion = NULL;
  if (TSRecordGetString("proxy.node.version.manager.short", &TMVersion) == TS_ERR_OKAY) {
    XMLNode *VersionString = new XMLNode;
    if (TMVersion != NULL) {
      VersionString->setNodeName("VersionString");
      VersionString->setNodeValue(TMVersion);
      child2->AppendChild(VersionString);
    } else {
      delete VersionString;
    }
  }


  XMLNode *child = new XMLNode;
  char *parentAttributes[3];
  len = sizeof("type") + 1;
  parentAttributes[0] = new char[len];
  snprintf(parentAttributes[0], len, "%s", "type");
  len = sizeof("NW Settings") + 1;
  parentAttributes[1] = new char[len];
  snprintf(parentAttributes[1], len, "%s", "NW Settings");
  parentAttributes[2] = NULL;
  child->setAttributes(parentAttributes);
  child->setNodeName("CONFIG_TYPE");

  XMLNode *HostName = new XMLNode;
  char NWHostName[256];
  Net_GetHostname(NWHostName, sizeof(NWHostName));
  if (NWHostName != NULL) {
    HostName->setNodeName("HostName");
    HostName->setNodeValue(NWHostName);
    child->AppendChild(HostName);
  }

  XMLNode *DefaultGateway = new XMLNode;
  char NWDefaultGateway[256];
  Net_GetDefaultRouter(NWDefaultGateway, sizeof(NWDefaultGateway));
  if (NWDefaultGateway != NULL) {
    DefaultGateway->setNodeName("DefaultGateway");
    DefaultGateway->setNodeValue(NWDefaultGateway);
    child->AppendChild(DefaultGateway);
  }

  char Int[5];
  char NWPerNICDefaultGateway[256];
  int intCount;
  len = sizeof("InterfaceName") + 1;
  for (intCount = 0; intCount < Net_GetNetworkIntCount(); intCount++) {
    XMLNode *PerNICDefaultGateway = new XMLNode;
    snprintf(Int, sizeof(Int), "eth%d", intCount);
    Net_GetNIC_Gateway(Int, NWPerNICDefaultGateway, sizeof(NWPerNICDefaultGateway));
    if (NWPerNICDefaultGateway != NULL) {
      PerNICDefaultGateway->setNodeName("PerNICDefaultGateway");
      PerNICDefaultGateway->setNodeValue(NWPerNICDefaultGateway);
      char *attributes[3];
      attributes[0] = new char[len];
      snprintf(attributes[0], len, "%s", "InterfaceName");
      attributes[1] = Int;
      attributes[2] = NULL;
      PerNICDefaultGateway->setAttributes(attributes);
      child->AppendChild(PerNICDefaultGateway);
    }
  }

  char NWInterfaceIPAddress[256];
  for (intCount = 0; intCount < Net_GetNetworkIntCount(); intCount++) {
    XMLNode *InterfaceIPAddress = new XMLNode;
    snprintf(Int, sizeof(Int), "eth%d", intCount);
    Net_GetNIC_IP(Int, NWInterfaceIPAddress, sizeof(NWInterfaceIPAddress));
    if (NWInterfaceIPAddress != NULL) {
      InterfaceIPAddress->setNodeName("InterfaceIPAddress");
      InterfaceIPAddress->setNodeValue(NWInterfaceIPAddress);
      char *attributes[3];
      attributes[0] = new char[len];
      snprintf(attributes[0], len, "%s", "InterfaceName");
      attributes[1] = Int;
      attributes[2] = NULL;
      InterfaceIPAddress->setAttributes(attributes);
      child->AppendChild(InterfaceIPAddress);
    }
  }

  char NWInterfaceNetmask[256];
  for (intCount = 0; intCount < Net_GetNetworkIntCount(); intCount++) {
    XMLNode *InterfaceNetmask = new XMLNode;
    snprintf(Int, sizeof(Int), "eth%d", intCount);
    Net_GetNIC_Netmask(Int, NWInterfaceNetmask, sizeof(NWInterfaceNetmask));
    if (NWInterfaceNetmask != NULL) {
      InterfaceNetmask->setNodeName("InterfaceNetmask");
      InterfaceNetmask->setNodeValue(NWInterfaceNetmask);
      char *attributes[3];
      attributes[0] = new char[len];
      snprintf(attributes[0], len, "%s", "InterfaceName");
      attributes[1] = Int;
      attributes[2] = NULL;
      InterfaceNetmask->setAttributes(attributes);
      child->AppendChild(InterfaceNetmask);
    }
  }

  char NWDNSSearch[512];
  Net_GetDomain(NWDNSSearch, sizeof(NWDNSSearch));
  if (NWDNSSearch != NULL) {
    XMLNode *DNSSearch = new XMLNode;
    DNSSearch->setNodeName("DNSSearch");
    DNSSearch->setNodeValue(NWDNSSearch);
    child->AppendChild(DNSSearch);
  }

   /***
     int DNSCtrlCount = 0;
     SimpleTokenizer DNS(NWDNSSearch, ' ');

     int DNSServerCount = DNS.getNumTokensRemaining();
     for(int index=0; index < DNSServerCount; index++) {
       char *  DNStokens = DNS.getNext();
       XMLNode *DNSSearch = new XMLNode;
       DNSSearch->setNodeName("DNSSearch");
       DNSSearch->setNodeValue(DNStokens);
       char *attributes[3];
       attributes[0] = new char[sizeof("DomainControllerOrder")+1];
       sprintf(attributes[0], "%s", "DomainControllerOrder");
       attributes[1] = new char[sizeof(int)+1];;
       sprintf(attributes[1], "%d", index+1);
       attributes[2] = NULL;
       DNSSearch->setAttributes(attributes);
       child->AppendChild(DNSSearch);
     }
    ***/


  char NWNameServer[512];
  Config_GetDNS_Servers(NWNameServer, sizeof(NWNameServer));
  if (NWNameServer != NULL) {
    SimpleTokenizer NS(NWNameServer, ' ');

    int DNSServerCount = NS.getNumTokensRemaining();
    for (int index = 0; index < DNSServerCount; index++) {
      char *NSTokens = NS.getNext();
      XMLNode *DNSServer = new XMLNode;
      DNSServer->setNodeName("DNSServer");
      DNSServer->setNodeValue(NSTokens);
      char *attributes[3];
      len = sizeof("DomainControllerOrder") + 1;
      attributes[0] = new char[len];
      snprintf(attributes[0], len, "%s", "DomainControllerOrder");
      len = sizeof(int) + 1;
      attributes[1] = new char[len];;
      snprintf(attributes[1], len, "%d", index + 1);
      attributes[2] = NULL;
      DNSServer->setAttributes(attributes);
      child->AppendChild(DNSServer);
    }
  }


  XMLNode *NTPServers = new XMLNode;
  char NTPServerName[256];
  Config_GetNTP_Servers(NTPServerName, sizeof(NTPServerName));
  if (NTPServerName != NULL) {
    NTPServers->setNodeName("NTPServers");
    NTPServers->setNodeValue(NTPServerName);
    child->AppendChild(NTPServers);
  }

  XMLNode *child1 = new XMLNode;
  char *parentAttributes1[3];
  len = sizeof("type") + 1;
  parentAttributes1[0] = new char[len];
  snprintf(parentAttributes1[0], len, "%s", "type");
  len = sizeof("OS Settings") + 1;
  parentAttributes1[1] = new char[len];
  snprintf(parentAttributes1[1], len, "%s", "OS Settings");
  parentAttributes1[2] = NULL;
  child1->setAttributes(parentAttributes1);
  child1->setNodeName("CONFIG_TYPE");


   /***
   XMLNode *EncryptedRootPasswd = new XMLNode;
   char *NWEncryptedRootPasswd;
   Net_GetEncryptedRootPassword(&NWEncryptedRootPasswd);
     if(NWEncryptedRootPasswd) {
       EncryptedRootPasswd->setNodeName("EncryptedRootPasswd");
       EncryptedRootPasswd->setNodeValue(NWEncryptedRootPasswd);
       child1->AppendChild(EncryptedRootPasswd);
     }
   ***/

  XMLNode *TimeZone = new XMLNode;
  char NWTimeZone[256];
  Time_GetTimezone(NWTimeZone, sizeof(NWTimeZone));
  if (NWTimeZone != NULL) {
    TimeZone->setNodeName("TimeZone");
    TimeZone->setNodeValue(NWTimeZone);
    child1->AppendChild(TimeZone);
  }



  netConfigXML.AppendChild(child2);
  netConfigXML.AppendChild(child);
  netConfigXML.AppendChild(child1);
  netConfigXML.SaveToFile(file);
#endif /* !freebsd && !darwin */
  return 0;
}


int
XmlObject::LoadFile(char *file)
{
  return xmlDom.LoadFile(file);
}

char *
XmlObject::getXmlTagValue(const char *XmlTagName)
{
  char XmlTagValue[1024] = "";

  for (int parent = 0; parent < xmlDom.getChildCount(); parent++) {
    XMLNode *parentNode = xmlDom.getChildNode(parent);
    if (parentNode != NULL) {
      int XmlTagCount = parentNode->getChildCount(XmlTagName);
      for (int tagCount = 0; tagCount < XmlTagCount; tagCount++) {
        if (parentNode->getChildNode(XmlTagName, tagCount)->getNodeValue() != NULL) {
          ink_strlcat(XmlTagValue, parentNode->getChildNode(XmlTagName, tagCount)->getNodeValue(),
                  sizeof(XmlTagValue));
          if (tagCount + 1 < XmlTagCount)
            ink_strlcat(XmlTagValue, " ", sizeof(XmlTagValue));
        }
      }
    }
  }
  if (strlen(XmlTagValue) == 0)
    return NULL;
  return ats_strdup(XmlTagValue);
}


char *
XmlObject::getXmlTagValueAndAttribute(char *XmlAttribute, const char *XmlTagName)
{
  char XmlTagValue[1024] = "";

  for (int parent = 0; parent < xmlDom.getChildCount(); parent++) {
    XMLNode *parentNode = xmlDom.getChildNode(parent);

    int XmlTagCount = parentNode->getChildCount(XmlTagName);
    for (int tagCount = 0; tagCount < XmlTagCount; tagCount++) {
      if (parentNode->getChildNode(XmlTagName, tagCount)->getNodeValue() != NULL) {
        if ((parentNode->getChildNode(XmlTagName, tagCount)->m_nACount > 0) && (strcmp(parentNode->
                                                                                       getChildNode(XmlTagName,
                                                                                                    tagCount)->
                                                                                       m_pAList[0].pAValue,
                                                                                       XmlAttribute) == 0)) {
          ink_strlcat(XmlTagValue, parentNode->getChildNode(XmlTagName, tagCount)->getNodeValue(),
                  sizeof(XmlTagValue));
          return ats_strdup(XmlTagValue);
        }
      }
    }
  }
  return NULL;
}





int
Config_SetSMTP_Server(char *server)
{
  return (Net_SetSMTP_Server(server));
}

int
Config_GetSMTP_Server(char *server)
{
  return (Net_GetSMTP_Server(server));
}



/* helper function to umount the flopppy when we are done.
 *
 */


int
uMountFloppy(char *net_floppy_config)
{
  pid_t pid;

  if ((pid = fork()) < 0) {
    DPRINTF(("Config_FloppyNetRestore [uMountFloppy]: unable to fork()\n"));
    return 1;
  } else if (pid > 0) {         /* Parent */
    int status;
    waitpid(pid, &status, 0);

    if (status != 0) {
      DPRINTF(("Config_FloppyNetRestore [uMountFloppy]: %s done failed!\n", net_floppy_config));
      return 1;
    }
  } else {
    int res = execl(net_floppy_config,"net_floppy_config","done",(char*)NULL);
    return res;
  }

  return 0;
}



/* This function will use mostly available APIs to set network settings from floppy.
 * It uses the same XML file format used by the snapshot function, with added funtionality.
 * It also uses a script name net_floppy_config to make sure the floppy is mounted and has the right XML file
 *
 */

int
Config_FloppyNetRestore()
{

  FILE *tmp_floppy_config;
  int i = 0;
  pid_t pid;
  char buffer[1024];
  char floppy_config_file[1024];
  char mount_dir[1024];
  char net_floppy_config[PATH_NAME_MAX + 1]; //script file which mounts the floppy
  // None of these seems to be used ...
  //char *mail_address, *sys_location, *sys_contact, *sys_name, *authtrapenable, *trap_community, *trap_host;
  //char *gui_passwd, *e_gui_passwd;
  //INKActionNeedT action_need, top_action_req = INK_ACTION_UNDEFINED;
  int status = 0;

  ink_filepath_make(net_floppy_config, PATH_NAME_MAX,
                    Layout::get()->bindir, "net_floppy_config");

  if (access(net_floppy_config, R_OK | X_OK) == -1) {
    DPRINTF(("Config_FloppyNetRestore: net_floppy_config does not exist - abort\n"));
    return 1;
  }

  if ((pid = fork()) < 0) {
    DPRINTF(("Config_FloppyNetRestore: unable to fork()\n"));
    return 1;
  } else if (pid > 0) {         /* Parent */
    waitpid(pid, &status, 0);

    if (status != 0) {
      DPRINTF(("Config_FloppyNetRestore: %s do failed!\n", net_floppy_config));
      return status;
    }
  } else {
    status = execl(net_floppy_config,"net_floppy_config","do", (char*)NULL);
    return status;
  }

  //now the floppy is mounted with the right file
  // First, call the snapshot restore function
  //Here we assume for now /mnt/floppy/floppy.cnf - but we can change it
  //easily as this is written in /tmp/net_floppy_config by the script

  if ((tmp_floppy_config = fopen("/tmp/net_floppy_config", "r")) == NULL) {
    DPRINTF(("Config_FloppyNetRestore: unable to open /tmp/net_floppy_config.\n"));
    return 1;
  }

  i = 0;
  NOWARN_UNUSED_RETURN(fgets(buffer, 1024, tmp_floppy_config));
  fclose(tmp_floppy_config);
  while (!isspace(buffer[i])) {
    mount_dir[i] = buffer[i];
    i++;
  }
  mount_dir[i] = '\0';

  // Copy the net_config.xml from floppy to /tmp/net_config.xml.
  // Unmount floppy and then use /tmp/net_config.xml to restore the
  // settings. This is required as a restart of traffic_manager
  //  might hinder unmount of floppy
  NOWARN_UNUSED_RETURN(system("rm -f /tmp/net_config.xml"));

  char xml_temp_dir[256];
  snprintf(xml_temp_dir, sizeof(xml_temp_dir), "/bin/cp -f %s/net_config.xml /tmp/net_config.xml", mount_dir);
  NOWARN_UNUSED_RETURN(system(xml_temp_dir));
  uMountFloppy(net_floppy_config);      //umount the floppy

  //sprintf(floppy_config_file, "%s/net_config.xml", mount_dir);
  // TODO: Make this real temp file, so that multiple instances
  //       of TrafficServer can operate
  snprintf(floppy_config_file, sizeof(floppy_config_file), "/tmp/net_config.xml");

/** Lock file manipulation. We should implement this
    struct stat floppyNetConfig;
    bool restoreConfig = true;
    int oldModTime;
    if(!stat(floppy_config_file, &floppyNetConfig)) {
      FILE *floppyLockFile = fopen("/home/inktomi/5.2.12/etc/trafficserver/internal/floppy.dat", "r+");
      if(floppyLockFile != NULL) {
        fscanf(floppyLockFile, "%d", &oldModTime);
        if(oldModTime == floppyNetConfig.st_mtime)
          restoreConfig = false;
        else {
          rewind(floppyLockFile);
          fprintf(floppyLockFile, "%d", floppyNetConfig.st_mtime);
        }
        fclose(floppyLockFile);
      }
    }
***/

  bool restoreConfig = true;
  if (restoreConfig) {
    status = Config_RestoreNetConfig(floppy_config_file);
    if (status) {
      DPRINTF(("Config_FloppyNetRestore: call to Config_RestoreNetConfig failed!\n"));
      //uMountFloppy(net_floppy_config); //umount the floppy
      return status;
    }
  }

  return 0;

}

#endif /* defined(linux) || defined(solaris) || defined(freebsd) || defined(darwin) */
