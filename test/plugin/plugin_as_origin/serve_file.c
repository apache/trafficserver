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

static char *doc_buf = NULL;
static int doc_size;

struct pvc_state_t
{
  INKVConn net_vc;
  INKVIO read_vio;
  INKVIO write_vio;

  INKIOBuffer req_buffer;
  INKIOBufferReader req_reader;

  INKIOBuffer resp_buffer;
  INKIOBufferReader resp_reader;

  INKHttpTxn http_txnp;

  int output_bytes;
  int body_written;
};
typedef struct pvc_state_t pvc_state;

static void
pvc_cleanup(INKCont contp, pvc_state * my_state)
{
  LOG_SET_FUNCTION_NAME("pvc_cleanup");

  /* Fix for INKqa12401: need to destroy req_buffer */
  if (my_state->req_buffer) {
    if (INKIOBufferDestroy(my_state->req_buffer) == INK_ERROR) {
      LOG_ERROR("INKIOBufferDestroy");
    }
    my_state->req_buffer = NULL;
  }

  /* Fix for INKqa12401: need to destroy resp_buffer */
  if (my_state->resp_buffer) {
    if (INKIOBufferDestroy(my_state->resp_buffer) == INK_ERROR) {
      LOG_ERROR("INKIOBufferDestroy");
    }
    my_state->resp_buffer = NULL;
  }

  /* Close net_vc */
  if (INKVConnClose(my_state->net_vc) == INK_ERROR) {
    LOG_ERROR("INKVConnClose");
  }

  /* Fix for INKqa12401: need to free the continuation data */
  INKfree(my_state);

  /* Fix for INKqa12401: need to destroy the continuation */
  INKContDestroy(contp);
}


static int
pvc_add_data_to_resp_buffer(const char *s, pvc_state * my_state)
{
  LOG_SET_FUNCTION_NAME("pvc_add_data_to_resp_buffer");

  int s_len = strlen(s);
  char *buf = (char *) INKmalloc(s_len);

  memcpy(buf, s, s_len);
  INKIOBufferWrite(my_state->resp_buffer, buf, s_len);

  INKfree(buf);
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
pvc_process_accept(INKCont contp, pvc_state * my_state)
{
  LOG_SET_FUNCTION_NAME("pvc_process_accept");

  my_state->req_buffer = INKIOBufferCreate();
  if (my_state->req_buffer == INK_ERROR_PTR) {
    LOG_ERROR("INKIOBufferCreate");
    return;
  }
  my_state->req_reader = INKIOBufferReaderAlloc(my_state->req_buffer);
  if (my_state->req_reader == INK_ERROR_PTR) {
    LOG_ERROR("INKIOBufferReaderAlloc");
    return;
  }
  my_state->resp_buffer = INKIOBufferCreate();
  if (my_state->resp_buffer == INK_ERROR_PTR) {
    LOG_ERROR("INKIOBufferCreate");
    return;
  }
  my_state->resp_reader = INKIOBufferReaderAlloc(my_state->resp_buffer);
  if (my_state->resp_reader == INK_ERROR_PTR) {
    LOG_ERROR("INKIOBufferReaderAlloc");
    return;
  }

  my_state->read_vio = INKVConnRead(my_state->net_vc, contp, my_state->req_buffer, INT_MAX);
  if (my_state->read_vio == INK_ERROR_PTR) {
    LOG_ERROR("INKVConnRead");
    return;
  }
}

static void
pvc_process_read(INKCont contp, INKEvent event, pvc_state * my_state)
{
  LOG_SET_FUNCTION_NAME("pvc_process_read");

/*     int bytes; */

  INKDebug(DEBUG_TAG, "plugin called: pvc_process_read with event %d", event);

  if (event == INK_EVENT_VCONN_READ_READY) {
/* 	if (INKVIOReenable(my_state->read_vio) == INK_ERROR) { */
/* 	    LOG_ERROR("INKVIOReenable"); */
/* 	    return; */
/* 	} */
/*     } else if (event == INK_EVENT_VCONN_READ_COMPLETE) { */
    INKDebug(DEBUG_TAG, "writing response header and shutting down read side");
    my_state->output_bytes = pvc_add_resp_header(my_state);
    if (INKVConnShutdown(my_state->net_vc, 1, 0) == INK_ERROR) {
      LOG_ERROR("INKVConnShutdown");
      return;
    }
#ifdef DEBUG
    if (INKVConnShutdown(NULL, 0, 0) != INK_ERROR) {
      LOG_ERROR_NEG("INKVConnShutdown");
    } else
      INKDebug(NEG_DEBUG_TAG, "Negative Test INKVConnShutdown 1 passed");
#endif

    my_state->write_vio = INKVConnWrite(my_state->net_vc, contp, my_state->resp_reader, INT_MAX);
    if (my_state->write_vio == INK_ERROR_PTR) {
      LOG_ERROR("INKVConnWrite");
      return;
    }
  } else if (event == INK_EVENT_ERROR) {
    INKError("pvc_process_read: Received INK_EVENT_ERROR\n");
  } else if (event == INK_EVENT_VCONN_EOS) {
    /* client may end the connection, simply return */
    return;
  } else {
    printf("Unexpected Event %d\n", event);
    INKReleaseAssert(!"Unexpected Event");
  }
}

static void
pvc_process_write(INKCont contp, INKEvent event, pvc_state * my_state)
{
  LOG_SET_FUNCTION_NAME("pvc_process_write");

  char body[] = "This is a test\n";
  int nbytes = INKVIONBytesGet(my_state->write_vio);
  int ndone = INKVIONDoneGet(my_state->write_vio);

#ifdef DEBUG
  if (INKVIONBytesGet(NULL) != INK_ERROR) {
    LOG_ERROR_NEG("INKVIONBytesGet");
  } else
    INKDebug(NEG_DEBUG_TAG, "Negative Test INKVIONBytesGet 1 passed");
#endif

  INKDebug(DEBUG_TAG, "plugin called: pvc_process_write with event %d", event);

  if (event == INK_EVENT_VCONN_WRITE_READY) {
    if (my_state->body_written == 0) {
      INKDebug(DEBUG_TAG, "plugin adding response body");
      my_state->body_written = 1;
      my_state->output_bytes += pvc_add_data_to_resp_buffer(doc_buf, my_state);
      if (INKVIONBytesSet(my_state->write_vio, my_state->output_bytes) == INK_ERROR) {
        LOG_ERROR("INKVIONBytesSet");
        return;
      }
    }
    if (INKVIOReenable(my_state->write_vio) == INK_ERROR) {
      LOG_ERROR("INKVIOReenable");
      return;
    }
  } else if (INK_EVENT_VCONN_WRITE_COMPLETE) {
    pvc_cleanup(contp, my_state);
  } else if (event == INK_EVENT_ERROR) {
    INKError("pvc_process_write: Received INK_EVENT_ERROR\n");
  } else {
    INKReleaseAssert(!"Unexpected Event");
  }
}

static int
pvc_plugin(INKCont contp, INKEvent event, void *edata)
{
  pvc_state *my_state = INKContDataGet(contp);

  if (event == INK_EVENT_NET_ACCEPT) {
    my_state->net_vc = (INKVConn) edata;
    pvc_process_accept(contp, my_state);
  } else if (edata == my_state->read_vio) {
    pvc_process_read(contp, event, my_state);
  } else if (edata == my_state->write_vio) {
    pvc_process_write(contp, event, my_state);
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
  INKCont new_cont;
  pvc_state *my_state;

  switch (event) {
  case INK_EVENT_HTTP_READ_REQUEST_HDR:
    new_cont = INKContCreate(pvc_plugin, INKMutexCreate());
    /* INKqa12409 */
/* 	new_cont = INKContCreate (pvc_plugin, NULL); */
    if (new_cont == INK_ERROR_PTR) {
      LOG_ERROR_AND_REENABLE("INKContCreate");
    }

    my_state = (pvc_state *) INKmalloc(sizeof(pvc_state));
    my_state->net_vc = NULL;
    my_state->read_vio = NULL;
    my_state->write_vio = NULL;
    my_state->http_txnp = txnp;
    my_state->body_written = 0;
    if (INKContDataSet(new_cont, my_state) == INK_ERROR) {
      LOG_ERROR_AND_REENABLE("INKContDataSet");
      return -1;
    }

    if (INKHttpTxnIntercept(new_cont, txnp) == INK_ERROR) {
      LOG_ERROR_AND_REENABLE("INKHttpTxnIntercept");
      return -1;
    }
#ifdef DEBUG
    if (INKHttpTxnIntercept(NULL, NULL) != INK_ERROR) {
      LOG_ERROR_NEG("INKHttpTxnIntercept");
    } else
      INKDebug(NEG_DEBUG_TAG, "Negative Test INKHttpTxnIntercept 1 passed");
#endif

/*	INKHttpTxnServerIntercept(new_cont, txnp); */
    if (INKHttpTxnReenable(txnp, INK_EVENT_HTTP_CONTINUE) == INK_ERROR) {
      LOG_ERROR("INKHttpTxnReenable");
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

int
load_file(char *file_name)
{

  int fd;
  struct stat finfo;
  char *buf;
  int r;

  fd = open(file_name, O_RDONLY);
  if (fd < 0) {
    INKError("Failed to open file %s : (%d)", file_name, errno);
    return 0;
  }

  if (fstat(fd, &finfo) < 0) {
    close(fd);
    return 0;
  }

  buf = (char *) INKmalloc(finfo.st_size + 1);

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

/* NEGATIVE TEST for INKPluginRegister */
#ifdef DEBUG
  if (INKPluginRegister(INK_SDK_VERSION_5_2, NULL) != 0) {
    LOG_ERROR_NEG("INKPluginRegister");
  } else
    INKDebug(NEG_DEBUG_TAG, "Negative Test INKPluginRegister 1 passed");
#endif
  if (!INKPluginRegister(INK_SDK_VERSION_5_2, &info)) {
    INKError("Plugin registration failed.\n");
  }

  if (!check_ts_version()) {
    INKError("Plugin requires Traffic Server 3.5.2 or later\n");
    return;
  }

  if (argc != 2) {
    INKError("Need file name arguement");
  }

  if (load_file((char *) argv[1]) > 0) {
    contp = INKContCreate(attach_pvc_plugin, NULL);
    if (contp == INK_ERROR_PTR) {
      LOG_ERROR("INKContCreate");
    } else {
      if (INKHttpHookAdd(INK_HTTP_READ_REQUEST_HDR_HOOK, contp) == INK_ERROR) {
        LOG_ERROR("INKHttpHookAdd");
      }
    }
  } else {
    fprintf(stderr, "Failed to load file %s\n", argv[1]);
    INKError("Failed to load file %s", argv[1]);
  }
}

/* Plugin needs license in order to be loaded */
int
INKPluginLicenseRequired(void)
{
  return 1;
}
