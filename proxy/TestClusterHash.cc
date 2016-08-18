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

  TestClusterHash.cc
 ****************************************************************************/
#include "Cluster.h"
#include "ts/ink_platform.h"

//
// This test function produces the table included
// in Memo.ClusterHash
//

void
test()
{
  int n[CLUSTER_MAX_MACHINES];
  int i;
  Machine *m;
  int j;
  ink_hrtime t, t2;
  int total;
  int high, low, share;
  int version = 7;

  while (version > -1) {
    // the select the version
    //
    machineClusterHash = !!(version & 1);
    boundClusterHash   = !!(version & 2);
    randClusterHash    = !!(version & 4);

    // fabricate fake cluster

    clusterProcessor.this_cluster = new Cluster;
    ClusterConfiguration *cc      = new ClusterConfiguration;
    cc->n_machines                = 1;
    cc->machines[0]               = this_cluster_machine();
    memset(cc->hash_table, 0, CLUSTER_HASH_TABLE_SIZE);
    clusterProcessor.this_cluster->configurations.push(cc);

    ClusterConfiguration *c = this_cluster()->current_configuration();

    printf("hash by %s - %s - %s\n", (machineClusterHash ? "MACHINE" : "BUCKET"), (boundClusterHash ? "BOUNDED" : "UNBOUND"),
           (randClusterHash ? "RAND" : "LINEAR CONGUENCE"));

    // from 1 to 32 machines

    for (i = 1; i < 32; i++) {
      m = new ClusterMachine(*this_cluster_machine());
      m->ip += i;
      t  = ink_get_hrtime();
      cc = configuration_add_machine(c, m);
      t2 = ink_get_hrtime();

      //
      // Compute new distribution
      //
      high = 0;
      low  = CLUSTER_HASH_TABLE_SIZE + 1;
      for (j = 0; j < cc->n_machines; j++)
        n[j] = 0;
      for (j = 0; j < CLUSTER_HASH_TABLE_SIZE; j++) {
        ink_assert(cc->hash_table[j] < cc->n_machines);
        n[cc->hash_table[j]]++;
      }
      total = CLUSTER_HASH_TABLE_SIZE;
      for (j = 0; j < cc->n_machines; j++) {
        total -= n[j];
        if (low > n[j])
          low = n[j];
        if (high < n[j])
          high = n[j];
      }
      ink_assert(!total);
      printf("n = %d:", i);
      printf(" high = %d low = %d high/low = %f", high, low, (float)high / (float)low);

      //
      // Compute sharing with n-1
      //
      share = 0;
      for (j = 0; j < CLUSTER_HASH_TABLE_SIZE; j++) {
        if (c->machines[c->hash_table[j]] == cc->machines[cc->hash_table[j]])
          share++;
      }
      printf(" shared = %d %%%6.2f", share, (float)share / (float)CLUSTER_HASH_TABLE_SIZE);

      printf(" time = %f secs\n", ((float)(t2 - t) / (float)HRTIME_SECONDS(1)));
      c = cc;
    }

    version--;
  }
}
