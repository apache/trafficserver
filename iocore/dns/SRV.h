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

#ifndef _SRV_h_
#define _SRV_h_

#include "ts/ink_platform.h"
#include "I_HostDBProcessor.h"

struct HostDBInfo;

#define HOST_DB_MAX_ROUND_ROBIN_INFO 16
#define RAND_INV_RANGE(r) ((int)((RAND_MAX + 1) / (r)))

struct SRV {
  unsigned int weight;
  unsigned int port;
  unsigned int priority;
  unsigned int ttl;
  unsigned int host_len;
  unsigned int key;
  char host[MAXDNAME];

  SRV() : weight(0), port(0), priority(0), ttl(0), host_len(0), key(0) { host[0] = '\0'; }
};

inline bool
operator<(const SRV &left, const SRV &right)
{
  // lower priorities first, then the key
  return (left.priority == right.priority) ? (left.key < right.key) : (left.priority < right.priority);
}

struct SRVHosts {
  unsigned srv_host_count;
  unsigned srv_hosts_length;
  SRV hosts[HOST_DB_MAX_ROUND_ROBIN_INFO];

  ~SRVHosts() {}
  SRVHosts() : srv_host_count(0), srv_hosts_length(0) {}
};

#endif
