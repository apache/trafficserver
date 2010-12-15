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

/*   net_bridge.c - another test program to test plugin as origin
 *     server interface.  Connects to origin server allowing
 *     the use all existing test & load tools
 *
 *
 */

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <limits.h>
#include "ts.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>

/* #define DEBUG 1 */
#define DEBUG_TAG "net_bridge-dbg"

/**************************************************
   Log macros for error code return verification
**************************************************/
#define PLUGIN_NAME "net_bridge"
#define VALID_POINTER(X) ((X != NULL) && (X != TS_ERROR_PTR))
#define LOG_SET_FUNCTION_NAME(NAME) const char * FUNCTION_NAME = NAME
#define LOG_ERROR(API_NAME) { \
    TSDebug(PLUGIN_NAME, "%s: %s %s %s File %s, line number %d", PLUGIN_NAME, API_NAME, "APIFAIL", \
	     FUNCTION_NAME, __FILE__, __LINE__); \
}
#define LOG_ERROR_NEG(API_NAME) { \
    TSDebug(PLUGIN_NAME, "%s: %s %s %s File %s, line number %d", PLUGIN_NAME, API_NAME, "NEGAPIFAIL", \
	     FUNCTION_NAME, __FILE__, __LINE__); \
}
#define LOG_ERROR_AND_RETURN(API_NAME) { \
    LOG_ERROR(API_NAME); \
    return -1; \
}
#define LOG_ERROR_AND_CLEANUP(API_NAME) { \
  LOG_ERROR(API_NAME); \
  goto Lcleanup; \
}
#define LOG_ERROR_AND_REENABLE(API_NAME) { \
  LOG_ERROR(API_NAME); \
  TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE); \
}

static TSStat pvc_count;

#ifdef USE_PVC_HISTORY
struct pvc_hist_t
{
  int event;
  int line_number;
};
typedef struct pvc_hist_t pvc_hist;

#define PVC_HISTORY_SIZE 32
#define PVC_ADD_HISTORY_ENTRY(e) \
   my_state->history[my_state->history_index].event = e; \
   my_state->history[my_state->history_index].line_number = __LINE__; \
   my_state->history_index = (my_state->history_index + 1) % PVC_HISTORY_SIZE;

#else /* #ifdef USE_PVC_HISTORY */

#define PVC_ADD_HISTORY_ENTRY(e)

#endif /* #ifdef USE_PVC_HISTORY */

struct pvc_state_t
{
  TSVConn p_vc;
  TSVIO p_read_vio;
  TSVIO p_write_vio;

  TSVConn net_vc;
  TSVIO n_read_vio;
  TSVIO n_write_vio;

  TSIOBuffer req_buffer;
  TSIOBufferReader req_reader;

  TSIOBuffer resp_buffer;
  TSIOBufferReader resp_reader;

  int req_finished;
  int resp_finished;

  TSAction connect_timeout_event;

  TSHttpTxn http_txnp;

  unsigned int dest_ip;
  int dest_port;

#ifdef USE_PVC_HISTORY
  pvc_hist history[PVC_HISTORY_SIZE];
  int history_index;
#endif

#ifdef USE_PVC_DEBUG_LIST
  struct pvc_state_t *flink;
  struct pvc_state_t *blink;
#endif

};
typedef struct pvc_state_t pvc_state;


#ifdef USE_PVC_DEBUG_LIST
static pvc_state *debug_list_head = NULL;
static pvc_state *debug_list_tail = NULL;
static TSMutex debug_list_mutex = NULL;

static void
pvc_add_to_debug_list(pvc_state * to_add)
{
  TSMutexLock(debug_list_mutex);

  TSAssert(to_add->flink == NULL);
  TSAssert(to_add->blink == NULL);

  if (debug_list_head == NULL) {
    TSAssert(debug_list_tail == NULL);
    debug_list_head = debug_list_tail = to_add;
  } else {
    TSAssert(debug_list_tail->flink == NULL);
    to_add->blink = debug_list_tail;
    debug_list_tail->flink = to_add;
    debug_list_tail = to_add;
  }
  TSMutexUnlock(debug_list_mutex);
}

static void
pvc_remove_from_debug_list(pvc_state * to_remove)
{
  TSMutexLock(debug_list_mutex);

  if (debug_list_head == to_remove) {
    TSAssert(to_remove->blink == NULL);
    debug_list_head = to_remove->flink;

    if (debug_list_head) {
      TSAssert(debug_list_head->blink == to_remove);
      TSAssert(debug_list_tail != to_remove);
      debug_list_head->blink = NULL;
    } else {
      TSAssert(debug_list_tail == to_remove);
      debug_list_tail = NULL;
    }
  } else if (debug_list_tail == to_remove) {
    debug_list_tail = to_remove->blink;
    TSAssert(debug_list_tail->flink == to_remove);
    debug_list_tail->flink = NULL;
  } else {
    TSAssert(to_remove->flink != NULL);
    TSAssert(to_remove->blink != NULL);
    to_remove->blink->flink = to_remove->flink;
    to_remove->flink->blink = to_remove->blink;
  }

  to_remove->flink = NULL;
  to_remove->blink = NULL;

  TSMutexUnlock(debug_list_mutex);
}
#endif /* USE_PVC_DEBUG_LIST */

static void
pvc_cleanup(TSCont contp, pvc_state * my_state)
{

  LOG_SET_FUNCTION_NAME("pvc_cleanup");
  PVC_ADD_HISTORY_ENTRY(0xdeadbeef);

  if (my_state->req_buffer) {
    if (TSIOBufferDestroy(my_state->req_buffer) == TS_ERROR) {
      LOG_ERROR("TSIOBufferDestroy");
    }
    my_state->req_buffer = NULL;
  }

  if (my_state->resp_buffer) {
    if (TSIOBufferDestroy(my_state->resp_buffer) == TS_ERROR) {
      LOG_ERROR("TSIOBufferDestroy");
    }
    my_state->resp_buffer = NULL;
  }

  if (my_state->connect_timeout_event) {
    TSActionCancel(my_state->connect_timeout_event);
    my_state->connect_timeout_event = NULL;
  }
#ifdef USE_PVC_DEBUG_LIST
  /* Remove this entry from our debug list */
  pvc_remove_from_debug_list(my_state);
#endif

  TSfree(my_state);
  if (TSContDestroy(contp) == TS_ERROR) {
    LOG_ERROR("TSContDestroy");
  }

  /* Decrement pvc_count */
  TSStatDecrement(pvc_count);
}

static void
pvc_check_done(TSCont contp, pvc_state * my_state)
{
  LOG_SET_FUNCTION_NAME("pvc_check_done");

  if (my_state->req_finished && my_state->resp_finished) {
    if (TSVConnClose(my_state->p_vc) == TS_ERROR) {
      LOG_ERROR("TSVConnClose");
    }
    if (TSVConnClose(my_state->net_vc) == TS_ERROR) {
      LOG_ERROR("TSVConnClose");
    }
    pvc_cleanup(contp, my_state);
  }
}

static void
pvc_process_accept(TSCont contp, int event, void *edata, pvc_state * my_state)
{

  LOG_SET_FUNCTION_NAME("pvc_process_accept");
  PVC_ADD_HISTORY_ENTRY(event);

  TSDebug(DEBUG_TAG, "plugin called: pvc_process_accept with event %d", event);

  if (event == TS_EVENT_NET_ACCEPT) {
    my_state->p_vc = (TSVConn) edata;

    my_state->dest_ip = TSHttpTxnNextHopIPGet(my_state->http_txnp);
    if (!my_state->dest_ip) {
      LOG_ERROR("TSHttpTxnNextHopIPGet");
      return;
    }
    my_state->dest_port = TSHttpTxnNextHopPortGet(my_state->http_txnp);
    if (!my_state->dest_port) {
      LOG_ERROR("TSHttpTxnNextHopPortGet");
      return;
    }

    my_state->req_buffer = TSIOBufferCreate();
    my_state->req_reader = TSIOBufferReaderAlloc(my_state->req_buffer);
    my_state->resp_buffer = TSIOBufferCreate();
    my_state->resp_reader = TSIOBufferReaderAlloc(my_state->resp_buffer);
    if ((my_state->req_buffer == TS_ERROR_PTR) || (my_state->req_reader == TS_ERROR_PTR)
        || (my_state->resp_buffer == TS_ERROR_PTR) || (my_state->resp_reader == TS_ERROR_PTR)) {
      LOG_ERROR("TSIOBufferCreate || TSIOBufferReaderAlloc");
      if (TSVConnClose(my_state->net_vc) == TS_ERROR) {
        LOG_ERROR("TSVConnClose");
      }
      pvc_cleanup(contp, my_state);
    } else {
      TSNetConnect(contp, my_state->dest_ip, my_state->dest_port);
    }
  } else if (event == TS_EVENT_NET_ACCEPT_FAILED) {
    pvc_cleanup(contp, my_state);
  } else {
    TSReleaseAssert(!"Unexpected Event");
  }
}

static void
pvc_process_connect(TSCont contp, int event, void *data, pvc_state * my_state)
{

  LOG_SET_FUNCTION_NAME("pvc_process_connect");
  PVC_ADD_HISTORY_ENTRY(event);

  TSDebug(DEBUG_TAG, "plugin called: pvc_process_connect with event %d", event);

  if (event == TS_EVENT_NET_CONNECT_FAILED) {
    LOG_ERROR("TS_EVENT_NET_CONNECT_FAILED");
    if (TSVConnClose(my_state->p_vc) == TS_ERROR) {
      LOG_ERROR("TSVConnClose");
    }
    pvc_cleanup(contp, my_state);
  } else if (event == TS_EVENT_NET_CONNECT) {
    my_state->net_vc = (TSVConn *) data;

    /* TSqa12967 - the IO Core has a bug where if the connect fails, we
       do not always get VC_EVENT_ERROR on write side of the connection.
       Timeout if we do not reach the host after 30 seconds to prevent leaks.
    */
    my_state->connect_timeout_event = TSContSchedule(contp, 30 * 1000);

    my_state->p_read_vio = TSVConnRead(my_state->p_vc, contp, my_state->req_buffer, INT_MAX);
    if (my_state->p_read_vio == TS_ERROR_PTR) {
      LOG_ERROR("TSVConnRead");
      return;
    }
    my_state->p_write_vio = TSVConnWrite(my_state->p_vc, contp, my_state->resp_reader, INT_MAX);
    if (my_state->p_write_vio == TS_ERROR_PTR) {
      LOG_ERROR("TSVConnWrite");
      return;
    }

    my_state->n_read_vio = TSVConnRead(my_state->net_vc, contp, my_state->resp_buffer, INT_MAX);
    if (my_state->n_read_vio == TS_ERROR_PTR) {
      LOG_ERROR("TSVConnRead");
      return;
    }
    my_state->n_write_vio = TSVConnWrite(my_state->net_vc, contp, my_state->req_reader, INT_MAX);
    if (my_state->n_write_vio == TS_ERROR_PTR) {
      LOG_ERROR("TSVConnWrite");
      return;
    }
  } else {
    TSReleaseAssert(!"Unexpected Event");
  }
}

static void
pvc_process_p_read(TSCont contp, TSEvent event, pvc_state * my_state)
{

  LOG_SET_FUNCTION_NAME("pvc_process_p_read");
  int bytes;

  PVC_ADD_HISTORY_ENTRY(event);
  TSDebug(DEBUG_TAG, "plugin called: pvc_process_p_read with event %d", event);

  switch (event) {
  case TS_EVENT_VCONN_READ_READY:
    if (TSVIOReenable(my_state->n_write_vio) == TS_ERROR) {
      LOG_ERROR("TSVIOReenable");
    }
    break;
  case TS_EVENT_VCONN_READ_COMPLETE:
  case TS_EVENT_VCONN_EOS:
  case TS_EVENT_ERROR:
    {
      /* We're finished reading from the plugin vc */
      int ndone;
      int todo;

      ndone = TSVIONDoneGet(my_state->p_read_vio);
      if (ndone == TS_ERROR) {
        LOG_ERROR("TSVIODoneGet");
      }

      my_state->p_read_vio = NULL;
      if (TSVIONBytesSet(my_state->n_write_vio, ndone) == TS_ERROR) {
        LOG_ERROR("TSVIONBytesSet");
      }
      if (TSVConnShutdown(my_state->p_vc, 1, 0) == TS_ERROR) {
        LOG_ERROR("TSVConnShutdown");
      }

      todo = TSVIONTodoGet(my_state->n_write_vio);
      if (todo == TS_ERROR) {
        LOG_ERROR("TSVIONTodoGet");
        todo = 0;
      }

      if (todo == 0) {
        my_state->req_finished = 1;
        if (TSVConnShutdown(my_state->net_vc, 0, 1) == TS_ERROR) {
          LOG_ERROR("TSVConnShutdown");
        }
        pvc_check_done(contp, my_state);
      } else {
        if (TSVIOReenable(my_state->n_write_vio) == TS_ERROR) {
          LOG_ERROR("TSVIOReenable");
        }
      }

      break;
    }
  default:
    TSReleaseAssert(!"Unexpected Event");
    break;
  }
}

static void
pvc_process_n_write(TSCont contp, TSEvent event, pvc_state * my_state)
{

  LOG_SET_FUNCTION_NAME("pvc_process_n_write");
  int bytes;

  PVC_ADD_HISTORY_ENTRY(event);
  TSDebug(DEBUG_TAG, "plugin called: pvc_process_n_write with event %d", event);

  /* Any event from the write side of the connection means that either
     the connect completed successfully or we already know about the
     error.  Either way cancel the timeout event */
  if (my_state->connect_timeout_event) {
    TSActionCancel(my_state->connect_timeout_event);
    my_state->connect_timeout_event = NULL;
  }

  switch (event) {
  case TS_EVENT_VCONN_WRITE_READY:
    if (my_state->p_read_vio) {
      if (TSVIOReenable(my_state->p_read_vio) == TS_ERROR) {
        LOG_ERROR("TSVIOReenable");
      }
    }
    break;
  case TS_EVENT_ERROR:
    if (my_state->p_read_vio) {
      if (TSVConnShutdown(my_state->p_vc, 1, 0) == TS_ERROR) {
        LOG_ERROR("TSVConnShutdown");
      }
      my_state->p_read_vio = NULL;
    }
    /* FALL THROUGH */
  case TS_EVENT_VCONN_WRITE_COMPLETE:
    /* We should have already shutdown read pvc side */
    TSAssert(my_state->p_read_vio == NULL);
    if (TSVConnShutdown(my_state->net_vc, 0, 1) == TS_ERROR) {
      LOG_ERROR("TSVConnShutdown");
    }
    my_state->req_finished = 1;
    pvc_check_done(contp, my_state);
    break;
  default:
    TSReleaseAssert(!"Unexpected Event");
    break;
  }
}

static void
pvc_process_n_read(TSCont contp, TSEvent event, pvc_state * my_state)
{

  LOG_SET_FUNCTION_NAME("pvc_process_n_read");
  int bytes;

  PVC_ADD_HISTORY_ENTRY(event);
  TSDebug("pvc", "plugin called: pvc_process_n_read with event %d", event);

  switch (event) {
  case TS_EVENT_VCONN_READ_READY:
    if (TSVIOReenable(my_state->p_write_vio) == TS_ERROR) {
      LOG_ERROR("TSVIOReenable");
    }
    break;
  case TS_EVENT_VCONN_READ_COMPLETE:
  case TS_EVENT_VCONN_EOS:
  case TS_EVENT_ERROR:
    {
      /* We're finished reading from the plugin vc */
      int ndone;
      int todo;

      ndone = TSVIONDoneGet(my_state->n_read_vio);
      if (ndone == TS_ERROR) {
        LOG_ERROR("TSVIODoneGet");
      }

      my_state->n_read_vio = NULL;
      if (TSVIONBytesSet(my_state->p_write_vio, ndone) == TS_ERROR) {
        LOG_ERROR("TSVIONBytesSet");
      }
      if (TSVConnShutdown(my_state->net_vc, 1, 0) == TS_ERROR) {
        LOG_ERROR("TSVConnShutdown");
      }

      todo = TSVIONTodoGet(my_state->p_write_vio);
      if (todo == TS_ERROR) {
        LOG_ERROR("TSVIOTodoGet");
        /* Error so set it to 0 to cleanup */
        todo = 0;
      }

      if (todo == 0) {
        my_state->resp_finished = 1;
        if (TSVConnShutdown(my_state->p_vc, 0, 1) == TS_ERROR) {
          LOG_ERROR("TSVConnShutdown");
        }
        pvc_check_done(contp, my_state);
      } else {
        if (TSVIOReenable(my_state->p_write_vio) == TS_ERROR) {
          LOG_ERROR("TSVIOReenable");
        }
      }

      break;
    }
  default:
    TSReleaseAssert(!"Unexpected Event");
    break;
  }
}

static void
pvc_process_p_write(TSCont contp, TSEvent event, pvc_state * my_state)
{

  LOG_SET_FUNCTION_NAME("pvc_process_p_write");
  int bytes;

  PVC_ADD_HISTORY_ENTRY(event);
  TSDebug(DEBUG_TAG, "plugin called: pvc_process_p_write with event %d", event);

  switch (event) {
  case TS_EVENT_VCONN_WRITE_READY:
    if (my_state->n_read_vio) {
      if (TSVIOReenable(my_state->n_read_vio) == TS_ERROR) {
        LOG_ERROR("TSVIOReenable");
      }
    }
    break;
  case TS_EVENT_ERROR:
    if (my_state->n_read_vio) {
      if (TSVConnShutdown(my_state->net_vc, 1, 0) == TS_ERROR) {
        LOG_ERROR("INVConnShutdown");
      }
      my_state->n_read_vio = NULL;
    }
    /* FALL THROUGH */
  case TS_EVENT_VCONN_WRITE_COMPLETE:
    /* We should have already shutdown read net side */
    TSAssert(my_state->n_read_vio == NULL);
    if (TSVConnShutdown(my_state->p_vc, 0, 1) == TS_ERROR) {
      LOG_ERROR("TSVConnShutdown");
    }
    my_state->resp_finished = 1;
    pvc_check_done(contp, my_state);
    break;
  default:
    TSReleaseAssert(!"Unexpected Event");
    break;
  }
}

static void
pvc_process_connect_timeout(TSCont contp, int event, pvc_state * my_state)
{


  PVC_ADD_HISTORY_ENTRY(event);
  TSDebug(DEBUG_TAG, "plugin called: pvc_process_conect_time with event %d", event);
  TSDebug(DEBUG_TAG "-connect", "Timing out connect to %u.%u.%u.%u:%d",
           ((unsigned char *) &my_state->dest_ip)[0],
           ((unsigned char *) &my_state->dest_ip)[1],
           ((unsigned char *) &my_state->dest_ip)[2],
           ((unsigned char *) &my_state->dest_ip)[3], (int) my_state->dest_port);

  my_state->connect_timeout_event = NULL;

  /* Simulate an error event which is what we should gotten anyway */
  pvc_process_n_write(contp, TS_EVENT_ERROR, my_state);
}

static int
pvc_plugin(TSCont contp, TSEvent event, void *edata)
{
  pvc_state *my_state = TSContDataGet(contp);

  if (event == TS_EVENT_NET_ACCEPT || event == TS_EVENT_NET_ACCEPT_FAILED) {
    pvc_process_accept(contp, event, edata, my_state);
  } else if (event == TS_EVENT_NET_CONNECT || event == TS_EVENT_NET_CONNECT_FAILED) {
    pvc_process_connect(contp, event, edata, my_state);
  } else if (edata == my_state->p_read_vio) {
    pvc_process_p_read(contp, event, my_state);
  } else if (edata == my_state->p_write_vio) {
    pvc_process_p_write(contp, event, my_state);
  } else if (edata == my_state->n_read_vio) {
    pvc_process_n_read(contp, event, my_state);
  } else if (edata == my_state->n_write_vio) {
    pvc_process_n_write(contp, event, my_state);
  } else if (event == TS_EVENT_TIMEOUT &&
             ((TSAction) (((unsigned long) edata) | 0x1)) == my_state->connect_timeout_event) {
    /* FIX ME - SDK should have a function to compare the Action received from
       InkContSchedule and edata on TS_EVENT_TIMEOUT */
    pvc_process_connect_timeout(contp, event, my_state);
  } else {
    TSReleaseAssert(!"Unexpected Event");
  }

  return 0;
}

static int
attach_pvc_plugin(TSCont contp, TSEvent event, void *edata)
{
  LOG_SET_FUNCTION_NAME("attach_pvc_plugin");

  TSHttpTxn txnp = (TSHttpTxn) edata;
  TSMutex mutex;
  TSCont new_cont;
  pvc_state *my_state;

  switch (event) {
  case TS_EVENT_HTTP_READ_REQUEST_HDR:
    mutex = TSMutexCreate();
    if (mutex == TS_ERROR_PTR) {
      LOG_ERROR("TSMutexCreate");
      break;
    }
    new_cont = TSContCreate(pvc_plugin, mutex);
    if (new_cont == TS_ERROR_PTR) {
      LOG_ERROR("TSContCreate");
      break;
    }

    my_state = (pvc_state *) TSmalloc(sizeof(pvc_state));
    my_state->p_vc = NULL;
    my_state->p_read_vio = NULL;
    my_state->p_write_vio = NULL;

    my_state->net_vc = NULL;
    my_state->n_read_vio = NULL;
    my_state->n_write_vio = NULL;

    my_state->req_buffer = NULL;
    my_state->req_reader = NULL;
    my_state->resp_buffer = NULL;
    my_state->resp_reader = NULL;

    my_state->connect_timeout_event = NULL;
    my_state->http_txnp = txnp;
    my_state->dest_ip = 0;
    my_state->dest_port = 80;

    my_state->req_finished = 0;
    my_state->resp_finished = 0;

    /* Increment pvc_count */
    TSStatIncrement(pvc_count);

#ifdef USE_PVC_HISTORY
    my_state->history_index = 0;
#endif

#ifdef USE_PVC_DEBUG_LIST
    my_state->flink = NULL;
    my_state->blink = NULL;

    /* Add this entry to our debug list */
    pvc_add_to_debug_list(my_state);
#endif

    if (TSContDataSet(new_cont, my_state) == TS_ERROR) {
      LOG_ERROR("TSContDataSet");
      break;
    }

    if (TSHttpTxnServerIntercept(new_cont, txnp) == TS_ERROR) {
      LOG_ERROR("TSHttpTxnServerIntercept");
      break;
    }
#ifdef DEBUG
    if (TSHttpTxnServerIntercept(NULL, NULL) != TS_ERROR) {
      LOG_ERROR_NEG("TSHttpTxnServerIntercept");
    }
#endif

    break;
  default:
    TSReleaseAssert(!"Unexpected Event");
    break;
  }

  if (TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE) == TS_ERROR) {
    LOG_ERROR_AND_RETURN("TSHttpTxnReenable");
  }

  return 0;
}

int
check_ts_version()
{

  const char *ts_version = TSTrafficServerVersionGet();
  int result = 0;

  if (ts_version) {
    int major_ts_version = 0;
    int minor_ts_version = 0;
    int patch_ts_version = 0;

    if (sscanf(ts_version, "%d.%d.%d", &major_ts_version, &minor_ts_version, &patch_ts_version) != 3) {
      return 0;
    }

    /* Since this is an TS-SDK 2.0 plugin, we need at
       least Traffic Server 2.0 to run */
    if (major_ts_version >= 2) {
      result = 1;
    }
  }

  return result;
}

void
TSPluginInit(int argc, const char *argv[])
{
  LOG_SET_FUNCTION_NAME("TSPluginInit");

  TSMLoc field_loc;
  const char *p;
  int i;
  TSPluginRegistrationInfo info;
  TSCont contp;

  info.plugin_name = "test-pos";
  info.vendor_name = "MyCompany";
  info.support_email = "ts-api-support@MyCompany.com";

  if (!TSPluginRegister(TS_SDK_VERSION_3_0, &info)) {
    TSError("Plugin registration failed.\n");
  }

  if (!check_ts_version()) {
    TSError("Plugin requires Traffic Server 3.0 or later\n");
    return;
  }

  /* create the statistic variables */
  pvc_count = TSStatCreate("pvc.count", TSSTAT_TYPE_INT64);
  if (pvc_count == TS_ERROR_PTR) {
    LOG_ERROR("TSStatsCreate");
  }
#ifdef USE_PVC_DEBUG_LIST
  /* create our debug list mutx */
  debug_list_mutex = TSMutexCreate();
  if (debug_list_mutex == NULL) {
    LOG_ERROR("TSMutexCreate");
  }
#endif

  contp = TSContCreate(attach_pvc_plugin, NULL);
  if (contp == TS_ERROR_PTR) {
    LOG_ERROR("TSContCreate");
  } else {
    if (TSHttpHookAdd(TS_HTTP_READ_REQUEST_HDR_HOOK, contp) == TS_ERROR) {
      LOG_ERROR("TSHttpHookAdd");
    }
  }

}
