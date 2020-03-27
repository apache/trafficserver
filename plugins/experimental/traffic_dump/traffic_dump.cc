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

#include <cinttypes>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <getopt.h>
#include <unistd.h>

#include <sys/types.h>
#include <fcntl.h>
#include <cerrno>
#include <arpa/inet.h>
#include <netinet/in.h>

#include <algorithm>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <atomic>
#include <string>
#include <string_view>
#include <unordered_set>

#include "tscore/ts_file.h"
#include "tscpp/util/TextView.h"
#include "ts/ts.h"

namespace
{
const char *PLUGIN_NAME   = "traffic_dump";
const std::string closing = "]}]}";
std::string defaut_sensitive_field_value;

// A case-insensitive comparitor used for comparing HTTP field names.
struct InsensitiveCompare {
  bool
  operator()(std::string_view a, std::string_view b) const
  {
    return strcasecmp(a, b) == 0;
  }
};

struct StringHashByLower {
public:
  size_t
  operator()(const std::string &str) const
  {
    std::string lower;
    std::transform(str.begin(), str.end(), lower.begin(), [](unsigned char c) -> unsigned char { return std::tolower(c); });
    return std::hash<std::string>()(lower);
  }
};

/// Fields considered sensitive because they may contain user-private
/// information. These fields are replaced with auto-generated generic content
/// by default. To turn off this behavior, the user should add the
/// --promiscuous-mode flag as a commandline argument.
///
/// While these are specified with case, they are matched case-insensitively.
std::unordered_set<std::string, StringHashByLower, InsensitiveCompare> default_sensitive_fields = {
  "Set-Cookie",
  "Cookie",
};

/// The set of fields, default and user-specified, that are sensitive and whose
/// values will be replaced with auto-generated generic content.
std::unordered_set<std::string, StringHashByLower, InsensitiveCompare> sensitive_fields;

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
  if (buf == nullptr) {
    return 0;
  }
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
        jsonfile << "\\u" << std::hex << std::setw(4) << std::setfill('0') << static_cast<int>(c);
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
escape_json(std::string_view s)
{
  std::ostringstream o;
  esc_json_out(s.data(), s.length(), o);
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
json_entry(std::string const &name, const char *value, int64_t size)
{
  return "\"" + escape_json(name) + "\":\"" + escape_json(value, size) + "\"";
}

/// json_entry_array(): Formats to array-style entry i.e. ["field","value"]
inline std::string
json_entry_array(std::string_view name, std::string_view value)
{
  return "[\"" + escape_json(name) + "\", \"" + escape_json(value) + "\"]";
}

/** Remove the scheme prefix from the url.
 *
 * @return The view without the scheme prefix.
 */
std::string_view
remove_scheme_prefix(std::string_view url)
{
  const auto scheme_separator = url.find("://");
  if (scheme_separator == std::string::npos) {
    return url;
  }
  url.remove_prefix(scheme_separator + 3);
  return url;
}

/// Write the content node.
//
/// "content"
///    "encoding"
///    "size"
std::string
write_content_node(int64_t num_body_bytes)
{
  return std::string(R"(,"content":{"encoding":"plain","size":)" + std::to_string(num_body_bytes) + '}');
}

/** Initialize the generic sensitive field to be dumped. This is used instead
 * of the sensitive field values seen on the wire.
 */
void
initialize_default_sensitive_field()
{
  // 128 KB is the maximum size supported for all headers, so this size should
  // be plenty large for our needs.
  constexpr size_t default_field_size = 128 * 1024;
  defaut_sensitive_field_value.resize(default_field_size);

  char *field_buffer = defaut_sensitive_field_value.data();
  for (auto i = 0u; i < default_field_size; i += 8) {
    sprintf(field_buffer, "%07x ", i / 8);
    field_buffer += 8;
  }
}

/** Inspect the field to see whether it is sensitive and return a generic value
 * of equal size to the original if it is.
 *
 * @param[in] name The field name to inspect.
 * @param[in] original_value The field value to inspect.
 *
 * @return The value traffic_dump should dump for the given field.
 */
std::string_view
replace_sensitive_fields(std::string_view name, std::string_view original_value)
{
  auto search = sensitive_fields.find(std::string(name));
  if (search == sensitive_fields.end()) {
    return original_value;
  }
  auto new_value_size = original_value.size();
  if (original_value.size() > defaut_sensitive_field_value.size()) {
    new_value_size = defaut_sensitive_field_value.size();
    TSError("[%s] Encountered a sensitive field value larger than our default "
            "field size. Default size: %zu, incoming field size: %zu",
            PLUGIN_NAME, defaut_sensitive_field_value.size(), original_value.size());
  }
  return std::string_view{defaut_sensitive_field_value.data(), new_value_size};
}

/// Read the txn information from TSMBuffer and write the header information.
/// This function does not write the content node.
std::string
write_message_node_no_content(TSMBuffer &buffer, TSMLoc &hdr_loc)
{
  std::string result = "{";
  int len            = 0;
  const char *cp     = nullptr;
  TSMLoc url_loc     = nullptr;

  // Log scheme+method+request-target or status+reason based on header type
  if (TSHttpHdrTypeGet(buffer, hdr_loc) == TS_HTTP_TYPE_REQUEST) {
    // 1. "version"
    int version = TSHttpHdrVersionGet(buffer, hdr_loc);
    result += R"("version":")" + std::to_string(TS_HTTP_MAJOR(version)) + "." + std::to_string(TS_HTTP_MINOR(version)) + '"';

    TSAssert(TS_SUCCESS == TSHttpHdrUrlGet(buffer, hdr_loc, &url_loc));
    // 2. "scheme":
    cp = TSUrlSchemeGet(buffer, url_loc, &len);
    TSDebug(PLUGIN_NAME, "write_message_node(): found scheme %.*s ", len, cp);
    result += "," + json_entry("scheme", cp, len);

    // 3. "method":(string)
    cp = TSHttpHdrMethodGet(buffer, hdr_loc, &len);
    TSDebug(PLUGIN_NAME, "write_message_node(): found method %.*s ", len, cp);
    result += "," + json_entry("method", cp, len);

    // 4. "url"
    cp = TSUrlHostGet(buffer, url_loc, &len);
    std::string_view host{cp, static_cast<size_t>(len)};

    char *url = TSUrlStringGet(buffer, url_loc, &len);
    std::string_view url_string{url, static_cast<size_t>(len)};

    if (host.empty()) {
      // TSUrlStringGet will add the scheme to the URL, even if the request
      // target doesn't contain it. However, we cannot just always remove the
      // scheme because the original request target may include it. We assume
      // here that a URL with a scheme but not a host is artificial and thus
      // we remove it.
      url_string = remove_scheme_prefix(url_string);
    }

    TSDebug(PLUGIN_NAME, "write_message_node(): found host target %.*s", static_cast<int>(url_string.size()), url_string.data());
    result += "," + json_entry("url", url_string.data(), url_string.size());
    TSfree(url);
    TSHandleMLocRelease(buffer, hdr_loc, url_loc);
  } else {
    // 1. "status":(string)
    result += R"("status":)" + std::to_string(TSHttpHdrStatusGet(buffer, hdr_loc));
    // 2. "reason":(string)
    cp = TSHttpHdrReasonGet(buffer, hdr_loc, &len);
    result += "," + json_entry("reason", cp, len);
    // 3. "encoding"
  }

  // "headers": [[name(string), value(string)]]
  result += R"(,"headers":{"encoding":"esc_json", "fields": [)";
  TSMLoc field_loc = TSMimeHdrFieldGet(buffer, hdr_loc, 0);
  while (field_loc) {
    TSMLoc next_field_loc;
    const char *name  = nullptr;
    const char *value = nullptr;
    int name_len = 0, value_len = 0;
    // Append to "fields" list if valid value exists
    if ((name = TSMimeHdrFieldNameGet(buffer, hdr_loc, field_loc, &name_len)) && name_len) {
      std::string_view name_view{name, static_cast<size_t>(name_len)};
      value = TSMimeHdrFieldValueStringGet(buffer, hdr_loc, field_loc, -1, &value_len);
      std::string_view value_view{value, static_cast<size_t>(value_len)};
      std::string_view new_value = replace_sensitive_fields(name_view, value_view);
      result += json_entry_array(name_view, new_value);
    }

    next_field_loc = TSMimeHdrFieldNext(buffer, hdr_loc, field_loc);
    TSHandleMLocRelease(buffer, hdr_loc, field_loc);
    if ((field_loc = next_field_loc) != nullptr) {
      result += ",";
    }
  }
  return result += "]}";
}

/// Read the txn information from TSMBuffer and write the header information including
/// the content node describing the body characteristics.
std::string
write_message_node(TSMBuffer &buffer, TSMLoc &hdr_loc, int64_t num_body_bytes)
{
  std::string result = write_message_node_no_content(buffer, hdr_loc);
  result += write_content_node(num_body_bytes);
  return result + "}";
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
  SsnData *ssnData = static_cast<SsnData *>(TSUserArgGet(ssnp, s_arg_idx));

  // If no valid ssnData, continue transaction as if nothing happened
  if (!ssnData) {
    TSDebug(PLUGIN_NAME, "session_txn_handler(): No ssnData found. Abort.");
    TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
    return TS_SUCCESS;
  }

  std::string txn_info;
  switch (event) {
  case TS_EVENT_HTTP_TXN_START: {
    // Get UUID
    char uuid[TS_CRUUID_STRING_LEN + 1];
    TSAssert(TS_SUCCESS == TSClientRequestUuidGet(txnp, uuid));
    std::string_view uuid_view{uuid, strnlen(uuid, TS_CRUUID_STRING_LEN)};

    // Generate per transaction json records
    if (!ssnData->first) {
      txn_info += ",";
    }
    ssnData->first = false;

    // "uuid":(string)
    txn_info += "{";
    // "connection-time":(number)
    TSHRTime start_time;
    TSHttpTxnMilestoneGet(txnp, TS_MILESTONE_UA_BEGIN, &start_time);
    txn_info += "\"connection-time\":" + std::to_string(start_time);

    // The uuid is a header field for each message in the transaction. Use the
    // "all" node to apply to each message.
    std::string_view name = "uuid";
    txn_info += ",\"all\":{\"headers\":{\"fields\":[" + json_entry_array(name, uuid_view);
    txn_info += "]}}";
    ssnData->write_to_disk(txn_info);
    break;
  }

  case TS_EVENT_HTTP_READ_REQUEST_HDR: {
    // This hook is registered globally, not at TS_EVENT_HTTP_SSN_START in
    // global_ssn_handler(). As such, this handler will be called with every
    // transaction. However, we know that we are dumping this transaction
    // because there is a ssnData associated with it.

    // We must grab the client request information before remap happens because
    // the remap process modifies the request buffer.
    TSMBuffer buffer;
    TSMLoc hdr_loc;
    if (TS_SUCCESS == TSHttpTxnClientReqGet(txnp, &buffer, &hdr_loc)) {
      TSDebug(PLUGIN_NAME, "Found client request");
      // We don't have an accurate view of the body size until TXN_CLOSE so we hold
      // off on writing the content:size node until then.
      txn_info += R"(,"client-request":)" + write_message_node_no_content(buffer, hdr_loc);
      TSHandleMLocRelease(buffer, TS_NULL_MLOC, hdr_loc);
      buffer = nullptr;
    }
    ssnData->write_to_disk(txn_info);
    break;
  }

  case TS_EVENT_HTTP_TXN_CLOSE: {
    // proxy-request/response headers
    TSMBuffer buffer;
    TSMLoc hdr_loc;
    if (TS_SUCCESS == TSHttpTxnClientReqGet(txnp, &buffer, &hdr_loc)) {
      txn_info += write_content_node(TSHttpTxnClientReqBodyBytesGet(txnp)) + "}";
      TSHandleMLocRelease(buffer, TS_NULL_MLOC, hdr_loc);
      buffer = nullptr;
    }
    if (TS_SUCCESS == TSHttpTxnServerReqGet(txnp, &buffer, &hdr_loc)) {
      TSDebug(PLUGIN_NAME, "Found proxy request");
      txn_info += R"(,"proxy-request":)" + write_message_node(buffer, hdr_loc, TSHttpTxnServerReqBodyBytesGet(txnp));
      TSHandleMLocRelease(buffer, TS_NULL_MLOC, hdr_loc);
      buffer = nullptr;
    }
    if (TS_SUCCESS == TSHttpTxnServerRespGet(txnp, &buffer, &hdr_loc)) {
      TSDebug(PLUGIN_NAME, "Found server response");
      txn_info += R"(,"server-response":)" + write_message_node(buffer, hdr_loc, TSHttpTxnServerRespBodyBytesGet(txnp));
      TSHandleMLocRelease(buffer, TS_NULL_MLOC, hdr_loc);
      buffer = nullptr;
    }
    if (TS_SUCCESS == TSHttpTxnClientRespGet(txnp, &buffer, &hdr_loc)) {
      TSDebug(PLUGIN_NAME, "Found proxy response");
      txn_info += R"(,"proxy-response":)" + write_message_node(buffer, hdr_loc, TSHttpTxnClientRespBodyBytesGet(txnp));
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
    TSUserArgSet(ssnp, s_arg_idx, ssnData);

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

    TSHttpSsnHookAdd(ssnp, TS_HTTP_TXN_START_HOOK, ssnData->txn_cont);
    TSHttpSsnHookAdd(ssnp, TS_HTTP_TXN_CLOSE_HOOK, ssnData->txn_cont);
    break;
  }
  case TS_EVENT_HTTP_SSN_CLOSE: {
    // Write session and log file closing
    int64_t id = TSHttpSsnIdGet(ssnp);
    TSDebug(PLUGIN_NAME, "global_ssn_handler(): Closing session %" PRId64 "...", id);
    // Retrieve SsnData
    SsnData *ssnData = static_cast<SsnData *>(TSUserArgGet(ssnp, s_arg_idx));
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

  bool sensitive_fields_were_specified = false;
  /// Commandline options
  static const struct option longopts[] = {{"logdir", required_argument, nullptr, 'l'},
                                           {"sample", required_argument, nullptr, 's'},
                                           {"limit", required_argument, nullptr, 'm'},
                                           {"sensitive-fields", required_argument, nullptr, 'f'},
                                           {nullptr, no_argument, nullptr, 0}};
  int opt                               = 0;
  while (opt >= 0) {
    opt = getopt_long(argc, const_cast<char *const *>(argv), "l:", longopts, nullptr);
    switch (opt) {
    case 'f': {
      // --sensitive-fields takes a comma-separated list of HTTP fields that
      // are sensitive.  The field values for these fields will be replaced
      // with generic traffic_dump generated data.
      //
      // If this option is not used, then the default values in
      // default_sensitive_fields is used. If this option is used, then it
      // replaced the default sensitive fields with the user-supplied list of
      // sensitive fields.
      sensitive_fields_were_specified = true;
      ts::TextView input_filter_fields{std::string_view{optarg}};
      ts::TextView filter_field;
      while (!(filter_field = input_filter_fields.take_prefix_at(',')).empty()) {
        filter_field.trim_if(&isspace);
        if (filter_field.empty()) {
          continue;
        }
        sensitive_fields.emplace(filter_field);
      }
      break;
    }
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

  if (!sensitive_fields_were_specified) {
    // The user did not provide their own list of sensitive fields. Use the
    // default.
    sensitive_fields.merge(default_sensitive_fields);
  }

  std::string sensitive_fields_string;
  bool is_first = true;
  for (const auto &field : sensitive_fields) {
    if (!is_first) {
      sensitive_fields_string += ", ";
    }
    is_first = false;
    sensitive_fields_string += field;
  }
  TSDebug(PLUGIN_NAME, "Sensitive fields for which generic values will be dumped: %s", sensitive_fields_string.c_str());

  // Make absolute path if not
  if (!log_path.is_absolute()) {
    log_path = ts::file::path(TSInstallDirGet()) / log_path;
  }
  TSDebug(PLUGIN_NAME, "Initialized with log directory: %s", log_path.c_str());

  if (TS_SUCCESS != TSPluginRegister(&info)) {
    TSError("[%s] Unable to initialize plugin (disabled). Failed to register plugin.", PLUGIN_NAME);
  } else if (TS_SUCCESS != TSUserArgIndexReserve(TS_USER_ARGS_SSN, PLUGIN_NAME, "Track log related data", &s_arg_idx)) {
    TSError("[%s] Unable to initialize plugin (disabled). Failed to reserve ssn arg.", PLUGIN_NAME);
  } else {
    initialize_default_sensitive_field();

    /// Add global hooks
    TSCont ssncont = TSContCreate(global_ssn_handler, nullptr);
    TSHttpHookAdd(TS_HTTP_SSN_START_HOOK, ssncont);
    TSHttpHookAdd(TS_HTTP_SSN_CLOSE_HOOK, ssncont);

    // Register the collecting of client-request headers at the global level so
    // we can process requests before other plugins. (Global hooks are
    // processed before session and transaction ones.)
    TSCont txn_cont = TSContCreate(session_txn_handler, nullptr);
    TSHttpHookAdd(TS_HTTP_READ_REQUEST_HDR_HOOK, txn_cont);

    TSLifecycleHookAdd(TS_LIFECYCLE_MSG_HOOK, ssncont);
    TSDebug(PLUGIN_NAME, "Initialized with sample pool size %" PRId64 " bytes and disk limit %" PRId64 " bytes",
            sample_pool_size.load(), max_disk_usage.load());
  }

  return;
}
