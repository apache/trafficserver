/** @file

  Unit tests for origin request header write failures.

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

#include "proxy/http/HttpSM.h"
#include "proxy/http/Http1ServerTransaction.h"
#include "proxy/http/HttpConfig.h"
#include "P_UnixNetVConnection.h"
#include "tsutil/PostScript.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

/// Supply origin I/O events while deferring the transaction continuation.
class TestableHttpSM : public HttpSM
{
public:
  using HttpSM::handle_server_setup_error;
  using HttpSM::state_send_server_request_header;

  void
  attach_for_error(ProxyTransaction &txn, VIO &read, VIO &write)
  {
    server_txn                     = &txn;
    server_entry                   = vc_table.new_entry();
    server_entry->vc               = &txn;
    server_entry->vc_type          = HttpVC_t::SERVER_VC;
    server_entry->read_vio         = &read;
    server_entry->write_vio        = &write;
    server_entry->vc_write_handler = &HttpSM::state_send_server_request_header;
    callout_state                  = HttpApiState_t::IN_CALLOUT;
    t_state.state_machine          = this;
    t_state.current.server         = &t_state.server_info;
    t_state.method                 = HTTP_WKSIDX_QUERY;
    server_request_hdr_bytes       = 100;
  }

  void
  replace_write_vio(VIO &write)
  {
    server_entry->write_vio = &write;
  }

  int64_t
  written_header_bytes() const
  {
    return server_request_hdr_bytes_written;
  }
};

namespace
{
class HeaderWriteTransaction : public Http1ServerTransaction
{
public:
  NetVConnection *
  get_netvc() const override
  {
    return &netvc;
  }
  void
  do_io_close(int = -1) override
  {
    closed = true;
  }

  mutable UnixNetVConnection netvc;
  bool                       closed = false;
};
} // namespace

TEST_CASE("QUERY header errors distinguish queued bytes from write progress", "[http][query][retry]")
{
  url_init();
  mime_init();
  http_init();
  static int const config_id          = configProcessor.set(0, nullptr);
  int const        previous_config_id = HttpConfig::m_id;
  HttpConfig::m_id                    = config_id;
  ts::PostScript restore_config([&]() { HttpConfig::m_id = previous_config_id; });
  auto          *counter                 = http_rsb.origin_shutdown_cleanup_entry;
  http_rsb.origin_shutdown_cleanup_entry = Metrics::Counter::createPtr("proxy.process.http.test.header_write_cleanup");
  ts::PostScript restore_counter([&]() { http_rsb.origin_shutdown_cleanup_entry = counter; });

  int64_t const written    = GENERATE(0, 1, 100);
  bool const    read_error = GENERATE(false, true);
  int const     event      = GENERATE(VC_EVENT_ERROR, VC_EVENT_EOS);
  CAPTURE(written, read_error, event);
  HeaderWriteTransaction txn;
  TestableHttpSM         sm;
  VIO                    read;
  VIO                    write;
  txn.netvc.lerrno = EPIPE;
  write.nbytes     = 100;
  write.ndone      = written;
  sm.attach_for_error(txn, read, write);

  if (read_error) {
    sm.handle_server_setup_error(event, &read);
  } else {
    sm.state_send_server_request_header(event, &write);
  }

  CHECK(txn.closed);
  CHECK(sm.server_request_hdr_bytes == 100);
  CHECK(sm.written_header_bytes() == written);
  CHECK(sm.t_state.current.state ==
        (event == VC_EVENT_ERROR && written == 0 ? HttpTransact::CONNECTION_ERROR : HttpTransact::CONNECTION_CLOSED));
  CHECK(HttpTransact::is_request_retryable(&sm.t_state) == (written == 0));
}

TEST_CASE("QUERY header progress survives replacement by the body write VIO", "[http][query][retry]")
{
  url_init();
  mime_init();
  http_init();
  static int const config_id          = configProcessor.set(0, nullptr);
  int const        previous_config_id = HttpConfig::m_id;
  HttpConfig::m_id                    = config_id;
  ts::PostScript restore_config([&]() { HttpConfig::m_id = previous_config_id; });
  auto          *counter                 = http_rsb.origin_shutdown_cleanup_entry;
  http_rsb.origin_shutdown_cleanup_entry = Metrics::Counter::createPtr("proxy.process.http.test.header_write_cleanup");
  ts::PostScript restore_counter([&]() { http_rsb.origin_shutdown_cleanup_entry = counter; });

  int const event = GENERATE(VC_EVENT_ERROR, VC_EVENT_EOS);
  CAPTURE(event);
  HeaderWriteTransaction txn;
  TestableHttpSM         sm;
  VIO                    read;
  VIO                    write;
  VIO                    body_write;
  txn.netvc.lerrno = EPIPE;
  write.nbytes = write.ndone = 100;
  sm.attach_for_error(txn, read, write);
  sm.state_send_server_request_header(VC_EVENT_WRITE_COMPLETE, &write);
  sm.replace_write_vio(body_write);
  sm.handle_server_setup_error(event, &read);

  CHECK(txn.closed);
  CHECK(sm.written_header_bytes() == 100);
  CHECK(sm.t_state.current.state == HttpTransact::CONNECTION_CLOSED);
  CHECK_FALSE(HttpTransact::is_request_retryable(&sm.t_state));
}
