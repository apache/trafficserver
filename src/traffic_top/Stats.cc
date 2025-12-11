/** @file

    Stats class implementation for traffic_top.

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

#include "Stats.h"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <sstream>
#include <unistd.h>

#include "tscore/ink_assert.h"
#include "shared/rpc/RPCRequests.h"
#include "shared/rpc/RPCClient.h"
#include "shared/rpc/yaml_codecs.h"

namespace traffic_top
{

namespace
{
  /// Convenience class for creating metric lookup requests
  struct MetricParam : shared::rpc::RecordLookupRequest::Params {
    explicit MetricParam(std::string name)
      : shared::rpc::RecordLookupRequest::Params{std::move(name), shared::rpc::NOT_REGEX, shared::rpc::METRIC_REC_TYPES}
    {
    }
  };
} // namespace

Stats::Stats()
{
  char hostname[256];
  hostname[sizeof(hostname) - 1] = '\0';
  if (gethostname(hostname, sizeof(hostname) - 1) == 0) {
    _host = hostname;
  } else {
    _host = "localhost";
  }

  initializeLookupTable();
}

void
Stats::initializeLookupTable()
{
  // Version
  _lookup_table.emplace("version", LookupItem("Version", "proxy.process.version.server.short", StatType::Absolute));

  // Cache storage stats
  _lookup_table.emplace("disk_used", LookupItem("Disk Used", "proxy.process.cache.bytes_used", StatType::Absolute));
  _lookup_table.emplace("disk_total", LookupItem("Disk Total", "proxy.process.cache.bytes_total", StatType::Absolute));
  _lookup_table.emplace("ram_used", LookupItem("RAM Used", "proxy.process.cache.ram_cache.bytes_used", StatType::Absolute));
  _lookup_table.emplace("ram_total", LookupItem("RAM Total", "proxy.process.cache.ram_cache.total_bytes", StatType::Absolute));

  // Cache operations
  _lookup_table.emplace("lookups", LookupItem("Lookups", "proxy.process.http.cache_lookups", StatType::Rate));
  _lookup_table.emplace("cache_writes", LookupItem("Writes", "proxy.process.http.cache_writes", StatType::Rate));
  _lookup_table.emplace("cache_updates", LookupItem("Updates", "proxy.process.http.cache_updates", StatType::Rate));
  _lookup_table.emplace("cache_deletes", LookupItem("Deletes", "proxy.process.http.cache_deletes", StatType::Rate));
  _lookup_table.emplace("read_active", LookupItem("Read Active", "proxy.process.cache.read.active", StatType::Absolute));
  _lookup_table.emplace("write_active", LookupItem("Write Active", "proxy.process.cache.write.active", StatType::Absolute));
  _lookup_table.emplace("update_active", LookupItem("Update Active", "proxy.process.cache.update.active", StatType::Absolute));
  _lookup_table.emplace("entries", LookupItem("Entries", "proxy.process.cache.direntries.used", StatType::Absolute));
  _lookup_table.emplace("avg_size", LookupItem("Avg Size", "disk_used", "entries", StatType::Ratio));

  // DNS stats
  _lookup_table.emplace("dns_entry", LookupItem("DNS Entries", "proxy.process.hostdb.cache.current_items", StatType::Absolute));
  _lookup_table.emplace("dns_hits", LookupItem("DNS Hits", "proxy.process.hostdb.total_hits", StatType::Rate));
  _lookup_table.emplace("dns_lookups", LookupItem("DNS Lookups", "proxy.process.hostdb.total_lookups", StatType::Rate));
  _lookup_table.emplace("dns_serve_stale", LookupItem("DNS Serve Stale", "proxy.process.hostdb.total_serve_stale", StatType::Rate));
  _lookup_table.emplace("dns_ratio", LookupItem("DNS Hit Rate", "dns_hits", "dns_lookups", StatType::Percentage));

  // Client connections - HTTP/1.x and HTTP/2
  _lookup_table.emplace("client_req", LookupItem("Requests", "proxy.process.http.incoming_requests", StatType::Rate));
  _lookup_table.emplace("client_conn_h1",
                        LookupItem("New Conn HTTP/1.x", "proxy.process.http.total_client_connections", StatType::Rate));
  _lookup_table.emplace("client_conn_h2",
                        LookupItem("New Conn HTTP/2", "proxy.process.http2.total_client_connections", StatType::Rate));
  _lookup_table.emplace("client_conn", LookupItem("New Conn", "client_conn_h1", "client_conn_h2", StatType::Sum));
  _lookup_table.emplace("client_req_conn", LookupItem("Req/Conn", "client_req", "client_conn", StatType::Ratio));

  // Current client connections
  _lookup_table.emplace("client_curr_conn_h1",
                        LookupItem("Curr Conn HTTP/1.x", "proxy.process.http.current_client_connections", StatType::Absolute));
  _lookup_table.emplace("client_curr_conn_h2",
                        LookupItem("Curr Conn HTTP/2", "proxy.process.http2.current_client_connections", StatType::Absolute));
  _lookup_table.emplace("client_curr_conn",
                        LookupItem("Current Conn", "client_curr_conn_h1", "client_curr_conn_h2", StatType::SumAbsolute));

  // Active client connections
  _lookup_table.emplace(
    "client_actv_conn_h1",
    LookupItem("Active Conn HTTP/1.x", "proxy.process.http.current_active_client_connections", StatType::Absolute));
  _lookup_table.emplace(
    "client_actv_conn_h2",
    LookupItem("Active Conn HTTP/2", "proxy.process.http2.current_active_client_connections", StatType::Absolute));
  _lookup_table.emplace("client_actv_conn",
                        LookupItem("Active Conn", "client_actv_conn_h1", "client_actv_conn_h2", StatType::SumAbsolute));

  // Server connections
  _lookup_table.emplace("server_req", LookupItem("Requests", "proxy.process.http.outgoing_requests", StatType::Rate));
  _lookup_table.emplace("server_conn", LookupItem("New Conn", "proxy.process.http.total_server_connections", StatType::Rate));
  _lookup_table.emplace("server_req_conn", LookupItem("Req/Conn", "server_req", "server_conn", StatType::Ratio));
  _lookup_table.emplace("server_curr_conn",
                        LookupItem("Current Conn", "proxy.process.http.current_server_connections", StatType::Absolute));

  // Bandwidth stats
  _lookup_table.emplace("client_head",
                        LookupItem("Header Bytes", "proxy.process.http.user_agent_response_header_total_size", StatType::Rate));
  _lookup_table.emplace("client_body",
                        LookupItem("Body Bytes", "proxy.process.http.user_agent_response_document_total_size", StatType::Rate));
  _lookup_table.emplace("server_head",
                        LookupItem("Header Bytes", "proxy.process.http.origin_server_response_header_total_size", StatType::Rate));
  _lookup_table.emplace("server_body",
                        LookupItem("Body Bytes", "proxy.process.http.origin_server_response_document_total_size", StatType::Rate));

  // RAM cache hits/misses
  _lookup_table.emplace("ram_hit", LookupItem("RAM Hits", "proxy.process.cache.ram_cache.hits", StatType::Rate));
  _lookup_table.emplace("ram_miss", LookupItem("RAM Misses", "proxy.process.cache.ram_cache.misses", StatType::Rate));
  _lookup_table.emplace("ram_hit_miss", LookupItem("RAM Hit+Miss", "ram_hit", "ram_miss", StatType::Sum));
  _lookup_table.emplace("ram_ratio", LookupItem("RAM Hit Rate", "ram_hit", "ram_hit_miss", StatType::Percentage));

  // Keep-alive stats
  _lookup_table.emplace("ka_total",
                        LookupItem("KA Total", "proxy.process.net.dynamic_keep_alive_timeout_in_total", StatType::Rate));
  _lookup_table.emplace("ka_count",
                        LookupItem("KA Count", "proxy.process.net.dynamic_keep_alive_timeout_in_count", StatType::Rate));
  _lookup_table.emplace("client_dyn_ka", LookupItem("Dynamic KA", "ka_total", "ka_count", StatType::Ratio));

  // Error stats
  _lookup_table.emplace("client_abort", LookupItem("Client Abort", "proxy.process.http.err_client_abort_count", StatType::Rate));
  _lookup_table.emplace("conn_fail", LookupItem("Conn Failed", "proxy.process.http.err_connect_fail_count", StatType::Rate));
  _lookup_table.emplace("abort", LookupItem("Aborts", "proxy.process.http.transaction_counts.errors.aborts", StatType::Rate));
  _lookup_table.emplace("t_conn_fail",
                        LookupItem("Conn Failed", "proxy.process.http.transaction_counts.errors.connect_failed", StatType::Rate));
  _lookup_table.emplace("other_err",
                        LookupItem("Other Errors", "proxy.process.http.transaction_counts.errors.other", StatType::Rate));

  // Cache hit/miss breakdown (percentage of requests)
  _lookup_table.emplace("fresh", LookupItem("Fresh", "proxy.process.http.transaction_counts.hit_fresh", StatType::RequestPct));
  _lookup_table.emplace("reval",
                        LookupItem("Revalidated", "proxy.process.http.transaction_counts.hit_revalidated", StatType::RequestPct));
  _lookup_table.emplace("cold", LookupItem("Cold Miss", "proxy.process.http.transaction_counts.miss_cold", StatType::RequestPct));
  _lookup_table.emplace("changed",
                        LookupItem("Changed", "proxy.process.http.transaction_counts.miss_changed", StatType::RequestPct));
  _lookup_table.emplace(
    "not", LookupItem("Not Cacheable", "proxy.process.http.transaction_counts.miss_not_cacheable", StatType::RequestPct));
  _lookup_table.emplace("no",
                        LookupItem("No Cache", "proxy.process.http.transaction_counts.miss_client_no_cache", StatType::RequestPct));

  // Transaction times
  _lookup_table.emplace(
    "fresh_time", LookupItem("Fresh (ms)", "proxy.process.http.transaction_totaltime.hit_fresh", "fresh", StatType::TimeRatio));
  _lookup_table.emplace("reval_time", LookupItem("Revalidated (ms)", "proxy.process.http.transaction_totaltime.hit_revalidated",
                                                 "reval", StatType::TimeRatio));
  _lookup_table.emplace("cold_time",
                        LookupItem("Cold (ms)", "proxy.process.http.transaction_totaltime.miss_cold", "cold", StatType::TimeRatio));
  _lookup_table.emplace("changed_time", LookupItem("Changed (ms)", "proxy.process.http.transaction_totaltime.miss_changed",
                                                   "changed", StatType::TimeRatio));
  _lookup_table.emplace("not_time", LookupItem("Not Cacheable (ms)", "proxy.process.http.transaction_totaltime.miss_not_cacheable",
                                               "not", StatType::TimeRatio));
  _lookup_table.emplace("no_time", LookupItem("No Cache (ms)", "proxy.process.http.transaction_totaltime.miss_client_no_cache",
                                              "no", StatType::TimeRatio));

  // HTTP methods (percentage of requests)
  _lookup_table.emplace("get", LookupItem("GET", "proxy.process.http.get_requests", StatType::RequestPct));
  _lookup_table.emplace("head", LookupItem("HEAD", "proxy.process.http.head_requests", StatType::RequestPct));
  _lookup_table.emplace("post", LookupItem("POST", "proxy.process.http.post_requests", StatType::RequestPct));
  _lookup_table.emplace("put", LookupItem("PUT", "proxy.process.http.put_requests", StatType::RequestPct));
  _lookup_table.emplace("delete", LookupItem("DELETE", "proxy.process.http.delete_requests", StatType::RequestPct));

  // HTTP response codes (percentage of requests)
  _lookup_table.emplace("100", LookupItem("100", "proxy.process.http.100_responses", StatType::RequestPct));
  _lookup_table.emplace("101", LookupItem("101", "proxy.process.http.101_responses", StatType::RequestPct));
  _lookup_table.emplace("1xx", LookupItem("1xx", "proxy.process.http.1xx_responses", StatType::RequestPct));
  _lookup_table.emplace("200", LookupItem("200", "proxy.process.http.200_responses", StatType::RequestPct));
  _lookup_table.emplace("201", LookupItem("201", "proxy.process.http.201_responses", StatType::RequestPct));
  _lookup_table.emplace("202", LookupItem("202", "proxy.process.http.202_responses", StatType::RequestPct));
  _lookup_table.emplace("203", LookupItem("203", "proxy.process.http.203_responses", StatType::RequestPct));
  _lookup_table.emplace("204", LookupItem("204", "proxy.process.http.204_responses", StatType::RequestPct));
  _lookup_table.emplace("205", LookupItem("205", "proxy.process.http.205_responses", StatType::RequestPct));
  _lookup_table.emplace("206", LookupItem("206", "proxy.process.http.206_responses", StatType::RequestPct));
  _lookup_table.emplace("2xx", LookupItem("2xx", "proxy.process.http.2xx_responses", StatType::RequestPct));
  _lookup_table.emplace("300", LookupItem("300", "proxy.process.http.300_responses", StatType::RequestPct));
  _lookup_table.emplace("301", LookupItem("301", "proxy.process.http.301_responses", StatType::RequestPct));
  _lookup_table.emplace("302", LookupItem("302", "proxy.process.http.302_responses", StatType::RequestPct));
  _lookup_table.emplace("303", LookupItem("303", "proxy.process.http.303_responses", StatType::RequestPct));
  _lookup_table.emplace("304", LookupItem("304", "proxy.process.http.304_responses", StatType::RequestPct));
  _lookup_table.emplace("305", LookupItem("305", "proxy.process.http.305_responses", StatType::RequestPct));
  _lookup_table.emplace("307", LookupItem("307", "proxy.process.http.307_responses", StatType::RequestPct));
  _lookup_table.emplace("3xx", LookupItem("3xx", "proxy.process.http.3xx_responses", StatType::RequestPct));
  _lookup_table.emplace("400", LookupItem("400", "proxy.process.http.400_responses", StatType::RequestPct));
  _lookup_table.emplace("401", LookupItem("401", "proxy.process.http.401_responses", StatType::RequestPct));
  _lookup_table.emplace("402", LookupItem("402", "proxy.process.http.402_responses", StatType::RequestPct));
  _lookup_table.emplace("403", LookupItem("403", "proxy.process.http.403_responses", StatType::RequestPct));
  _lookup_table.emplace("404", LookupItem("404", "proxy.process.http.404_responses", StatType::RequestPct));
  _lookup_table.emplace("405", LookupItem("405", "proxy.process.http.405_responses", StatType::RequestPct));
  _lookup_table.emplace("406", LookupItem("406", "proxy.process.http.406_responses", StatType::RequestPct));
  _lookup_table.emplace("407", LookupItem("407", "proxy.process.http.407_responses", StatType::RequestPct));
  _lookup_table.emplace("408", LookupItem("408", "proxy.process.http.408_responses", StatType::RequestPct));
  _lookup_table.emplace("409", LookupItem("409", "proxy.process.http.409_responses", StatType::RequestPct));
  _lookup_table.emplace("410", LookupItem("410", "proxy.process.http.410_responses", StatType::RequestPct));
  _lookup_table.emplace("411", LookupItem("411", "proxy.process.http.411_responses", StatType::RequestPct));
  _lookup_table.emplace("412", LookupItem("412", "proxy.process.http.412_responses", StatType::RequestPct));
  _lookup_table.emplace("413", LookupItem("413", "proxy.process.http.413_responses", StatType::RequestPct));
  _lookup_table.emplace("414", LookupItem("414", "proxy.process.http.414_responses", StatType::RequestPct));
  _lookup_table.emplace("415", LookupItem("415", "proxy.process.http.415_responses", StatType::RequestPct));
  _lookup_table.emplace("416", LookupItem("416", "proxy.process.http.416_responses", StatType::RequestPct));
  _lookup_table.emplace("4xx", LookupItem("4xx", "proxy.process.http.4xx_responses", StatType::RequestPct));
  _lookup_table.emplace("500", LookupItem("500", "proxy.process.http.500_responses", StatType::RequestPct));
  _lookup_table.emplace("501", LookupItem("501", "proxy.process.http.501_responses", StatType::RequestPct));
  _lookup_table.emplace("502", LookupItem("502", "proxy.process.http.502_responses", StatType::RequestPct));
  _lookup_table.emplace("503", LookupItem("503", "proxy.process.http.503_responses", StatType::RequestPct));
  _lookup_table.emplace("504", LookupItem("504", "proxy.process.http.504_responses", StatType::RequestPct));
  _lookup_table.emplace("505", LookupItem("505", "proxy.process.http.505_responses", StatType::RequestPct));
  _lookup_table.emplace("5xx", LookupItem("5xx", "proxy.process.http.5xx_responses", StatType::RequestPct));

  // Derived bandwidth stats
  _lookup_table.emplace("client_net", LookupItem("Net (bits/s)", "client_head", "client_body", StatType::SumBits));
  _lookup_table.emplace("client_size", LookupItem("Total Size", "client_head", "client_body", StatType::Sum));
  _lookup_table.emplace("client_avg_size", LookupItem("Avg Size", "client_size", "client_req", StatType::Ratio));
  _lookup_table.emplace("server_net", LookupItem("Net (bits/s)", "server_head", "server_body", StatType::SumBits));
  _lookup_table.emplace("server_size", LookupItem("Total Size", "server_head", "server_body", StatType::Sum));
  _lookup_table.emplace("server_avg_size", LookupItem("Avg Size", "server_size", "server_req", StatType::Ratio));

  // Total transaction time
  _lookup_table.emplace("total_time", LookupItem("Total Time", "proxy.process.http.total_transactions_time", StatType::Rate));
  _lookup_table.emplace("client_req_time", LookupItem("Resp Time (ms)", "total_time", "client_req", StatType::Ratio));

  // SSL/TLS stats
  _lookup_table.emplace("ssl_handshake_success",
                        LookupItem("SSL Handshake OK", "proxy.process.ssl.total_success_handshake_count_in", StatType::Rate));
  _lookup_table.emplace("ssl_handshake_fail",
                        LookupItem("SSL Handshake Fail", "proxy.process.ssl.total_handshake_time", StatType::Rate));
  _lookup_table.emplace("ssl_session_hit",
                        LookupItem("SSL Session Hit", "proxy.process.ssl.ssl_session_cache_hit", StatType::Rate));
  _lookup_table.emplace("ssl_session_miss",
                        LookupItem("SSL Session Miss", "proxy.process.ssl.ssl_session_cache_miss", StatType::Rate));
  _lookup_table.emplace("ssl_curr_sessions",
                        LookupItem("SSL Current Sessions", "proxy.process.ssl.user_agent_sessions", StatType::Absolute));

  // Extended SSL/TLS handshake stats
  _lookup_table.emplace("ssl_attempts_in",
                        LookupItem("Handshake Attempts In", "proxy.process.ssl.total_attempts_handshake_count_in", StatType::Rate));
  _lookup_table.emplace("ssl_attempts_out", LookupItem("Handshake Attempts Out",
                                                       "proxy.process.ssl.total_attempts_handshake_count_out", StatType::Rate));
  _lookup_table.emplace("ssl_success_in",
                        LookupItem("Handshake Success In", "proxy.process.ssl.total_success_handshake_count_in", StatType::Rate));
  _lookup_table.emplace("ssl_success_out",
                        LookupItem("Handshake Success Out", "proxy.process.ssl.total_success_handshake_count_out", StatType::Rate));
  _lookup_table.emplace("ssl_handshake_time",
                        LookupItem("Handshake Time", "proxy.process.ssl.total_handshake_time", StatType::Rate));

  // SSL session stats
  _lookup_table.emplace("ssl_sess_new",
                        LookupItem("Session New", "proxy.process.ssl.ssl_session_cache_new_session", StatType::Rate));
  _lookup_table.emplace("ssl_sess_evict",
                        LookupItem("Session Eviction", "proxy.process.ssl.ssl_session_cache_eviction", StatType::Rate));
  _lookup_table.emplace("ssl_origin_reused",
                        LookupItem("Origin Sess Reused", "proxy.process.ssl.origin_session_reused", StatType::Rate));

  // SSL/TLS origin errors
  _lookup_table.emplace("ssl_origin_bad_cert", LookupItem("Bad Cert", "proxy.process.ssl.origin_server_bad_cert", StatType::Rate));
  _lookup_table.emplace("ssl_origin_expired",
                        LookupItem("Cert Expired", "proxy.process.ssl.origin_server_expired_cert", StatType::Rate));
  _lookup_table.emplace("ssl_origin_revoked",
                        LookupItem("Cert Revoked", "proxy.process.ssl.origin_server_revoked_cert", StatType::Rate));
  _lookup_table.emplace("ssl_origin_unknown_ca",
                        LookupItem("Unknown CA", "proxy.process.ssl.origin_server_unknown_ca", StatType::Rate));
  _lookup_table.emplace("ssl_origin_verify_fail",
                        LookupItem("Verify Failed", "proxy.process.ssl.origin_server_cert_verify_failed", StatType::Rate));
  _lookup_table.emplace("ssl_origin_decrypt_fail",
                        LookupItem("Decrypt Failed", "proxy.process.ssl.origin_server_decryption_failed", StatType::Rate));
  _lookup_table.emplace("ssl_origin_wrong_ver",
                        LookupItem("Wrong Version", "proxy.process.ssl.origin_server_wrong_version", StatType::Rate));
  _lookup_table.emplace("ssl_origin_other",
                        LookupItem("Other Errors", "proxy.process.ssl.origin_server_other_errors", StatType::Rate));

  // SSL/TLS client errors
  _lookup_table.emplace("ssl_client_bad_cert",
                        LookupItem("Client Bad Cert", "proxy.process.ssl.user_agent_bad_cert", StatType::Rate));

  // SSL general errors
  _lookup_table.emplace("ssl_error_ssl", LookupItem("SSL Error", "proxy.process.ssl.ssl_error_ssl", StatType::Rate));
  _lookup_table.emplace("ssl_error_syscall", LookupItem("Syscall Error", "proxy.process.ssl.ssl_error_syscall", StatType::Rate));
  _lookup_table.emplace("ssl_error_async", LookupItem("Async Error", "proxy.process.ssl.ssl_error_async", StatType::Rate));

  // TLS version stats
  _lookup_table.emplace("tls_v10", LookupItem("TLSv1.0", "proxy.process.ssl.ssl_total_tlsv1", StatType::Rate));
  _lookup_table.emplace("tls_v11", LookupItem("TLSv1.1", "proxy.process.ssl.ssl_total_tlsv11", StatType::Rate));
  _lookup_table.emplace("tls_v12", LookupItem("TLSv1.2", "proxy.process.ssl.ssl_total_tlsv12", StatType::Rate));
  _lookup_table.emplace("tls_v13", LookupItem("TLSv1.3", "proxy.process.ssl.ssl_total_tlsv13", StatType::Rate));

  // Connection error stats
  _lookup_table.emplace("err_conn_fail", LookupItem("Conn Failed", "proxy.process.http.err_connect_fail_count", StatType::Rate));
  _lookup_table.emplace("err_client_abort",
                        LookupItem("Client Abort", "proxy.process.http.err_client_abort_count", StatType::Rate));
  _lookup_table.emplace("err_client_read",
                        LookupItem("Client Read Err", "proxy.process.http.err_client_read_error_count", StatType::Rate));

  // Transaction error stats
  _lookup_table.emplace("txn_aborts", LookupItem("Aborts", "proxy.process.http.transaction_counts.errors.aborts", StatType::Rate));
  _lookup_table.emplace(
    "txn_possible_aborts",
    LookupItem("Possible Aborts", "proxy.process.http.transaction_counts.errors.possible_aborts", StatType::Rate));
  _lookup_table.emplace("txn_other_errors",
                        LookupItem("Other Errors", "proxy.process.http.transaction_counts.errors.other", StatType::Rate));

  // Cache error stats
  _lookup_table.emplace("cache_read_errors", LookupItem("Cache Read Err", "proxy.process.cache.read.failure", StatType::Rate));
  _lookup_table.emplace("cache_write_errors", LookupItem("Cache Write Err", "proxy.process.cache.write.failure", StatType::Rate));
  _lookup_table.emplace("cache_lookup_fail", LookupItem("Lookup Fail", "proxy.process.cache.lookup.failure", StatType::Rate));

  // HTTP/2 error stats
  _lookup_table.emplace("h2_stream_errors", LookupItem("Stream Errors", "proxy.process.http2.stream_errors", StatType::Rate));
  _lookup_table.emplace("h2_conn_errors", LookupItem("Conn Errors", "proxy.process.http2.connection_errors", StatType::Rate));
  _lookup_table.emplace("h2_session_die_error",
                        LookupItem("Session Die Err", "proxy.process.http2.session_die_error", StatType::Rate));
  _lookup_table.emplace("h2_session_die_high_error",
                        LookupItem("High Error Rate", "proxy.process.http2.session_die_high_error_rate", StatType::Rate));

  // HTTP/2 stream stats
  _lookup_table.emplace("h2_streams_total",
                        LookupItem("Total Streams", "proxy.process.http2.total_client_streams", StatType::Rate));
  _lookup_table.emplace("h2_streams_current",
                        LookupItem("Current Streams", "proxy.process.http2.current_client_streams", StatType::Absolute));

  // Network stats
  _lookup_table.emplace("net_open_conn",
                        LookupItem("Open Conn", "proxy.process.net.connections_currently_open", StatType::Absolute));
  _lookup_table.emplace("net_throttled",
                        LookupItem("Throttled Conn", "proxy.process.net.connections_throttled_in", StatType::Rate));
}

bool
Stats::getStats()
{
  _old_stats = std::move(_stats);
  _stats     = std::make_unique<std::map<std::string, std::string>>();

  gettimeofday(&_time, nullptr);
  double now = _time.tv_sec + static_cast<double>(_time.tv_usec) / 1000000;

  _last_error = fetch_and_fill_stats(_lookup_table, _stats.get());
  if (!_last_error.empty()) {
    return false;
  }

  _old_time  = _now;
  _now       = now;
  _time_diff = _now - _old_time;

  // Record history for key metrics used in graphs
  static const std::vector<std::string> history_keys = {
    "client_req",       // Requests/sec
    "client_net",       // Client bandwidth
    "server_net",       // Origin bandwidth
    "ram_ratio",        // Cache hit rate
    "client_curr_conn", // Current connections
    "server_curr_conn", // Origin connections
    "lookups",          // Cache lookups
    "cache_writes",     // Cache writes
    "dns_lookups",      // DNS lookups
    "2xx",              // 2xx responses
    "4xx",              // 4xx responses
    "5xx",              // 5xx responses
  };

  for (const auto &key : history_keys) {
    double value = 0;
    getStat(key, value);

    auto &hist = _history[key];
    hist.push_back(value);

    // Keep history bounded
    while (hist.size() > MAX_HISTORY_LENGTH) {
      hist.pop_front();
    }
  }

  return true;
}

std::string
Stats::fetch_and_fill_stats(const std::map<std::string, LookupItem> &lookup_table, std::map<std::string, std::string> *stats)
{
  namespace rpc = shared::rpc;

  if (stats == nullptr) {
    return "Invalid stats parameter, it shouldn't be null.";
  }

  try {
    rpc::RecordLookupRequest request;

    // Build the request with all metrics we need to fetch
    for (const auto &[key, item] : lookup_table) {
      // Only add direct metrics (not derived ones)
      if (item.type == StatType::Absolute || item.type == StatType::Rate || item.type == StatType::RequestPct ||
          item.type == StatType::TimeRatio) {
        try {
          request.emplace_rec(MetricParam{item.name});
        } catch (const std::exception &e) {
          return std::string("Error configuring stats request: ") + e.what();
        }
      }
    }

    rpc::RPCClient rpcClient;
    auto const    &rpcResponse = rpcClient.invoke<>(request, std::chrono::milliseconds(1000), 10);

    if (!rpcResponse.is_error()) {
      auto const &records = rpcResponse.result.as<rpc::RecordLookUpResponse>();

      if (!records.errorList.empty()) {
        std::stringstream ss;
        for (const auto &err : records.errorList) {
          ss << err << "\n";
        }
        return ss.str();
      }

      for (auto &&recordInfo : records.recordList) {
        (*stats)[recordInfo.name] = recordInfo.currentValue;
      }
    } else {
      std::stringstream ss;
      ss << rpcResponse.error.as<rpc::JSONRPCError>();
      return ss.str();
    }
  } catch (const std::exception &ex) {
    std::string error_msg = ex.what();

    // Check for permission denied error (EACCES = 13)
    if (error_msg.find("(13)") != std::string::npos || error_msg.find("Permission denied") != std::string::npos) {
      return "Permission denied accessing RPC socket.\n"
             "Ensure you have permission to access the ATS runtime directory.\n"
             "You may need to run as the traffic_server user or with sudo.\n"
             "Original error: " +
             error_msg;
    }

    // Check for connection refused (server not running)
    if (error_msg.find("ECONNREFUSED") != std::string::npos || error_msg.find("Connection refused") != std::string::npos) {
      return "Cannot connect to ATS - is traffic_server running?\n"
             "Original error: " +
             error_msg;
    }

    return error_msg;
  }

  return {}; // No error
}

int64_t
Stats::getValue(const std::string &key, const std::map<std::string, std::string> *stats) const
{
  if (stats == nullptr) {
    return 0;
  }
  auto it = stats->find(key);
  if (it == stats->end()) {
    return 0;
  }
  return std::atoll(it->second.c_str());
}

void
Stats::getStat(const std::string &key, double &value, StatType overrideType)
{
  std::string prettyName;
  StatType    type;
  getStat(key, value, prettyName, type, overrideType);
}

void
Stats::getStat(const std::string &key, std::string &value)
{
  auto it = _lookup_table.find(key);
  ink_assert(it != _lookup_table.end());
  const auto &item = it->second;

  if (_stats) {
    auto stats_it = _stats->find(item.name);
    if (stats_it != _stats->end()) {
      value = stats_it->second;
      return;
    }
  }
  value = "";
}

void
Stats::getStat(const std::string &key, double &value, std::string &prettyName, StatType &type, StatType overrideType)
{
  value = 0;

  auto it = _lookup_table.find(key);
  ink_assert(it != _lookup_table.end());
  const auto &item = it->second;

  prettyName = item.pretty;
  type       = (overrideType != StatType::Absolute) ? overrideType : item.type;

  switch (type) {
  case StatType::Absolute:
  case StatType::Rate:
  case StatType::RequestPct:
  case StatType::TimeRatio: {
    if (_stats) {
      value = getValue(item.name, _stats.get());
    }

    // Special handling for total_time (convert from nanoseconds)
    if (key == "total_time") {
      value = value / 10000000;
    }

    // Calculate rate if needed
    if ((type == StatType::Rate || type == StatType::RequestPct || type == StatType::TimeRatio) && _old_stats != nullptr &&
        !_absolute) {
      double old = getValue(item.name, _old_stats.get());
      if (key == "total_time") {
        old = old / 10000000;
      }
      value = _time_diff > 0 ? (value - old) / _time_diff : 0;
    }
    break;
  }

  case StatType::Ratio:
  case StatType::Percentage: {
    double numerator   = 0;
    double denominator = 0;
    getStat(item.numerator, numerator);
    getStat(item.denominator, denominator);
    value = (denominator != 0) ? numerator / denominator : 0;
    if (type == StatType::Percentage) {
      value *= 100;
    }
    break;
  }

  case StatType::Sum:
  case StatType::SumBits: {
    double first  = 0;
    double second = 0;
    getStat(item.numerator, first, StatType::Rate);
    getStat(item.denominator, second, StatType::Rate);
    value = first + second;
    if (type == StatType::SumBits) {
      value *= 8; // Convert bytes to bits
    }
    break;
  }

  case StatType::SumAbsolute: {
    double first  = 0;
    double second = 0;
    getStat(item.numerator, first);
    getStat(item.denominator, second);
    value = first + second;
    break;
  }
  }

  // Post-processing for TimeRatio: convert to milliseconds
  if (type == StatType::TimeRatio) {
    double denominator = 0;
    getStat(item.denominator, denominator, StatType::Rate);
    value = (denominator != 0) ? value / denominator * 1000 : 0;
  }

  // Post-processing for RequestPct: calculate percentage of client requests
  if (type == StatType::RequestPct) {
    double client_req = 0;
    getStat("client_req", client_req);
    value = (client_req != 0) ? value / client_req * 100 : 0;
  }
}

bool
Stats::toggleAbsolute()
{
  _absolute = !_absolute;
  return _absolute;
}

std::vector<std::string>
Stats::getStatKeys() const
{
  std::vector<std::string> keys;
  keys.reserve(_lookup_table.size());
  for (const auto &[key, _] : _lookup_table) {
    keys.push_back(key);
  }
  return keys;
}

bool
Stats::hasStat(const std::string &key) const
{
  return _lookup_table.find(key) != _lookup_table.end();
}

const LookupItem *
Stats::getLookupItem(const std::string &key) const
{
  auto it = _lookup_table.find(key);
  return (it != _lookup_table.end()) ? &it->second : nullptr;
}

std::vector<double>
Stats::getHistory(const std::string &key, double maxValue) const
{
  std::vector<double> result;

  auto it = _history.find(key);
  if (it == _history.end() || it->second.empty()) {
    return result;
  }

  const auto &hist = it->second;

  // Find max value for normalization if not specified
  if (maxValue <= 0.0) {
    maxValue = *std::max_element(hist.begin(), hist.end());
    if (maxValue <= 0.0) {
      maxValue = 1.0; // Avoid division by zero
    }
  }

  // Normalize values to 0.0-1.0 range
  result.reserve(hist.size());
  for (double val : hist) {
    result.push_back(val / maxValue);
  }

  return result;
}

} // namespace traffic_top
