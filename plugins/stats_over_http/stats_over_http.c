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

/* stats.c:  expose traffic server stats over http
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <ctype.h>
#include <limits.h>
#include <ts/ts.h>
#include <string.h>
#include <inttypes.h>
#include <getopt.h>

#include "ts/ink_defs.h"

#define PLUGIN_NAME "stats_over_http"

/* global holding the path used for access to this JSON data */
static const char *url_path = "_stats";
static int url_path_len;

static bool integer_counters = false;
static bool wrap_counters    = false;

typedef struct stats_state_t {
  TSVConn net_vc;
  TSVIO read_vio;
  TSVIO write_vio;

  TSIOBuffer req_buffer;
  TSIOBuffer resp_buffer;
  TSIOBufferReader resp_reader;

  int output_bytes;
  int body_written;
} stats_state;

static void
stats_cleanup(TSCont contp, stats_state *my_state)
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

static void
stats_process_accept(TSCont contp, stats_state *my_state)
{
  my_state->req_buffer  = TSIOBufferCreate();
  my_state->resp_buffer = TSIOBufferCreate();
  my_state->resp_reader = TSIOBufferReaderAlloc(my_state->resp_buffer);
  my_state->read_vio    = TSVConnRead(my_state->net_vc, contp, my_state->req_buffer, INT64_MAX);
}

static int
stats_add_data_to_resp_buffer(const char *s, stats_state *my_state)
{
  int s_len = strlen(s);

  TSIOBufferWrite(my_state->resp_buffer, s, s_len);

  return s_len;
}

static const char RESP_HEADER[] = "HTTP/1.0 200 Ok\r\nContent-Type: text/javascript\r\nCache-Control: no-cache\r\n\r\n";

static int
stats_add_resp_header(stats_state *my_state)
{
  return stats_add_data_to_resp_buffer(RESP_HEADER, my_state);
}

static void
stats_process_read(TSCont contp, TSEvent event, stats_state *my_state)
{
  TSDebug(PLUGIN_NAME, "stats_process_read(%d)", event);
  if (event == TS_EVENT_VCONN_READ_READY) {
    my_state->output_bytes = stats_add_resp_header(my_state);
    TSVConnShutdown(my_state->net_vc, 1, 0);
    my_state->write_vio = TSVConnWrite(my_state->net_vc, contp, my_state->resp_reader, INT64_MAX);
  } else if (event == TS_EVENT_ERROR) {
    TSError("[%s] stats_process_read: Received TS_EVENT_ERROR", PLUGIN_NAME);
  } else if (event == TS_EVENT_VCONN_EOS) {
    /* client may end the connection, simply return */
    return;
  } else if (event == TS_EVENT_NET_ACCEPT_FAILED) {
    TSError("[%s] stats_process_read: Received TS_EVENT_NET_ACCEPT_FAILED", PLUGIN_NAME);
  } else {
    printf("Unexpected Event %d\n", event);
    TSReleaseAssert(!"Unexpected Event");
  }
}

#define APPEND(a) my_state->output_bytes += stats_add_data_to_resp_buffer(a, my_state)
#define APPEND_STAT(a, fmt, v)                                              \
  do {                                                                      \
    char b[256];                                                            \
    if (snprintf(b, sizeof(b), "\"%s\": \"" fmt "\",\n", a, v) < sizeof(b)) \
      APPEND(b);                                                            \
  } while (0)
#define APPEND_STAT_NUMERIC(a, fmt, v)                                          \
  do {                                                                          \
    char b[256];                                                                \
    if (integer_counters) {                                                     \
      if (snprintf(b, sizeof(b), "\"%s\": " fmt ",\n", a, v) < sizeof(b)) {     \
        APPEND(b);                                                              \
      }                                                                         \
    } else {                                                                    \
      if (snprintf(b, sizeof(b), "\"%s\": \"" fmt "\",\n", a, v) < sizeof(b)) { \
        APPEND(b);                                                              \
      }                                                                         \
    }                                                                           \
  } while (0)

// This wraps uint64_t values to the int64_t range to fit into a Java long. Java 8 has an unsigned long which
// can interoperate with a full uint64_t, but it's unlikely that much of the ecosystem supports that yet.
static uint64_t
wrap_unsigned_counter(uint64_t value)
{
  if (wrap_counters) {
    return (value > INT64_MAX) ? value % INT64_MAX : value;
  } else {
    return value;
  }
}

static void
json_out_stat(TSRecordType rec_type ATS_UNUSED, void *edata, int registered ATS_UNUSED, const char *name,
              TSRecordDataType data_type, TSRecordData *datum)
{
  stats_state *my_state = edata;

  switch (data_type) {
  case TS_RECORDDATATYPE_COUNTER:
    APPEND_STAT_NUMERIC(name, "%" PRIu64, wrap_unsigned_counter(datum->rec_counter));
    break;
  case TS_RECORDDATATYPE_INT:
    APPEND_STAT_NUMERIC(name, "%" PRIu64, wrap_unsigned_counter(datum->rec_int));
    break;
  case TS_RECORDDATATYPE_FLOAT:
    APPEND_STAT_NUMERIC(name, "%f", datum->rec_float);
    break;
  case TS_RECORDDATATYPE_STRING:
    APPEND_STAT(name, "%s", datum->rec_string);
    break;
  default:
    TSDebug(PLUGIN_NAME, "unknown type for %s: %d", name, data_type);
    break;
  }
}
static void
json_out_stats(stats_state *my_state)
{
  const char *version;
  APPEND("{ \"global\": {\n");

  TSRecordDump((TSRecordType)(TS_RECORDTYPE_PLUGIN | TS_RECORDTYPE_NODE | TS_RECORDTYPE_PROCESS), json_out_stat, my_state);
  version = TSTrafficServerVersionGet();
  APPEND("\"server\": \"");
  APPEND(version);
  APPEND("\"\n");
  APPEND("  }\n}\n");
}

static void
stats_process_write(TSCont contp, TSEvent event, stats_state *my_state)
{
  if (event == TS_EVENT_VCONN_WRITE_READY) {
    if (my_state->body_written == 0) {
      TSDebug(PLUGIN_NAME, "plugin adding response body");
      my_state->body_written = 1;
      json_out_stats(my_state);
      TSVIONBytesSet(my_state->write_vio, my_state->output_bytes);
    }
    TSVIOReenable(my_state->write_vio);
  } else if (TS_EVENT_VCONN_WRITE_COMPLETE) {
    stats_cleanup(contp, my_state);
  } else if (event == TS_EVENT_ERROR) {
    TSError("[%s] stats_process_write: Received TS_EVENT_ERROR", PLUGIN_NAME);
  } else {
    TSReleaseAssert(!"Unexpected Event");
  }
}

static int
stats_dostuff(TSCont contp, TSEvent event, void *edata)
{
  stats_state *my_state = TSContDataGet(contp);
  if (event == TS_EVENT_NET_ACCEPT) {
    my_state->net_vc = (TSVConn)edata;
    stats_process_accept(contp, my_state);
  } else if (edata == my_state->read_vio) {
    stats_process_read(contp, event, my_state);
  } else if (edata == my_state->write_vio) {
    stats_process_write(contp, event, my_state);
  } else {
    TSReleaseAssert(!"Unexpected Event");
  }
  return 0;
}

static int
stats_origin(TSCont contp ATS_UNUSED, TSEvent event ATS_UNUSED, void *edata)
{
  TSCont icontp;
  stats_state *my_state;
  TSHttpTxn txnp = (TSHttpTxn)edata;
  TSMBuffer reqp;
  TSMLoc hdr_loc = NULL, url_loc = NULL;
  TSEvent reenable = TS_EVENT_HTTP_CONTINUE;

  TSDebug(PLUGIN_NAME, "in the read stuff");

  if (TSHttpTxnClientReqGet(txnp, &reqp, &hdr_loc) != TS_SUCCESS)
    goto cleanup;

  if (TSHttpHdrUrlGet(reqp, hdr_loc, &url_loc) != TS_SUCCESS)
    goto cleanup;

  int path_len     = 0;
  const char *path = TSUrlPathGet(reqp, url_loc, &path_len);
  TSDebug(PLUGIN_NAME, "Path: %.*s", path_len, path);

  if (!(path_len != 0 && path_len == url_path_len && !memcmp(path, url_path, url_path_len))) {
    goto notforme;
  }

  TSSkipRemappingSet(txnp, 1); // not strictly necessary, but speed is everything these days

  /* This is us -- register our intercept */
  TSDebug(PLUGIN_NAME, "Intercepting request");

  icontp   = TSContCreate(stats_dostuff, TSMutexCreate());
  my_state = (stats_state *)TSmalloc(sizeof(*my_state));
  memset(my_state, 0, sizeof(*my_state));
  TSContDataSet(icontp, my_state);
  TSHttpTxnIntercept(icontp, txnp);
  goto cleanup;

notforme:

cleanup:
  if (url_loc)
    TSHandleMLocRelease(reqp, hdr_loc, url_loc);
  if (hdr_loc)
    TSHandleMLocRelease(reqp, TS_NULL_MLOC, hdr_loc);

  TSHttpTxnReenable(txnp, reenable);
  return 0;
}

void
TSPluginInit(int argc, const char *argv[])
{
  TSPluginRegistrationInfo info;

  static const char usage[]             = PLUGIN_NAME ".so [--integer-counters] [PATH]";
  static const struct option longopts[] = {{(char *)("integer-counters"), required_argument, NULL, 'i'},
                                           {(char *)("wrap-counters"), required_argument, NULL, 'w'},
                                           {NULL, 0, NULL, 0}};

  info.plugin_name   = PLUGIN_NAME;
  info.vendor_name   = "Apache Software Foundation";
  info.support_email = "dev@trafficserver.apache.org";

  if (TSPluginRegister(&info) != TS_SUCCESS) {
    TSError("[%s] registration failed", PLUGIN_NAME);
  }

  optind = 0;
  for (;;) {
    switch (getopt_long(argc, (char *const *)argv, "iw", longopts, NULL)) {
    case 'i':
      integer_counters = true;
      break;
    case 'w':
      wrap_counters = true;
      break;
    case -1:
      goto init;
    default:
      TSError("[%s] usage: %s", PLUGIN_NAME, usage);
    }
  }

init:
  argc -= optind;
  argv += optind;

  if (argc > 0) {
    url_path = TSstrdup(argv[0] + ('/' == argv[0][0] ? 1 : 0)); /* Skip leading / */
  }
  url_path_len = strlen(url_path);

  /* Create a continuation with a mutex as there is a shared global structure
     containing the headers to add */
  TSHttpHookAdd(TS_HTTP_READ_REQUEST_HDR_HOOK, TSContCreate(stats_origin, NULL));
  TSDebug(PLUGIN_NAME, "stats module registered");
}
