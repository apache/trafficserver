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

  Machine.cc
 ****************************************************************************/

#include "ts/ink_platform.h"
#include "ts/ink_file.h"
#include <unistd.h>
#include "P_Cluster.h"
#include "ts/I_Layout.h"
extern int num_of_cluster_threads;

MachineList *machines_config = NULL;
MachineList *cluster_config  = NULL;

ProxyMutex *the_cluster_config_mutex;

static ClusterMachine *cluster_machine;

MachineList *
the_cluster_machines_config()
{
  return machines_config;
}

MachineList *
the_cluster_config()
{
  return cluster_config;
}

ClusterMachine *
this_cluster_machine()
{
  return cluster_machine;
}

void
create_this_cluster_machine()
{
  the_cluster_config_mutex = new_ProxyMutex();
  cluster_machine          = new ClusterMachine;
}

ClusterMachine::ClusterMachine(char *ahostname, unsigned int aip, int aport)
  : dead(false),
    hostname(ahostname),
    ip(aip),
    cluster_port(aport),
    num_connections(0),
    now_connections(0),
    free_connections(0),
    rr_count(0),
    msg_proto_major(0),
    msg_proto_minor(0),
    clusterHandlers(0)
{
  EThread *thread   = this_ethread();
  ProxyMutex *mutex = thread->mutex;
  CLUSTER_INCREMENT_DYN_STAT(CLUSTER_MACHINES_ALLOCATED_STAT);
  if (!aip) {
    char localhost[1024];
    if (!ahostname) {
      ink_release_assert(!gethostname(localhost, 1023));
      ahostname = localhost;
    }
    hostname = ats_strdup(ahostname);

// If we are running if the manager, it the our ip address for
//   clustering from the manager, so the manager can control what
//   interface we cluster over.  Otherwise figure it out ourselves
#ifdef LOCAL_CLUSTER_TEST_MODE
    ip = inet_addr("127.0.0.1");
#else
#ifdef CLUSTER_TEST
    int clustering_enabled = true;
#else
    int clustering_enabled = !!getenv("PROXY_CLUSTER_ADDR");
#endif
    if (clustering_enabled) {
      char *clusterIP = getenv("PROXY_CLUSTER_ADDR");
      Debug("cluster_note", "[Machine::Machine] Cluster IP addr: %s\n", clusterIP);
      ip = inet_addr(clusterIP);
    } else {
      ink_gethostbyname_r_data data;
      struct hostent *r = ink_gethostbyname_r(ahostname, &data);
      if (!r) {
        Warning("unable to DNS %s: %d", ahostname, data.herrno);
        ip = 0;
      } else {
        // lowest IP address

        ip = (unsigned int)-1; // 0xFFFFFFFF
        for (int i = 0; r->h_addr_list[i]; i++)
          if (ip > *(unsigned int *)r->h_addr_list[i])
            ip = *(unsigned int *)r->h_addr_list[i];
        if (ip == (unsigned int)-1)
          ip = 0;
      }
      // ip = htonl(ip); for the alpha!
    }
#endif // LOCAL_CLUSTER_TEST_MODE
  } else {
    ip = aip;

    ink_gethostbyaddr_r_data data;
    struct hostent *r = ink_gethostbyaddr_r((char *)&ip, sizeof(int), AF_INET, &data);

    if (r == NULL) {
      Alias32 x;
      memcpy(&x.u32, &ip, sizeof(x.u32));
      Debug("machine_debug", "unable to reverse DNS %u.%u.%u.%u: %d", x.byte[0], x.byte[1], x.byte[2], x.byte[3], data.herrno);
    } else
      hostname = ats_strdup(r->h_name);
  }
  if (hostname)
    hostname_len = strlen(hostname);
  else
    hostname_len = 0;

  num_connections = num_of_cluster_threads;
  clusterHandlers = (ClusterHandler **)ats_calloc(num_connections, sizeof(ClusterHandler *));
}

ClusterHandler *
ClusterMachine::pop_ClusterHandler(int no_rr)
{
  int find    = 0;
  int64_t now = rr_count;
  if (no_rr == 0) {
    ink_atomic_increment(&rr_count, 1);
  }

  /* will happen when ts start (cluster connection is not established) */
  while (!clusterHandlers[now % this->num_connections] && (find < this->num_connections)) {
    now++;
    find++;
  }
  return this->clusterHandlers[now % this->num_connections];
}

ClusterMachine::~ClusterMachine()
{
  ats_free(hostname);
  ats_free(clusterHandlers);
}

struct MachineTimeoutContinuation;
typedef int (MachineTimeoutContinuation::*McTimeoutContHandler)(int, void *);
struct MachineTimeoutContinuation : public Continuation {
  ClusterMachine *m;
  int
  dieEvent(int event, Event *e)
  {
    (void)event;
    (void)e;
    delete m;
    delete this;
    return EVENT_DONE;
  }

  MachineTimeoutContinuation(ClusterMachine *am) : Continuation(NULL), m(am)
  {
    SET_HANDLER((McTimeoutContHandler)&MachineTimeoutContinuation::dieEvent);
  }
};

void
free_ClusterMachine(ClusterMachine *m)
{
  EThread *thread   = this_ethread();
  ProxyMutex *mutex = thread->mutex;
  // delay before the final free
  CLUSTER_INCREMENT_DYN_STAT(CLUSTER_MACHINES_FREED_STAT);
  m->dead = true;
  eventProcessor.schedule_in(new MachineTimeoutContinuation(m), MACHINE_TIMEOUT, ET_CALL);
}

void
free_MachineList(MachineList *l)
{
  new_Freer(l, MACHINE_TIMEOUT);
}

MachineList *
read_MachineList(const char *filename, int afd)
{
  char line[256];
  int n = -1, i = 0, ln = 0;
  MachineList *l = NULL;
  ink_assert(filename || (afd != -1));
  ats_scoped_str path(RecConfigReadConfigPath(NULL, filename));

  int fd = ((afd != -1) ? afd : open(path, O_RDONLY));
  if (fd >= 0) {
    while (ink_file_fd_readline(fd, sizeof(line) - 1, line) > 0) {
      ln++;
      if (*line == '#')
        continue;
      if (n == -1 && ParseRules::is_digit(*line)) {
        n = atoi(line);
        if (n > 0) {
          l    = (MachineList *)ats_malloc(sizeof(MachineList) + (n - 1) * sizeof(MachineListElement));
          l->n = 0;
        } else {
          l = NULL;
        }
        continue;
      }
      if (l && ParseRules::is_digit(*line) && i < n) {
        char *port = strchr(line, ':');
        if (!port)
          goto Lfail;
        *port++          = 0;
        l->machine[i].ip = inet_addr(line);
        if (-1 == (int)l->machine[i].ip) {
          if (afd == -1) {
            Warning("read machine list failure, bad ip, line %d", ln);
            return NULL;
          } else {
            char s[256];
            snprintf(s, sizeof s, "bad ip, line %d", ln);
            return (MachineList *)ats_strdup(s);
          }
        }
        l->machine[i].port = atoi(port);
        if (!l->machine[i].port)
          goto Lfail;
        i++;
        l->n++;
        continue;
      Lfail:
        if (afd == -1) {
          Warning("read machine list failure, bad port, line %d", ln);
          return NULL;
        } else {
          char s[256];
          snprintf(s, sizeof s, "bad port, line %d", ln);
          return (MachineList *)ats_strdup(s);
        }
      }
    }
    close(fd);
  } else {
    Warning("read machine list failure, open failed");
    return NULL;
  }
  if (n >= 0) {
    if (i != n) {
      if (afd == -1) {
        Warning("read machine list failure, length mismatch");
        return NULL;
      } else
        ats_free(l);
      return (MachineList *)ats_strdup("number of machines does not match length of list\n");
    }
  }
  return (afd != -1) ? (MachineList *)NULL : l;
}
