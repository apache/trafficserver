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

#include <arpa/inet.h>
#include <cctype>
#include <chrono>
#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <getopt.h>
#include <netinet/in.h>
#include <string>
#include <string_view>
#include <sys/stat.h>
#include <ts/ts.h>
#include <unistd.h>
#include <zlib.h>

#include <ts/remap.h>
#include "swoc/TextView.h"
#include "tscore/ink_config.h"
#include <tsutil/ts_ip.h>

#if HAVE_BROTLI_ENCODE_H
#include <brotli/encode.h>
#endif

#define PLUGIN_NAME     "stats_over_http"
#define FREE_TMOUT      300000
#define STR_BUFFER_SIZE 1024

#define SYSTEM_RECORD_TYPE   (0x100)
#define DEFAULT_RECORD_TYPES (SYSTEM_RECORD_TYPE | TS_RECORDTYPE_PROCESS | TS_RECORDTYPE_PLUGIN)

static DbgCtl dbg_ctl{PLUGIN_NAME};

static const swoc::IP4Range DEFAULT_IP{swoc::IP4Addr::MIN, swoc::IP4Addr::MAX};
static const swoc::IP6Range DEFAULT_IP6{swoc::IP6Addr::MIN, swoc::IP6Addr::MAX};

/* global holding the path used for access to this JSON data */
std::string const DEFAULT_URL_PATH = "_stats";

// from mod_deflate:
// ZLIB's compression algorithm uses a
// 0-9 based scale that GZIP does where '1' is 'Best speed'
// and '9' is 'Best compression'. Testing has proved level '6'
// to be about the best level to use in an HTTP Server.

const int   ZLIB_COMPRESSION_LEVEL = 6;
const char *dictionary             = nullptr;

// zlib stuff, see [deflateInit2] at http://www.zlib.net/manual.html
static const int ZLIB_MEMLEVEL = 9; // min=1 (optimize for memory),max=9 (optimized for speed)

static const int WINDOW_BITS_DEFLATE = 15;
static const int WINDOW_BITS_GZIP    = 16;
#define DEFLATE_MODE WINDOW_BITS_DEFLATE
#define GZIP_MODE    (WINDOW_BITS_DEFLATE | WINDOW_BITS_GZIP)

// brotli compression quality 1-11. Testing proved level '6'
#if HAVE_BROTLI_ENCODE_H
const int BROTLI_COMPRESSION_LEVEL = 6;
const int BROTLI_LGW               = 16;
#endif

static bool integer_counters = false;
static bool wrap_counters    = false;

struct config_t {
  unsigned int     recordTypes;
  std::string      stats_path;
  swoc::IPRangeSet addrs;
};
struct config_holder_t {
  char           *config_path;
  volatile time_t last_load;
  config_t       *config;
};

enum class output_format_t { JSON_OUTPUT, CSV_OUTPUT, PROMETHEUS_OUTPUT };
enum class encoding_format_t { NONE, DEFLATE, GZIP, BR };

int    configReloadRequests = 0;
int    configReloads        = 0;
time_t lastReloadRequest    = 0;
time_t lastReload           = 0;
time_t astatsLoad           = 0;

static int              free_handler(TSCont cont, TSEvent event, void *edata);
static int              config_handler(TSCont cont, TSEvent event, void *edata);
static config_t        *get_config(TSCont cont);
static config_holder_t *new_config_holder(const char *path);
static bool             is_ipmap_allowed(const config_t *config, const struct sockaddr *addr);

#if HAVE_BROTLI_ENCODE_H
struct b_stream {
  BrotliEncoderState *br        = nullptr;
  uint8_t            *next_in   = nullptr;
  size_t              avail_in  = 0;
  uint8_t            *next_out  = nullptr;
  size_t              avail_out = 0;
  size_t              total_in  = 0;
  size_t              total_out = 0;

  ~b_stream()
  {
    if (br) {
      BrotliEncoderDestroyInstance(br);
      br = nullptr;
    }
  }
};
#endif

struct stats_state {
  TSVConn net_vc    = nullptr;
  TSVIO   read_vio  = nullptr;
  TSVIO   write_vio = nullptr;

  TSIOBuffer       req_buffer  = nullptr;
  TSIOBuffer       resp_buffer = nullptr;
  TSIOBufferReader resp_reader = nullptr;

  int               output_bytes  = 0;
  int               body_written  = 0;
  output_format_t   output_format = output_format_t::JSON_OUTPUT;
  encoding_format_t encoding      = encoding_format_t::NONE;
  z_stream          zstrm;
#if HAVE_BROTLI_ENCODE_H
  b_stream bstrm;
#endif
  stats_state()
  {
    memset(&zstrm, 0, sizeof(z_stream));
    zstrm.zalloc    = Z_NULL;
    zstrm.zfree     = Z_NULL;
    zstrm.opaque    = Z_NULL;
    zstrm.data_type = Z_ASCII;
  }
};

static char *
nstr(const char *s)
{
  char *mys = (char *)TSmalloc(strlen(s) + 1);
  strcpy(mys, s);
  return mys;
}

#if HAVE_BROTLI_ENCODE_H
encoding_format_t
init_br(stats_state *my_state)
{
  my_state->bstrm.br = nullptr;

  my_state->bstrm.br = BrotliEncoderCreateInstance(nullptr, nullptr, nullptr);
  if (!my_state->bstrm.br) {
    Dbg(dbg_ctl, "Brotli Encoder Instance Failed");
    return encoding_format_t::NONE;
  }
  BrotliEncoderSetParameter(my_state->bstrm.br, BROTLI_PARAM_QUALITY, BROTLI_COMPRESSION_LEVEL);
  BrotliEncoderSetParameter(my_state->bstrm.br, BROTLI_PARAM_LGWIN, BROTLI_LGW);
  my_state->bstrm.next_in   = nullptr;
  my_state->bstrm.avail_in  = 0;
  my_state->bstrm.total_in  = 0;
  my_state->bstrm.next_out  = nullptr;
  my_state->bstrm.avail_out = 0;
  my_state->bstrm.total_out = 0;
  return encoding_format_t::BR;
}
#endif

namespace
{
inline uint64_t
ms_since_epoch()
{
  return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}
} // namespace

encoding_format_t
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
    Dbg(dbg_ctl, "gzip initialization failed");
    return encoding_format_t::NONE;
  } else {
    Dbg(dbg_ctl, "gzip initialized successfully");
    if (mode == GZIP_MODE) {
      return encoding_format_t::GZIP;
    } else if (mode == DEFLATE_MODE) {
      return encoding_format_t::DEFLATE;
    }
  }
  return encoding_format_t::NONE;
}

static void
stats_cleanup(TSCont contp, stats_state *my_state)
{
  if (my_state->req_buffer) {
    TSIOBufferDestroy(my_state->req_buffer);
    my_state->req_buffer = nullptr;
  }

  if (my_state->resp_buffer) {
    TSIOBufferDestroy(my_state->resp_buffer);
    my_state->resp_buffer = nullptr;
  }

  TSVConnClose(my_state->net_vc);
  delete my_state;
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
static const char RESP_HEADER_PROMETHEUS[] =
  "HTTP/1.0 200 OK\r\nContent-Type: text/plain; version=0.0.4; charset=utf-8\r\nCache-Control: no-cache\r\n\r\n";
static const char RESP_HEADER_PROMETHEUS_GZIP[] = "HTTP/1.0 200 OK\r\nContent-Type: text/plain; version=0.0.4; "
                                                  "charset=utf-8\r\nContent-Encoding: gzip\r\nCache-Control: no-cache\r\n\r\n";
static const char RESP_HEADER_PROMETHEUS_DEFLATE[] =
  "HTTP/1.0 200 OK\r\nContent-Type: text/plain; version=0.0.4; charset=utf-8\r\nContent-Encoding: deflate\r\nCache-Control: "
  "no-cache\r\n\r\n";
static const char RESP_HEADER_PROMETHEUS_BR[] = "HTTP/1.0 200 OK\r\nContent-Type: text/plain; version=0.0.4; "
                                                "charset=utf-8\r\nContent-Encoding: br\r\nCache-Control: no-cache\r\n\r\n";

static int
stats_add_resp_header(stats_state *my_state)
{
  switch (my_state->output_format) {
  case output_format_t::JSON_OUTPUT:
    if (my_state->encoding == encoding_format_t::GZIP) {
      return stats_add_data_to_resp_buffer(RESP_HEADER_JSON_GZIP, my_state);
    } else if (my_state->encoding == encoding_format_t::DEFLATE) {
      return stats_add_data_to_resp_buffer(RESP_HEADER_JSON_DEFLATE, my_state);
    } else if (my_state->encoding == encoding_format_t::BR) {
      return stats_add_data_to_resp_buffer(RESP_HEADER_JSON_BR, my_state);
    } else {
      return stats_add_data_to_resp_buffer(RESP_HEADER_JSON, my_state);
    }
    break;
  case output_format_t::CSV_OUTPUT:
    if (my_state->encoding == encoding_format_t::GZIP) {
      return stats_add_data_to_resp_buffer(RESP_HEADER_CSV_GZIP, my_state);
    } else if (my_state->encoding == encoding_format_t::DEFLATE) {
      return stats_add_data_to_resp_buffer(RESP_HEADER_CSV_DEFLATE, my_state);
    } else if (my_state->encoding == encoding_format_t::BR) {
      return stats_add_data_to_resp_buffer(RESP_HEADER_CSV_BR, my_state);
    } else {
      return stats_add_data_to_resp_buffer(RESP_HEADER_CSV, my_state);
    }
    break;
  case output_format_t::PROMETHEUS_OUTPUT:
    if (my_state->encoding == encoding_format_t::GZIP) {
      return stats_add_data_to_resp_buffer(RESP_HEADER_PROMETHEUS_GZIP, my_state);
    } else if (my_state->encoding == encoding_format_t::DEFLATE) {
      return stats_add_data_to_resp_buffer(RESP_HEADER_PROMETHEUS_DEFLATE, my_state);
    } else if (my_state->encoding == encoding_format_t::BR) {
      return stats_add_data_to_resp_buffer(RESP_HEADER_PROMETHEUS_BR, my_state);
    } else {
      return stats_add_data_to_resp_buffer(RESP_HEADER_PROMETHEUS, my_state);
    }
    break;
  }
  // Not reached.
  return stats_add_data_to_resp_buffer(RESP_HEADER_JSON, my_state);
}

static void
stats_process_read(TSCont contp, TSEvent event, stats_state *my_state)
{
  Dbg(dbg_ctl, "stats_process_read(%d)", event);
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

//-----------------------------------------------------------------------------
// JSON Formatters
//-----------------------------------------------------------------------------
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

//-----------------------------------------------------------------------------
// CSV Formatters
//-----------------------------------------------------------------------------
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

//-----------------------------------------------------------------------------
// Prometheus Formatters
//-----------------------------------------------------------------------------
// Note that Prometheus only supports numeric types.
#define APPEND_STAT_PROMETHEUS_NUMERIC(a, fmt, v)                        \
  do {                                                                   \
    char b[256];                                                         \
    if (snprintf(b, sizeof(b), "%s " fmt "\n", a, v) < (int)sizeof(b)) { \
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
json_out_stat(TSRecordType /* rec_type ATS_UNUSED */, void *edata, int /* registered ATS_UNUSED */, const char *name,
              TSRecordDataType data_type, TSRecordData *datum)
{
  stats_state *my_state = static_cast<stats_state *>(edata);

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
    Dbg(dbg_ctl, "unknown type for %s: %d", name, data_type);
    break;
  }
}

static void
csv_out_stat(TSRecordType /* rec_type ATS_UNUSED */, void *edata, int /* registered ATS_UNUSED */, const char *name,
             TSRecordDataType data_type, TSRecordData *datum)
{
  stats_state *my_state = static_cast<stats_state *>(edata);
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
    Dbg(dbg_ctl, "unknown type for %s: %d", name, data_type);
    break;
  }
}

/** Replace characters offensive to Prometheus with '_'.
 *
 * See: https://prometheus.io/docs/concepts/data_model/#metric-names-and-labels
 *
 *  > Metric names SHOULD match the regex [a-zA-Z_:][a-zA-Z0-9_:]*
 *  > for the best experience and compatibility
 *
 * @param[in] name The metric name to sanitize.
 * @return A sanitized metric name.
 */
static
// Remove this check when we drop support for pre-13 GCC versions.
#if defined(__cpp_lib_constexpr_string) && __cpp_lib_constexpr_string >= 201907L
  constexpr
#endif
  std::string
  sanitize_metric_name_for_prometheus(std::string_view name)
{
  std::string sanitized_name(name);
  // If the first character is a digit, prepend an underscore since Prometheus
  // doesn't allow digits as the first character.
  if (sanitized_name.length() > 0 && sanitized_name[0] >= '0' && sanitized_name[0] <= '9') {
    sanitized_name = "_" + sanitized_name;
  }
  // Convert characters that Prometheus doesn't like to '_'.
  // : letters (a-z, A-Z), digits (0-9), underscores (_), and colons (:).
  for (auto &c : sanitized_name) {
    if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == ':')) {
      c = '_';
    }
  }
  return sanitized_name;
}

static void
prometheus_out_stat(TSRecordType /* rec_type ATS_UNUSED */, void *edata, int /* registered ATS_UNUSED */, const char *name,
                    TSRecordDataType data_type, TSRecordData *datum)
{
  stats_state *my_state       = static_cast<stats_state *>(edata);
  std::string  sanitized_name = sanitize_metric_name_for_prometheus(name);
  char         type_buffer[256];
  char         help_buffer[256];

  snprintf(help_buffer, sizeof(help_buffer), "# HELP %s %s\n", sanitized_name.c_str(), name);
  switch (data_type) {
  case TS_RECORDDATATYPE_COUNTER:
    APPEND(help_buffer);
    snprintf(type_buffer, sizeof(type_buffer), "# TYPE %s counter\n", sanitized_name.c_str());
    APPEND(type_buffer);
    APPEND_STAT_PROMETHEUS_NUMERIC(sanitized_name.c_str(), "%" PRIu64, wrap_unsigned_counter(datum->rec_counter));
    break;
  case TS_RECORDDATATYPE_INT:
    APPEND(help_buffer);
    snprintf(type_buffer, sizeof(type_buffer), "# TYPE %s gauge\n", sanitized_name.c_str());
    APPEND(type_buffer);
    APPEND_STAT_PROMETHEUS_NUMERIC(sanitized_name.c_str(), "%" PRIu64, wrap_unsigned_counter(datum->rec_int));
    break;
  case TS_RECORDDATATYPE_FLOAT:
    APPEND(help_buffer);
    APPEND_STAT_PROMETHEUS_NUMERIC(sanitized_name.c_str(), "%f", datum->rec_float);
    break;
  case TS_RECORDDATATYPE_STRING:
    Dbg(dbg_ctl, "Prometheus does not support string values, skipping: %s", sanitized_name.c_str());
    break;
  default:
    Dbg(dbg_ctl, "unknown type for %s: %d", sanitized_name.c_str(), data_type);
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
  APPEND_STAT_JSON_NUMERIC("current_time_epoch_ms", "%" PRIu64, ms_since_epoch());
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
  size_t  outputsize = BrotliEncoderMaxCompressedSize(my_state->output_bytes);
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
    Dbg(dbg_ctl, "brotli compress error");
  }
  my_state->output_bytes += TSIOBufferWrite(my_state->resp_buffer, outputbuf, outputsize);
  BrotliEncoderDestroyInstance(my_state->bstrm.br);
  my_state->bstrm.br = nullptr;
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

  my_state->output_bytes    -= toconsume;
  my_state->zstrm.avail_in   = inputbytes;
  my_state->zstrm.avail_out  = sizeof(outputbuf);
  my_state->zstrm.next_in    = (Bytef *)inputbuf;
  my_state->zstrm.next_out   = (Bytef *)outputbuf;
  int err                    = deflate(&my_state->zstrm, Z_FINISH);
  if (err != Z_STREAM_END) {
    Dbg(dbg_ctl, "deflate error: %d", err);
  }

  err = deflateEnd(&my_state->zstrm);
  if (err != Z_OK) {
    Dbg(dbg_ctl, "deflate end err: %d", err);
  }

  my_state->output_bytes += TSIOBufferWrite(my_state->resp_buffer, outputbuf, my_state->zstrm.total_out);
}

static void
csv_out_stats(stats_state *my_state)
{
  TSRecordDump((TSRecordType)(TS_RECORDTYPE_PLUGIN | TS_RECORDTYPE_NODE | TS_RECORDTYPE_PROCESS), csv_out_stat, my_state);
  const char *version = TSTrafficServerVersionGet();
  APPEND_STAT_CSV_NUMERIC("current_time_epoch_ms", "%" PRIu64, ms_since_epoch());
  APPEND_STAT_CSV("version", "%s", version);
}

static void
prometheus_out_stats(stats_state *my_state)
{
  TSRecordDump((TSRecordType)(TS_RECORDTYPE_PLUGIN | TS_RECORDTYPE_NODE | TS_RECORDTYPE_PROCESS), prometheus_out_stat, my_state);
  APPEND_STAT_PROMETHEUS_NUMERIC("current_time_epoch_ms", "%" PRIu64, ms_since_epoch());
  // No version printed, since string stats are not supported by Prometheus.
}

static void
stats_process_write(TSCont contp, TSEvent event, stats_state *my_state)
{
  if (event == TS_EVENT_VCONN_WRITE_READY) {
    if (my_state->body_written == 0) {
      my_state->body_written = 1;
      switch (my_state->output_format) {
      case output_format_t::JSON_OUTPUT:
        json_out_stats(my_state);
        break;
      case output_format_t::CSV_OUTPUT:
        csv_out_stats(my_state);
        break;
      case output_format_t::PROMETHEUS_OUTPUT:
        prometheus_out_stats(my_state);
        break;
      }

      if ((my_state->encoding == encoding_format_t::GZIP) || (my_state->encoding == encoding_format_t::DEFLATE)) {
        gzip_out_stats(my_state);
      }
#if HAVE_BROTLI_ENCODE_H
      else if (my_state->encoding == encoding_format_t::BR) {
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
  stats_state *my_state = static_cast<stats_state *>(TSContDataGet(contp));
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
stats_origin(TSCont contp, TSEvent /* event ATS_UNUSED */, void *edata)
{
  TSCont          icontp;
  stats_state    *my_state;
  config_t       *config;
  TSHttpTxn       txnp = (TSHttpTxn)edata;
  TSMBuffer       reqp;
  TSMLoc          hdr_loc = nullptr, url_loc = nullptr, accept_field = nullptr, accept_encoding_field = nullptr;
  TSEvent         reenable = TS_EVENT_HTTP_CONTINUE;
  int             path_len = 0;
  const char     *path     = nullptr;
  swoc::TextView  request_path;
  swoc::TextView  request_path_suffix;
  output_format_t format_per_path          = output_format_t::JSON_OUTPUT;
  bool            path_had_explicit_format = false;

  Dbg(dbg_ctl, "in the read stuff");
  config = get_config(contp);

  if (TSHttpTxnClientReqGet(txnp, &reqp, &hdr_loc) != TS_SUCCESS) {
    goto cleanup;
  }

  if (TSHttpHdrUrlGet(reqp, hdr_loc, &url_loc) != TS_SUCCESS) {
    goto cleanup;
  }

  path = TSUrlPathGet(reqp, url_loc, &path_len);
  Dbg(dbg_ctl, "Path: %.*s", path_len, path);

  if (path_len == 0) {
    Dbg(dbg_ctl, "Empty path");
    goto notforme;
  }

  request_path = swoc::TextView{path, static_cast<size_t>(path_len)};
  if (!request_path.starts_with(config->stats_path)) {
    Dbg(dbg_ctl, "Not the configured path for stats: %.*s, expected: %s", path_len, path, config->stats_path.c_str());
    goto notforme;
  }

  if (request_path == config->stats_path) {
    Dbg(dbg_ctl, "Exact match for stats path: %s", config->stats_path.c_str());
    format_per_path          = output_format_t::JSON_OUTPUT;
    path_had_explicit_format = false;
  } else {
    request_path_suffix = request_path.remove_prefix(config->stats_path.length());
    if (request_path_suffix == "/json") {
      format_per_path = output_format_t::JSON_OUTPUT;
    } else if (request_path_suffix == "/csv") {
      format_per_path = output_format_t::CSV_OUTPUT;
    } else if (request_path_suffix == "/prometheus") {
      format_per_path = output_format_t::PROMETHEUS_OUTPUT;
    } else {
      Dbg(dbg_ctl, "Unknown suffix for stats path: %.*s", static_cast<int>(request_path_suffix.length()),
          request_path_suffix.data());
      goto notforme;
    }
    path_had_explicit_format = true;
  }

  if (auto addr = TSHttpTxnClientAddrGet(txnp); !is_ipmap_allowed(config, addr)) {
    Dbg(dbg_ctl, "not right ip");
    TSHttpTxnStatusSet(txnp, TS_HTTP_STATUS_FORBIDDEN);
    reenable = TS_EVENT_HTTP_ERROR;
    goto notforme;
  }

  TSHttpTxnCntlSet(txnp, TS_HTTP_CNTL_SKIP_REMAPPING, true); // not strictly necessary, but speed is everything these days

  /* This is us -- register our intercept */
  Dbg(dbg_ctl, "Intercepting request");

  my_state = new stats_state;
  icontp   = TSContCreate(stats_dostuff, TSMutexCreate());

  if (path_had_explicit_format) {
    Dbg(dbg_ctl, "Path had explicit format, ignoring any Accept header: %s", request_path_suffix.data());
    my_state->output_format = format_per_path;
  } else {
    // Check for an Accept header to determine response type.
    accept_field            = TSMimeHdrFieldFind(reqp, hdr_loc, TS_MIME_FIELD_ACCEPT, TS_MIME_LEN_ACCEPT);
    my_state->output_format = output_format_t::JSON_OUTPUT; // default to json output
    // accept header exists, use it to determine response type
    if (accept_field != TS_NULL_MLOC) {
      int         len = -1;
      const char *str = TSMimeHdrFieldValueStringGet(reqp, hdr_loc, accept_field, -1, &len);

      // Parse the Accept header, default to JSON output unless its another supported format
      if (!strncasecmp(str, "text/csv", len)) {
        Dbg(dbg_ctl, "Saw text/csv in accept header, sending CSV output.");
        my_state->output_format = output_format_t::CSV_OUTPUT;
      } else if (!strncasecmp(str, "text/plain; version=0.0.4", len)) {
        Dbg(dbg_ctl, "Saw text/plain; version=0.0.4 in accept header, sending Prometheus output.");
        my_state->output_format = output_format_t::PROMETHEUS_OUTPUT;
      } else {
        Dbg(dbg_ctl, "Saw %.*s in accept header, defaulting to JSON output.", len, str);
        my_state->output_format = output_format_t::JSON_OUTPUT;
      }
    }
  }

  // Check for Accept Encoding and init
  accept_encoding_field = TSMimeHdrFieldFind(reqp, hdr_loc, TS_MIME_FIELD_ACCEPT_ENCODING, TS_MIME_LEN_ACCEPT_ENCODING);
  my_state->encoding    = encoding_format_t::NONE;
  if (accept_encoding_field != TS_NULL_MLOC) {
    int         len = -1;
    const char *str = TSMimeHdrFieldValueStringGet(reqp, hdr_loc, accept_encoding_field, -1, &len);
    if (len >= TS_HTTP_LEN_DEFLATE && strstr(str, TS_HTTP_VALUE_DEFLATE) != nullptr) {
      Dbg(dbg_ctl, "Saw deflate in accept encoding");
      my_state->encoding = init_gzip(my_state, DEFLATE_MODE);
    } else if (len >= TS_HTTP_LEN_GZIP && strstr(str, TS_HTTP_VALUE_GZIP) != nullptr) {
      Dbg(dbg_ctl, "Saw gzip in accept encoding");
      my_state->encoding = init_gzip(my_state, GZIP_MODE);
    }
#if HAVE_BROTLI_ENCODE_H
    else if (len >= TS_HTTP_LEN_BROTLI && strstr(str, TS_HTTP_VALUE_BROTLI) != nullptr) {
      Dbg(dbg_ctl, "Saw br in accept encoding");
      my_state->encoding = init_br(my_state);
    }
#endif
    else {
      my_state->encoding = encoding_format_t::NONE;
    }
  }
  Dbg(dbg_ctl, "Finished AE check");

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

  static const char          usage[]    = PLUGIN_NAME ".so [--integer-counters] [PATH]";
  static const struct option longopts[] = {
    {(char *)("integer-counters"), no_argument, nullptr, 'i'},
    {(char *)("wrap-counters"),    no_argument, nullptr, 'w'},
    {nullptr,                      0,           nullptr, 0  }
  };
  TSCont           main_cont, config_cont;
  config_holder_t *config_holder;

  info.plugin_name   = PLUGIN_NAME;
  info.vendor_name   = "Apache Software Foundation";
  info.support_email = "dev@trafficserver.apache.org";

  if (TSPluginRegister(&info) != TS_SUCCESS) {
    TSError("[%s] registration failed", PLUGIN_NAME);
    goto done;
  }

  for (;;) {
    switch (getopt_long(argc, (char *const *)argv, "iw", longopts, nullptr)) {
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

  config_holder = new_config_holder(argc > 0 ? argv[0] : nullptr);

  /* Path was not set during load, so the param was not a config file, we also
    have an argument so it must be the path, set it here.  Otherwise if no argument
    then use the default _stats path */
  if ((config_holder->config != nullptr) && (config_holder->config->stats_path.empty()) && (argc > 0) &&
      (config_holder->config_path == nullptr)) {
    config_holder->config->stats_path = argv[0] + ('/' == argv[0][0] ? 1 : 0);
  } else if ((config_holder->config != nullptr) && (config_holder->config->stats_path.empty())) {
    config_holder->config->stats_path = DEFAULT_URL_PATH;
  }

  /* Create a continuation with a mutex as there is a shared global structure
     containing the headers to add */
  main_cont = TSContCreate(stats_origin, nullptr);
  TSContDataSet(main_cont, (void *)config_holder);
  TSHttpHookAdd(TS_HTTP_READ_REQUEST_HDR_HOOK, main_cont);

  /* Create continuation for management updates to re-read config file */
  if (config_holder->config_path != nullptr) {
    config_cont = TSContCreate(config_handler, TSMutexCreate());
    TSContDataSet(config_cont, (void *)config_holder);
    TSMgmtUpdateRegister(config_cont, PLUGIN_NAME);
  }

  if (config_holder->config != nullptr) {
    Dbg(dbg_ctl, "stats module registered with path %s", config_holder->config->stats_path.c_str());
  }

done:
  return;
}

static bool
is_ipmap_allowed(const config_t *config, const struct sockaddr *addr)
{
  if (!addr) {
    return true;
  }

  if (config->addrs.contains(swoc::IPAddr(addr))) {
    return true;
  }

  return false;
}
static void
parseIpMap(config_t *config, swoc::TextView txt)
{
  // sent null ipstring, fill with default open IPs
  if (txt.empty()) {
    config->addrs.fill(DEFAULT_IP6);
    config->addrs.fill(DEFAULT_IP);
    Dbg(dbg_ctl, "Empty allow settings, setting all IPs in allow list");
    return;
  }

  while (txt) {
    auto token{txt.take_prefix_at(',')};
    if (swoc::IPRange r; r.load(token)) {
      config->addrs.fill(r);
      Dbg(dbg_ctl, "Added %.*s to allow ip list", int(token.length()), token.data());
    }
  }
}

static config_t *
new_config(std::fstream &fh)
{
  config_t *config    = nullptr;
  config              = new config_t;
  config->recordTypes = DEFAULT_RECORD_TYPES;
  config->stats_path  = "";
  std::string cur_line;

  if (!fh) {
    Dbg(dbg_ctl, "No config file, using defaults");
    return config;
  }

  while (std::getline(fh, cur_line)) {
    swoc::TextView line{cur_line};
    if (line.ltrim_if(&isspace).empty() || '#' == *line) {
      continue; /* # Comments, only at line beginning */
    }

    size_t p = 0;

    static constexpr swoc::TextView PATH_TAG   = "path=";
    static constexpr swoc::TextView RECORD_TAG = "record_types=";
    static constexpr swoc::TextView ADDR_TAG   = "allow_ip=";
    static constexpr swoc::TextView ADDR6_TAG  = "allow_ip6=";

    if ((p = line.find(PATH_TAG)) != std::string::npos) {
      line.remove_prefix(p + PATH_TAG.size()).ltrim('/');
      Dbg(dbg_ctl, "parsing path");
      config->stats_path = line;
    } else if ((p = line.find(RECORD_TAG)) != std::string::npos) {
      Dbg(dbg_ctl, "parsing record types");
      line.remove_prefix(p).remove_prefix(RECORD_TAG.size());
      config->recordTypes = swoc::svtou(line, nullptr, 16);
    } else if ((p = line.find(ADDR_TAG)) != std::string::npos) {
      parseIpMap(config, line.remove_prefix(p).remove_prefix(ADDR_TAG.size()));
    } else if ((p = line.find(ADDR6_TAG)) != std::string::npos) {
      parseIpMap(config, line.remove_prefix(p).remove_prefix(ADDR6_TAG.size()));
    }
  }

  if (config->addrs.count() == 0) {
    Dbg(dbg_ctl, "empty ip map found, setting defaults");
    parseIpMap(config, nullptr);
  }

  Dbg(dbg_ctl, "config path=%s", config->stats_path.c_str());

  return config;
}

static void
delete_config(config_t *config)
{
  Dbg(dbg_ctl, "Freeing config");
  delete config;
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
  std::fstream fh;
  struct stat  s;

  config_t *newconfig, *oldconfig;
  TSCont    free_cont;

  configReloadRequests++;
  lastReloadRequest = time(nullptr);

  // check date
  if ((config_holder->config_path == nullptr) || (stat(config_holder->config_path, &s) < 0)) {
    Dbg(dbg_ctl, "Could not stat %s", config_holder->config_path);
    config_holder->config_path = nullptr;
    if (config_holder->config) {
      return;
    }
  } else {
    Dbg(dbg_ctl, "s.st_mtime=%lu, last_load=%lu", s.st_mtime, config_holder->last_load);
    if (s.st_mtime < config_holder->last_load) {
      return;
    }
  }

  if (config_holder->config_path != nullptr) {
    Dbg(dbg_ctl, "Opening config file: %s", config_holder->config_path);
    fh.open(config_holder->config_path, std::ios::in);
  }

  if (!fh.is_open() && config_holder->config_path != nullptr) {
    TSError("[%s] Unable to open config: %s. Will use the param as the path, or %s if null\n", PLUGIN_NAME,
            config_holder->config_path, DEFAULT_URL_PATH.c_str());
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
      Dbg(dbg_ctl, "scheduling free: %p (%p)", oldconfig, newconfig);
      free_cont = TSContCreate(free_handler, TSMutexCreate());
      TSContDataSet(free_cont, (void *)oldconfig);
      TSContScheduleOnPool(free_cont, FREE_TMOUT, TS_THREAD_POOL_TASK);
    }
  }
  if (fh) {
    fh.close();
  }
  return;
}

static config_holder_t *
new_config_holder(const char *path)
{
  config_holder_t *config_holder = static_cast<config_holder_t *>(TSmalloc(sizeof(config_holder_t)));
  config_holder->config_path     = 0;
  config_holder->config          = 0;
  config_holder->last_load       = 0;

  if (path) {
    config_holder->config_path = nstr(path);
  } else {
    config_holder->config_path = nullptr;
  }
  load_config_file(config_holder);
  return config_holder;
}

static int
free_handler(TSCont cont, TSEvent /* event ATS_UNUSED */, void * /* edata ATS_UNUSED */)
{
  config_t *config;
  config = (config_t *)TSContDataGet(cont);
  delete_config(config);
  TSContDestroy(cont);
  return 0;
}

static int
config_handler(TSCont cont, TSEvent /* event ATS_UNUSED */, void * /* edata ATS_UNUSED */)
{
  config_holder_t *config_holder;
  config_holder = (config_holder_t *)TSContDataGet(cont);
  load_config_file(config_holder);

  /* We received a reload, check if the path value was removed since it was not set after load.
     If unset, then we'll use the default */
  if (config_holder->config->stats_path == "") {
    config_holder->config->stats_path = DEFAULT_URL_PATH;
  }
  return 0;
}

//
// Compilation time unit tests.
//
#ifdef DEBUG
// Remove this check when we drop support for pre-13 GCC versions.
#if defined(__cpp_lib_constexpr_string) && __cpp_lib_constexpr_string >= 201907L
constexpr void
test_sanitize_metric_name_for_prometheus()
{
  // Various unchanged names.
  static_assert(sanitize_metric_name_for_prometheus("foo") == "foo");
  static_assert(sanitize_metric_name_for_prometheus("foo_bar") == "foo_bar");
  static_assert(sanitize_metric_name_for_prometheus("foo_bar:baz") == "foo_bar:baz");
  static_assert(sanitize_metric_name_for_prometheus("FooBar123") == "FooBar123");
  static_assert(sanitize_metric_name_for_prometheus("UPPERCASE_NAME") == "UPPERCASE_NAME");
  static_assert(sanitize_metric_name_for_prometheus("lowercase_name") == "lowercase_name");
  static_assert(sanitize_metric_name_for_prometheus("Mixed_Case_123") == "Mixed_Case_123");

  // Test dots conversion (common in ATS metrics).
  static_assert(sanitize_metric_name_for_prometheus("foo.bar") == "foo_bar");
  static_assert(sanitize_metric_name_for_prometheus("proxy.process.allocator.inuse") == "proxy_process_allocator_inuse");

  // Various invalid characters.
  static_assert(sanitize_metric_name_for_prometheus("foo-bar") == "foo_bar");
  static_assert(sanitize_metric_name_for_prometheus("foo+bar") == "foo_bar");
  static_assert(sanitize_metric_name_for_prometheus("foo@bar") == "foo_bar");
  static_assert(sanitize_metric_name_for_prometheus("foo#bar") == "foo_bar");
  static_assert(sanitize_metric_name_for_prometheus("foo$bar") == "foo_bar");
  static_assert(sanitize_metric_name_for_prometheus("foo%bar") == "foo_bar");
  static_assert(sanitize_metric_name_for_prometheus("foo^bar") == "foo_bar");
  static_assert(sanitize_metric_name_for_prometheus("foo&bar") == "foo_bar");
  static_assert(sanitize_metric_name_for_prometheus("foo*bar") == "foo_bar");
  static_assert(sanitize_metric_name_for_prometheus("foo(bar)") == "foo_bar_");
  static_assert(sanitize_metric_name_for_prometheus("foo=bar") == "foo_bar");
  static_assert(sanitize_metric_name_for_prometheus("foo|bar") == "foo_bar");
  static_assert(sanitize_metric_name_for_prometheus("foo\\bar") == "foo_bar");
  static_assert(sanitize_metric_name_for_prometheus("foo/bar") == "foo_bar");
  static_assert(sanitize_metric_name_for_prometheus("foo?bar") == "foo_bar");
  static_assert(sanitize_metric_name_for_prometheus("foo<bar>") == "foo_bar_");
  static_assert(sanitize_metric_name_for_prometheus("foo,bar;baz") == "foo_bar_baz");
  static_assert(sanitize_metric_name_for_prometheus("foo\"bar'baz") == "foo_bar_baz");
  static_assert(sanitize_metric_name_for_prometheus("foo`bar~baz") == "foo_bar_baz");
  static_assert(sanitize_metric_name_for_prometheus("foo!bar") == "foo_bar");
  static_assert(sanitize_metric_name_for_prometheus("foo_bar[baz]") == "foo_bar_baz_");
  static_assert(sanitize_metric_name_for_prometheus("proxy.process.allocator.inuse.ioBufAllocator[0]") ==
                "proxy_process_allocator_inuse_ioBufAllocator_0_");

  // Whitespace and control characters.
  static_assert(sanitize_metric_name_for_prometheus("foo bar") == "foo_bar");
  static_assert(sanitize_metric_name_for_prometheus("foo\tbar") == "foo_bar");
  static_assert(sanitize_metric_name_for_prometheus("foo\nbar") == "foo_bar");
  static_assert(sanitize_metric_name_for_prometheus("foo\rbar") == "foo_bar");

  // Initial digit variations.
  static_assert(sanitize_metric_name_for_prometheus("0foo") == "_0foo");
  static_assert(sanitize_metric_name_for_prometheus("1.proxy.process.allocator.inuse.ioBufAllocator[0]") ==
                "_1_proxy_process_allocator_inuse_ioBufAllocator_0_");

  // Complex combinations.
  static_assert(sanitize_metric_name_for_prometheus("proxy.process.http.connection_errors[500].rate") ==
                "proxy_process_http_connection_errors_500__rate");
  static_assert(sanitize_metric_name_for_prometheus("cache.hit_ratio[0-5min]") == "cache_hit_ratio_0_5min_");
  static_assert(sanitize_metric_name_for_prometheus("worker.thread[0].cpu.usage%") == "worker_thread_0__cpu_usage_");
  static_assert(sanitize_metric_name_for_prometheus("1st.metric.name-with+special@chars") == "_1st_metric_name_with_special_chars");

  // Minimal edge cases.
  static_assert(sanitize_metric_name_for_prometheus("") == "");
  static_assert(sanitize_metric_name_for_prometheus("a") == "a");
  static_assert(sanitize_metric_name_for_prometheus(".") == "_");
  static_assert(sanitize_metric_name_for_prometheus("1") == "_1");

  // Edge cases with multiple consecutive invalid characters.
  static_assert(sanitize_metric_name_for_prometheus("foo...bar") == "foo___bar");
  static_assert(sanitize_metric_name_for_prometheus("123foo---bar") == "_123foo___bar");
  static_assert(sanitize_metric_name_for_prometheus("foo [[[bar]]]") == "foo____bar___");
  static_assert(sanitize_metric_name_for_prometheus("foo@#$%bar") == "foo____bar");
}
#endif // defined(__cpp_lib_constexpr_string) && __cpp_lib_constexpr_string >= 201907L
#endif // DEBUG
