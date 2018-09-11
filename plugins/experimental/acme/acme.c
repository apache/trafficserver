/** @file

@section license

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
 */

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include <getopt.h>
#include <sys/stat.h>

#include "ts/ts.h"
#include "tscore/ink_platform.h"
#include "tscore/ink_defs.h"

static const char PLUGIN_NAME[]      = "acme";
static const char ACME_WK_PATH[]     = ".well-known/acme-challenge/";
static const char ACME_OK_RESP[]     = "HTTP/1.1 200 OK\r\nContent-Type: application/jose\r\nCache-Control: no-cache\r\n";
static const char ACME_DENIED_RESP[] = "HTTP/1.1 404 Not Found\r\nContent-Type: application/jose\r\nCache-Control: no-cache\r\n";

#define MAX_PATH_LEN 4096

/* This should hold all configurations going forward. */
typedef struct AcmeConfig_t {
  char *proof;
} AcmeConfig;

static AcmeConfig gConfig;

/* State used for the intercept plugin. ToDo: Can this be improved ? */
typedef struct AcmeState_t {
  TSVConn net_vc;
  TSVIO read_vio;
  TSVIO write_vio;

  TSIOBuffer req_buffer;
  TSIOBuffer resp_buffer;
  TSIOBufferReader resp_reader;

  int output_bytes;
  int fd;
  struct stat stat_buf;
} AcmeState;

inline static AcmeState *
make_acme_state()
{
  AcmeState *state = (AcmeState *)TSmalloc(sizeof(AcmeState));

  memset(state, 0, sizeof(AcmeState));
  state->fd = -1;

  return state;
}

/* Create a safe pathname to the proof-type file, the destination must be sufficiently large. */
static size_t
make_absolute_path(char *dest, int dest_len, const char *file, int file_len)
{
  int i;

  for (i = 0; i < file_len; ++i) {
    char c = file[i];

    /* Assure that only Base64-URL chracter are in the path */
    if (!(c == '-' || c == '_' || (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'))) {
      TSDebug(PLUGIN_NAME, "Invalid Base64 character found, error");
      return 0;
    }
  }

  return snprintf(dest, dest_len, "%s/%.*s", gConfig.proof, file_len, file);
}

static void
open_acme_file(AcmeState *state, const char *file, int file_len)
{
  char fname[MAX_PATH_LEN];
  int len = make_absolute_path(fname, MAX_PATH_LEN - 1, file, file_len);

  /* 1. Make sure the filename is reasonable */
  if (!len || (len >= (MAX_PATH_LEN - 1))) {
    TSDebug(PLUGIN_NAME, "invalid filename");
    return;
  }

  /* 2. Open the file */
  state->fd = open(fname, O_RDONLY);
  if (-1 == state->fd) {
    TSDebug(PLUGIN_NAME, "can not open file %s (%s)", fname, strerror(errno));
    return;
  }

  /* 3. stat() the file */
  if (fstat(state->fd, &state->stat_buf)) {
    TSDebug(PLUGIN_NAME, "can not stat() file %s (%s)", fname, strerror(errno));
    close(state->fd);
    state->fd = -1;
    return;
  }

  TSDebug(PLUGIN_NAME, "opened filename of %s for read()", fname);
  return;
}

/* Cleanup after intercept has completed */
static void
cleanup(TSCont contp, AcmeState *my_state)
{
  if (my_state->req_buffer) {
    TSIOBufferDestroy(my_state->req_buffer);
    my_state->req_buffer = NULL;
  }

  if (my_state->resp_buffer) {
    TSIOBufferDestroy(my_state->resp_buffer);
    my_state->resp_buffer = NULL;
  }

  TSVConnClose(my_state->net_vc);
  TSfree(my_state);
  TSContDestroy(contp);
}

/* Add data to the output */
inline static int
add_data_to_resp(const char *buf, int len, AcmeState *my_state)
{
  TSIOBufferWrite(my_state->resp_buffer, buf, len);
  return len;
}

static int
add_file_to_resp(AcmeState *my_state)
{
  if (-1 == my_state->fd) {
    return add_data_to_resp("\r\n", 2, my_state);
  } else {
    int ret = 0, len;
    char buf[8192];

    while (1) {
      len = read(my_state->fd, buf, sizeof(buf));
      if ((0 == len) || ((-1 == len) && (errno != EAGAIN) && (errno != EINTR))) {
        break;
      } else {
        TSIOBufferWrite(my_state->resp_buffer, buf, len);
        ret += len;
      }
    }
    close(my_state->fd);
    my_state->fd = -1;

    return ret;
  }
}

/* Process a read event from the SM */
static void
acme_process_read(TSCont contp, TSEvent event, AcmeState *my_state)
{
  if (event == TS_EVENT_VCONN_READ_READY) {
    if (-1 == my_state->fd) {
      my_state->output_bytes = add_data_to_resp(ACME_DENIED_RESP, strlen(ACME_DENIED_RESP), my_state);
    } else {
      my_state->output_bytes = add_data_to_resp(ACME_OK_RESP, strlen(ACME_OK_RESP), my_state);
    }
    TSVConnShutdown(my_state->net_vc, 1, 0);
    my_state->write_vio = TSVConnWrite(my_state->net_vc, contp, my_state->resp_reader, INT64_MAX);
  } else if (event == TS_EVENT_ERROR) {
    TSError("[%s] acme_process_read: Received TS_EVENT_ERROR", PLUGIN_NAME);
  } else if (event == TS_EVENT_VCONN_EOS) {
    /* client may end the connection, simply return */
    return;
  } else if (event == TS_EVENT_NET_ACCEPT_FAILED) {
    TSError("[%s] acme_process_read: Received TS_EVENT_NET_ACCEPT_FAILED", PLUGIN_NAME);
  } else {
    TSReleaseAssert(!"Unexpected Event");
  }
}

/* Process a write event from the SM */
static void
acme_process_write(TSCont contp, TSEvent event, AcmeState *my_state)
{
  if (event == TS_EVENT_VCONN_WRITE_READY) {
    char buf[64]; /* Plenty of space for CL: header */
    int len;

    len = snprintf(buf, sizeof(buf), "Content-Length: %zd\r\n\r\n", (size_t)my_state->stat_buf.st_size);
    my_state->output_bytes += add_data_to_resp(buf, len, my_state);
    my_state->output_bytes += add_file_to_resp(my_state);

    TSVIONBytesSet(my_state->write_vio, my_state->output_bytes);
    TSVIOReenable(my_state->write_vio);
  } else if (TS_EVENT_VCONN_WRITE_COMPLETE) {
    cleanup(contp, my_state);
  } else if (event == TS_EVENT_ERROR) {
    TSError("[%s] acme_process_write: Received TS_EVENT_ERROR", PLUGIN_NAME);
  } else {
    TSReleaseAssert(!"Unexpected Event");
  }
}

/* Process the accept event from the SM */
static void
acme_process_accept(TSCont contp, AcmeState *my_state)
{
  my_state->req_buffer  = TSIOBufferCreate();
  my_state->resp_buffer = TSIOBufferCreate();
  my_state->resp_reader = TSIOBufferReaderAlloc(my_state->resp_buffer);
  my_state->read_vio    = TSVConnRead(my_state->net_vc, contp, my_state->req_buffer, INT64_MAX);
}

/* Implement the server intercept */
static int
acme_intercept(TSCont contp, TSEvent event, void *edata)
{
  AcmeState *my_state = TSContDataGet(contp);

  if (event == TS_EVENT_NET_ACCEPT) {
    my_state->net_vc = (TSVConn)edata;
    acme_process_accept(contp, my_state);
  } else if (edata == my_state->read_vio) { /* All read events */
    acme_process_read(contp, event, my_state);
  } else if (edata == my_state->write_vio) { /* All write events */
    acme_process_write(contp, event, my_state);
  } else {
    TSReleaseAssert(!"Unexpected Event");
  }

  return 0;
}

/* Read-request header continuation, used to kick off the server intercept if necessary */
static int
acme_hook(TSCont contp ATS_UNUSED, TSEvent event ATS_UNUSED, void *edata)
{
  TSMBuffer reqp;
  TSMLoc hdr_loc = NULL, url_loc = NULL;
  TSCont icontp;
  AcmeState *my_state;
  TSHttpTxn txnp = (TSHttpTxn)edata;

  TSDebug(PLUGIN_NAME, "kicking off ACME hook");

  if ((TS_SUCCESS == TSHttpTxnClientReqGet(txnp, &reqp, &hdr_loc)) && (TS_SUCCESS == TSHttpHdrUrlGet(reqp, hdr_loc, &url_loc))) {
    int path_len     = 0;
    const char *path = TSUrlPathGet(reqp, url_loc, &path_len);

    /* Short circuit the / path, common case */
    if (!path || path_len < (int)(strlen(ACME_WK_PATH) + 2) || *path != '.' || memcmp(path, ACME_WK_PATH, strlen(ACME_WK_PATH))) {
      TSDebug(PLUGIN_NAME, "skipping URL path = %.*s", path_len, path);
      goto cleanup;
    }

    TSSkipRemappingSet(txnp, 1); /* not strictly necessary, but speed is everything these days */

    /* This request is for us -- register our intercept */
    icontp = TSContCreate(acme_intercept, TSMutexCreate());

    my_state = make_acme_state();
    open_acme_file(my_state, path + strlen(ACME_WK_PATH), path_len - strlen(ACME_WK_PATH));

    TSContDataSet(icontp, my_state);
    TSHttpTxnIntercept(icontp, txnp);
    TSDebug(PLUGIN_NAME, "created intercept hook");
  }

cleanup:
  if (url_loc) {
    TSHandleMLocRelease(reqp, hdr_loc, url_loc);
  }
  if (hdr_loc) {
    TSHandleMLocRelease(reqp, TS_NULL_MLOC, hdr_loc);
  }

  TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);

  return 0;
}

/* Initialize the plugin / global continuation hook */
void
TSPluginInit(int argc, const char *argv[])
{
  TSPluginRegistrationInfo info;
  const char *proof = "acme";

  static const struct option longopt[] = {
    {(char *)"proof-directory", optional_argument, NULL, 'p'},
    {NULL, no_argument, NULL, '\0'},
  };

  memset(&gConfig, 0, sizeof(gConfig));
  while (true) {
    int opt = getopt_long(argc, (char *const *)argv, "", longopt, NULL);

    switch (opt) {
    case 'p':
      proof = optarg;
      break;
    }

    if (opt == -1) {
      break;
    }
  }

  if ('/' != *proof) {
    const char *confdir = TSConfigDirGet();
    int len             = strlen(proof) + strlen(confdir) + 8;

    gConfig.proof = TSmalloc(len);
    snprintf(gConfig.proof, len, "%s/%s", confdir, proof);
    TSDebug(PLUGIN_NAME, "base directory for proof-types is %s", gConfig.proof);
  } else {
    gConfig.proof = TSstrdup(proof);
  }

  info.plugin_name   = "acme";
  info.vendor_name   = "Apache Software Foundation";
  info.support_email = "dev@trafficserver.apache.org";

  if (TS_SUCCESS != TSPluginRegister(&info)) {
    TSError("[%s] Plugin registration failed", PLUGIN_NAME);
    return;
  }

  TSDebug(PLUGIN_NAME, "Started the %s plugin", PLUGIN_NAME);
  TSDebug(PLUGIN_NAME, "\tproof-type dir = %s", gConfig.proof);

  TSHttpHookAdd(TS_HTTP_READ_REQUEST_HDR_HOOK, TSContCreate(acme_hook, NULL));
}
