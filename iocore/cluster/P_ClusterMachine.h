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

  Machine.h
 ****************************************************************************/

//
//  The Machine is the set of processes which share part of an
//  address space.
//

#ifndef _P_ClusterMachine_h
#define _P_ClusterMachine_h

//
// Timeout the Machine * this amount of time after they
// fall out of the current configuration that are deleted.
//
#define MACHINE_TIMEOUT (HRTIME_DAY * 2)

//
//  This is the time processors should delay before freeing up resouces
//  which are shared with other threads in non-long running operations.
//  For example, a Machine * is returned by the hash and used to do
//  a remote invoke.  For the pointer to remain valid (or be recognized as
//  invalid) he resource should not be raclaimed for NO_RACE_DELAY.
//
//  Long running operations should use more sophisticated synchronization.
//
#define NO_RACE_DELAY HRTIME_HOUR // a long long time

struct ClusterHandler; // Leave this a class - VC++ gets very anal  ~SR -- which version of VC++? ~igalic

struct ClusterMachine : public Server {
  bool dead;
  char *hostname;
  int hostname_len;
  //
  // The network address of the current machine,
  // stored in network byte order
  //
  unsigned int ip;
  int cluster_port;
  int num_connections;
  int now_connections;
  int free_connections;
  int64_t rr_count;

  Link<ClusterMachine> link;

  // default for localhost
  ClusterMachine(char *hostname = NULL, unsigned int ip = 0, int acluster_port = 0);
  ~ClusterMachine();
  ClusterHandler *pop_ClusterHandler(int no_rr = 0);

  // Cluster message protocol version
  uint16_t msg_proto_major;
  uint16_t msg_proto_minor;

  // Private data for ClusterProcessor
  //
  ClusterHandler **clusterHandlers;
};

struct MachineListElement {
  unsigned int ip;
  int port;
};

struct MachineList {
  int n;
  MachineListElement machine[1];
  MachineListElement *
  find(unsigned int ip, int port = 0)
  {
    for (int i = 0; i < n; i++)
      if (machine[i].ip == ip && (!port || machine[i].port == port))
        return &machine[i];
    return NULL;
  }
};

MachineList *read_MachineList(const char *filename, int test_fd = -1);
void free_MachineList(MachineList *l);

struct clusterConfigFile {
  char *
  parseFile(int fd)
  {
    return (char *)read_MachineList(NULL, fd);
  }
};

inkcoreapi ClusterMachine *this_cluster_machine();
void create_this_cluster_machine();

void free_ClusterMachine(ClusterMachine *m);

MachineList *the_cluster_machines_config();
MachineList *the_cluster_config();
extern ProxyMutex *the_cluster_config_mutex;

//
// Private
//
extern MachineList *machines_config;
extern MachineList *cluster_config;

#endif /* _Machine_h */
