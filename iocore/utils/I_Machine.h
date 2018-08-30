/** @file

  Machine

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

  @section details Details

  Part of the utils library which contains classes that use multiple
  components of the IO-Core to implement some useful functionality. The
  classes also serve as good examples of how to use the IO-Core.

 */

#pragma once

#include "tscore/ink_inet.h"
#include "tscore/ink_uuid.h"
#include "tscore/ink_hash_table.h"

/**
  The Machine is a simple place holder for the hostname and the ip
  address of an internet host.

  If a hostname or an IP address is not provided in the constructor,
  the hostname defaults to the name of the current processor and the
  IP address is the address of the current host.  If the host has
  multiple IP addresses, the numerically lowest IP address is used.
  The IP address is stored in the network byte order.

  @internal This does not handle multi-homed systems. That should be
  fixed.

 */
struct Machine {
  typedef Machine self; ///< Self reference type.

  char *hostname;   // name of the internet host
  int hostname_len; // size of the string pointed to by hostname

  IpEndpoint ip;  ///< Preferred IP address of the host (network order)
  IpEndpoint ip4; ///< IPv4 address if present.
  IpEndpoint ip6; ///< IPv6 address if present.

  ip_text_buffer ip_string; // IP address of the host as a string.
  int ip_string_len;

  char ip_hex_string[TS_IP6_SIZE * 2 + 1]; ///< IP address as hex string
  int ip_hex_string_len;

  ATSUuid uuid;

  ~Machine();

  /** Initialize the singleton.
      If @a hostname or @a ip are @c nullptr then system defaults are used.

      @note This must be called before called @c instance so that the
      singleton is not @em inadvertently default initialized.
  */
  static self *init(char const *name     = nullptr, ///< Host name of the machine.
                    sockaddr const *addr = nullptr  ///< Primary IP address of the machine.
  );
  /// @return The global instance of this class.
  static self *instance();
  bool is_self(const char *name);
  bool is_self(const IpAddr *ipaddr);
  void insert_id(char *id);
  void insert_id(IpAddr *ipaddr);

protected:
  Machine(char const *hostname, sockaddr const *addr);

  static self *_instance; ///< Singleton for the class.

  InkHashTable *machine_id_strings;
  InkHashTable *machine_id_ipaddrs;
};
