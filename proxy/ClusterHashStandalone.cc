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
/****************************************************************************

  ClusterHashStandalone.cc

  
 ****************************************************************************/

#include <stdlib.h>
#include <string.h>
#include "INK_MD5.h"
#include "URL.h"

extern "C"
{
#include "ClusterHashStandalone.h"
}

#define CLUSTER_MAX_MACHINES                256
// less than 1% disparity at 255 machines, 32707 is prime less than 2^15
#define CLUSTER_HASH_TABLE_SIZE             32707

struct Machine
{
  unsigned int ip;
    Machine(unsigned int aip):ip(aip)
  {
  }
};

struct ClusterConfiguration
{
  int n_machines;
  Machine *machines[CLUSTER_MAX_MACHINES];
  unsigned char hash_table[CLUSTER_HASH_TABLE_SIZE];
};

#define STANDALONE 	1
#include "ClusterHash.cc"

static ClusterConfiguration cc;

inline unsigned int
cache_hash(INK_MD5 & md5)
{
  inku64 f = md5.fold();
  unsigned int mhash = f >> 32;
  return mhash;
}

static int
intcompare(int *i, int *j)
{
  return (*i > *j) ? 1 : ((*i < *j) ? -1 : 0);
}

void
build_standalone_cluster_hash_table(int n_machines, unsigned int *ip_addresses)
{
  cc.n_machines = n_machines;
  qsort((char *) ip_addresses, n_machines, sizeof(int), intcompare);
  for (int i = 0; i < n_machines; i++)
    cc.machines[i] = new Machine(ip_addresses[i]);
  build_cluster_hash_table(&cc);
}

unsigned int
standalone_machine_hash(char *url)
{
  HttpHeaderHeap heap;
  URL *xurl;
  INK_MD5 md5;
  int length;
  int s, e;

  s = 0;
  length = strlen(url);
  xurl = URL::create(&heap, url, length, s, e);
  if (!xurl)
    return (unsigned int) -1;
  xurl->get_MD5(&md5);

  unsigned int hash_value = cache_hash(md5);
  return cc.machines[cc.hash_table[hash_value % CLUSTER_HASH_TABLE_SIZE]]->ip;
}
