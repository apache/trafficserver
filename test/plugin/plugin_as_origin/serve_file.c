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

/* serve_file.c: plugin to test plugin as origin server interface by
 *   serving a file from the file system
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

#define DEBUG_TAG "serve_file-dbg"
#define NEG_DEBUG_TAG "serve_file-neg"
/* #define DEBUG 1 */

/**************************************************
   Log macros for error code return verification
**************************************************/
#define PLUGIN_NAME "serve_file"
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

static char *doc_buf = NULL;
static int doc_size;

struct pvc_state_t
{
  TSVConn net_vc;
  TSVIO read_vio;
  TSVIO write_vio;

  TSIOBuffer req_buffer;
  TSIOBufferReader req_reader;

  TSIOBuffer resp_buffer;
  TSIOBufferReader resp_reader;

  TSHttpTxn http_txnp;

  int output_bytes;
  int body_written;
};
typedef struct pvc_state_t pvc_state;

static void
pvc_cleanup(TSCont contp, pvc_state * my_state)
{
  LOG_SET_FUNCTION_NAME("pvc_cleanup");

  /* Fix for TSqa12401: need to destroy req_buffer */
  if (my_state->req_buffer) {
    if (TSIOBufferDestroy(my_state->req_buffer) == TS_ERROR) {
      LOG_ERROR("TSIOBufferDestroy");
    }
    my_state->req_buffer = NULL;
  }

  /* Fix for TSqa12401: need to destroy resp_buffer */
  if (my_state->resp_buffer) {
    if (TSIOBufferDestroy(my_state->resp_buffer) == TS_ERROR) {
      LOG_ERROR("TSIOBufferDestroy");
    }
    my_state->resp_buffer = NULL;
  }

  /* Close net_vc */
  if (TSVConnClose(my_state->net_vc) == TS_ERROR) {
    LOG_ERROR("TSVConnClose");
  }

  /* Fix for TSqa12401: need to free the continuation data */
  TSfree(my_state);

  /* Fix for TSqa12401: need to destroy the continuation */
  TSContDestroy(contp);
}


static int
pvc_add_data_to_resp_buffer(const char *s, pvc_state * my_state)
{
  LOG_SET_FUNCTION_NAME("pvc_add_data_to_resp_buffer");

  int s_len = strlen(s);
  char *buf = (char *) TSmalloc(s_len);

  memcpy(buf, s, s_len);
  TSIOBufferWrite(my_state->resp_buffer, buf, s_len);

  TSfree(buf);
  buf = NULL;
  return s_len;
}

static int
pvc_add_resp_header(pvc_state * my_state)
{
  char resp[] = "HTTP/1.0 200 Ok\r\nServer: PluginVC\r\n"
    "Content-Type: text/plain\r\nCache-Control: no-cache\r\n\r\n";
  return pvc_add_data_to_resp_buffer(resp, my_state);

}

static void
pvc_process_accept(TSCont contp, pvc_state * my_state)
{
  LOG_SET_FUNCTION_NAME("pvc_process_accept");

  my_state->req_buffer = TSIOBufferCreate();
  if (my_state->req_buffer == TS_ERROR_PTR) {
    LOG_ERROR("TSIOBufferCreate");
    return;
  }
  my_state->req_reader = TSIOBufferReaderAlloc(my_state->req_buffer);
  if (my_state->req_reader == TS_ERROR_PTR) {
    LOG_ERROR("TSIOBufferReaderAlloc");
    return;
  }
  my_state->resp_buffer = TSIOBufferCreate();
  if (my_state->resp_buffer == TS_ERROR_PTR) {
    LOG_ERROR("TSIOBufferCreate");
    return;
  }
  my_state->resp_reader = TSIOBufferReaderAlloc(my_state->resp_buffer);
  if (my_state->resp_reader == TS_ERROR_PTR) {
    LOG_ERROR("TSIOBufferReaderAlloc");
    return;
  }

  my_state->read_vio = TSVConnRead(my_state->net_vc, contp, my_state->req_buffer, INT_MAX);
  if (my_state->read_vio == TS_ERROR_PTR) {
    LOG_ERROR("TSVConnRead");
    return;
  }
}

static void
pvc_process_read(TSCont contp, TSEvent event, pvc_state * my_state)
{
  LOG_SET_FUNCTION_NAME("pvc_process_read");

/*     int bytes; */

  TSDebug(DEBUG_TAG, "plugin called: pvc_process_read with event %d", event);

  if (event == TS_EVENT_VCONN_READ_READY) {
/* 	if (TSVIOReenable(my_state->read_vio) == TS_ERROR) { */
/* 	    LOG_ERROR("TSVIOReenable"); */
/* 	    return; */
/* 	} */
/*     } else if (event == TS_EVENT_VCONN_READ_COMPLETE) { */
    TSDebug(DEBUG_TAG, "writing response header and shutting down read side");
    my_state->output_bytes = pvc_add_resp_header(my_state);
    if (TSVConnShutdown(my_state->net_vc, 1, 0) == TS_ERROR) {
      LOG_ERROR("TSVConnShutdown");
      return;
    }
#ifdef DEBUG
    if (TSVConnShutdown(NULL, 0, 0) != TS_ERROR) {
      LOG_ERROR_NEG("TSVConnShutdown");
    } else
      TSDebug(NEG_DEBUG_TAG, "Negative Test TSVConnShutdown 1 passed");
#endif

    my_state->write_vio = TSVConnWrite(my_state->net_vc, contp, my_state->resp_reader, INT_MAX);
    if (my_state->write_vio == TS_ERROR_PTR) {
      LOG_ERROR("TSVConnWrite");
      return;
    }
  } else if (event == TS_EVENT_ERROR) {
    TSError("pvc_process_read: Received TS_EVENT_ERROR\n");
  } else if (event == TS_EVENT_VCONN_EOS) {
    /* client may end the connection, simply return */
    return;
  } else {
    printf("Unexpected Event %d\n", event);
    TSReleaseAssert(!"Unexpected Event");
  }
}

static void
pvc_process_write(TSCont contp, TSEvent event, pvc_state * my_state)
{
  LOG_SET_FUNCTION_NAME("pvc_process_write");

  char body[] = "This is a test\n";
  int nbytes = TSVIONBytesGet(my_state->write_vio);
  int ndone = TSVIONDoneGet(my_state->write_vio);

#ifdef DEBUG
  if (TSVIONBytesGet(NULL) != TS_ERROR) {
    LOG_ERROR_NEG("TSVIONBytesGet");
  } else
    TSDebug(NEG_DEBUG_TAG, "Negative Test TSVIONBytesGet 1 passed");
#endif

  TSDebug(DEBUG_TAG, "plugin called: pvc_process_write with event %d", event);

  if (event == TS_EVENT_VCONN_WRITE_READY) {
    if (my_state->body_written == 0) {
      TSDebug(DEBUG_TAG, "plugin adding response body");
      my_state->body_written = 1;
      my_state->output_bytes += pvc_add_data_to_resp_buffer(doc_buf, my_state);
      if (TSVIONBytesSet(my_state->write_vio, my_state->output_bytes) == TS_ERROR) {
        LOG_ERROR("TSVIONBytesSet");
        return;
      }
    }
    if (TSVIOReenable(my_state->write_vio) == TS_ERROR) {
      LOG_ERROR("TSVIOReenable");
      return;
    }
  } else if (TS_EVENT_VCONN_WRITE_COMPLETE) {
    pvc_cleanup(contp, my_state);
  } else if (event == TS_EVENT_ERROR) {
    TSError("pvc_process_write: Received TS_EVENT_ERROR\n");
  } else {
    TSReleaseAssert(!"Unexpected Event");
  }
}

static int
pvc_plugin(TSCont contp, TSEvent event, void *edata)
{
  pvc_state *my_state = TSContDataGet(contp);

  if (event == TS_EVENT_NET_ACCEPT) {
    my_state->net_vc = (TSVConn) edata;
    pvc_process_accept(contp, my_state);
  } else if (edata == my_state->read_vio) {
    pvc_process_read(contp, event, my_state);
  } else if (edata == my_state->write_vio) {
    pvc_process_write(contp, event, my_state);
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
  TSCont new_cont;
  pvc_state *my_state;

  switch (event) {
  case TS_EVENT_HTTP_READ_REQUEST_HDR:
    new_cont = TSContCreate(pvc_plugin, TSMutexCreate());
    /* TSqa12409 */
/* 	new_cont = TSContCreate (pvc_plugin, NULL); */
    if (new_cont == TS_ERROR_PTR) {
      LOG_ERROR_AND_REENABLE("TSContCreate");
    }

    my_state = (pvc_state *) TSmalloc(sizeof(pvc_state));
    my_state->net_vc = NULL;
    my_state->read_vio = NULL;
    my_state->write_vio = NULL;
    my_state->http_txnp = txnp;
    my_state->body_written = 0;
    if (TSContDataSet(new_cont, my_state) == TS_ERROR) {
      LOG_ERROR_AND_REENABLE("TSContDataSet");
      return -1;
    }

    if (TSHttpTxnIntercept(new_cont, txnp) == TS_ERROR) {
      LOG_ERROR_AND_REENABLE("TSHttpTxnIntercept");
      return -1;
    }
#ifdef DEBUG
    if (TSHttpTxnIntercept(NULL, NULL) != TS_ERROR) {
      LOG_ERROR_NEG("TSHttpTxnIntercept");
    } else
      TSDebug(NEG_DEBUG_TAG, "Negative Test TSHttpTxnIntercept 1 passed");
#endif

/*	TSHttpTxnServerIntercept(new_cont, txnp); */
    if (TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE) == TS_ERROR) {
      LOG_ERROR("TSHttpTxnReenable");
      return -1;
    }
    return 0;
  default:
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

int
load_file(char *file_name)
{

  int fd;
  struct stat finfo;
  char *buf;
  int r;

  fd = open(file_name, O_RDONLY);
  if (fd < 0) {
    TSError("Failed to open file %s : (%d)", file_name, errno);
    return 0;
  }

  if (fstat(fd, &finfo) < 0) {
    close(fd);
    return 0;
  }

  buf = (char *) TSmalloc(finfo.st_size + 1);

  r = read(fd, buf, finfo.st_size);
  if (r < 0) {
    close(fd);
    return 0;
  }
  doc_size = r;
  doc_buf = buf;
  doc_buf[r] = '\0';

  close(fd);
  return 1;
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

/* NEGATIVE TEST for TSPluginRegister */
#ifdef DEBUG
  if (TSPluginRegister(TS_SDK_VERSION_2_0, NULL) != 0) {
    LOG_ERROR_NEG("TSPluginRegister");
  } else
    TSDebug(NEG_DEBUG_TAG, "Negative Test TSPluginRegister 1 passed");
#endif
  if (!TSPluginRegister(TS_SDK_VERSION_2_0, &info)) {
    TSError("Plugin registration failed.\n");
  }

  if (!check_ts_version()) {
    TSError("Plugin requires Traffic Server 2.0 or later\n");
    return;
  }

  if (argc != 2) {
    TSError("Need file name arguement");
  }

  if (load_file((char *) argv[1]) > 0) {
    contp = TSContCreate(attach_pvc_plugin, NULL);
    if (contp == TS_ERROR_PTR) {
      LOG_ERROR("TSContCreate");
    } else {
      if (TSHttpHookAdd(TS_HTTP_READ_REQUEST_HDR_HOOK, contp) == TS_ERROR) {
        LOG_ERROR("TSHttpHookAdd");
      }
    }
  } else {
    fprintf(stderr, "Failed to load file %s\n", argv[1]);
    TSError("Failed to load file %s", argv[1]);
  }
}

/* Plugin needs license in order to be loaded */
int
TSPluginLicenseRequired(void)
{
  return 1;
}
