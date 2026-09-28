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

#include <algorithm>
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
#include <memory>
#include <mutex>
#include <netinet/in.h>
#include <optional>
#include <string>
#include <string_view>
#include <sys/stat.h>
#include <ts/ts.h>
#include <utility>
#include <vector>
#include <unistd.h>
#include <zlib.h>

#include <ts/remap.h>
#include <yaml-cpp/yaml.h>
#include "prometheus_render.h"
#include "prometheus_rules.h"
#include "swoc/TextView.h"
#include "tscore/ink_config.h"
#include <tsutil/Metrics.h>
#include <tsutil/ts_ip.h>
#include <tsutil/StringCompare.h>

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
static const int ZLIB_MEMLEVEL = 8; // zlib's default

static const int WINDOW_BITS_DEFLATE = 15;
static const int WINDOW_BITS_GZIP    = 16;
#define DEFLATE_MODE WINDOW_BITS_DEFLATE
#define GZIP_MODE    (WINDOW_BITS_DEFLATE | WINDOW_BITS_GZIP)

// brotli compression quality 1-11. Testing proved level '6'
#if HAVE_BROTLI_ENCODE_H
const int BROTLI_COMPRESSION_LEVEL = 6;
const int BROTLI_LGW               = 16;
#endif

#if defined(__cpp_lib_constexpr_string) && __cpp_lib_constexpr_string >= 201907L && (!defined(__clang__) || __clang_major__ > 16)
#define STATS_OVER_HTTP_HAS_CONSTEXPR_STRING 1
#else
#define STATS_OVER_HTTP_HAS_CONSTEXPR_STRING 0
#endif

struct prometheus_v2_metric {
  std::string name;
  std::string labels;
};

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

enum class output_format_t { JSON_OUTPUT, CSV_OUTPUT, PROMETHEUS_OUTPUT, PROMETHEUS_V2_OUTPUT };
enum class encoding_format_t { NONE, DEFLATE, GZIP, BR };

constexpr size_t FORMAT_COUNT   = 4;
constexpr size_t ENCODING_COUNT = 4;

// The options of one remap rule, or of the global plugin.
struct stats_options {
  output_format_t format           = output_format_t::JSON_OUTPUT;
  bool            integer_counters = false;
  bool            wrap_counters    = false;
  bool            prometheus_help  = true;
  int64_t         max_age_ms       = 1000;
  int64_t         wait_timeout_ms  = 10000;
  // The prometheus section of the configuration file of a remap rule, or null.
  std::shared_ptr<const PrometheusRules> rules;
  // The configuration file of the remap rule has an error, so each request gets a 503.
  bool config_error = false;
  // The Prometheus output ends with a current_time_epoch_ms sample.
  bool prometheus_epoch = true;
};

// The metrics of the plugin itself.
struct stats_metrics {
  using counter = ts::Metrics::Counter::AtomicType;

  counter *requests              = create("requests");
  counter *renders               = create("renders");
  counter *render_us             = create("render_us");
  counter *intercept_us          = create("intercept_us");
  counter *series                = create("series");
  counter *series_dropped        = create("series_dropped");
  counter *series_relabeled      = create("series_relabeled");
  counter *series_duplicates     = create("series_duplicates");
  counter *series_type_conflicts = create("series_type_conflicts");
  counter *waiter_timeouts       = create("waiter_timeouts");
  counter *config_errors         = create("config_errors");
  counter *bytes_out             = create("bytes_out");

  static counter *
  create(std::string_view name)
  {
    return ts::Metrics::Counter::createPtr("plugin.stats_over_http.", name);
  }
};

// createPtr returns the existing metric for a name, so each copy of the plugin that a remap reload loads uses the same metrics.
static const stats_metrics &
metrics()
{
  static const stats_metrics instance;

  return instance;
}

static void
count(ts::Metrics::Counter::AtomicType *metric, uint64_t value = 1)
{
  ts::Metrics::Counter::increment(metric, value);
}

static int64_t
thread_cpu_ns()
{
  timespec now;

  clock_gettime(CLOCK_THREAD_CPUTIME_ID, &now);
  return static_cast<int64_t>(now.tv_sec) * 1000000000 + now.tv_nsec;
}

// @a rest carries the nanoseconds that do not make a whole microsecond to the next call on this thread.
static void
count_cpu_time(ts::Metrics::Counter::AtomicType *metric, int64_t start, int64_t &rest)
{
  rest += thread_cpu_ns() - start;
  if (rest >= 1000) {
    count(metric, rest / 1000);
    rest %= 1000;
  }
}

struct stats_instance;

// The instance of the global plugin, which lives as long as the process.
static std::shared_ptr<stats_instance> *global_instance = nullptr;

static std::shared_ptr<stats_instance> make_stats_instance(const stats_options &options);
static void                            serve_global_scrape(TSHttpTxn txnp, output_format_t format, encoding_format_t encoding);
static bool                            parse_format(std::string_view name, output_format_t &format);

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

struct render_state {
  TSIOBuffer           resp_buffer = nullptr;
  const stats_options *options     = nullptr;
};

static char *
nstr(const char *s)
{
  if (s == nullptr) {
    return nullptr;
  }
  char *mys = (char *)TSmalloc(strlen(s) + 1);
  strcpy(mys, s);
  return mys;
}

// The bound keeps the conversion of an option to nanoseconds from overflowing.
constexpr int64_t MAX_MILLISECONDS = 24 * 60 * 60 * 1000;

// Parses a decimal integer from @a min to @a max.
static bool
parse_integer(std::string_view arg, int64_t min, int64_t max, int64_t &value)
{
  swoc::TextView parsed;
  auto const     number = swoc::svtoi(arg, &parsed, 10);

  if (arg.empty() || parsed.size() != arg.size() || number < min || number > max) {
    return false;
  }
  value = number;
  return true;
}

namespace
{
inline uint64_t
ms_since_epoch()
{
  return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}
} // namespace

static void
stats_add_data_to_resp_buffer(const char *s, render_state *my_state)
{
  if (s != nullptr) {
    TSIOBufferWrite(my_state->resp_buffer, s, strlen(s));
  }
}

static const char RESP_HEADER_UNAVAILABLE[] = "HTTP/1.0 503 Service Unavailable\r\nCache-Control: no-cache\r\n\r\n";

#define APPEND(a) stats_add_data_to_resp_buffer(a, my_state)

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
    if (my_state->options->integer_counters) {                                       \
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

// This wraps uint64_t values to the int64_t range to fit into a Java long. Java 8 has an unsigned long which
// can interoperate with a full uint64_t, but it's unlikely that much of the ecosystem supports that yet.
static uint64_t
wrap_unsigned_counter(const render_state *my_state, uint64_t value)
{
  if (my_state->options->wrap_counters) {
    return (value > INT64_MAX) ? value % INT64_MAX : value;
  } else {
    return value;
  }
}

static void
json_out_stat(TSRecordType /* rec_type ATS_UNUSED */, void *edata, int /* registered ATS_UNUSED */, const char *name,
              TSRecordDataType data_type, TSRecordData *datum)
{
  render_state *my_state = static_cast<render_state *>(edata);

  switch (data_type) {
  case TS_RECORDDATATYPE_COUNTER:
    APPEND_STAT_JSON_NUMERIC(name, "%" PRIu64, wrap_unsigned_counter(my_state, datum->rec_counter));
    break;
  case TS_RECORDDATATYPE_INT:
    APPEND_STAT_JSON_NUMERIC(name, "%" PRId64, datum->rec_int);
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
  render_state *my_state = static_cast<render_state *>(edata);
  switch (data_type) {
  case TS_RECORDDATATYPE_COUNTER:
    APPEND_STAT_CSV_NUMERIC(name, "%" PRIu64, wrap_unsigned_counter(my_state, datum->rec_counter));
    break;
  case TS_RECORDDATATYPE_INT:
    APPEND_STAT_CSV_NUMERIC(name, "%" PRId64, datum->rec_int);
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
#if STATS_OVER_HTTP_HAS_CONSTEXPR_STRING
static constexpr std::string
#else
static std::string
#endif
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
append_prometheus_v2_label(std::string &labels, std::string_view key, std::string_view val)
{
  if (!labels.empty()) {
    labels += ", ";
  }
  labels += key;
  labels += "=\"";
  prometheus_escape_label_value(labels, val);
  labels += "\"";
}

static bool
contains_prometheus_v2_token(const std::string_view *tokens, size_t size, std::string_view token)
{
  for (size_t i = 0; i < size; ++i) {
    if (tokens[i] == token) {
      return true;
    }
  }
  return false;
}

static swoc::TextView
take_prometheus_v2_token(swoc::TextView &view)
{
  size_t         sep = view.find_first_of("._[]");
  swoc::TextView token;

  if (sep == swoc::TextView::npos) {
    token = view;
    view.clear();
  } else {
    token = view.prefix(sep);
    view.remove_prefix(sep + 1);
  }
  return token;
}

/** Parse a Prometheus v2 metric name and return the base name and labels.
 *
 * @param[in] name The metric name to parse.
 * @return A prometheus_v2_metric struct containing the base name and labels.
 */
static prometheus_v2_metric
parse_metric_v2(std::string_view name)
{
  swoc::TextView name_view{name};
  std::string    labels;
  std::string    base_name;

  constexpr std::string_view methods[] = {"get", "post", "head", "put", "delete", "options", "trace", "connect", "push", "purge"};
  constexpr std::string_view directions[] = {"incoming", "outgoing"};
  constexpr std::string_view results[]    = {"hit", "miss", "error", "errors", "success", "failure"};
  constexpr std::string_view categories[] = {"volume", "thread", "interface", "net", "host", "port"};

  while (!name_view.empty()) {
    swoc::TextView token = take_prometheus_v2_token(name_view);

    if (token.empty()) {
      continue;
    }

    bool token_handled = false;

    // Status codes (200, 4xx, etc.)
    if (token.length() == 3 && (token[0] >= '0' && token[0] <= '9') && ((token[1] >= '0' && token[1] <= '9') || token[1] == 'x') &&
        ((token[2] >= '0' && token[2] <= '9') || token[2] == 'x')) {
      append_prometheus_v2_label(labels, "status", token);
      token_handled = true;
    }
    // Direction (incoming / outgoing)
    else if (contains_prometheus_v2_token(directions, sizeof(directions) / sizeof(directions[0]), token)) {
      append_prometheus_v2_label(labels, "direction", token);
      token_handled = true;
    }
    // Multi-token method categories.
    else if (token == "extension" || token == "invalid") {
      swoc::TextView next       = name_view;
      swoc::TextView next_token = take_prometheus_v2_token(next);

      if (token == "extension" && next_token == "method") {
        append_prometheus_v2_label(labels, "method", "extension_method");
        name_view     = next;
        token_handled = true;
      } else if (token == "invalid" && next_token == "client") {
        append_prometheus_v2_label(labels, "method", "invalid_client");
        name_view     = next;
        token_handled = true;
      }
    }
    // Methods
    else if (contains_prometheus_v2_token(methods, sizeof(methods) / sizeof(methods[0]), token)) {
      append_prometheus_v2_label(labels, "method", token);
      token_handled = true;
    }
    // Generic Categories + Index (volume, 0, etc.)
    else if (contains_prometheus_v2_token(categories, sizeof(categories) / sizeof(categories[0]), token)) {
      swoc::TextView next = name_view;
      swoc::TextView id   = take_prometheus_v2_token(next);

      bool is_id = !id.empty();
      for (char c : id) {
        if (!(c >= '0' && c <= '9') && c != 'x') {
          is_id = false;
          break;
        }
      }
      if (is_id) {
        append_prometheus_v2_label(labels, token, id);
        if (!base_name.empty()) {
          base_name += ".";
        }
        base_name     += token;
        name_view      = next;
        token_handled  = true;
      }
    }
    // Results (hit, miss)
    else if (contains_prometheus_v2_token(results, sizeof(results) / sizeof(results[0]), token)) {
      // 'hit' and 'miss' are almost always labels.
      if (token == "hit" || token == "miss" || !name_view.empty()) {
        append_prometheus_v2_label(labels, "result", token);
        token_handled = true;
      }
    }
    // Buckets (e.g., 10ms)
    else {
      constexpr std::string_view units[] = {"ms", "us", "s"};
      for (const auto &unit : units) {
        size_t unit_len = unit.length();
        if (token.length() > unit_len && token.substr(token.length() - unit_len) == unit) {
          bool all_digits = true;
          for (size_t j = 0; j < token.length() - unit_len; ++j) {
            if (!(token[j] >= '0' && token[j] <= '9')) {
              all_digits = false;
              break;
            }
          }
          if (all_digits && token.length() > unit_len) {
            append_prometheus_v2_label(labels, "le", token);
            token_handled = true;
            break;
          }
        }
      }
    }

    if (!token_handled) {
      if (!base_name.empty()) {
        base_name += ".";
      }
      base_name += token;
    }
  }

  return {std::move(base_name), std::move(labels)};
}

static PrometheusName
prometheus_v1_name(std::string_view name, TSRecordDataType data_type)
{
  switch (data_type) {
  case TS_RECORDDATATYPE_COUNTER:
    return {sanitize_metric_name_for_prometheus(name), {}, PrometheusType::COUNTER};
  case TS_RECORDDATATYPE_INT:
    return {sanitize_metric_name_for_prometheus(name), {}, PrometheusType::GAUGE};
  default:
    // Floats have no TYPE line in this format, for compatibility.
    return {sanitize_metric_name_for_prometheus(name), {}, PrometheusType::UNTYPED};
  }
}

static PrometheusName
prometheus_v2_name(std::string_view name, TSRecordDataType data_type)
{
  auto v2 = parse_metric_v2(name);

  return {sanitize_metric_name_for_prometheus(v2.name), std::move(v2.labels),
          data_type == TS_RECORDDATATYPE_COUNTER ? PrometheusType::COUNTER : PrometheusType::GAUGE};
}

static void
warn_prometheus(const std::string &message)
{
  TSWarning("[%s] %s", PLUGIN_NAME, message.c_str());
}

static std::unique_ptr<PrometheusRenderer>
make_prometheus_renderer(output_format_t format, const stats_options &stats)
{
  PrometheusOptions options;

  options.help          = stats.prometheus_help;
  options.wrap_counters = stats.wrap_counters;
  if (format == output_format_t::PROMETHEUS_OUTPUT) {
    options.namer = prometheus_v1_name;
    options.rules = stats.rules;
    options.warn  = warn_prometheus;
  } else if (format == output_format_t::PROMETHEUS_V2_OUTPUT) {
    options.namer = prometheus_v2_name;
  } else {
    return nullptr;
  }
  return std::make_unique<PrometheusRenderer>(options);
}

static void
prometheus_add_stat(TSRecordType /* rec_type ATS_UNUSED */, void *edata, int /* registered ATS_UNUSED */, const char *name,
                    TSRecordDataType data_type, TSRecordData *datum)
{
  static_cast<PrometheusRenderer *>(edata)->add(name, data_type, *datum);
}

static void
json_out_stats(render_state *my_state)
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
static bool
br_encode(BrotliEncoderState *br, const char *data, int64_t len, bool finish, TSIOBuffer out)
{
  BrotliEncoderOperation const op       = finish ? BROTLI_OPERATION_FINISH : BROTLI_OPERATION_PROCESS;
  const uint8_t               *next_in  = reinterpret_cast<const uint8_t *>(data);
  size_t                       avail_in = static_cast<size_t>(len);

  do {
    int64_t  avail     = 0;
    uint8_t *next_out  = reinterpret_cast<uint8_t *>(TSIOBufferBlockWriteStart(TSIOBufferStart(out), &avail));
    size_t   avail_out = static_cast<size_t>(avail);

    if (!BrotliEncoderCompressStream(br, op, &avail_in, &next_in, &avail_out, &next_out, nullptr)) {
      return false;
    }
    TSIOBufferProduce(out, avail - avail_out);
  } while (avail_in > 0 || BrotliEncoderHasMoreOutput(br) || (finish && !BrotliEncoderIsFinished(br)));

  return true;
}
#endif

static bool
gzip_encode(z_stream &zstrm, const char *data, int64_t len, bool finish, TSIOBuffer out)
{
  int const flush = finish ? Z_FINISH : Z_NO_FLUSH;
  int       err   = Z_OK;

  zstrm.next_in  = reinterpret_cast<Bytef *>(const_cast<char *>(data));
  zstrm.avail_in = static_cast<uInt>(len);
  do {
    int64_t avail   = 0;
    zstrm.next_out  = reinterpret_cast<Bytef *>(TSIOBufferBlockWriteStart(TSIOBufferStart(out), &avail));
    zstrm.avail_out = static_cast<uInt>(avail);
    err             = deflate(&zstrm, flush);
    TSIOBufferProduce(out, avail - zstrm.avail_out);
  } while (err == Z_OK && (finish || zstrm.avail_out == 0));

  return finish ? err == Z_STREAM_END : (err == Z_OK || err == Z_BUF_ERROR);
}

// A deflate stream that the renders of one instance reuse, one render at a time.  deflateReset costs much less than
// deflateInit2, which allocates and clears the zlib tables.
struct zlib_stream {
  explicit zlib_stream(int wrapper_mode) : mode(wrapper_mode) {}
  ~zlib_stream()
  {
    if (ready) {
      deflateEnd(&strm);
    }
  }
  zlib_stream(const zlib_stream &)            = delete;
  zlib_stream &operator=(const zlib_stream &) = delete;

  // Returns the stream, ready for a new body, or nullptr when zlib cannot set it up.
  z_stream *
  start()
  {
    if (ready && deflateReset(&strm) == Z_OK) {
      return &strm;
    }
    if (ready) {
      deflateEnd(&strm);
      ready = false;
    }
    strm = z_stream{};
    if (deflateInit2(&strm, ZLIB_COMPRESSION_LEVEL, Z_DEFLATED, mode, ZLIB_MEMLEVEL, Z_DEFAULT_STRATEGY) != Z_OK) {
      return nullptr;
    }
    ready = true;
    return &strm;
  }

  int      mode;
  z_stream strm{};
  bool     ready = false;
};

struct zlib_streams {
  zlib_stream gzip{GZIP_MODE};
  zlib_stream deflate{DEFLATE_MODE};
};

#if HAVE_BROTLI_ENCODE_H
static bool
br_compress(TSIOBufferReader reader, TSIOBuffer out)
{
  std::unique_ptr<BrotliEncoderState, decltype(&BrotliEncoderDestroyInstance)> br{
    BrotliEncoderCreateInstance(nullptr, nullptr, nullptr), BrotliEncoderDestroyInstance};
  bool ok = br != nullptr;

  if (ok) {
    BrotliEncoderSetParameter(br.get(), BROTLI_PARAM_QUALITY, BROTLI_COMPRESSION_LEVEL);
    BrotliEncoderSetParameter(br.get(), BROTLI_PARAM_LGWIN, BROTLI_LGW);
  }
  for (TSIOBufferBlock blk = TSIOBufferReaderStart(reader); ok && blk != nullptr; blk = TSIOBufferBlockNext(blk)) {
    int64_t     len  = 0;
    const char *data = TSIOBufferBlockReadStart(blk, reader, &len);

    ok = br_encode(br.get(), data, len, false, out);
  }
  return ok && br_encode(br.get(), nullptr, 0, true, out);
}
#endif

static bool
gzip_compress(zlib_stream &stream, TSIOBufferReader reader, TSIOBuffer out)
{
  z_stream *zstrm = stream.start();
  bool      ok    = zstrm != nullptr;

  for (TSIOBufferBlock blk = TSIOBufferReaderStart(reader); ok && blk != nullptr; blk = TSIOBufferBlockNext(blk)) {
    int64_t     len  = 0;
    const char *data = TSIOBufferBlockReadStart(blk, reader, &len);

    ok = gzip_encode(*zstrm, data, len, false, out);
  }
  return ok && gzip_encode(*zstrm, nullptr, 0, true, out);
}

static bool
compress_body(encoding_format_t encoding, zlib_streams &zlib, TSIOBufferReader reader, TSIOBuffer out)
{
#if HAVE_BROTLI_ENCODE_H
  if (encoding == encoding_format_t::BR) {
    return br_compress(reader, out);
  }
#endif
  return gzip_compress(encoding == encoding_format_t::DEFLATE ? zlib.deflate : zlib.gzip, reader, out);
}

static void
csv_out_stats(render_state *my_state)
{
  TSRecordDump((TSRecordType)(TS_RECORDTYPE_PLUGIN | TS_RECORDTYPE_NODE | TS_RECORDTYPE_PROCESS), csv_out_stat, my_state);
  const char *version = TSTrafficServerVersionGet();
  APPEND_STAT_CSV_NUMERIC("current_time_epoch_ms", "%" PRIu64, ms_since_epoch());
  APPEND_STAT_CSV("version", "%s", version);
}

static void
prometheus_out_stats(output_format_t format, PrometheusRenderer &renderer, render_state *my_state)
{
  renderer.begin();
  TSRecordDump((TSRecordType)(TS_RECORDTYPE_PLUGIN | TS_RECORDTYPE_NODE | TS_RECORDTYPE_PROCESS), prometheus_add_stat, &renderer);

  std::string &body = renderer.render();

  if (my_state->options->prometheus_epoch) {
    if (format == output_format_t::PROMETHEUS_V2_OUTPUT) {
      if (renderer.options().help) {
        body.append("# HELP current_time_epoch_ms Current time in milliseconds since epoch.\n");
      }
      body.append("# TYPE current_time_epoch_ms gauge\n");
    }
    body.append("current_time_epoch_ms ").append(std::to_string(ms_since_epoch())).append("\n");
  }
  TSIOBufferWrite(my_state->resp_buffer, body.data(), body.size());

  auto const &stats = renderer.stats();

  count(metrics().series, stats.series);
  count(metrics().series_dropped, stats.dropped);
  count(metrics().series_relabeled, stats.relabeled);
  count(metrics().series_duplicates, stats.duplicates);
  count(metrics().series_type_conflicts, stats.type_conflicts);
}

static void
render_stats(output_format_t format, render_state *my_state, PrometheusRenderer *prometheus)
{
  switch (format) {
  case output_format_t::JSON_OUTPUT:
    json_out_stats(my_state);
    break;
  case output_format_t::CSV_OUTPUT:
    csv_out_stats(my_state);
    break;
  case output_format_t::PROMETHEUS_OUTPUT:
  case output_format_t::PROMETHEUS_V2_OUTPUT:
    prometheus_out_stats(format, *prometheus, my_state);
    break;
  }
}

static int
stats_origin(TSCont contp, TSEvent /* event ATS_UNUSED */, void *edata)
{
  config_t         *config;
  TSHttpTxn         txnp = (TSHttpTxn)edata;
  TSMBuffer         reqp;
  TSMLoc            hdr_loc = nullptr, url_loc = nullptr, accept_field = nullptr, accept_encoding_field = nullptr;
  TSEvent           reenable = TS_EVENT_HTTP_CONTINUE;
  int               path_len = 0;
  const char       *path     = nullptr;
  swoc::TextView    request_path;
  swoc::TextView    request_path_suffix;
  output_format_t   format_per_path          = output_format_t::JSON_OUTPUT;
  bool              path_had_explicit_format = false;
  bool              intercept                = false;
  output_format_t   output_format            = output_format_t::JSON_OUTPUT;
  encoding_format_t encoding                 = encoding_format_t::NONE;

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
    if (!request_path_suffix.starts_with('/') || !parse_format(request_path_suffix.substr(1), format_per_path)) {
      Dbg(dbg_ctl, "Unknown suffix for stats path: %.*s", static_cast<int>(request_path_suffix.length()),
          request_path_suffix.data());
      goto notforme;
    }
    path_had_explicit_format = true;
  }

  if (auto addr = TSHttpTxnClientAddrGet(txnp); !is_ipmap_allowed(config, addr)) {
    Dbg(dbg_ctl, "not right ip");
    TSHttpTxnStatusSet(txnp, TS_HTTP_STATUS_FORBIDDEN, PLUGIN_NAME);
    reenable = TS_EVENT_HTTP_ERROR;
    goto notforme;
  }

  TSHttpTxnCntlSet(txnp, TS_HTTP_CNTL_SKIP_REMAPPING, true); // not strictly necessary, but speed is everything these days

  /* This is us -- register our intercept */
  Dbg(dbg_ctl, "Intercepting request");
  intercept = true;

  if (path_had_explicit_format) {
    Dbg(dbg_ctl, "Path had explicit format, ignoring any Accept header: %.*s", static_cast<int>(request_path_suffix.size()),
        request_path_suffix.data());
    output_format = format_per_path;
  } else {
    // Check for an Accept header to determine response type.
    accept_field  = TSMimeHdrFieldFind(reqp, hdr_loc, TS_MIME_FIELD_ACCEPT, TS_MIME_LEN_ACCEPT);
    output_format = output_format_t::JSON_OUTPUT; // default to json output
    // accept header exists, use it to determine response type
    if (accept_field != TS_NULL_MLOC) {
      int              len = -1;
      const char      *str = TSMimeHdrFieldValueStringGet(reqp, hdr_loc, accept_field, -1, &len);
      std::string_view accept{};

      if (str != nullptr && len > 0) {
        accept = std::string_view{str, static_cast<std::string_view::size_type>(len)};
      }

      // Parse the Accept header, default to JSON output unless its another supported format
      if (ts::iequals(accept, "text/csv")) {
        Dbg(dbg_ctl, "Saw text/csv in accept header, sending CSV output.");
        output_format = output_format_t::CSV_OUTPUT;
      } else if (ts::iequals(accept, "text/plain; version=0.0.4")) {
        Dbg(dbg_ctl, "Saw text/plain; version=0.0.4 in accept header, sending Prometheus output.");
        output_format = output_format_t::PROMETHEUS_OUTPUT;
      } else if (ts::iequals(accept, "text/plain; version=2.0.0")) {
        Dbg(dbg_ctl, "Saw text/plain; version=2.0.0 in accept header, sending Prometheus v2 output.");
        output_format = output_format_t::PROMETHEUS_V2_OUTPUT;
      } else {
        Dbg(dbg_ctl, "Saw %.*s in accept header, defaulting to JSON output.", static_cast<int>(accept.size()),
            accept.empty() ? "" : accept.data());
        output_format = output_format_t::JSON_OUTPUT;
      }
    }
  }

  // Check for Accept Encoding
  accept_encoding_field = TSMimeHdrFieldFind(reqp, hdr_loc, TS_MIME_FIELD_ACCEPT_ENCODING, TS_MIME_LEN_ACCEPT_ENCODING);
  if (accept_encoding_field != TS_NULL_MLOC) {
    int              len = -1;
    const char      *str = TSMimeHdrFieldValueStringGet(reqp, hdr_loc, accept_encoding_field, -1, &len);
    std::string_view accept_encoding =
      (str != nullptr && len > 0) ? std::string_view{str, static_cast<size_t>(len)} : std::string_view{};
    if (len >= TS_HTTP_LEN_DEFLATE && accept_encoding.find(TS_HTTP_VALUE_DEFLATE) != std::string_view::npos) {
      Dbg(dbg_ctl, "Saw deflate in accept encoding");
      encoding = encoding_format_t::DEFLATE;
    } else if (len >= TS_HTTP_LEN_GZIP && accept_encoding.find(TS_HTTP_VALUE_GZIP) != std::string_view::npos) {
      Dbg(dbg_ctl, "Saw gzip in accept encoding");
      encoding = encoding_format_t::GZIP;
    }
#if HAVE_BROTLI_ENCODE_H
    else if (len >= TS_HTTP_LEN_BROTLI && accept_encoding.find(TS_HTTP_VALUE_BROTLI) != std::string_view::npos) {
      Dbg(dbg_ctl, "Saw br in accept encoding");
      encoding = encoding_format_t::BR;
    }
#endif
    else {
      encoding = encoding_format_t::NONE;
    }
  }
  Dbg(dbg_ctl, "Finished AE check");
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
  if (intercept) {
    serve_global_scrape(txnp, output_format, encoding);
  } else {
    TSHttpTxnReenable(txnp, reenable);
  }
  return 0;
}

void
TSPluginInit(int argc, const char *argv[])
{
  TSPluginRegistrationInfo info;

  static const char usage[] = PLUGIN_NAME ".so [--integer-counters] [--wrap-counters] [--no-prometheus-help] [--max-age-ms=N] "
                                          "[--wait-timeout-ms=N] [PATH]";
  static const struct option longopts[] = {
    {(char *)("integer-counters"),   no_argument,       nullptr, 'i'},
    {(char *)("wrap-counters"),      no_argument,       nullptr, 'w'},
    {(char *)("no-prometheus-help"), no_argument,       nullptr, 'n'},
    {(char *)("max-age-ms"),         required_argument, nullptr, 'a'},
    {(char *)("wait-timeout-ms"),    required_argument, nullptr, 't'},
    {nullptr,                        0,                 nullptr, 0  }
  };
  TSCont           main_cont, config_cont;
  config_holder_t *config_holder;
  stats_options    options;

  // Each request to the global plugin waits for a new render, unless --max-age-ms is set.
  options.max_age_ms = 0;

  info.plugin_name   = PLUGIN_NAME;
  info.vendor_name   = "Apache Software Foundation";
  info.support_email = "dev@trafficserver.apache.org";

  if (TSPluginRegister(&info) != TS_SUCCESS) {
    TSError("[%s] registration failed", PLUGIN_NAME);
    goto done;
  }
  metrics();

  for (;;) {
    switch (getopt_long(argc, (char *const *)argv, "iw", longopts, nullptr)) {
    case 'i':
      options.integer_counters = true;
      break;
    case 'w':
      options.wrap_counters = true;
      break;
    case 'n':
      options.prometheus_help = false;
      break;
    case 'a':
      if (!parse_integer(optarg, 0, MAX_MILLISECONDS, options.max_age_ms)) {
        TSError("[%s] --max-age-ms must be an integer from 0 to %" PRId64 ", not '%s', usage: %s", PLUGIN_NAME, MAX_MILLISECONDS,
                optarg, usage);
      }
      break;
    case 't':
      if (!parse_integer(optarg, 1, MAX_MILLISECONDS, options.wait_timeout_ms)) {
        TSError("[%s] --wait-timeout-ms must be an integer from 1 to %" PRId64 ", not '%s', usage: %s", PLUGIN_NAME,
                MAX_MILLISECONDS, optarg, usage);
      }
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

  global_instance = new std::shared_ptr<stats_instance>(make_stats_instance(options));

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
// Requests for the stats.  A render on a task thread answers the requests to the global plugin and to remap rules.
//

static constexpr std::string_view STATS_FORMAT_FIELD = "X-Stats-Format";
static constexpr std::string_view ALLOWED_METHODS    = "GET, HEAD";

static const char REMAP_USAGE[] = "[--format=json|csv|prometheus|prometheus_v2] [--integer-counters] [--wrap-counters] "
                                  "[--no-prometheus-help] [--max-age-ms=N] [--wait-timeout-ms=N] [--config=FILE] "
                                  "[--on-config-error=fail|503]";

static std::string_view
format_name(output_format_t format)
{
  switch (format) {
  case output_format_t::JSON_OUTPUT:
    return "json";
  case output_format_t::CSV_OUTPUT:
    return "csv";
  case output_format_t::PROMETHEUS_OUTPUT:
    return "prometheus";
  case output_format_t::PROMETHEUS_V2_OUTPUT:
    return "prometheus_v2";
  }
  return "json";
}

static std::string_view
format_content_type(output_format_t format)
{
  switch (format) {
  case output_format_t::JSON_OUTPUT:
    return "text/json";
  case output_format_t::CSV_OUTPUT:
    return "text/csv";
  case output_format_t::PROMETHEUS_OUTPUT:
    return "text/plain; version=0.0.4; charset=utf-8";
  case output_format_t::PROMETHEUS_V2_OUTPUT:
    return "text/plain; version=2.0.0; charset=utf-8";
  }
  return "text/json";
}

static std::string_view
encoding_name(encoding_format_t encoding)
{
  switch (encoding) {
  case encoding_format_t::DEFLATE:
    return "deflate";
  case encoding_format_t::GZIP:
    return "gzip";
  case encoding_format_t::BR:
    return "br";
  case encoding_format_t::NONE:
    break;
  }
  return {};
}

static bool
parse_format(std::string_view name, output_format_t &format)
{
  for (auto candidate : {output_format_t::JSON_OUTPUT, output_format_t::CSV_OUTPUT, output_format_t::PROMETHEUS_OUTPUT,
                         output_format_t::PROMETHEUS_V2_OUTPUT}) {
    if (name == format_name(candidate)) {
      format = candidate;
      return true;
    }
  }
  return false;
}

// A rendered body in one encoding.  It does not change after the render publishes it, so requests on any thread share its
// blocks.
struct stats_snapshot {
  stats_snapshot() : body(TSIOBufferCreate()), reader(TSIOBufferReaderAlloc(body)) {}
  ~stats_snapshot() { TSIOBufferDestroy(body); }
  stats_snapshot(const stats_snapshot &)            = delete;
  stats_snapshot &operator=(const stats_snapshot &) = delete;

  TSIOBuffer                            body;
  TSIOBufferReader                      reader;
  int64_t                               bytes    = 0;
  encoding_format_t                     encoding = encoding_format_t::NONE;
  std::chrono::steady_clock::time_point rendered;
};

using snapshot_ptr   = std::shared_ptr<const stats_snapshot>;
using snapshot_table = snapshot_ptr[FORMAT_COUNT][ENCODING_COUNT];

// One request for the stats.  The waiter list, the transaction hooks and the intercept share it.
struct stats_scrape {
  stats_scrape(TSHttpTxn txn, output_format_t fmt, encoding_format_t enc, bool rule)
    : txnp(txn), format(fmt), encoding(enc), remap(rule)
  {
  }

  TSHttpTxn         txnp;
  output_format_t   format;
  encoding_format_t encoding;
  bool              remap; // A remap rule serves the request, rather than the global plugin.
  // When the request started to wait for a render.
  std::chrono::steady_clock::time_point arrived;

  // TSRemapDoRemap, scrape_wait, the render or the watchdog sets these before the transaction continues, and the intercept
  // reads them only after that.  The snapshot is null for a HEAD request to a remap rule and for a 503.
  snapshot_ptr snapshot;
  TSHttpStatus status = TS_HTTP_STATUS_OK;
};

struct stats_watchdog;

// The stats of one remap rule, or of the global plugin.  The remap rule and each continuation that works for the instance
// hold a reference, so that a render or a watchdog can outlive the rule.
struct stats_instance {
  explicit stats_instance(const stats_options &opts) : options(opts) {}
  ~stats_instance() { Dbg(dbg_ctl, "Freeing stats instance %p", this); }
  stats_instance(const stats_instance &)            = delete;
  stats_instance &operator=(const stats_instance &) = delete;

  PrometheusRenderer *
  prometheus(output_format_t format)
  {
    std::unique_ptr<PrometheusRenderer> *renderer = nullptr;

    if (format == output_format_t::PROMETHEUS_OUTPUT) {
      renderer = &prometheus_v1;
    } else if (format == output_format_t::PROMETHEUS_V2_OUTPUT) {
      renderer = &prometheus_v2;
    } else {
      return nullptr;
    }
    if (*renderer == nullptr) {
      *renderer = make_prometheus_renderer(format, options);
    }
    return renderer->get();
  }

  const stats_options options;
  // Only the render in flight uses these, so the mutex does not guard them.
  std::unique_ptr<PrometheusRenderer> prometheus_v1;
  std::unique_ptr<PrometheusRenderer> prometheus_v2;
  zlib_streams                        zlib;

  // The mutex guards the members below it.  No thread holds it while it calls an API that can run a continuation.
  std::mutex                                 mutex;
  snapshot_table                             snapshots;
  std::vector<std::shared_ptr<stats_scrape>> waiters;
  bool                                       rendering = false;
  stats_watchdog                            *watchdog  = nullptr; // Set while a request waits.
};

static std::shared_ptr<stats_instance>
make_stats_instance(const stats_options &options)
{
  return std::make_shared<stats_instance>(options);
}

// Renders each wanted format once and compresses it for each wanted encoding.
static void
render_snapshots(stats_instance &instance, const bool (&wanted)[FORMAT_COUNT][ENCODING_COUNT], snapshot_table &fresh)
{
  auto const now = std::chrono::steady_clock::now();

  for (size_t f = 0; f < FORMAT_COUNT; ++f) {
    if (std::find(std::begin(wanted[f]), std::end(wanted[f]), true) == std::end(wanted[f])) {
      continue;
    }

    auto const   format = static_cast<output_format_t>(f);
    auto         body   = std::make_shared<stats_snapshot>();
    render_state render{body->body, &instance.options};

    render_stats(format, &render, instance.prometheus(format));
    body->bytes    = TSIOBufferReaderAvail(body->reader);
    body->rendered = now;

    for (size_t e = 1; e < ENCODING_COUNT; ++e) {
      if (!wanted[f][e]) {
        continue;
      }

      auto const encoding   = static_cast<encoding_format_t>(e);
      auto       compressed = std::make_shared<stats_snapshot>();

      if (compress_body(encoding, instance.zlib, body->reader, compressed->body)) {
        compressed->bytes    = TSIOBufferReaderAvail(compressed->reader);
        compressed->encoding = encoding;
        compressed->rendered = now;
        fresh[f][e]          = std::move(compressed);
      } else {
        TSError("[%s] Cannot compress the stats, sending them uncompressed", PLUGIN_NAME);
        fresh[f][e] = body;
      }
    }
    fresh[f][static_cast<size_t>(encoding_format_t::NONE)] = std::move(body);
  }
}

static int watchdog_handler(TSCont contp, TSEvent event, void *edata);

// Answers the waiting requests with a 503 when the render takes longer than wait_timeout_ms.
struct stats_watchdog {
  explicit stats_watchdog(std::shared_ptr<stats_instance> inst)
    : instance(std::move(inst)), cont(TSContCreate(watchdog_handler, TSMutexCreate()))
  {
    TSContDataSet(cont, this);
  }

  std::shared_ptr<stats_instance> instance;
  TSCont                          cont;

  // The mutex of the continuation guards these.
  TSAction action = nullptr;
  bool     fired  = false;
};

static int
watchdog_handler(TSCont contp, TSEvent /* event ATS_UNUSED */, void * /* edata ATS_UNUSED */)
{
  auto                                      *watchdog = static_cast<stats_watchdog *>(TSContDataGet(contp));
  stats_instance                            &instance = *watchdog->instance;
  std::vector<std::shared_ptr<stats_scrape>> expired;

  watchdog->fired = true;
  {
    std::lock_guard lock{instance.mutex};

    // A render took this watchdog or replaced it, and that render destroys it.
    if (instance.watchdog != watchdog) {
      return 0;
    }
    instance.watchdog = nullptr;
    expired.swap(instance.waiters);
  }

  Dbg(dbg_ctl, "Answering %zu requests with a 503 after %" PRId64 " ms without a render of stats instance %p", expired.size(),
      instance.options.wait_timeout_ms, &instance);
  count(metrics().waiter_timeouts, expired.size());
  for (auto const &scrape : expired) {
    scrape->status = TS_HTTP_STATUS_SERVICE_UNAVAILABLE;
    TSHttpTxnReenable(scrape->txnp, TS_EVENT_HTTP_CONTINUE);
  }
  TSContDestroy(contp);
  delete watchdog;
  return 0;
}

// Stops and destroys a watchdog that a render took from its instance.
static void
stop_watchdog(stats_watchdog *watchdog)
{
  TSMutex mutex = TSContMutexGet(watchdog->cont);

  // Only the holder of its continuation's mutex can cancel an action.
  TSMutexLock(mutex);
  if (!watchdog->fired) {
    TSActionCancel(watchdog->action);
  }
  TSMutexUnlock(mutex);
  TSContDestroy(watchdog->cont);
  delete watchdog;
}

// Creates a watchdog for the caller to publish under the instance mutex.  The calling thread holds the mutex of the watchdog
// until arm_watchdog sets its action, so that a render that finishes first can cancel that action.
static stats_watchdog *
new_watchdog(std::shared_ptr<stats_instance> instance)
{
  auto *watchdog = new stats_watchdog(std::move(instance));

  TSMutexLock(TSContMutexGet(watchdog->cont));
  return watchdog;
}

static void
arm_watchdog(stats_watchdog *watchdog)
{
  TSMutex mutex = TSContMutexGet(watchdog->cont);

  watchdog->action = TSContScheduleOnPool(watchdog->cont, watchdog->instance->options.wait_timeout_ms, TS_THREAD_POOL_NET);
  TSMutexUnlock(mutex);
}

static int render_handler(TSCont contp, TSEvent event, void *edata);

// Each render gets a new continuation, because TSContScheduleOnPool locks the mutex of the continuation on the calling
// thread, and render_handler holds that mutex until it returns.  A shared continuation could block the ET_NET thread that
// schedules the next render.
static void
schedule_render(std::shared_ptr<stats_instance> instance)
{
  TSCont contp = TSContCreate(render_handler, TSMutexCreate());

  TSContDataSet(contp, new std::shared_ptr<stats_instance>(std::move(instance)));
  TSContScheduleOnPool(contp, 0, TS_THREAD_POOL_TASK);
}

static int
render_handler(TSCont contp, TSEvent /* event ATS_UNUSED */, void * /* edata ATS_UNUSED */)
{
  auto           *ref                                  = static_cast<std::shared_ptr<stats_instance> *>(TSContDataGet(contp));
  stats_instance &instance                             = **ref;
  bool            wanted[FORMAT_COUNT][ENCODING_COUNT] = {};
  bool            waiting                              = false;
  snapshot_table  fresh;

  {
    std::lock_guard lock{instance.mutex};

    for (auto const &scrape : instance.waiters) {
      wanted[static_cast<size_t>(scrape->format)][static_cast<size_t>(scrape->encoding)] = true;
      waiting                                                                            = true;
    }
  }
  // The watchdog can answer every waiting request before the render starts.
  if (waiting) {
    static thread_local int64_t rest  = 0;
    int64_t const               start = thread_cpu_ns();

    render_snapshots(instance, wanted, fresh);
    count(metrics().renders);
    count_cpu_time(metrics().render_us, start, rest);
  }

  std::vector<std::shared_ptr<stats_scrape>> ready;
  stats_watchdog                            *watchdog    = nullptr;
  stats_watchdog                            *replacement = nullptr;
  bool                                       again       = false;

  {
    std::lock_guard lock{instance.mutex};

    for (size_t f = 0; f < FORMAT_COUNT; ++f) {
      for (size_t e = 0; e < ENCODING_COUNT; ++e) {
        if (fresh[f][e] != nullptr) {
          instance.snapshots[f][e] = fresh[f][e];
        }
      }
    }

    // As in scrape_wait, a request takes a render only if the render is younger than max_age_ms when the request arrives.
    auto const max_age  = std::chrono::milliseconds{instance.options.max_age_ms};
    auto       rendered = [&fresh, max_age](const std::shared_ptr<stats_scrape> &scrape) -> snapshot_ptr {
      auto const &snapshot = fresh[static_cast<size_t>(scrape->format)][static_cast<size_t>(scrape->encoding)];

      return snapshot != nullptr && scrape->arrived - snapshot->rendered < max_age ? snapshot : nullptr;
    };
    auto answered = std::stable_partition(instance.waiters.begin(), instance.waiters.end(),
                                          [&rendered](const auto &scrape) { return rendered(scrape) == nullptr; });

    for (auto it = answered; it != instance.waiters.end(); ++it) {
      (*it)->snapshot = rendered(*it);
      ready.push_back(std::move(*it));
    }
    instance.waiters.erase(answered, instance.waiters.end());

    // Requests for another format or encoding, and requests that arrived too late for this render, wait for the next one.  They
    // get a new watchdog, because the current one can have started long before they arrived.
    again = !instance.waiters.empty();
    if (again) {
      replacement = new_watchdog(*ref);
    } else {
      instance.rendering = false;
    }
    watchdog = std::exchange(instance.watchdog, replacement);
  }

  if (waiting) {
    Dbg(dbg_ctl, "Rendered stats instance %p for %zu waiting requests", &instance, ready.size());
  }
  if (watchdog != nullptr) {
    stop_watchdog(watchdog);
  }
  if (replacement != nullptr) {
    arm_watchdog(replacement);
  }
  for (auto const &scrape : ready) {
    TSHttpTxnReenable(scrape->txnp, TS_EVENT_HTTP_CONTINUE);
  }
  if (again) {
    schedule_render(*ref);
  }
  delete ref;
  TSContDestroy(contp);
  return 0;
}

// Answers a request from a snapshot younger than max_age_ms.  Otherwise the request waits for a render, which starts unless
// one is in flight.  Either way, the transaction continues once it has its answer.
static void
scrape_wait(const std::shared_ptr<stats_instance> &instance, std::shared_ptr<stats_scrape> scrape)
{
  auto const      now      = std::chrono::steady_clock::now();
  bool            waits    = false;
  bool            render   = false;
  stats_watchdog *watchdog = nullptr;

  {
    std::lock_guard lock{instance->mutex};
    auto const     &snapshot = instance->snapshots[static_cast<size_t>(scrape->format)][static_cast<size_t>(scrape->encoding)];

    if (snapshot != nullptr && now - snapshot->rendered < std::chrono::milliseconds{instance->options.max_age_ms}) {
      scrape->snapshot = snapshot;
    } else {
      scrape->arrived = now;
      instance->waiters.push_back(scrape);
      waits  = true;
      render = !std::exchange(instance->rendering, true);
      if (instance->watchdog == nullptr) {
        watchdog = instance->watchdog = new_watchdog(instance);
      }
    }
  }

  if (!waits) {
    TSHttpTxnReenable(scrape->txnp, TS_EVENT_HTTP_CONTINUE);
    return;
  }

  Dbg(dbg_ctl, "Waiting for a render of stats instance %p", instance.get());
  if (watchdog != nullptr) {
    arm_watchdog(watchdog);
  }
  if (render) {
    schedule_render(instance);
  }
}

// The intercept holds only its scrape, because intercept events can arrive after the transaction and its remap rule are gone.
struct scrape_intercept {
  explicit scrape_intercept(std::shared_ptr<stats_scrape> s) : scrape(std::move(s)) {}
  ~scrape_intercept()
  {
    if (net_vc != nullptr) {
      TSVConnClose(net_vc);
    }
    if (req_buffer != nullptr) {
      TSIOBufferDestroy(req_buffer);
    }
    if (resp_buffer != nullptr) {
      TSIOBufferDestroy(resp_buffer);
    }
  }
  scrape_intercept(const scrape_intercept &)            = delete;
  scrape_intercept &operator=(const scrape_intercept &) = delete;

  std::shared_ptr<stats_scrape> scrape;
  TSVConn                       net_vc      = nullptr;
  TSVIO                         write_vio   = nullptr;
  TSIOBuffer                    req_buffer  = nullptr;
  TSIOBuffer                    resp_buffer = nullptr;
  TSIOBufferReader              resp_reader = nullptr;
};

static std::string
scrape_response_header(const stats_scrape &scrape)
{
  if (!scrape.remap) {
    if (scrape.status != TS_HTTP_STATUS_OK) {
      return RESP_HEADER_UNAVAILABLE;
    }

    std::string header{"HTTP/1.0 200 OK\r\nContent-Type: "};

    header.append(format_content_type(scrape.format)).append("\r\n");
    if (auto const name = encoding_name(scrape.snapshot->encoding); !name.empty()) {
      header.append("Content-Encoding: ").append(name).append("\r\n");
    }
    header.append("Cache-Control: no-cache\r\n\r\n");
    return header;
  }

  std::string header{"HTTP/1.1 "};

  header.append(std::to_string(scrape.status)).append(" ").append(TSHttpHdrReasonLookup(scrape.status)).append("\r\n");
  if (scrape.status == TS_HTTP_STATUS_OK) {
    header.append("Content-Type: ").append(format_content_type(scrape.format)).append("\r\n");
  }
  header.append("Cache-Control: no-store\r\n");
  header.append(STATS_FORMAT_FIELD).append(": ").append(format_name(scrape.format)).append("\r\n");
  if (scrape.snapshot != nullptr || scrape.status != TS_HTTP_STATUS_OK) {
    header.append("Content-Length: ")
      .append(std::to_string(scrape.snapshot != nullptr ? scrape.snapshot->bytes : 0))
      .append("\r\n");
  }
  header.append("\r\n");
  return header;
}

static void
scrape_send_response(TSCont contp, scrape_intercept *intercept)
{
  const stats_scrape &scrape = *intercept->scrape;
  std::string const   header = scrape_response_header(scrape);

  intercept->resp_buffer = TSIOBufferCreate();
  intercept->resp_reader = TSIOBufferReaderAlloc(intercept->resp_buffer);

  int64_t bytes = TSIOBufferWrite(intercept->resp_buffer, header.data(), header.size());

  if (scrape.snapshot != nullptr) {
    // This shares the blocks of the snapshot rather than copying its data.
    bytes += TSIOBufferCopy(intercept->resp_buffer, scrape.snapshot->reader, scrape.snapshot->bytes, 0);
  }
  TSVConnShutdown(intercept->net_vc, 1, 0);
  intercept->write_vio = TSVConnWrite(intercept->net_vc, contp, intercept->resp_reader, bytes);
  count(metrics().requests);
  count(metrics().bytes_out, bytes);
}

static int
scrape_intercept_handler(TSCont contp, TSEvent event, void *edata)
{
  static thread_local int64_t rest      = 0;
  int64_t const               start     = thread_cpu_ns();
  auto                       *intercept = static_cast<scrape_intercept *>(TSContDataGet(contp));

  switch (event) {
  case TS_EVENT_NET_ACCEPT:
    intercept->net_vc     = static_cast<TSVConn>(edata);
    intercept->req_buffer = TSIOBufferCreate();
    TSVConnRead(intercept->net_vc, contp, intercept->req_buffer, INT64_MAX);
    break;
  case TS_EVENT_VCONN_READ_READY:
    scrape_send_response(contp, intercept);
    break;
  case TS_EVENT_VCONN_WRITE_READY:
    TSVIOReenable(intercept->write_vio);
    break;
  case TS_EVENT_NET_ACCEPT_FAILED:
  case TS_EVENT_VCONN_EOS:
  case TS_EVENT_ERROR:
  case TS_EVENT_VCONN_INACTIVITY_TIMEOUT:
  case TS_EVENT_VCONN_ACTIVE_TIMEOUT:
  case TS_EVENT_VCONN_WRITE_COMPLETE:
    Dbg(dbg_ctl, "Intercept finished on %s", TSHttpEventNameLookup(event));
    delete intercept;
    TSContDestroy(contp);
    break;
  default:
    TSReleaseAssert(!"Unexpected Event");
  }
  count_cpu_time(metrics().intercept_us, start, rest);
  return 0;
}

// The global plugin intercepts the request in its read request header hook, then waits there for the stats.
static void
serve_global_scrape(TSHttpTxn txnp, output_format_t format, encoding_format_t encoding)
{
  auto   scrape         = std::make_shared<stats_scrape>(txnp, format, encoding, false);
  TSCont intercept_cont = TSContCreate(scrape_intercept_handler, TSMutexCreate());

  TSContDataSet(intercept_cont, new scrape_intercept(scrape));
  TSHttpTxnIntercept(intercept_cont, txnp);
  scrape_wait(*global_instance, std::move(scrape));
}

//
// Remap plugin.
//

// The transaction hooks of a GET request to a remap rule.
struct scrape_txn {
  std::shared_ptr<stats_instance> instance;
  std::shared_ptr<stats_scrape>   scrape;
};

static int
scrape_txn_handler(TSCont contp, TSEvent event, void *edata)
{
  auto *txn = static_cast<scrape_txn *>(TSContDataGet(contp));

  if (event == TS_EVENT_HTTP_CACHE_LOOKUP_COMPLETE) {
    scrape_wait(txn->instance, txn->scrape);
    return 0;
  }
  if (event == TS_EVENT_HTTP_TXN_CLOSE) {
    delete txn;
    TSContDestroy(contp);
  }
  TSHttpTxnReenable(static_cast<TSHttpTxn>(edata), TS_EVENT_HTTP_CONTINUE);
  return 0;
}

static void
add_allow_field(TSHttpTxn txnp)
{
  TSMBuffer bufp;
  TSMLoc    hdr_loc;

  if (TSHttpTxnClientRespGet(txnp, &bufp, &hdr_loc) == TS_SUCCESS) {
    TSMLoc field_loc;

    // A remap ACL filter that denies the request replaces the 405 with a 403.
    if (TSHttpHdrStatusGet(bufp, hdr_loc) == TS_HTTP_STATUS_METHOD_NOT_ALLOWED &&
        TSMimeHdrFieldCreateNamed(bufp, hdr_loc, TS_MIME_FIELD_ALLOW, TS_MIME_LEN_ALLOW, &field_loc) == TS_SUCCESS) {
      TSMimeHdrFieldValueStringSet(bufp, hdr_loc, field_loc, -1, ALLOWED_METHODS.data(), ALLOWED_METHODS.size());
      TSMimeHdrFieldAppend(bufp, hdr_loc, field_loc);
      TSHandleMLocRelease(bufp, hdr_loc, field_loc);
    }
    TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc);
  }
}

static int
allow_handler(TSCont contp, TSEvent event, void *edata)
{
  auto txnp = static_cast<TSHttpTxn>(edata);

  if (event == TS_EVENT_HTTP_SEND_RESPONSE_HDR) {
    add_allow_field(txnp);
  } else if (event == TS_EVENT_HTTP_TXN_CLOSE) {
    TSContDestroy(contp);
  }
  TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
  return 0;
}

// The settings that a remap rule or its configuration file can set.
struct stats_settings {
  std::optional<output_format_t>         format;
  std::optional<int64_t>                 max_age_ms;
  std::optional<int64_t>                 wait_timeout_ms;
  std::optional<bool>                    prometheus_help;
  std::shared_ptr<const PrometheusRules> rules;
};

static std::string
yaml_line(const YAML::Node &node)
{
  auto const mark = node.Mark();

  return mark.is_null() ? std::string{} : "line " + std::to_string(mark.line + 1) + ": ";
}

// Reads the configuration file of a remap rule.  Returns an empty string, or a description of the first error.
static std::string
load_settings_file(const std::string &path, stats_settings &settings)
{
  std::ifstream file{path};

  if (!file) {
    return std::string{"cannot open the file: "} + strerror(errno);
  }
  try {
    YAML::Node const root = YAML::Load(file);

    if (!root.IsMap()) {
      return yaml_line(root) + "the file must be a map";
    }
    for (auto const &item : root) {
      std::string const key   = item.first.as<std::string>();
      YAML::Node const  value = item.second;

      if (key == "format") {
        output_format_t format;

        if (!value.IsScalar() || !parse_format(value.Scalar(), format)) {
          return yaml_line(value) + "format must be json, csv, prometheus or prometheus_v2";
        }
        settings.format = format;
      } else if (key == "render") {
        if (!value.IsMap()) {
          return yaml_line(value) + "render must be a map";
        }
        for (auto const &setting : value) {
          std::string const name   = setting.first.as<std::string>();
          std::string const text   = setting.second.IsScalar() ? setting.second.Scalar() : std::string{};
          int64_t           number = 0;
          int64_t           min    = 0;
          bool              valid  = false;

          if (name == "max_age_ms") {
            valid               = parse_integer(text, min, MAX_MILLISECONDS, number);
            settings.max_age_ms = number;
          } else if (name == "wait_timeout_ms") {
            min                      = 1;
            valid                    = parse_integer(text, min, MAX_MILLISECONDS, number);
            settings.wait_timeout_ms = number;
          } else {
            return yaml_line(setting.first) + "unknown key render." + name;
          }
          if (!valid) {
            return yaml_line(setting.second) + "render." + name + " must be an integer from " + std::to_string(min) + " to " +
                   std::to_string(MAX_MILLISECONDS) + ", not '" + text + "'";
          }
        }
      } else if (key == "prometheus") {
        auto rules = std::make_shared<PrometheusRules>();

        if (std::string error = rules->load(value); !error.empty()) {
          return error;
        }
        settings.prometheus_help = rules->help();
        settings.rules           = std::move(rules);
      } else {
        return yaml_line(item.first) + "unknown key " + key;
      }
    }
  } catch (const YAML::Exception &e) {
    return e.what();
  }
  return {};
}

// Registers the configuration file of a remap rule as a child of the remap configuration.  After a change to the file, a
// configuration reload loads the remap configuration again, and with it the file.
static void
watch_config_file(const std::string &path)
{
  TSMgmtString parent = nullptr;

  if (TSMgmtStringGet("proxy.config.url_remap.filename", &parent) == TS_SUCCESS) {
    TSMgmtConfigFileAdd(parent, path.c_str());
  } else {
    TSWarning("[%s] Cannot read proxy.config.url_remap.filename, so a configuration reload does not detect a change to %s",
              PLUGIN_NAME, path.c_str());
  }
  TSfree(parent);
}

TSReturnCode
TSRemapInit(TSRemapInterface * /* api_info ATS_UNUSED */, char * /* errbuf ATS_UNUSED */, int /* errbuf_size ATS_UNUSED */)
{
  metrics();
  return TS_SUCCESS;
}

TSReturnCode
TSRemapNewInstance(int argc, char *argv[], void **ih, char *errbuf, int errbuf_size)
{
  static const struct option longopts[] = {
    {"format",             required_argument, nullptr, 'f'},
    {"integer-counters",   no_argument,       nullptr, 'i'},
    {"wrap-counters",      no_argument,       nullptr, 'w'},
    {"no-prometheus-help", no_argument,       nullptr, 'n'},
    {"max-age-ms",         required_argument, nullptr, 'a'},
    {"wait-timeout-ms",    required_argument, nullptr, 't'},
    {"config",             required_argument, nullptr, 'c'},
    {"on-config-error",    required_argument, nullptr, 'e'},
    {nullptr,              0,                 nullptr, 0  }
  };
  stats_options  options;
  stats_settings rule;
  std::string    config_path;
  bool           answer_errors = false;
  int64_t        number        = 0;

  options.prometheus_epoch = false;

  // argv[0] is the "from" URL.  Skip it so that the "to" URL poses as the program name.
  --argc;
  ++argv;
  for (int opt, option_index = 0; (opt = getopt_long(argc, argv, "", longopts, &option_index)) != -1;) {
    bool    valid = true;
    int64_t min   = 0;
    int64_t max   = 0;

    switch (opt) {
    case 'f':
      if (output_format_t format; parse_format(optarg, format)) {
        rule.format = format;
      } else {
        valid = false;
      }
      break;
    case 'i':
      options.integer_counters = true;
      break;
    case 'w':
      options.wrap_counters = true;
      break;
    case 'n':
      rule.prometheus_help = false;
      break;
    case 'a':
      max             = MAX_MILLISECONDS;
      valid           = parse_integer(optarg, min, max, number);
      rule.max_age_ms = number;
      break;
    case 't':
      min                  = 1;
      max                  = MAX_MILLISECONDS;
      valid                = parse_integer(optarg, min, max, number);
      rule.wait_timeout_ms = number;
      break;
    case 'c':
      config_path = optarg;
      break;
    case 'e':
      if (std::string_view{optarg} == "503") {
        answer_errors = true;
      } else if (std::string_view{optarg} != "fail") {
        valid = false;
      }
      break;
    default:
      snprintf(errbuf, errbuf_size, "[%s] Invalid option '%s', usage: %s", PLUGIN_NAME, argv[optind - 1], REMAP_USAGE);
      return TS_ERROR;
    }
    if (!valid) {
      if (max > 0) {
        snprintf(errbuf, errbuf_size, "[%s] --%s must be an integer from %" PRId64 " to %" PRId64 ", not '%s', usage: %s",
                 PLUGIN_NAME, longopts[option_index].name, min, max, optarg, REMAP_USAGE);
      } else {
        snprintf(errbuf, errbuf_size, "[%s] Invalid --%s '%s', usage: %s", PLUGIN_NAME, longopts[option_index].name, optarg,
                 REMAP_USAGE);
      }
      return TS_ERROR;
    }
  }
  if (optind < argc) {
    snprintf(errbuf, errbuf_size, "[%s] Unexpected argument '%s', usage: %s", PLUGIN_NAME, argv[optind], REMAP_USAGE);
    return TS_ERROR;
  }

  stats_settings file;

  if (!config_path.empty()) {
    if (config_path.front() != '/') {
      config_path = std::string{TSConfigDirGet()} + "/" + config_path;
    }
    // Watch the file even when it has an error, so that a reload after a fix loads it.
    watch_config_file(config_path);

    std::string error  = load_settings_file(config_path, file);
    auto const  format = rule.format.value_or(file.format.value_or(options.format));

    if (error.empty() && file.rules != nullptr && format != output_format_t::PROMETHEUS_OUTPUT) {
      error = "the prometheus settings need the prometheus format, not " + std::string{format_name(format)};
    }
    if (!error.empty()) {
      count(metrics().config_errors);
      if (!answer_errors) {
        snprintf(errbuf, errbuf_size, "[%s] %s: %s", PLUGIN_NAME, config_path.c_str(), error.c_str());
        return TS_ERROR;
      }
      TSError("[%s] %s: %s.  The remap rule answers each request with a 503", PLUGIN_NAME, config_path.c_str(), error.c_str());
      file                 = {};
      options.config_error = true;
    }
  }

  // The options of the remap rule take precedence over the file.
  options.format          = rule.format.value_or(file.format.value_or(options.format));
  options.max_age_ms      = rule.max_age_ms.value_or(file.max_age_ms.value_or(options.max_age_ms));
  options.wait_timeout_ms = rule.wait_timeout_ms.value_or(file.wait_timeout_ms.value_or(options.wait_timeout_ms));
  options.prometheus_help = rule.prometheus_help.value_or(file.prometheus_help.value_or(options.prometheus_help));
  options.rules           = std::move(file.rules);

  *ih = new std::shared_ptr<stats_instance>(make_stats_instance(options));
  return TS_SUCCESS;
}

void
TSRemapDeleteInstance(void *ih)
{
  auto *instance = static_cast<std::shared_ptr<stats_instance> *>(ih);

  Dbg(dbg_ctl, "Releasing remap instance %p", instance->get());
  delete instance;
}

TSRemapStatus
TSRemapDoRemap(void *ih, TSHttpTxn txnp, TSRemapRequestInfo *rri)
{
  int         method_len = 0;
  const char *method     = TSHttpHdrMethodGet(rri->requestBufp, rri->requestHdrp, &method_len);

  if (method != TS_HTTP_METHOD_GET && method != TS_HTTP_METHOD_HEAD) {
    TSCont allow_cont = TSContCreate(allow_handler, nullptr);

    TSHttpTxnStatusSet(txnp, TS_HTTP_STATUS_METHOD_NOT_ALLOWED, PLUGIN_NAME);
    TSHttpTxnHookAdd(txnp, TS_HTTP_SEND_RESPONSE_HDR_HOOK, allow_cont);
    TSHttpTxnHookAdd(txnp, TS_HTTP_TXN_CLOSE_HOOK, allow_cont);
    return TSREMAP_NO_REMAP;
  }

  TSHttpTxnConfigIntSet(txnp, TS_CONFIG_HTTP_CACHE_HTTP, 0);

  auto const &instance = *static_cast<std::shared_ptr<stats_instance> *>(ih);
  auto        scrape   = std::make_shared<stats_scrape>(txnp, instance->options.format, encoding_format_t::NONE, true);

  if (instance->options.config_error) {
    scrape->status = TS_HTTP_STATUS_SERVICE_UNAVAILABLE;
  } else if (method == TS_HTTP_METHOD_GET) {
    // Wait for the stats in the cache lookup hook, which runs after the remap ACL filters, so that a request they deny starts
    // no render.
    TSCont txn_cont = TSContCreate(scrape_txn_handler, nullptr);

    TSContDataSet(txn_cont, new scrape_txn{instance, scrape});
    TSHttpTxnHookAdd(txnp, TS_HTTP_CACHE_LOOKUP_COMPLETE_HOOK, txn_cont);
    TSHttpTxnHookAdd(txnp, TS_HTTP_TXN_CLOSE_HOOK, txn_cont);
  }

  TSCont intercept_cont = TSContCreate(scrape_intercept_handler, TSMutexCreate());

  TSContDataSet(intercept_cont, new scrape_intercept(std::move(scrape)));
  TSHttpTxnServerIntercept(intercept_cont, txnp);
  Dbg(dbg_ctl, "Intercepting %.*s request", method_len, method);
  return TSREMAP_NO_REMAP;
}

//
// Compilation time unit tests.
//
#if defined(DEBUG) && STATS_OVER_HTTP_HAS_CONSTEXPR_STRING
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
#endif // defined(DEBUG) && STATS_OVER_HTTP_HAS_CONSTEXPR_STRING
