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

/* clusterRPC.c:  Example usage of the Cluster RPC API
 *                   
 **************************************************************************
 *   INTERNAL USE ONLY.  NOT FOR GENERAL DISTRIBUTION.
 **************************************************************************
 *
 *
 *	Usage:	
 * 	(NT): clusterRPC.dll 
 * 	(Solaris): clusterRPC.so 
 *
 *
 */

#include <stdio.h>
#include "InkAPI.h"
#include "InkAPIPrivate.h"

/****************************************************************************
 *  Declarations.
 ****************************************************************************/
#define HELLO_MSG_VERSION   1
typedef struct hello_msg
{
  int hm_version;
  INKNodeHandle_t hm_source_handle;
  INKNodeHandle_t hm_dest_handle;
  int hm_instance;
  int hm_data_size;
  int hm_data;
} hello_msg_t;

typedef struct msg_log
{
  INKNodeHandle_t ml_nh;
  int ml_msgs_received;
  int ml_last_msgs_received;
  int ml_bytes_received;
} msg_log_t;

#define DOT_SEPARATED(_x)                             \
((unsigned char*)&(_x))[0], ((unsigned char*)&(_x))[1],   \
  ((unsigned char*)&(_x))[2], ((unsigned char*)&(_x))[3]

#define PLUGIN_DEBUG_TAG 	"cluster_rpc_plugin"
#define PLUGIN_DEBUG_ERR_TAG    "cluster_rpc_plugin-error"

/****************************************************************************
 *  Global data declarations.
 ****************************************************************************/
int clusterRPC_plugin_shutdown;
static INKMutex node_status_mutex;
static INKClusterStatusHandle_t status_callout_handle;
static INKClusterRPCHandle_t rpc_wireless_f10_handle;

static INKCont periodic_event_cont;
static INKAction periodic_event_action;
static int periodic_event_callouts;

static INKNodeHandle_t my_node_handle;
static INKNodeHandle_t nodes[MAX_CLUSTER_NODES + 1];    /* entry 0 not used */
static int online_nodes;

static int msg_instance;
static msg_log_t log[MAX_CLUSTER_NODES];
static int total_msgs_received;

static void clusterRPC_init();

int
check_ts_version()
{

  const char *ts_version = INKTrafficServerVersionGet();
  int result = 0;

  if (ts_version) {
    int major_ts_version = 0;
    int minor_ts_version = 0;
    int patch_ts_version = 0;

    if (sscanf(ts_version, "%d.%d.%d", &major_ts_version, &minor_ts_version, &patch_ts_version) != 3) {
      return 0;
    }

    /* Since this is an TS-SDK 2.0 plugin, we need at
       least Traffic Server 3.5.2 to run */
    if (major_ts_version > 3) {
      result = 1;
    } else if (major_ts_version == 3) {
      if (minor_ts_version > 5) {
        result = 1;
      } else if (minor_ts_version == 5) {
        if (patch_ts_version >= 2) {
          result = 1;
        }
      }
    }
  }

  return result;
}

void
INKPluginInit(int argc, const char *argv[])
{
  INKPluginRegistrationInfo info;

  info.plugin_name = "cluster-RPC";
  info.vendor_name = "MyCompany";
  info.support_email = "ts-api-support@MyCompany.com";

  if (!INKPluginRegister(INK_SDK_VERSION_2_0, &info)) {
    INKError("Plugin registration failed. \n");
  }

  if (!check_ts_version()) {
    INKError("Plugin requires Traffic Server 3.5.2 or later\n");
    return;
  }
  clusterRPC_init();
}

static void
shutdown()
{
  INKActionCancel(periodic_event_action);
  INKDeleteClusterStatusFunction(&status_callout_handle);
  INKDeleteClusterRPCFunction(&rpc_wireless_f10_handle);
}

static int
find_node_entry(INKNodeHandle_t nh)
{
  int n;

  for (n = 1; n <= MAX_CLUSTER_NODES; ++n) {
    if (nodes[n] == nh) {
      return n;
    }
  }
  return 0;
}

static int
find_free_node_entry()
{
  int n;

  for (n = 1; n <= MAX_CLUSTER_NODES; ++n) {
    if (nodes[n] == 0) {
      return n;
    }
  }
  return 0;
}

/*****************************************************************************
 *  Handler for node status callouts.
 *****************************************************************************/
static void
status_callout(INKNodeHandle_t * nhp, INKNodeStatus_t status)
{
    /*************************************************************************
     *  Note: Cluster always calls us with 'node_status_mutex' held.
     *************************************************************************/
  int found, n;
  INKNodeHandle_t nh = *nhp;
  struct in_addr in;

  INKNodeHandleToIPAddr(&nh, &in);
  found = find_node_entry(nh);

  if (status == NODE_ONLINE) {
    if (!found) {
      n = find_free_node_entry();
      if (n > 0) {
        nodes[n] = nh;
        online_nodes++;

        INKDebug(PLUGIN_DEBUG_TAG, "Node [%u.%u.%u.%u] online, nodes=%d\n", DOT_SEPARATED(in.s_addr), online_nodes);
      } else {
        /* Should never happen */
        INKDebug(PLUGIN_DEBUG_ERR_TAG, "clusterRPC plugin: No free entries.\n");

        INKDebug(PLUGIN_DEBUG_TAG,
                 "Node [%u.%u.%u.%u] online observed, nodes=%d\n", DOT_SEPARATED(in.s_addr), online_nodes);
      }
    } else {
      INKDebug(PLUGIN_DEBUG_TAG,
               "Duplicate node [%u.%u.%u.%u] online, nodes=%d\n", DOT_SEPARATED(in.s_addr), online_nodes);
    }
  } else {
    if (found) {
      nodes[found] = 0;
      online_nodes--;
      INKDebug(PLUGIN_DEBUG_TAG, "Node [%u.%u.%u.%u] offline, nodes=%d\n", DOT_SEPARATED(in.s_addr), online_nodes);
    } else {
      INKDebug(PLUGIN_DEBUG_TAG,
               "Unexpected node [%u.%u.%u.%u] offline, nodes=%d\n", DOT_SEPARATED(in.s_addr), online_nodes);
    }
  }
}

static void
fill_data(char *p, int size)
{
  int n;
  char val = random() % 256;

  for (n = 0; n < size; ++n)
    p[n] = val++;
}

static int
check_data(char *p, int size)
{
  int n;
  char val = p[0];

  for (n = 0; n < size; ++n, ++val) {
    if (p[n] != val) {
      INKDebug(PLUGIN_DEBUG_ERR_TAG, "check_data fail actual %d expected %d n %d data 0x%x\n", p[n], val, n, &p[n]);
      return 1;
    }
  }
  return 0;
}

void
log_msg(hello_msg_t * h, int msg_data_len)
{
  int n;
  int n_free = -1;
  struct in_addr in;

  for (n = 0; n < MAX_CLUSTER_NODES; ++n) {
    if (log[n].ml_nh == h->hm_source_handle) {
      log[n].ml_msgs_received++;
      log[n].ml_bytes_received += msg_data_len;
      break;
    } else if ((n_free < 0) && !log[n].ml_nh) {
      n_free = n;
    }
  }
  if (n >= MAX_CLUSTER_NODES) {
    log[n_free].ml_nh = h->hm_source_handle;
    log[n_free].ml_msgs_received++;
    log[n_free].ml_bytes_received += msg_data_len;
  }

  total_msgs_received++;
  if (total_msgs_received % 10) {
    return;
  }

  for (n = 0; n < MAX_CLUSTER_NODES; ++n) {
    if (log[n].ml_nh && (log[n].ml_msgs_received != log[n].ml_last_msgs_received)) {
      log[n].ml_last_msgs_received = log[n].ml_msgs_received;
      INKNodeHandleToIPAddr(&log[n].ml_nh, &in);
      INKDebug(PLUGIN_DEBUG_ERR_TAG,
               "[%u.%u.%u.%u] msgs rcvd: %d total bytes rcvd: %d\n",
               DOT_SEPARATED(in.s_addr), log[n].ml_msgs_received, log[n].ml_bytes_received);

    }
  }
}

/*****************************************************************************
 *  RPC Handler for key RPC_API_WIRELESS_F10.
 *****************************************************************************/
static void
rpc_wireless_f10_func(INKNodeHandle_t * nh, INKClusterRPCMsg_t * msg, int msg_data_len)
{
  hello_msg_t hello_msg;
  struct in_addr in;

  if (msg_data_len >= sizeof(hello_msg_t)) {
        /*********************************************************************
  	 *  Unmarshal data.
	 *********************************************************************/
    memcpy((char *) &hello_msg, msg->m_data, sizeof(hello_msg));

        /*********************************************************************
  	 *  Message consistency checks.
	 *********************************************************************/
    INKNodeHandleToIPAddr(&hello_msg.hm_source_handle, &in);
    if (hello_msg.hm_version != HELLO_MSG_VERSION) {
      INKDebug(PLUGIN_DEBUG_ERR_TAG,
               "rpc_wireless_f10_func() vers, actual %d expected %d \n", hello_msg.hm_version, HELLO_MSG_VERSION);
      return;
    }
    if (hello_msg.hm_source_handle != *nh) {
      INKDebug(PLUGIN_DEBUG_ERR_TAG,
               "rpc_wireless_f10_func() src, actual %d expected %d \n", hello_msg.hm_source_handle, *nh);
      return;
    }
    if (hello_msg.hm_data_size != msg_data_len) {
      INKDebug(PLUGIN_DEBUG_ERR_TAG,
               "rpc_wireless_f10_func() len, actual %d expected %d \n", msg_data_len, hello_msg.hm_data_size);
      return;
    }
    if (check_data(msg->m_data +
                   sizeof(hello_msg) - sizeof(hello_msg.hm_data_size),
                   msg_data_len - sizeof(hello_msg) + sizeof(hello_msg.hm_data))) {
      INKDebug(PLUGIN_DEBUG_ERR_TAG,
               "rpc_wireless_f10_func() data check failed, "
               "[%u.%u.%u.%u] len %d data 0x%x\n", DOT_SEPARATED(in.s_addr), msg_data_len, msg->m_data);
    }
    log_msg(&hello_msg, msg_data_len);
    INKFreeRPCMsg(msg, msg_data_len);

    INKDebug(PLUGIN_DEBUG_TAG,
             "Received hello from [%u.%u.%u.%u] instance %d\n", DOT_SEPARATED(in.s_addr), hello_msg.hm_instance);
  } else {
    INKFreeRPCMsg(msg, msg_data_len);
    INKDebug(PLUGIN_DEBUG_ERR_TAG,
             "rpc_wireless_f10_func() msglen, actual %d expect >= %d \n", msg_data_len, sizeof(hello_msg_t));
  }
}

/*****************************************************************************
 *  Periodic handler to send RPC messages.
 *****************************************************************************/
static int
periodic_event(INKCont contp, INKEvent event, void *e)
{
    /*************************************************************************
     *  Note: Event subsystem always calls us with 'node_status_mutex' held.
     *************************************************************************/
  int n, size, ret;

  INKClusterRPCMsg_t *rmsg;
  hello_msg_t hello_msg;
  struct in_addr in;

  if (clusterRPC_plugin_shutdown) {
    shutdown();
    INKContDestroy(contp);
    return 0;
  }
    /*************************************************************************
     *  Send a hello message to all online nodes.
     *************************************************************************/
  for (n = 1; n <= MAX_CLUSTER_NODES; ++n) {
    if (nodes[n]) {
      INKNodeHandleToIPAddr(&nodes[n], &in);

      hello_msg.hm_version = HELLO_MSG_VERSION;
      hello_msg.hm_source_handle = my_node_handle;
      hello_msg.hm_dest_handle = nodes[n];
      hello_msg.hm_instance = msg_instance++;

      size = random() % (1 * 1024 * 1024);
      if (size < sizeof(hello_msg_t)) {
        size = sizeof(hello_msg_t);
      }
      rmsg = INKAllocClusterRPCMsg(&rpc_wireless_f10_handle, size);
      hello_msg.hm_data_size = size;
            /******************************************************************
 	     *  Marshal data into message.
	     *****************************************************************/
      memcpy(rmsg->m_data, (char *) &hello_msg, sizeof(hello_msg));
      fill_data(rmsg->m_data +
                sizeof(hello_msg) - sizeof(hello_msg.hm_data_size),
                size - sizeof(hello_msg) + sizeof(hello_msg.hm_data));

      INKDebug(PLUGIN_DEBUG_TAG,
               "Sending hello to [%u.%u.%u.%u] instance %d bytes %d\n",
               DOT_SEPARATED(in.s_addr), hello_msg.hm_instance, size);

      ret = INKSendClusterRPC(&nodes[n], rmsg);
      if (ret) {
        INKDebug(PLUGIN_DEBUG_ERR_TAG, "INKSendClusterRPC failed\n");
      }
    }
  }
  periodic_event_action = INKContSchedule(periodic_event_cont, (1 * 1000) /* 1 sec */ );
  return 0;
}

static void
clusterRPC_init()
{
  int ret;

    /***********************************************************************
     *  Create plugin mutex
     ***********************************************************************/
  node_status_mutex = INKMutexCreate();
  if (!node_status_mutex) {
    INKDebug(PLUGIN_DEBUG_ERR_TAG, "INKMutexCreate for node_status failed\n");
    return;
  }
  if (!INKMutexTryLock(node_status_mutex)) {
    /* Should never fail */
    INKDebug(PLUGIN_DEBUG_ERR_TAG, "INKMutexTryLock failed\n");
  }
    /***********************************************************************
     *  Register our RPC handler.
     ***********************************************************************/
  ret = INKAddClusterRPCFunction(RPC_API_WIRELESS_F10, rpc_wireless_f10_func, &rpc_wireless_f10_handle);
  if (ret) {
    INKDebug(PLUGIN_DEBUG_ERR_TAG, "INKAddClusterRPCFunction failed\n");
    return;
  }
    /***********************************************************************
     *  Subscribe to cluster node status callouts.
     ***********************************************************************/
  ret = INKAddClusterStatusFunction(status_callout, node_status_mutex, &status_callout_handle);
  if (ret) {
    INKDebug(PLUGIN_DEBUG_ERR_TAG, "INKAddClusterStatusFunction failed\n");
    return;
  }
    /***********************************************************************
     *  Perform node status initializations.
     ***********************************************************************/
  INKGetMyNodeHandle(&my_node_handle);

    /***********************************************************************
     *  Enable cluster node status callouts.
     ***********************************************************************/
  INKEnableClusterStatusCallout(&status_callout_handle);

    /***********************************************************************
     *  Establish the periodic event.
     ***********************************************************************/
  periodic_event_cont = INKContCreate(periodic_event, node_status_mutex);
  if (!periodic_event_cont) {
    INKDebug(PLUGIN_DEBUG_ERR_TAG, "INKContCreate for periodic_event failed\n");
    return;
  }
  periodic_event_action = INKContSchedule(periodic_event_cont, (1 * 1000) /* 1 sec */ );
  INKMutexUnlock(node_status_mutex);
}

/*
 *  End of clusterRPC.c
 */
