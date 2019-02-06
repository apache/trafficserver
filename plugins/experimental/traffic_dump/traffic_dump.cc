/** @traffic_dump.cc
  Plugin Traffic Dump captures traffic on a per session basis. A sampling ratio can be set via plugin.config or traffic_ctl to dump
  one out of n sessions. The dump file schema can be found
  https://github.com/apache/trafficserver/tree/master/tests/tools/lib/replay_schema.json
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

#include <inttypes.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <getopt.h>
#include <unistd.h>

#include <sys/types.h>
#include <fcntl.h>
#include <errno.h>
#include <arpa/inet.h>
#include <netinet/in.h>

#include <sstream>
#include <iomanip>
#include <chrono>
#include <atomic>
#include <string>
#include <string_view>

#include "tscore/ts_file.h"
#include "ts/ts.h"

namespace
{
const char *PLUGIN_NAME   = "traffic_dump";
const std::string closing = "]}]}";

ts::file::path log_path{"dump"};               // default log directory
int s_arg_idx = 0;                             // Session Arg Index to pass on session data
std::atomic<int64_t> sample_pool_size(1000);   // Sampling ratio
std::atomic<int64_t> max_disk_usage(10000000); //< Max disk space for logs (approximate)
std::atomic<int64_t> disk_usage(0);            //< Actual disk usage
// handler declaration
int session_aio_handler(TSCont contp, TSEvent event, void *edata);
int session_txn_handler(TSCont contp, TSEvent event, void *edata);

/// Custom structure for per session data
struct SsnData {
  int log_fd           = -1;    //< Log file descriptor
  int aio_count        = 0;     //< Active AIO counts
  int64_t write_offset = 0;     //< AIO write offset
  bool first           = true;  //< First Transaction
  bool ssn_closed      = false; //< Session closed flag
  ts::file::path log_name;      //< Log file path

  TSCont aio_cont       = nullptr; //< AIO callback
  TSCont txn_cont       = nullptr; //< Transaction callback
  TSMutex disk_io_mutex = nullptr; //< AIO mutex

  SsnData()
  {
    disk_io_mutex = TSMutexCreate();
    aio_cont      = TSContCreate(session_aio_handler, TSMutexCreate());
    txn_cont      = TSContCreate(session_txn_handler, nullptr);
  }

  ~SsnData()
  {
    if (disk_io_mutex) {
      TSMutexDestroy(disk_io_mutex);
    }
    if (aio_cont) {
      TSContDestroy(aio_cont);
    }
    if (txn_cont) {
      TSContDestroy(txn_cont);
    }
  }

  /// write_to_disk(): Takes a string object and writes to file via AIO
  int
  write_to_disk(const std::string &body)
  {
    TSMutexLock(disk_io_mutex);
    char *pBuf = nullptr;
    // Allocate a buffer for aio writing
    if ((pBuf = static_cast<char *>(TSmalloc(sizeof(char) * body.size())))) {
      memcpy(pBuf, body.c_str(), body.size());
      if (TS_SUCCESS == TSAIOWrite(log_fd, write_offset, pBuf, body.size(), aio_cont)) {
        // Update offset within file and aio events count
        write_offset += body.size();
        aio_count += 1;

        TSMutexUnlock(disk_io_mutex);
        return TS_SUCCESS;
      }
      TSfree(pBuf);
    }
    TSMutexUnlock(disk_io_mutex);
    return TS_ERROR;
  }
};

/// Local helper functions about json formatting
/// min_write(): Inline function for repeating code
inline void
min_write(const char *buf, int64_t &prevIdx, int64_t &idx, std::ostream &jsonfile)
{
  if (prevIdx < idx) {
    jsonfile.write(buf + prevIdx, idx - prevIdx);
  }
  prevIdx = idx + 1;
}

/// esc_json_out(): Escape characters in a buffer and output to ofstream object
///                 in a way to minimize ofstream operations
int
esc_json_out(const char *buf, int64_t len, std::ostream &jsonfile)
{
  if (buf == nullptr)
    return 0;
  int64_t idx, prevIdx = 0;
  for (idx = 0; idx < len; idx++) {
    char c = *(buf + idx);
    switch (c) {
    case '"':
    case '\\': {
      min_write(buf, prevIdx, idx, jsonfile);
      jsonfile << "\\" << c;
      break;
    }
    case '\b': {
      min_write(buf, prevIdx, idx, jsonfile);
      jsonfile << "\\b";
      break;
    }
    case '\f': {
      min_write(buf, prevIdx, idx, jsonfile);
      jsonfile << "\\f";
      break;
    }
    case '\n': {
      min_write(buf, prevIdx, idx, jsonfile);
      jsonfile << "\\n";
      break;
    }
    case '\r': {
      min_write(buf, prevIdx, idx, jsonfile);
      jsonfile << "\\r";
      break;
    }
    case '\t': {
      min_write(buf, prevIdx, idx, jsonfile);
      jsonfile << "\\t";
      break;
    }
    default: {
      if ('\x00' <= c && c <= '\x1f') {
        min_write(buf, prevIdx, idx, jsonfile);
        jsonfile << "\\u" << std::hex << std::setw(4) << std::setfill('0') << (int)c;
      }
      break;
    }
    }
  }
  min_write(buf, prevIdx, idx, jsonfile);

  return len;
}

/// escape_json(): escape chars in a string and returns json string
std::string
escape_json(std::string const &s)
{
  std::ostringstream o;
  esc_json_out(s.c_str(), s.length(), o);
  return o.str();
}
std::string
escape_json(const char *buf, int64_t size)
{
  std::ostringstream o;
  esc_json_out(buf, size, o);
  return o.str();
}

inline std::string
json_entry(std::string const &name, const char *buf, int64_t size)
{
  return "\"" + escape_json(name) + "\":\"" + escape_json(buf, size) + "\"";
}

/// json_entry_array(): Formats to array-style entry i.e. ["field","value"]
inline std::string
json_entry_array(const char *name, int name_len, const char *value, int value_len)
{
  return "[\"" + escape_json(name, name_len) + "\", \"" + escape_json(value, value_len) + "\"]";
}

/// Helper functions to collect txn information from TSMBuffer
std::string
collect_headers(TSMBuffer &buffer, TSMLoc &hdr_loc, int64_t body_bytes)
{
  std::string result = "{";
  int len            = 0;
  const char *cp     = nullptr;
  TSMLoc url_loc     = nullptr;

  // Log scheme+method or status+reason based on header type
  if (TSHttpHdrTypeGet(buffer, hdr_loc) == TS_HTTP_TYPE_REQUEST) {
    // 1. "version"
    int version = TSHttpHdrVersionGet(buffer, hdr_loc);
    result += R"("version":")" + std::to_string(TS_HTTP_MAJOR(version)) + "." + std::to_string(TS_HTTP_MINOR(version)) + '"';

    // 2. "scheme":
    TSAssert(TS_SUCCESS == TSHttpHdrUrlGet(buffer, hdr_loc, &url_loc));
    cp = TSUrlSchemeGet(buffer, url_loc, &len);
    TSDebug(PLUGIN_NAME, "collect_headers(): found scheme %d ", len);
    result += "," + json_entry("scheme", cp, len);

    // 3. "method":(string)
    cp = TSHttpHdrMethodGet(buffer, hdr_loc, &len);
    result += "," + json_entry("method", cp, len);

    // 4. "url"
    cp = TSUrlStringGet(buffer, url_loc, &len);
    TSDebug(PLUGIN_NAME, "collect_headers(): found url %.*s", len, cp);
    result += "," + json_entry("url", cp, len);
    TSHandleMLocRelease(buffer, hdr_loc, url_loc);
  } else {
    // 1. "status":(string)
    result += R"("status":)" + std::to_string(TSHttpHdrStatusGet(buffer, hdr_loc));
    // 2. "reason":(string)
    cp = TSHttpHdrReasonGet(buffer, hdr_loc, &len);
    result += "," + json_entry("reason", cp, len);
    // 3. "encoding"
  }

  // "content"
  //    "encoding"
  //    "size"
  result += R"(,"content":{"encoding":"plain","size":)" + std::to_string(body_bytes) + '}';

  // "headers": [[name(string), value(string)]]
  result += R"(,"headers":{"encoding":"esc_json", "fields": [)";
  TSMLoc field_loc = TSMimeHdrFieldGet(buffer, hdr_loc, 0);
  while (field_loc) {
    TSMLoc next_field_loc;
    const char *name;
    const char *value;
    int name_len, value_len;
    // Append to "fields" list if valid value exists
    if ((name = TSMimeHdrFieldNameGet(buffer, hdr_loc, field_loc, &name_len)) && name_len) {
      value = TSMimeHdrFieldValueStringGet(buffer, hdr_loc, field_loc, -1, &value_len);
      result += json_entry_array(name, name_len, value, value_len);
    }

    next_field_loc = TSMimeHdrFieldNext(buffer, hdr_loc, field_loc);
    TSHandleMLocRelease(buffer, hdr_loc, field_loc);
    if ((field_loc = next_field_loc) != nullptr) {
      result += ",";
    }
  }

  return result + "]}}";
}

// Per session AIO handler: update AIO counts and clean up
int
session_aio_handler(TSCont contp, TSEvent event, void *edata)
{
  switch (event) {
  case TS_EVENT_AIO_DONE: {
    TSAIOCallback cb = static_cast<TSAIOCallback>(edata);
    SsnData *ssnData = static_cast<SsnData *>(TSContDataGet(contp));
    if (!ssnData) {
      TSDebug(PLUGIN_NAME, "session_aio_handler(): No valid ssnData. Abort.");
      return TS_ERROR;
    }
    char *buf = TSAIOBufGet(cb);
    TSMutexLock(ssnData->disk_io_mutex);

    // Free the allocated buffer and update aio_count
    if (buf) {
      TSfree(buf);
      if (--ssnData->aio_count == 0 && ssnData->ssn_closed) {
        // check for ssn close, if closed, do clean up
        TSContDataSet(contp, nullptr);
        close(ssnData->log_fd);
        TSMutexUnlock(ssnData->disk_io_mutex);
        std::error_code ec;
        ts::file::file_status st = ts::file::status(ssnData->log_name, ec);
        if (!ec) {
          disk_usage += ts::file::file_size(st);
          TSDebug(PLUGIN_NAME, "Finish a session with log file of %" PRIuMAX "bytes", ts::file::file_size(st));
        }
        delete ssnData;
        return TS_SUCCESS;
      }
    }
    TSMutexUnlock(ssnData->disk_io_mutex);
    return TS_SUCCESS;
  }
  default:
    TSDebug(PLUGIN_NAME, "session_aio_handler(): unhandled events %d", event);
    return TS_ERROR;
  }
  return TS_SUCCESS;
}

// Transaction handler: writes headers to the log file using AIO
int
session_txn_handler(TSCont contp, TSEvent event, void *edata)
{
  TSHttpTxn txnp = static_cast<TSHttpTxn>(edata);

  // Retrieve SsnData
  TSHttpSsn ssnp   = TSHttpTxnSsnGet(txnp);
  SsnData *ssnData = static_cast<SsnData *>(TSHttpSsnArgGet(ssnp, s_arg_idx));

  // If no valid ssnData, continue transaction as if nothing happened
  if (!ssnData) {
    TSDebug(PLUGIN_NAME, "session_txn_handler(): No ssnData found. Abort.");
    TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
    return TS_SUCCESS;
  }

  switch (event) {
  case TS_EVENT_HTTP_TXN_CLOSE: {
    // Get UUID
    char uuid[TS_CRUUID_STRING_LEN + 1];
    TSAssert(TS_SUCCESS == TSClientRequestUuidGet(txnp, uuid));

    // Generate per transaction json records
    std::string txn_info;
    if (!ssnData->first) {
      txn_info += ",";
    }
    ssnData->first = false;

    // "uuid":(string)
    txn_info += "{" + json_entry("uuid", uuid, strlen(uuid));

    // "connect-time":(number)
    TSHRTime start_time;
    TSHttpTxnMilestoneGet(txnp, TS_MILESTONE_UA_BEGIN, &start_time);
    txn_info += ",\"start-time\":" + std::to_string(start_time);

    // client/proxy-request/response headers
    TSMBuffer buffer;
    TSMLoc hdr_loc;
    if (TS_SUCCESS == TSHttpTxnClientReqGet(txnp, &buffer, &hdr_loc)) {
      TSDebug(PLUGIN_NAME, "Found client request");
      txn_info += R"(,"client-request":)" + collect_headers(buffer, hdr_loc, TSHttpTxnClientReqBodyBytesGet(txnp));
      TSHandleMLocRelease(buffer, TS_NULL_MLOC, hdr_loc);
      buffer = nullptr;
    }
    if (TS_SUCCESS == TSHttpTxnServerReqGet(txnp, &buffer, &hdr_loc)) {
      TSDebug(PLUGIN_NAME, "Found proxy request");
      txn_info += R"(,"proxy-request":)" + collect_headers(buffer, hdr_loc, TSHttpTxnServerReqBodyBytesGet(txnp));
      TSHandleMLocRelease(buffer, TS_NULL_MLOC, hdr_loc);
      buffer = nullptr;
    }
    if (TS_SUCCESS == TSHttpTxnServerRespGet(txnp, &buffer, &hdr_loc)) {
      TSDebug(PLUGIN_NAME, "Found server response");
      txn_info += R"(,"server-response":)" + collect_headers(buffer, hdr_loc, TSHttpTxnServerRespBodyBytesGet(txnp));
      TSHandleMLocRelease(buffer, TS_NULL_MLOC, hdr_loc);
      buffer = nullptr;
    }
    if (TS_SUCCESS == TSHttpTxnClientRespGet(txnp, &buffer, &hdr_loc)) {
      TSDebug(PLUGIN_NAME, "Found proxy response");
      txn_info += R"(,"proxy-response":)" + collect_headers(buffer, hdr_loc, TSHttpTxnClientRespBodyBytesGet(txnp));
      TSHandleMLocRelease(buffer, TS_NULL_MLOC, hdr_loc);
      buffer = nullptr;
    }

    txn_info += "}";
    ssnData->write_to_disk(txn_info);
    break;
  }
  default:
    TSDebug(PLUGIN_NAME, "session_txn_handler(): Unhandled events %d", event);
    TSHttpTxnReenable(txnp, TS_EVENT_HTTP_ERROR);
    return TS_ERROR;
  }

  TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
  return TS_SUCCESS;
}

// Session handler for global hooks; Assign per-session data structure and log files
static int
global_ssn_handler(TSCont contp, TSEvent event, void *edata)
{
  TSHttpSsn ssnp = static_cast<TSHttpSsn>(edata);

  switch (event) {
  // Also handles LIFECYCLE_MSG from traffic_ctl
  case TS_EVENT_LIFECYCLE_MSG: {
    TSPluginMsg *msg = static_cast<TSPluginMsg *>(edata);
    // String view of plugin message prefix
    static constexpr std::string_view PLUGIN_PREFIX("traffic_dump."_sv);

    std::string_view tag(msg->tag, strlen(msg->tag));

    if (tag.substr(0, PLUGIN_PREFIX.size()) == PLUGIN_PREFIX) {
      tag.remove_prefix(PLUGIN_PREFIX.size());
      if (tag == "sample") {
        sample_pool_size = static_cast<int64_t>(strtol(static_cast<const char *>(msg->data), nullptr, 0));
        TSDebug(PLUGIN_NAME, "TS_EVENT_LIFECYCLE_MSG: Received Msg to change sample size to %" PRId64 "bytes",
                sample_pool_size.load());
      } else if (tag == "reset") {
        disk_usage = 0;
        TSDebug(PLUGIN_NAME, "TS_EVENT_LIFECYCLE_MSG: Received Msg to reset disk usage counter");
      } else if (tag == "limit") {
        max_disk_usage = static_cast<int64_t>(strtol(static_cast<const char *>(msg->data), nullptr, 0));
        TSDebug(PLUGIN_NAME, "TS_EVENT_LIFECYCLE_MSG: Received Msg to change max disk usage to %" PRId64 "bytes",
                max_disk_usage.load());
      }
    }
    return TS_SUCCESS;
  }
  case TS_EVENT_HTTP_SSN_START: {
    // Grab session id to do sampling
    int64_t id = TSHttpSsnIdGet(ssnp);
    if (id % sample_pool_size != 0) {
      TSDebug(PLUGIN_NAME, "global_ssn_handler(): Ignore session %" PRId64 "...", id);
      break;
    } else if (disk_usage >= max_disk_usage) {
      TSDebug(PLUGIN_NAME, "global_ssn_handler(): Ignore session %" PRId64 "due to disk usage %" PRId64 "bytes", id,
              disk_usage.load());
      break;
    }
    // Beginning of a new session
    /// Get epoch time
    auto start = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch());

    // Create new per session data
    SsnData *ssnData = new SsnData;
    TSHttpSsnArgSet(ssnp, s_arg_idx, ssnData);

    TSContDataSet(ssnData->aio_cont, ssnData);

    // 1. "protocol":(string)
    const char *protocol[10];
    int count = 0;
    TSAssert(TS_SUCCESS == TSHttpSsnClientProtocolStackGet(ssnp, 10, protocol, &count));
    std::string result;
    for (int i = 0; i < count; i++) {
      if (i > 0) {
        result += ",";
      }
      result += '"' + std::string(protocol[i]) + '"';
    }

    std::string beginning = R"({"meta":{"version":"1.0"},"sessions":[{"protocol":[)" + result + "]" + R"(,"connection-time":)" +
                            std::to_string(start.count()) + R"(,"transactions":[)";

    // Grab session id and use its hex string as fname
    std::stringstream stream;
    stream << std::setw(16) << std::setfill('0') << std::hex << id;
    std::string session_id = stream.str();

    // Use client ip as sub directory name
    char client_str[INET6_ADDRSTRLEN];
    const sockaddr *client_ip = TSHttpSsnClientAddrGet(ssnp);
    if (AF_INET == client_ip->sa_family) {
      inet_ntop(AF_INET, &(reinterpret_cast<const sockaddr_in *>(client_ip)->sin_addr), client_str, INET_ADDRSTRLEN);
    } else if (AF_INET6 == client_ip->sa_family) {
      inet_ntop(AF_INET6, &(reinterpret_cast<const sockaddr_in6 *>(client_ip)->sin6_addr), client_str, INET6_ADDRSTRLEN);
    } else {
      TSDebug(PLUGIN_NAME, "global_ssn_handler(): Unknown address family.");
      snprintf(client_str, INET6_ADDRSTRLEN, "unknown");
    }

    // Initialize AIO file
    TSMutexLock(ssnData->disk_io_mutex);
    if (ssnData->log_fd < 0) {
      ts::file::path log_p = log_path / ts::file::path(std::string(client_str, 3));
      ts::file::path log_f = log_p / ts::file::path(session_id);

      // Create subdir if not existing
      std::error_code ec;
      ts::file::status(log_p, ec);
      if (ec && mkdir(log_p.c_str(), 0755) == -1) {
        TSDebug(PLUGIN_NAME, "global_ssn_handler(): Failed to create dir %s", log_p.c_str());
        TSError("[%s] Failed to create dir %s", PLUGIN_NAME, log_p.c_str());
      }

      // Try to open log files for AIO
      ssnData->log_fd = open(log_f.c_str(), O_RDWR | O_CREAT, S_IRWXU);
      if (ssnData->log_fd < 0) {
        TSMutexUnlock(ssnData->disk_io_mutex);
        TSDebug(PLUGIN_NAME, "global_ssn_handler(): Failed to open log files %s. Abort.", log_f.c_str());
        TSHttpSsnReenable(ssnp, TS_EVENT_HTTP_CONTINUE);
        return TS_EVENT_HTTP_CONTINUE;
      }
      ssnData->log_name = log_f;
      // Write log file beginning to disk
      ssnData->write_to_disk(beginning);
    }
    TSMutexUnlock(ssnData->disk_io_mutex);

    TSHttpSsnHookAdd(ssnp, TS_HTTP_TXN_CLOSE_HOOK, ssnData->txn_cont);
    break;
  }
  case TS_EVENT_HTTP_SSN_CLOSE: {
    // Write session and log file closing
    int64_t id = TSHttpSsnIdGet(ssnp);
    TSDebug(PLUGIN_NAME, "global_ssn_handler(): Closing session %" PRId64 "...", id);
    // Retrieve SsnData
    SsnData *ssnData = static_cast<SsnData *>(TSHttpSsnArgGet(ssnp, s_arg_idx));
    // If no valid ssnData, continue transaction as if nothing happened
    if (!ssnData) {
      TSDebug(PLUGIN_NAME, "global_ssn_handler(): [TS_EVENT_HTTP_SSN_CLOSE] No ssnData found. Abort.");
      TSHttpSsnReenable(ssnp, TS_EVENT_HTTP_CONTINUE);
      return TS_SUCCESS;
    }
    ssnData->write_to_disk(closing);
    TSMutexLock(ssnData->disk_io_mutex);
    ssnData->ssn_closed = true;
    TSMutexUnlock(ssnData->disk_io_mutex);

    break;
  }
  default:
    break;
  }
  TSHttpSsnReenable(ssnp, TS_EVENT_HTTP_CONTINUE);
  return TS_SUCCESS;
}

} // End of anonymous namespace

void
TSPluginInit(int argc, const char *argv[])
{
  TSDebug(PLUGIN_NAME, "initializing plugin");
  TSPluginRegistrationInfo info;

  info.plugin_name   = "traffic_dump";
  info.vendor_name   = "Apache Software Foundation";
  info.support_email = "dev@trafficserver.apache.org";

  /// Commandline options
  static const struct option longopts[] = {{"logdir", required_argument, nullptr, 'l'},
                                           {"sample", required_argument, nullptr, 's'},
                                           {"limit", required_argument, nullptr, 'm'},
                                           {nullptr, no_argument, nullptr, 0}};
  int opt                               = 0;
  while (opt >= 0) {
    opt = getopt_long(argc, (char *const *)argv, "l:", longopts, nullptr);
    switch (opt) {
    case 'l': {
      log_path = ts::file::path{optarg};
      break;
    }
    case 's': {
      sample_pool_size = static_cast<int64_t>(std::strtol(optarg, nullptr, 0));
      break;
    }
    case 'm': {
      max_disk_usage = static_cast<int64_t>(std::strtol(optarg, nullptr, 0));
    }
    case -1:
    case '?':
      break;

    default:
      TSDebug(PLUGIN_NAME, "Unexpected options.");
      TSError("[%s] Unexpected options error.", PLUGIN_NAME);
      return;
    }
  }

  // Make absolute path if not
  if (!log_path.is_absolute()) {
    log_path = ts::file::path(TSInstallDirGet()) / log_path;
  }
  TSDebug(PLUGIN_NAME, "Initialized with log directory: %s", log_path.c_str());

  if (TS_SUCCESS != TSPluginRegister(&info)) {
    TSError("[%s] Unable to initialize plugin (disabled). Failed to register plugin.", PLUGIN_NAME);
  } else if (TS_SUCCESS != TSHttpSsnArgIndexReserve(PLUGIN_NAME, "Track log related data", &s_arg_idx)) {
    TSError("[%s] Unable to initialize plugin (disabled). Failed to reserve ssn arg.", PLUGIN_NAME);
  } else {
    /// Add global hooks
    TSCont ssncont = TSContCreate(global_ssn_handler, nullptr);
    TSHttpHookAdd(TS_HTTP_SSN_START_HOOK, ssncont);
    TSHttpHookAdd(TS_HTTP_SSN_CLOSE_HOOK, ssncont);
    TSLifecycleHookAdd(TS_LIFECYCLE_MSG_HOOK, ssncont);
    TSDebug(PLUGIN_NAME, "Initialized with sample pool size %" PRId64 " bytes and disk limit %" PRId64 "bytes",
            sample_pool_size.load(), max_disk_usage.load());
  }

  return;
}
