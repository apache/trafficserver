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

  ClusterConfig.cc
****************************************************************************/

#include "P_Cluster.h"
// updated from the cluster port configuration variable
int cluster_port = DEFAULT_CLUSTER_PORT_NUMBER;

ClusterAccept::ClusterAccept(int *port, int send_bufsize, int recv_bufsize)
  : Continuation(0),
    p_cluster_port(port),
    socket_send_bufsize(send_bufsize),
    socket_recv_bufsize(recv_bufsize),
    current_cluster_port(-1),
    accept_action(0),
    periodic_event(0)
{
  mutex = new_ProxyMutex();
  SET_HANDLER(&ClusterAccept::ClusterAcceptEvent);
}

ClusterAccept::~ClusterAccept()
{
  mutex = 0;
}

void
ClusterAccept::Init()
{
  // Setup initial accept by simulating EVENT_INTERVAL
  // where cluster accept port has changed.

  current_cluster_port = ~*p_cluster_port;
  ClusterAcceptEvent(EVENT_INTERVAL, 0);

  // Setup periodic event to handle changing cluster accept port.
  periodic_event = eventProcessor.schedule_every(this, HRTIME_SECONDS(60));
}

void
ClusterAccept::ShutdownDelete()
{
  MUTEX_TRY_LOCK(lock, this->mutex, this_ethread());
  if (!lock.is_locked()) {
    eventProcessor.schedule_imm(this, ET_CALL);
    return;
  }
  // Kill all events and delete.
  if (accept_action) {
    accept_action->cancel();
    accept_action = 0;
  }
  if (periodic_event) {
    periodic_event->cancel();
    periodic_event = 0;
  }
  delete this;
}

int
ClusterAccept::ClusterAcceptEvent(int event, void *data)
{
  switch (event) {
  case EVENT_IMMEDIATE: {
    ShutdownDelete();
    return EVENT_DONE;
  }
  case EVENT_INTERVAL: {
    int cluster_port = *p_cluster_port;

    if (cluster_port != current_cluster_port) {
      // Configuration changed cluster port, redo accept on new port.
      if (accept_action) {
        accept_action->cancel();
        accept_action = 0;
      }

      NetProcessor::AcceptOptions opt;
      opt.recv_bufsize   = socket_recv_bufsize;
      opt.send_bufsize   = socket_send_bufsize;
      opt.etype          = ET_CLUSTER;
      opt.local_port     = cluster_port;
      opt.ip_family      = AF_INET;
      opt.localhost_only = false;

      accept_action = netProcessor.main_accept(this, NO_FD, opt);
      if (!accept_action) {
        Warning("Unable to accept cluster connections on port: %d", cluster_port);
      } else {
        current_cluster_port = cluster_port;
      }
    }
    return EVENT_CONT;
  }
  case NET_EVENT_ACCEPT: {
    ClusterAcceptMachine((NetVConnection *)data);
    return EVENT_DONE;
  }
  default: {
    Warning("ClusterAcceptEvent: received unknown event %d", event);
    return EVENT_DONE;
  }
  } // End of switch
}

int
ClusterAccept::ClusterAcceptMachine(NetVConnection *NetVC)
{
  // Validate remote IP address.
  unsigned int remote_ip = NetVC->get_remote_ip();
  MachineList *mc        = the_cluster_machines_config();

  if (mc && !mc->find(remote_ip)) {
    Note("Illegal cluster connection from %u.%u.%u.%u", DOT_SEPARATED(remote_ip));
    NetVC->do_io(VIO::CLOSE);
    return 0;
  }

  Debug(CL_NOTE, "Accepting machine %u.%u.%u.%u", DOT_SEPARATED(remote_ip));
  ClusterHandler *ch = new ClusterHandler;
  ch->machine        = new ClusterMachine(NULL, remote_ip);
  ch->ip             = remote_ip;
  ch->net_vc         = NetVC;
  eventProcessor.schedule_imm_signal(ch, ET_CLUSTER);
  return 1;
}

static void
make_cluster_connections(MachineList *l)
{
  //
  // Connect to all new machines.
  //
  uint32_t ip         = this_cluster_machine()->ip;
  int num_connections = this_cluster_machine()->num_connections;

  if (l) {
    for (int i = 0; i < l->n; i++) {
#ifdef LOCAL_CLUSTER_TEST_MODE
      if (ip < l->machine[i].ip || (ip == l->machine[i].ip && (cluster_port < l->machine[i].port))) {
#else
      if (ip < l->machine[i].ip) {
#endif
        for (int j = 0; j < num_connections; j++) {
          clusterProcessor.connect(l->machine[i].ip, l->machine[i].port, j);
        }
      }
    }
  }
}

int
machine_config_change(const char * /* name ATS_UNUSED */, RecDataT /* data_type ATS_UNUSED */, RecData data, void *cookie)
{
  // Handle changes to the cluster.config or machines.config
  // file.  cluster.config is the list of machines in the
  // cluster proper ( in the cluster hash table ).  machines.config
  // is the list of machines which communicate with the cluster.
  // This may include front-end load redirectors, machines going
  // up or coming down etc.
  //
  char *filename   = (char *)data.rec_string;
  MachineList *l   = read_MachineList(filename);
  MachineList *old = NULL;
#ifdef USE_SEPARATE_MACHINE_CONFIG
  switch ((int)cookie) {
  case MACHINE_CONFIG:
    old             = machines_config;
    machines_config = l;
    break;
  case CLUSTER_CONFIG:
    old            = cluster_config;
    cluster_config = l;
    make_cluster_connections(l);
    break;
  }
#else
  (void)cookie;
  old             = cluster_config;
  machines_config = l;
  cluster_config  = l;
  make_cluster_connections(l);
#endif
  if (old)
    free_MachineList(old);
  return 0;
}

void
do_machine_config_change(void *d, const char *s)
{
  char cluster_config_filename[PATH_NAME_MAX] = "";
  REC_ReadConfigString(cluster_config_filename, s, sizeof(cluster_config_filename) - 1);
  RecData data;
  data.rec_string = cluster_config_filename;
  machine_config_change(s, RECD_STRING, data, d);
}

/*************************************************************************/
// ClusterConfiguration member functions (Public Class)
/*************************************************************************/
ClusterConfiguration::ClusterConfiguration() : n_machines(0), changed(0)
{
  memset(machines, 0, sizeof(machines));
  memset(hash_table, 0, sizeof(hash_table));
}

/*************************************************************************/
// ConfigurationContinuation member functions (Internal Class)
/*************************************************************************/
struct ConfigurationContinuation;
typedef int (ConfigurationContinuation::*CfgContHandler)(int, void *);
struct ConfigurationContinuation : public Continuation {
  ClusterConfiguration *c;
  ClusterConfiguration *prev;

  int
  zombieEvent(int /* event ATS_UNUSED */, Event *e)
  {
    prev->link.next = NULL; // remove that next pointer
    SET_HANDLER((CfgContHandler)&ConfigurationContinuation::dieEvent);
    e->schedule_in(CLUSTER_CONFIGURATION_ZOMBIE);
    return EVENT_CONT;
  }

  int
  dieEvent(int event, Event *e)
  {
    (void)event;
    (void)e;
    delete c;
    delete this;
    return EVENT_DONE;
  }

  ConfigurationContinuation(ClusterConfiguration *cc, ClusterConfiguration *aprev) : Continuation(NULL), c(cc), prev(aprev)
  {
    mutex = new_ProxyMutex();
    SET_HANDLER((CfgContHandler)&ConfigurationContinuation::zombieEvent);
  }
};

static void
free_configuration(ClusterConfiguration *c, ClusterConfiguration *prev)
{
  //
  // Delete the configuration after a time.
  // The problem is that configurations change infrequently, and
  // are used in different threads, so reference counts are
  // relatively difficult and expensive.  The solution I have
  // chosen is to simply delete the object after some (very long)
  // time after it has ceased to be accessible.
  //
  eventProcessor.schedule_in(new ConfigurationContinuation(c, prev), CLUSTER_CONFIGURATION_TIMEOUT, ET_CALL);
}

ClusterConfiguration *
configuration_add_machine(ClusterConfiguration *c, ClusterMachine *m)
{
  // Build a new cluster configuration with the new machine.
  // Machines are stored in ip sorted order.
  //
  EThread *thread          = this_ethread();
  ProxyMutex *mutex        = thread->mutex;
  int i                    = 0;
  ClusterConfiguration *cc = new ClusterConfiguration(*c);

  // Find the place to insert this new machine
  //
  for (i = 0; i < cc->n_machines; i++) {
    if (cc->machines[i]->ip > m->ip)
      break;
  }

  // Move the other machines out of the way
  //
  for (int j            = cc->n_machines - 1; j >= i; j--)
    cc->machines[j + 1] = cc->machines[j];

  // Insert it
  //
  cc->machines[i] = m;
  cc->n_machines++;

  cc->link.next = c;
  cc->changed   = Thread::get_hrtime();
  ink_assert(cc->n_machines < CLUSTER_MAX_MACHINES);

  build_cluster_hash_table(cc);
  INK_MEMORY_BARRIER; // commit writes before freeing old hash table
  CLUSTER_INCREMENT_DYN_STAT(CLUSTER_CONFIGURATION_CHANGES_STAT);

  free_configuration(c, cc);
  return cc;
}

ClusterConfiguration *
configuration_remove_machine(ClusterConfiguration *c, ClusterMachine *m)
{
  EThread *thread   = this_ethread();
  ProxyMutex *mutex = thread->mutex;

  //
  // Build a new cluster configuration without a machine
  //
  ClusterConfiguration *cc = new ClusterConfiguration(*c);
  //
  // remove m and move others down
  //
  for (int i = 0; i < cc->n_machines - 1; i++)
    if (m == cc->machines[i])
      m = cc->machines[i] = cc->machines[i + 1];
  cc->n_machines--;

  ink_assert(cc->n_machines > 0);

  cc->link.next = c;
  cc->changed   = Thread::get_hrtime();

  build_cluster_hash_table(cc);
  INK_MEMORY_BARRIER; // commit writes before freeing old hash table
  CLUSTER_INCREMENT_DYN_STAT(CLUSTER_CONFIGURATION_CHANGES_STAT);

  free_configuration(c, cc);
  return cc;
}

//
// cluster_machine_at_depth()
//   Find a machine at a particular depth into the past.
//   We don't want to probe the current machine or machines
//   we have probed before, so we store a list of "past_probes".
//   If probe_depth and past_probes are NULL we only want the
//   owner (machine now as opposed to in the past).
//
ClusterMachine *
cluster_machine_at_depth(unsigned int hash, int *pprobe_depth, ClusterMachine **past_probes)
{
#ifdef CLUSTER_TOMCAT
  if (!cache_clustering_enabled)
    return NULL;
#endif
  ClusterConfiguration *cc      = this_cluster()->current_configuration();
  ClusterConfiguration *next_cc = cc;
  ink_hrtime now                = Thread::get_hrtime();
  int fake_probe_depth          = 0;
  int &probe_depth              = pprobe_depth ? (*pprobe_depth) : fake_probe_depth;
  int tprobe_depth              = probe_depth;

#ifdef CLUSTER_TEST
  if (cc->n_machines > 1) {
    for (int i = 0; i < cc->n_machines; ++i) {
      if (cc->machines[i] != this_cluster_machine()) {
        return cc->machines[i];
      }
    }
  }
#endif // CLUSTER_TEST

  while (1) {
    // If we are out of our depth, fail
    //
    if (probe_depth > CONFIGURATION_HISTORY_PROBE_DEPTH)
      break;

    // If there is no configuration, fail
    //
    if (!cc || !next_cc)
      break;

    cc      = next_cc;
    next_cc = next_cc->link.next;

    // Find the correct configuration
    //
    if (tprobe_depth) {
      if (cc->changed > (now + CLUSTER_CONFIGURATION_TIMEOUT))
        break;
      tprobe_depth--;
      continue;
    }

    ClusterMachine *m = cc->machine_hash(hash);

    // If it is not this machine, or a machine we have done before
    // and one that is still up, try again
    //
    bool ok = !(m == this_cluster_machine() || (past_probes && machine_in_vector(m, past_probes, probe_depth)) || m->dead);

    // Store the all but the last probe, so that we never return
    // the same machine
    //
    if (past_probes && probe_depth < CONFIGURATION_HISTORY_PROBE_DEPTH)
      past_probes[probe_depth] = m;
    probe_depth++;

    if (!ok) {
      if (!pprobe_depth)
        break; // don't go down if we don't have a depth
      continue;
    }

    return (m != this_cluster_machine()) ? m : NULL;
  }
  return NULL;
}

//
// initialize_thread_for_cluster()
//   This is not required since we have a separate handler
//   for each machine-machine pair, the pointers to which are
//   stored in the ClusterMachine structures
//
void
initialize_thread_for_cluster(EThread *e)
{
  (void)e;
}

/*************************************************************************/
// Cluster member functions (Public Class)
/*************************************************************************/
Cluster::Cluster()
{
}

// End of ClusterConfig.cc
