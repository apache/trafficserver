/** @session_handler.h
  Traffic Dump session handling implementation
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

#include <arpa/inet.h>
#include <chrono>
#include <fcntl.h>
#include <iomanip>
#include <openssl/ssl.h>
#include <netinet/in.h>
#include <sstream>
#include <sys/stat.h>
#include <sys/types.h>

#include "session_data.h"
#include "global_variables.h"
#include "transaction_data.h"

namespace
{
/** The final string used to close a JSON session. */
char const constexpr *const json_closing = "]}]}";
} // namespace

namespace traffic_dump
{
// Static member initialization.
int SessionData::session_arg_index                 = -1;
std::atomic<int64_t> SessionData::sample_pool_size = default_sample_pool_size;
std::atomic<int64_t> SessionData::max_disk_usage   = default_max_disk_usage;
std::atomic<int64_t> SessionData::disk_usage       = 0;
ts::file::path SessionData::log_directory{default_log_directory};
uint64_t SessionData::session_counter = 0;
std::string SessionData::sni_filter;

int
SessionData::get_session_arg_index()
{
  return session_arg_index;
}

void
SessionData::set_sample_pool_size(int64_t new_sample_size)
{
  sample_pool_size = new_sample_size;
}

void
SessionData::reset_disk_usage()
{
  disk_usage = 0;
}

void
SessionData::set_max_disk_usage(int64_t new_max_disk_usage)
{
  max_disk_usage = new_max_disk_usage;
}

bool
SessionData::init(std::string_view log_directory, int64_t max_disk_usage, int64_t sample_size)
{
  SessionData::log_directory    = log_directory;
  SessionData::max_disk_usage   = max_disk_usage;
  SessionData::sample_pool_size = sample_size;

  if (TS_SUCCESS != TSUserArgIndexReserve(TS_USER_ARGS_SSN, debug_tag, "Track log related data", &session_arg_index)) {
    TSError("[%s] Unable to initialize plugin (disabled). Failed to reserve ssn arg.", traffic_dump::debug_tag);
    return false;
  }

  TSCont ssncont = TSContCreate(global_session_handler, nullptr);
  TSHttpHookAdd(TS_HTTP_SSN_START_HOOK, ssncont);
  TSHttpHookAdd(TS_HTTP_SSN_CLOSE_HOOK, ssncont);

  TSDebug(debug_tag, "Initialized with log directory: %s", SessionData::log_directory.c_str());
  TSDebug(debug_tag, "Initialized with sample pool size %" PRId64 " bytes and disk limit %" PRId64 " bytes", sample_size,
          max_disk_usage);
  return true;
}

bool
SessionData::init(std::string_view log_directory, int64_t max_disk_usage, int64_t sample_size, std::string_view sni_filter)
{
  if (!init(log_directory, max_disk_usage, sample_size)) {
    return false;
  }
  SessionData::sni_filter = sni_filter;
  TSDebug(debug_tag, "Filtering to only dump connections with SNI: %s", SessionData::sni_filter.c_str());
  return true;
}

/** Create a TLS characteristics node.
 *
 * This function encapsulates the logic common between the client-side and
 * server-side logic for populating the TLS node.
 *
 * @param[in] ssnp The pointer for this session.
 *
 * @return The node describing the TLS properties of this session.
 */
std::string
get_tls_description_helper(TSVConn ssn_vc)
{
  TSSslConnection ssl_conn = TSVConnSslConnectionGet(ssn_vc);
  SSL *ssl_obj             = reinterpret_cast<SSL *>(ssl_conn);
  if (ssl_obj == nullptr) {
    return "";
  }
  std::ostringstream tls_description;
  tls_description << R"("tls":{)";
  char const *sni_ptr = SSL_get_servername(ssl_obj, TLSEXT_NAMETYPE_host_name);
  if (sni_ptr != nullptr) {
    std::string_view sni{sni_ptr};
    if (!sni.empty()) {
      tls_description << R"("sni":")" << sni << R"(",)";
    }
  }
  tls_description << R"("verify_mode":")" << std::to_string(SSL_get_verify_mode(ssl_obj)) << R"(")";
  tls_description << "}";
  return tls_description.str();
}

/** Create a server-side TLS characteristics node.
 *
 * @param[in] ssnp The pointer for this session.
 *
 * @return The node describing the TLS properties of this session.
 */
std::string
get_server_tls_description(TSHttpSsn ssnp)
{
  TSVConn ssn_vc = TSHttpSsnServerVConnGet(ssnp);
  return get_tls_description_helper(ssn_vc);
}

/** Create a client-side TLS characteristics node.
 *
 * @param[in] ssnp The pointer for this session.
 *
 * @return The node describing the TLS properties of this session.
 */
std::string
get_client_tls_description(TSHttpSsn ssnp)
{
  TSVConn ssn_vc = TSHttpSsnClientVConnGet(ssnp);
  return get_tls_description_helper(ssn_vc);
}

/// A named boolean for callers who pass the is_client parameter.
constexpr bool IS_CLIENT = true;

/** Create the nodes that describe the session's sub-HTTP protocols.
 *
 * This function encapsulates the logic common between the client-side and
 * server-side logic for describing the session's characteristics.
 *
 * This will create the string representing the "protocol" and "tls" nodes. The
 * "tls" node will only be present if the connection is over SSL/TLS.
 *
 * @param[in] ssnp The pointer for this session.
 *
 * @return The description of the protocol stack and certain TLS attributes.
 */
std::string
get_protocol_description_helper(TSHttpSsn ssnp, bool is_client)
{
  std::ostringstream protocol_description;
  protocol_description << R"("protocol":[)";

  char const *protocol[10];
  int count = -1;
  if (is_client) {
    TSAssert(TS_SUCCESS == TSHttpSsnClientProtocolStackGet(ssnp, 10, protocol, &count));
  } else {
    // See the TODO below in the commented out defintion of get_server_protocol_description.
    // TSAssert(TS_SUCCESS == TSHttpSsnServerProtocolStackGet(ssnp, 10, protocol, &count));
  }
  for (int i = 0; i < count; i++) {
    if (i > 0) {
      protocol_description << ",";
    }
    protocol_description << '"' << std::string(protocol[i]) << '"';
  }

  protocol_description << "]";
  std::string tls_description;
  if (is_client) {
    tls_description = get_client_tls_description(ssnp);
  } else {
    tls_description = get_server_tls_description(ssnp);
  }
  if (!tls_description.empty()) {
    protocol_description << "," << tls_description;
  }
  return protocol_description.str();
}

#if 0
// See the TODO above the get_server_protocol_description declaration.
//
// It will be important to add this eventually, but
// TSHttpSsnServerProtocolStackGet is not defined yet. Once it (or some other
// mechanism for getting the server side stack) is implemented, we will call
// this as a part of writing the server-response node.
std::string
SessionData::get_server_protocol_description(TSHttpSsn ssnp)
{
  return get_protocol_description_helper(ssnp, !IS_CLIENT);
}
#endif

std::string
SessionData::get_client_protocol_description(TSHttpSsn ssnp)
{
  return get_protocol_description_helper(ssnp, IS_CLIENT);
}

SessionData::SessionData()
{
  aio_cont = TSContCreate(session_aio_handler, TSMutexCreate());
  txn_cont = TSContCreate(TransactionData::global_transaction_handler, nullptr);
}

SessionData::~SessionData()
{
  if (aio_cont) {
    TSContDestroy(aio_cont);
  }
  if (txn_cont) {
    TSContDestroy(txn_cont);
  }
}

/*
 * Note this assumes that the caller holds the disk_io_mutex lock. This is a
 * private member function. The two publically accessible functions hold the
 * lock before calling this.
 */
int
SessionData::write_to_disk_no_lock(std::string_view content)
{
  char *pBuf = nullptr;
  // Allocate a buffer for aio writing
  if ((pBuf = static_cast<char *>(TSmalloc(sizeof(char) * content.size())))) {
    memcpy(pBuf, content.data(), content.size());
    if (TS_SUCCESS == TSAIOWrite(log_fd, write_offset, pBuf, content.size(), aio_cont)) {
      // Update offset within file and aio events count
      write_offset += content.size();
      aio_count += 1;

      return TS_SUCCESS;
    }
    TSfree(pBuf);
  }
  return TS_ERROR;
}

int
SessionData::write_to_disk(std::string_view content)
{
  const std::lock_guard<std::recursive_mutex> _(disk_io_mutex);
  const int result = write_to_disk_no_lock(content);
  return result;
}

int
SessionData::write_transaction_to_disk(std::string_view content)
{
  const std::lock_guard<std::recursive_mutex> _(disk_io_mutex);

  int result = TS_SUCCESS;
  if (has_written_first_transaction) {
    // Prepend a comma.
    std::string with_comma;
    with_comma.reserve(content.size() + 1);
    with_comma.insert(0, ",");
    with_comma.insert(1, content);
    result = write_to_disk_no_lock(with_comma);
  } else {
    result                        = write_to_disk_no_lock(content);
    has_written_first_transaction = true;
  }

  return result;
}

int
SessionData::session_aio_handler(TSCont contp, TSEvent event, void *edata)
{
  switch (event) {
  case TS_EVENT_AIO_DONE: {
    TSAIOCallback cb     = static_cast<TSAIOCallback>(edata);
    SessionData *ssnData = static_cast<SessionData *>(TSContDataGet(contp));
    if (!ssnData) {
      TSDebug(debug_tag, "session_aio_handler(): No valid ssnData. Abort.");
      return TS_ERROR;
    }
    char *buf = TSAIOBufGet(cb);
    const std::lock_guard<std::recursive_mutex> _(ssnData->disk_io_mutex);

    // Free the allocated buffer and update aio_count
    if (buf) {
      TSfree(buf);
      if (--ssnData->aio_count == 0 && ssnData->ssn_closed) {
        // check for ssn close, if closed, do clean up
        TSContDataSet(contp, nullptr);
        close(ssnData->log_fd);
        std::error_code ec;
        ts::file::file_status st = ts::file::status(ssnData->log_name, ec);
        if (!ec) {
          disk_usage += ts::file::file_size(st);
          TSDebug(debug_tag, "Finish a session with log file of %" PRIuMAX "bytes", ts::file::file_size(st));
        }
        delete ssnData;
        return TS_SUCCESS;
      }
    }
    return TS_SUCCESS;
  }
  default:
    TSDebug(debug_tag, "session_aio_handler(): unhandled events %d", event);
    return TS_ERROR;
  }
  return TS_SUCCESS;
}

int
SessionData::global_session_handler(TSCont contp, TSEvent event, void *edata)
{
  TSHttpSsn ssnp = static_cast<TSHttpSsn>(edata);

  switch (event) {
  case TS_EVENT_HTTP_SSN_START: {
    // Grab session id for logging against a global value rather than the local
    // session_counter.
    int64_t id = TSHttpSsnIdGet(ssnp);

    // If the user has asked for SNI filtering, filter on that first because
    // any sampling will apply just to that subset of connections that match
    // that SNI.
    if (!sni_filter.empty()) {
      TSVConn ssn_vc           = TSHttpSsnClientVConnGet(ssnp);
      TSSslConnection ssl_conn = TSVConnSslConnectionGet(ssn_vc);
      SSL *ssl_obj             = reinterpret_cast<SSL *>(ssl_conn);
      if (ssl_obj == nullptr) {
        TSDebug(debug_tag, "global_session_handler(): Ignore non-HTTPS session %" PRId64 "...", id);
        break;
      }
      char const *sni_ptr = SSL_get_servername(ssl_obj, TLSEXT_NAMETYPE_host_name);
      if (sni_ptr == nullptr) {
        TSDebug(debug_tag, "global_session_handler(): Ignore HTTPS session with non-existent SNI.");
        break;
      } else {
        const std::string_view sni{sni_ptr};
        if (sni != sni_filter) {
          TSDebug(debug_tag, "global_session_handler(): Ignore HTTPS session with non-filtered SNI: %s", sni_ptr);
          break;
        }
      }
    }
    const auto this_session_count = session_counter++;
    if (this_session_count % sample_pool_size != 0) {
      TSDebug(debug_tag, "global_session_handler(): Ignore session %" PRId64 "...", id);
      break;
    } else if (disk_usage >= max_disk_usage) {
      TSDebug(debug_tag, "global_session_handler(): Ignore session %" PRId64 "due to disk usage %" PRId64 "bytes", id,
              disk_usage.load());
      break;
    }
    // Beginning of a new session
    /// Get epoch time
    auto start = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch());

    // Create new per session data
    SessionData *ssnData = new SessionData;
    TSUserArgSet(ssnp, session_arg_index, ssnData);

    TSContDataSet(ssnData->aio_cont, ssnData);

    // "protocol":(string),"tls":(string)
    // The "tls" node will only be present if the session is over SSL/TLS.
    std::string protocol_description = get_client_protocol_description(ssnp);

    std::string beginning = R"({"meta":{"version":"1.0"},"sessions":[{)" + protocol_description + R"(,"connection-time":)" +
                            std::to_string(start.count()) + R"(,"transactions":[)";

    // Use the session count's hex string as the filename.
    std::stringstream stream;
    stream << std::setw(16) << std::setfill('0') << std::hex << this_session_count;
    std::string session_hex_name = stream.str();

    // Use client ip as sub directory name
    char client_str[INET6_ADDRSTRLEN];
    sockaddr const *client_ip = TSHttpSsnClientAddrGet(ssnp);
    if (AF_INET == client_ip->sa_family) {
      inet_ntop(AF_INET, &(reinterpret_cast<sockaddr_in const *>(client_ip)->sin_addr), client_str, INET_ADDRSTRLEN);
    } else if (AF_INET6 == client_ip->sa_family) {
      inet_ntop(AF_INET6, &(reinterpret_cast<sockaddr_in6 const *>(client_ip)->sin6_addr), client_str, INET6_ADDRSTRLEN);
    } else {
      TSDebug(debug_tag, "global_session_handler(): Unknown address family.");
      snprintf(client_str, INET6_ADDRSTRLEN, "unknown");
    }

    // Initialize AIO file
    const std::lock_guard<std::recursive_mutex> _(ssnData->disk_io_mutex);
    if (ssnData->log_fd < 0) {
      ts::file::path log_p = log_directory / ts::file::path(std::string(client_str, 3));
      ts::file::path log_f = log_p / ts::file::path(session_hex_name);

      // Create subdir if not existing
      std::error_code ec;
      ts::file::status(log_p, ec);
      if (ec && mkdir(log_p.c_str(), 0755) == -1) {
        TSDebug(debug_tag, "global_session_handler(): Failed to create dir %s", log_p.c_str());
        TSError("[%s] Failed to create dir %s", debug_tag, log_p.c_str());
      }

      // Try to open log files for AIO
      ssnData->log_fd = open(log_f.c_str(), O_RDWR | O_CREAT, S_IRWXU);
      if (ssnData->log_fd < 0) {
        TSDebug(debug_tag, "global_session_handler(): Failed to open log files %s. Abort.", log_f.c_str());
        TSHttpSsnReenable(ssnp, TS_EVENT_HTTP_CONTINUE);
        return TS_EVENT_HTTP_CONTINUE;
      }
      ssnData->log_name = log_f;
      // Write log file beginning to disk
      ssnData->write_to_disk(beginning);
    }

    TSHttpSsnHookAdd(ssnp, TS_HTTP_TXN_START_HOOK, ssnData->txn_cont);
    TSHttpSsnHookAdd(ssnp, TS_HTTP_TXN_CLOSE_HOOK, ssnData->txn_cont);
    break;
  }
  case TS_EVENT_HTTP_SSN_CLOSE: {
    // Write session and close the log file.
    int64_t id = TSHttpSsnIdGet(ssnp);
    TSDebug(debug_tag, "global_session_handler(): Closing session %" PRId64 "...", id);
    // Retrieve SessionData
    SessionData *ssnData = static_cast<SessionData *>(TSUserArgGet(ssnp, session_arg_index));
    // If no valid ssnData, continue transaction as if nothing happened
    if (!ssnData) {
      TSDebug(debug_tag, "global_session_handler(): [TS_EVENT_HTTP_SSN_CLOSE] No ssnData found. Abort.");
      TSHttpSsnReenable(ssnp, TS_EVENT_HTTP_CONTINUE);
      return TS_SUCCESS;
    }
    ssnData->write_to_disk(json_closing);
    {
      const std::lock_guard<std::recursive_mutex> _(ssnData->disk_io_mutex);
      ssnData->ssn_closed = true;
    }

    break;
  }
  default:
    break;
  }
  TSHttpSsnReenable(ssnp, TS_EVENT_HTTP_CONTINUE);
  return TS_SUCCESS;
}

} // namespace traffic_dump
