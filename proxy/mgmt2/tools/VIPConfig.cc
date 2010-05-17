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
 * VIPConfig.cc
 *   Tool to configure up/down/create virtual ip interfaces. To be exec'ed
 * by management processes so they need not be running set uid root.
 *
 * $Date: 2008-05-20 17:26:18 $
 *
 *
 */

#include "inktomi++.h"
#include "I_Layout.h"
#include "I_Version.h"

#include <sys/un.h>
struct ifafilt;
#include <net/if.h>

#if (HOST_OS == linux)
#include <sys/ioctl.h>
#else
#include <sys/sockio.h>
#endif

#if (HOST_OS == linux)
#include <net/if.h>
#include <netinet/in.h>
#include <netinet/if_ether.h>
#include <sys/ioctl.h>
#include <sys/raw.h>
#endif

#define UP_INTERFACE     0
#define DOWN_INTERFACE   1

void up_interface(char *binary, char *vip, char *interface);
void down_interface(char *binary, char *vip, char *interface);

#if (HOST_OS == linux)
char *get_netmask_for_intr(char *intrName);
char *get_broadcast_for_intr(char *intrName);
#endif



/*
 * main(...)
 *   Main for the vip config tool. Return values are status to caller.
 *
 *  Return Value:
 *
 */
int
main(int argc, char **argv)
{
  int operation, interface_id;
  char binary[1024], tinterface[1024], interface[1024], vip[1024];

  // Before accessing file system initialize Layout engine
  create_default_layout();

  if (argc != 6 || (argc != 2 && strstr(argv[1], "help"))) {
    ink_fputln(stderr, "[vip_config] Usage incorrect(1)");
    exit(1);
  } else {                      /* Handle args */

    if (strcmp(argv[1], "up") == 0) {
      operation = UP_INTERFACE;
    } else if (strcmp(argv[1], "down") == 0) {
      operation = DOWN_INTERFACE;
    } else {
#ifdef DEBUG
      ink_fputln(stderr, "[vip_config] Usage incorrect(2)");
#endif
      exit(1);
    }

    ink_strncpy(vip, argv[2], sizeof(vip));     /* Get the virtual ip + interface id */
    ink_strncpy(binary, argv[3], sizeof(binary));
    ink_strncpy(tinterface, argv[4], sizeof(tinterface));
    interface_id = atoi(argv[5]);

    snprintf(interface, sizeof(interface) - 1, "%s:%d", tinterface, interface_id);
  }

  switch (operation) {
  case UP_INTERFACE:
    up_interface(binary, vip, interface);
    break;
  case DOWN_INTERFACE:
    down_interface(binary, vip, interface);
    break;
  default:
    break;
  }
  return 0;
}                               /* End main */


/*
 * up_interface(...)
 *   This function will attempt to bring up and create an interface.
 */
void
up_interface(char *binary, char *vip, char *interface)
{
  int status;
  pid_t pid;

#if (HOST_OS == linux)

  char *netmask = NULL;
  char *broadcast = NULL;

  // INKqa02784 - IRIX does not automatically set netmask to same
  //  as the physical interface.  Figure out what the netmask of
  //  physical interface is so that we can explictly set it
  netmask = get_netmask_for_intr(interface);
  if (netmask == NULL) {
    fprintf(stderr, "[vip_config] WARNING: Could not determine netmask for %s\n", interface);
  }
  // INKqa02784 - IRIX does not automatically set netmask to same
  //  as the physical interface.  Figure out what the netmask of
  //  physical interface is so that we can explictly set it

  broadcast = get_broadcast_for_intr(interface);
  if (broadcast == NULL) {
    fprintf(stderr, "[vip_config] WARNING: Could not determine netmask for %s\n", interface);
  }
  if ((pid = fork()) < 0) {
    exit(1);
  } else if (pid > 0) {
    wait(&status);
  } else {                      /* Exec the up */

    int res;
    if (netmask == NULL) {
      res = execl(binary, "ifconfig", interface, vip, (char *) NULL);
    } else {
      if (broadcast == NULL) {
        res = execl(binary, "ifconfig", interface, vip, "netmask", netmask, (char *) NULL);
      } else {
        res = execl(binary, "ifconfig", interface, vip, "netmask", netmask, "broadcast", broadcast, (char *) NULL);
      }
    }

    if (res != 0) {
      perror("[vip_confg] ");
    }
    _exit(res);
  }

  char intrNameOnly[BUFSIZ];
  ink_strncpy(intrNameOnly, interface, strlen(interface) - strlen(strchr(interface, ':')));

  if ((pid = fork()) < 0) {
    down_interface(binary, vip, interface);
    _exit(1);
  } else if (pid > 0) {
    wait(&status);
  } else {
#ifdef DEBUG
    fprintf(stderr, "Executing: /usr/sbin/arping -q -A -c1 -I %s -s %s %s\n", intrNameOnly, vip, vip);
#endif
    int res = execl("/usr/sbin/arping", "arping", "-q", "-A", "-c1", "-I", intrNameOnly, "-s", vip, vip, (char *) NULL);
    if (res != 0) {
      perror("[vip_config] ");
    }
    _exit(res);
  }

#else  // linux check
// for freebsd

  if ((pid = fork()) < 0) {
#ifdef DEBUG
    fprintf(stderr, "[vip_config] fork failed\n");
#endif
    exit(1);
  } else if (pid > 0) {         /* parent */
    wait(&status);
    if ((pid = fork()) < 0) {
#ifdef DEBUG
      fprintf(stderr, "[vip_config] fork failed\n");
#endif
      exit(1);
    } else if (pid > 0) {
      wait(&status);
    } else {                    /* Exec the up */
      int res = execl(binary, "ifconfig", interface, "up", (char*)NULL);
      if (res != 0) {
        perror("[vip_confg] ");
      }
#ifdef DEBUG
      fprintf(stderr, "RES: %d  0\n", res);
#endif
      _exit(res);
    }
  } else {                      /* Exec the create */

// "netmask +" is broken on Solaris 2.6 & 2.7,
// it never actually checks against /etc/netmasks.
    int res = execl(binary, "ifconfig", interface, vip, "netmask", "+", "broadcast", "+", "metric", "1", (char*)NULL);
    if (res != 0) {
      perror("[vip_config] ");
    }
#ifdef DEBUG
    fprintf(stderr, "RES: %d  1\n", res);
#endif
    _exit(res);
  }
#endif  // linux check

  return;
}                               /* End up_interface */

/*
 * down_interface(...)
 *   This function will attempt to bring down and remove an interface.
 */
void
down_interface(char *binary, char *vip, char *interface)
{
  int status;
  pid_t pid;

#if (HOST_OS == linux)

  if ((pid = fork()) < 0) {
    exit(1);
  } else if (pid > 0) {
    wait(&status);
  } else {                      /* Exec the up */
    int res = execl(binary, "ifconfig", interface, "inet", "0.0.0.0", (char *) NULL);
    if (res != 0) {
      perror("[vip_confg] ");
    }
    _exit(res);
  }

  if ((pid = fork()) < 0) {
    down_interface(binary, vip, interface);
    _exit(1);
  } else if (pid > 0) {
    wait(&status);
  } else {

    // 'arp' -d does not work!
    // int res = execl("/sbin/arp", "arp", "-d", vip, NULL);
    int res = 0;
    if (res != 0) {
      perror("[vip_config] ");
    }
    _exit(res);
  }

#else  // linux check

  if ((pid = fork()) < 0) {
#ifdef DEBUG
    fprintf(stderr, "[vip_config] fork failed\n");
#endif
    exit(1);
  } else if (pid > 0) {         /* parent */
    wait(&status);
    if ((pid = fork()) < 0) {
#ifdef DEBUG
      fprintf(stderr, "[vip_config] fork failed\n");
#endif
      exit(1);
    } else if (pid > 0) {
      wait(&status);
    } else {                    /* Exec the remove */
      int res = execl(binary, "ifconfig", interface, "inet", "0.0.0.0", (char*)NULL);
      if (res != 0) {
        perror("[vip_confg] ");
      }
#ifdef DEBUG
      fprintf(stderr, "RES: %d  2\n", res);
#endif
      _exit(res);
    }
  } else {                      /* Exec the down */
    int res = 0;
    // don't down the inter on linux, it'll shutdown the driver
    res = execl(binary, "ifconfig", interface, "down", (char*)NULL);
    if (res != 0) {
      perror("[vip_confg] ");
    }
#ifdef DEBUG
    fprintf(stderr, "RES: %d  3\n", res);
#endif
    _exit(res);
  }

#endif  // linux check

  return;
}                               /* End down_interface */

#if (HOST_OS == linux)
// char* get_netmask_for_intr(char* intrName)
//
//    Looks up the netmask for the interface specifiec by intrName
//       Returns either NULL or a pointer to static (thread unsafe!) buffer
//       containing the netmask
//
char *
get_netmask_for_intr(char *intrName)
{

  int fakeSocket;               // a temporary socket to pass to ioctl
  struct sockaddr_in *tmp;      // a tmp ptr for addresses
  struct ifconf ifc;            // ifconf information
  char ifbuf[2048];             // ifconf buffer
  struct ifreq *ifr;            // pointer to individual inferface info
  int n;                        // loop var
  static char static_buffer[17];        // static return buffer
  char *return_ptr = NULL;      // what we are planning to return to the callee

  // Prevent UMRs
  memset(ifbuf, 0, sizeof(ifbuf));

  if (intrName == NULL) {
    return return_ptr;
  }

  char intrNameOnly[BUFSIZ];
  ink_strncpy(intrNameOnly, intrName, strlen(intrName) - strlen(strchr(intrName, ':')));

  if ((fakeSocket = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
    perror("[vip_config] Unable to create socket : ");
  }
  // Fetch the list of network interfaces
  ifc.ifc_len = sizeof(ifbuf);
  ifc.ifc_buf = ifbuf;
  if (ioctl(fakeSocket, SIOCGIFCONF, (char *) &ifc) < 0) {
    perror("[vip_config] Unable to read network interface configuration : ");
  }

  ifr = ifc.ifc_req;

  // Loop through the list of interfaces
  for (n = ifc.ifc_len / sizeof(struct ifreq); --n >= 0; ifr++) {

    if (strcmp(ifr->ifr_name, intrNameOnly) == 0) {
      // Get the address of the interface
      if (ioctl(fakeSocket, SIOCGIFNETMASK, (char *) ifr) >= 0) {
        tmp = (struct sockaddr_in *) &ifr->ifr_ifru.ifru_addr;
        ink_strncpy(static_buffer, inet_ntoa(tmp->sin_addr), sizeof(static_buffer));
        return_ptr = static_buffer;
        break;
      }
    }
  }
  close(fakeSocket);
  return return_ptr;
}                               /* End get_netmask_for_intr() */

// char* get_broadcast_for_intr(char* intrName)
//
//    Looks up the broadcast address for the interface specifiec by intrName
//       Returns either NULL or a pointer to static (thread unsafe!) buffer
//       containing the netmask
//
char *
get_broadcast_for_intr(char *intrName)
{

  int fakeSocket;               // a temporary socket to pass to ioctl
  struct sockaddr_in *tmp;      // a tmp ptr for addresses
  struct ifconf ifc;            // ifconf information
  char ifbuf[2048];             // ifconf buffer
  struct ifreq *ifr;            // pointer to individual inferface info
  int n;                        // loop var
  static char static_buffer[17];        // static return buffer
  char *return_ptr = NULL;      // what we are planning to return to the callee

  // Prevent UMRs
  memset(ifbuf, 0, sizeof(ifbuf));

  if (intrName == NULL) {
    return return_ptr;
  }

  char intrNameOnly[BUFSIZ];
  ink_strncpy(intrNameOnly, intrName, strlen(intrName) - strlen(strchr(intrName, ':')));

  if ((fakeSocket = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
    perror("[vip_config] Unable to create socket : ");
  }
  // Fetch the list of network interfaces
  ifc.ifc_len = sizeof(ifbuf);
  ifc.ifc_buf = ifbuf;
  if (ioctl(fakeSocket, SIOCGIFCONF, (char *) &ifc) < 0) {
    perror("[vip_config] Unable to read network interface configuration : ");
  }

  ifr = ifc.ifc_req;

  // Loop through the list of interfaces
  for (n = ifc.ifc_len / sizeof(struct ifreq); --n >= 0; ifr++) {

    if (strcmp(ifr->ifr_name, intrNameOnly) == 0) {
      // Get the address of the interface
      if (ioctl(fakeSocket, SIOCGIFBRDADDR, (char *) ifr) >= 0) {
        tmp = (struct sockaddr_in *) &ifr->ifr_ifru.ifru_addr;
        ink_strncpy(static_buffer, inet_ntoa(tmp->sin_addr), sizeof(static_buffer));
        return_ptr = static_buffer;
        break;
      }
    }
  }
  close(fakeSocket);
  return return_ptr;
}                               /* End get_netmask_for_intr() */

#endif  // linux check
