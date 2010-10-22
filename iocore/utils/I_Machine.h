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

#ifndef _I_Machine_h
#define _I_Machine_h


/**
  The Machine is a simple place holder for the hostname and the ip
  address of an internet host.

  If a hostname or an IP address is not provided in the constructor,
  the hostname defaults to the name of the current processor and the
  IP address is the address of the current host.  If the host has
  multiple IP addresses, the numerically lowest IP address is used.
  The IP address is stored in the network byte order.

 */
struct Machine
{
  char *hostname;               // name of the internet host
  int hostname_len;             // size of the string pointed to by hostname

  unsigned int ip;              // IP address of the host (network order)
  char *ip_string;              // IP address of the host as a string.
  int ip_string_len;
  char *ip_hex_string;          // IP address of the host as a hex string
  int ip_hex_string_len;

  Machine(char *hostname = 0, unsigned int ip = 0);
  ~Machine();
};

/**
  Creates a Machine Object with the hostname and IP address.  If no
  hostname or IP address is given, the hostname defaults to the name of
  the current processor.

 */
void create_this_machine(char *hostname = 0, unsigned int ip = 0);

/**
  Returns the Machine object created by create_this_machine().

 */
Machine *this_machine();

#endif
