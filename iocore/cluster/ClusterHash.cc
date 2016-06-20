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

/****************************************************************************

  ClusterHash.cc
 ****************************************************************************/
#include "P_Cluster.h"

//
// Configuration of the cluster hash function
//
// machineClusterHash  - whether or not the random number generators
//                       are based on the machines or on the buckets
// boundClusterHash    - whether or not we force a fixed number of buckets
//                       to map to each machine
// randClusterHash     - whether or not to use system rand(3C)
//                       or a simple linear congruence random number
//                       generator
//
// This produces very stable results (computation time ~.6 seconds) on
// a UltraSparc at 143Mz.
// These are only global for testing purposes.
//
bool machineClusterHash = true;
bool boundClusterHash   = false;
bool randClusterHash    = false;

// This produces better speed for large numbers of machines > 18
//
// bool machineClusterHash = false;
// bool boundClusterHash = true;
// bool randClusterHash = true;

//
// Cluster Hash Table
//
// see Memo.ClusterHash for details
//

//
// Linear Congruence Random number generator

// Not very random, but it generates all the numbers
// within 1 period which is all we need.
//
inline unsigned short
next_rnd15(unsigned int *p)
{
  unsigned int seed = *p;
  seed              = 1103515145 * seed + 12345;
  seed              = seed & 0x7FFF;
  *p                = seed;
  return seed;
}

//
// Build the hash table
// This function is relatively expensive.
// It costs: (g++ at -02)
// ~.04 CPU seconds on a 143MHz Ultra 1 at 1 node
// ~.3 CPU seconds on a 143MHz Ultra 1 at 31 nodes
// Overall it is roughly linear in the number of nodes.
//
void
build_hash_table_machine(ClusterConfiguration *c)
{
  int left = CLUSTER_HASH_TABLE_SIZE;
  int m    = 0;
  int i    = 0;
  unsigned int rnd[CLUSTER_MAX_MACHINES];
  unsigned int mach[CLUSTER_MAX_MACHINES];
  int total = CLUSTER_HASH_TABLE_SIZE;

  for (i = 0; i < c->n_machines; i++) {
    int mine = total / (c->n_machines - i);
    mach[i]  = mine;
    total -= mine;
  }

  // seed the random number generator with the ip address
  // do a little xor folding to get it into 15 bits
  //
  for (m   = 0; m < c->n_machines; m++)
    rnd[m] = (((c->machines[m]->ip >> 15) & 0x7FFF) ^ (c->machines[m]->ip & 0x7FFF)) ^ (c->machines[m]->ip >> 30);

  // Initialize the table to "empty"
  //
  for (i             = 0; i < CLUSTER_HASH_TABLE_SIZE; i++)
    c->hash_table[i] = 255;

  // Until we have hit every element of the table, give each
  // machine a chance to select it's favorites.
  //
  m = 0;
  while (left) {
    if (!mach[m] && boundClusterHash) {
      m = (m + 1) % c->n_machines;
      continue;
    }
    do {
      if (randClusterHash) {
        i = ink_rand_r(&rnd[m]) % CLUSTER_HASH_TABLE_SIZE;
      } else
        i = next_rand(&rnd[m]) % CLUSTER_HASH_TABLE_SIZE;
    } while (c->hash_table[i] != 255);
    mach[m]--;
    c->hash_table[i] = m;
    left--;
    m = (m + 1) % c->n_machines;
  }
}

static void
build_hash_table_bucket(ClusterConfiguration *c)
{
  int i = 0;
  unsigned int rnd[CLUSTER_HASH_TABLE_SIZE];
  unsigned int mach[CLUSTER_MAX_MACHINES];
  int total = CLUSTER_HASH_TABLE_SIZE;

  for (i = 0; i < c->n_machines; i++) {
    int mine = total / (c->n_machines - i);
    mach[i]  = mine;
    total -= mine;
  }

  for (i   = 0; i < CLUSTER_HASH_TABLE_SIZE; i++)
    rnd[i] = i;

  for (i = 0; i < CLUSTER_HASH_TABLE_SIZE; i++) {
    unsigned char x = 0;
    do {
      if (randClusterHash) {
        x = ink_rand_r(&rnd[i]) % CLUSTER_MAX_MACHINES;
      } else
        x = next_rand(&rnd[i]) % CLUSTER_MAX_MACHINES;
    } while (x >= c->n_machines || (!mach[x] && boundClusterHash));
    mach[x]--;
    c->hash_table[i] = x;
  }
}

void
build_cluster_hash_table(ClusterConfiguration *c)
{
  if (machineClusterHash)
    build_hash_table_machine(c);
  else
    build_hash_table_bucket(c);
}
