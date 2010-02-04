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

/*
 *
 * NetConfig.cc
 *   Tool to configure the OS/network settings. To be exec'ed 
 * by management processes so they need not be running set uid root.
 *
 *
 */

#if (HOST_OS == darwin) || (HOST_OS == freebsd)
/* This program has not been ported to these operating systems at all,
 * and even the linux one is insanely distro specific.
 */
int main()
{
  return 255;
};
#endif

#if (HOST_OS == linux)

#include <ctype.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>

#include <sys/types.h>
#include <sys/wait.h>

#include <netinet/in.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#include <sys/un.h>
struct ifafilt;
#include <net/if.h>

#include <sys/ioctl.h>

#include <net/if.h>
#include <netinet/in.h>
#include <netinet/if_ether.h>
#include <sys/ioctl.h>
#include <sys/raw.h>
#include <signal.h>
#include <errno.h>
#include <inktomi++.h>

#include "ink_bool.h"

// TODO: consolidate location of these defaults
#define DEFAULT_ROOT_DIRECTORY            PREFIX
#define DEFAULT_LOCAL_STATE_DIRECTORY     "var/trafficserver"
#define DEFAULT_SYSTEM_CONFIG_DIRECTORY   "etc/trafficserver"
#define DEFAULT_LOG_DIRECTORY             "var/log/trafficserver"
#define DEFAULT_TS_DIRECTORY_FILE         PREFIX "/etc/traffic_server"

#define UP_INTERFACE     0
#define DOWN_INTERFACE   1
#define HOSTNAME         2
#define IPADDR           3
#define NETMASK          4
#define DNSSERVER        5
#define GATEWAY          6

#define HOSTNAME_PATH "/etc/sysconfig/network"
#define ETC_HOSTNAME_PATH "/etc/hosts"
#define GATEWAY_CONFIG "/etc/sysconfig/network"
#define IFCONFIG "/sbin/ifconfig"
#define MV_BINARY "/bin/mv"
#define NIC_CONFIG "/etc/sysconfig/network-scripts/"
#define ROUTE_BINARY "/sbin/route"
#define IF_UP "/sbin/ifup"
#define DOMAIN_CONFIG "/etc/resolv.conf"
#define SEARCH_DOMAIN_CONFIG "/etc/resolv.conf"
#define DNS_CONFIG "/etc/resolv.conf"
#define MRTG_PATH "ui/mrtg"
#define MODULE_CONFIG "/etc/modules.conf"
#define RMMOD_BINARY "/sbin/rmmod"
#define INSMOD_BINARY "/sbin/insmod"
#define SNMP_PATH "/etc/snmp/snmpd.conf"
#define TS_SNMP_PATH "snmpd.cnf"

int set_hostname(char *hostname, char *old_hostname, char *ip_addr);
int set_gateway(char *ip_address, char *old_ip_address);
int set_dns_server(char *dns_server_ips);
int set_domain_name(char *domain_name);
int set_search_domain(char *search_name);
int up_interface(char *nic_name, bool static_ip, char *ip, char *netmask, bool onboot, char *gateway_ip);
int down_interface(char *nic_name);
int setSNMP(char *sys_location, char *sys_contact, char *sys_name, char *authtrapenable, char *trap_community,
            char *trap_host);

// mask is valid if the bit values are heading 1's following by trailing 0's
// or all 0's
bool
isValidMask(char *mask)
{
  unsigned int mask_array[4];
  // coverity[secure_coding]
  sscanf(mask, "%u.%u.%u.%u", &mask_array[0], &mask_array[1], &mask_array[2], &mask_array[3]);
  bool zero_started = false;
  unsigned char c;

  for (int i = 0; i < 4; i++) {
    if (!zero_started) {
      c = (unsigned char) mask_array[i];
      if (c == 0x00 || c == 0x80 || c == 0xC0 || c == 0xE0 || c == 0xF0 || c == 0xF8 || c == 0xFC || c == 0xFE) // all valid heading 1 and trailing 0 byte
        zero_started = true;    // that has 0 in it
      else if (c != 0xFF)       // 0xFF is also a valid byte
        return false;
    } else {
      if (mask_array[i] != 0)
        return false;
    }
  }
  return true;
}

/*
  for each bit, if mask bit is 1, return bit is assigned to value of ip bit 
                if mask bit is 0, return bit is assigned to 1
 */
char
makeBroadcastHelper(char ip, char mask)
{
  int n = 8;
  unsigned char temp_mask = 1 << (n - 1);
  unsigned char one_mask = 0x01;        // 0000 0001
  unsigned char zero_mask = 0x00;       //0000 0000
  unsigned char return_val = 0x00;

  for (int i = 0; i < n; i++) {
    if ((temp_mask & mask) >> (n - 1)) {        // mask_bit == 1
      // mask_bit == 1, copy ip_bit to return bit
      if ((ip & mask) >> (n - 1))       //one
        return_val = return_val | one_mask;
      else                      // zero
        return_val = return_val | zero_mask;
    } else {                    // set return bit to 1
      return_val = return_val | one_mask;
    }
    mask <<= 1;
    ip <<= 1;
    if (i < n - 1)
      return_val <<= 1;
  }
  return return_val;
}

int
getBroadCastAddr(char *ip, char *netmask, char *broadcast, const int broadcast_size)
{
  unsigned int ip_array[4];
  unsigned int mask_array[4];
  unsigned char broadcast_array[4];

  // coverity[secure_coding]
  sscanf(ip, "%u.%u.%u.%u", &ip_array[0], &ip_array[1], &ip_array[2], &ip_array[3]);
  // coverity[secure_coding]
  sscanf(netmask, "%u.%u.%u.%u", &mask_array[0], &mask_array[1], &mask_array[2], &mask_array[3]);

  if (!isValidMask(netmask)) {
    broadcast[0] = '\0';        // setting to NULL
    perror("[net_config] invalid netmask, unable to calculate broadcast addr");
    return 1;
  }

  for (int i = 0; i < 4; i++)
    broadcast_array[i] = makeBroadcastHelper((unsigned char) ip_array[i], (unsigned char) mask_array[i]);

  // This format string used to be explicitly null terminated, makes no sense. /leif
  snprintf(broadcast, broadcast_size, "%u.%u.%u.%u", broadcast_array[0], broadcast_array[1], broadcast_array[2],
           broadcast_array[3]);

  return 0;
}

// Disable the interface and assign null to the devices IP, NM, GW, BC....
int
disable_interface(char *nic_name)
{
  char nic_path[1024], nic_path_new[1024], buf[1024];
  FILE *fp;
  FILE *fp1;
  const char *ifconfig_binary = IFCONFIG;
  const char *mv_binary = MV_BINARY;
  int status;
  pid_t pid;
  int str_len = strlen("GATEWAY");

  down_interface(nic_name);

  snprintf(nic_path, sizeof(nic_path), "%s/ifcfg-%s", NIC_CONFIG, nic_name);
  if ((fp = fopen(nic_path, "r")) == NULL) {
    if ((fp = fopen(nic_path, "a+")) == NULL) {
      perror("[net_config] failed to open nic configuration file");
      return 1;
    }
  }
  snprintf(nic_path_new, sizeof(nic_path_new), "%s/ifcfg-%s.new", NIC_CONFIG, nic_name);
  if ((fp1 = fopen(nic_path_new, "w")) == NULL) {
    perror("[net_config] failed to open new nic configuration file");
    return 1;
  }

  NOWARN_UNUSED_RETURN(fgets(buf, 1024, fp));

  while (!feof(fp)) {
    if (strcasestr(buf, "DEVICE")) {
      fprintf(fp1, "DEVICE=%s\n", nic_name);
    } else if (strcasestr(buf, "ONBOOT")) {
      fprintf(fp1, "ONBOOT=no\n");
    } else if (strcasestr(buf, "BOOTPROTO")) {
      fprintf(fp1, "BOOTPROTO=static\n");
    } else if (strcasestr(buf, "IPADDR")) {
      // fprintf(fp1,"");
      ;
    } else if (strcasestr(buf, "NETMASK")) {
      // fprintf(fp1,"");
      ;
    } else if (strcasestr(buf, "NETWORK")) {
      // fprintf(fp1,"");
      ;
    } else if (strcasestr(buf, "GATEWAY") &&
               ((strncmp((buf + str_len), "=", 1) == 0) || (strncmp((buf + str_len), " ", 1) == 0))) {
      // fprintf(fp1,"");
      ;
    } else if (strcasestr(buf, "BROADCAST")) {
      // fprintf(fp1,"");
      ;
    } else
      fputs(buf, fp1);

    NOWARN_UNUSED_RETURN(fgets(buf, 1024, fp));
  }

  fclose(fp);
  fclose(fp1);

  if ((pid = fork()) < 0) {
    exit(1);
  } else if (pid > 0) {
    wait(&status);
  } else {
    int res;
    res = execl(mv_binary, "mv", nic_path_new, nic_path, (char *) NULL);
    if (res != 0) {
      perror("[net_config] mv of new nic-cfg file failed ");
    }
    _exit(res);
  }

  if ((pid = fork()) < 0) {
    exit(1);
  } else if (pid > 0) {
    wait(&status);
  } else {

    int res;
    res = execl(ifconfig_binary, "ifconfig", nic_name, "down", (char *) NULL);

    if (res != 0) {
      perror("[net_config] ifconfig failed ");
    }
    _exit(res);
  }

  return 0;
}


int
set_interface(char *nic_name, char *ip, char *netmask, bool onboot, char *gateway_ip,
              char *broadcast_addr, char *network_addr)
{
  char nic_path[1024], nic_path_new[1024], buf[1024], temp_buf[1024], old_gateway_ip[1024], gateway_path[1024];
  char default_gateway_ip[1024];
  char *pos;
  FILE *fp;
  FILE *fp1;
  const char *mv_binary = MV_BINARY;
  const char *route_binary = ROUTE_BINARY;
  int status;
  pid_t pid;
  bool nic_flag = false, ip_flag = false, netmask_flag = false, onboot_flag = false, gateway_flag =
    false, bootproto_flag = false, gateway_delete = false, network_flag = false;

  snprintf(nic_path, sizeof(nic_path), "%s/ifcfg-%s", NIC_CONFIG, nic_name);
  if ((fp = fopen(nic_path, "r")) == NULL) {
    if ((fp = fopen(nic_path, "a+")) == NULL) {
      perror("[net_config] failed to open nic configuration file");
      return 1;
    }
  }
  snprintf(nic_path_new, sizeof(nic_path_new), "%s/ifcfg-%s.new", NIC_CONFIG, nic_name);
  if ((fp1 = fopen(nic_path_new, "w")) == NULL) {
    perror("[net_config] failed to open new nic configuration file");
    return 1;
  }

  NOWARN_UNUSED_RETURN(fgets(buf, sizeof(buf), fp));

  while (!feof(fp)) {
    if (strcasestr(buf, "DEVICE")) {
      fprintf(fp1, "DEVICE=%s\n", nic_name);
      nic_flag = true;
    } else if (strcasestr(buf, "ONBOOT")) {
      if (onboot)
        fprintf(fp1, "ONBOOT=yes\n");
      else
        fprintf(fp1, "ONBOOT=no\n");
      onboot_flag = true;
    } else if (strcasestr(buf, "BOOTPROTO")) {
      fprintf(fp1, "BOOTPROTO=static\n");
      bootproto_flag = true;
    } else if (strcasestr(buf, "IPADDR")) {
      fprintf(fp1, "IPADDR=%s\n", ip);
      ip_flag = true;
    } else if (strcasestr(buf, "NETMASK")) {
      fprintf(fp1, "NETMASK=%s\n", netmask);
      netmask_flag = true;
    } else if (strcasestr(buf, "GATEWAY") && (strcasestr(buf, "GATEWAYDEV")) == NULL) {
      if ((gateway_ip != NULL) && (strlen(gateway_ip) != 0)) {
        fprintf(fp1, "GATEWAY=%s\n", gateway_ip);
      }
      ink_strncpy(temp_buf, buf, sizeof(temp_buf));
      gateway_flag = true;
    } else if (strcasestr(buf, "BROADCAST")) {
      if (broadcast_addr != NULL && strlen(broadcast_addr) > 0)
        fprintf(fp1, "BROADCAST=%s\n", broadcast_addr);
      else                      // leave the old value as it is
        fputs(buf, fp1);
    } else if (strcasestr(buf, "NETWORK")) {
      if (network_addr != NULL && strlen(network_addr) > 0)
        fprintf(fp1, "NETWORK=%s\n", network_addr);
      // else, better delete it then leave the corrupted value around
      network_flag = true;
    } else
      fputs(buf, fp1);

    NOWARN_UNUSED_RETURN(fgets(buf, 1024, fp));
  }

  //now check which of the options is not set, and set it...
  if (!nic_flag)
    fprintf(fp1, "DEVICE=%s\n", nic_name);
  if (onboot && !onboot_flag)
    fprintf(fp1, "ONBOOT=yes\n");
  else if (!onboot_flag)
    fprintf(fp1, "ONBOOT=no\n");
  if (!bootproto_flag)
    fprintf(fp1, "BOOTPROTO=none\n");
  if (!ip_flag)
    fprintf(fp1, "IPADDR=%s\n", ip);
  if (!netmask_flag)
    fprintf(fp1, "NETMASK=%s\n", netmask);
  if (!gateway_flag && gateway_ip != NULL && (strlen(gateway_ip) != 0))
    fprintf(fp1, "GATEWAY=%s\n", gateway_ip);
  if (!network_flag && network_addr != NULL && (strlen(network_addr) > 0))
    fprintf(fp1, "NETWORK=%s\n", network_addr);
  fclose(fp);
  fclose(fp1);

  if ((pid = fork()) < 0) {
    exit(1);
  } else if (pid > 0) {
    wait(&status);
  } else {
    int res;
    res = execl(mv_binary, "mv", nic_path_new, nic_path, (char *) NULL);
    if (res != 0) {
      perror("[net_config] mv of new nic-cfg file failed ");
    }
    _exit(res);
  }

  //Now, delete previous gateway if necessary - see BZ47979
  if (gateway_flag) {           //now we know we had a previous gateway, so we better act

    pos = strstr(temp_buf, "=");
    if (pos != NULL) {
      pos++;
      ink_strncpy(old_gateway_ip, pos, sizeof(old_gateway_ip));
      if (old_gateway_ip[strlen(old_gateway_ip) - 1] == '\n') {
        old_gateway_ip[strlen(old_gateway_ip) - 1] = '\0';
      }
    }
    // we have the old gateway ip - now delete it from the route table

    // Here we should be careful not to delete the default general gateway
    // This can happen when we have the same gateway for eth0 and as the
    // default general gateway

    if (strcmp(nic_name, "eth0") == 0) {        // this is the special case
      // now check whether the old gateway ip and the general default one are the same
      //first find the current general gateway IP
      snprintf(gateway_path, sizeof(gateway_path), "%s", GATEWAY_CONFIG);
      if ((fp = fopen(gateway_path, "r")) == NULL) {
        if ((fp = fopen(gateway_path, "a+")) == NULL) {
          perror("[net_config] failed to open gateway configuration file in set_interface");
          return 1;
        }
      }
      bool found = false;
      NOWARN_UNUSED_RETURN(fgets(buf, 1024, fp));
      while (!feof(fp)) {
        if (strcasestr(buf, "GATEWAY") && (strcasestr(buf, "GATEWAYDEV") == NULL)) {
          ink_strncpy(temp_buf, buf, sizeof(temp_buf));
          found = true;
        }
        NOWARN_UNUSED_RETURN(fgets(buf, 1024, fp));
      }
      fclose(fp);
      if (found) {              //gateway was defined - let's compare to eth0 new gateway
        pos = strstr(temp_buf, "=");
        if (pos != NULL) {
          pos++;
          ink_strncpy(default_gateway_ip, pos, sizeof(default_gateway_ip));
          if (default_gateway_ip[strlen(default_gateway_ip) - 1] == '\n') {
            default_gateway_ip[strlen(default_gateway_ip) - 1] = '\0';
          }
        }
        // finally, if eth0 gateway doesn't match the default one - delete the gateway!!
        if (strcmp(default_gateway_ip, old_gateway_ip) != 0) {
          gateway_delete = true;
        }
      }
    } else {
      gateway_delete = true;
    }

    if (gateway_delete) {       //either it is not eth0, or it is eth0 but old gateway doesn't match to the general gateway
      if ((pid = fork()) < 0) {
        exit(1);
      } else if (pid > 0) {
        wait(&status);
      } else if (old_gateway_ip != NULL) {
        int res;
        res = execl(route_binary, "route", "del", "default", "gateway", old_gateway_ip, "dev", nic_name, (char *) NULL);
        if (res != 0) {
          perror("[net_config] del NIC's gateway failed ");
        }
        _exit(res);
      }
    }
  }
  return 0;

}

int
set_interface_down(char *nic_name)
{
  char buf[1024], nic_path[1024], nic_path_new[1024];
  FILE *fp;
  FILE *fp1;
  const char *mv_binary = MV_BINARY;
  int status;
  pid_t pid;

  snprintf(nic_path, sizeof(nic_path), "%s/ifcfg-%s", NIC_CONFIG, nic_name);
  if ((fp = fopen(nic_path, "r")) == NULL) {
    if ((fp = fopen(nic_path, "a+")) == NULL) {
      perror("[net_config] failed to open nic configuration file for down int");
      return 1;
    }
  }
  snprintf(nic_path_new, sizeof(nic_path_new), "%s/ifcfg-%s.new", NIC_CONFIG, nic_name);
  if ((fp1 = fopen(nic_path_new, "w")) == NULL) {
    perror("[net_config] failed to open new nic configuration file for down int");
    return 1;
  }

  NOWARN_UNUSED_RETURN(fgets(buf, sizeof(buf), fp));
  while (!feof(fp)) {
    if (strcasestr(buf, "ONBOOT")) {
      fprintf(fp1, "ONBOOT=no\n");
    } else
      fputs(buf, fp1);
    NOWARN_UNUSED_RETURN(fgets(buf, sizeof(buf), fp));
  }
  fclose(fp);
  fclose(fp1);

  if ((pid = fork()) < 0) {
    exit(1);
  } else if (pid > 0) {
    wait(&status);
  } else {                      /* Exec the up */
    int res;
    res = execl(mv_binary, "mv", nic_path_new, nic_path, (char *) NULL);
    if (res != 0) {
      perror("[net_config] mv of new nic-cfg file failed ");
    }
    _exit(res);
  }
  return 0;
}

int
set_interface_dhcp(char *nic_name, bool boot)
{
  char nic_path[1024], nic_path_new[1024];
  //FILE *fp;
  FILE *fp1;
  const char *mv_binary = MV_BINARY;
  const char *if_up = IF_UP;
  int status;
  pid_t pid;

  snprintf(nic_path, sizeof(nic_path), "%s/ifcfg-%s", NIC_CONFIG, nic_name);
  //if (fp = fopen(nic_path,"r") == NULL) {
  //perror("[net_config] failed to open nic configuration file");
  //return 1;
  //}
  snprintf(nic_path_new, sizeof(nic_path_new), "%s/ifcfg-%s.new", NIC_CONFIG, nic_name);
  if ((fp1 = fopen(nic_path_new, "w")) == NULL) {
    perror("[net_config] failed to open new nic configuration file");
    return 1;
  }
  fprintf(fp1, "DEVICE=%s\n", nic_name);
  if (boot)
    fprintf(fp1, "ONBOOT=yes\n");
  else
    fprintf(fp1, "ONBOOT=no\n");
  fprintf(fp1, "BOOTPROTO=dhcp\n");
  fclose(fp1);

  if ((pid = fork()) < 0) {
    exit(1);
  } else if (pid > 0) {
    wait(&status);
  } else {
    int res;
    res = execl(mv_binary, "mv", nic_path_new, nic_path, (char *) NULL);
    if (res != 0) {
      perror("[net_config] mv of new nic-cfg file failed ");
    }
    _exit(res);
  }

  //restart the dev for DHCP
  if ((pid = fork()) < 0) {
    exit(1);
  } else if (pid > 0) {
    wait(&status);
  } else {
    int res;
    res = execl(if_up, "ifup", nic_name, (char *) NULL);
    if (res != 0) {
      perror("[net_config] ifup of DHCP dev has failed ");
    }
    _exit(res);
  }
  return 0;
}

/*
   converts a dot notaion ip address in a null terminated string to an unsigned int
*/
unsigned int
ipDot2int(char *ipAddr)
{
  unsigned int one;
  unsigned int two;
  unsigned int three;
  unsigned int four;
  unsigned int total;

  // coverity[secure_coding]
  sscanf(ipAddr, "%u.%u.%u.%u", &one, &two, &three, &four);

  total = 0;
  total += (one << 24);
  total += (two << 16);
  total += (three << 8);
  total += four;
  return total;
}

/*
   converts unsigned int ip address to a dot notaion ip address in a null terminated string
*/
unsigned int
ipInt2Dot(const int ip, char *ipAddr, const int ipAddrSize)
{

  const unsigned int FIRST_OCTET = 0xFF000000;
  const unsigned int SECOND_OCTET = 0x00FF0000;
  const unsigned int THIRD_OCTET = 0x0000FF00;
  const unsigned int FOURTH_OCTET = 0x000000FF;

  //need 3 + 1 + 3 + 1+ 3 + 1 + 3 = 15 chars, +1 for the null
  char buff[17];
  snprintf(buff, sizeof(buff), "%d.%d.%d.%d",
           (ip & FIRST_OCTET) >> 24, (ip & SECOND_OCTET) >> 16, (ip & THIRD_OCTET) >> 8, (ip & FOURTH_OCTET));
  ink_strncpy(ipAddr, buff, ipAddrSize);
  return 0;

}

/* getNetworkNumber
      network number = (ip & netmask);
 */
int
getNetworkNumber(char *ip, char *netmask, char *network_number, const int network_number_size)
{
  unsigned int network = (ipDot2int(ip) & ipDot2int(netmask));
  return ipInt2Dot(network, network_number, network_number_size);
}

/*
 * up_interface(...)
 *   This function will attempt to bring up and create an interface.
 */

int
up_interface(char *nic_name, bool static_ip, char *ip, char *netmask, bool onboot, char *gateway_ip)
{
  int status;
  pid_t pid;
  const char *ifconfig_binary = IFCONFIG;
  const char *route_binary = ROUTE_BINARY;
  char hostname[1024], buf[1024];
  char etc_hostname_path[1024], etc_hostname_path_new[1024];
  FILE *fp, *fp1;
  char *mv_binary = MV_BINARY;
  bool hostname_flag = false;
  char broadcast[17];
  char network_addr[17];

  if (static_ip) {

    /* This is changed due to BZ47979
       // We delete the previous gateway in set_interface_up
       // first delete pervious default
       if((pid = fork()) < 0) {
       exit(1);
       } else if(pid > 0) {
       wait(&status);
       } else if (gateway_ip != NULL) {
       int res;
       res = execl(route_binary, "route", "del", "default", "dev", nic_name, NULL);
       if(res != 0) {
       perror("[net_config] del default route failed ");
       }
       _exit(res);
       }

     */
    // BUG 48763 update braodcast addr
    int validBroadcastAddr = getBroadCastAddr(ip, netmask, broadcast, sizeof(broadcast));

    if ((pid = fork()) < 0) {
      exit(1);
    } else if (pid > 0) {
      wait(&status);
    } else {

      int res;
      if (validBroadcastAddr == 0)      // success code is 0
        res = execl(ifconfig_binary, "ifconfig", nic_name, ip, "netmask", netmask,
                    "broadcast", broadcast, (char *) NULL);
      else
        res = execl(ifconfig_binary, "ifconfig", nic_name, ip, "netmask", netmask, (char *) NULL);

      if (res != 0) {
        perror("[net_config] ifconfig failed ");
      }
      _exit(res);
    }
    if (gateway_ip != NULL && (strlen(gateway_ip) != 0)) {
      if ((pid = fork()) < 0) {
        exit(1);
      } else if (pid > 0) {
        wait(&status);
      } else {

        int res;
        res = execl(route_binary, "route", "add", "default", "gateway", gateway_ip, "dev", nic_name, (char *) NULL);
        //system(test);
        if (res != 0) {
          perror("[net_config] add route failed ");
        }
        _exit(res);
      }

    }

    getNetworkNumber(ip, netmask, network_addr, sizeof(network_addr));

    if (set_interface(nic_name, ip, netmask, onboot, gateway_ip, broadcast, network_addr) != 0) {
      perror("[net_config] set interface for boot failed");
    } else {

      //everytime we setup an interface, we need to update /etc/hosts as well - see BZ38199

      if (gethostname(hostname, 256) != 0) {
        perror("[net_config] couldn't get hostname to update /etc/hosts");
        return 1;
      } else {
        snprintf(etc_hostname_path, sizeof(etc_hostname_path), "%s", ETC_HOSTNAME_PATH);
        if ((fp = fopen(etc_hostname_path, "r")) == NULL) {
          if ((fp = fopen(etc_hostname_path, "a+")) == NULL) {
            perror("[net_config] failed to open /etc/hosts file");
            return 1;
          }
        }
        snprintf(etc_hostname_path_new, sizeof(etc_hostname_path_new), "%s.new", ETC_HOSTNAME_PATH);
        if ((fp1 = fopen(etc_hostname_path_new, "w")) == NULL) {
          perror("[net_config] failed to open new /etc/hosts.new file");
          return 1;
        }

        char host_alias[1024], *first_dot;
        int no_alias = 0;
        ink_strncpy(host_alias, hostname, sizeof(host_alias));
        first_dot = strchr(host_alias, '.');
        if (first_dot) {
          *first_dot = '\0';
        } else {
          no_alias = 1;
        }

        NOWARN_UNUSED_RETURN(fgets(buf, sizeof(buf), fp));
        while (!feof(fp)) {
          if (strcasestr(buf, hostname)) {
            //now create the new entry for the new host 
            buf[0] = '\0';
            if (no_alias) {
              snprintf(buf, sizeof(buf), "%s %s\n", ip, hostname);
            } else {
              snprintf(buf, sizeof(buf), "%s %s %s\n", ip, hostname, host_alias);
            }
            fprintf(fp1, "%s", buf);
            hostname_flag = true;
          } else {
            fputs(buf, fp1);
          }
          NOWARN_UNUSED_RETURN(fgets(buf, sizeof(buf), fp));
        }

        //add the entry if it didn't exist before
        if (hostname_flag == false) {
          buf[0] = '\0';
          if (no_alias) {
            snprintf(buf, sizeof(buf), "%s %s\n", ip, hostname);
          } else {
            snprintf(buf, sizeof(buf), "%s %s %s\n", ip, hostname, host_alias);
          }
          fprintf(fp1, "%s", buf);
        }
        fclose(fp);
        fclose(fp1);

        if ((pid = fork()) < 0) {
          exit(1);
        } else if (pid > 0) {
          wait(&status);
        } else {
          int res;
          res = execl(mv_binary, "mv", etc_hostname_path_new, etc_hostname_path, (char *) NULL);
          if (res != 0) {
            perror("[net_config] mv of new /etc/hosts file failed ");
          }
          _exit(res);
        }
      }                         //else for gethostname
    }                           //else for set_interface
  } else {                      // static ip
    // down the interface - perhaps it was active before as static 
    // THIS IS FOR DHCP - WE NO LONGER SUPPORT THIS!!!
    down_interface(nic_name);
    if (set_interface_dhcp(nic_name, onboot) != 0) {
      perror("[net_config] set interface for dhcp failed");
    }

  }
  return 0;
}                               /* End up_interface */

/*
 * down_interface(...)
 *   This function will attempt to bring down and remove an interface.
 */
int
down_interface(char *nic_name)
{
  int status;
  pid_t pid;
  char *ifconfig_binary = IFCONFIG;

  if ((pid = fork()) < 0) {
    exit(1);
  } else if (pid > 0) {
    wait(&status);
  } else {
    int res = execl(ifconfig_binary, "ifconfig", nic_name, "down", (char *) NULL);
    if (res != 0) {
      perror("[net_confg] couldn't bring interface down");
    }
    _exit(res);
  }
  if (set_interface_down(nic_name) != 0) {
    perror("[net_config] set interface down for boot failed");
  }

  return 0;
}                               /* End down_interface */


//FIXME - this is a hack for hard coded hostname in mrtg html files

int
mrtg_hostname_change(char *hostname, char *old_hostname)
{
  char buf[1024], tmp_buf[1024], mrtg_dir_path[1024], mrtg_path[1024], mrtg_path_new[1024], *index;
  FILE *fp;
  FILE *fp1;
  char *mv_binary = MV_BINARY;
  int res;
  int status;
  pid_t pid;
  struct dirent **namelist;
  int n, pos;

  snprintf(mrtg_dir_path, sizeof(mrtg_dir_path), "%s", MRTG_PATH);
  n = scandir(MRTG_PATH, &namelist, 0, alphasort);
  if (n < 0) {
    perror("[net_config] scandir failed");
    return 1;
  }
  while (n--) {
    //printf("%s\n", namelist[n]->d_name);
    if (strcasestr(namelist[n]->d_name, ".html") != NULL) {
      snprintf(mrtg_path, sizeof(mrtg_path), "%s/%s", mrtg_dir_path, namelist[n]->d_name);
      if ((fp = fopen(mrtg_path, "r")) == NULL) {
        perror("[net_config] failed to open mrtg file");
        return 1;
      }
      snprintf(mrtg_path_new, sizeof(mrtg_path_new), "%s/%s.new", mrtg_dir_path, namelist[n]->d_name);
      if ((fp1 = fopen(mrtg_path_new, "w")) == NULL) {
        perror("[net_config] failed to open new mrtg file");
        return 1;
      }
      NOWARN_UNUSED_RETURN(fgets(buf, 1024, fp));
      while (!feof(fp)) {
        if ((index = (strcasestr(buf, old_hostname))) != NULL) {
          pos = strlen(buf) - strlen(index);
          ink_strncpy(tmp_buf, buf, pos);
          fprintf(fp1, "%s", tmp_buf);
          fputs(hostname, fp1);
          index += strlen(old_hostname);
          ink_strncpy(tmp_buf, index, sizeof(tmp_buf));
          fprintf(fp1, "%s\n", tmp_buf);
        } else
          fputs(buf, fp1);
        NOWARN_UNUSED_RETURN(fgets(buf, sizeof(buf), fp));
      }
      fclose(fp);
      fclose(fp1);

      if ((pid = fork()) < 0) {
        exit(1);
      } else if (pid > 0) {
        wait(&status);
      } else {
        // printf("MV: %s %s %s \n",mv_binary,mrtg_path_new, mrtg_path); 
        res = execl(mv_binary, "mv", mrtg_path_new, mrtg_path, (char *) NULL);
        if (res != 0) {
          perror("[net_config] mv of new mrtg file failed ");
        }
        _exit(res);
      }
    }
  }

  return 0;
}


int
set_hostname(char *hostname, char *old_hostname, char *ip_addr)
{

  char buf[1024], hostname_path[1024], hostname_path_new[1024];
  char etc_hostname_path[1024], etc_hostname_path_new[1024], ip_address[1024];
  FILE *fp;
  FILE *fp1;
  char *mv_binary = MV_BINARY;
  int res;
  int status;
  pid_t pid;
  bool hostname_flag = false;

  //first do the simple stuff - change hostname using API
  res = sethostname(hostname, strlen(hostname));

  if (res < 0) {
    perror("[net_config] OS sethostname failed");
    return res;
  }
  // everything is OK, now setup the machine hostname for next reboot

  //FIXME - we fix MRTG here although this is a hack!!
  if (mrtg_hostname_change(hostname, old_hostname) != 0)
    perror("[net_config] failed to change mrtg hostname");

  snprintf(hostname_path, sizeof(hostname_path), "%s", HOSTNAME_PATH);
  if ((fp = fopen(hostname_path, "r")) == NULL) {
    if ((fp = fopen(hostname_path, "a+")) == NULL) {
      perror("[net_config] failed to open hostname configuration file");
      return 1;
    }
  }
  snprintf(hostname_path_new, sizeof(hostname_path_new), "%s.new", HOSTNAME_PATH);
  if ((fp1 = fopen(hostname_path_new, "w")) == NULL) {
    perror("[net_config] failed to open new hostname configuration file");
    return 1;
  }

  NOWARN_UNUSED_RETURN(fgets(buf, sizeof(buf), fp));
  while (!feof(fp)) {
    if (strcasestr(buf, "HOSTNAME")) {
      fprintf(fp1, "HOSTNAME=%s\n", hostname);
      hostname_flag = true;
    } else {
      fputs(buf, fp1);
    }
    NOWARN_UNUSED_RETURN(fgets(buf, sizeof(buf), fp));
  }

  if (!hostname_flag)
    fprintf(fp1, "HOSTNAME=%s\n", hostname);
  fclose(fp);
  fclose(fp1);

  if ((pid = fork()) < 0) {
    exit(1);
  } else if (pid > 0) {
    wait(&status);
  } else {
    int res;
    //printf("MV: %s %s %s \n",mv_binary,hostname_path_new, hostname_path); 
    //snprintf(buf, sizeof(buf), "/bin/mv %s %s", hostname_path_new, hostname_path); 
    //system(buf);
    res = execl(mv_binary, "mv", hostname_path_new, hostname_path, (char *) NULL);
    if (res != 0) {
      perror("[net_config] mv of new hostname config file failed ");
    }
    _exit(res);
  }


  // This was added to fix the /etc/hosts problem. See BZ38199
  snprintf(etc_hostname_path, sizeof(etc_hostname_path), "%s", ETC_HOSTNAME_PATH);
  if ((fp = fopen(etc_hostname_path, "r")) == NULL) {
    if ((fp = fopen(etc_hostname_path, "a+")) == NULL) {
      perror("[net_config] failed to open /etc/hosts file");
      return 1;
    }
  }
  snprintf(etc_hostname_path_new, sizeof(etc_hostname_path_new), "%s.new", ETC_HOSTNAME_PATH);
  if ((fp1 = fopen(etc_hostname_path_new, "w")) == NULL) {
    perror("[net_config] failed to open new /etc/hosts.new file");
    return 1;
  }
  //Get the alias from the hostname
  char host_alias[1024], *first_dot;
  ink_strncpy(host_alias, hostname, sizeof(host_alias));
  first_dot = strchr(host_alias, '.');
  *first_dot = '\0';

  hostname_flag = false;
  NOWARN_UNUSED_RETURN(fgets(buf, 1024, fp));
  while (!feof(fp)) {
    if (strcasestr(buf, old_hostname)) {
      ink_strncpy(ip_address, strtok(buf, " \t"), sizeof(ip_address));  //here we change buf!!
      if (ip_address == NULL) { //something is wrong with this file - abort
        perror("[net_config] /etc/hosts format is wrong - not changing it!!");
        return -1;
      }
      //now create the new entry for the new host       
      buf[0] = '\0';
      snprintf(buf, sizeof(buf), "%s %s %s\n", ip_address, hostname, host_alias);
      fprintf(fp1, "%s", buf);
      hostname_flag = true;
    } else {
      fputs(buf, fp1);
    }
    NOWARN_UNUSED_RETURN(fgets(buf, 1024, fp));
  }

  //didn't find the host entry - add it as long as we have an external IP - fix for BZ48925

  //printf("hostname flag is %d, and ip_addr is %s\n", hostname_flag, ip_addr);
  if ((hostname_flag == false) && (ip_addr != NULL)) {
    buf[0] = '\0';
    snprintf(buf, sizeof(buf), "%s %s %s\n", ip_addr, hostname, host_alias);
    fprintf(fp1, "%s", buf);
  }
  fclose(fp);
  fclose(fp1);

  if ((pid = fork()) < 0) {
    exit(1);
  } else if (pid > 0) {
    wait(&status);
  } else {
    int res;
    res = execl(mv_binary, "mv", etc_hostname_path_new, etc_hostname_path, (char *) NULL);
    if (res != 0) {
      perror("[net_config] mv of new /etc/hosts file failed ");
    }
    _exit(res);
  }
  return 0;
}

int
set_gateway(char *ip_address, char *old_ip_address)
{

  char buf[1024], gateway_path[1024], gateway_path_new[1024], nic_path[1024], temp_buf[1024], eth0_gateway_ip[1024];
  FILE *fp;
  FILE *fp1;
  const char *mv_binary = MV_BINARY;
  int res;
  char *route_binary = ROUTE_BINARY;
  int status;
  pid_t pid;
  char *pos;
  bool gateway_delete = false;

  // First setup the files for the new gateway

  snprintf(gateway_path, sizeof(gateway_path), "%s", GATEWAY_CONFIG);
  if ((fp = fopen(gateway_path, "r")) == NULL) {
    if ((fp = fopen(gateway_path, "a+")) == NULL) {
      perror("[net_config] failed to open gateway configuration file");
      return 1;
    }
  }
  snprintf(gateway_path_new, sizeof(gateway_path_new), "%s.new", GATEWAY_CONFIG);
  if ((fp1 = fopen(gateway_path_new, "w")) == NULL) {
    perror("[net_config] failed to open new gateway configuration file");
    return 1;
  }

  bool changed = false;
  NOWARN_UNUSED_RETURN(fgets(buf, sizeof(buf), fp));
  while (!feof(fp)) {
    if ((strcasestr(buf, "GATEWAY")) && (strcasestr(buf, "GATEWAYDEV") == NULL)) {
      if (ip_address != NULL) {
        fprintf(fp1, "GATEWAY=%s\n", ip_address);
        changed = true;
      }                         //if NULL we delete the old gateway - no new one
    } else
      fputs(buf, fp1);
    NOWARN_UNUSED_RETURN(fgets(buf, sizeof(buf), fp));
  }
  if (!changed) {               //gateway wasn't in there before
    fprintf(fp1, "GATEWAY=%s\n", ip_address);
  }
  fclose(fp);
  fclose(fp1);

  if ((pid = fork()) < 0) {
    exit(1);
  } else if (pid > 0) {
    wait(&status);
  } else {
    int res;
    res = execl(mv_binary, "mv", gateway_path_new, gateway_path, (char *) NULL);
    if (res != 0) {
      perror("[net_config] mv of new gateway config file failed ");
    }
    _exit(res);
  }

  // then delete pervious default - setup the run time routing info
  if ((pid = fork()) < 0) {
    exit(1);
  } else if (pid > 0) {
    wait(&status);
  } else {
    //We currently assume default gateway is for eth0
    //res = execl(route_binary, "route", "del", "default", "gateway", old_ip_address, "dev", "eth0", NULL);

    //Last known good
    //res = execl(route_binary, "route", "-v", "del", "default", "gw", old_ip_address, "dev", "eth0", NULL);

    //Here we should check whether eth0 has the same gateway_ip
    //If it does - don't delete the previous gateway

    snprintf(nic_path, sizeof(nic_path), "%s/ifcfg-%s", NIC_CONFIG, "eth0");
    if ((fp = fopen(nic_path, "r")) == NULL) {
      if ((fp = fopen(nic_path, "a+")) == NULL) {
        perror("[net_config] failed to open eth0 nic configuration file");
        return 1;
      }
    }

    bool found = false;
    NOWARN_UNUSED_RETURN(fgets(buf, sizeof(buf), fp));
    while (!feof(fp)) {
      if (strcasestr(buf, "GATEWAY") && (strcasestr(buf, "GATEWAYDEV") == NULL)) {
        ink_strncpy(temp_buf, buf, sizeof(temp_buf));
        found = true;
      }
      NOWARN_UNUSED_RETURN(fgets(buf, sizeof(buf), fp));
    }
    fclose(fp);
    if (found) {                //gateway was defined - let's compare eth0 with old general gateway
      pos = strstr(temp_buf, "=");
      if (pos != NULL) {
        pos++;
        ink_strncpy(eth0_gateway_ip, pos, sizeof(eth0_gateway_ip));
        if (eth0_gateway_ip[strlen(eth0_gateway_ip) - 1] == '\n') {
          eth0_gateway_ip[strlen(eth0_gateway_ip) - 1] = '\0';
        }
      }
      // finally, if eth0 gateway doesn't match the default one - delete the gateway!!
      if (strcmp(eth0_gateway_ip, old_ip_address) != 0) {
        gateway_delete = true;
      }
    } else {
      gateway_delete = true;
    }


    if (gateway_delete) {
      res = execl(route_binary, "route", "del", "default", "gateway", old_ip_address, (char *) NULL);
      if (res != 0) {
        perror("[net_config] del default route failed ");
      }
      _exit(res);
    }
  }


  //add the new gateway to the routing table
  if ((pid = fork()) < 0) {
    exit(1);
  } else if (pid > 0) {
    wait(&status);
  } else {
    res = execl(route_binary, "route", "add", "default", "gateway", ip_address, (char *) NULL);
    if (res != 0) {
      perror("[net_config] add default route failed ");
    }
    _exit(res);
  }
  return 0;
}


int
set_dns_server(char *dns_server_ips)
{
  char buf[1024], dns_path[1024], dns_path_new[1024];
  FILE *fp;
  FILE *fp1;
  const char *mv_binary = MV_BINARY;
  int status;
  pid_t pid;
  char *dns_ip;

  snprintf(dns_path, sizeof(dns_path), "%s", DNS_CONFIG);
  if ((fp = fopen(dns_path, "r")) == NULL) {
    if ((fp = fopen(dns_path, "a+")) == NULL) {
      perror("[net_config] failed to open dns configuration file");
      return 1;
    }
  }
  snprintf(dns_path_new, sizeof(dns_path_new), "%s.new", DNS_CONFIG);
  if ((fp1 = fopen(dns_path_new, "w")) == NULL) {
    perror("[net_config] failed to open new dns configuration file");
    return 1;
  }
  //  bool changed = false;
  NOWARN_UNUSED_RETURN(fgets(buf, 1024, fp));
  while (!feof(fp)) {
    if (strcasestr(buf, "nameserver")) {
      /*      if (!changed) {
         fprintf(fp1,"nameserver %s \n",dns_server_ips);
         changed = true;
         }
       */ } else
      fputs(buf, fp1);
    NOWARN_UNUSED_RETURN(fgets(buf, 1024, fp));
  }
  dns_ip = strtok(dns_server_ips, " ");
  while (dns_ip) {
    fprintf(fp1, "nameserver %s\n", dns_ip);
    dns_ip = strtok(NULL, " ");
  }

  fclose(fp);
  fclose(fp1);
  /*
     if ((fp1 = fopen(dns_path_new,"a")) == NULL) {
     perror("[net_config] failed to open new dns configuration file");
     return 1;
     }
     dns_ip = strtok(dns_server_ips, " ");
     while(dns_ip) {
     fprintf(fp1,"nameserver %s\n",dns_ip);
     dns_ip = strtok(NULL, " ");
     } 
     fclose(fp1);

   */

  if ((pid = fork()) < 0) {
    exit(1);
  } else if (pid > 0) {
    wait(&status);
  } else {
    int res;
    res = execl(mv_binary, "mv", dns_path_new, dns_path, (char *) NULL);
    if (res != 0) {
      perror("[net_config] mv of new dns config file failed ");
    }
    _exit(res);
  }

  return 0;
}


int
set_domain_name(char *domain_name)
{

  char buf[1024], domain_path[1024], domain_path_new[1024];
  FILE *fp;
  FILE *fp1;
  const char *mv_binary = MV_BINARY;
  int status;
  pid_t pid;
  bool domain_flag = false;

  snprintf(domain_path, sizeof(domain_path), "%s", DOMAIN_CONFIG);
  if ((fp = fopen(domain_path, "r")) == NULL) {
    if ((fp = fopen(domain_path, "a+")) == NULL) {
      perror("[net_config] failed to open domain name configuration file");
      return 1;
    }
  }
  snprintf(domain_path_new, sizeof(domain_path_new), "%s.new", DOMAIN_CONFIG);
  if ((fp1 = fopen(domain_path_new, "w")) == NULL) {
    perror("[net_config] failed to open new domain configuration file");
    return 1;
  }

  NOWARN_UNUSED_RETURN(fgets(buf, sizeof(buf), fp));
  while (!feof(fp)) {
    if (strcasestr(buf, "domain")) {
      fprintf(fp1, "domain %s \n", domain_name);
      domain_flag = true;
    } else
      fputs(buf, fp1);
    NOWARN_UNUSED_RETURN(fgets(buf, sizeof(buf), fp));
  }

  if (!domain_flag)
    fprintf(fp1, "domain %s \n", domain_name);

  fclose(fp);
  fclose(fp1);

  if ((pid = fork()) < 0) {
    exit(1);
  } else if (pid > 0) {
    wait(&status);
  } else {
    int res;
    res = execl(mv_binary, "mv", domain_path_new, domain_path, (char *) NULL);
    if (res != 0) {
      perror("[net_config] mv of new domain config file failed ");
    }
    _exit(res);
  }

  return 0;
}

int
set_search_domain(char *search_name)
{
  char buf[1024], search_domain_path[1024], search_domain_path_new[1024];
  FILE *fp;
  FILE *fp1;
  const char *mv_binary = MV_BINARY;
  int status;
  pid_t pid;
  bool search_flag = false;

  snprintf(search_domain_path, sizeof(search_domain_path), "%s", SEARCH_DOMAIN_CONFIG);
  if ((fp = fopen(search_domain_path, "r")) == NULL) {
    if ((fp = fopen(search_domain_path, "a+")) == NULL) {
      perror("[net_config] failed to open search domain name configuration file");
      return 1;
    }
  }
  snprintf(search_domain_path_new, sizeof(search_domain_path_new), "%s.new", SEARCH_DOMAIN_CONFIG);
  if ((fp1 = fopen(search_domain_path_new, "w")) == NULL) {
    perror("[net_config] failed to open new search domain configuration file");
    return 1;
  }

  NOWARN_UNUSED_RETURN(fgets(buf, sizeof(buf), fp));
  while (!feof(fp)) {
    if (strcasestr(buf, "search")) {
      fprintf(fp1, "search %s\n", search_name);
      search_flag = true;
    } else
      fputs(buf, fp1);
    NOWARN_UNUSED_RETURN(fgets(buf, sizeof(buf), fp));
  }

  if (!search_flag)
    fprintf(fp1, "search %s\n", search_name);

  fclose(fp);
  fclose(fp1);

  if ((pid = fork()) < 0) {
    exit(1);
  } else if (pid > 0) {
    wait(&status);
  } else {
    int res;
    res = execl(mv_binary, "mv", search_domain_path_new, search_domain_path, (char *) NULL);
    if (res != 0) {
      perror("[net_config] mv of new  search domain config file failed ");
    }
    _exit(res);
  }

  return 0;
}


#ifdef OEM
//Input: connection_speed - 10, 100, 1000 Mb/sec or 0 for autonegotiate
//       duplex - true - full duplex - false half duplex

int
setNICConnection(char *nic_name, int connection_speed, bool duplex, bool auto_negotiate)
{
  char buf[1024], tmp_buf[1024], module_path[1024], module_path_new[1024];
  FILE *fp, *fp1, *fp2;
  const char *mv_binary = MV_BINARY;
  const char *rmmod_binary = RMMOD_BINARY;
  const char *insmod_binary = INSMOD_BINARY;
  int status;
  pid_t pid;
  bool file_exists = true;
  char *tmp, *tmp1, *options, *modname;
  bool found = false;

#if (HOST_OS == linux)
  if ((fp2 = fopen("/dev/.nic", "r")) == NULL) {
    perror("[net_config] failed to open NIC configuration file");
    return 1;
  }
  snprintf(module_path, sizeof(module_path), "%s", MODULE_CONFIG);
  if ((fp = fopen(module_path, "r")) == NULL) {
    file_exists = false;
    perror("[net_config] module file doesn't exist\n");
  }
  snprintf(module_path_new, sizeof(module_path_new), "%s.new", MODULE_CONFIG);
  if ((fp1 = fopen(module_path_new, "w")) == NULL) {
    perror("[net_config] failed to open new module configuration file");
    return 1;
  }
  //first read in the NIC configuration file data
  while (!feof(fp2) && !found) {
    if (strcasestr(buf, nic_name)) {
      found = true;
    } else
      fgets(buf, 1024, fp2);
  }
  fclose(fp2);

  ink_strncpy(tmp_buf, buf, sizeof(tmp_buf));

  //buf includes the options for the card, let's do a quick validation and parsing
  //This should probably be moved to a seperate function

  //if (tmp = strstr(buf, "options")) {
  //let's analyse the options according to the input
  bool map = false;
  if (auto_negotiate) {
    if (tmp = strcasestr(buf, "auto")) {
      options = strtok(tmp, ">");       //this should have the options
      if (tmp1 = strcasestr(tmp_buf, "modname")) {
        modname = strtok(tmp1, ">");
        //now clear the begining of options and modename
        options = strstr(options, "=");
        options++;
        modname = strstr(modname, "=");
        modname++;
        map = true;
      }
    }
  }
  if (connection_speed == 10 && !duplex) {
    if (tmp = strstr(buf, "10h")) {
      options = strtok(tmp, ">");       //this should have the options
      if (tmp1 = strstr(tmp_buf, "modname")) {
        modname = strtok(tmp1, ">");
        map = true;
        //now clear the begining of options and modename
        options = strstr(options, "=");
        options++;
        modname = strstr(modname, "=");
        modname++;
      }
    }
  }
  if (connection_speed == 10 && duplex) {
    if (tmp = strstr(buf, "10f")) {
      options = strtok(tmp, ">");       //this should have the options
      if (tmp1 = strstr(tmp_buf, "modname")) {
        modname = strtok(tmp1, ">");
        map = true;
        //now clear the begining of options and modename
        options = strstr(options, "=");
        options++;
        modname = strstr(modname, "=");
        modname++;
      }
    }
  }
  if (connection_speed == 100 && !duplex) {
    if (tmp = strstr(buf, "100h")) {
      options = strtok(tmp, ">");       //this should have the options
      if (tmp1 = strstr(tmp_buf, "modname")) {
        modname = strtok(tmp1, ">");
        map = true;
        //now clear the begining of options and modename
        options = strstr(options, "=");
        options++;
        modname = strstr(modname, "=");
        modname++;
        //printf("!!!!finally options has %s and modname has %s\n", options, modname);
      }
    }
  }
  if (connection_speed == 100 && duplex) {
    if (tmp = strstr(buf, "100f")) {
      options = strtok(tmp, ">");       //this should have the options
      if (tmp1 = strstr(tmp_buf, "modname")) {
        modname = strtok(tmp1, ">");
        map = true;
        options = strstr(options, "=");
        options++;
        modname = strstr(modname, "=");
        modname++;
      }
    }
  }
  if (!map) {
    perror("[net_config] module config file has wrong syntax");
    if (fp)
      fclose(fp);
    if (fp1)
      fclose(fp1);
    return 1;
  }
  //now setup the card for this round
  //bring the interface down
  char *ifconfig_binary = IFCONFIG;

  if ((pid = fork()) < 0) {
    exit(1);
  } else if (pid > 0) {
    wait(&status);
  } else {
    int res = execl(ifconfig_binary, "ifconfig", nic_name, "down", NULL);
    if (res != 0) {
      perror("[net_confg] couldn't bring interface down");
    }
    _exit(res);
  }

  // rmmod the driver

  if ((pid = fork()) < 0) {
    exit(1);
  } else if (pid > 0) {
    wait(&status);
  } else {
    int res = execl(rmmod_binary, "rmmod", modname, NULL);
    if (res != 0) {
      perror("[net_confg] couldn't rmmod the ethernet driver");
    }
    _exit(res);
  }

  //now insmod the driver with the right options

  if ((pid = fork()) < 0) {
    exit(1);
  } else if (pid > 0) {
    wait(&status);
  } else {
    int res = execl(insmod_binary, "insmod", modname, options, NULL);
    if (res != 0) {
      perror("[net_confg] couldn't insmod the ethernet driver");
    }
    _exit(res);
  }

  //lastly, we need to ifconfig up the nic again
  if ((pid = fork()) < 0) {
    exit(1);
  } else if (pid > 0) {
    wait(&status);
  } else {
    //trying something a bit different
    int res = execl("/etc/rc.d/init.d/network", "network", "start", NULL);
    //int res = execl(ifconfig_binary, "ifconfig", nic_name, "up", NULL);
    if (res != 0) {
      perror("[net_confg] couldn't bring interface up");
    }
    _exit(res);
  }


  // fix it for next boot

  bool alias_found = false, options_found = false;

  if (file_exists) {
    fgets(buf, 1024, fp);
    while (!feof(fp)) {
      //check alias 
      if ((tmp = strcasestr(buf, "alias")) && !alias_found) {
        if (tmp1 = strcasestr(buf, nic_name)) {
          tmp = NULL;
          if ((tmp = strstr(tmp1, modname)) == NULL) {
            if (fp)
              fclose(fp);
            if (fp1)
              fclose(fp1);
            perror("[net_cofig] modules.conf file syntax is wrong - aborting");
            return 1;
          }
          alias_found = true;
          fprintf(fp1, buf);
        }
      } else if ((tmp = strstr(buf, "options")) && !options_found) {
        if (tmp1 = strstr(buf, modname)) {
          //change it to the new options:
          if (!alias_found) {
            fprintf(fp1, "alias %s %s\n", nic_name, modname);
            alias_found = true;
          }
          fprintf(fp1, "options %s %s\n", modname, options);
          options_found = true;
        }
      }
      //not interesting 
      fprintf(fp1, buf);
      fgets(buf, 1024, fp);
    }
    // fix file - last round
    if (!alias_found)
      fprintf(fp1, "alias %s %s\n", nic_name, modname);
    if (!options_found)
      fprintf(fp1, "options %s %s\n", modname, options);
    if (fp)
      fclose(fp);
    if (fp1)
      fclose(fp1);

    //move then new file over the old file
    if ((pid = fork()) < 0) {
      exit(1);
    } else if (pid > 0) {
      wait(&status);
    } else {
      int res;
      res = execl(mv_binary, "mv", module_path_new, module_path, NULL);
      if (res != 0) {
        perror("[net_config] mv of new module config file failed ");
      }
      _exit(res);
    }
  } else {                      //file doesn't exist
    if ((fp = fopen(module_path, "w")) == NULL) {
      perror("[net_config] could't open module file for writing\n");
      return 1;
    }
    fprintf(fp, "alias %s %s\n", nic_name, modname);
    fprintf(fp, "options %s %s\n", modname, options);
    if (fp)
      fclose(fp);
  }
#endif
  return 0;
}

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

#endif

int
rm_stop_proxy()
{


  FILE *ts_file, *rec_file, *pid_file;
  int i = 0, found_pid_path = 0;
  pid_t pid, old_pid;
  char buffer[1024];
  char proxy_pid_path[1024];
  char ts_base_dir[1024];
  char rec_config[1024];
  char *tmp;

  if ((tmp = getenv("TS_ROOT"))) {
    ink_strncpy(ts_base_dir, tmp, sizeof(ts_base_dir));
  } else {
    if ((ts_file = fopen(DEFAULT_TS_DIRECTORY_FILE, "r")) == NULL) {
      ink_strncpy(ts_base_dir, "/usr/local", sizeof(ts_base_dir));
    } else {
      NOWARN_UNUSED_RETURN(fgets(buffer, sizeof(buffer), ts_file));
      fclose(ts_file);
      while (!isspace(buffer[i])) {
        ts_base_dir[i] = buffer[i];
        i++;
      }
      ts_base_dir[i] = '\0';
    }
  }

  snprintf(rec_config, sizeof(rec_config), "%s/etc/trafficserver/records.config", ts_base_dir);

  if ((rec_file = fopen(rec_config, "r")) == NULL) {
    //fprintf(stderr, "Error: unable to open %s.\n", rec_config);
    return -1;
  }

  while (fgets(buffer, sizeof(buffer), rec_file) != NULL) {
    if (strstr(buffer, "proxy.config.rni.proxy_pid_path") != NULL) {
      if ((tmp = strstr(buffer, "STRING ")) != NULL) {
        tmp += strlen("STRING ");
        for (i = 0; tmp[i] != '\n' && tmp[i] != '\0'; i++) {
          proxy_pid_path[i] = tmp[i];
        }
        proxy_pid_path[i] = '\0';
        found_pid_path = 1;
      }
    }
  }
  fclose(rec_file);

  if (found_pid_path == 0) {
    //fprintf(stderr,"Error: unable to find rni variables in %s.\n",rec_config);
    return -1;
  }

  bool done = false;
  int count = 0;

  while (!done && count < 3) {

    /* get the old pid before sleep */

    if ((pid_file = fopen(proxy_pid_path, "r")) == NULL) {
      //fprintf(stderr, "Error: unable to open %s.\n", proxy_pid_path);
      return -1;
    }

    NOWARN_UNUSED_RETURN(fgets(buffer, 1024, pid_file));
    fclose(pid_file);
    // coverity[secure_coding]
    if (sscanf(buffer, "%d\n", &old_pid) != 1) {
      //fprintf(stderr, "Error: unable to read pid from %s\n", proxy_pid_path);
      return -1;
    }

    sleep(65);

    /* Kill the whole RNI process group.
       The RealProxy internal restart logic will restart
       the process group with the new rmserver.cfg file. */

    //printf("\nproxy_pid_path: %s\n",proxy_pid_path);
    if ((pid_file = fopen(proxy_pid_path, "r")) == NULL) {
      //fprintf(stderr, "Error: unable to open %s.\n", proxy_pid_path);
      return -1;
    }

    NOWARN_UNUSED_RETURN(fgets(buffer, 1024, pid_file));
    fclose(pid_file);
    // coverity[secure_coding]
    if (sscanf(buffer, "%d\n", &pid) != 1) {
      //fprintf(stderr, "Error: unable to read pid from %s\n", proxy_pid_path);
      return -1;
    }

    /* only kill if old pid equals new pid */
    //printf ("old_pid %d, pid %d\n", old_pid, pid);
    if (old_pid == pid) {
      kill(-pid, SIGTERM);
      done = true;
    } else
      count++;
  }

  if (!done) {
    errno = 47;                 // ECANCELED, Operation canceled
    perror("[net_config] rm_stop_proxy gave up trying to stop rmserver\n");
  }

  return 0;
}



int
main(int argc, char **argv)
{

  int fun_no;
  if (argv) {
    fun_no = atoi(argv[1]);
  }

  fun_no = atoi(argv[1]);

  switch (fun_no) {
  case 0:
    set_hostname(argv[2], argv[3], argv[4]);
    break;
  case 1:
    set_gateway(argv[2], argv[3]);
    break;
  case 2:
    set_search_domain(argv[2]);
    break;
  case 3:
    set_dns_server(argv[2]);
    break;
  case 4:
    up_interface(argv[2], atoi(argv[3]), argv[4], argv[5], atoi(argv[6]), argv[7]);
    break;
  case 5:
    down_interface(argv[2]);
    break;
#ifdef OEM
  case 6:
    setNICConnection(argv[2], atoi(argv[3]), atoi(argv[4]), atoi(argv[5]));
    break;
#endif
  case 7:
    rm_stop_proxy();
    break;
  case 8:
    disable_interface(argv[2]);
    break;
  default:
    return 1;
  }

  return 0;
}

#endif

#if (HOST_OS == sunos)

#include <ctype.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>

#include <sys/types.h>
#include <sys/wait.h>

#include <netinet/in.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#include <sys/un.h>
struct ifafilt;
#include <net/if.h>

#include <sys/ioctl.h>

#include <net/if.h>
#include <netinet/in.h>
#include <netinet/if_ether.h>
#include <sys/ioctl.h>

#include <sys/systeminfo.h>
#include "ParseRules.h"

#include<dirent.h>
#include<stdlib.h>
#include<string.h>
#include<sys/types.h>

#include "ink_bool.h"

#define UP_INTERFACE     0
#define DOWN_INTERFACE   1
#define HOSTNAME         2
#define IPADDR           3
#define NETMASK          4
#define DNSSERVER        5
#define GATEWAY          6

#define NODENAME_PATH "/etc/nodename"
#define ETC_HOSTS_PATH "/etc/inet/hosts"
#define ETC_NETMASK_PATH "/etc/inet/netmasks"
#define HOSTNAME_PATH "/etc/*hostname.*[0-9]"
#define DEFAULT_ROUTER_PATH "/etc/defaultrouter"
#define DOMAIN_CONFIG "/etc/resolv.conf"
#define SEARCH_DOMAIN_CONFIG "/etc/resolv.conf"
#define DNS_CONFIG "/etc/resolv.conf"
#define STATIC_ROUTE_FILENAME "/etc/init.d/staticroutes"
#define STATIC_ROUTE_LINKNAME "/etc/rc2.d/S70staticroutes"
#define IFCONFIG "/sbin/ifconfig"
#define MRTG_PATH "ui/mrtg"
const char *MV_BINARY = "/bin/mv";
const char *ROUTE_BINARY = "/usr/sbin/route";
const char *IF_CONFIG_BINARY = "/sbin/ifconfig";
const char *SYMBOLIC_LINK_BINARY = "/usr/bin/ln";
const char *CHMOD_BINARY = "/usr/bin/chmod";

#define  NETCONFIG_FAIL 1
#define  NETCONFIG_SUCCESS 0

int set_hostname(char *hostname, char *old_hostname, char *ip_addr);
int set_gateway(char *ip_address, char *old_ip_address);
int set_dns_server(char *dns_server_ips);
int set_domain_name(char *domain_name);
int set_search_domain(char *search_name);
int up_interface(char *nic_name, bool static_ip, char *ip, char *netmask, bool onboot, char *gateway_ip,
                 char *old_ip, char *old_netmask, char *old_gateway, char *default_gateway);
int down_interface(char *nic_name);

int overwriteFiles(char *source, char *dest, char *api_name);
// for default gateway
int addRoute(char *dest, char *gateway, char *api_name);
// get to associate interface to a gateway
int addRoute(char *dest, char *gateway, char *interface, char *api_name);
// for default gateway
int delRoute(char *dest, char *gateway, char *api_name);
// get to associate interface to a gateway
int delRoute(char *dest, char *gateway, char *interface, char *api_name);
int setIpAndMask(char *ip, char *mask, char *interface, char *api_name);

bool isLineCommented(char *line);
bool defaultGatewayHasOwnEntry(char *default_gateway);
int setIpForBoot(char *nic_name, char *new_ip, char *old_ip, char *hostname);
int setNetmaskForBoot(char *nic_name, char *old_ip, char *old_netmask, char *ip, char *netmask);
int getNetworkNumber(char *ip, char *netmask, char *network_number);
int setGatewayForBoot(char *nic_name, char *gateway);
int createSymbolicLink(char *original_file, char *symbolic_link);
const char *strcasestr(char *container, char *substr);
int setInterfaceUpForBoot(char *nic_name);
int setInterfaceDownForBoot(char *nic_name);
void makeStr(char *str);

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

const char *
strcasestr(char *container, char *substr)
{
  return ParseRules::strcasestr(container, substr);
}

/*
int alphasort(const struct dirent **a, const struct dirent **b){
  return(strcmp((*a)->d_name, (*b)->d_name));
}
*/

int
alphasort(const void *a, const void *b)
{
  return (strcmp((*((const struct dirent **) a))->d_name, (*((const struct dirent **) b))->d_name));
}

int
overwriteFiles(char *source, char *dest, char *api_name)
{
  char errmsg[200];
  pid_t pid;
  int status;

  snprintf(errmsg, sizeof(errmsg), "[net_config] %s: mv %s %s failed\n", api_name, source, dest);
  if ((pid = fork()) < 0) {
    exit(1);
  } else if (pid > 0) {
    waitpid(pid, &status, 0);
  } else {
    int res;
    //printf("!! overwriteFiles mv %s %s\n", source, dest);
    res = execl(MV_BINARY, "mv", source, dest, NULL);
    if (res != 0)
      perror(errmsg);
    _exit(res);
  }
  return NETCONFIG_SUCCESS;
}

int
createSymbolicLink(char *original_file, char *symbolic_link)
{
  char errmsg[200];
  pid_t pid;
  int status;

  snprintf(errmsg, sizeof(errmsg), "[net_config] ln %s %s failed\n", original_file, symbolic_link);
  if ((pid = fork()) < 0) {
    exit(1);
  } else if (pid > 0) {
    waitpid(pid, &status, 0);
  } else {
    int res;
    res = execl(SYMBOLIC_LINK_BINARY, "ln", "-s", original_file, symbolic_link, NULL);
    if (res != 0)
      perror(errmsg);
    _exit(res);
  }
  return NETCONFIG_SUCCESS;
}

int
changeFilePermission(char *mode, char *filename)
{
  char errmsg[200];
  pid_t pid;
  int status;

  // printf("!! changeFilePermission %s %s\n", mode, filename);

  snprintf(errmsg, sizeof(errmsg), "[net_config] chmod %s %s failed\n", mode, filename);
  if ((pid = fork()) < 0) {
    exit(1);
  } else if (pid > 0) {
    waitpid(pid, &status, 0);
  } else {
    int res;
    res = execl(CHMOD_BINARY, "chmod", mode, filename, NULL);
    if (res != 0)
      perror(errmsg);
    _exit(res);
  }
  return NETCONFIG_SUCCESS;

}

int
delRoute(char *dest, char *gateway, char *api_name)
{
  char errmsg[200];
  pid_t pid;
  int status;

  snprintf(errmsg, sizeof(errmsg), "[net_config] %s: delete route dest:%s gateway:%s failed\n", api_name, dest,
           gateway);

  if ((pid = fork()) < 0) {
    exit(1);
  } else if (pid > 0) {
    waitpid(pid, &status, 0);
  } else {
    int res;
    res = execl(ROUTE_BINARY, "route", "delete", dest, gateway, NULL);
    if (res != 0) {
      perror(errmsg);
    }
    _exit(res);
  }

  return NETCONFIG_SUCCESS;
}

int
delRoute(char *dest, char *gateway, char *interface, char *api_name)
{
  char errmsg[200];
  pid_t pid;
  int status;

  snprintf(errmsg, sizeof(errmsg), "[net_config] %s: delete route dest:%s gateway:%s interface:%s failed\n",
           api_name, dest, gateway, interface);

  if ((pid = fork()) < 0) {
    exit(1);
  } else if (pid > 0) {
    waitpid(pid, &status, 0);
  } else {
    int res;
    res = execl(ROUTE_BINARY, "route", "delete", dest, gateway, "-ifp", interface, NULL);
    if (res != 0) {
      perror(errmsg);
    }
    _exit(res);
  }
  return NETCONFIG_SUCCESS;
}

int
addRoute(char *dest, char *gateway, char *api_name)
{
  char errmsg[200];
  pid_t pid;
  int status;

  snprintf(errmsg, sizeof(errmsg), "[net_config] %s: add route dest:%s gateway:%s failed\n", api_name, dest, gateway);

  if ((pid = fork()) < 0) {
    exit(1);
  } else if (pid > 0) {
    waitpid(pid, &status, 0);
  } else {
    int res = execl(ROUTE_BINARY, "route", "add", dest, gateway, NULL);
    if (res != 0) {
      perror(errmsg);
    }
    _exit(res);
  }

  return NETCONFIG_SUCCESS;
}

int
addRoute(char *dest, char *gateway, char *interface, char *api_name)
{
  char errmsg[200];
  pid_t pid;
  int status = 0;

  snprintf(errmsg, sizeof(errmsg), "[net_config] %s: add route dest:%s gateway:%s interface:%s failed\n",
           api_name, dest, gateway, interface);

  if ((pid = fork()) < 0) {
    exit(1);
  } else if (pid > 0) {
    waitpid(pid, &status, 0);
  } else {
    int res;
    res = execl(ROUTE_BINARY, "route", "add", dest, gateway, "-ifp", interface, NULL);
    if (res != 0) {
      perror(errmsg);
    }
    _exit(res);
  }

  return NETCONFIG_SUCCESS;
}

int
setIpAndMask(char *ip, char *mask, char *interface, char *api_name)
{
  char errmsg[200];
  pid_t pid;
  int status;

  snprintf(errmsg, sizeof(errmsg), "[net_config] %s: setIpAndMask ip:%s mask:%s interface:%s failed\n",
           api_name, ip, mask, interface);

  if ((pid = fork()) < 0) {
    exit(1);
  } else if (pid > 0) {
    waitpid(pid, &status, 0);
  } else {                      // also update the broadcast address field
    int res;
    res = execl(IF_CONFIG_BINARY, "ifconfig", interface, ip, "netmask", mask, "broadcast", "+", NULL);
    if (res != 0) {
      perror(errmsg);
    }
    _exit(res);
  }
  return NETCONFIG_SUCCESS;
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

bool
defaultGatewayHasOwnEntry(char *default_gateway)
{
  const int BUFFLEN = 200;
  char command[BUFFLEN];
  char buffer[BUFFLEN];
  int num = 0;
  snprintf(command, sizeof(command), "/usr/bin/netstat -rn | grep default | grep %s | grep -v hme | wc -l",
           default_gateway);
  FILE *fd = popen(command, "r");
  if (fd && fgets(buffer, BUFFLEN, fd)) {
    num = atoi(buffer);
  }
  if (num > 0)
    return true;
  else
    return false;
}

int
setIpForBoot(char *nic_name, char *new_ip, char *old_ip)
{
  const int BUFFLEN = 1024;
  const int PATHLEN = 200;
  char command[PATHLEN], buffer[BUFFLEN], hostname_path[PATHLEN];
  char hosts_path[PATHLEN], hosts_path_new[PATHLEN];
  char hostname[PATHLEN];
  FILE *fd, *fd1;

  /* get hostname related to the nic */
  snprintf(hostname_path, sizeof(hostname_path), "/etc/hostname.%s", nic_name);
  if ((fd = fopen(hostname_path, "r")) == NULL) {       // could be in the backup file
    snprintf(hostname_path, sizeof(hostname_path), "/etc/inkt.save.hostname.%s", nic_name);
    if ((fd = fopen(hostname_path, "r")) == NULL) {
      perror("[net_config] failed to open hostname configuration file");
      return NETCONFIG_FAIL;
    }
  }
  fgets(hostname, PATHLEN, fd);
  while (!feof(fd) && isLineCommented(hostname))        // skip # and blank
    fgets(hostname, PATHLEN, fd);

  if (strlen(hostname) <= 0) {
    perror("[net_config] setIpForBoot, failed to get hostname");
    return NETCONFIG_FAIL;
  }

  /* change ip address of hostname entry in /etc/hosts file */
  ink_strncpy(hosts_path, ETC_HOSTS_PATH, sizeof(hosts_path));
  if ((fd = fopen(hosts_path, "r")) == NULL) {
    perror("[net_config] setIpForBoot, failed to open /etc/hosts file");
    return NETCONFIG_FAIL;
  }
  snprintf(hosts_path_new, sizeof(hosts_path_new), "%s.new", ETC_HOSTS_PATH);
  if ((fd1 = fopen(hosts_path_new, "w")) == NULL) {
    perror("[net_config] setIpForBoot, failed to open new /etc/hosts file");
    return NETCONFIG_FAIL;
  }
  bool found = false;
  fgets(buffer, BUFFLEN, fd);
  while (!feof(fd)) {
    if (!isLineCommented(buffer) && (strcasestr(buffer, hostname) || strstr(buffer, old_ip))) { //replace
      makeStr(hostname);
      fprintf(fd1, "%s \t%s\n", new_ip, hostname);
      found = true;
    } else
      fputs(buffer, fd1);
    fgets(buffer, BUFFLEN, fd);
  }
  if (!found)                   // need to add this entry
    fprintf(fd1, "%s \t%s\n", new_ip, hostname);
  fclose(fd);
  fclose(fd1);
  return overwriteFiles(hosts_path_new, hosts_path, "setIpForBoot");
}

int
setNetmaskForBoot(char *nic_name, char *old_ip, char *old_netmask, char *ip, char *netmask)
{
  const int BUFFLEN = 1024;
  const int PATHLEN = 200;
  char buffer[BUFFLEN];
  char old_network_number[PATHLEN], network_number[PATHLEN];
  char netmask_path[PATHLEN], netmask_path_new[PATHLEN];
  FILE *fd, *fd1;

  /* find old/new network-number */

  getNetworkNumber(old_ip, old_netmask, old_network_number);
  getNetworkNumber(ip, netmask, network_number);

  //  printf("setNetMaskForBoot:old:%s, %s, %s\n", old_ip, old_netmask, old_network_number);
  //  printf("setNetMaskForBoot:new:%s, %s, %s\n", ip, netmask, network_number);

  /* 
     replace <old_network_number, old_netmask> pair in /etc/inet/netmasks
     with the new <network_number, network_mask> pair
   */
  ink_strncpy(netmask_path, ETC_NETMASK_PATH, sizeof(netmask_path));
  snprintf(netmask_path_new, sizeof(netmask_path_new), "%s.new", ETC_NETMASK_PATH);
  if ((fd = fopen(netmask_path, "r")) == NULL) {
    perror("[net_config] setNetmaskForBoot, failed to open config files");
    return NETCONFIG_FAIL;
  }
  if ((fd1 = fopen(netmask_path_new, "w")) == NULL) {
    perror("[net_config] setNetmaskForBoot, failed to open new config files");
    return NETCONFIG_FAIL;
  }
  bool found = false;
  fgets(buffer, BUFFLEN, fd);
  while (!feof(fd)) {
    if (!isLineCommented(buffer) && (((strstr(buffer, old_network_number) != NULL) &&   // replace old entry
                                      (strstr(buffer, old_netmask) != NULL)) || ((strstr(buffer, network_number) != NULL) &&    // entry already there
                                                                                 (strstr(buffer, netmask) != NULL)))
      ) {                       // replace
      fprintf(fd1, "%s \t%s\n", network_number, netmask);
      found = true;
    } else
      fputs(buffer, fd1);
    fgets(buffer, BUFFLEN, fd);
  }
  if (!found)                   // append to the end
    fprintf(fd1, "%s \t%s\n", network_number, netmask);

  fclose(fd);
  fclose(fd1);
  return overwriteFiles(netmask_path_new, netmask_path, "setNetmaskForBoot");
}


/*
   converts a dot notaion ip address in a null terminated string to an unsigned int
*/
unsigned int
ipDot2int(char *ipAddr)
{
  unsigned int one;
  unsigned int two;
  unsigned int three;
  unsigned int four;
  unsigned int total;

  // coverity[secure_coding]
  sscanf(ipAddr, "%u.%u.%u.%u", &one, &two, &three, &four);

  total = 0;
  total += (one << 24);
  total += (two << 16);
  total += (three << 8);
  total += four;
  return total;
}

/*
   converts unsigned int ip address to a dot notaion ip address in a null terminated string
*/
unsigned int
ipInt2Dot(int ip, char *ipAddr, const int ipAddrSize)
{

  const unsigned int FIRST_OCTET = 0xFF000000;
  const unsigned int SECOND_OCTET = 0x00FF0000;
  const unsigned int THIRD_OCTET = 0x0000FF00;
  const unsigned int FOURTH_OCTET = 0x000000FF;

  //need 3 + 1 + 3 + 1+ 3 + 1 + 3 = 15 chars, +1 for the null
  char buff[17];
  snprintf(buff, sizeof(buf), "%d.%d.%d.%d",
           (ip & FIRST_OCTET) >> 24, (ip & SECOND_OCTET) >> 16, (ip & THIRD_OCTET) >> 8, (ip & FOURTH_OCTET));
  ink_strncpy(ipAddr, buff, ipAddrSize);
  return NETCONFIG_SUCCESS;

}

/* getNetworkNumber
      network number = (ip & netmask);
 */
int
getNetworkNumber(char *ip, char *netmask, char *network_number)
{
  unsigned int network = (ipDot2int(ip) & ipDot2int(netmask));
  return ipInt2Dot(network, network_number);
}

int
setGatewayForBoot(char *nic_name, char *gateway)
{

  FILE *fd, *fd1;
  int status = NETCONFIG_SUCCESS;
  const int PATHLEN = 200;
  const int BUFFLEN = 1024;
  char static_filename[PATHLEN], static_filename_new[PATHLEN], static_linkname[PATHLEN];
  char buffer[BUFFLEN];

  ink_strncpy(static_filename, STATIC_ROUTE_FILENAME, sizeof(static_filename));
  ink_strncpy(static_linkname, STATIC_ROUTE_LINKNAME, sizeof(static_linkname));
  if ((fd = fopen(static_filename, "r")) == NULL) {
    /* first time creation, create the file */
    if ((fd = fopen(static_filename, "w")) == NULL) {
      perror("[net_config] failed to open static route configuration file");
      return NETCONFIG_FAIL;
    }
    /* add warning comments */
    fprintf(fd, "#!/bin/sh\n");
    fprintf(fd, "##########################################################################\n");
    fprintf(fd, "# File %s created by Inktomi Traffic Manager\n", STATIC_ROUTE_FILENAME);
    fprintf(fd, "# Symbolic link %s is also created\n\n", STATIC_ROUTE_LINKNAME);
    fprintf(fd, "# WARNING: MODIFY/DELETE this file will affect gateway configuration \n");
    fprintf(fd, "#          on each network interface during the boot time\n");
    fprintf(fd, "# WARNING: MODIFY/DELETE this file may also cause Traffic Manager to behave \n");
    fprintf(fd, "#          unexpectedly.\n");
    fprintf(fd, "############################################################################\n");
    /* add route */
    fprintf(fd, "route add default %s -ifp %s\n", gateway, nic_name);
    fclose(fd);
    /* add symbolic link */
    status = createSymbolicLink(static_filename, static_linkname);
  } else {                      // file exist
    /* 
       modify - find line of adding route to specific nic_name
       replace that line
     */
    snprintf(static_filename_new, sizeof(stat_filename_new), "%s.new", static_filename);
    if ((fd1 = fopen(static_filename_new, "w")) == NULL) {
      perror("[net_config] setGatewayForBoot, failed to open new config files");
      return NETCONFIG_FAIL;
    }
    bool found = false;
    fgets(buffer, BUFFLEN, fd);
    while (!feof(fd)) {
      if (strcasestr(buffer, nic_name)) {       // static route for interface found, replace
        found = true;
        fprintf(fd1, "route add default %s -ifp %s\n", gateway, nic_name);
      } else
        fputs(buffer, fd1);
      fgets(buffer, BUFFLEN, fd);
    }
    if (!found)                 // append
      fprintf(fd1, "route add default %s -ifp %s\n", gateway, nic_name);
    fclose(fd);
    fclose(fd1);
    status = changeFilePermission("+x", static_filename_new);
    status = overwriteFiles(static_filename_new, static_filename, "setGatewayForBoot");
  }

  /* chmod on the newly created file */
  status = changeFilePermission("+x", static_filename);
  return status;
}

/*
  since interface will be started upon boot
  if up and /etc/hostname.<interface> file is not there..
  make sure to copy over from /etc/inkt.save.hostname.<interface> ..
*/
int
setInterfaceUpForBoot(char *nic_name)
{
  const int PATHLEN = 200;
  const int BUFFLEN = 1024;
  char interface_filename[PATHLEN], interface_filename_new[PATHLEN];
  char backup_filename[PATHLEN];
  char buffer[BUFFLEN];
  FILE *fd, *fd1;

  snprintf(interface_filename, sizeof(interface_filename), "/etc/hostname.%s", nic_name);
  if ((fd = fopen(interface_filename, "r")) == NULL) {  // file was not there
    snprintf(backup_filename, sizeof(backup_filename), "/etc/inkt.save.hostname.%s", nic_name);
    if ((fd = fopen(backup_filename, "r")) == NULL) {   // backup file not found
      perror("[net_config] setInterfaceForBoot inkt.saved hostname file not found");
      return NETCONFIG_FAIL;
    }
    snprintf(interface_filename_new, sizeof(interface_filename_new), "%s.new", interface_filename);
    if ((fd1 = fopen(interface_filename, "w")) == NULL) {
      perror("[net_config] failed to open /etc/hostname file for write");
    }
    fgets(buffer, BUFFLEN, fd);
    while (!feof(fd)) {         // skip comments
      if (!isLineCommented(buffer))
        fputs(buffer, fd1);
      fgets(buffer, BUFFLEN, fd);
    }
    fclose(fd);
    fclose(fd1);
    remove(backup_filename);
    overwriteFiles(interface_filename_new, interface_filename, "setInterfaceForBoot");
  }
  return NETCONFIG_SUCCESS;
}


int
setInterfaceDownForBoot(char *nic_name)
{

  /*
     Move /etc/hostname.<interface> file to /etc/ink.save.hostname.<interface>
     check for all modified interface stuff.. make sure all the path are set correctly
     we always look for /etc/hostname .. if can't find, then we modify on 
     /etc/ink.save.hostname<interface> .. if can't find.. then something is really wrong
     we go and create /etc/hostname .. < > and put the data there.
   */

  const int PATHLEN = 200;
  const int BUFFLEN = 1024;
  char interface_filename[PATHLEN];
  char backup_filename[PATHLEN];
  char buffer[BUFFLEN];
  FILE *fd, *fd1;

  snprintf(interface_filename, sizeof(interface_filename), "/etc/hostname.%s", nic_name);
  snprintf(backup_filename, sizeof(backup_filename), "/etc/inkt.save.hostname.%s", nic_name);

  if ((fd = fopen(interface_filename, "r")) == NULL) {  // file was not there
    if ((fd = fopen(backup_filename, "r")) == NULL) {
      perror("[net_config] set_interface_down failed: /etc/hostname file not found\n");
      return NETCONFIG_FAIL;
    }
    fclose(fd);
  } else {                      // found /etc/hostname.<interface> file
    // copy the file over to /etc/inkt.save.hostname.<interface>
    if ((fd1 = fopen(backup_filename, "w")) == NULL) {  // file was there
      fclose(fd);
      perror("[net_config] set_interface_down failed: can not open backup file for write\n");
      return NETCONFIG_FAIL;
    }
    fprintf(fd1, "##########################################################################\n");
    fprintf(fd1, "# File %s created by Inktomi Traffic Manager\n", backup_filename);
    fprintf(fd1, "# WARNING: MODIFY/DELETE this file will affect network interface configuration \n");
    fprintf(fd1, "#          during the boot time\n");
    fprintf(fd1, "# WARNING: MODIFY/DELETE this file may also cause Traffic Manager to behave \n");
    fprintf(fd1, "#          unexpectedly.\n");
    fprintf(fd1, "############################################################################\n");
    fgets(buffer, BUFFLEN, fd);
    while (!feof(fd)) {
      fputs(buffer, fd1);
      fgets(buffer, BUFFLEN, fd);
    }
    fclose(fd);
    fclose(fd1);
    remove(interface_filename);
  }
  return NETCONFIG_SUCCESS;
}

int
set_interface_down(char *nic_name)
{
  return 0;
}

int
set_interface_dhcp(char *nic_name, bool boot)
{
  return 0;
}

// call ifconfig <interface> up, sets up deafult_gateway and static routes
int
bringUpInterface(char *nic_name, char *ip, char *default_gateway)
{
  char errmsg[200];
  pid_t pid;
  int status;
  bool attached = false;
  const int BUFFLEN = 1024;
  char buffer[BUFFLEN];
  char command[BUFFLEN];
  FILE *fd;

  // first check if the interface is attached 
  snprintf(command, sizeof(command), "/sbin/ifconfig -a | grep %s", nic_name);
  fd = popen(command, "r");
  if (fd == NULL) {
    pclose(fd);
    perror("[bringUpInterface] failed to open pipe\n");
    return -1;
  }
  if (fgets(buffer, BUFFLEN, fd) == NULL)       // interface not found
    attached = false;
  else
    attached = true;
  pclose(fd);

  snprintf(errmsg, sizeof(errmsg), "[net_config] bringUpInterface failed\n");
  if (attached) {               // bonus, use ifconfig to bring interface up
    if ((pid = fork()) < 0) {
      exit(1);
    } else if (pid > 0) {
      waitpid(pid, &status, 0);
    } else {
      int res = execl(IF_CONFIG_BINARY, "ifconfig", nic_name, "up", NULL);
      if (res != 0)
        perror(errmsg);
      _exit(res);
    }                           // jump to return
  } else {                      // trouble, gotta bring up network card by hand

    // plumb network card
    if ((pid = fork()) < 0) {
      exit(1);
    } else if (pid > 0) {
      waitpid(pid, &status, 0);
    } else {
      int res;
      res = execl(IF_CONFIG_BINARY, "ifconfig", nic_name, "plumb", NULL);
      if (res != 0)
        perror(errmsg);
      _exit(res);
    }

    // set up ip for interface
    if (ip) {
      snprintf(command, sizeof(command), "%s %s inet %s netmask + broadcast + -trailers up 2>&1 >/dev/null",
               IF_CONFIG_BINARY, nic_name, ip);
      system(command);
    }
    // set up the rest of interface automatically, quietly
    snprintf(command, sizeof(command), "%s -ad auto-revarp netmask + broadcast + -trailers up 2>&1 >/dev/null",
             IF_CONFIG_BINARY);
    system(command);

    // add default gateway
    if (default_gateway)
      addRoute("default", default_gateway, "bringUpInterface");

    // execute staticroutes if any
    if ((fd = fopen(STATIC_ROUTE_FILENAME, "r")) != NULL) {
      if ((pid = fork()) < 0) {
        exit(1);
      } else if (pid > 0) {
        waitpid(pid, &status, 0);
      } else {
        int res;
        res = execl(STATIC_ROUTE_FILENAME, NULL);
        if (res != 0)
          perror(errmsg);
        _exit(res);
      }
    }
  }
  return NETCONFIG_SUCCESS;
}


bool
isDHCP(char *nic)
{
  const int PATHLEN = 200;
  char dhcp_filename[PATHLEN];
  FILE *fd;
  snprintf(dhcp_filename, sizeof(dhcp_filename), "/etc/dhcp.%s", nic);
  if ((fd = fopen(dhcp_filename, "r")) == NULL) // dhcp file not exist
    return false;
  else
    return true;
}

// return 0 if sucess, -1 if failure
int
removeDHCPfile(char *nic)
{
  const int PATHLEN = 200;
  char dhcp_filename[PATHLEN];
  snprintf(dhcp_filename, sizeof(dhcp_filename), "/etc/dhcp.%s", nic);
  return remove(dhcp_filename);
}

int
dropDHCP(char *nic_name)
{
  char errmsg[200];
  pid_t pid;
  int status;

  snprintf(errmsg, sizeof(errmsg), "[net_config] dropDHCP on %s failed\n", nic_name);

  if ((pid = fork()) < 0) {
    exit(1);
  } else if (pid > 0) {
    waitpid(pid, &status, 0);
  } else {
    int res = execl(IFCONFIG, "ifconfig", nic_name, "auto-dhcp", "drop", NULL);
    if (res != 0) {
      perror(errmsg);
    }
    _exit(res);
  }
  return status;
}

/* 
   Function : fillEntryEtcHostnameFile
   Description: 
     fill entry in /etc/hostname.<interface>
       if <interface> = hme0 or le0
         use hostname as index
       else
         use interface name as index
 */
int
fillEntryEtcHostnameFile(char *nic_name)
{
  const int PATHLEN = 200;
  char hostname_path[PATHLEN], hostname_path_new[PATHLEN];
  char hostname[PATHLEN];

  hostname[0] = '\0';

  /* first, compose hostname */
  if ((strcmp(nic_name, "le0") == 0) || (strcmp(nic_name, "hme0") == 0)) {
    gethostname(hostname, PATHLEN);
  }

  if (strlen(hostname) == 0 || strcasestr(hostname, "unknown"))
    snprintf(hostname, sizeof(hostname), "inkt_ts_%s", nic_name);

  FILE *fd;
  snprintf(hostname_path, sizeof(hostname_path), "/etc/hostname.%s", nic_name);
  snprintf(hostname_path_new, sizeof(hostname_path_new), "/etc/hostname.%s.new", nic_name);

  if ((fd = fopen(hostname_path_new, "w")) == NULL) {
    perror("[net_config] fillEntryEtcHostnameFile, unable to open etc hostname new file\n");
    return NETCONFIG_FAIL;
  }
  fprintf(fd, "%s\n", hostname);
  fclose(fd);
  return overwriteFiles(hostname_path_new, hostname_path, "fillEntryEtcHostnameFile");
}

/*
 * up_interface(...)
 *   This function will attempt to bring up and create an interface.
 */

int
up_interface(char *nic_name, bool static_ip, char *ip, char *netmask, bool onboot, char *gateway_ip,
             char *old_ip, char *old_netmask, char *old_gateway, char *default_gateway)
{

  //  printf("up_interface:nic_name:%s$\n static_ip:%d$\n ip:%s$\n netmask:%s$\n onboot:%d$\n gateway_ip:%s$\n old_ip:%s$\n old_netmask:%s$\n old_gateway:%s$\n default_gateway:%s$\n", nic_name, static_ip, ip, netmask, onboot, gateway_ip, old_ip, old_netmask, old_gateway, default_gateway);

  if (!static_ip) {
    perror("[net_config] up_interface error: we no longer support DHCP for this api");
    return 1;
  }

  /* bring up interface first, then do the change accordingly */
  bringUpInterface(nic_name, old_ip, default_gateway);

  /* find out the current mode */
  bool isdhcp = isDHCP(nic_name);
  int status = NETCONFIG_SUCCESS;

  /* if DHCP, remove all DHCP related files, drop dhcp from ifconfig */
  if (isdhcp) {
    status = removeDHCPfile(nic_name);
    if (status != 0)            // remove failed
      perror("WARNING: removing of dhcp file failed\n");
    status = dropDHCP(nic_name);
    if (status != 0)
      perror("WARNING: unable to drop DHCP from ifconfig\n");
  }

  /* find what needs to be changed */
  bool isIpModified = ((strcmp(old_ip, ip) == 0) ? false : true);
  bool isNetMaskModified = ((strcmp(old_netmask, netmask) == 0) ? false : true);
  bool isGatewayModified = ((strcmp(old_gateway, gateway_ip) == 0) ? false : true);

  //  printf("up_interface:isdhcp(%d) isIpModified(%d), isNetMaskModified(%d), isGatewayModified(%d)\n", isdhcp, isIpModified, isNetMaskModified, isGatewayModified); fflush(stdout);

  /* set ip, netmask for interface */
  if (isIpModified || isNetMaskModified || isdhcp)
    setIpAndMask(ip, netmask, nic_name, "up_interface");

  if (isdhcp)                   // call interface up
    bringUpInterface(nic_name, NULL, NULL);

  if (isGatewayModified) {      // if gateway is the same, don't touch it, even it is dhcp
    /* set up gateway to maintian the reachability */
    if ((gateway_ip != NULL) && (strcmp(gateway_ip, "") != 0))
      addRoute("default", gateway_ip, nic_name, "up_interface");

    /* delete old gateway when necessary */
    if (strcmp(nic_name, "hme0") == 0 &&        // not sure about hme0, but to be competitable with Linux
        !defaultGatewayHasOwnEntry(default_gateway) &&  // default gateway does not have its own entry
        (strcmp(default_gateway, old_gateway) == 0)) {  // default gateway is the same as old gateway
      // DO NOT DELETE  
      // if we delete this, we may get rid of the default gateway route in the system
    } else if (old_gateway != NULL)
      delRoute("default", old_gateway, nic_name, "up_interface");
  }
  // need to write interface name to /etc/hostname.<interface> file first
  if (isdhcp)
    fillEntryEtcHostnameFile(nic_name);

  /* set up ip for boot time */
  if (isdhcp || isIpModified)
    setIpForBoot(nic_name, ip, old_ip);

  /* set up netmask for boot time */
  if (isdhcp || isIpModified || isNetMaskModified)
    setNetmaskForBoot(nic_name, old_ip, old_netmask, ip, netmask);

  /* set up gateway for boot time */
  // need to add script to add routes at startup time
  if (isGatewayModified)
    setGatewayForBoot(nic_name, gateway_ip);

  /* set up interface parameter on boot time */
  if (onboot)
    setInterfaceUpForBoot(nic_name);
  else
    setInterfaceDownForBoot(nic_name);

  if (isdhcp)                   // /etc/defaultrouter may be empty
    set_gateway(default_gateway, NULL);

  return 0;
}                               /* End up_interface */

/*
 * down_interface(...)
 *   This function will attempt to bring down and remove an interface.
 */
int
down_interface(char *nic_name)
{
  int status;
  pid_t pid;
  char *ifconfig_binary = IFCONFIG;

  if ((pid = fork()) < 0) {
    exit(1);
  } else if (pid > 0) {
    waitpid(pid, &status, 0);
  } else {
    int res = execl(ifconfig_binary, "ifconfig", nic_name, "down", NULL);
    if (res != 0) {
      perror("[net_confg] couldn't bring interface down");
    }
    _exit(res);
  }
  if (set_interface_down(nic_name) != 0) {
    perror("[net_config] set interface down for boot failed");
  }

  return 0;
}                               /* End down_interface */


//FIXME - this is a hack for hard coded hostname in mrtg html files

int
mrtg_hostname_change(char *hostname, char *old_hostname)
{
  char buf[1024], tmp_buf[1024], mrtg_dir_path[1024], mrtg_path[1024], mrtg_path_new[1024];
  const char *index;
  FILE *fp;
  FILE *fp1;
  const char *mv_binary = MV_BINARY;
  int res;
  int status;
  pid_t pid;
  struct dirent **namelist;
  int n, pos;

  snprintf(mrtg_dir_path, sizeof(mrtg_dir_path), "%s", MRTG_PATH);
  n = scandir(MRTG_PATH, &namelist, 0, alphasort);
  if (n < 0) {
    perror("[net_config] scandir failed");
    return 1;
  }
  while (n--) {
    //printf("%s\n", namelist[n]->d_name);
    if (strcasestr(namelist[n]->d_name, ".html") != NULL) {
      snprintf(mrtg_path, sizeof(mrtg_path), "%s/%s", mrtg_dir_path, namelist[n]->d_name);
      if ((fp = fopen(mrtg_path, "r")) == NULL) {
        perror("[net_config] failed to open mrtg file");
        return 1;
      }
      snprintf(mrtg_path_new, sizeof(mrtg_path_new), "%s/%s.new", mrtg_dir_path, namelist[n]->d_name);
      if ((fp1 = fopen(mrtg_path_new, "w")) == NULL) {
        perror("[net_config] failed to open new mrtg file");
        return 1;
      }
      fgets(buf, 1024, fp);
      while (!feof(fp)) {
        if ((index = (strcasestr(buf, old_hostname))) != NULL) {
          pos = strlen(buf) - strlen(index);
          strncpy(tmp_buf, buf, pos);
          fprintf(fp1, "%s", tmp_buf);
          fprintf(fp1, hostname);
          index += strlen(old_hostname);
          ink_strncpy(tmp_buf, index, sizeof(tmp_buf));
          fprintf(fp1, "%s\n", tmp_buf);
        } else
          fputs(buf, fp1);
        fgets(buf, 1024, fp);
      }
      fclose(fp);
      fclose(fp1);

      if ((pid = fork()) < 0) {
        exit(1);
      } else if (pid > 0) {
        waitpid(pid, &status, 0);
      } else {
        // printf("MV: %s %s %s \n",mv_binary,mrtg_path_new, mrtg_path); 
        res = execl(mv_binary, "mv", mrtg_path_new, mrtg_path, NULL);
        if (res != 0) {
          perror("[net_config] mv of new mrtg file failed ");
        }
        _exit(res);
      }
    }
  }

  return 0;
}


int
set_hostname(char *hostname, char *old_hostname, char *ip_addr)
{

  const int PATHLEN = 200;
  const int BUFFLEN = 1024;
  int status = NETCONFIG_SUCCESS;
  char ip_address[PATHLEN];
  char nodename_path[PATHLEN], nodename_path_new[PATHLEN];
  char etc_hosts_path[PATHLEN], etc_hosts_path_new[PATHLEN];
  char etc_hostname_path[PATHLEN], etc_hostname_path_new[PATHLEN];
  char buffer[BUFFLEN], command[PATHLEN];
  FILE *fd, *fd1;
  bool hostname_flag = false;

  /* Change hostname using API */
  status = sysinfo(SI_SET_HOSTNAME, hostname, strlen(hostname));

  if (status < 0) {
    perror("[net_config] OS sethostname failed");
    return status;
  }

  /* Setup the machine hostname for next reboot */
  if (mrtg_hostname_change(hostname, old_hostname) != 0)
    perror("[net_config] failed to change mrtg hostname");

  /* first, overwrite /etc/nodename file */
  snprintf(nodename_path, sizeof(nodename_path), "%s", NODENAME_PATH);
  snprintf(nodename_path_new, sizeof(nodename_path_new), "%s.new", NODENAME_PATH);
  if ((fd = fopen(nodename_path_new, "w")) == NULL) {
    perror("[net_config] failed to open new nodename configuration file");
    return 1;
  }
  fprintf(fd, "%s\n", hostname);
  fclose(fd);
  overwriteFiles(nodename_path_new, nodename_path, "set_hostname");

  /* second, replace entry in /etc/hosts file */
  snprintf(etc_hosts_path, sizeof(etc_hosts_path), "%s", ETC_HOSTS_PATH);
  if ((fd = fopen(etc_hosts_path, "r")) == NULL) {
    if ((fd = fopen(etc_hosts_path, "a+")) == NULL) {
      perror("[net_config] failed to open /etc/hosts file");
    }
  }
  snprintf(etc_hosts_path_new, sizeof(etc_hosts_path_new), "%s.new", ETC_HOSTS_PATH);
  if ((fd1 = fopen(etc_hosts_path_new, "w")) == NULL) {
    perror("[net_config] failed to open new /etc/hosts.new file");
    return 1;
  }
  fgets(buffer, BUFFLEN, fd);
  while (!feof(fd)) {
    if (!isLineCommented(buffer) && strcasestr(buffer, old_hostname)) {
      strcpy(ip_address, strtok(buffer, " "));  //here we change buf!!
      if (ip_address == NULL) { //something is wrong with this file - abort
        perror("[net_config] /etc/hosts format is wrong - not changing it!!");
        return -1;
      }
      //now create the new entry for the new host       
      buffer[0] = '\0';
      snprintf(buffer, sizeof(buffer), "%s \t%s\n", ip_address, hostname);
      fprintf(fd1, "%s", buffer);
      hostname_flag = true;
    } else {
      fputs(buffer, fd1);
    }
    fgets(buffer, BUFFLEN, fd);
  }

  /*  can not do this, because Solaris uses hostname as index to find IP address
     doing this will cause multiple interface to have same IP address
     if ((hostname_flag == false) && (ip_addr != NULL)) {
     ink_strncpy(buffer, "", sizeof(buffer)); // just to make sure
     snprintf(buffer, sizeof(buffer), "%s %s\n", ip_addr, hostname);
     fprintf(fd1,"%s", buffer);
     }
   */

  fclose(fd);
  fclose(fd1);
  overwriteFiles(etc_hosts_path_new, etc_hosts_path, "set_hostname");

  /* third, replace entries in /etc/hostname.<interface> files */
  FILE *fd2;
  snprintf(command, sizeof(command), "%s %s %s", "grep -l", old_hostname, HOSTNAME_PATH);
  fd = popen(command, "r");
  if (fgets(buffer, BUFFLEN, fd)) {     // hostname exist in one of the files
    while (!feof(fd)) {
      if (!strstr(buffer, ":")) {       // not a virtual IP interface
        ink_strncpy(etc_hostname_path, buffer, sizeof(etc_hostname_path));
        makeStr(etc_hostname_path);
        if ((fd1 = fopen(etc_hostname_path, "r")) == NULL) {
          perror("[net_config] failed to open /etc/hostname.*[0-9] file");
          return 1;
        }
        snprintf(etc_hostname_path_new, sizeof(etc_hostname_path_new), "%s.new", etc_hostname_path);
        if ((fd2 = fopen(etc_hostname_path_new, "w")) == NULL) {
          perror("[net_config] failed to open new /etc/hostsname.*[0-9].new file");
          return 1;
        }
        fgets(buffer, BUFFLEN, fd1);
        while (!feof(fd1)) {
          if (!isLineCommented(buffer)) {
            makeStr(buffer);
            if (strcmp(buffer, old_hostname) == 0)      // strict exact match
              fprintf(fd2, "%s\n", hostname);   // overwrite
            else
              fputs(buffer, fd2);
          } else
            fputs(buffer, fd2);
          fgets(buffer, BUFFLEN, fd1);
        }
        fclose(fd1);
        fclose(fd2);
        overwriteFiles(etc_hostname_path_new, etc_hostname_path, "set_hostname");
      }
      fgets(buffer, BUFFLEN, fd);
    }
  }
  fclose(fd);
  return 0;
}

int
set_gateway(char *ip_address, char *old_ip_address)
{

  const int BUFFLEN = 1024;
  const int PATHLEN = 200;
  char buffer[BUFFLEN];
  char default_router_path[PATHLEN], default_router_path_new[PATHLEN];
  char cur_gateway_addr[BUFFLEN];
  FILE *fd, *fd1;

  /* first overwrite the default router file */
  snprintf(default_router_path, sizeof(default_router_path), "%s", DEFAULT_ROUTER_PATH);
  snprintf(default_router_path_new, sizeof(default_router_path_new), "%s.new", DEFAULT_ROUTER_PATH);
  if ((fd = fopen(default_router_path_new, "w")) == NULL) {
    perror("[net_config] failed to open new default router configuration file");
    return 1;
  }
  fprintf(fd, "%s\n", ip_address);
  fclose(fd);
  overwriteFiles(default_router_path_new, default_router_path, "set_gateway");

  /* check whether the default route is present, if so, delete */
  char command[PATHLEN];
  if (old_ip_address != NULL)
    snprintf(command, sizeof(command), "/usr/bin/netstat -rn | grep default | grep %s", old_ip_address);
  else                          // hme for UltraSpark5 le for Spark10
    snprintf(command, sizeof(command), "/usr/bin/netstat -rn | grep default | grep -v hme | grep -v le");

  fd = popen(command, "r");
  if (fd && fgets(buffer, BUFFLEN, fd)) {
    char *p = buffer;
    char *gateway_ptr = cur_gateway_addr;
    while (*p && !isspace(*p))
      p++;                      // reach first white space
    while (*p && isspace(*p))
      p++;                      // skip white space
    while (*p && !isspace(*p))
      *(gateway_ptr++) = *(p++);
    *gateway_ptr = 0;
    delRoute("default", cur_gateway_addr, "set_gateway");
  }

  /* add route */
  addRoute("default", ip_address, "set_gateway");

  return 0;
}


int
set_dns_server(char *dns_server_ips)
{
  char buf[1024], dns_path[1024], dns_path_new[1024];
  FILE *fp;
  FILE *fp1;
  const char *mv_binary = MV_BINARY;
  int status;
  pid_t pid;
  char *dns_ip;

  snprintf(dns_path, sizeof(dns_path), "%s", DNS_CONFIG);
  if ((fp = fopen(dns_path, "r")) == NULL) {
    if ((fp = fopen(dns_path, "a+")) == NULL) {
      perror("[net_config] failed to open dns configuration file");
      return 1;
    }
  }
  snprintf(dns_path_new, sizeof(dns_path_new), "%s.new", DNS_CONFIG);
  if ((fp1 = fopen(dns_path_new, "w")) == NULL) {
    perror("[net_config] failed to open new dns configuration file");
    return 1;
  }
  //  bool changed = false;
  fgets(buf, 1024, fp);
  while (!feof(fp)) {
    if (strcasestr(buf, "nameserver")) {
      /*      if (!changed) {
         fprintf(fp1,"nameserver %s \n",dns_server_ips);
         changed = true;
         }
       */ } else
      fputs(buf, fp1);
    fgets(buf, 1024, fp);
  }
  dns_ip = strtok(dns_server_ips, " ");
  while (dns_ip) {
    fprintf(fp1, "nameserver %s\n", dns_ip);
    dns_ip = strtok(NULL, " ");
  }

  fclose(fp);
  fclose(fp1);
  /*
     if ((fp1 = fopen(dns_path_new,"a")) == NULL) {
     perror("[net_config] failed to open new dns configuration file");
     return 1;
     }
     dns_ip = strtok(dns_server_ips, " ");
     while(dns_ip) {
     fprintf(fp1,"nameserver %s\n",dns_ip);
     dns_ip = strtok(NULL, " ");
     } 
     fclose(fp1);

   */

  if ((pid = fork()) < 0) {
    exit(1);
  } else if (pid > 0) {
    waitpid(pid, &status, 0);
  } else {
    int res;
    res = execl(mv_binary, "mv", dns_path_new, dns_path, NULL);
    if (res != 0) {
      perror("[net_config] mv of new dns config file failed ");
    }
    _exit(res);
  }

  return 0;
}


int
set_domain_name(char *domain_name)
{

  char buf[1024], domain_path[1024], domain_path_new[1024];
  FILE *fp;
  FILE *fp1;
  const char *mv_binary = MV_BINARY;
  int status;
  pid_t pid;
  bool domain_flag = false;

  snprintf(domain_path, sizeof(domain_path), "%s", DOMAIN_CONFIG);
  if ((fp = fopen(domain_path, "r")) == NULL) {
    if ((fp = fopen(domain_path, "a+")) == NULL) {
      perror("[net_config] failed to open domain name configuration file");
      return 1;
    }
  }
  snprintf(domain_path_new, sizeof(domain_path_new), "%s.new", DOMAIN_CONFIG);
  if ((fp1 = fopen(domain_path_new, "w")) == NULL) {
    perror("[net_config] failed to open new domain configuration file");
    return 1;
  }

  fgets(buf, 1024, fp);
  while (!feof(fp)) {
    if (strcasestr(buf, "domain")) {
      fprintf(fp1, "domain %s \n", domain_name);
      domain_flag = true;
    } else
      fputs(buf, fp1);
    fgets(buf, 1024, fp);
  }

  if (!domain_flag)
    fprintf(fp1, "domain %s \n", domain_name);

  fclose(fp);
  fclose(fp1);

  if ((pid = fork()) < 0) {
    exit(1);
  } else if (pid > 0) {
    waitpid(pid, &status, 0);
  } else {
    int res;
    res = execl(mv_binary, "mv", domain_path_new, domain_path, NULL);
    if (res != 0) {
      perror("[net_config] mv of new domain config file failed ");
    }
    _exit(res);
  }

  return 0;
}

int
set_search_domain(char *search_name)
{
  char buf[1024], search_domain_path[1024], search_domain_path_new[1024];
  FILE *fp;
  FILE *fp1;
  const char *mv_binary = MV_BINARY;
  int status;
  pid_t pid;
  bool search_flag = false;

  snprintf(search_domain_path, sizeof(search_domain_path), "%s", SEARCH_DOMAIN_CONFIG);
  if ((fp = fopen(search_domain_path, "r")) == NULL) {
    if ((fp = fopen(search_domain_path, "a+")) == NULL) {
      perror("[net_config] failed to open search domain name configuration file");
      return 1;
    }
  }
  snprintf(search_domain_path_new, sizeof(search_domain_path_new), "%s.new", SEARCH_DOMAIN_CONFIG);
  if ((fp1 = fopen(search_domain_path_new, "w")) == NULL) {
    perror("[net_config] failed to open new search domain configuration file");
    return 1;
  }

  fgets(buf, 1024, fp);
  while (!feof(fp)) {
    if (strcasestr(buf, "search")) {
      fprintf(fp1, "search %s\n", search_name);
      search_flag = true;
    } else
      fputs(buf, fp1);
    fgets(buf, 1024, fp);
  }

  if (!search_flag)
    fprintf(fp1, "search %s\n", search_name);

  fclose(fp);
  fclose(fp1);

  if ((pid = fork()) < 0) {
    exit(1);
  } else if (pid > 0) {
    waitpid(pid, &status, 0);
  } else {
    int res;
    res = execl(mv_binary, "mv", search_domain_path_new, search_domain_path, NULL);
    if (res != 0) {
      perror("[net_config] mv of new  search domain config file failed ");
    }
    _exit(res);
  }

  return 0;
}


#ifdef OEM
//Input: connection_speed - 10, 100, 1000 Mb/sec or 0 for autonegotiate
//       duplex - true - full duplex - false half duplex

int
setNICConnection(char *nic_name, int connection_speed, bool duplex, bool auto_negotiate)
{
  char buf[1024], tmp_buf[1024], module_path[1024], module_path_new[1024];
  FILE *fp, *fp1, *fp2;
  const char *mv_binary = MV_BINARY;
  const char *rmmod_binary = RMMOD_BINARY;
  const char *insmod_binary = INSMOD_BINARY;
  int status;
  pid_t pid;
  bool file_exists = true;
  char *tmp, *tmp1, *options, *modname;
  bool found = false;

#if (HOST_OS == linux)
  if ((fp2 = fopen("/dev/.nic", "r")) == NULL) {
    perror("[net_config] failed to open NIC configuration file");
    return 1;
  }
  snprintf(module_path, sizeof(module_path), "%s", MODULE_CONFIG);
  if ((fp = fopen(module_path, "r")) == NULL) {
    file_exists = false;
    perror("[net_config] module file doesn't exist\n");
  }
  snprintf(module_path_new, sizeof(module_path_new), "%s.new", MODULE_CONFIG);
  if ((fp1 = fopen(module_path_new, "w")) == NULL) {
    perror("[net_config] failed to open new module configuration file");
    return 1;
  }
  //first read in the NIC configuration file data
  while (!feof(fp2) && !found) {
    if (strcasestr(buf, nic_name)) {
      found = true;
    } else
      fgets(buf, 1024, fp2);
  }
  fclose(fp2);

  ink_strncpy(tmp_buf, buf, sizeof(tmp_buf));

  //buf includes the options for the card, let's do a quick validation and parsing
  //This should probably be moved to a seperate function

  //if (tmp = strstr(buf, "options")) {
  //let's analyse the options according to the input
  bool map = false;
  if (auto_negotiate) {
    if (tmp = strcasestr(buf, "auto")) {
      options = strtok(tmp, ">");       //this should have the options
      if (tmp1 = strcasestr(tmp_buf, "modname")) {
        modname = strtok(tmp1, ">");
        //now clear the begining of options and modename
        options = strstr(options, "=");
        options++;
        modname = strstr(modname, "=");
        modname++;
        map = true;
      }
    }
  }
  if (connection_speed == 10 && !duplex) {
    if (tmp = strstr(buf, "10h")) {
      options = strtok(tmp, ">");       //this should have the options
      if (tmp1 = strstr(tmp_buf, "modname")) {
        modname = strtok(tmp1, ">");
        map = true;
        //now clear the begining of options and modename
        options = strstr(options, "=");
        options++;
        modname = strstr(modname, "=");
        modname++;
      }
    }
  }
  if (connection_speed == 10 && duplex) {
    if (tmp = strstr(buf, "10f")) {
      options = strtok(tmp, ">");       //this should have the options
      if (tmp1 = strstr(tmp_buf, "modname")) {
        modname = strtok(tmp1, ">");
        map = true;
        //now clear the begining of options and modename
        options = strstr(options, "=");
        options++;
        modname = strstr(modname, "=");
        modname++;
      }
    }
  }
  if (connection_speed == 100 && !duplex) {
    if (tmp = strstr(buf, "100h")) {
      options = strtok(tmp, ">");       //this should have the options
      if (tmp1 = strstr(tmp_buf, "modname")) {
        modname = strtok(tmp1, ">");
        map = true;
        //now clear the begining of options and modename
        options = strstr(options, "=");
        options++;
        modname = strstr(modname, "=");
        modname++;
        //printf("!!!!finally options has %s and modname has %s\n", options, modname);
      }
    }
  }
  if (connection_speed == 100 && duplex) {
    if (tmp = strstr(buf, "100f")) {
      options = strtok(tmp, ">");       //this should have the options
      if (tmp1 = strstr(tmp_buf, "modname")) {
        modname = strtok(tmp1, ">");
        map = true;
        options = strstr(options, "=");
        options++;
        modname = strstr(modname, "=");
        modname++;
      }
    }
  }
  if (!map) {
    perror("[net_config] module config file has wrong syntax");
    if (fp)
      fclose(fp);
    if (fp1)
      fclose(fp1);
    return 1;
  }
  //now setup the card for this round
  //bring the interface down
  char *ifconfig_binary = IFCONFIG;

  if ((pid = fork()) < 0) {
    exit(1);
  } else if (pid > 0) {
    waitpid(pid, &status, 0);
  } else {
    int res = execl(ifconfig_binary, "ifconfig", nic_name, "down", NULL);
    if (res != 0) {
      perror("[net_confg] couldn't bring interface down");
    }
    _exit(res);
  }

  // rmmod the driver

  if ((pid = fork()) < 0) {
    exit(1);
  } else if (pid > 0) {
    waitpid(pid, &status, 0);
  } else {
    int res = execl(rmmod_binary, "rmmod", modname, NULL);
    if (res != 0) {
      perror("[net_confg] couldn't rmmod the ethernet driver");
    }
    _exit(res);
  }

  //now insmod the driver with the right options

  if ((pid = fork()) < 0) {
    exit(1);
  } else if (pid > 0) {
    waitpid(pid, &status, 0);
  } else {
    int res = execl(insmod_binary, "insmod", modname, options, NULL);
    if (res != 0) {
      perror("[net_confg] couldn't insmod the ethernet driver");
    }
    _exit(res);
  }

  //lastly, we need to ifconfig up the nic again
  if ((pid = fork()) < 0) {
    exit(1);
  } else if (pid > 0) {
    waitpid(pid, &status, 0);
  } else {
    //trying something a bit different
    int res = execl("/etc/rc.d/init.d/network", "network", "start", NULL);
    //int res = execl(ifconfig_binary, "ifconfig", nic_name, "up", NULL);
    if (res != 0) {
      perror("[net_confg] couldn't bring interface up");
    }
    _exit(res);
  }


  // fix it for next boot

  bool alias_found = false, options_found = false;

  if (file_exists) {
    fgets(buf, 1024, fp);
    while (!feof(fp)) {
      //check alias 
      if ((tmp = strcasestr(buf, "alias")) && !alias_found) {
        if (tmp1 = strcasestr(buf, nic_name)) {
          tmp = NULL;
          if ((tmp = strstr(tmp1, modname)) == NULL) {
            if (fp)
              fclose(fp);
            if (fp1)
              fclose(fp1);
            perror("[net_cofig] modules.conf file syntax is wrong - aborting");
            return 1;
          }
          alias_found = true;
          fprintf(fp1, buf);
        }
      } else if ((tmp = strstr(buf, "options")) && !options_found) {
        if (tmp1 = strstr(buf, modname)) {
          //change it to the new options:
          if (!alias_found) {
            fprintf(fp1, "alias %s %s\n", nic_name, modname);
            alias_found = true;
          }
          fprintf(fp1, "options %s %s\n", modname, options);
          options_found = true;
        }
      }
      //not interesting 
      fprintf(fp1, buf);
      fgets(buf, 1024, fp);
    }
    // fix file - last round
    if (!alias_found)
      fprintf(fp1, "alias %s %s\n", nic_name, modname);
    if (!options_found)
      fprintf(fp1, "options %s %s\n", modname, options);
    if (fp)
      fclose(fp);
    if (fp1)
      fclose(fp1);

    //move then new file over the old file
    if ((pid = fork()) < 0) {
      exit(1);
    } else if (pid > 0) {
      waitpid(pid, &status, 0);
    } else {
      int res;
      res = execl(mv_binary, "mv", module_path_new, module_path, NULL);
      if (res != 0) {
        perror("[net_config] mv of new module config file failed ");
      }
      _exit(res);
    }
  } else {                      //file doesn't exist
    if ((fp = fopen(module_path, "w")) == NULL) {
      perror("[net_config] could't open module file for writing\n");
      return 1;
    }
    fprintf(fp, "alias %s %s\n", nic_name, modname);
    fprintf(fp, "options %s %s\n", modname, options);
    if (fp)
      fclose(fp);
  }
#endif
  return 0;
}

#endif

int
main(int argc, char **argv)
{

  int fun_no;
  if (argv) {
    fun_no = atoi(argv[1]);
  }

  fun_no = atoi(argv[1]);

  switch (fun_no) {
  case 0:
    //      printf("set_hostname 2 %s 3 %s 4 %s\n", argv[2], argv[3], argv[4]);
    set_hostname(argv[2], argv[3], argv[4]);
    break;
  case 1:
    //      printf("set_gateway 2 %s 3 %s \n", argv[2], argv[3]);
    set_gateway(argv[2], argv[3]);
    break;
  case 2:
    //printf("set_search_domain 2 %s \n", argv[2]);
    set_search_domain(argv[2]);
    break;
  case 3:
    //printf("set_dns_server 2 %s \n", argv[2]);
    set_dns_server(argv[2]);
    break;
  case 4:
    //printf("before interface up: 1 %s 2 %s 3 %s 4 %s 5 %s \n 6 %s 7 %s 8 %s 9 %s 10 %s\n 11 %s\n", argv[1], argv[2], argv[3], argv[4], argv[5], argv[6], argv[7], argv[8], argv[9], argv[10], argv[11]);
    up_interface(argv[2], atoi(argv[3]), argv[4], argv[5], atoi(argv[6]), argv[7],
                 argv[8], argv[9], argv[10], argv[11]);
    break;
  case 5:
    down_interface(argv[2]);
    break;
#ifdef OEM
  case 6:
    setNICConnection(argv[2], atoi(argv[3]), atoi(argv[4]), atoi(argv[5]));
    break;
#endif
  default:
    return 1;
  }

  return 0;
}

#endif
