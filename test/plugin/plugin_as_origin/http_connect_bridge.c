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

/*   http_connect_bridge.c - Test program for TSHttpConnect() interface.
 *     Listens on a port and forwards all traffic to http system
 *     allowing the use all existing test & load tools
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

#define DEBUG_TAG "http_connect_bridge-dbg"
/* #define DEBUG 1 */

/**************************************************
   Log macros for error code return verification
**************************************************/
#define PLUGIN_NAME "http_connect_bridge"
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

/* Global variables */
TSAction *accept_action;
static int plugin_port = 2499;
static unsigned int plugin_ip = 0;

/* static char* doc_buf = NULL; */
/* static int   doc_size; */

struct pvc_state_t
{

  TSVConn http_vc;
  TSVIO h_read_vio;
  TSVIO h_write_vio;

  TSVConn net_vc;
  TSVIO n_read_vio;
  TSVIO n_write_vio;

  TSIOBuffer req_buffer;
  TSIOBufferReader req_reader;

  TSIOBuffer resp_buffer;
  TSIOBufferReader resp_reader;

  int req_finished;
  int resp_finished;

};
typedef struct pvc_state_t pvc_state;

static void
pvc_cleanup(TSCont contp, pvc_state * my_state)
{
  LOG_SET_FUNCTION_NAME("pvc_cleanup");

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

  TSfree(my_state);
  if (TSContDestroy(contp) == TS_ERROR) {
    LOG_ERROR("TSContDestroy");
  }
}

static void
pvc_check_done(TSCont contp, pvc_state * my_state)
{
  LOG_SET_FUNCTION_NAME("pvc_check_done");

  if (my_state->req_finished && my_state->resp_finished) {
    if (TSVConnClose(my_state->http_vc) == TS_ERROR) {
      LOG_ERROR("TSVConnClose");
    }
    if (TSVConnClose(my_state->net_vc) == TS_ERROR) {
      LOG_ERROR("TSVConnClose");
    }
    pvc_cleanup(contp, my_state);
  }
}

static void
pvc_process_n_read(TSCont contp, TSEvent event, pvc_state * my_state)
{
  LOG_SET_FUNCTION_NAME("pvc_process_n_read");

  int bytes;

  TSDebug(DEBUG_TAG, "plugin called: pvc_process_n_read with event %d", event);

  switch (event) {
  case TS_EVENT_VCONN_READ_READY:
    if (TSVIOReenable(my_state->h_write_vio) == TS_ERROR) {
      LOG_ERROR("TSVIOReenable");
    }
    break;
  case TS_EVENT_VCONN_READ_COMPLETE:
  case TS_EVENT_VCONN_EOS:
  case TS_EVENT_ERROR:
    {
      /* We're finished reading from the net vc */
      int ndone = TSVIONDoneGet(my_state->n_read_vio);
      int todo;

      if (ndone == TS_ERROR) {
        LOG_ERROR("TSVIONDoneGet");
      }
      my_state->n_read_vio = NULL;
      if (TSVIONBytesSet(my_state->h_write_vio, ndone) == TS_ERROR) {
        LOG_ERROR("TSVIONBytesSet");
      }
      if (TSVConnShutdown(my_state->net_vc, 1, 0) == TS_ERROR) {
        LOG_ERROR("TSVConnShutdown");
      }

      todo = TSVIONTodoGet(my_state->h_write_vio);
      if (todo == TS_ERROR) {
        LOG_ERROR("TSVIONTodoGet");
        /* Error so set it to 0 to cleanup */
        todo = 0;
      }

      if (todo == 0) {
        my_state->req_finished = 1;
        if (TSVConnShutdown(my_state->http_vc, 0, 1) == TS_ERROR) {
          LOG_ERROR("TSVConnShutdown");
        }
        pvc_check_done(contp, my_state);
      } else {
        if (TSVIOReenable(my_state->h_write_vio) == TS_ERROR) {
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
pvc_process_h_write(TSCont contp, TSEvent event, pvc_state * my_state)
{
  LOG_SET_FUNCTION_NAME("pvc_process_h_write");

  int bytes;

  TSDebug(DEBUG_TAG, "plugin called: pvc_process_h_write with event %d", event);

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
        LOG_ERROR("TSVConnShutdown");
      }
      my_state->n_read_vio = NULL;
    }
    /* FALL THROUGH */
  case TS_EVENT_VCONN_WRITE_COMPLETE:
    /* We should have already shutdown read pvc side */
    TSAssert(my_state->n_read_vio == NULL);
    if (TSVConnShutdown(my_state->http_vc, 0, 1) == TS_ERROR) {
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
pvc_process_h_read(TSCont contp, TSEvent event, pvc_state * my_state)
{
  LOG_SET_FUNCTION_NAME("pvc_process_h_read");

  int bytes;

  TSDebug(DEBUG_TAG, "plugin called: pvc_process_h_read with event %d", event);

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
      /* We're finished reading from the http vc */
      int ndone;
      int todo;

      if ((ndone = TSVIONDoneGet(my_state->h_read_vio)) == TS_ERROR) {
        LOG_ERROR("TSVIONDoneGet");
      }

      my_state->h_read_vio = NULL;
      if (TSVIONBytesSet(my_state->n_write_vio, ndone) == TS_ERROR) {
        LOG_ERROR("TSVIONBytesSet");
      }
      if (TSVConnShutdown(my_state->http_vc, 1, 0) == TS_ERROR) {
        LOG_ERROR("TSVConnShutdown");
      }

      todo = TSVIONTodoGet(my_state->n_write_vio);
      if (todo == TS_ERROR) {
        LOG_ERROR("TSVIONTodoGet");
        /* Error so set it to 0 to cleanup */
        todo = 0;
      }

      if (todo == 0) {
        my_state->resp_finished = 1;
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

  TSDebug(DEBUG_TAG, "plugin called: pvc_process_n_write with event %d", event);

  switch (event) {
  case TS_EVENT_VCONN_WRITE_READY:
    if (my_state->h_read_vio) {
      if (TSVIOReenable(my_state->h_read_vio) == TS_ERROR) {
        LOG_ERROR("TSVIOReenable");
      }
    }
    break;
  case TS_EVENT_ERROR:
    if (my_state->h_read_vio) {
      if (TSVConnShutdown(my_state->http_vc, 1, 0) == TS_ERROR) {
        LOG_ERROR("TSVConnShutdown");
      }
      my_state->h_read_vio = NULL;
    }
    /* FALL THROUGH */
  case TS_EVENT_VCONN_WRITE_COMPLETE:
    /* We should have already shutdown read http side */
    TSAssert(my_state->h_read_vio == NULL);
    if (TSVConnShutdown(my_state->net_vc, 0, 1) == TS_ERROR) {
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

static int
pvc_plugin(TSCont contp, TSEvent event, void *edata)
{
  LOG_SET_FUNCTION_NAME("pvc_plugin");

  pvc_state *my_state = TSContDataGet(contp);
  if (my_state == TS_ERROR_PTR) {
    LOG_ERROR("TSContDataGet");
  }

  if (edata == my_state->h_read_vio) {
    pvc_process_h_read(contp, event, my_state);
  } else if (edata == my_state->h_write_vio) {
    pvc_process_h_write(contp, event, my_state);
  } else if (edata == my_state->n_read_vio) {
    pvc_process_n_read(contp, event, my_state);
  } else if (edata == my_state->n_write_vio) {
    pvc_process_n_write(contp, event, my_state);
  } else {
    TSAssert(0);
  }

  return 0;
}

static void
pvc_process_accept(TSVConn net_vc)
{
  LOG_SET_FUNCTION_NAME("pvc_process_accept");

  TSMutex mutexp;
  TSCont contp;
  pvc_state *my_state = (pvc_state *) TSmalloc(sizeof(pvc_state));
  unsigned int remote_ip;

  mutexp = TSMutexCreate();
  if (mutexp == TS_ERROR_PTR) {
    LOG_ERROR("TSMutexCreate");
    return;
  }
  contp = TSContCreate(pvc_plugin, mutexp);
  if (contp == TS_ERROR_PTR) {
    LOG_ERROR("TSContCreate");
    return;
  }

  /* We need to lock the mutex to prevent I/O callbacks before
     we set everything up */
  if (TSMutexLock(mutexp) == TS_ERROR) {
    LOG_ERROR("TSMutexLock");
  }

  my_state->net_vc = net_vc;

  my_state->req_finished = 0;
  my_state->resp_finished = 0;

  my_state->req_buffer = TSIOBufferCreate();
  my_state->req_reader = TSIOBufferReaderAlloc(my_state->req_buffer);
  my_state->resp_buffer = TSIOBufferCreate();
  my_state->resp_reader = TSIOBufferReaderAlloc(my_state->resp_buffer);
  if ((my_state->req_buffer == TS_ERROR_PTR) || (my_state->req_reader == TS_ERROR_PTR) ||
      (my_state->resp_buffer == TS_ERROR_PTR) || (my_state->resp_reader == TS_ERROR_PTR)) {
    if (TSVConnClose(my_state->net_vc) == TS_ERROR) {
      LOG_ERROR("TSVConnClose");
    }
    pvc_cleanup(contp, my_state);
    LOG_ERROR_AND_CLEANUP("TSIOBufferCreate || TSIOBufferReaderAlloc");
  }

  if (TSNetVConnRemoteIPGet(my_state->net_vc, &remote_ip) == TS_ERROR) {
    LOG_ERROR_AND_CLEANUP("TSNetVConnRemoteIPGet");
  }
/*     if (TSHttpConnect(ntohl(remote_ip), 0, &(my_state->http_vc)) == TS_ERROR) { */
/* 	LOG_ERROR_AND_CLEANUP("TSHttpConnect"); */
/*     } */
  if (TSHttpConnect(remote_ip, plugin_port, &(my_state->http_vc)) == TS_ERROR) {
    LOG_ERROR_AND_CLEANUP("TSHttpConnect");
  }

/* Negative test for TSHttpConnect */
#ifdef DEBUG
  if (TSHttpConnect(plugin_ip, plugin_port, NULL) != TS_ERROR) {
    LOG_ERROR_NEG("TSHttpConnect");
  }
#endif

  if (TSContDataSet(contp, my_state) == TS_ERROR) {
    LOG_ERROR_AND_CLEANUP("TSHttpConnect");
  }

  my_state->h_read_vio = TSVConnRead(my_state->http_vc, contp, my_state->resp_buffer, INT_MAX);
  if (my_state->h_read_vio == TS_ERROR_PTR) {
    LOG_ERROR_AND_CLEANUP("TSVConnRead");
  }
  my_state->h_write_vio = TSVConnWrite(my_state->http_vc, contp, my_state->req_reader, INT_MAX);
  if (my_state->h_write_vio == TS_ERROR_PTR) {
    LOG_ERROR_AND_CLEANUP("TSVConnWrite");
  }

  my_state->n_read_vio = TSVConnRead(my_state->net_vc, contp, my_state->req_buffer, INT_MAX);
  if (my_state->n_read_vio == TS_ERROR_PTR) {
    LOG_ERROR_AND_CLEANUP("TSVConnRead");
  }
  my_state->n_write_vio = TSVConnWrite(my_state->net_vc, contp, my_state->resp_reader, INT_MAX);
  if (my_state->n_write_vio == TS_ERROR_PTR) {
    LOG_ERROR_AND_CLEANUP("TSVConnWrite");
  }

Lcleanup:
  if (TSMutexUnlock(mutexp) == TS_ERROR) {
    LOG_ERROR("TSMutexUnlock");
  }
}


static int
accept_func(TSCont contp, TSEvent event, void *edata)
{
  LOG_SET_FUNCTION_NAME("accept_func");

  switch (event) {
  case TS_EVENT_NET_ACCEPT:
    pvc_process_accept((TSVConn) edata);
    break;

  case TS_EVENT_NET_ACCEPT_FAILED:
    LOG_ERROR("TS_EVENT_NET_ACCEPT_FAILED");
    TSError("Accept failed\n");
    break;

  default:
    TSDebug(PLUGIN_NAME, "Bad event %d", event);
    TSReleaseAssert(!"Unexpected event");
    break;
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
  TSCont accept_cont;
  TSMutex mutex;
  int port;

  info.plugin_name = "test-pos";
  info.vendor_name = "MyCompany";
  info.support_email = "ts-api-support@MyCompany.com";

  if (!TSPluginRegister(TS_SDK_VERSION_2_0, &info)) {
    TSError("Plugin registration failed.\n");
  }

  if (!check_ts_version()) {
    TSError("Plugin requires Traffic Server 2.0 or later\n");
    return;
  }

  if (argc != 2) {
    TSError("No accept port specified\n");
    return;
  }
  port = atoi(argv[1]);

  if (port <= 0) {
    TSError("Bad port specified\n");
  } else if (port <= 1024) {
    TSError("Priveledged port specified\n");
  }

  mutex = TSMutexCreate();
  if (mutex == TS_ERROR_PTR) {
    LOG_ERROR("TSMutexCreate");
    return;
  }
  accept_cont = TSContCreate(accept_func, mutex);
  if (accept_cont == TS_ERROR_PTR) {
    LOG_ERROR("TSContCreate");
    return;
  }
  accept_action = TSNetAccept(accept_cont, port);
  if (accept_action == TS_ERROR_PTR) {
    LOG_ERROR("TSNetAccept");
    return;
  }
}
