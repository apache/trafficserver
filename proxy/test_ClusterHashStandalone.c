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

#include "ink_unused.h"        /* MAGIC_EDITING_TAG */
#include <stdio.h>
#include "ClusterHashStandalone.h"

#define MAKE_IP(a,b,c,d)  (((a) << 24) | ((b) << 16) | ((c) << 8) | (d))

int
main()
{
  unsigned int ipaddrs[8];
  unsigned int ipaddr;
  int i;
  char *urls[] = {
    "http://foo.bar.com",
    "http://bar.foo.com",
    "http://argh.foo.bar.com",
    NULL,
  };

  for (i = 0; i < 8; i++)
    ipaddrs[i] = MAKE_IP(0, 0, 0, i + 1);
  build_standalone_cluster_hash_table(8, ipaddrs);

  for (i = 0; urls[i]; i++) {
    ipaddr = standalone_machine_hash(urls[i]);
    printf("cluster IP ('%s') = %d.%d.%d.%d\n", urls[i],
           (ipaddr >> 24) & 0xFF, (ipaddr >> 16) & 0xFF, (ipaddr >> 8) & 0xFF, (ipaddr >> 0) & 0xFF);
  }
}
