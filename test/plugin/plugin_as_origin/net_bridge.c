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
#include "InkAPI.h"
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
#define VALID_POINTER(X) ((X != NULL) && (X != INK_ERROR_PTR))
#define LOG_SET_FUNCTION_NAME(NAME) const char * FUNCTION_NAME = NAME
#define LOG_ERROR(API_NAME) { \
    INKDebug(PLUGIN_NAME, "%s: %s %s %s File %s, line number %d", PLUGIN_NAME, API_NAME, "APIFAIL", \
	     FUNCTION_NAME, __FILE__, __LINE__); \
}
#define LOG_ERROR_NEG(API_NAME) { \
    INKDebug(PLUGIN_NAME, "%s: %s %s %s File %s, line number %d", PLUGIN_NAME, API_NAME, "NEGAPIFAIL", \
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
  INKHttpTxnReenable(txnp, INK_EVENT_HTTP_CONTINUE); \
}

static INKStat pvc_count;

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
  INKVConn p_vc;
  INKVIO p_read_vio;
  INKVIO p_write_vio;

  INKVConn net_vc;
  INKVIO n_read_vio;
  INKVIO n_write_vio;

  INKIOBuffer req_buffer;
  INKIOBufferReader req_reader;

  INKIOBuffer resp_buffer;
  INKIOBufferReader resp_reader;

  int req_finished;
  int resp_finished;

  INKAction connect_timeout_event;

  INKHttpTxn http_txnp;

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
static INKMutex debug_list_mutex = NULL;

static void
pvc_add_to_debug_list(pvc_state * to_add)
{
  INKMutexLock(debug_list_mutex);

  INKAssert(to_add->flink == NULL);
  INKAssert(to_add->blink == NULL);

  if (debug_list_head == NULL) {
    INKAssert(debug_list_tail == NULL);
    debug_list_head = debug_list_tail = to_add;
  } else {
    INKAssert(debug_list_tail->flink == NULL);
    to_add->blink = debug_list_tail;
    debug_list_tail->flink = to_add;
    debug_list_tail = to_add;
  }
  INKMutexUnlock(debug_list_mutex);
}

static void
pvc_remove_from_debug_list(pvc_state * to_remove)
{
  INKMutexLock(debug_list_mutex);

  if (debug_list_head == to_remove) {
    INKAssert(to_remove->blink == NULL);
    debug_list_head = to_remove->flink;

    if (debug_list_head) {
      INKAssert(debug_list_head->blink == to_remove);
      INKAssert(debug_list_tail != to_remove);
      debug_list_head->blink = NULL;
    } else {
      INKAssert(debug_list_tail == to_remove);
      debug_list_tail = NULL;
    }
  } else if (debug_list_tail == to_remove) {
    debug_list_tail = to_remove->blink;
    INKAssert(debug_list_tail->flink == to_remove);
    debug_list_tail->flink = NULL;
  } else {
    INKAssert(to_remove->flink != NULL);
    INKAssert(to_remove->blink != NULL);
    to_remove->blink->flink = to_remove->flink;
    to_remove->flink->blink = to_remove->blink;
  }

  to_remove->flink = NULL;
  to_remove->blink = NULL;

  INKMutexUnlock(debug_list_mutex);
}
#endif /* USE_PVC_DEBUG_LIST */

static void
pvc_cleanup(INKCont contp, pvc_state * my_state)
{

  LOG_SET_FUNCTION_NAME("pvc_cleanup");
  PVC_ADD_HISTORY_ENTRY(0xdeadbeef);

  if (my_state->req_buffer) {
    if (INKIOBufferDestroy(my_state->req_buffer) == INK_ERROR) {
      LOG_ERROR("INKIOBufferDestroy");
    }
    my_state->req_buffer = NULL;
  }

  if (my_state->resp_buffer) {
    if (INKIOBufferDestroy(my_state->resp_buffer) == INK_ERROR) {
      LOG_ERROR("INKIOBufferDestroy");
    }
    my_state->resp_buffer = NULL;
  }

  if (my_state->connect_timeout_event) {
    INKActionCancel(my_state->connect_timeout_event);
    my_state->connect_timeout_event = NULL;
  }
#ifdef USE_PVC_DEBUG_LIST
  /* Remove this entry from our debug list */
  pvc_remove_from_debug_list(my_state);
#endif

  INKfree(my_state);
  if (INKContDestroy(contp) == INK_ERROR) {
    LOG_ERROR("INKContDestroy");
  }

  /* Decrement pvc_count */
  INKStatDecrement(pvc_count);
}

static void
pvc_check_done(INKCont contp, pvc_state * my_state)
{
  LOG_SET_FUNCTION_NAME("pvc_check_done");

  if (my_state->req_finished && my_state->resp_finished) {
    if (INKVConnClose(my_state->p_vc) == INK_ERROR) {
      LOG_ERROR("INKVConnClose");
    }
    if (INKVConnClose(my_state->net_vc) == INK_ERROR) {
      LOG_ERROR("INKVConnClose");
    }
    pvc_cleanup(contp, my_state);
  }
}

static void
pvc_process_accept(INKCont contp, int event, void *edata, pvc_state * my_state)
{

  LOG_SET_FUNCTION_NAME("pvc_process_accept");
  PVC_ADD_HISTORY_ENTRY(event);

  INKDebug(DEBUG_TAG, "plugin called: pvc_process_accept with event %d", event);

  if (event == INK_EVENT_NET_ACCEPT) {
    my_state->p_vc = (INKVConn) edata;

    my_state->dest_ip = INKHttpTxnNextHopIPGet(my_state->http_txnp);
    if (!my_state->dest_ip) {
      LOG_ERROR("INKHttpTxnNextHopIPGet");
      return;
    }
    my_state->dest_port = INKHttpTxnNextHopPortGet(my_state->http_txnp);
    if (!my_state->dest_port) {
      LOG_ERROR("INKHttpTxnNextHopPortGet");
      return;
    }

    my_state->req_buffer = INKIOBufferCreate();
    my_state->req_reader = INKIOBufferReaderAlloc(my_state->req_buffer);
    my_state->resp_buffer = INKIOBufferCreate();
    my_state->resp_reader = INKIOBufferReaderAlloc(my_state->resp_buffer);
    if ((my_state->req_buffer == INK_ERROR_PTR) || (my_state->req_reader == INK_ERROR_PTR)
        || (my_state->resp_buffer == INK_ERROR_PTR) || (my_state->resp_reader == INK_ERROR_PTR)) {
      LOG_ERROR("INKIOBufferCreate || INKIOBufferReaderAlloc");
      if (INKVConnClose(my_state->net_vc) == INK_ERROR) {
        LOG_ERROR("INKVConnClose");
      }
      pvc_cleanup(contp, my_state);
    } else {
      INKNetConnect(contp, my_state->dest_ip, my_state->dest_port);
    }
  } else if (event == INK_EVENT_NET_ACCEPT_FAILED) {
    pvc_cleanup(contp, my_state);
  } else {
    INKReleaseAssert(!"Unexpected Event");
  }
}

static void
pvc_process_connect(INKCont contp, int event, void *data, pvc_state * my_state)
{

  LOG_SET_FUNCTION_NAME("pvc_process_connect");
  PVC_ADD_HISTORY_ENTRY(event);

  INKDebug(DEBUG_TAG, "plugin called: pvc_process_connect with event %d", event);

  if (event == INK_EVENT_NET_CONNECT_FAILED) {
    LOG_ERROR("INK_EVENT_NET_CONNECT_FAILED");
    if (INKVConnClose(my_state->p_vc) == INK_ERROR) {
      LOG_ERROR("INKVConnClose");
    }
    pvc_cleanup(contp, my_state);
  } else if (event == INK_EVENT_NET_CONNECT) {
    my_state->net_vc = (INKVConn *) data;

    /* INKqa12967 - the IO Core has a bug where if the connect fails, we
       do not always get VC_EVENT_ERROR on write side of the connection.
       Timeout if we do not reach the host after 30 seconds to prevent leaks.
    */
    my_state->connect_timeout_event = INKContSchedule(contp, 30 * 1000);

    my_state->p_read_vio = INKVConnRead(my_state->p_vc, contp, my_state->req_buffer, INT_MAX);
    if (my_state->p_read_vio == INK_ERROR_PTR) {
      LOG_ERROR("INKVConnRead");
      return;
    }
    my_state->p_write_vio = INKVConnWrite(my_state->p_vc, contp, my_state->resp_reader, INT_MAX);
    if (my_state->p_write_vio == INK_ERROR_PTR) {
      LOG_ERROR("INKVConnWrite");
      return;
    }

    my_state->n_read_vio = INKVConnRead(my_state->net_vc, contp, my_state->resp_buffer, INT_MAX);
    if (my_state->n_read_vio == INK_ERROR_PTR) {
      LOG_ERROR("INKVConnRead");
      return;
    }
    my_state->n_write_vio = INKVConnWrite(my_state->net_vc, contp, my_state->req_reader, INT_MAX);
    if (my_state->n_write_vio == INK_ERROR_PTR) {
      LOG_ERROR("INKVConnWrite");
      return;
    }
  } else {
    INKReleaseAssert(!"Unexpected Event");
  }
}

static void
pvc_process_p_read(INKCont contp, INKEvent event, pvc_state * my_state)
{

  LOG_SET_FUNCTION_NAME("pvc_process_p_read");
  int bytes;

  PVC_ADD_HISTORY_ENTRY(event);
  INKDebug(DEBUG_TAG, "plugin called: pvc_process_p_read with event %d", event);

  switch (event) {
  case INK_EVENT_VCONN_READ_READY:
    if (INKVIOReenable(my_state->n_write_vio) == INK_ERROR) {
      LOG_ERROR("INKVIOReenable");
    }
    break;
  case INK_EVENT_VCONN_READ_COMPLETE:
  case INK_EVENT_VCONN_EOS:
  case INK_EVENT_ERROR:
    {
      /* We're finished reading from the plugin vc */
      int ndone;
      int todo;

      ndone = INKVIONDoneGet(my_state->p_read_vio);
      if (ndone == INK_ERROR) {
        LOG_ERROR("INKVIODoneGet");
      }

      my_state->p_read_vio = NULL;
      if (INKVIONBytesSet(my_state->n_write_vio, ndone) == INK_ERROR) {
        LOG_ERROR("INKVIONBytesSet");
      }
      if (INKVConnShutdown(my_state->p_vc, 1, 0) == INK_ERROR) {
        LOG_ERROR("INKVConnShutdown");
      }

      todo = INKVIONTodoGet(my_state->n_write_vio);
      if (todo == INK_ERROR) {
        LOG_ERROR("INKVIONTodoGet");
        todo = 0;
      }

      if (todo == 0) {
        my_state->req_finished = 1;
        if (INKVConnShutdown(my_state->net_vc, 0, 1) == INK_ERROR) {
          LOG_ERROR("INKVConnShutdown");
        }
        pvc_check_done(contp, my_state);
      } else {
        if (INKVIOReenable(my_state->n_write_vio) == INK_ERROR) {
          LOG_ERROR("INKVIOReenable");
        }
      }

      break;
    }
  default:
    INKReleaseAssert(!"Unexpected Event");
    break;
  }
}

static void
pvc_process_n_write(INKCont contp, INKEvent event, pvc_state * my_state)
{

  LOG_SET_FUNCTION_NAME("pvc_process_n_write");
  int bytes;

  PVC_ADD_HISTORY_ENTRY(event);
  INKDebug(DEBUG_TAG, "plugin called: pvc_process_n_write with event %d", event);

  /* Any event from the write side of the connection means that either
     the connect completed successfully or we already know about the
     error.  Either way cancel the timeout event */
  if (my_state->connect_timeout_event) {
    INKActionCancel(my_state->connect_timeout_event);
    my_state->connect_timeout_event = NULL;
  }

  switch (event) {
  case INK_EVENT_VCONN_WRITE_READY:
    if (my_state->p_read_vio) {
      if (INKVIOReenable(my_state->p_read_vio) == INK_ERROR) {
        LOG_ERROR("INKVIOReenable");
      }
    }
    break;
  case INK_EVENT_ERROR:
    if (my_state->p_read_vio) {
      if (INKVConnShutdown(my_state->p_vc, 1, 0) == INK_ERROR) {
        LOG_ERROR("INKVConnShutdown");
      }
      my_state->p_read_vio = NULL;
    }
    /* FALL THROUGH */
  case INK_EVENT_VCONN_WRITE_COMPLETE:
    /* We should have already shutdown read pvc side */
    INKAssert(my_state->p_read_vio == NULL);
    if (INKVConnShutdown(my_state->net_vc, 0, 1) == INK_ERROR) {
      LOG_ERROR("INKVConnShutdown");
    }
    my_state->req_finished = 1;
    pvc_check_done(contp, my_state);
    break;
  default:
    INKReleaseAssert(!"Unexpected Event");
    break;
  }
}

static void
pvc_process_n_read(INKCont contp, INKEvent event, pvc_state * my_state)
{

  LOG_SET_FUNCTION_NAME("pvc_process_n_read");
  int bytes;

  PVC_ADD_HISTORY_ENTRY(event);
  INKDebug("pvc", "plugin called: pvc_process_n_read with event %d", event);

  switch (event) {
  case INK_EVENT_VCONN_READ_READY:
    if (INKVIOReenable(my_state->p_write_vio) == INK_ERROR) {
      LOG_ERROR("INKVIOReenable");
    }
    break;
  case INK_EVENT_VCONN_READ_COMPLETE:
  case INK_EVENT_VCONN_EOS:
  case INK_EVENT_ERROR:
    {
      /* We're finished reading from the plugin vc */
      int ndone;
      int todo;

      ndone = INKVIONDoneGet(my_state->n_read_vio);
      if (ndone == INK_ERROR) {
        LOG_ERROR("INKVIODoneGet");
      }

      my_state->n_read_vio = NULL;
      if (INKVIONBytesSet(my_state->p_write_vio, ndone) == INK_ERROR) {
        LOG_ERROR("INKVIONBytesSet");
      }
      if (INKVConnShutdown(my_state->net_vc, 1, 0) == INK_ERROR) {
        LOG_ERROR("INKVConnShutdown");
      }

      todo = INKVIONTodoGet(my_state->p_write_vio);
      if (todo == INK_ERROR) {
        LOG_ERROR("INKVIOTodoGet");
        /* Error so set it to 0 to cleanup */
        todo = 0;
      }

      if (todo == 0) {
        my_state->resp_finished = 1;
        if (INKVConnShutdown(my_state->p_vc, 0, 1) == INK_ERROR) {
          LOG_ERROR("INKVConnShutdown");
        }
        pvc_check_done(contp, my_state);
      } else {
        if (INKVIOReenable(my_state->p_write_vio) == INK_ERROR) {
          LOG_ERROR("INKVIOReenable");
        }
      }

      break;
    }
  default:
    INKReleaseAssert(!"Unexpected Event");
    break;
  }
}

static void
pvc_process_p_write(INKCont contp, INKEvent event, pvc_state * my_state)
{

  LOG_SET_FUNCTION_NAME("pvc_process_p_write");
  int bytes;

  PVC_ADD_HISTORY_ENTRY(event);
  INKDebug(DEBUG_TAG, "plugin called: pvc_process_p_write with event %d", event);

  switch (event) {
  case INK_EVENT_VCONN_WRITE_READY:
    if (my_state->n_read_vio) {
      if (INKVIOReenable(my_state->n_read_vio) == INK_ERROR) {
        LOG_ERROR("INKVIOReenable");
      }
    }
    break;
  case INK_EVENT_ERROR:
    if (my_state->n_read_vio) {
      if (INKVConnShutdown(my_state->net_vc, 1, 0) == INK_ERROR) {
        LOG_ERROR("INVConnShutdown");
      }
      my_state->n_read_vio = NULL;
    }
    /* FALL THROUGH */
  case INK_EVENT_VCONN_WRITE_COMPLETE:
    /* We should have already shutdown read net side */
    INKAssert(my_state->n_read_vio == NULL);
    if (INKVConnShutdown(my_state->p_vc, 0, 1) == INK_ERROR) {
      LOG_ERROR("INKVConnShutdown");
    }
    my_state->resp_finished = 1;
    pvc_check_done(contp, my_state);
    break;
  default:
    INKReleaseAssert(!"Unexpected Event");
    break;
  }
}

static void
pvc_process_connect_timeout(INKCont contp, int event, pvc_state * my_state)
{


  PVC_ADD_HISTORY_ENTRY(event);
  INKDebug(DEBUG_TAG, "plugin called: pvc_process_conect_time with event %d", event);
  INKDebug(DEBUG_TAG "-connect", "Timing out connect to %u.%u.%u.%u:%d",
           ((unsigned char *) &my_state->dest_ip)[0],
           ((unsigned char *) &my_state->dest_ip)[1],
           ((unsigned char *) &my_state->dest_ip)[2],
           ((unsigned char *) &my_state->dest_ip)[3], (int) my_state->dest_port);

  my_state->connect_timeout_event = NULL;

  /* Simulate an error event which is what we should gotten anyway */
  pvc_process_n_write(contp, INK_EVENT_ERROR, my_state);
}

static int
pvc_plugin(INKCont contp, INKEvent event, void *edata)
{
  pvc_state *my_state = INKContDataGet(contp);

  if (event == INK_EVENT_NET_ACCEPT || event == INK_EVENT_NET_ACCEPT_FAILED) {
    pvc_process_accept(contp, event, edata, my_state);
  } else if (event == INK_EVENT_NET_CONNECT || event == INK_EVENT_NET_CONNECT_FAILED) {
    pvc_process_connect(contp, event, edata, my_state);
  } else if (edata == my_state->p_read_vio) {
    pvc_process_p_read(contp, event, my_state);
  } else if (edata == my_state->p_write_vio) {
    pvc_process_p_write(contp, event, my_state);
  } else if (edata == my_state->n_read_vio) {
    pvc_process_n_read(contp, event, my_state);
  } else if (edata == my_state->n_write_vio) {
    pvc_process_n_write(contp, event, my_state);
  } else if (event == INK_EVENT_TIMEOUT &&
             ((INKAction) (((unsigned long) edata) | 0x1)) == my_state->connect_timeout_event) {
    /* FIX ME - SDK should have a function to compare the Action received from
       InkContSchedule and edata on INK_EVENT_TIMEOUT */
    pvc_process_connect_timeout(contp, event, my_state);
  } else {
    INKReleaseAssert(!"Unexpected Event");
  }

  return 0;
}

static int
attach_pvc_plugin(INKCont contp, INKEvent event, void *edata)
{
  LOG_SET_FUNCTION_NAME("attach_pvc_plugin");

  INKHttpTxn txnp = (INKHttpTxn) edata;
  INKMutex mutex;
  INKCont new_cont;
  pvc_state *my_state;

  switch (event) {
  case INK_EVENT_HTTP_READ_REQUEST_HDR:
    mutex = INKMutexCreate();
    if (mutex == INK_ERROR_PTR) {
      LOG_ERROR("INKMutexCreate");
      break;
    }
    new_cont = INKContCreate(pvc_plugin, mutex);
    if (new_cont == INK_ERROR_PTR) {
      LOG_ERROR("INKContCreate");
      break;
    }

    my_state = (pvc_state *) INKmalloc(sizeof(pvc_state));
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
    INKStatIncrement(pvc_count);

#ifdef USE_PVC_HISTORY
    my_state->history_index = 0;
#endif

#ifdef USE_PVC_DEBUG_LIST
    my_state->flink = NULL;
    my_state->blink = NULL;

    /* Add this entry to our debug list */
    pvc_add_to_debug_list(my_state);
#endif

    if (INKContDataSet(new_cont, my_state) == INK_ERROR) {
      LOG_ERROR("INKContDataSet");
      break;
    }

    if (INKHttpTxnServerIntercept(new_cont, txnp) == INK_ERROR) {
      LOG_ERROR("INKHttpTxnServerIntercept");
      break;
    }
#ifdef DEBUG
    if (INKHttpTxnServerIntercept(NULL, NULL) != INK_ERROR) {
      LOG_ERROR_NEG("INKHttpTxnServerIntercept");
    }
#endif

    break;
  default:
    INKReleaseAssert(!"Unexpected Event");
    break;
  }

  if (INKHttpTxnReenable(txnp, INK_EVENT_HTTP_CONTINUE) == INK_ERROR) {
    LOG_ERROR_AND_RETURN("INKHttpTxnReenable");
  }

  return 0;
}

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

    /* Since this is an TS-SDK 5.2 plugin, we need at
       least Traffic Server 5.2 to run */
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
  LOG_SET_FUNCTION_NAME("INKPluginInit");

  INKMLoc field_loc;
  const char *p;
  int i;
  INKPluginRegistrationInfo info;
  INKCont contp;

  info.plugin_name = "test-pos";
  info.vendor_name = "MyCompany";
  info.support_email = "ts-api-support@MyCompany.com";

  if (!INKPluginRegister(INK_SDK_VERSION_2_0, &info)) {
    INKError("Plugin registration failed.\n");
  }

  if (!check_ts_version()) {
    INKError("Plugin requires Traffic Server 3.5.2 or later\n");
    return;
  }

  /* create the statistic variables */
  pvc_count = INKStatCreate("pvc.count", INKSTAT_TYPE_INT64);
  if (pvc_count == INK_ERROR_PTR) {
    LOG_ERROR("INKStatsCreate");
  }
#ifdef USE_PVC_DEBUG_LIST
  /* create our debug list mutx */
  debug_list_mutex = INKMutexCreate();
  if (debug_list_mutex == NULL) {
    LOG_ERROR("INKMutexCreate");
  }
#endif

  contp = INKContCreate(attach_pvc_plugin, NULL);
  if (contp == INK_ERROR_PTR) {
    LOG_ERROR("INKContCreate");
  } else {
    if (INKHttpHookAdd(INK_HTTP_READ_REQUEST_HDR_HOOK, contp) == INK_ERROR) {
      LOG_ERROR("INKHttpHookAdd");
    }
  }

}
