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
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <zlib.h>

#include "ink_autoconf.h"

#if HAVE_BROTLI_ENCODE_H
#include <brotli/encode.h>
#endif

#include "tscore/ink_defs.h"

#define PLUGIN_NAME "stats_over_http"
#define FREE_TMOUT 300000
#define STR_BUFFER_SIZE 1024

#define SYSTEM_RECORD_TYPE (0x100)
#define DEFAULT_RECORD_TYPES (SYSTEM_RECORD_TYPE | TS_RECORDTYPE_PROCESS | TS_RECORDTYPE_PLUGIN)

#define DEFAULT_IP "0.0.0.0"
#define DEFAULT_IP6 "::"

/* global holding the path used for access to this JSON data */
#define DEFAULT_URL_PATH "_stats"

// from mod_deflate:
// ZLIB's compression algorithm uses a
// 0-9 based scale that GZIP does where '1' is 'Best speed'
// and '9' is 'Best compression'. Testing has proved level '6'
// to be about the best level to use in an HTTP Server.

const int ZLIB_COMPRESSION_LEVEL = 6;
const char *dictionary           = NULL;

// zlib stuff, see [deflateInit2] at http://www.zlib.net/manual.html
static const int ZLIB_MEMLEVEL = 9; // min=1 (optimize for memory),max=9 (optimized for speed)

static const int WINDOW_BITS_DEFLATE = 15;
static const int WINDOW_BITS_GZIP    = 16;
#define DEFLATE_MODE WINDOW_BITS_DEFLATE
#define GZIP_MODE (WINDOW_BITS_DEFLATE | WINDOW_BITS_GZIP)

// brotli compression quality 1-11. Testing proved level '6'
#if HAVE_BROTLI_ENCODE_H
const int BROTLI_COMPRESSION_LEVEL = 6;
const int BROTLI_LGW               = 16;
#endif

static bool integer_counters = false;
static bool wrap_counters    = false;

typedef struct {
  unsigned int recordTypes;
  char *stats_path;
  int stats_path_len;
  char *allowIps;
  int ipCount;
  char *allowIps6;
  int ip6Count;
} config_t;
typedef struct {
  char *config_path;
  volatile time_t last_load;
  config_t *config;
} config_holder_t;

typedef enum { JSON_OUTPUT, CSV_OUTPUT } output_format;
typedef enum { NONE, DEFLATE, GZIP, BR } encoding_format;

int configReloadRequests = 0;
int configReloads        = 0;
time_t lastReloadRequest = 0;
time_t lastReload        = 0;
time_t astatsLoad        = 0;

static int free_handler(TSCont cont, TSEvent event, void *edata);
static int config_handler(TSCont cont, TSEvent event, void *edata);
static config_t *get_config(TSCont cont);
static config_holder_t *new_config_holder(const char *path);
static bool is_ip_allowed(const config_t *config, const struct sockaddr *addr);

#if HAVE_BROTLI_ENCODE_H
typedef struct {
  BrotliEncoderState *br;
  uint8_t *next_in;
  size_t avail_in;
  uint8_t *next_out;
  size_t avail_out;
  size_t total_in;
  size_t total_out;
} b_stream;
#endif

typedef struct stats_state_t {
  TSVConn net_vc;
  TSVIO read_vio;
  TSVIO write_vio;

  TSIOBuffer req_buffer;
  TSIOBuffer resp_buffer;
  TSIOBufferReader resp_reader;

  int output_bytes;
  int body_written;
  output_format output;
  encoding_format encoding;
  z_stream zstrm;
#if HAVE_BROTLI_ENCODE_H
  b_stream bstrm;
#endif
} stats_state;

static char *
nstr(const char *s)
{
  char *mys = (char *)TSmalloc(strlen(s) + 1);
  strcpy(mys, s);
  return mys;
}

#if HAVE_BROTLI_ENCODE_H
encoding_format
init_br(stats_state *my_state)
{
  my_state->bstrm.br = NULL;

  my_state->bstrm.br = BrotliEncoderCreateInstance(NULL, NULL, NULL);
  if (!my_state->bstrm.br) {
    TSDebug(PLUGIN_NAME, "Brotli Encoder Instance Failed");
    return NONE;
  }
  BrotliEncoderSetParameter(my_state->bstrm.br, BROTLI_PARAM_QUALITY, BROTLI_COMPRESSION_LEVEL);
  BrotliEncoderSetParameter(my_state->bstrm.br, BROTLI_PARAM_LGWIN, BROTLI_LGW);
  my_state->bstrm.next_in   = NULL;
  my_state->bstrm.avail_in  = 0;
  my_state->bstrm.total_in  = 0;
  my_state->bstrm.next_out  = NULL;
  my_state->bstrm.avail_out = 0;
  my_state->bstrm.total_out = 0;
  return BR;
}
#endif

encoding_format
init_gzip(stats_state *my_state, int mode)
{
  my_state->zstrm.next_in   = Z_NULL;
  my_state->zstrm.avail_in  = 0;
  my_state->zstrm.total_in  = 0;
  my_state->zstrm.next_out  = Z_NULL;
  my_state->zstrm.avail_out = 0;
  my_state->zstrm.total_out = 0;
  my_state->zstrm.zalloc    = Z_NULL;
  my_state->zstrm.zfree     = Z_NULL;
  my_state->zstrm.opaque    = Z_NULL;
  my_state->zstrm.data_type = Z_ASCII;
  int err = deflateInit2(&my_state->zstrm, ZLIB_COMPRESSION_LEVEL, Z_DEFLATED, mode, ZLIB_MEMLEVEL, Z_DEFAULT_STRATEGY);
  if (err != Z_OK) {
    TSDebug(PLUGIN_NAME, "gzip initialization failed");
    return NONE;
  } else {
    TSDebug(PLUGIN_NAME, "gzip initialized successfully");
    if (mode == GZIP_MODE) {
      return GZIP;
    } else if (mode == DEFLATE_MODE) {
      return DEFLATE;
    }
  }
  return NONE;
}

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

static const char RESP_HEADER_JSON[] = "HTTP/1.0 200 OK\r\nContent-Type: text/json\r\nCache-Control: no-cache\r\n\r\n";
static const char RESP_HEADER_JSON_GZIP[] =
  "HTTP/1.0 200 OK\r\nContent-Type: text/json\r\nContent-Encoding: gzip\r\nCache-Control: no-cache\r\n\r\n";
static const char RESP_HEADER_JSON_DEFLATE[] =
  "HTTP/1.0 200 OK\r\nContent-Type: text/json\r\nContent-Encoding: deflate\r\nCache-Control: no-cache\r\n\r\n";
static const char RESP_HEADER_JSON_BR[] =
  "HTTP/1.0 200 OK\r\nContent-Type: text/json\r\nContent-Encoding: br\r\nCache-Control: no-cache\r\n\r\n";
static const char RESP_HEADER_CSV[] = "HTTP/1.0 200 OK\r\nContent-Type: text/csv\r\nCache-Control: no-cache\r\n\r\n";
static const char RESP_HEADER_CSV_GZIP[] =
  "HTTP/1.0 200 OK\r\nContent-Type: text/csv\r\nContent-Encoding: gzip\r\nCache-Control: no-cache\r\n\r\n";
static const char RESP_HEADER_CSV_DEFLATE[] =
  "HTTP/1.0 200 OK\r\nContent-Type: text/csv\r\nContent-Encoding: deflate\r\nCache-Control: no-cache\r\n\r\n";
static const char RESP_HEADER_CSV_BR[] =
  "HTTP/1.0 200 OK\r\nContent-Type: text/csv\r\nContent-Encoding: br\r\nCache-Control: no-cache\r\n\r\n";

static int
stats_add_resp_header(stats_state *my_state)
{
  switch (my_state->output) {
  case JSON_OUTPUT:
    if (my_state->encoding == GZIP) {
      return stats_add_data_to_resp_buffer(RESP_HEADER_JSON_GZIP, my_state);
    } else if (my_state->encoding == DEFLATE) {
      return stats_add_data_to_resp_buffer(RESP_HEADER_JSON_DEFLATE, my_state);
    } else if (my_state->encoding == BR) {
      return stats_add_data_to_resp_buffer(RESP_HEADER_JSON_BR, my_state);
    } else {
      return stats_add_data_to_resp_buffer(RESP_HEADER_JSON, my_state);
    }
    break;
  case CSV_OUTPUT:
    if (my_state->encoding == GZIP) {
      return stats_add_data_to_resp_buffer(RESP_HEADER_CSV_GZIP, my_state);
    } else if (my_state->encoding == DEFLATE) {
      return stats_add_data_to_resp_buffer(RESP_HEADER_CSV_DEFLATE, my_state);
    } else if (my_state->encoding == BR) {
      return stats_add_data_to_resp_buffer(RESP_HEADER_CSV_BR, my_state);
    } else {
      return stats_add_data_to_resp_buffer(RESP_HEADER_CSV, my_state);
    }
    break;
  default:
    TSError("stats_add_resp_header: Unknown output format");
    break;
  }
  return stats_add_data_to_resp_buffer(RESP_HEADER_JSON, my_state);
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
#define APPEND_STAT_JSON(a, fmt, v)                                              \
  do {                                                                           \
    char b[256];                                                                 \
    if (snprintf(b, sizeof(b), "\"%s\": \"" fmt "\",\n", a, v) < (int)sizeof(b)) \
      APPEND(b);                                                                 \
  } while (0)
#define APPEND_STAT_JSON_NUMERIC(a, fmt, v)                                          \
  do {                                                                               \
    char b[256];                                                                     \
    if (integer_counters) {                                                          \
      if (snprintf(b, sizeof(b), "\"%s\": " fmt ",\n", a, v) < (int)sizeof(b)) {     \
        APPEND(b);                                                                   \
      }                                                                              \
    } else {                                                                         \
      if (snprintf(b, sizeof(b), "\"%s\": \"" fmt "\",\n", a, v) < (int)sizeof(b)) { \
        APPEND(b);                                                                   \
      }                                                                              \
    }                                                                                \
  } while (0)

#define APPEND_STAT_CSV(a, fmt, v)                                     \
  do {                                                                 \
    char b[256];                                                       \
    if (snprintf(b, sizeof(b), "%s," fmt "\n", a, v) < (int)sizeof(b)) \
      APPEND(b);                                                       \
  } while (0)
#define APPEND_STAT_CSV_NUMERIC(a, fmt, v)                               \
  do {                                                                   \
    char b[256];                                                         \
    if (snprintf(b, sizeof(b), "%s," fmt "\n", a, v) < (int)sizeof(b)) { \
      APPEND(b);                                                         \
    }                                                                    \
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
    APPEND_STAT_JSON_NUMERIC(name, "%" PRIu64, wrap_unsigned_counter(datum->rec_counter));
    break;
  case TS_RECORDDATATYPE_INT:
    APPEND_STAT_JSON_NUMERIC(name, "%" PRIu64, wrap_unsigned_counter(datum->rec_int));
    break;
  case TS_RECORDDATATYPE_FLOAT:
    APPEND_STAT_JSON_NUMERIC(name, "%f", datum->rec_float);
    break;
  case TS_RECORDDATATYPE_STRING:
    APPEND_STAT_JSON(name, "%s", datum->rec_string);
    break;
  default:
    TSDebug(PLUGIN_NAME, "unknown type for %s: %d", name, data_type);
    break;
  }
}

static void
csv_out_stat(TSRecordType rec_type ATS_UNUSED, void *edata, int registered ATS_UNUSED, const char *name, TSRecordDataType data_type,
             TSRecordData *datum)
{
  stats_state *my_state = edata;
  switch (data_type) {
  case TS_RECORDDATATYPE_COUNTER:
    APPEND_STAT_CSV_NUMERIC(name, "%" PRIu64, wrap_unsigned_counter(datum->rec_counter));
    break;
  case TS_RECORDDATATYPE_INT:
    APPEND_STAT_CSV_NUMERIC(name, "%" PRIu64, wrap_unsigned_counter(datum->rec_int));
    break;
  case TS_RECORDDATATYPE_FLOAT:
    APPEND_STAT_CSV_NUMERIC(name, "%f", datum->rec_float);
    break;
  case TS_RECORDDATATYPE_STRING:
    APPEND_STAT_CSV(name, "%s", datum->rec_string);
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

#if HAVE_BROTLI_ENCODE_H
// Takes an input stats state struct holding the uncompressed
// stats values. Compresses and copies it back into the state struct
static void
br_out_stats(stats_state *my_state)
{
  size_t outputsize = BrotliEncoderMaxCompressedSize(my_state->output_bytes);
  uint8_t inputbuf[my_state->output_bytes];
  uint8_t outputbuf[outputsize];

  memset(&inputbuf, 0, sizeof(inputbuf));
  memset(&outputbuf, 0, sizeof(outputbuf));

  int64_t inputbytes = TSIOBufferReaderCopy(my_state->resp_reader, &inputbuf, my_state->output_bytes);

  // Consume existing uncompressed buffer now that it has been stored to
  // free up the buffer to contain the compressed data
  int64_t toconsume = TSIOBufferReaderAvail(my_state->resp_reader);
  TSIOBufferReaderConsume(my_state->resp_reader, toconsume);
  my_state->output_bytes -= toconsume;
  BROTLI_BOOL err = BrotliEncoderCompress(BROTLI_DEFAULT_QUALITY, BROTLI_DEFAULT_WINDOW, BROTLI_DEFAULT_MODE, inputbytes, inputbuf,
                                          &outputsize, outputbuf);

  if (err == BROTLI_FALSE) {
    TSDebug(PLUGIN_NAME, "brotli compress error");
  }
  my_state->output_bytes += TSIOBufferWrite(my_state->resp_buffer, outputbuf, outputsize);
  BrotliEncoderDestroyInstance(my_state->bstrm.br);
}
#endif

// Takes an input stats state struct holding the uncompressed
// stats values. Compresses and copies it back into the state struct
static void
gzip_out_stats(stats_state *my_state)
{
  char inputbuf[my_state->output_bytes];
  char outputbuf[deflateBound(&my_state->zstrm, my_state->output_bytes)];
  memset(&inputbuf, 0, sizeof(inputbuf));
  memset(&outputbuf, 0, sizeof(outputbuf));

  int64_t inputbytes = TSIOBufferReaderCopy(my_state->resp_reader, &inputbuf, my_state->output_bytes);

  // Consume existing uncompressed buffer now that it has been stored to
  // free up the buffer to contain the compressed data
  int64_t toconsume = TSIOBufferReaderAvail(my_state->resp_reader);
  TSIOBufferReaderConsume(my_state->resp_reader, toconsume);

  my_state->output_bytes -= toconsume;
  my_state->zstrm.avail_in  = inputbytes;
  my_state->zstrm.avail_out = sizeof(outputbuf);
  my_state->zstrm.next_in   = (Bytef *)inputbuf;
  my_state->zstrm.next_out  = (Bytef *)outputbuf;
  int err                   = deflate(&my_state->zstrm, Z_FINISH);
  if (err != Z_STREAM_END) {
    TSDebug(PLUGIN_NAME, "deflate error: %d", err);
  }

  err = deflateEnd(&my_state->zstrm);
  if (err != Z_OK) {
    TSDebug(PLUGIN_NAME, "deflate end err: %d", err);
  }

  my_state->output_bytes += TSIOBufferWrite(my_state->resp_buffer, outputbuf, my_state->zstrm.total_out);
}

static void
csv_out_stats(stats_state *my_state)
{
  TSRecordDump((TSRecordType)(TS_RECORDTYPE_PLUGIN | TS_RECORDTYPE_NODE | TS_RECORDTYPE_PROCESS), csv_out_stat, my_state);
  const char *version = TSTrafficServerVersionGet();
  APPEND_STAT_CSV("version", "%s", version);
}

static void
stats_process_write(TSCont contp, TSEvent event, stats_state *my_state)
{
  if (event == TS_EVENT_VCONN_WRITE_READY) {
    if (my_state->body_written == 0) {
      my_state->body_written = 1;
      switch (my_state->output) {
      case JSON_OUTPUT:
        json_out_stats(my_state);
        break;
      case CSV_OUTPUT:
        csv_out_stats(my_state);
        break;
      default:
        TSError("stats_process_write: Unknown output type\n");
        break;
      }

      if ((my_state->encoding == GZIP) || (my_state->encoding == DEFLATE)) {
        gzip_out_stats(my_state);
      }
#if HAVE_BROTLI_ENCODE_H
      else if (my_state->encoding == BR) {
        br_out_stats(my_state);
      }
#endif
      TSVIONBytesSet(my_state->write_vio, my_state->output_bytes);
    }
    TSVIOReenable(my_state->write_vio);
  } else if (event == TS_EVENT_VCONN_WRITE_COMPLETE) {
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
  config_t *config;
  TSHttpTxn txnp = (TSHttpTxn)edata;
  TSMBuffer reqp;
  TSMLoc hdr_loc = NULL, url_loc = NULL, accept_field = NULL, accept_encoding_field = NULL;
  TSEvent reenable = TS_EVENT_HTTP_CONTINUE;

  TSDebug(PLUGIN_NAME, "in the read stuff");
  config = get_config(contp);

  if (TSHttpTxnClientReqGet(txnp, &reqp, &hdr_loc) != TS_SUCCESS) {
    goto cleanup;
  }

  if (TSHttpHdrUrlGet(reqp, hdr_loc, &url_loc) != TS_SUCCESS) {
    goto cleanup;
  }

  int path_len     = 0;
  const char *path = TSUrlPathGet(reqp, url_loc, &path_len);
  TSDebug(PLUGIN_NAME, "Path: %.*s", path_len, path);

  if (!(path_len != 0 && path_len == config->stats_path_len && !memcmp(path, config->stats_path, config->stats_path_len))) {
    goto notforme;
  }

  const struct sockaddr *addr = TSHttpTxnClientAddrGet(txnp);
  if (!is_ip_allowed(config, addr)) {
    TSDebug(PLUGIN_NAME, "not right ip");
    goto notforme;
  }

  TSHttpTxnCntlSet(txnp, TS_HTTP_CNTL_SKIP_REMAPPING, true); // not strictly necessary, but speed is everything these days

  /* This is us -- register our intercept */
  TSDebug(PLUGIN_NAME, "Intercepting request");

  my_state = (stats_state *)TSmalloc(sizeof(*my_state));
  memset(my_state, 0, sizeof(*my_state));
  icontp = TSContCreate(stats_dostuff, TSMutexCreate());

  accept_field     = TSMimeHdrFieldFind(reqp, hdr_loc, TS_MIME_FIELD_ACCEPT, TS_MIME_LEN_ACCEPT);
  my_state->output = JSON_OUTPUT; // default to json output
  // accept header exists, use it to determine response type
  if (accept_field != TS_NULL_MLOC) {
    int len         = -1;
    const char *str = TSMimeHdrFieldValueStringGet(reqp, hdr_loc, accept_field, -1, &len);

    // Parse the Accept header, default to JSON output unless its another supported format
    if (!strncasecmp(str, "text/csv", len)) {
      my_state->output = CSV_OUTPUT;
    } else {
      my_state->output = JSON_OUTPUT;
    }
  }

  // Check for Accept Encoding and init
  accept_encoding_field = TSMimeHdrFieldFind(reqp, hdr_loc, TS_MIME_FIELD_ACCEPT_ENCODING, TS_MIME_LEN_ACCEPT_ENCODING);
  my_state->encoding    = NONE;
  if (accept_encoding_field != TS_NULL_MLOC) {
    int len         = -1;
    const char *str = TSMimeHdrFieldValueStringGet(reqp, hdr_loc, accept_encoding_field, -1, &len);
    if (len >= TS_HTTP_LEN_DEFLATE && strstr(str, TS_HTTP_VALUE_DEFLATE) != NULL) {
      TSDebug(PLUGIN_NAME, "Saw deflate in accept encoding");
      my_state->encoding = init_gzip(my_state, DEFLATE_MODE);
    } else if (len >= TS_HTTP_LEN_GZIP && strstr(str, TS_HTTP_VALUE_GZIP) != NULL) {
      TSDebug(PLUGIN_NAME, "Saw gzip in accept encoding");
      my_state->encoding = init_gzip(my_state, GZIP_MODE);
    }
#if HAVE_BROTLI_ENCODE_H
    else if (len >= TS_HTTP_LEN_BROTLI && strstr(str, TS_HTTP_VALUE_BROTLI) != NULL) {
      TSDebug(PLUGIN_NAME, "Saw br in accept encoding");
      my_state->encoding = init_br(my_state);
    }
#endif
    else {
      my_state->encoding = NONE;
    }
  }
  TSDebug(PLUGIN_NAME, "Finished AE check");

  TSContDataSet(icontp, my_state);
  TSHttpTxnIntercept(icontp, txnp);
  goto cleanup;

notforme:

cleanup:
  if (url_loc) {
    TSHandleMLocRelease(reqp, hdr_loc, url_loc);
  }
  if (hdr_loc) {
    TSHandleMLocRelease(reqp, TS_NULL_MLOC, hdr_loc);
  }
  if (accept_field) {
    TSHandleMLocRelease(reqp, TS_NULL_MLOC, accept_field);
  }
  if (accept_encoding_field) {
    TSHandleMLocRelease(reqp, TS_NULL_MLOC, accept_encoding_field);
  }
  TSHttpTxnReenable(txnp, reenable);
  return 0;
}

void
TSPluginInit(int argc, const char *argv[])
{
  TSPluginRegistrationInfo info;

  static const char usage[]             = PLUGIN_NAME ".so [--integer-counters] [PATH]";
  static const struct option longopts[] = {{(char *)("integer-counters"), no_argument, NULL, 'i'},
                                           {(char *)("wrap-counters"), no_argument, NULL, 'w'},
                                           {NULL, 0, NULL, 0}};
  TSCont main_cont, config_cont;
  config_holder_t *config_holder;

  info.plugin_name   = PLUGIN_NAME;
  info.vendor_name   = "Apache Software Foundation";
  info.support_email = "dev@trafficserver.apache.org";

  if (TSPluginRegister(&info) != TS_SUCCESS) {
    TSError("[%s] registration failed", PLUGIN_NAME);
    goto done;
  }

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

  config_holder = new_config_holder(argc > 0 ? argv[0] : NULL);

  /* Path was not set during load, so the param was not a config file, we also
    have an argument so it must be the path, set it here.  Otherwise if no argument
    then use the default _stats path */
  if ((config_holder->config != NULL) && (config_holder->config->stats_path == 0) && (argc > 0) &&
      (config_holder->config_path == NULL)) {
    config_holder->config->stats_path     = TSstrdup(argv[0] + ('/' == argv[0][0] ? 1 : 0));
    config_holder->config->stats_path_len = strlen(config_holder->config->stats_path);
  } else if ((config_holder->config != NULL) && (config_holder->config->stats_path == 0)) {
    config_holder->config->stats_path     = nstr(DEFAULT_URL_PATH);
    config_holder->config->stats_path_len = strlen(config_holder->config->stats_path);
  }

  /* Create a continuation with a mutex as there is a shared global structure
     containing the headers to add */
  main_cont = TSContCreate(stats_origin, NULL);
  TSContDataSet(main_cont, (void *)config_holder);
  TSHttpHookAdd(TS_HTTP_READ_REQUEST_HDR_HOOK, main_cont);

  /* Create continuation for management updates to re-read config file */
  config_cont = TSContCreate(config_handler, TSMutexCreate());
  TSContDataSet(config_cont, (void *)config_holder);
  TSMgmtUpdateRegister(config_cont, PLUGIN_NAME);
  TSDebug(PLUGIN_NAME, "stats module registered with path %s", config_holder->config->stats_path);

done:
  return;
}

static bool
is_ip_match(const char *ip, char *ipmask, char mask)
{
  unsigned int j, i, k;
  char cm;
  // to be able to set mask to 128
  unsigned int umask = 0xff & mask;

  for (j = 0, i = 0; ((i + 1) * 8) <= umask; i++) {
    if (ip[i] != ipmask[i]) {
      return false;
    }
    j += 8;
  }
  cm = 0;
  for (k = 0; j < umask; j++, k++) {
    cm |= 1 << (7 - k);
  }

  if ((ip[i] & cm) != (ipmask[i] & cm)) {
    return false;
  }
  return true;
}

static bool
is_ip_allowed(const config_t *config, const struct sockaddr *addr)
{
  char ip_port_text_buffer[INET6_ADDRSTRLEN];
  int i;
  char *ipmask;
  if (!addr) {
    return true;
  }

  if (addr->sa_family == AF_INET && config->allowIps) {
    const struct sockaddr_in *addr_in = (struct sockaddr_in *)addr;
    const char *ip                    = (char *)&addr_in->sin_addr;

    for (i = 0; i < config->ipCount; i++) {
      ipmask = config->allowIps + (i * (sizeof(struct in_addr) + 1));
      if (is_ip_match(ip, ipmask, ipmask[4])) {
        TSDebug(PLUGIN_NAME, "clientip is %s--> ALLOW", inet_ntop(AF_INET, ip, ip_port_text_buffer, INET6_ADDRSTRLEN));
        return true;
      }
    }
    TSDebug(PLUGIN_NAME, "clientip is %s--> DENY", inet_ntop(AF_INET, ip, ip_port_text_buffer, INET6_ADDRSTRLEN));
    return false;

  } else if (addr->sa_family == AF_INET6 && config->allowIps6) {
    const struct sockaddr_in6 *addr_in6 = (struct sockaddr_in6 *)addr;
    const char *ip                      = (char *)&addr_in6->sin6_addr;

    for (i = 0; i < config->ip6Count; i++) {
      ipmask = config->allowIps6 + (i * (sizeof(struct in6_addr) + 1));
      if (is_ip_match(ip, ipmask, ipmask[sizeof(struct in6_addr)])) {
        TSDebug(PLUGIN_NAME, "clientip6 is %s--> ALLOW", inet_ntop(AF_INET6, ip, ip_port_text_buffer, INET6_ADDRSTRLEN));
        return true;
      }
    }
    TSDebug(PLUGIN_NAME, "clientip6 is %s--> DENY", inet_ntop(AF_INET6, ip, ip_port_text_buffer, INET6_ADDRSTRLEN));
    return false;
  }
  return true;
}

static void
parseIps(config_t *config, char *ipStr)
{
  char buffer[STR_BUFFER_SIZE];
  char *p, *tok1, *tok2, *ip;
  int i, mask;
  char ip_port_text_buffer[INET_ADDRSTRLEN];

  if (!ipStr) {
    config->ipCount = 1;
    ip = config->allowIps = TSmalloc(sizeof(struct in_addr) + 1);
    inet_pton(AF_INET, DEFAULT_IP, ip);
    ip[4] = 0;
    return;
  }

  strcpy(buffer, ipStr);
  p = buffer;
  while (strtok_r(p, ", \n", &p)) {
    config->ipCount++;
  }
  if (!config->ipCount) {
    return;
  }
  config->allowIps = TSmalloc(5 * config->ipCount); // 4 bytes for ip + 1 for bit mask
  strcpy(buffer, ipStr);
  p = buffer;
  i = 0;
  while ((tok1 = strtok_r(p, ", \n", &p))) {
    TSDebug(PLUGIN_NAME, "%d) parsing: %s", i + 1, tok1);
    tok2 = strtok_r(tok1, "/", &tok1);
    ip   = config->allowIps + ((sizeof(struct in_addr) + 1) * i);
    if (!inet_pton(AF_INET, tok2, ip)) {
      TSDebug(PLUGIN_NAME, "%d) skipping: %s", i + 1, tok1);
      continue;
    }

    if (tok1 != NULL) {
      tok2 = strtok_r(tok1, "/", &tok1);
    }
    if (!tok2) {
      mask = 32;
    } else {
      mask = atoi(tok2);
    }
    ip[4] = mask;
    TSDebug(PLUGIN_NAME, "%d) adding netmask: %s/%d", i + 1, inet_ntop(AF_INET, ip, ip_port_text_buffer, INET_ADDRSTRLEN), ip[4]);
    i++;
  }
}
static void
parseIps6(config_t *config, char *ipStr)
{
  char buffer[STR_BUFFER_SIZE];
  char *p, *tok1, *tok2, *ip;
  int i, mask;
  char ip_port_text_buffer[INET6_ADDRSTRLEN];

  if (!ipStr) {
    config->ip6Count = 1;
    ip = config->allowIps6 = TSmalloc(sizeof(struct in6_addr) + 1);
    inet_pton(AF_INET6, DEFAULT_IP6, ip);
    ip[sizeof(struct in6_addr)] = 0;
    return;
  }

  strcpy(buffer, ipStr);
  p = buffer;
  while (strtok_r(p, ", \n", &p)) {
    config->ip6Count++;
  }
  if (!config->ip6Count) {
    return;
  }

  config->allowIps6 = TSmalloc((sizeof(struct in6_addr) + 1) * config->ip6Count); // 16 bytes for ip + 1 for bit mask
  strcpy(buffer, ipStr);
  p = buffer;
  i = 0;
  while ((tok1 = strtok_r(p, ", \n", &p))) {
    TSDebug(PLUGIN_NAME, "%d) parsing: %s", i + 1, tok1);
    tok2 = strtok_r(tok1, "/", &tok1);
    ip   = config->allowIps6 + ((sizeof(struct in6_addr) + 1) * i);
    if (!inet_pton(AF_INET6, tok2, ip)) {
      TSDebug(PLUGIN_NAME, "%d) skipping: %s", i + 1, tok1);
      continue;
    }

    if (tok1 != NULL) {
      tok2 = strtok_r(tok1, "/", &tok1);
    }

    if (!tok2) {
      mask = 128;
    } else {
      mask = atoi(tok2);
    }
    ip[sizeof(struct in6_addr)] = mask;
    TSDebug(PLUGIN_NAME, "%d) adding netmask: %s/%d", i + 1, inet_ntop(AF_INET6, ip, ip_port_text_buffer, INET6_ADDRSTRLEN),
            ip[sizeof(struct in6_addr)]);
    i++;
  }
}

static config_t *
new_config(TSFile fh)
{
  char buffer[STR_BUFFER_SIZE];
  config_t *config       = NULL;
  config                 = (config_t *)TSmalloc(sizeof(config_t));
  config->stats_path     = 0;
  config->stats_path_len = 0;
  config->allowIps       = 0;
  config->ipCount        = 0;
  config->allowIps6      = 0;
  config->ip6Count       = 0;
  config->recordTypes    = DEFAULT_RECORD_TYPES;

  if (!fh) {
    TSDebug(PLUGIN_NAME, "No config file, using defaults");
    return config;
  }

  while (TSfgets(fh, buffer, STR_BUFFER_SIZE - 1)) {
    if (*buffer == '#') {
      continue; /* # Comments, only at line beginning */
    }
    char *p = 0;
    if ((p = strstr(buffer, "path="))) {
      p += strlen("path=");
      if (p[0] == '/') {
        p++;
      }
      config->stats_path     = nstr(strtok_r(p, " \n", &p));
      config->stats_path_len = strlen(config->stats_path);
    } else if ((p = strstr(buffer, "record_types="))) {
      p += strlen("record_types=");
      config->recordTypes = strtol(strtok_r(p, " \n", &p), NULL, 16);
    } else if ((p = strstr(buffer, "allow_ip="))) {
      p += strlen("allow_ip=");
      parseIps(config, p);
    } else if ((p = strstr(buffer, "allow_ip6="))) {
      p += strlen("allow_ip6=");
      parseIps6(config, p);
    }
  }
  if (!config->ipCount) {
    parseIps(config, NULL);
  }
  if (!config->ip6Count) {
    parseIps6(config, NULL);
  }
  TSDebug(PLUGIN_NAME, "config path=%s", config->stats_path);

  return config;
}

static void
delete_config(config_t *config)
{
  TSDebug(PLUGIN_NAME, "Freeing config");
  TSfree(config->allowIps);
  TSfree(config->allowIps6);
  TSfree(config->stats_path);
  TSfree(config);
}

// standard api below...
static config_t *
get_config(TSCont cont)
{
  config_holder_t *configh = (config_holder_t *)TSContDataGet(cont);
  if (!configh) {
    return 0;
  }
  return configh->config;
}

static void
load_config_file(config_holder_t *config_holder)
{
  TSFile fh = NULL;
  struct stat s;

  config_t *newconfig, *oldconfig;
  TSCont free_cont;

  configReloadRequests++;
  lastReloadRequest = time(NULL);

  // check date
  if ((config_holder->config_path == NULL) || (stat(config_holder->config_path, &s) < 0)) {
    TSDebug(PLUGIN_NAME, "Could not stat %s", config_holder->config_path);
    config_holder->config_path = NULL;
    if (config_holder->config) {
      return;
    }
  } else {
    TSDebug(PLUGIN_NAME, "s.st_mtime=%lu, last_load=%lu", s.st_mtime, config_holder->last_load);
    if (s.st_mtime < config_holder->last_load) {
      return;
    }
  }

  if (config_holder->config_path != NULL) {
    TSDebug(PLUGIN_NAME, "Opening config file: %s", config_holder->config_path);
    fh = TSfopen(config_holder->config_path, "r");
  }

  if (!fh && config_holder->config_path != NULL) {
    TSError("[%s] Unable to open config: %s. Will use the param as the path, or %s if null\n", PLUGIN_NAME,
            config_holder->config_path, DEFAULT_URL_PATH);
    if (config_holder->config) {
      return;
    }
  }

  newconfig = 0;
  newconfig = new_config(fh);
  if (newconfig) {
    configReloads++;
    lastReload               = lastReloadRequest;
    config_holder->last_load = lastReloadRequest;
    config_t **confp         = &(config_holder->config);
    oldconfig                = __sync_lock_test_and_set(confp, newconfig);
    if (oldconfig) {
      TSDebug(PLUGIN_NAME, "scheduling free: %p (%p)", oldconfig, newconfig);
      free_cont = TSContCreate(free_handler, TSMutexCreate());
      TSContDataSet(free_cont, (void *)oldconfig);
      TSContScheduleOnPool(free_cont, FREE_TMOUT, TS_THREAD_POOL_TASK);
    }
  }
  if (fh) {
    TSfclose(fh);
  }
  return;
}

static config_holder_t *
new_config_holder(const char *path)
{
  config_holder_t *config_holder = TSmalloc(sizeof(config_holder_t));
  config_holder->config_path     = 0;
  config_holder->config          = 0;
  config_holder->last_load       = 0;

  if (path) {
    config_holder->config_path = nstr(path);
  } else {
    config_holder->config_path = NULL;
  }
  load_config_file(config_holder);
  return config_holder;
}

static int
free_handler(TSCont cont, TSEvent event, void *edata)
{
  config_t *config;
  config = (config_t *)TSContDataGet(cont);
  delete_config(config);
  TSContDestroy(cont);
  return 0;
}

static int
config_handler(TSCont cont, TSEvent event, void *edata)
{
  config_holder_t *config_holder;
  config_holder = (config_holder_t *)TSContDataGet(cont);
  load_config_file(config_holder);

  /* We received a reload, check if the path value was removed since it was not set after load.
     If unset, then we'll use the default */
  if (config_holder->config->stats_path == 0) {
    config_holder->config->stats_path     = nstr(DEFAULT_URL_PATH);
    config_holder->config->stats_path_len = strlen(config_holder->config->stats_path);
  }
  return 0;
}
