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

/*   http_connect_bridge.c - Test program for INKHttpConnect() interface.
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

/* Global variables */
INKAction *accept_action;
static int plugin_port = 2499;
static unsigned int plugin_ip = 0;

/* static char* doc_buf = NULL; */
/* static int   doc_size; */

struct pvc_state_t
{

  INKVConn http_vc;
  INKVIO h_read_vio;
  INKVIO h_write_vio;

  INKVConn net_vc;
  INKVIO n_read_vio;
  INKVIO n_write_vio;

  INKIOBuffer req_buffer;
  INKIOBufferReader req_reader;

  INKIOBuffer resp_buffer;
  INKIOBufferReader resp_reader;

  int req_finished;
  int resp_finished;

};
typedef struct pvc_state_t pvc_state;

static void
pvc_cleanup(INKCont contp, pvc_state * my_state)
{
  LOG_SET_FUNCTION_NAME("pvc_cleanup");

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

  INKfree(my_state);
  if (INKContDestroy(contp) == INK_ERROR) {
    LOG_ERROR("INKContDestroy");
  }
}

static void
pvc_check_done(INKCont contp, pvc_state * my_state)
{
  LOG_SET_FUNCTION_NAME("pvc_check_done");

  if (my_state->req_finished && my_state->resp_finished) {
    if (INKVConnClose(my_state->http_vc) == INK_ERROR) {
      LOG_ERROR("INKVConnClose");
    }
    if (INKVConnClose(my_state->net_vc) == INK_ERROR) {
      LOG_ERROR("INKVConnClose");
    }
    pvc_cleanup(contp, my_state);
  }
}

static void
pvc_process_n_read(INKCont contp, INKEvent event, pvc_state * my_state)
{
  LOG_SET_FUNCTION_NAME("pvc_process_n_read");

  int bytes;

  INKDebug(DEBUG_TAG, "plugin called: pvc_process_n_read with event %d", event);

  switch (event) {
  case INK_EVENT_VCONN_READ_READY:
    if (INKVIOReenable(my_state->h_write_vio) == INK_ERROR) {
      LOG_ERROR("INKVIOReenable");
    }
    break;
  case INK_EVENT_VCONN_READ_COMPLETE:
  case INK_EVENT_VCONN_EOS:
  case INK_EVENT_ERROR:
    {
      /* We're finished reading from the net vc */
      int ndone = INKVIONDoneGet(my_state->n_read_vio);
      int todo;

      if (ndone == INK_ERROR) {
        LOG_ERROR("INKVIONDoneGet");
      }
      my_state->n_read_vio = NULL;
      if (INKVIONBytesSet(my_state->h_write_vio, ndone) == INK_ERROR) {
        LOG_ERROR("INKVIONBytesSet");
      }
      if (INKVConnShutdown(my_state->net_vc, 1, 0) == INK_ERROR) {
        LOG_ERROR("INKVConnShutdown");
      }

      todo = INKVIONTodoGet(my_state->h_write_vio);
      if (todo == INK_ERROR) {
        LOG_ERROR("INKVIONTodoGet");
        /* Error so set it to 0 to cleanup */
        todo = 0;
      }

      if (todo == 0) {
        my_state->req_finished = 1;
        if (INKVConnShutdown(my_state->http_vc, 0, 1) == INK_ERROR) {
          LOG_ERROR("INKVConnShutdown");
        }
        pvc_check_done(contp, my_state);
      } else {
        if (INKVIOReenable(my_state->h_write_vio) == INK_ERROR) {
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
pvc_process_h_write(INKCont contp, INKEvent event, pvc_state * my_state)
{
  LOG_SET_FUNCTION_NAME("pvc_process_h_write");

  int bytes;

  INKDebug(DEBUG_TAG, "plugin called: pvc_process_h_write with event %d", event);

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
        LOG_ERROR("INKVConnShutdown");
      }
      my_state->n_read_vio = NULL;
    }
    /* FALL THROUGH */
  case INK_EVENT_VCONN_WRITE_COMPLETE:
    /* We should have already shutdown read pvc side */
    INKAssert(my_state->n_read_vio == NULL);
    if (INKVConnShutdown(my_state->http_vc, 0, 1) == INK_ERROR) {
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
pvc_process_h_read(INKCont contp, INKEvent event, pvc_state * my_state)
{
  LOG_SET_FUNCTION_NAME("pvc_process_h_read");

  int bytes;

  INKDebug(DEBUG_TAG, "plugin called: pvc_process_h_read with event %d", event);

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
      /* We're finished reading from the http vc */
      int ndone;
      int todo;

      if ((ndone = INKVIONDoneGet(my_state->h_read_vio)) == INK_ERROR) {
        LOG_ERROR("INKVIONDoneGet");
      }

      my_state->h_read_vio = NULL;
      if (INKVIONBytesSet(my_state->n_write_vio, ndone) == INK_ERROR) {
        LOG_ERROR("INKVIONBytesSet");
      }
      if (INKVConnShutdown(my_state->http_vc, 1, 0) == INK_ERROR) {
        LOG_ERROR("INKVConnShutdown");
      }

      todo = INKVIONTodoGet(my_state->n_write_vio);
      if (todo == INK_ERROR) {
        LOG_ERROR("INKVIONTodoGet");
        /* Error so set it to 0 to cleanup */
        todo = 0;
      }

      if (todo == 0) {
        my_state->resp_finished = 1;
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

  INKDebug(DEBUG_TAG, "plugin called: pvc_process_n_write with event %d", event);

  switch (event) {
  case INK_EVENT_VCONN_WRITE_READY:
    if (my_state->h_read_vio) {
      if (INKVIOReenable(my_state->h_read_vio) == INK_ERROR) {
        LOG_ERROR("INKVIOReenable");
      }
    }
    break;
  case INK_EVENT_ERROR:
    if (my_state->h_read_vio) {
      if (INKVConnShutdown(my_state->http_vc, 1, 0) == INK_ERROR) {
        LOG_ERROR("INKVConnShutdown");
      }
      my_state->h_read_vio = NULL;
    }
    /* FALL THROUGH */
  case INK_EVENT_VCONN_WRITE_COMPLETE:
    /* We should have already shutdown read http side */
    INKAssert(my_state->h_read_vio == NULL);
    if (INKVConnShutdown(my_state->net_vc, 0, 1) == INK_ERROR) {
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

static int
pvc_plugin(INKCont contp, INKEvent event, void *edata)
{
  LOG_SET_FUNCTION_NAME("pvc_plugin");

  pvc_state *my_state = INKContDataGet(contp);
  if (my_state == INK_ERROR_PTR) {
    LOG_ERROR("INKContDataGet");
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
    INKAssert(0);
  }

  return 0;
}

static void
pvc_process_accept(INKVConn net_vc)
{
  LOG_SET_FUNCTION_NAME("pvc_process_accept");

  INKMutex mutexp;
  INKCont contp;
  pvc_state *my_state = (pvc_state *) INKmalloc(sizeof(pvc_state));
  unsigned int remote_ip;

  mutexp = INKMutexCreate();
  if (mutexp == INK_ERROR_PTR) {
    LOG_ERROR("INKMutexCreate");
    return;
  }
  contp = INKContCreate(pvc_plugin, mutexp);
  if (contp == INK_ERROR_PTR) {
    LOG_ERROR("INKContCreate");
    return;
  }

  /* We need to lock the mutex to prevent I/O callbacks before
     we set everything up */
  if (INKMutexLock(mutexp) == INK_ERROR) {
    LOG_ERROR("INKMutexLock");
  }

  my_state->net_vc = net_vc;

  my_state->req_finished = 0;
  my_state->resp_finished = 0;

  my_state->req_buffer = INKIOBufferCreate();
  my_state->req_reader = INKIOBufferReaderAlloc(my_state->req_buffer);
  my_state->resp_buffer = INKIOBufferCreate();
  my_state->resp_reader = INKIOBufferReaderAlloc(my_state->resp_buffer);
  if ((my_state->req_buffer == INK_ERROR_PTR) || (my_state->req_reader == INK_ERROR_PTR) ||
      (my_state->resp_buffer == INK_ERROR_PTR) || (my_state->resp_reader == INK_ERROR_PTR)) {
    if (INKVConnClose(my_state->net_vc) == INK_ERROR) {
      LOG_ERROR("INKVConnClose");
    }
    pvc_cleanup(contp, my_state);
    LOG_ERROR_AND_CLEANUP("INKIOBufferCreate || INKIOBufferReaderAlloc");
  }

  if (INKNetVConnRemoteIPGet(my_state->net_vc, &remote_ip) == INK_ERROR) {
    LOG_ERROR_AND_CLEANUP("INKNetVConnRemoteIPGet");
  }
/*     if (INKHttpConnect(ntohl(remote_ip), 0, &(my_state->http_vc)) == INK_ERROR) { */
/* 	LOG_ERROR_AND_CLEANUP("INKHttpConnect"); */
/*     } */
  if (INKHttpConnect(remote_ip, plugin_port, &(my_state->http_vc)) == INK_ERROR) {
    LOG_ERROR_AND_CLEANUP("INKHttpConnect");
  }

/* Negative test for INKHttpConnect */
#ifdef DEBUG
  if (INKHttpConnect(plugin_ip, plugin_port, NULL) != INK_ERROR) {
    LOG_ERROR_NEG("INKHttpConnect");
  }
#endif

  if (INKContDataSet(contp, my_state) == INK_ERROR) {
    LOG_ERROR_AND_CLEANUP("INKHttpConnect");
  }

  my_state->h_read_vio = INKVConnRead(my_state->http_vc, contp, my_state->resp_buffer, INT_MAX);
  if (my_state->h_read_vio == INK_ERROR_PTR) {
    LOG_ERROR_AND_CLEANUP("INKVConnRead");
  }
  my_state->h_write_vio = INKVConnWrite(my_state->http_vc, contp, my_state->req_reader, INT_MAX);
  if (my_state->h_write_vio == INK_ERROR_PTR) {
    LOG_ERROR_AND_CLEANUP("INKVConnWrite");
  }

  my_state->n_read_vio = INKVConnRead(my_state->net_vc, contp, my_state->req_buffer, INT_MAX);
  if (my_state->n_read_vio == INK_ERROR_PTR) {
    LOG_ERROR_AND_CLEANUP("INKVConnRead");
  }
  my_state->n_write_vio = INKVConnWrite(my_state->net_vc, contp, my_state->resp_reader, INT_MAX);
  if (my_state->n_write_vio == INK_ERROR_PTR) {
    LOG_ERROR_AND_CLEANUP("INKVConnWrite");
  }

Lcleanup:
  if (INKMutexUnlock(mutexp) == INK_ERROR) {
    LOG_ERROR("INKMutexUnlock");
  }
}


static int
accept_func(INKCont contp, INKEvent event, void *edata)
{
  LOG_SET_FUNCTION_NAME("accept_func");

  switch (event) {
  case INK_EVENT_NET_ACCEPT:
    pvc_process_accept((INKVConn) edata);
    break;

  case INK_EVENT_NET_ACCEPT_FAILED:
    LOG_ERROR("INK_EVENT_NET_ACCEPT_FAILED");
    INKError("Accept failed\n");
    break;

  default:
    INKDebug(PLUGIN_NAME, "Bad event %d", event);
    INKReleaseAssert(!"Unexpected event");
    break;
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

    /* Since this is an TS-SDK 2.0 plugin, we need at
       least Traffic Server 2.0 to run */
    if (major_ts_version >= 2) {
      result = 1;
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
  INKCont accept_cont;
  INKMutex mutex;
  int port;

  info.plugin_name = "test-pos";
  info.vendor_name = "MyCompany";
  info.support_email = "ts-api-support@MyCompany.com";

  if (!INKPluginRegister(INK_SDK_VERSION_2_0, &info)) {
    INKError("Plugin registration failed.\n");
  }

  if (!check_ts_version()) {
    INKError("Plugin requires Traffic Server 2.0 or later\n");
    return;
  }

  if (argc != 2) {
    INKError("No accept port specified\n");
    return;
  }
  port = atoi(argv[1]);

  if (port <= 0) {
    INKError("Bad port specified\n");
  } else if (port <= 1024) {
    INKError("Priveledged port specified\n");
  }

  mutex = INKMutexCreate();
  if (mutex == INK_ERROR_PTR) {
    LOG_ERROR("INKMutexCreate");
    return;
  }
  accept_cont = INKContCreate(accept_func, mutex);
  if (accept_cont == INK_ERROR_PTR) {
    LOG_ERROR("INKContCreate");
    return;
  }
  accept_action = INKNetAccept(accept_cont, port);
  if (accept_action == INK_ERROR_PTR) {
    LOG_ERROR("INKNetAccept");
    return;
  }
}
