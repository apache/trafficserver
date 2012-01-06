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


#include "libts.h"
#include "I_Layout.h"

#ifdef HAVE_PCRE_PCRE_H
#include <pcre/pcre.h>
#else
#include <pcre.h>
#endif

#if defined(linux) || defined(freebsd) || defined(darwin) || defined(openbsd)

#include "SysAPI.h"
#include <unistd.h>
#include <sys/wait.h>
#include <ink_string.h>
#include <grp.h>

#include <ctype.h>
#include "mgmtapi.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <sys/stat.h>


#endif

#if defined(solaris)
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <stdarg.h>


#include "ParseRules.h"

#endif

#define NETCONFIG_HOSTNAME  0
#define NETCONFIG_GATEWAY   1
#define NETCONFIG_DOMAIN    2
#define NETCONFIG_DNS       3
#define NETCONFIG_INTF_UP   4
#define NETCONFIG_INTF_DOWN 5
#define NETCONFIG_INTF_DISABLE 8

#define TIMECONFIG_ALL	    0
#define TIMECONFIG_TIME     1
#define TIMECONFIG_DATE     2
#define TIMECONFIG_TIMEZONE 3
#define TIMECONFIG_NTP      4

#ifdef DEBUG_SYSAPI
#define DPRINTF(x)  printf x
#else
#define DPRINTF(x)
#endif


const char *
strcasestr(char *container, char *substr)
{
  return ParseRules::strcasestr(container, substr);
}

// go through string, ane terminate it with \0
void
makeStr(char *str)
{
  char *p = str;
  while (*p) {
    if (*p == '\n') {
      *p = '\0';
      return;
    }
    p++;
  }
}


int Net_GetNetworkIntCount();
int Net_GetNetworkInt(int int_num, char *interface, size_t interface_len);
int Net_GetNIC_Protocol(char *interface, char *protocol, size_t protocol_len);
int Net_GetNIC_Start(char *interface, char *start, size_t start_len);
int Net_GetNIC_Status(char *interface, char *status, size_t status_len);
int Net_SetNIC_Up(char *interface, char *onboot, char *protocol, char *ip, char *netmask, const char *gateway);
int Net_GetNIC_Gateway(char *interface, char *gateway, size_t gateway_len);
int Net_GetNIC_Netmask(char *interface, char *netmask, size_t netmask_len);
int Net_GetNIC_IP(char *interface, char *ip, size_t ip_len);
int Net_GetDefaultRouter(char *router, size_t router_len);
int NetConfig_Action(int index, ...);
int TimeConfig_Action(int index, bool restart, ...);
int Net_GetNIC_Values(char *interface, char *status, char *onboot, char *static_ip, char *ip, char *netmask,
                      char *gateway);
int find_value(const char *pathname, const char *key, char *value, size_t value_len, const char *delim, int no);
static bool recordRegexCheck(const char *pattern, const char *value);

int
Net_GetHostname(char *hostname, size_t hostname_len)
{
  hostname[0] = 0;
  return (gethostname(hostname, hostname_len));
}


int
Net_IsValid_Hostname(char *hostname)
{

  if (hostname == NULL) {
    return 0;
  } else if (strstr(hostname, " ") != NULL || hostname[strlen(hostname) - 1] == '.') {
    return 0;
  } else if (!recordRegexCheck(".+\\..+\\..+", hostname)) {
    return 0;
  }
  return 1;
}
  
int
NetConfig_Action(int index, ...)
{
  const char *argv[10];
  pid_t pid;
  int status;

  va_list ap;
  va_start(ap, index);

  argv[0] = "net_config";

  switch (index) {
  case NETCONFIG_HOSTNAME:
    argv[1] = "0";
    argv[2] = va_arg(ap, char *);
    argv[3] = va_arg(ap, char *);
    argv[4] = va_arg(ap, char *);
    argv[5] = NULL;
    break;
  case NETCONFIG_GATEWAY:
    argv[1] = "1";
    argv[2] = va_arg(ap, char *);
    argv[3] = va_arg(ap, char *);
    argv[4] = NULL;
    break;
  case NETCONFIG_DOMAIN:
    argv[1] = "2";
    argv[2] = va_arg(ap, char *);
    argv[3] = NULL;
    break;
  case NETCONFIG_DNS:
    argv[1] = "3";
    argv[2] = va_arg(ap, char *);
    argv[3] = NULL;
    break;
  case NETCONFIG_INTF_UP:
    argv[1] = "4";
    argv[2] = va_arg(ap, char *);       // nic_name
    argv[3] = va_arg(ap, char *);       // static_ip (1/0)
    argv[4] = va_arg(ap, char *);       // ip
    argv[5] = va_arg(ap, char *);       // netmask
    argv[6] = va_arg(ap, char *);       // onboot (1/0)
    argv[7] = va_arg(ap, char *);       // gateway_ip
    argv[8] = NULL;
    break;
  case NETCONFIG_INTF_DOWN:
    argv[1] = "5";
    argv[2] = va_arg(ap, char *);
    argv[3] = NULL;
    break;
  case NETCONFIG_INTF_DISABLE:
    argv[1] = "8";
    argv[2] = va_arg(ap, char *);
    argv[3] = NULL;
    break;
  }

  va_end(ap);

  if ((pid = fork()) < 0) {
    exit(1);
  } else if (pid > 0) {
    wait(&status);
  } else {
    int res;

    close(1);                   // close STDOUT
    close(2);                   // close STDERR

    char *command_path;

    command_path = Layout::relative_to(Layout::get()->bindir, "net_config");
    res = execv(command_path, (char* const*)argv);

    ats_free(command_path);
    if (res != 0) {
      DPRINTF(("[SysAPI] fail to call net_config\n"));
    }
    _exit(res);
  }

  return 0;
}

int
Net_SetHostname(char *hostname)
{
  int status;
  char old_hostname[256], protocol[80];
  char ip_addr[80], name[80], nic_status[80];
  bool found = false;

  old_hostname[0] = '\0';

  DPRINTF(("Net_SetHostname: hostname %s\n", hostname));

  if (!Net_IsValid_Hostname(hostname)) {
    DPRINTF(("Net_SetHostname: invalid hostname\n"));
    return -1;
  }

  Net_GetHostname(old_hostname, sizeof(old_hostname));
  if (!strlen(old_hostname)) {
    DPRINTF(("Net_SetHostname: failed to get old_hostname\n"));
    return -1;
  }
  //Fix for BZ48925 - adding the correct ip to /etc/hosts
  //First get an IP of a valid interface - we don't care so much which one as we don't
  //use it in TS - it is just a place holder for Real Proxy with no DNS server (see BZ38199)

  ip_addr[0] = '\0';
  int count = Net_GetNetworkIntCount();
  if (count == 0) {             //this means we didn't find any interface
    ip_addr[0] = '\0';
  } else {
    name[0] = '\0';
    nic_status[0] = '\0';
    protocol[0] = '\0';
    for (int i = 0; i < count; i++) {   //since we are looping - we will get the "last" available IP - doesn't matter to us
      Net_GetNetworkInt(i, name, sizeof(name)); //we know we have at least one
      if (strlen(name) > 0) {
        Net_GetNIC_Status(name, nic_status, sizeof(nic_status));
        Net_GetNIC_Protocol(name, protocol, sizeof(protocol));
#if !defined (solaris)
        if ((strcmp("up", nic_status) == 0) && (!found) ) {
#else
        if ((strcmp("up", nic_status) == 0) && (!found)  
            && (strcasecmp(protocol, "dhcp") != 0)) {
#endif
          //we can use this interface
          Net_GetNIC_IP(name, ip_addr, sizeof(ip_addr));
          found = true;
        }
      }
    }
  }
  DPRINTF(("Net_SetHostname: calling INKSetHostname \"%s %s %s\"\n", hostname, old_hostname, ip_addr));
  status = NetConfig_Action(NETCONFIG_HOSTNAME, hostname, old_hostname, ip_addr);

  return status;
}

// return true if the line is commented out, or if line is blank
// return false otherwise
bool
isLineCommented(char *line)
{
  char *p = line;
  while (*p) {
    if (*p == '#')
      return true;
    if (!isspace(*p) && *p != '#')
      return false;
    p++;
  }
  return true;
}

// return 1 if the IP addr valid, return 0 if invalid
//    valid IP address is four decimal numbers (0-255) separated by dots
int
Net_IsValid_IP(char *ip_addr)
{
  char addr[80];
  char octet1[80], octet2[80], octet3[80], octet4[80];
  int byte1, byte2, byte3, byte4;
  char junk[256];

  if (ip_addr == NULL) {
    return 1;
  }

  ink_strlcpy(addr, ip_addr, sizeof(addr));

  octet1[0] = '\0';
  octet2[0] = '\0';
  octet3[0] = '\0';
  octet4[0] = '\0';
  junk[0] = '\0';

  // each of the octet? is not shorter than addr, so no overflow will happen
  // disable coverity check in this case
  // coverity[secure_coding]
  int matches = sscanf(addr, "%[0-9].%[0-9].%[0-9].%[0-9]%[^0-9]",
                       octet1, octet2, octet3, octet4, junk);

  if (matches != 4) {
    return 0;
  }

  byte1 = octet1[0] ? atoi(octet1) : 0;
  byte2 = octet2[0] ? atoi(octet2) : 0;
  byte3 = octet3[0] ? atoi(octet3) : 0;
  byte4 = octet4[0] ? atoi(octet4) : 0;

  if (byte1<0 || byte1> 255 || byte2<0 || byte2> 255 || byte3<0 || byte3> 255 || byte4<0 || byte4> 255) {
    return 0;
  }

  if (strlen(junk)) {
    return 0;
  }

  return 1;
}

int
Net_SetDefaultRouter(char *router)
{
  int status;
  char old_router[80];

  DPRINTF(("Net_SetDefaultRouter: router %s\n", router));

  if (!Net_IsValid_IP(router)) {
    DPRINTF(("Net_SetDefaultRouter: invalid IP\n"));
    return -1;
  }


  Net_GetDefaultRouter(old_router, sizeof(old_router));
  if (!strlen(old_router)) {
    DPRINTF(("Net_SetHostname: failed to get old_router\n"));
    return -1;
  }

  status = NetConfig_Action(NETCONFIG_GATEWAY, router, old_router);
  DPRINTF(("Net_SetDefaultRouter: NetConfig_Action returned %d\n", status));
  if (status) {
    return status;
  }

  return status;
}

int
Net_GetDomain(char *domain, size_t domain_len)
{
  //  domain can be defined using search or domain keyword
  domain[0] = 0;
  return !find_value("/etc/resolv.conf", "search", domain, domain_len, " ", 0);
}

int
Net_SetDomain(const char *domain)
{
  int status;

  DPRINTF(("Net_SetDomain: domain %s\n", domain));

  status = NetConfig_Action(NETCONFIG_DOMAIN, domain);
  if (status) {
    return status;
  }

  return status;
}


int
Net_GetDNS_Servers(char *dns, size_t dns_len)
{
  char ip[80];
  dns[0] = 0;
  int i = 0;
  while (find_value("/etc/resolv.conf", "nameserver", ip, sizeof(ip), " ", i++)) {
    ink_strlcat(dns, ip, dns_len);
    ink_strlcat(dns, " ", dns_len);
  }
  return 0;
}

int
Net_SetDNS_Servers(char *dns)
{
  int status;
  char buff[512];
  char *tmp1, *tmp2;
  memset(buff, 0, 512);

  DPRINTF(("Net_SetDNS_Servers: dns %s\n", dns));

  if (dns == NULL) {
    return -1;
  }
  // check all IP addresses for validity
  ink_strlcpy(buff, dns, sizeof(buff));
  tmp1 = buff;
  while ((tmp2 = strtok(tmp1, " \t")) != NULL) {
    DPRINTF(("Net_SetDNS_Servers: tmp2 %s\n", tmp2));
    if (!Net_IsValid_IP(tmp2)) {
      return -1;
    }
    tmp1 = NULL;
  }
  DPRINTF(("Net_SetDNS_Servers: dns %s\n", dns));
  status = NetConfig_Action(NETCONFIG_DNS, dns);
  if (status) {
    return status;
  }

  return status;
}

int
Net_IsValid_Interface(char *interface)
{
  char name[80];

  if (interface == NULL) {
    return 0;
  }
  int count = Net_GetNetworkIntCount();
  for (int i = 0; i < count; i++) {
    Net_GetNetworkInt(i, name, sizeof(name));
    if (strcmp(name, interface) == 0)
      return 1;
  }
  return 0;
}

int
Net_SetNIC_Down(char *interface)
{
  int status;
  char ip[80];

  if (!Net_IsValid_Interface(interface))
    return -1;

  status = NetConfig_Action(NETCONFIG_INTF_DOWN, interface);
  if (status) {
    return status;
  }

  Net_GetNIC_IP(interface, ip, sizeof(ip));

  return status;
}

int
Net_SetNIC_StartOnBoot(char *interface, char *onboot)
{
  char nic_protocol[80], nic_ip[80], nic_netmask[80], nic_gateway[80];

  Net_GetNIC_Protocol(interface, nic_protocol, sizeof(nic_protocol));
  Net_GetNIC_IP(interface, nic_ip, sizeof(nic_ip));
  Net_GetNIC_Netmask(interface, nic_netmask, sizeof(nic_netmask));
  Net_GetNIC_Gateway(interface, nic_gateway, sizeof(nic_gateway));

  return (Net_SetNIC_Up(interface, onboot, nic_protocol, nic_ip, nic_netmask, nic_gateway));
}

int
Net_SetNIC_BootProtocol(char *interface, char *nic_protocol)
{
#if !defined(freebsd) && !defined(darwin)
  char nic_boot[80], nic_ip[80], nic_netmask[80], nic_gateway[80];
  Net_GetNIC_Start(interface, nic_boot, sizeof(nic_boot));
  Net_GetNIC_IP(interface, nic_ip, sizeof(nic_ip));
  Net_GetNIC_Netmask(interface, nic_netmask, sizeof(nic_netmask));
  Net_GetNIC_Gateway(interface, nic_gateway, sizeof(nic_gateway));

  return (Net_SetNIC_Up(interface, nic_boot, nic_protocol, nic_ip, nic_netmask, nic_gateway));
#else
  return -1;
#endif
}

int
Net_SetNIC_IP(char *interface, char *nic_ip)
{
#if !defined(freebsd) && !defined(darwin) && !defined(solaris)
  char nic_boot[80], nic_protocol[80], nic_netmask[80], nic_gateway[80], old_ip[80];
  Net_GetNIC_IP(interface, old_ip, sizeof(old_ip));
  Net_GetNIC_Start(interface, nic_boot, sizeof(nic_boot));
  Net_GetNIC_Protocol(interface, nic_protocol, sizeof(nic_protocol));
  Net_GetNIC_Netmask(interface, nic_netmask, sizeof(nic_netmask));
  Net_GetNIC_Gateway(interface, nic_gateway, sizeof(nic_gateway));

  return (Net_SetNIC_Up(interface, nic_boot, nic_protocol, nic_ip, nic_netmask, nic_gateway));
#else
  return -1;
#endif
}

int
Net_SetNIC_Netmask(char *interface, char *nic_netmask)
{
#if !defined(freebsd) && !defined(darwin) && !defined(solaris)
  char nic_boot[80], nic_protocol[80], nic_ip[80], nic_gateway[80];
  Net_GetNIC_Start(interface, nic_boot, sizeof(nic_boot));
  Net_GetNIC_Protocol(interface, nic_protocol, sizeof(nic_protocol));
  Net_GetNIC_IP(interface, nic_ip, sizeof(nic_ip));
  Net_GetNIC_Gateway(interface, nic_gateway, sizeof(nic_gateway));

  return (Net_SetNIC_Up(interface, nic_boot, nic_protocol, nic_ip, nic_netmask, nic_gateway));
#else
  return -1;
#endif
}

int
Net_SetNIC_Gateway(char *interface, char *nic_gateway)
{
#if !defined(freebsd) && !defined(darwin) && !defined(solaris)
  char nic_boot[80], nic_protocol[80], nic_ip[80], nic_netmask[80];
  Net_GetNIC_Start(interface, nic_boot, sizeof(nic_boot));
  Net_GetNIC_Protocol(interface, nic_protocol, sizeof(nic_protocol));
  Net_GetNIC_IP(interface, nic_ip, sizeof(nic_ip));
  Net_GetNIC_Netmask(interface, nic_netmask, sizeof(nic_netmask));
  DPRINTF(("Net_SetNIC_Gateway:: interface %s onboot %s protocol %s ip %s netmask %s gateway %s\n", interface, nic_boot,
           nic_protocol, nic_ip, nic_netmask, nic_gateway));

  return (Net_SetNIC_Up(interface, nic_boot, nic_protocol, nic_ip, nic_netmask, nic_gateway));
#else
  return -1;
#endif
}


int
find_value(const char *pathname, const char *key, char *value, size_t value_len, const char *delim, int no)
{
  char buffer[1024];
  char *pos;
  char *open_quot, *close_quot;
  FILE *fp;
  int find = 0;
  int counter = 0;

  int str_len = strlen(key);

  value[0] = 0;
  // coverity[fs_check_call]
  if (access(pathname, R_OK)) {
    return find;
  }
  // coverity[toctou]
  if ((fp = fopen(pathname, "r")) != NULL) {
    NOWARN_UNUSED_RETURN(fgets(buffer, 1024, fp));

    while (!feof(fp)) {
      if (!isLineCommented(buffer) &&   // skip if line is commented
          (strstr(buffer, key) != NULL) && ((strncmp((buffer + str_len), "=", 1) == 0) ||
                                            (strncmp((buffer + str_len), " ", 1) == 0) ||
                                            (strncmp((buffer + str_len), "\t", 1) == 0))) {
        if (counter != no) {
          counter++;
        } else {
          find = 1;

          pos = strstr(buffer, delim);
          if (pos == NULL && (strcmp(delim, " ") == 0)) {       // anniec - give tab a try
            pos = strstr(buffer, "\t");
          }
          if (pos != NULL) {
            pos++;
            if ((open_quot = strchr(pos, '"')) != NULL) {
              pos = open_quot + 1;
              close_quot = strrchr(pos, '"');
              *close_quot = '\0';
            }
            //Bug49159, Dell use "'" in the ifcfg-ethx file
            else if ((open_quot = strchr(pos, '\'')) != NULL) {
              pos = open_quot + 1;
              close_quot = strrchr(pos, '\'');
              *close_quot = '\0';
            }
            //filter the comment on the same line
            char *cur;
            cur = pos;
            while (*cur != '#' && *cur != '\0') {
              cur++;
            }
            *cur = '\0';

            ink_strlcpy(value, pos, value_len);

            if (value[strlen(value) - 1] == '\n') {
              value[strlen(value) - 1] = '\0';
            }
          }
          break;
        }
      }
      NOWARN_UNUSED_RETURN(fgets(buffer, 1024, fp));
    }
    fclose(fp);
  }
  return find;
}

int
Time_GetTime(char *hour, const size_t hourSize, char *minute, const size_t minuteSize, char *second,
             const size_t secondSize)
{
  int status;
  struct tm *my_tm;
  struct timeval tv;

  status = gettimeofday(&tv, NULL);
  if (status != 0) {
    return status;
  }
  my_tm = localtime((const time_t *)&(tv.tv_sec));

  snprintf(hour, hourSize, "%d", my_tm->tm_hour);
  snprintf(minute, minuteSize, "%d", my_tm->tm_min);
  snprintf(second, secondSize, "%d", my_tm->tm_sec);

  return status;
}

int
Time_SetTime(bool restart, char *hour, char *minute, char *second)
{
  int status;

  status = TimeConfig_Action(TIMECONFIG_TIME, restart, hour, minute, second);

  return status;
}

int
Time_SetTimezone(bool restart, char *timezone)
{
  int status;

  status = TimeConfig_Action(3, restart, timezone);

  return status;
}

int
Time_GetDate(char *month, const size_t monthSize, char *day, const size_t daySize, char *year, const size_t yearSize)
{
  int status;
  struct tm *my_tm;
  struct timeval tv;

  status = gettimeofday(&tv, NULL);
  if (status != 0) {
    return status;
  }
  my_tm = localtime((time_t *)&(tv.tv_sec));

  snprintf(month, monthSize, "%d", my_tm->tm_mon + 1);
  snprintf(day, daySize, "%d", my_tm->tm_mday);
  snprintf(year, yearSize, "%d", my_tm->tm_year + 1900);

  return status;
}

int
Time_SetDate(bool restart, char *month, char *day, char *year)
{
  int status;

  status = TimeConfig_Action(TIMECONFIG_DATE, restart, month, day, year);

  return status;
}

int
TimeConfig_Action(int index, bool restart ...)
{
  const char *argv[20];
  pid_t pid;
  int status;

  va_list ap;
  va_start(ap, restart);

  argv[0] = "time_config";
  if (restart) {
    argv[1] = "1";
  } else {
    argv[1] = "0";
  }

  switch (index) {
  case TIMECONFIG_TIME:
    argv[2] = "1";
    argv[3] = va_arg(ap, char *);
    argv[4] = va_arg(ap, char *);
    argv[5] = va_arg(ap, char *);
    argv[6] = NULL;
    break;
  case TIMECONFIG_DATE:
    argv[2] = "2";
    argv[3] = va_arg(ap, char *);
    argv[4] = va_arg(ap, char *);
    argv[5] = va_arg(ap, char *);
    argv[6] = NULL;
    break;
  case TIMECONFIG_TIMEZONE:
    argv[2] = "3";
    argv[3] = va_arg(ap, char *);
    argv[4] = NULL;
    break;
  case TIMECONFIG_NTP:
    argv[2] = "4";
    argv[3] = va_arg(ap, char *);
    argv[4] = NULL;
    break;
  }
  va_end(ap);

  if ((pid = fork()) < 0) {
    exit(1);
  } else if (pid > 0) {
    wait(&status);
  } else {
    int res;

    //close(1);  // close STDOUT
    //close(2);  // close STDERR

    char *command_path;

    command_path = Layout::relative_to(Layout::get()->bindir, "time_config");
    res = execv(command_path, (char* const*)argv);

    ats_free(command_path);
    if (res != 0) {
      DPRINTF(("[SysAPI] fail to call time_config\n"));
    }
    _exit(res);
  }
  return 0;
}

int
Net_SetSMTP_Server(char *server)
{
  NOWARN_UNUSED(server);
  return 0;
}

int
Net_GetSMTP_Server(char *server)
{
  NOWARN_UNUSED(server);
  return 0;
}


#if defined(linux) || defined(freebsd) || defined(darwin) || defined(openbsd)

int
Net_GetDefaultRouter(char *router, size_t router_len)
{

  int value;
  router[0] = '\0';
  value = find_value("/etc/sysconfig/network", "GATEWAY", router, router_len, "=", 0);
  DPRINTF(("[Net_GetDefaultRouter] Find returned %d\n", value));
  if (value) {
    return !value;
  } else {
    char command[80];
    const char *tmp_file = "/tmp/route_status";
    char buffer[256];
    FILE *fp;

    ink_strlcpy(command, "/sbin/route -n > /tmp/route_status", sizeof(command));
    remove(tmp_file);
    if (system(command) == -1) {
      DPRINTF(("[Net_GetDefaultRouter] run route -n\n"));
      return -1;
    }

    fp = fopen(tmp_file, "r");
    if (fp == NULL) {
      DPRINTF(("[Net_GetDefaultRouter] can not open the temp file\n"));
      return -1;
    }

    char *gw_start;
    bool find_UG = false;
    NOWARN_UNUSED_RETURN(fgets(buffer, 256, fp));
    while (!feof(fp)) {
      if (strstr(buffer, "UG") != NULL) {
        find_UG = true;
        strtok(buffer, " \t");
        gw_start = strtok(NULL, " \t");
        break;
      }
      NOWARN_UNUSED_RETURN(fgets(buffer, 256, fp));
    }
    if (find_UG) {
      ink_strlcpy(router, gw_start, router_len);
      fclose(fp);
      return 0;
    }
    fclose(fp);
  }
  return 1;

}


int
Net_GetDNS_Server(char *server, size_t server_len, int no)
{
  server[0] = 0;
  return (!find_value("/etc/resolv.conf", "nameserver", server, server_len, " ", no));
}

int
Net_GetNetworkIntCount()
{

  FILE *net_device;
  char buffer[200] = "";
  int count = 0;

  // for each NIC
  net_device = fopen("/proc/net/dev", "r");
  while (!feof(net_device)) {
    if (fgets(buffer, 200, net_device)) {
      if (*buffer && strstr(buffer, "eth")) {  // only counts eth interface
        count++;
      }
    }
  }
  fclose(net_device);
  return count;
}

int
Net_GetNetworkInt(int int_num, char *interface, size_t interface_len)
{
  interface[0] = 0;

  FILE *net_device;
  char buffer[200];
  int space_len;
  char *pos, *tmp;
  int i = -1;

  *buffer = '\0';
  net_device = fopen("/proc/net/dev", "r");

  while (!feof(net_device) && (i != int_num)) {
    if (fgets(buffer, 200, net_device)) {
      if (strstr(buffer, "eth"))  // only counts the eth interface
        i++;
    }
  }
  fclose(net_device);
  if (!*buffer || (i != int_num))
    return -1;

  pos = strchr(buffer, ':');
  if (pos)
    *pos = '\0';
  space_len = strspn(buffer, " ");
  tmp = buffer + space_len;

  ink_strlcpy(interface, tmp, interface_len);

  return 0;

}

int
Net_GetNIC_Status(char *interface, char *status, size_t status_len)
{

  char ifcfg[80], command[80];

  status[0] = 0;

  ink_strlcpy(ifcfg, "/etc/sysconfig/network-scripts/ifcfg-", sizeof(ifcfg));
  ink_strlcat(ifcfg, interface, sizeof(ifcfg));

  ink_strlcpy(command, "/sbin/ifconfig | grep ", sizeof(command));
  ink_strlcat(command, interface, sizeof(command));
  ink_strlcat(command, " >/dev/null 2>&1", sizeof(command));

  if (system(command) == 0) {
    ink_strlcpy(status, "up", status_len);
  } else {
    ink_strlcpy(status, "down", status_len);
  }
  return 0;
}

int
Net_GetNIC_Start(char *interface, char *start, size_t start_len)
{

  char ifcfg[80], value[80];

  start[0] = 0;

  ink_strlcpy(ifcfg, "/etc/sysconfig/network-scripts/ifcfg-", sizeof(ifcfg));
  ink_strlcat(ifcfg, interface, sizeof(ifcfg));

  if (find_value(ifcfg, "ONBOOT", value, sizeof(value), "=", 0)) {
    if (strcasecmp(value, "yes") == 0) {
      ink_strlcpy(start, "onboot", start_len);
    } else {
      ink_strlcpy(start, "not-onboot", start_len);
    }
    return 0;
  } else {
    return 1;
  }
}

int
Net_GetNIC_Protocol(char *interface, char *protocol, size_t protocol_len)
{

  char ifcfg[80], value[80];

  protocol[0] = 0;

  ink_strlcpy(ifcfg, "/etc/sysconfig/network-scripts/ifcfg-", sizeof(ifcfg));
  ink_strlcat(ifcfg, interface, sizeof(ifcfg));

  if (find_value(ifcfg, "BOOTPROTO", value, sizeof(value), "=", 0)) {
    if ((strcasecmp(value, "none") == 0) || (strcasecmp(value, "static") == 0) || (strcasecmp(value, "dhcp") == 0)) {
      ink_strlcpy(protocol, value, protocol_len);
    } else {
      ink_strlcpy(protocol, "none", protocol_len);
    }
    return 0;
  } else {
    //if there is no BOOTPROTO, assume the default is "none" now
    ink_strlcpy(protocol, "none", protocol_len);
    return 1;
  }
}

int
Net_GetNIC_IP(char *interface, char *ip, size_t ip_len)
{

  char ifcfg[80], protocol[80], status[80];
  char command[80];

  ip[0] = 0;
  Net_GetNIC_Protocol(interface, protocol, sizeof(protocol));
  if (strcmp(protocol, "none") == 0 || strcmp(protocol, "static") == 0) {
    ink_strlcpy(ifcfg, "/etc/sysconfig/network-scripts/ifcfg-", sizeof(ifcfg));
    ink_strlcat(ifcfg, interface, sizeof(ifcfg));

    return (!find_value(ifcfg, "IPADDR", ip, ip_len, "=", 0));
  } else {
    Net_GetNIC_Status(interface, status, sizeof(status));
    if (strcmp(status, "up") == 0) {
      const char *tmp_file = "/tmp/dhcp_status";
      char buffer[256];
      FILE *fp;

      ink_strlcpy(command, "/sbin/ifconfig ", sizeof(command));
      ink_strlcat(command, interface, sizeof(command));
      ink_strlcat(command, " >", sizeof(command));
      ink_strlcat(command, tmp_file, sizeof(command));

      remove(tmp_file);
      if (system(command) == -1) {
        DPRINTF(("[Net_GetNIC_IP] can not run ifconfig\n"));
        return -1;
      }
      fp = fopen(tmp_file, "r");
      if (fp == NULL) {
        DPRINTF(("[Net_GetNIC_IP] can not open the temp file\n"));
        return -1;
      }

      char *pos, *addr_end, *addr_start = NULL;
      NOWARN_UNUSED_RETURN(fgets(buffer, 256, fp));
      while (!feof(fp)) {
        if (strstr(buffer, "inet addr:") != NULL) {
          pos = strchr(buffer, ':');
          addr_start = pos + 1;
          addr_end = strchr(addr_start, ' ');
          *addr_end = '\0';
          break;
        }
        NOWARN_UNUSED_RETURN(fgets(buffer, 256, fp));
      }

      if (addr_start)
        ink_strlcpy(ip, addr_start, ip_len);
      fclose(fp);
      return 0;
    }
  }
  return 1;
}

int
Net_GetNIC_Netmask(char *interface, char *netmask, size_t netmask_len)
{

  char ifcfg[80], protocol[80], status[80];
  char command[80];

  netmask[0] = 0;
  Net_GetNIC_Protocol(interface, protocol, sizeof(protocol));
  if (strcmp(protocol, "none") == 0 || strcmp(protocol, "static") == 0) {
    ink_strlcpy(ifcfg, "/etc/sysconfig/network-scripts/ifcfg-", sizeof(ifcfg));
    ink_strlcat(ifcfg, interface, sizeof(ifcfg));
    return (!find_value(ifcfg, "NETMASK", netmask, netmask_len, "=", 0));
  } else {
    Net_GetNIC_Status(interface, status, sizeof(status));
    if (strcmp(status, "up") == 0) {
      const char *tmp_file = "/tmp/dhcp_status";
      char buffer[256];
      FILE *fp;

      ink_strlcpy(command, "/sbin/ifconfig ", sizeof(command));
      ink_strlcat(command, interface, sizeof(command));
      ink_strlcat(command, " >", sizeof(command));
      ink_strlcat(command, tmp_file, sizeof(command));

      remove(tmp_file);
      if (system(command) == -1) {
        DPRINTF(("[Net_GetNIC_Netmask] can not run ifconfig\n"));
        return -1;
      }
      fp = fopen(tmp_file, "r");
      if (fp == NULL) {
        DPRINTF(("[Net_GetNIC_Netmask] can not open the temp file\n"));
        return -1;
      }

      char *pos, *mask_end, *mask_start = NULL;
      NOWARN_UNUSED_RETURN(fgets(buffer, 256, fp));
      while (!feof(fp)) {
        if (strstr(buffer, "Mask:") != NULL) {
          pos = strstr(buffer, "Mask:");
          mask_start = pos + 5;
          mask_end = strchr(mask_start, '\n');
          *mask_end = '\0';
          break;
        }
        NOWARN_UNUSED_RETURN(fgets(buffer, 256, fp));
      }

      if (mask_start)
        ink_strlcpy(netmask, mask_start, netmask_len);
      fclose(fp);
      return 0;
    }
  }
  return 1;

}

int
Net_GetNIC_Gateway(char *interface, char *gateway, size_t gateway_len)
{

  char ifcfg[80];

  gateway[0] = 0;
  ink_strlcpy(ifcfg, "/etc/sysconfig/network-scripts/ifcfg-", sizeof(ifcfg));
  ink_strlcat(ifcfg, interface, sizeof(ifcfg));

  return (!find_value(ifcfg, "GATEWAY", gateway, gateway_len, "=", 0));
}

int
Net_SetNIC_Up(char *interface, char *onboot, char *protocol, char *ip, char *netmask, const char *gateway)
{
  int status;

  DPRINTF(("Net_SetNIC_Up:: interface %s onboot %s protocol %s ip %s netmask %s gateway %s\n", interface, onboot,
           protocol, ip, netmask, gateway));

  if (!Net_IsValid_Interface(interface))
    return -1;

  char onboot_bool[8], protocol_bool[8], old_ip[80];
  //char *new_gateway;

  if (strcmp(onboot, "onboot") == 0) {
    ink_strlcpy(onboot_bool, "1", sizeof(onboot_bool));
  } else {
    ink_strlcpy(onboot_bool, "0", sizeof(onboot_bool));
  }

  if (strcmp(protocol, "dhcp") == 0) {
    ink_strlcpy(protocol_bool, "0", sizeof(protocol_bool));
  } else {
    ink_strlcpy(protocol_bool, "1", sizeof(protocol_bool));
  }

  if (!Net_IsValid_IP(ip))
    return -1;

  if (!Net_IsValid_IP(netmask))
    return -1;

  Net_GetNIC_IP(interface, old_ip, sizeof(old_ip));

  //DPRINTF(("Net_SetNIC_Up: int %s prot %s ip %s net %s onboot %s gw %s\n",
  //interface, protocol_bool, ip, netmask, onboot_bool, new_gateway));

  //status = NetConfig_Action(NETCONFIG_INTF_UP, interface, protocol, ip, netmask, onboot, gateway);
  status = NetConfig_Action(NETCONFIG_INTF_UP, interface, protocol_bool, ip, netmask, onboot_bool, gateway);
  if (status) {
    DPRINTF(("Net_SetNIC_Up: NetConfig_Action returned %d\n", status));
    return status;
  }

  return status;
}


#if defined(linux)

// Disable all previously disabled, and now active interfaces
int
Net_DisableInterface(char *interface)
{
  int status;

  DPRINTF(("Net_DisableInterface:: interface %s\n", interface));

  //if (!Net_IsValid_Interface(interface))
  //return -1;

  status = NetConfig_Action(NETCONFIG_INTF_DISABLE, interface);
  if (status) {
    DPRINTF(("Net_DisableInterface: NetConfig_Action returned %d\n", status));
    return status;
  }

  return status;
}

#endif /* linux */

int
Sys_User_Root(int *old_euid)
{

  *old_euid = getuid();
  seteuid(0);
  setreuid(0, 0);

  return 0;
}

int
Sys_User_Inktomi(int euid)
{
// bug 50394 - preserve saved uid as root,
//             while changing effiective and real uid to input parameter value
  setreuid(euid, 0);
  seteuid(euid);
  return 0;
}

int
Sys_Grp_Root(int *old_egid)
{
  *old_egid = getegid();
  setregid(0, *old_egid);
  return 0;
}

int
Sys_Grp_Inktomi(int egid)
{
  setregid(egid, egid);
  return 0;
}




bool
recordRegexCheck(const char *pattern, const char *value)
{
  pcre* regex;
  const char* error;
  int erroffset;
  int result;

  if (!(regex = pcre_compile(pattern, 0, &error, &erroffset, NULL)))
    return false;

  result = pcre_exec(regex, NULL, value, strlen(value), 0, 0, NULL, 0);
  pcre_free(regex);

  return (result != -1) ? true : false;
}

int
Time_SortTimezone()
{
  FILE *fp, *tmp;
  const char *zonetable = "/usr/share/zoneinfo/zone.tab";
  char buffer[1024];
  char *zone;

  fp = fopen(zonetable, "r");
  tmp = fopen("/tmp/zonetab.tmp", "w");
  if (fp == NULL || tmp == NULL) {
    DPRINTF(("[Time_SortTimezone] Can not open the file\n"));
    return -1;
  }
  NOWARN_UNUSED_RETURN(fgets(buffer, 1024, fp));
  while (!feof(fp)) {
    if (buffer[0] != '#') {
      strtok(buffer, " \t");
      strtok(NULL, " \t");
      zone = strtok(NULL, " \t");
      if (zone[strlen(zone) - 1] == '\n') {
        zone[strlen(zone) - 1] = '\0';
      }
      fprintf(tmp, "%s\n", zone);
    }
    NOWARN_UNUSED_RETURN(fgets(buffer, 1024, fp));
  }
  fclose(fp);
  fclose(tmp);
  remove("/tmp/zonetab");
  NOWARN_UNUSED_RETURN(system("/bin/sort /tmp/zonetab.tmp > /tmp/zonetab"));
  remove("/tmp/zonetab.tmp");

  return 0;
}

int
Time_GetTimezone(char *timezone, size_t timezone_len)
{
  //const char *zonetable="/usr/share/zoneinfo/zone.tab";
  //char buffer[1024];

  return (!find_value("/etc/sysconfig/clock", "ZONE", timezone, timezone_len, "=", 0));
}

int
Time_GetNTP_Servers(char *server, size_t server_len)
{
  server[0] = 0;
  return (!find_value("/etc/ntp.conf", "server", server, server_len, " ", 0));
}

int
Time_SetNTP_Servers(bool restart, char *server)
{
  int status;



  status = TimeConfig_Action(TIMECONFIG_NTP, restart, server);

  return status;
}

int
Time_GetNTP_Server(char *server, size_t server_len, int no)
{
  server[0] = 0;
  return (!find_value("/etc/ntp.conf", "server", server, server_len, " ", no));
}


int
Time_GetNTP_Status(char *status, size_t status_len)
{
  FILE *fp;
  char buffer[1024];

  status[0] = 0;

  fp = popen("/etc/init.d/ntpd status", "r");
  NOWARN_UNUSED_RETURN(fgets(buffer, 1024, fp));
  if (strstr(buffer, "running") != NULL) {
    ink_strlcpy(status, "on", status_len);
  } else {
    ink_strlcpy(status, "off", status_len);
  }

  pclose(fp);
  return 0;
}

int
Time_SetNTP_Off()
{
  int status;

  status = system("/etc/init.d/ntpd stop");
  status = system("/sbin/chkconfig --level 2345 ntpd off");
  return status;
}



#define MV_BINARY "/bin/mv"

//location - 1 is always the location of the named parameter in the buffer
int
Get_Value(char *buffer, size_t buffer_len, char *buf, int location)
{
  char *tmp_buffer;
  int i = 1;

  ink_strlcpy(buffer, strtok(buf, " "), buffer_len);

  if (buffer[0] == '"') {
    tmp_buffer = strtok(NULL, "\"");
    ink_strlcat(buffer, tmp_buffer, buffer_len);
    ink_strlcat(buffer, "\"", buffer_len);
  }

  if (location == 1) {
    char *ptr = strchr(buffer, '\n');
    if (ptr != NULL)
      *ptr = '\0';
    return 0;
  }
  while ((tmp_buffer = strtok(NULL, " ")) != NULL) {
    if (tmp_buffer[0] == '"') {
      ink_strlcpy(buffer, tmp_buffer, buffer_len);
      tmp_buffer = strtok(NULL, "\"");
      ink_strlcat(buffer, " ", buffer_len);
      ink_strlcat(buffer, tmp_buffer, buffer_len);
      ink_strlcat(buffer, "\"", buffer_len);
    } else
      ink_strlcpy(buffer, tmp_buffer, buffer_len);
    i++;
    if (i == location) {
      char *ptr = strchr(buffer, '\n');
      if (ptr != NULL)
        *ptr = '\0';
      return 0;
    }
  }
  //if we are - we didn't get the value...
  return -1;
}

#endif /* defined(linux) || defined(freebsd) || defined(darwin) */

#if defined(solaris)

int
getDefaultRouterViaNetstat(char *gateway)
{
  const int BUFLEN = 256;
  char command[BUFLEN], buffer[BUFLEN];
  int found_status = 1;

  // hme for UltraSpark5 le for Spark10
  snprintf(command, sizeof(command), "/usr/bin/netstat -rn | grep default | grep -v hme | grep -v le");

  FILE *fd = popen(command, "r");
  if (fd && fgets(buffer, BUFLEN, fd)) {
    char *p = buffer;
    char *gateway_ptr = gateway;
    while (*p && !isspace(*p))
      p++;                      // reach first white space
    while (*p && isspace(*p))
      p++;                      // skip white space
    while (*p && !isspace(*p))
      *(gateway_ptr++) = *(p++);
    *gateway_ptr = 0;
    found_status = 0;           // success
  }
  pclose(fd);
  return found_status;
}

// if error, return -1
// if found return 0
// if not found and no error, return 1
int
Net_GetDefaultRouter(char *router, size_t router_len)
{

  router[0] = '\0';
  FILE *fd;
  const int BUFFLEN = 80;
  char buffer[BUFFLEN];
  char command[BUFFLEN];
  const char *GATEWAY_CONFIG = "/etc/defaultrouter";

  if ((fd = fopen(GATEWAY_CONFIG, "r")) == NULL) {
    DPRINTF(("[Net_GetDefaultRouter] failed to open file \"%s\"\n", GATEWAY_CONFIG));
    return -1;
  }

  char *state = fgets(buffer, BUFFLEN, fd);
  if (state == NULL)            // empty file, try netstat, this may be because of DHCP as primary
    return getDefaultRouterViaNetstat(router);

  /* healthy /etc/defaultrouter file, parse through this */

  bool found = false;
  while (!feof(fd) && !found) {
    if (!isLineCommented(buffer))       // skip # and blank
      found = true;
    else
      fgets(buffer, BUFFLEN, fd);
  }
  fclose(fd);

  if (found) {                  // a healthy line
    makeStr(buffer);
    // found, but not in IP format, need to look up in /etc/inet/hosts
    if (!Net_IsValid_IP(buffer)) {
      snprintf(command, sizeof(command), "grep %s /etc/inet/hosts", buffer);
      FILE *fd = popen(command, "r");
      if (fd == NULL) {
        pclose(fd);
        DPRINTF(("[Net_GetDefaultRouter] failed to open pipe\n"));
        return -1;
      }
      if (fgets(buffer, BUFFLEN, fd)) {
        char *p = buffer;
        while (*p && !isspace(*p) && ((size_t)(p - buffer) < router_len))
          *(router++) = *(p++);
        *router = 0;
      }
      fclose(fd);
    } else {                    // found, already in ip format, just need to cpy it
      char *p = buffer;
      while (*p && !isspace(*p) && ((size_t)(p - buffer) < router_len))
        *(router++) = *(p++);
      *router = 0;
    }
    return 0;
  }
  return 1;                     // follow Linux behavior, return 1 if not found, with no error
}


int
Net_GetNetworkIntCount()
{
  const int BUFFLEN = 80;
  char buffer[BUFFLEN];
  /*
     get the list of network interfaces using pattern ^/etc/hostname.*[0-9]$
     need to get rid of any virtual IP definition which is defined with ':'
     Example of virtual IP definition is /etc/hostname.hme0:1
   */
  FILE *fd = popen("/bin/ls /etc/*hostname.*[0-9] | grep -v : | wc -l", "r");
  if (fd == NULL || fgets(buffer, BUFFLEN, fd) == NULL) {
    pclose(fd);
    DPRINTF(("[Net_GetNetworkIntCount] failed to open pipe\n"));
    return -1;
  }
  pclose(fd);
  return atoi(buffer);
}

int
Net_GetNetworkInt(int int_num, char *interface, size_t interface_len)
{
  interface[0] = 0;

  const int BUFFLEN = 200;
  char buffer[BUFFLEN];

  /*
     get the list of network interfaces using pattern ^/etc/hostname.*[0-9]$
     need to get rid of any virtual IP definition which is defined with ':'
     Example of virtual IP definition is /etc/hostname.hme0:1
   */

  FILE *fd = popen("/bin/ls /etc/*hostname.*[0-9] | grep -v :", "r");

  int i = 0;

  if (fd == NULL) {
    pclose(fd);
    DPRINTF(("[Net_GetNetworkInt] failed to open pipe\n"));
    return -1;
  }

  if (fgets(buffer, BUFFLEN, fd)) {
    while (!feof(fd) && i < int_num) {
      fgets(buffer, BUFFLEN, fd);
      i++;
    }

    if (i < int_num - 1) {
      pclose(fd);
      DPRINTF(("[Net_GetNetworkInt] failed to retrieved the interface\n"));
      return -1;
    }

    char *pos;
    if (strstr(buffer, "inkt")) // one of the inktomi backup file
      pos = buffer + strlen("/etc/inkt.save.hostname.");
    else
      pos = buffer + strlen("/etc/hostname.");

    if (pos != NULL) {
      ink_strlcpy(interface, pos, interface_len);
      if (interface[strlen(interface) - 1] == '\n') {
        interface[strlen(interface) - 1] = '\0';
      }
    }
  }
  pclose(fd);
  return 0;
}

int
Net_GetNIC_Status(char *interface, char *status, size_t status_len)
{
  const int BUFFLEN = 80;
  char buffer[BUFFLEN];
  char command[BUFFLEN];
  /* ifconfig -au shows all "up" interfaces in the system */
  snprintf(command, sizeof(command), "ifconfig -au | grep %s | wc -l", interface);
  FILE *fd = popen(command, "r");
  if (fd == NULL || fgets(buffer, BUFFLEN, fd) == NULL) {
    pclose(fd);
    DPRINTF(("[Net_GetNIC_Status] failed to open pipe\n"));
    return -1;
  }
  pclose(fd);
  if (atoi(buffer) == 1)
    ink_strlcpy(status, "up", status_len);
  else
    ink_strlcpy(status, "down", status_len);
  return 0;
}

int
Net_GetNIC_Start(char *interface, char *start, size_t start_len)
{
  const int PATHLEN = 200;
  char hostnamefile[PATHLEN];
  FILE *fd;
  snprintf(hostnamefile, sizeof(hostnamefile), "/etc/hostname.%s", interface);

  // if /etc/hostname.<interface> file exist, return true, else return false
  if ((fd = fopen(hostnamefile, "r")) != NULL)
    ink_strlcpy(start, "onboot", start_len);
  else
    ink_strlcpy(start, "not-onboot", start_len);
  return 0;
}

int
Net_GetNIC_Protocol(char *interface, char *protocol, size_t protocol_len)
{
  const int PATHLEN = 200;
  char dhcp_filename[PATHLEN];
  FILE *fd;
  snprintf(dhcp_filename, sizeof(dhcp_filename), "/etc/dhcp.%s", interface);

  if ((fd = fopen(dhcp_filename, "r")) == NULL)
    ink_strlcpy(protocol, "static", protocol_len);
  else
    ink_strlcpy(protocol, "dhcp", protocol_len);

  return 0;
}

int
parseIfconfig(char *interface, const char *keyword, char *value)
{

  const int BUFFLEN = 200;
  char buffer[BUFFLEN];
  char command[BUFFLEN];
  FILE *fd;

  // first check if the interface is attached
  snprintf(command, sizeof(command), "/sbin/ifconfig -a | grep %s", interface);
  fd = popen(command, "r");
  if (fd == NULL) {
    pclose(fd);
    DPRINTF(("[parseIfconfig ] failed to open pipe\n"));
    return -1;
  }
  if (fgets(buffer, BUFFLEN, fd) == NULL) {     // interface not found
    pclose(fd);
    return -1;
  }

  snprintf(command, sizeof(command), "/sbin/ifconfig %s", interface);
  fd = popen(command, "r");
  // can the first line
  if (fd == NULL || fgets(buffer, BUFFLEN, fd) == NULL) {
    pclose(fd);
    DPRINTF(("[parseIfconfig ] failed to open pipe\n"));
    return -1;
  }
  fgets(buffer, BUFFLEN, fd);
  char *pos = strstr(buffer, keyword);
  if (pos) {
    pos += strlen(keyword);
    while (*pos && !isspace(*pos))
      *(value++) = *(pos++);
    *value = 0;
  }
  return 0;
}


/*
  return number of heading matching bits for network address
 */
int
getMatchingBits(char *network, char *ip)
{

  unsigned int network_array[4];
  unsigned int ip_array[4];
  int count = 0, i = 0;

  sscanf(ip, "%u.%u.%u.%u", &ip_array[0], &ip_array[1], &ip_array[2], &ip_array[3]);
  sscanf(network, "%u.%u.%u.%u", &network_array[0], &network_array[1], &network_array[2], &network_array[3]);

  for (i = 0; i < 4; i++) {
    if (network_array[i] == ip_array[i])
      count += 8;
    else
      break;
  }

  if (count < 8 * 4) {
    unsigned char network_byte = network_array[i];
    unsigned char ip_byte = ip_array[i];

    int n = 8;                  // 8 bits
    unsigned char temp_mask = 1 << (n - 1);
    for (i = 0; i < n; i++) {
      if ((temp_mask & network_byte) == (temp_mask & ip_byte)) {
        count++;
      } else
        break;
      network_byte <<= 1;
      ip_byte <<= 1;
    }
  }
  return count;
}

int
Net_GetNIC_IP(char *interface, char *ip, size_t nic_ip_len)//FIXME: use nic_ip_len
{
  ip[0] = 0;               // bug 50628, initialize for null value
  int status = parseIfconfig(interface, "inet ", ip);
  if (status != 0) {            // in case of network down
    const int BUFFLEN = 1024;
    const int PATHLEN = 200;
    char command[PATHLEN], buffer[BUFFLEN], hostname_path[PATHLEN];
    char hostname[PATHLEN];
    FILE *fd;

    /* get hostname related to the nic */
    snprintf(hostname_path, sizeof(hostname_path), "/etc/hostname.%s", interface);
    if ((fd = fopen(hostname_path, "r")) == NULL) {     // could be in the backup file
      snprintf(hostname_path, sizeof(hostname_path), "/etc/inkt.save.hostname.%s", interface);
      if ((fd = fopen(hostname_path, "r")) == NULL) {
        DPRINTF(("[NET_GETNIC_IP] failed to open hostname configuration file"));
        return -1;
      }
    }
    if (fgets(hostname, PATHLEN, fd) == NULL) {
      DPRINTF(("[NET_GETNIC_IP] has empty %s file\n", hostname_path));
      return -1;
    }
    while (!feof(fd) && isLineCommented(hostname))      // skip # and blank
      fgets(hostname, PATHLEN, fd);

    if (hostname[0] == '\0') {
      DPRINTF(("[NET_GETNIC_IP] failed to get hostname"));
      return -1;
    }
    fclose(fd);

    /* lookup ip address entry in /etc/hosts file */
    makeStr(hostname);
    snprintf(command, sizeof(command), "grep %s /etc/inet/hosts", hostname);
    fd = popen(command, "r");
    if (fd && fgets(buffer, BUFFLEN, fd)) {
      char *p = buffer;
      while (*p && isspace(*p))
        p++;                    // skip white space
      char *tmp = ip;
      while (*p && !isspace(*p))
        *(tmp++) = *(p++);
      *tmp = 0;                 // finish filling ip
    }
    pclose(fd);
    status = 0;
  }
  return status;

}

int
Net_GetNIC_Netmask(char *interface, char *netmask, size_t netmask_len)
{
  netmask[0] = 0; // bug 50628, initialize for null value
  int status = parseIfconfig(interface, "netmask ", netmask);

  if (status != 0) {            // when network interface is down
    const int BUFFLEN = 1024;
    const int PATHLEN = 80;
    char ip_addr[PATHLEN], cur_network[PATHLEN], cur_netmask[PATHLEN];
    char buffer[BUFFLEN];
    char winnerMask[PATHLEN];
    int maxMatchingBits = 0;
    int curMatchingBits = 0;
    FILE *fd;


    status = Net_GetNIC_IP(interface, ip_addr, sizeof(ip_addr));
    if (status != 0) {
      DPRINTF(("[NET_GETNIC_NETMASK] failed to obtain ip address"));
      return -1;
    }
    // go through /etc/inet/netmasks
    if ((fd = fopen("/etc/inet/netmasks", "r")) == NULL) {
      DPRINTF(("[NET_GETNIC_NETMASK] failed to open netmasks file"));
      return -1;
    }

    if (fgets(buffer, BUFFLEN, fd) == NULL) {
      DPRINTF(("[NET_GETNIC_NETMASK] empty config file"));
      return -1;
    }

    while (!feof(fd)) {
      if (!isLineCommented(buffer)) {
        // parse network and netmask
        char *p = buffer;
        while (*p && isspace(*p))
          p++;                  // skip white space
        char *tmp = cur_network;
        while (*p && !isspace(*p)) {
          *(tmp++) = *(p++);
        }
        *tmp = 0;               // finish filling cur_network
        while (*p && isspace(*p))
          p++;                  // skip white space
        tmp = cur_netmask;
        while (*p && !isspace(*p)) {
          *(tmp++) = *(p++);
        }
        *tmp = 0;               // finish filling cur_netmask

        // find best match
        curMatchingBits = getMatchingBits(cur_network, ip_addr);
        if (curMatchingBits > maxMatchingBits) {
          maxMatchingBits = curMatchingBits;
          ink_strlcpy(winnerMask, cur_netmask, sizeof(winnerMask));
        }
      }
      fgets(buffer, BUFFLEN, fd);
    }
    if (maxMatchingBits > 0)
      ink_strlcpy(netmask, winnerMask, netmask_len);
    fclose(fd);
    status = 0;
  }

  if (!strstr(netmask, ".")) {  // not dotted format
    char temp[3];
    int oct[4];
    int j = 0;
    for (int i = 0; i < 4; i++, j += 2) {
      //       temp[0] = netmask[j+1];
      //temp[1] = netmask[j];
      temp[0] = netmask[j];
      temp[1] = netmask[j + 1];
      temp[2] = '\0';
      oct[i] = (int) strtol(temp, (char **) NULL, 16);
    }
    sprintf(netmask, "%d.%d.%d.%d", oct[0], oct[1], oct[2], oct[3]);
  }
  return status;

}

int
Net_GetNIC_Gateway(char *interface, char *gateway, size_t gateway_len)
{
  // command is netstat -rn | grep <interface name> | grep G
  // the 2nd column is the Gateway
  gateway[0] = 0;
  const int BUFFLEN = 200;
  char command[BUFFLEN];
  char buffer[BUFFLEN];
  snprintf(command, sizeof(command), "/usr/bin/netstat -rn | grep %s | grep G", interface);
  FILE *fd = popen(command, "r");

  if (fd && fgets(buffer, BUFFLEN, fd)) {       // gateway found
    char *p = buffer;
    while (*p && !isspace(*p))
      p++;                      // reach first white space
    while (*p && isspace(*p))
      p++;                      // skip white space
    while (*p && !isspace(*p))
      *(gateway++) = *(p++);
    *gateway = 0;
    return 0;
  } else                        // gateway not found
    return -1;
}

int
Net_SetNIC_Up(char *interface, char *onboot, char *protocol, char *ip, char *netmask, const char *gateway)
{
  int status;

  if (gateway == NULL)
    gateway = "";

  //   DPRINTF(("Net_SetNIC_Up:: interface %s onboot %s protocol %s ip %s netmask %s gateway %s\n", interface, onboot, protocol, ip, netmask, gateway));

  if (!Net_IsValid_Interface(interface))
    return -1;

  if (!Net_IsValid_IP(ip))
    return -1;

  if (!Net_IsValid_IP(netmask))
    return -1;

  const int BUFFLEN = 200;
  char old_ip[BUFFLEN], old_mask[BUFFLEN], old_gateway[BUFFLEN], default_gateway[BUFFLEN];

  Net_GetNIC_IP(interface, old_ip, sizeof(old_ip));
  Net_GetNIC_Netmask(interface, old_mask, sizeof(old_mask));
  Net_GetNIC_Gateway(interface, old_gateway, sizeof(old_gateway));
  Net_GetDefaultRouter(default_gateway, sizeof(default_gateway));

  if (strcmp(onboot, "onboot") == 0) {
    onboot[0] = '1';
  } else {
    onboot[0] = '0';
  }
  onboot[1] = '\0';

  if (strcmp(protocol, "dhcp") == 0) {
    protocol[0] = '0';
  } else {
    protocol[0] = '1';
  }
  protocol[1] = '\0';

  status = NetConfig_Action(NETCONFIG_INTF_UP, interface, protocol, ip, netmask, onboot, gateway,
                            old_ip, old_mask, old_gateway, default_gateway);

  if (status) {
    DPRINTF(("Net_SetNIC_Up: NetConfig_Action returned %d\n", status));
    return status;
  }

  return status;
}

bool
recordRegexCheck(const char *pattern, const char *value)
{
  pcre* regex;
  const char* error;
  int erroffset;
  int result;

  if (!(regex = pcre_compile(pattern, 0, &error, &erroffset, NULL)))
    return false;

  result = pcre_exec(regex, NULL, value, strlen(value), 0, 0, NULL, 0);
  pcre_free(regex);

  return (result != -1) ? true : false;
}

int
Time_SortTimezone()
{
  FILE *fp, *tmp;
  const char *zonetable = "/usr/share/zoneinfo/zone.tab";
  char buffer[1024];
  char *zone;

  fp = fopen(zonetable, "r");
  tmp = fopen("/tmp/zonetab.tmp", "w");
  if (fp == NULL || tmp == NULL) {
    DPRINTF(("[Time_SortTimezone] Can not open the file\n"));
    return -1;
  }
  fgets(buffer, 1024, fp);
  while (!feof(fp)) {
    if (buffer[0] != '#') {
      strtok(buffer, " \t");
      strtok(NULL, " \t");
      zone = strtok(NULL, " \t");
      if (zone[strlen(zone) - 1] == '\n') {
        zone[strlen(zone) - 1] = '\0';
      }
      fprintf(tmp, "%s\n", zone);
    }
    fgets(buffer, 1024, fp);
  }
  fclose(fp);
  fclose(tmp);
  remove("/tmp/zonetab");
  system("/bin/sort /tmp/zonetab.tmp > /tmp/zonetab");
  remove("/tmp/zonetab.tmp");

  return 0;
}


int
Time_SetNTP_Servers(bool restart, char *server)
{
  int status;

  DPRINTF(("[Time_SetNTP_Servers] restart %d, server %s\n", restart, server));
  status = TimeConfig_Action(TIMECONFIG_NTP, restart, server);

  return status;
}

int
Time_GetNTP_Server(char *server, int no)
{
  return 0;
}

int
Time_GetNTP_Status(char *status, size_t status_len)
{
  return 0;
}

int
Time_SetNTP_Off()
{
  return 0;
}

int
Sys_User_Root(int *old_euid)
{
  return 0;
}

int
Sys_User_Inktomi(int euid)
{
  return 0;
}

int
Sys_Grp_Root(int *old_egid)
{
  return 0;
}

int
Sys_Grp_Inktomi(int egid)
{
  return 0;
}

#endif /* defined(solaris) */
