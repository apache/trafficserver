/** @file

  Support class for describing the local machine.

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

#include "tscore/ink_platform.h"
#include "tscore/ink_inet.h"
#include "tscore/ink_assert.h"
#include "tscore/Diags.h"
#include "I_Machine.h"

#if HAVE_IFADDRS_H
#include <ifaddrs.h>
#endif

static void
make_to_lower_case(const char *name, char *lower_case_name, int buf_len)
{
  int name_len = strlen(name);
  int i;

  if (name_len > (buf_len - 1)) {
    name_len = buf_len - 1;
  }

  for (i = 0; i < name_len; i++) {
    lower_case_name[i] = ParseRules::ink_tolower(name[i]);
  }
  lower_case_name[i] = '\0';
}

// Singleton
Machine *Machine::_instance = nullptr;

Machine *
Machine::instance()
{
  ink_assert(_instance || !"Machine instance accessed before initialization");
  return Machine::_instance;
}

Machine *
Machine::init(char const *name, sockaddr const *ip)
{
  ink_assert(!_instance || !"Machine instance initialized twice.");
  Machine::_instance = new Machine(name, ip);
  return Machine::_instance;
}

Machine::Machine(char const *the_hostname, sockaddr const *addr)
  : hostname(nullptr),
    hostname_len(0),
    ip_string_len(0),
    ip_hex_string_len(0),
    machine_id_strings(ink_hash_table_create(InkHashTableKeyType_String)),
    machine_id_ipaddrs(ink_hash_table_create(InkHashTableKeyType_String))
{
  char localhost[1024];
  char ip_strbuf[INET6_ADDRSTRLEN];
  int status; // return for system calls.

  ip_string[0]     = 0;
  ip_hex_string[0] = 0;
  ink_zero(ip);
  ink_zero(ip4);
  ink_zero(ip6);

  uuid.initialize(TS_UUID_V4);
  ink_release_assert(nullptr != uuid.getString()); // The Process UUID must be available on startup

  localhost[sizeof(localhost) - 1] = 0; // ensure termination.

  if (!ats_is_ip(addr)) {
    if (!the_hostname) {
      ink_release_assert(!gethostname(localhost, sizeof(localhost) - 1));
      the_hostname = localhost;
    }
    hostname = ats_strdup(the_hostname);

#if HAVE_IFADDRS_H
    ifaddrs *ifa_addrs = nullptr;
    status             = getifaddrs(&ifa_addrs);
#else
    int s = socket(AF_INET, SOCK_DGRAM, 0);
    // This number is hard to determine, but needs to be much larger than
    // you would expect. On a normal system with just two interfaces and
    // one address / interface the return count is 120. Stack space is
    // cheap so it's best to go big.
    static const int N_REQ = 1024;
    ifconf conf;
    ifreq req[N_REQ];
    if (0 <= s) {
      conf.ifc_len = sizeof(req);
      conf.ifc_req = req;
      status       = ioctl(s, SIOCGIFCONF, &conf);
    } else {
      status = -1;
    }
#endif

    if (0 != status) {
      Warning("Unable to determine local host '%s' address information - %s", hostname, strerror(errno));
    } else {
      // Loop through the interface addresses and prefer by type.
      enum {
        NA, // Not an (IP) Address.
        LO, // Loopback.
        LL, // Link Local
        PR, // Private.
        MC, // Multicast.
        GL  // Global.
      } spot_type = NA,
        ip4_type = NA, ip6_type = NA;
      sockaddr const *ifip;
      unsigned int ifflags;
      for (
#if HAVE_IFADDRS_H
        ifaddrs *spot = ifa_addrs; spot; spot = spot->ifa_next
#else
        ifreq *spot = req, *req_limit = req + (conf.ifc_len / sizeof(*req)); spot < req_limit; ++spot
#endif
      ) {
#if HAVE_IFADDRS_H
        ifip    = spot->ifa_addr;
        ifflags = spot->ifa_flags;
#else
        ifip = &spot->ifr_addr;

        // get the interface's flags
        struct ifreq ifr;
        ink_strlcpy(ifr.ifr_name, spot->ifr_name, IFNAMSIZ);
        if (ioctl(s, SIOCGIFFLAGS, &ifr) == 0) {
          ifflags = ifr.ifr_flags;
        } else {
          ifflags = 0; // flags not available, default to just looking at IP
        }
#endif
        if (!ats_is_ip(ifip)) {
          spot_type = NA;
        } else if (ats_is_ip_loopback(ifip) || (IFF_LOOPBACK & ifflags)) {
          spot_type = LO;
        } else if (ats_is_ip_linklocal(ifip)) {
          spot_type = LL;
        } else if (ats_is_ip_private(ifip)) {
          spot_type = PR;
        } else if (ats_is_ip_multicast(ifip)) {
          spot_type = MC;
        } else {
          spot_type = GL;
        }
        if (spot_type == NA) {
          continue; // Next!
        }

        if (ats_is_ip4(ifip) || ats_is_ip6(ifip)) {
          ink_zero(ip_strbuf);
          ink_zero(localhost);
          ats_ip_ntop(ifip, ip_strbuf, sizeof(ip_strbuf));
          insert_id(ip_strbuf);
          if (spot_type != LL && getnameinfo(ifip, ats_ip_size(ifip), localhost, sizeof(localhost) - 1, nullptr, 0, 0) == 0) {
            insert_id(localhost);
          }
          IpAddr *ipaddr = new IpAddr(ifip);
          insert_id(ipaddr);
          if (ats_is_ip4(ifip)) {
            if (spot_type > ip4_type) {
              ats_ip_copy(&ip4, ifip);
              ip4_type = spot_type;
            }
          } else if (ats_is_ip6(ifip)) {
            if (spot_type > ip6_type) {
              ats_ip_copy(&ip6, ifip);
              ip6_type = spot_type;
            }
          }
        }
      }

#if HAVE_IFADDRS_H
      freeifaddrs(ifa_addrs);
#endif

      // What about the general address? Prefer IPv4?
      if (ip4_type >= ip6_type) {
        ats_ip_copy(&ip.sa, &ip4.sa);
      } else {
        ats_ip_copy(&ip.sa, &ip6.sa);
      }
    }
#if !HAVE_IFADDRS_H
    close(s);
#endif
  } else { // address provided.
    ats_ip_copy(&ip, addr);
    if (ats_is_ip4(addr)) {
      ats_ip_copy(&ip4, addr);
    } else if (ats_is_ip6(addr)) {
      ats_ip_copy(&ip6, addr);
    }

    status = getnameinfo(addr, ats_ip_size(addr), localhost, sizeof(localhost) - 1, nullptr, 0, 0); // no flags

    if (0 != status) {
      ip_text_buffer ipbuff;
      Warning("Failed to find hostname for address '%s' - %s", ats_ip_ntop(addr, ipbuff, sizeof(ipbuff)), gai_strerror(status));
    } else {
      hostname = ats_strdup(localhost);
    }
  }

  hostname_len = hostname ? strlen(hostname) : 0;

  ats_ip_ntop(&ip.sa, ip_string, sizeof(ip_string));
  ip_string_len     = strlen(ip_string);
  ip_hex_string_len = ats_ip_to_hex(&ip.sa, ip_hex_string, sizeof(ip_hex_string));
}

Machine::~Machine()
{
  ats_free(hostname);

  // release machine_id_strings hash table.
  InkHashTableIteratorState ht_iter;
  InkHashTableEntry *ht_entry = nullptr;
  ht_entry                    = ink_hash_table_iterator_first(machine_id_strings, &ht_iter);

  while (ht_entry != nullptr) {
    char *value = static_cast<char *>(ink_hash_table_entry_value(machine_id_strings, ht_entry));
    ats_free(value);
    ht_entry = ink_hash_table_iterator_next(machine_id_strings, &ht_iter);
  }
  ink_hash_table_destroy(machine_id_strings);

  // release machine_id_ipaddrs hash table.
  ht_entry = nullptr;
  ht_entry = ink_hash_table_iterator_first(machine_id_ipaddrs, &ht_iter);
  while (ht_entry != nullptr) {
    IpAddr *ipaddr = static_cast<IpAddr *>(ink_hash_table_entry_value(machine_id_ipaddrs, ht_entry));
    delete ipaddr;
    ht_entry = ink_hash_table_iterator_next(machine_id_ipaddrs, &ht_iter);
  }
  ink_hash_table_destroy(machine_id_ipaddrs);
}

bool
Machine::is_self(const char *name)
{
  char lower_case_name[TS_MAX_HOST_NAME_LEN + 1] = {0};
  void *value                                    = nullptr;

  if (name == nullptr) {
    return false;
  }

  make_to_lower_case(name, lower_case_name, sizeof(lower_case_name));

  return ink_hash_table_lookup(machine_id_strings, lower_case_name, &value) == 1 ? true : false;
}

bool
Machine::is_self(const IpAddr *ipaddr)
{
  void *value                             = nullptr;
  char string_value[INET6_ADDRSTRLEN + 1] = {0};

  if (ipaddr == nullptr) {
    return false;
  }
  ipaddr->toString(string_value, sizeof(string_value));
  return ink_hash_table_lookup(machine_id_ipaddrs, string_value, &value) == 1 ? true : false;
}

void
Machine::insert_id(char *id)
{
  char lower_case_name[TS_MAX_HOST_NAME_LEN + 1] = {0};
  char *value                                    = nullptr;

  make_to_lower_case(id, lower_case_name, sizeof(lower_case_name));
  value = ats_strndup(lower_case_name, strlen(lower_case_name));
  ink_hash_table_insert(machine_id_strings, lower_case_name, value);
}

void
Machine::insert_id(IpAddr *ipaddr)
{
  int length = INET6_ADDRSTRLEN + 1;

  if (ipaddr != nullptr) {
    char *string_value = static_cast<char *>(ats_calloc(length, 1));
    ipaddr->toString(string_value, length);
    ink_hash_table_insert(machine_id_strings, string_value, string_value);
    ink_hash_table_insert(machine_id_ipaddrs, string_value, ipaddr);
  }
}
