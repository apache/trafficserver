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

#include <unistd.h>
#include "P_Cluster.h"
extern char cache_system_config_directory[PATH_NAME_MAX + 1];

MachineList *machines_config = NULL;
MachineList *cluster_config = NULL;

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
  cluster_machine = NEW(new ClusterMachine);
}

ClusterMachine::ClusterMachine(char *ahostname, unsigned int aip, int aport):
dead(false),
hostname(ahostname),
ip(aip),
cluster_port(aport),
msg_proto_major(0),
msg_proto_minor(0),
clusterHandler(0)
{
  EThread *thread = this_ethread();
  ProxyMutex *mutex = thread->mutex;
#ifndef INK_NO_CLUSTER
  CLUSTER_INCREMENT_DYN_STAT(CLUSTER_MACHINES_ALLOCATED_STAT);
#endif
  if (!aip) {
    char localhost[1024];
    if (!ahostname) {
      ink_release_assert(!gethostname(localhost, 1023));
      ahostname = localhost;
    }
    hostname = xstrdup(ahostname);

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

        ip = (unsigned int) -1; // 0xFFFFFFFF
        for (int i = 0; r->h_addr_list[i]; i++)
          if (ip > *(unsigned int *) r->h_addr_list[i])
            ip = *(unsigned int *) r->h_addr_list[i];
        if (ip == (unsigned int) -1)
          ip = 0;
      }
      //ip = htonl(ip); for the alpha!
    }
#endif // LOCAL_CLUSTER_TEST_MODE
  } else {

    ip = aip;

    ink_gethostbyaddr_r_data data;
    struct hostent *r = ink_gethostbyaddr_r((char *) &ip, sizeof(int),
                                            AF_INET, &data);

    if (r == NULL) {
      unsigned char x[4];
      memset(x, 0, sizeof(x));
      *(inku32 *) & x = (inku32) ip;
      Debug("machine_debug", "unable to reverse DNS %u.%u.%u.%u: %d", x[0], x[1], x[2], x[3], data.herrno);
    } else
      hostname = xstrdup(r->h_name);
  }
  if (hostname)
    hostname_len = strlen(hostname);
  else
    hostname_len = 0;
}

ClusterMachine::~ClusterMachine()
{
  if (hostname)
    xfree(hostname);
}

#ifndef INK_NO_CLUSTER
struct MachineTimeoutContinuation;
typedef int (MachineTimeoutContinuation::*McTimeoutContHandler) (int, void *);
struct MachineTimeoutContinuation:Continuation
{
  ClusterMachine *m;
  int dieEvent(int event, Event * e)
  {
    (void) event;
    (void) e;
    delete m;
    delete this;
      return EVENT_DONE;
  }
  MachineTimeoutContinuation(ClusterMachine * am):Continuation(NULL), m(am)
  {
    SET_HANDLER((McTimeoutContHandler) & MachineTimeoutContinuation::dieEvent);
  }
};

void
free_ClusterMachine(ClusterMachine * m)
{
  EThread *thread = this_ethread();
  ProxyMutex *mutex = thread->mutex;
  // delay before the final free
  CLUSTER_INCREMENT_DYN_STAT(CLUSTER_MACHINES_FREED_STAT);
  m->dead = true;
  eventProcessor.schedule_in(NEW(new MachineTimeoutContinuation(m)), MACHINE_TIMEOUT, ET_CALL);
}

void
free_MachineList(MachineList * l)
{
  new_Freer(l, MACHINE_TIMEOUT);
}

MachineList *
read_MachineList(char *filename, int afd)
{
  char line[256];
  int n = -1, i = 0, ln = 0, rlen;
  MachineList *l = NULL;
  ink_assert(filename || (afd != -1));
  char p[PATH_NAME_MAX];
  size_t remaining_str_size = sizeof p;
  if (filename) {
    ink_strncpy(p, cache_system_config_directory, remaining_str_size);
    remaining_str_size -= strlen(cache_system_config_directory);
    strncat(p, "/", remaining_str_size);
    remaining_str_size--;
    strncat(p, filename, remaining_str_size);
  }
  int fd = ((afd != -1) ? afd : open(p, O_RDONLY));
  if (fd >= 0) {
    while ((rlen = ink_file_fd_readline(fd, sizeof(line) - 1, line)) > 0) {
      ln++;
//      fprintf(stderr,"line #%d, rlen %d: %s\n",ln,rlen,line);
      if (*line == '#')
        continue;
      if (n == -1 && ParseRules::is_digit(*line)) {
        n = atoi(line);
        if (n > 0) {
          l = (MachineList *) xmalloc(sizeof(MachineList) + (n - 1) * sizeof(MachineListElement));
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
        *port++ = 0;
        l->machine[i].ip = inet_addr(line);
        if (-1 == (int) l->machine[i].ip) {
          if (afd == -1) {
            Warning("read machine list failure, bad ip, line %d", ln);
            return NULL;
          } else {
            char s[256];
            snprintf(s, sizeof s, "bad ip, line %d", ln);
            return (MachineList *) xstrdup(s);
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
          return (MachineList *) xstrdup(s);
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
        xfree(l);
      return (MachineList *) xstrdup("number of machines does not match length of list\n");
    }
  }
  return (afd != -1) ? (MachineList *) NULL : l;
}
#endif // INK_NO_CLUSTER
