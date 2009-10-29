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

  ClusterAPI.cc

	Support for Cluster RPC API.
****************************************************************************/
#include "P_Cluster.h"

#ifdef NON_MODULAR
#include "InkAPIInternal.h"

class ClusterAPIPeriodicSM;
static void send_machine_online_list(INKClusterStatusHandle_t *);

typedef struct node_callout_entry
{
  Ptr<ProxyMutex> mutex;
  INKClusterStatusFunction func;
  int state;                    // See NE_STATE_XXX defines
} node_callout_entry_t;

#define NE_STATE_FREE			0
#define NE_STATE_INITIALIZED		1

#define MAX_CLUSTERSTATUS_CALLOUTS 	32

static ProxyMutex *ClusterAPI_mutex;
static ClusterAPIPeriodicSM *periodicSM;
static Action *periodicSM_action;

static node_callout_entry_t status_callouts[MAX_CLUSTERSTATUS_CALLOUTS];
static INKClusterRPCFunction RPC_Functions[API_END_CLUSTER_FUNCTION];

#define INDEX_TO_CLUSTER_STATUS_HANDLE(i) ((INKClusterStatusHandle_t)((i)))
#define CLUSTER_STATUS_HANDLE_TO_INDEX(h) ((int) ((h)))
#define NODE_HANDLE_TO_IP(h) (*((struct in_addr *) &((h))))
#define RPC_FUNCTION_KEY_TO_CLUSTER_NUMBER(k) ((int)((k)))
#define IP_TO_NODE_HANDLE(ip) ((INKNodeHandle_t)((ip)))
#define SIZEOF_RPC_MSG_LESS_DATA (sizeof(INKClusterRPCMsg_t) - \
	 (sizeof(INKClusterRPCMsg_t) - sizeof(INKClusterRPCHandle_t)))

typedef struct RPCHandle
{
  union
  {                             // Note: All union elements are assumed to be the same size
    //       sizeof(u.internal) == sizeof(u.external)
    INKClusterRPCHandle_t external;
    struct real_format
    {
      int cluster_function;
      int magic;
    } internal;
  } u;
} RPCHandle_t;

#define RPC_HANDLE_MAGIC 0x12345678

class MachineStatusSM;
typedef int (MachineStatusSM::*MachineStatusSMHandler) (int, void *);
class MachineStatusSM:public Continuation
{
public:
  // Broadcast constructor
  MachineStatusSM(INKNodeHandle_t h, INKNodeStatus_t s):_node_handle(h), _node_status(s), _status_handle(0),
    _broadcast(1), _restart(0), _next_n(0)
  {
    SET_HANDLER((MachineStatusSMHandler)
                & MachineStatusSM::MachineStatusSMEvent);
  }
  // Unicast constructor
  MachineStatusSM(INKNodeHandle_t h, INKNodeStatus_t s,
                  INKClusterStatusHandle_t sh):_node_handle(h), _node_status(s), _status_handle(sh),
    _broadcast(0), _restart(0), _next_n(0)
  {
    SET_HANDLER((MachineStatusSMHandler)
                & MachineStatusSM::MachineStatusSMEvent);
  }
  // Send machine online list constructor
MachineStatusSM(INKClusterStatusHandle_t sh):
  _node_handle(0), _node_status(NODE_ONLINE), _status_handle(sh), _broadcast(0), _restart(0), _next_n(0) {
    SET_HANDLER((MachineStatusSMHandler)
                & MachineStatusSM::MachineStatusSMEvent);
  }
  ~MachineStatusSM() {
  }
  int MachineStatusSMEvent(Event * e, void *d);

private:
  INKNodeHandle_t _node_handle;
  INKNodeStatus_t _node_status;
  INKClusterStatusHandle_t _status_handle;      // Valid only if !_broadcast
  int _broadcast;
  int _restart;
  int _next_n;
};

int
MachineStatusSM::MachineStatusSMEvent(Event * e, void *d)
{
  int n;
  EThread *et = this_ethread();

  if (_broadcast) {
    /////////////////////////////////////////////////////
    // Broadcast node transition to all subscribers
    /////////////////////////////////////////////////////
    n = _restart ? _next_n : 0;
    for (; n < MAX_CLUSTERSTATUS_CALLOUTS; ++n) {
      if (status_callouts[n].func && (status_callouts[n].state == NE_STATE_INITIALIZED)) {

        MUTEX_TRY_LOCK(lock, status_callouts[n].mutex, et);
        if (lock) {
          status_callouts[n].func(&_node_handle, _node_status);
          Debug("cluster_api", "callout: n %d ([%u.%u.%u.%u], %d)", n, DOT_SEPARATED(_node_handle), _node_status);
        } else {
          _restart = 1;
          _next_n = n;
          return EVENT_CONT;
        }
      }
    }
  } else {
    if (!_node_handle) {
      /////////////////////////////////////////////////////
      // Send online node list to a specific subscriber
      /////////////////////////////////////////////////////
      n = CLUSTER_STATUS_HANDLE_TO_INDEX(_status_handle);
      if (status_callouts[n].func) {
        MUTEX_TRY_LOCK(lock, status_callouts[n].mutex, et);
        if (lock) {
          int mi;
          unsigned int my_ipaddr = (this_cluster_machine())->ip;
          ClusterConfiguration *cc;

          INKNodeHandle_t nh;

          cc = this_cluster()->current_configuration();
          if (cc) {
            for (mi = 0; mi < cc->n_machines; ++mi) {
              if (cc->machines[mi]->ip != my_ipaddr) {
                nh = IP_TO_NODE_HANDLE(cc->machines[mi]->ip);
                status_callouts[n].func(&nh, NODE_ONLINE);

                Debug("cluster_api",
                      "initial callout: n %d ([%u.%u.%u.%u], %d)", n, DOT_SEPARATED(cc->machines[mi]->ip), NODE_ONLINE);
              }
            }
          }
          status_callouts[n].state = NE_STATE_INITIALIZED;

        } else {
          _restart = 1;
          _next_n = n;
          return EVENT_CONT;
        }
      }
    } else {
      /////////////////////////////////////////////////////
      // Send node status to a specific subscriber
      /////////////////////////////////////////////////////
      n = CLUSTER_STATUS_HANDLE_TO_INDEX(_status_handle);
      if (status_callouts[n].func) {
        MUTEX_TRY_LOCK(lock, status_callouts[n].mutex, et);
        if (lock) {
          status_callouts[n].func(&_node_handle, _node_status);

          Debug("cluster_api",
                "directed callout: n %d ([%u.%u.%u.%u], %d)", n, DOT_SEPARATED(_node_handle), _node_status);
        } else {
          _restart = 1;
          _next_n = n;
          return EVENT_CONT;
        }
      }
    }
  }
  delete this;
  return EVENT_DONE;
}

class ClusterAPIPeriodicSM;
typedef int (ClusterAPIPeriodicSM::*ClusterAPIPeriodicSMHandler) (int, void *);
class ClusterAPIPeriodicSM:public Continuation
{
public:
  ClusterAPIPeriodicSM(ProxyMutex * m):Continuation(m), _active_msmp(0)
  {
    SET_HANDLER((ClusterAPIPeriodicSMHandler)
                & ClusterAPIPeriodicSM::ClusterAPIPeriodicSMEvent);
  }
   ~ClusterAPIPeriodicSM()
  {
  }
  int ClusterAPIPeriodicSMEvent(int, void *);
  MachineStatusSM *GetNextSM();

private:
  MachineStatusSM * _active_msmp;
};

static InkAtomicList status_callout_atomic_q;
static Queue<MachineStatusSM> status_callout_q;

MachineStatusSM *
ClusterAPIPeriodicSM::GetNextSM()
{
  MachineStatusSM *msmp;
  MachineStatusSM *msmp_next;

  while (1) {
    msmp = status_callout_q.pop();
    if (!msmp) {
      msmp = (MachineStatusSM *)
        ink_atomiclist_popall(&status_callout_atomic_q);
      if (msmp) {
        while (msmp) {
          msmp_next = (MachineStatusSM *) msmp->link.next;
          msmp->link.next = 0;
          status_callout_q.push(msmp);
          msmp = msmp_next;
        }
        continue;
      } else {
        break;
      }
    } else {
      break;
    }
  }
  return msmp;
}

int
ClusterAPIPeriodicSM::ClusterAPIPeriodicSMEvent(int e, void *d)
{
  // Maintain node status event order by serializing the processing.
  int ret;

  while (1) {
    if (_active_msmp) {
      ret = _active_msmp->handleEvent(e, d);
      if (ret != EVENT_DONE) {
        return EVENT_CONT;
      }
    }
    _active_msmp = GetNextSM();
    if (!_active_msmp) {
      break;
    }
  }
  return EVENT_CONT;
}

void
clusterAPI_init()
{
  MachineStatusSM *mssmp = 0;
  ink_atomiclist_init(&status_callout_atomic_q,
                      "cluster API status_callout_q", (char *) &mssmp->link.next - (char *) mssmp);
  ClusterAPI_mutex = new_ProxyMutex();
  MUTEX_TRY_LOCK(lock, ClusterAPI_mutex, this_ethread());
  ink_release_assert(lock);     // Should never fail
  periodicSM = NEW(new ClusterAPIPeriodicSM(ClusterAPI_mutex));

  periodicSM_action = eventProcessor.schedule_every(periodicSM, HRTIME_SECONDS(1), ET_CALL);
}

/*
 *  Add the given function to the node status callout list which is
 *  invoked on each machine up/down transition.
 *
 *  Note: Using blocking mutex since interface is synchronous and is only 
 *	  called at plugin load time. 
 */
int
INKAddClusterStatusFunction(INKClusterStatusFunction Status_Function, INKMutex m, INKClusterStatusHandle_t * h)
{
  Debug("cluster_api", "INKAddClusterStatusFunction func 0x%x", Status_Function);
  int n;
  EThread *e = this_ethread();

  ink_release_assert(Status_Function);
  MUTEX_TAKE_LOCK(ClusterAPI_mutex, e);
  for (n = 0; n < MAX_CLUSTERSTATUS_CALLOUTS; ++n) {
    if (!status_callouts[n].func) {
      status_callouts[n].mutex = (ProxyMutex *) m;
      status_callouts[n].func = Status_Function;
      MUTEX_UNTAKE_LOCK(ClusterAPI_mutex, e);
      *h = INDEX_TO_CLUSTER_STATUS_HANDLE(n);

      Debug("cluster_api", "INKAddClusterStatusFunction: func 0x%x n %d", Status_Function, n);
      return 0;
    }
  }
  MUTEX_UNTAKE_LOCK(ClusterAPI_mutex, e);
  return 1;
}

/*
 *  Remove the given function from the node status callout list 
 *  established via INKAddClusterStatusFunction().
 *
 *  Note: Using blocking mutex since interface is synchronous and is only 
 *	  called at plugin unload time (unload currently not supported). 
 */
int
INKDeleteClusterStatusFunction(INKClusterStatusHandle_t * h)
{
  int n = CLUSTER_STATUS_HANDLE_TO_INDEX(*h);
  EThread *e = this_ethread();

  ink_release_assert((n >= 0) && (n < MAX_CLUSTERSTATUS_CALLOUTS));
  Debug("cluster_api", "INKDeleteClusterStatusFunction: n %d", n);

  MUTEX_TAKE_LOCK(ClusterAPI_mutex, e);
  status_callouts[n].mutex = 0;
  status_callouts[n].func = (INKClusterStatusFunction) 0;
  status_callouts[n].state = NE_STATE_FREE;
  MUTEX_UNTAKE_LOCK(ClusterAPI_mutex, e);

  return 0;
}

int
INKNodeHandleToIPAddr(INKNodeHandle_t * h, struct in_addr *in)
{
  *in = NODE_HANDLE_TO_IP(*h);
  return 0;
}

void
INKGetMyNodeHandle(INKNodeHandle_t * h)
{
  *h = IP_TO_NODE_HANDLE((this_cluster_machine())->ip);
}

/*
 *  Enable node status callouts for the added callout entry.
 *  Issued once after the call to INKAddClusterStatusFunction()
 *  to get the current node configuration.  All subsequent
 *  callouts are updates to the state obtained at this point.
 */
void
INKEnableClusterStatusCallout(INKClusterStatusHandle_t * h)
{
  int ci = CLUSTER_STATUS_HANDLE_TO_INDEX(*h);
  // This isn't used.
  // int my_ipaddr = (this_cluster_machine())->ip;
  EThread *e = this_ethread();

  NOWARN_UNUSED(e);

  ink_release_assert((ci >= 0) && (ci < MAX_CLUSTERSTATUS_CALLOUTS));

  if (status_callouts[ci].state == NE_STATE_INITIALIZED) {
    return;
  }

  Debug("cluster_api", "INKEnableClusterStatusCallout: n %d", ci);
  send_machine_online_list(h);
}

static void
send_machine_online_list(INKClusterStatusHandle_t * h)
{
  MachineStatusSM *msm = NEW(new MachineStatusSM(*h));

  ink_atomiclist_push(&status_callout_atomic_q, (void *) msm);
}

/*
 *  Send node online to a specific cluster status entry.
 */
// This doesn't seem to be used...
#ifdef NOT_USED_HERE
static void
directed_machine_online(int Ipaddr, INKClusterStatusHandle_t * h)
{
  MachineStatusSM *msm = NEW(new MachineStatusSM(IP_TO_NODE_HANDLE(Ipaddr), NODE_ONLINE, *h));

  ink_atomiclist_push(&status_callout_atomic_q, (void *) msm);
}
#endif

/*
 *  Called directly by the Cluster upon detection of node online.
 */
void
machine_online_APIcallout(int Ipaddr)
{
  MachineStatusSM *msm = NEW(new MachineStatusSM(IP_TO_NODE_HANDLE(Ipaddr), NODE_ONLINE));

  ink_atomiclist_push(&status_callout_atomic_q, (void *) msm);
}

/*
 *  Called directly by the Cluster upon detection of node offline.
 */
void
machine_offline_APIcallout(int Ipaddr)
{
  MachineStatusSM *msm = NEW(new MachineStatusSM(IP_TO_NODE_HANDLE(Ipaddr), NODE_OFFLINE));

  ink_atomiclist_push(&status_callout_atomic_q, (void *) msm);
}

/*
 *  Associate the given RPC function with the given key.
 *
 *  Note: Using blocking mutex since interface is synchronous and is only 
 *	  called at plugin load time. 
 */
int
INKAddClusterRPCFunction(INKClusterRPCKey_t k, INKClusterRPCFunction func, INKClusterRPCHandle_t * h)
{
  RPCHandle_t handle;
  int n = RPC_FUNCTION_KEY_TO_CLUSTER_NUMBER(k);
  EThread *e = this_ethread();

  ink_release_assert(func);
  ink_release_assert((n >= API_STARECT_CLUSTER_FUNCTION)
                     && (n <= API_END_CLUSTER_FUNCTION));
  Debug("cluster_api", "INKAddClusterRPCFunction: key %d func 0x%x", k, func);

  handle.u.internal.cluster_function = n;
  handle.u.internal.magic = RPC_HANDLE_MAGIC;

  MUTEX_TAKE_LOCK(ClusterAPI_mutex, e);
  if (n < API_END_CLUSTER_FUNCTION)
    RPC_Functions[n] = func;
  MUTEX_UNTAKE_LOCK(ClusterAPI_mutex, e);

  *h = handle.u.external;
  return 0;
}

/*
 *  Remove the given RPC function added via INKAddClusterRPCFunction().
 *
 *  Note: Using blocking mutex since interface is synchronous and is only 
 *	  called at plugin unload time (unload currently not supported).
 */
int
INKDeleteClusterRPCFunction(INKClusterRPCHandle_t * rpch)
{
  RPCHandle_t *h = (RPCHandle_t *) rpch;
  EThread *e = this_ethread();

  ink_release_assert(((h->u.internal.cluster_function >= API_STARECT_CLUSTER_FUNCTION)
                      && (h->u.internal.cluster_function <= API_END_CLUSTER_FUNCTION)));
  Debug("cluster_api", "INKDeleteClusterRPCFunction: n %d", h->u.internal.cluster_function);

  MUTEX_TAKE_LOCK(ClusterAPI_mutex, e);
  RPC_Functions[h->u.internal.cluster_function] = 0;
  MUTEX_UNTAKE_LOCK(ClusterAPI_mutex, e);
  return 0;
}

/*
 *  Cluster calls us here for each RPC API function.
 */
void
default_api_ClusterFunction(ClusterMachine * m, void *data, int len)
{
  Debug("cluster_api", "default_api_ClusterFunction: [%u.%u.%u.%u] data 0x%x len %d", DOT_SEPARATED(m->ip), data, len);

  INKClusterRPCMsg_t *msg = (INKClusterRPCMsg_t *) data;
  RPCHandle_t *rpch = (RPCHandle_t *) & msg->m_handle;
  int cluster_function = rpch->u.internal.cluster_function;

  ink_release_assert((size_t) len >= sizeof(INKClusterRPCMsg_t));
  ink_release_assert(((cluster_function >= API_STARECT_CLUSTER_FUNCTION)
                      && (cluster_function <= API_END_CLUSTER_FUNCTION)));

  if (cluster_function < API_END_CLUSTER_FUNCTION && RPC_Functions[cluster_function]) {
    int msg_data_len = len - SIZEOF_RPC_MSG_LESS_DATA;
    INKNodeHandle_t nh = IP_TO_NODE_HANDLE(m->ip);
    (*RPC_Functions[cluster_function]) (&nh, msg, msg_data_len);
  } else {
    clusterProcessor.free_remote_data((char *) data, len);
  }
}

/*
 *  Free INKClusterRPCMsg_t received via the RPC function.
 */
void
INKFreeRPCMsg(INKClusterRPCMsg_t * msg, int msg_data_len)
{
  RPCHandle_t *rpch = (RPCHandle_t *) & msg->m_handle;
  ink_release_assert(rpch->u.internal.magic == RPC_HANDLE_MAGIC);
  Debug("cluster_api", "INKFreeRPCMsg: msg 0x%x msg_data_len %d", msg, msg_data_len);

  clusterProcessor.free_remote_data((char *) msg, msg_data_len + SIZEOF_RPC_MSG_LESS_DATA);
}

/*
 *  Allocate a message structure for use in the call to INKSendClusterRPC().
 */
INKClusterRPCMsg_t *
INKAllocClusterRPCMsg(INKClusterRPCHandle_t * h, int data_size)
{
  ink_debug_assert(data_size >= 4);
  if (data_size < 4) {
    /* Message must be at least 4 bytes in length */
    return (INKClusterRPCMsg_t *) 0;
  }

  INKClusterRPCMsg_t *rpcm;
  OutgoingControl *c = OutgoingControl::alloc();

  c->len = sizeof(OutgoingControl *) + SIZEOF_RPC_MSG_LESS_DATA + data_size;
  c->alloc_data();
  *((OutgoingControl **) c->data) = c;

  rpcm = (INKClusterRPCMsg_t *) (c->data + sizeof(OutgoingControl *));
  rpcm->m_handle = *h;

  /*
   * Note: We have carefully constructed INKClusterRPCMsg_t so
   *       m_data[] is 8 byte aligned.  This allows the user to
   *       cast m_data[] to any type without any consideration
   *       for alignment issues.
   */
  return rpcm;
}

/*
 *  Send the given message to the specified node.
 */
int
INKSendClusterRPC(INKNodeHandle_t * nh, INKClusterRPCMsg_t * msg)
{
  struct in_addr ipaddr = NODE_HANDLE_TO_IP(*nh);
  RPCHandle_t *rpch = (RPCHandle_t *) & msg->m_handle;

  OutgoingControl *c = *((OutgoingControl **)
                         ((char *) msg - sizeof(OutgoingControl *)));
  ClusterConfiguration * cc = this_cluster()->current_configuration();
  ClusterMachine *m;

  ink_release_assert(rpch->u.internal.magic == RPC_HANDLE_MAGIC);

  if ((m = cc->find(ipaddr.s_addr))) {
    int len = c->len - sizeof(OutgoingControl *);
    ink_release_assert((size_t) len >= sizeof(INKClusterRPCMsg_t));

    clusterProcessor.invoke_remote(m, rpch->u.internal.cluster_function,
                                   msg, len, (CLUSTER_OPT_STEAL | CLUSTER_OPT_DATA_IS_OCONTROL));
    Debug("cluster_api",
          "INKSendClusterRPC: msg 0x%x dlen %d [%u.%u.%u.%u] sent", msg, len, DOT_SEPARATED(ipaddr.s_addr));
  } else {
    Debug("cluster_api", "INKSendClusterRPC: msg 0x%x to [%u.%u.%u.%u] dropped", msg, DOT_SEPARATED(ipaddr.s_addr));
    c->freeall();
  }

  return 0;
}
#endif /* NON_MODULAR */

/*
 *  End of ClusterAPI.cc
 */
