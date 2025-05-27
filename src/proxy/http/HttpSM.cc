/** @file

  HTTP state machine

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

#include "tsutil/ts_bw_format.h"
#include "proxy/ProxyTransaction.h"
#include "proxy/http/HttpSM.h"
#include "proxy/http/ConnectingEntry.h"
#include "proxy/http/HttpTransact.h"
#include "proxy/http/HttpBodyFactory.h"
#include "proxy/http/HttpTransactHeaders.h"
#include "iocore/eventsystem/ConfigProcessor.h"
#include "proxy/http/Http1ServerSession.h"
#include "proxy/http2/Http2ServerSession.h"
#include "proxy/http/HttpDebugNames.h"
#include "proxy/http/HttpSessionManager.h"
#include "proxy/http/HttpVCTable.h"
#include "../../iocore/cache/P_CacheInternal.h"
#include "../../iocore/net/P_Net.h"
#include "../../iocore/net/P_UnixNet.h"
#include "../../iocore/net/P_UnixNetVConnection.h"
#include "proxy/http/PreWarmConfig.h"
#include "proxy/logging/Log.h"
#include "proxy/logging/LogAccess.h"
#include "proxy/PluginVC.h"
#include "proxy/ReverseProxy.h"
#include "proxy/http/remap/RemapProcessor.h"
#include "proxy/Transform.h"
#include "../../iocore/net/P_SSLConfig.h"
#include "iocore/net/ConnectionTracker.h"
#include "iocore/net/TLSALPNSupport.h"
#include "iocore/net/TLSBasicSupport.h"
#include "iocore/net/TLSSNISupport.h"
#include "iocore/net/TLSSessionResumptionSupport.h"
#include "iocore/net/TLSTunnelSupport.h"
#include "proxy/IPAllow.h"

#include "iocore/net/ProxyProtocol.h"

#include "tscore/Layout.h"
#include "ts/ats_probe.h"

#include <openssl/ossl_typ.h>
#include <openssl/ssl.h>
#include <algorithm>
#include <atomic>

using namespace std::literals;

#define DEFAULT_RESPONSE_BUFFER_SIZE_INDEX 6 // 8K
#define DEFAULT_REQUEST_BUFFER_SIZE_INDEX  6 // 8K
#define MIN_CONFIG_BUFFER_SIZE_INDEX       5 // 4K

#define hsm_release_assert(EX)              \
  {                                         \
    if (!(EX)) {                            \
      this->dump_state_on_assert();         \
      _ink_assert(#EX, __FILE__, __LINE__); \
    }                                       \
  }

/*
 * Comment this off if you don't
 * want httpSM to use new_empty_MIOBuffer(..) call
 */

#define USE_NEW_EMPTY_MIOBUFFER

extern int cache_config_read_while_writer;

HttpBodyFactory *body_factory = nullptr;

// We have a debugging list that can use to find stuck
//  state machines
DList(HttpSM, debug_link) debug_sm_list;
ink_mutex debug_sm_list_mutex;

static DbgCtl dbg_ctl_dns{"dns"};
static DbgCtl dbg_ctl_dns_srv{"dns_srv"};
static DbgCtl dbg_ctl_http{"http"};
static DbgCtl dbg_ctl_http_cache_write{"http_cache_write"};
static DbgCtl dbg_ctl_http_connect{"http_connect"};
static DbgCtl dbg_ctl_http_hdrs{"http_hdrs"};
static DbgCtl dbg_ctl_http_parse{"http_parse"};
static DbgCtl dbg_ctl_http_range{"http_range"};
static DbgCtl dbg_ctl_http_redirect{"http_redirect"};
static DbgCtl dbg_ctl_http_redir_error{"http_redir_error"};
static DbgCtl dbg_ctl_http_seq{"http_seq"};
static DbgCtl dbg_ctl_http_ss{"http_ss"};
static DbgCtl dbg_ctl_http_ss_auth{"http_ss_auth"};
static DbgCtl dbg_ctl_http_timeout{"http_timeout"};
static DbgCtl dbg_ctl_http_track{"http_track"};
static DbgCtl dbg_ctl_http_trans{"http_trans"};
static DbgCtl dbg_ctl_http_tproxy{"http_tproxy"};
static DbgCtl dbg_ctl_http_tunnel{"http_tunnel"};
static DbgCtl dbg_ctl_http_websocket{"http_websocket"};
static DbgCtl dbg_ctl_ip_allow{"ip_allow"};
static DbgCtl dbg_ctl_ssl_alpn{"ssl_alpn"};
static DbgCtl dbg_ctl_ssl_early_data{"ssl_early_data"};
static DbgCtl dbg_ctl_ssl_sni{"ssl_sni"};
static DbgCtl dbg_ctl_url_rewrite{"url_rewrite"};

static const int sub_header_size = sizeof("Content-type: ") - 1 + 2 + sizeof("Content-range: bytes ") - 1 + 4;
static const int boundary_size   = 2 + sizeof("RANGE_SEPARATOR") - 1 + 2;

static const char *str_100_continue_response = "HTTP/1.1 100 Continue\r\n\r\n";
static const int   len_100_continue_response = strlen(str_100_continue_response);

// Handy alias for short (single line) message generation.
using lbw = swoc::LocalBufferWriter<256>;

namespace
{
DbgCtl dbg_ctl_proxyprotocol{"proxyprotocol"};

// Unique state machine identifier
std::atomic<int64_t> next_sm_id(0);

/// Buffer for some error logs.
thread_local std::string error_bw_buffer;

} // namespace

int64_t
do_outbound_proxy_protocol(MIOBuffer *miob, NetVConnection *vc_out, NetVConnection *vc_in, int conf)
{
  ink_release_assert(conf >= 0);

  ProxyProtocol        info       = vc_in->get_proxy_protocol_info();
  ProxyProtocolVersion pp_version = proxy_protocol_version_cast(conf);

  if (info.version == ProxyProtocolVersion::UNDEFINED) {
    if (conf == 0) {
      // nothing to forward
      return 0;
    } else {
      Dbg(dbg_ctl_proxyprotocol, "vc_in had no Proxy Protocol. Manufacturing from the vc_in socket.");
      // set info from incoming NetVConnection
      IpEndpoint local = vc_in->get_local_endpoint();
      info             = ProxyProtocol{pp_version, local.family(), vc_in->get_remote_endpoint(), local};
    }
  }

  vc_out->set_proxy_protocol_info(info);

  IOBufferBlock *block = miob->first_write_block();
  size_t         len   = proxy_protocol_build(reinterpret_cast<uint8_t *>(block->buf()), block->write_avail(), info, pp_version);

  if (len > 0) {
    miob->fill(len);
  }

  return len;
}

ClassAllocator<HttpSM> httpSMAllocator("httpSMAllocator");

void
initialize_thread_for_connecting_pools(EThread *thread)
{
  if (thread->connecting_pool == nullptr) {
    thread->connecting_pool = new ConnectingPool();
  }
}

#define SMDbg(tag, fmt, ...) SpecificDbg(debug_on, tag, "[%" PRId64 "] " fmt, sm_id, ##__VA_ARGS__)

#define REMEMBER(e, r)                             \
  {                                                \
    history.push_back(MakeSourceLocation(), e, r); \
  }

#ifdef STATE_ENTER
#undef STATE_ENTER
#endif
#define STATE_ENTER(state_name, event)                                                   \
  {                                                                                      \
    /*ink_assert (magic == HttpSmMagic_t::ALIVE); */ REMEMBER(event, reentrancy_count);  \
    SMDbg(dbg_ctl_http, "[%s, %s]", #state_name, HttpDebugNames::get_event_name(event)); \
    ATS_PROBE1(state_name, sm_id);                                                       \
  }

#define HTTP_SM_SET_DEFAULT_HANDLER(_h)   \
  {                                       \
    REMEMBER(NO_EVENT, reentrancy_count); \
    default_handler = _h;                 \
  }

/*
 * Helper functions to ensure that the parallel
 * API set timeouts are set consistently with the records.yaml settings
 */
ink_hrtime
HttpSM::get_server_inactivity_timeout()
{
  ink_hrtime retval = 0;
  if (t_state.api_txn_no_activity_timeout_value != -1) {
    retval = HRTIME_MSECONDS(t_state.api_txn_no_activity_timeout_value);
  } else {
    retval = HRTIME_SECONDS(t_state.txn_conf->transaction_no_activity_timeout_out);
  }
  return retval;
}

ink_hrtime
HttpSM::get_server_active_timeout()
{
  ink_hrtime retval = 0;
  if (t_state.api_txn_active_timeout_value != -1) {
    retval = HRTIME_MSECONDS(t_state.api_txn_active_timeout_value);
  } else {
    retval = HRTIME_SECONDS(t_state.txn_conf->transaction_active_timeout_out);
  }
  return retval;
}

ink_hrtime
HttpSM::get_server_connect_timeout()
{
  ink_hrtime retval = 0;
  if (t_state.api_txn_connect_timeout_value != -1) {
    retval = HRTIME_MSECONDS(t_state.api_txn_connect_timeout_value);
  } else {
    retval = HRTIME_SECONDS(t_state.txn_conf->connect_attempts_timeout);
  }
  return retval;
}

HttpSM::HttpSM() : Continuation(nullptr), vc_table(this) {}

void
HttpSM::cleanup()
{
  t_state.destroy();
  api_hooks.clear();
  http_parser_clear(&http_parser);

  HttpConfig::release(t_state.http_config_param);
  m_remap->release();

  mutex.clear();
  tunnel.mutex.clear();
  cache_sm.mutex.clear();
  transform_cache_sm.mutex.clear();
  magic    = HttpSmMagic_t::DEAD;
  debug_on = false;

  if (_prewarm_sm) {
    _prewarm_sm->destroy();
    THREAD_FREE(_prewarm_sm, preWarmSMAllocator, this_ethread());
    _prewarm_sm = nullptr;
  }
}

void
HttpSM::destroy()
{
  cleanup();
  THREAD_FREE(this, httpSMAllocator, this_thread());
}

void
HttpSM::init(bool from_early_data)
{
  milestones[TS_MILESTONE_SM_START] = ink_get_hrtime();

  _from_early_data = from_early_data;

  magic = HttpSmMagic_t::ALIVE;

  server_txn = nullptr;

  // Unique state machine identifier
  sm_id = next_sm_id++;
  ATS_PROBE1(milestone_sm_start, sm_id);
  t_state.state_machine = this;

  t_state.http_config_param = HttpConfig::acquire();
  // Acquire a lease on the global remap / rewrite table (stupid global name ...)
  m_remap = rewrite_table->acquire();

  // Simply point to the global config for the time being, no need to copy this
  // entire struct if nothing is going to change it.
  t_state.txn_conf = &t_state.http_config_param->oride;

  t_state.init();
  http_parser_init(&http_parser);

  // Added to skip dns if the document is in cache. DNS will be forced if there is a ip based ACL in
  // cache control or parent.config or if the doc_in_cache_skip_dns is disabled or if http caching is disabled
  // TODO: This probably doesn't honor this as a per-transaction overridable config.
  t_state.force_dns = (ip_rule_in_CacheControlTable() || t_state.parent_params->parent_table->ipMatch ||
                       !(t_state.txn_conf->doc_in_cache_skip_dns) || !(t_state.txn_conf->cache_http));

  SET_HANDLER(&HttpSM::main_handler);

  // Remember where this SM is running so it gets returned correctly
  this->setThreadAffinity(this_ethread());

#ifdef USE_HTTP_DEBUG_LISTS
  ink_mutex_acquire(&debug_sm_list_mutex);
  debug_sm_list.push(this);
  ink_mutex_release(&debug_sm_list_mutex);
#endif
}

void
HttpSM::set_ua_half_close_flag()
{
  _ua.get_txn()->set_half_close_flag(true);
}

inline int
HttpSM::do_api_callout()
{
  if (hooks_set) {
    return do_api_callout_internal();
  } else {
    handle_api_return();
    return 0;
  }
}

int
HttpSM::state_add_to_list(int /* event ATS_UNUSED */, void * /* data ATS_UNUSED */)
{
  t_state.api_next_action = HttpTransact::StateMachineAction_t::API_SM_START;
  if (do_api_callout() < 0) {
    // Didn't get the hook continuation lock. Clear the read and wait for next event
    if (_ua.get_entry()->read_vio) {
      // Seems like _ua.get_entry()->read_vio->disable(); should work, but that was
      // not sufficient to stop the state machine from processing IO events until the
      // TXN_START hooks had completed.  Just set the number of bytes to read to 0
      _ua.get_entry()->read_vio = _ua.get_entry()->vc->do_io_read(this, 0, nullptr);
    }
    return EVENT_CONT;
  }
  return EVENT_DONE;
}

int
HttpSM::state_remove_from_list(int /* event ATS_UNUSED */, void * /* data ATS_UNUSED */)
{
  // We're now ready to finish off the state machine
  terminate_sm         = true;
  kill_this_async_done = true;

  return EVENT_DONE;
}

void
HttpSM::start_sub_sm()
{
  tunnel.init(this, mutex);
  cache_sm.init(this, mutex);
  transform_cache_sm.init(this, mutex);
}

void
HttpSM::attach_client_session(ProxyTransaction *txn)
{
  ATS_PROBE1(milestone_ua_begin, sm_id);
  milestones[TS_MILESTONE_UA_BEGIN] = ink_get_hrtime();
  ink_assert(txn != nullptr);

  NetVConnection *netvc = txn->get_netvc();
  if (!netvc) {
    return;
  }
  _ua.set_txn(txn, milestones);

  // Collect log & stats information. We've already verified that the netvc is !nullptr above,
  // and netvc == _ua.get_txn()->get_netvc().

  is_internal = netvc->get_is_internal_request();
  mptcp_state = netvc->get_mptcp_state();

  ink_release_assert(_ua.get_txn()->get_half_close_flag() == false);
  mutex = txn->mutex;
  if (_ua.get_txn()->debug()) {
    debug_on = true;
  }

  t_state.setup_per_txn_configs();
  t_state.api_skip_all_remapping = netvc->get_is_unmanaged_request();

  ink_assert(_ua.get_txn()->get_proxy_ssn());
  ink_assert(_ua.get_txn()->get_proxy_ssn()->accept_options);

  // default the upstream IP style host resolution order from inbound
  t_state.my_txn_conf().host_res_data.order = _ua.get_txn()->get_proxy_ssn()->accept_options->host_res_preference;

  start_sub_sm();

  // Allocate a user agent entry in the state machine's
  //   vc table
  _ua.set_entry(vc_table.new_entry());
  _ua.get_entry()->vc      = txn;
  _ua.get_entry()->vc_type = HttpVC_t::UA_VC;

  ats_ip_copy(&t_state.client_info.src_addr, netvc->get_remote_addr());
  ats_ip_copy(&t_state.client_info.dst_addr, netvc->get_local_addr());
  t_state.client_info.is_transparent = netvc->get_is_transparent();
  t_state.client_info.port_attribute = static_cast<HttpProxyPort::TransportType>(netvc->attributes);

  // Record api hook set state
  hooks_set = txn->has_hooks();

  // Setup for parsing the header
  _ua.get_entry()->vc_read_handler = &HttpSM::state_read_client_request_header;
  t_state.hdr_info.client_request.destroy();
  t_state.hdr_info.client_request.create(HTTPType::REQUEST);

  // Prepare raw reader which will live until we are sure this is HTTP indeed
  auto tts = netvc->get_service<TLSTunnelSupport>();
  if (is_transparent_passthrough_allowed() || (tts && tts->is_decryption_needed())) {
    _ua.set_raw_buffer_reader(_ua.get_txn()->get_remote_reader()->clone());
  }

  // We first need to run the transaction start hook.  Since
  //  this hook maybe asynchronous, we need to disable IO on
  //  client but set the continuation to be the state machine
  //  so if we get an timeout events the sm handles them
  _ua.get_entry()->read_vio  = txn->do_io_read(this, 0, _ua.get_txn()->get_remote_reader()->mbuf);
  _ua.get_entry()->write_vio = txn->do_io_write(this, 0, nullptr);

  /////////////////////////
  // set up timeouts     //
  /////////////////////////
  txn->set_inactivity_timeout(HRTIME_SECONDS(t_state.txn_conf->transaction_no_activity_timeout_in));
  txn->set_active_timeout(HRTIME_SECONDS(t_state.txn_conf->transaction_active_timeout_in));

  ++reentrancy_count;
  // Add our state sm to the sm list
  state_add_to_list(EVENT_NONE, nullptr);

  // This is another external entry point and it is possible for the state machine to get terminated
  // while down the call chain from @c state_add_to_list. So we need to use the reentrancy_count to
  // prevent cleanup there and do it here as we return to the external caller.
  if (terminate_sm == true && reentrancy_count == 1) {
    kill_this();
  } else {
    --reentrancy_count;
    ink_assert(reentrancy_count >= 0);
  }
}

void
HttpSM::setup_client_read_request_header()
{
  ink_assert(_ua.get_entry()->vc_read_handler == &HttpSM::state_read_client_request_header);

  _ua.get_entry()->read_vio = _ua.get_txn()->do_io_read(this, INT64_MAX, _ua.get_txn()->get_remote_reader()->mbuf);
  // The header may already be in the buffer if this
  //  a request from a keep-alive connection
  handleEvent(VC_EVENT_READ_READY, _ua.get_entry()->read_vio);
}

void
HttpSM::setup_blind_tunnel_port()
{
  NetVConnection *netvc = _ua.get_txn()->get_netvc();
  ink_release_assert(netvc);

  // This applies to both the TLS and non TLS cases
  if (t_state.hdr_info.client_request.url_get()->host_get().empty()) {
    // the URL object has not been created in the start of the transaction. Hence, we need to create the URL here
    URL u;

    t_state.hdr_info.client_request.create(HTTPType::REQUEST);
    t_state.hdr_info.client_request.method_set(static_cast<std::string_view>(HTTP_METHOD_CONNECT));
    t_state.hdr_info.client_request.url_create(&u);
    u.scheme_set(std::string_view{URL_SCHEME_TUNNEL});
    t_state.hdr_info.client_request.url_set(&u);
  }

  TLSTunnelSupport *tts = nullptr;
  if (!_ua.get_txn()->is_outbound_transparent() && (tts = netvc->get_service<TLSTunnelSupport>())) {
    if (t_state.hdr_info.client_request.url_get()->host_get().empty()) {
      if (tts->has_tunnel_destination()) {
        auto tunnel_host = tts->get_tunnel_host();
        t_state.hdr_info.client_request.url_get()->host_set(tunnel_host);
        if (tts->get_tunnel_port() > 0) {
          t_state.tunnel_port_is_dynamic = tts->tunnel_port_is_dynamic();
          t_state.hdr_info.client_request.url_get()->port_set(tts->get_tunnel_port());
        } else {
          t_state.hdr_info.client_request.url_get()->port_set(netvc->get_local_port());
        }
      } else {
        const char *server_name = "";
        if (auto *snis = netvc->get_service<TLSSNISupport>(); snis) {
          server_name = snis->get_sni_server_name();
        }
        t_state.hdr_info.client_request.url_get()->host_set({server_name});
        t_state.hdr_info.client_request.url_get()->port_set(netvc->get_local_port());
      }
    }
  } else { // If outbound transparent or not TLS, just use the local IP as the origin
    char new_host[INET6_ADDRSTRLEN];
    ats_ip_ntop(netvc->get_local_addr(), new_host, sizeof(new_host));

    t_state.hdr_info.client_request.url_get()->host_set({new_host});
    t_state.hdr_info.client_request.url_get()->port_set(netvc->get_local_port());
  }
  t_state.api_next_action = HttpTransact::StateMachineAction_t::API_TUNNEL_START;
  do_api_callout();
}

int
HttpSM::state_read_client_request_header(int event, void *data)
{
  STATE_ENTER(&HttpSM::state_read_client_request_header, event);

  ink_assert(_ua.get_entry()->read_vio == (VIO *)data);
  ink_assert(server_entry == nullptr);
  ink_assert(server_txn == nullptr);

  int bytes_used = 0;
  ink_assert(_ua.get_entry()->eos == false);

  NetVConnection *netvc = _ua.get_txn()->get_netvc();
  if (!netvc && event != VC_EVENT_EOS) {
    return 0;
  }

  switch (event) {
  case VC_EVENT_READ_READY:
  case VC_EVENT_READ_COMPLETE:
    // More data to parse
    break;

  case VC_EVENT_EOS:
    _ua.get_entry()->eos = true;
    if ((client_request_hdr_bytes > 0) && is_transparent_passthrough_allowed() && (_ua.get_raw_buffer_reader() != nullptr)) {
      break;
    }
  // Fall through
  case VC_EVENT_ERROR:
  case VC_EVENT_INACTIVITY_TIMEOUT:
  case VC_EVENT_ACTIVE_TIMEOUT:
    // The user agent is hosed.  Close it &
    //   bail on the state machine
    vc_table.cleanup_entry(_ua.get_entry());
    _ua.set_entry(nullptr);
    set_ua_abort(HttpTransact::ABORTED, event);
    terminate_sm = true;
    return 0;
  }

  // Reset the inactivity timeout if this is the first
  //   time we've been called.  The timeout had been set to
  //   the accept timeout by the ProxyTransaction
  //
  if ((_ua.get_txn()->get_remote_reader()->read_avail() > 0) && (client_request_hdr_bytes == 0)) {
    ATS_PROBE1(milestone_ua_first_read, sm_id);
    milestones[TS_MILESTONE_UA_FIRST_READ] = ink_get_hrtime();
    _ua.get_txn()->set_inactivity_timeout(HRTIME_SECONDS(t_state.txn_conf->transaction_no_activity_timeout_in));
  }
  /////////////////////
  // tokenize header //
  /////////////////////

  ParseResult state = t_state.hdr_info.client_request.parse_req(&http_parser, _ua.get_txn()->get_remote_reader(), &bytes_used,
                                                                _ua.get_entry()->eos, t_state.http_config_param->strict_uri_parsing,
                                                                t_state.http_config_param->http_request_line_max_size,
                                                                t_state.http_config_param->http_hdr_field_max_size);

  client_request_hdr_bytes += bytes_used;

  // Check to see if we are over the hdr size limit
  if (client_request_hdr_bytes > t_state.txn_conf->request_hdr_max_size) {
    SMDbg(dbg_ctl_http, "client header bytes were over max header size; treating as a bad request");
    state = ParseResult::ERROR;
  }

  // We need to handle EOS as well as READ_READY because the client
  // may have sent all of the data already followed by a FIN and that
  // should be OK.
  if (_ua.get_raw_buffer_reader() != nullptr) {
    bool do_blind_tunnel = false;
    // If we had a parse error and we're done reading data
    // blind tunnel
    if ((event == VC_EVENT_READ_READY || event == VC_EVENT_EOS) && state == ParseResult::ERROR) {
      do_blind_tunnel = true;

      // If we had a GET request that has data after the
      // get request, do blind tunnel
    } else if (state == ParseResult::DONE && t_state.hdr_info.client_request.method_get_wksidx() == HTTP_WKSIDX_GET &&
               _ua.get_txn()->get_remote_reader()->read_avail() > 0 && !t_state.hdr_info.client_request.is_keep_alive_set()) {
      do_blind_tunnel = true;
    }
    if (do_blind_tunnel) {
      SMDbg(dbg_ctl_http, "first request on connection failed parsing, switching to passthrough.");

      t_state.transparent_passthrough = true;
      http_parser_clear(&http_parser);

      // Turn off read eventing until we get the
      // blind tunnel infrastructure set up
      if (netvc) {
        netvc->do_io_read(nullptr, 0, nullptr);
      }

      /* establish blind tunnel */
      setup_blind_tunnel_port();

      // Setting half close means we will send the FIN when we've written all of the data.
      if (event == VC_EVENT_EOS) {
        this->set_ua_half_close_flag();
        t_state.client_info.keep_alive = HTTPKeepAlive::NO_KEEPALIVE;
      }
      return 0;
    }
  }

  // Check to see if we are done parsing the header
  if (state != ParseResult::CONT || _ua.get_entry()->eos || (state == ParseResult::CONT && event == VC_EVENT_READ_COMPLETE)) {
    if (_ua.get_raw_buffer_reader() != nullptr) {
      _ua.get_raw_buffer_reader()->dealloc();
      _ua.set_raw_buffer_reader(nullptr);
    }
    http_parser_clear(&http_parser);
    _ua.get_entry()->vc_read_handler  = &HttpSM::state_watch_for_client_abort;
    _ua.get_entry()->vc_write_handler = &HttpSM::state_watch_for_client_abort;
    _ua.get_txn()->cancel_inactivity_timeout();
    ATS_PROBE1(milestone_ua_read_header_done, sm_id);
    milestones[TS_MILESTONE_UA_READ_HEADER_DONE] = ink_get_hrtime();
  }

  switch (state) {
  case ParseResult::ERROR:
    SMDbg(dbg_ctl_http, "error parsing client request header");

    // Disable further I/O on the client
    _ua.get_entry()->read_vio->nbytes = _ua.get_entry()->read_vio->ndone;

    (bytes_used > t_state.http_config_param->http_request_line_max_size) ?
      t_state.http_return_code = HTTPStatus::REQUEST_URI_TOO_LONG :
      t_state.http_return_code = HTTPStatus::NONE;

    if (!is_http1_hdr_version_supported(t_state.hdr_info.client_request.version_get())) {
      t_state.http_return_code = HTTPStatus::HTTPVER_NOT_SUPPORTED;
    }

    call_transact_and_set_next_state(HttpTransact::BadRequest);
    break;

  case ParseResult::CONT:
    if (_ua.get_entry()->eos) {
      SMDbg(dbg_ctl_http_seq, "EOS before client request parsing finished");
      set_ua_abort(HttpTransact::ABORTED, event);

      // Disable further I/O on the client
      _ua.get_entry()->read_vio->nbytes = _ua.get_entry()->read_vio->ndone;

      call_transact_and_set_next_state(HttpTransact::BadRequest);
      break;
    } else if (event == VC_EVENT_READ_COMPLETE) {
      SMDbg(dbg_ctl_http_parse, "VC_EVENT_READ_COMPLETE and PARSE CONT state");
      break;
    } else {
      if (is_transparent_passthrough_allowed() && _ua.get_raw_buffer_reader() != nullptr &&
          _ua.get_raw_buffer_reader()->get_current_block()->write_avail() <= 0) {
        // Disable passthrough regardless of eventual parsing failure or success -- otherwise
        // we either have to consume some data or risk blocking the writer.
        _ua.get_raw_buffer_reader()->dealloc();
        _ua.set_raw_buffer_reader(nullptr);
      }
      _ua.get_entry()->read_vio->reenable();
      return VC_EVENT_CONT;
    }
  case ParseResult::DONE:
    SMDbg(dbg_ctl_http, "done parsing client request header");

    if (!t_state.hdr_info.client_request.check_hdr_implements()) {
      t_state.http_return_code = HTTPStatus::NOT_IMPLEMENTED;
      call_transact_and_set_next_state(HttpTransact::BadRequest);
      break;
    }

    if (!is_internal && t_state.http_config_param->scheme_proto_mismatch_policy != 0) {
      auto scheme = t_state.hdr_info.client_request.url_get()->scheme_get_wksidx();
      if ((_ua.get_client_connection_is_ssl() && (scheme == URL_WKSIDX_HTTP || scheme == URL_WKSIDX_WS)) ||
          (!_ua.get_client_connection_is_ssl() && (scheme == URL_WKSIDX_HTTPS || scheme == URL_WKSIDX_WSS))) {
        Warning("scheme [%s] vs. protocol [%s] mismatch", hdrtoken_index_to_wks(scheme),
                _ua.get_client_connection_is_ssl() ? "tls" : "plaintext");
        if (t_state.http_config_param->scheme_proto_mismatch_policy == 2) {
          t_state.http_return_code = HTTPStatus::BAD_REQUEST;
          call_transact_and_set_next_state(HttpTransact::BadRequest);
          break;
        }
      }
    }

    if (_from_early_data) {
      // Only allow early data for safe methods defined in RFC7231 Section 4.2.1.
      // https://tools.ietf.org/html/rfc7231#section-4.2.1
      SMDbg(dbg_ctl_ssl_early_data, "%d", t_state.hdr_info.client_request.method_get_wksidx());
      if (!HttpTransactHeaders::is_method_safe(t_state.hdr_info.client_request.method_get_wksidx())) {
        SMDbg(dbg_ctl_http, "client request was from early data but is NOT safe");
        call_transact_and_set_next_state(HttpTransact::TooEarly);
        return 0;
      } else if (!SSLConfigParams::server_allow_early_data_params &&
                 t_state.hdr_info.client_request.m_http->u.req.m_url_impl->m_len_query > 0) {
        SMDbg(dbg_ctl_http, "client request was from early data but HAS parameters");
        call_transact_and_set_next_state(HttpTransact::TooEarly);
        return 0;
      }
      t_state.hdr_info.client_request.mark_early_data();
    }

    _ua.get_txn()->set_session_active();

    if (t_state.hdr_info.client_request.version_get() == HTTP_1_1 &&
        (t_state.hdr_info.client_request.method_get_wksidx() == HTTP_WKSIDX_POST ||
         t_state.hdr_info.client_request.method_get_wksidx() == HTTP_WKSIDX_PUT)) {
      auto expect{t_state.hdr_info.client_request.value_get(static_cast<std::string_view>(MIME_FIELD_EXPECT))};
      if (strcasecmp(expect, static_cast<std::string_view>(HTTP_VALUE_100_CONTINUE)) == 0) {
        // When receive an "Expect: 100-continue" request from client, ATS sends a "100 Continue" response to client
        // immediately, before receive the real response from original server.
        if (t_state.http_config_param->send_100_continue_response) {
          int64_t alloc_index = buffer_size_to_index(len_100_continue_response, t_state.http_config_param->max_payload_iobuf_index);
          if (_ua.get_entry()->write_buffer) {
            free_MIOBuffer(_ua.get_entry()->write_buffer);
            _ua.get_entry()->write_buffer = nullptr;
          }
          _ua.get_entry()->write_buffer = new_MIOBuffer(alloc_index);
          IOBufferReader *buf_start     = _ua.get_entry()->write_buffer->alloc_reader();
          SMDbg(dbg_ctl_http_seq, "send 100 Continue response to client");
          int64_t nbytes             = _ua.get_entry()->write_buffer->write(str_100_continue_response, len_100_continue_response);
          _ua.get_entry()->write_vio = _ua.get_txn()->do_io_write(this, nbytes, buf_start);
          t_state.hdr_info.client_request.m_100_continue_sent = true;
        } else {
          t_state.hdr_info.client_request.m_100_continue_required = true;
        }
      }
    }

    if (t_state.hdr_info.client_request.method_get_wksidx() == HTTP_WKSIDX_PUSH &&
        t_state.http_config_param->push_method_enabled == 0) {
      SMDbg(dbg_ctl_http, "Rejecting PUSH request because push_method_enabled is 0.");
      call_transact_and_set_next_state(HttpTransact::Forbidden);
      return 0;
    }

    // Call to ensure the content-length and transfer_encoding elements in client_request are filled in
    HttpTransact::set_client_request_state(&t_state, &t_state.hdr_info.client_request);

    if (t_state.hdr_info.client_request.get_content_length() == 0 &&
        t_state.client_info.transfer_encoding != HttpTransact::TransferEncoding_t::CHUNKED) {
      // Enable further IO to watch for client aborts
      _ua.get_entry()->read_vio->reenable();
    } else if (t_state.hdr_info.client_request.method_get_wksidx() == HTTP_WKSIDX_TRACE) {
      // Trace with request body is not allowed
      call_transact_and_set_next_state(HttpTransact::BadRequest);
      return 0;
    } else {
      // Disable further I/O on the client since there could
      //  be body that we are tunneling POST/PUT/CONNECT or
      //  extension methods and we can't issue another
      //  another IO later for the body with a different buffer
      _ua.get_entry()->read_vio->nbytes = _ua.get_entry()->read_vio->ndone;
    }

    call_transact_and_set_next_state(HttpTransact::ModifyRequest);

    break;
  default:
    ink_assert(!"not reached");
  }

  return 0;
}

void
HttpSM::wait_for_full_body()
{
  is_waiting_for_full_body = true;
  HTTP_SM_SET_DEFAULT_HANDLER(&HttpSM::tunnel_handler_post);
  bool                chunked = (t_state.client_info.transfer_encoding == HttpTransact::TransferEncoding_t::CHUNKED);
  int64_t             alloc_index;
  HttpTunnelProducer *p = nullptr;

  // content length is undefined, use default buffer size
  if (t_state.hdr_info.request_content_length == HTTP_UNDEFINED_CL) {
    alloc_index = static_cast<int>(t_state.txn_conf->default_buffer_size_index);
    if (alloc_index < MIN_CONFIG_BUFFER_SIZE_INDEX || alloc_index > MAX_BUFFER_SIZE_INDEX) {
      alloc_index = DEFAULT_REQUEST_BUFFER_SIZE_INDEX;
    }
  } else {
    alloc_index = buffer_size_to_index(t_state.hdr_info.request_content_length, t_state.http_config_param->max_payload_iobuf_index);
  }
  MIOBuffer      *post_buffer = new_MIOBuffer(alloc_index);
  IOBufferReader *buf_start   = post_buffer->alloc_reader();

  this->_postbuf.init(post_buffer->clone_reader(buf_start));

  // Note: Many browsers, Netscape and IE included send two extra
  //  bytes (CRLF) at the end of the post.  We just ignore those
  //  bytes since the sending them is not spec

  // Next order of business if copy the remaining data from the
  //  header buffer into new buffer
  int64_t post_bytes = chunked ? INT64_MAX : t_state.hdr_info.request_content_length;
  post_buffer->write(_ua.get_txn()->get_remote_reader(), chunked ? _ua.get_txn()->get_remote_reader()->read_avail() : post_bytes);

  p = tunnel.add_producer(_ua.get_entry()->vc, post_bytes, buf_start, &HttpSM::tunnel_handler_post_ua,
                          HttpTunnelType_t::BUFFER_READ, "ua post buffer");
  if (chunked) {
    bool const drop_chunked_trailers = t_state.http_config_param->oride.http_drop_chunked_trailers == 1;
    bool const parse_chunk_strictly  = t_state.http_config_param->oride.http_strict_chunk_parsing == 1;
    tunnel.set_producer_chunking_action(p, 0, TunnelChunkingAction_t::PASSTHRU_CHUNKED_CONTENT, drop_chunked_trailers,
                                        parse_chunk_strictly);
  }
  _ua.get_entry()->in_tunnel = true;
  _ua.get_txn()->set_inactivity_timeout(HRTIME_SECONDS(t_state.txn_conf->transaction_no_activity_timeout_in));
  tunnel.tunnel_run(p);
}

int
HttpSM::state_watch_for_client_abort(int event, void *data)
{
  STATE_ENTER(&HttpSM::state_watch_for_client_abort, event);

  ink_assert(_ua.get_entry()->read_vio == (VIO *)data || _ua.get_entry()->write_vio == (VIO *)data);
  ink_assert(_ua.get_entry()->vc == _ua.get_txn());

  switch (event) {
  /* EOS means that the client has initiated the connection shut down.
   * Only half close the client connection so ATS can read additional
   * data that may still be sent from the server and send it to the
   * client.
   */
  case VC_EVENT_EOS: {
    // We got an early EOS. If the tunnal has cache writer, don't kill it for background fill.
    if (!terminate_sm) { // Not done already
      NetVConnection *netvc = _ua.get_txn()->get_netvc();
      if (_ua.get_txn()->allow_half_open() || tunnel.has_consumer_besides_client()) {
        if (netvc) {
          netvc->do_io_shutdown(IO_SHUTDOWN_READ);
        }
      } else {
        _ua.get_txn()->do_io_close();
        vc_table.cleanup_entry(_ua.get_entry());
        _ua.set_entry(nullptr);
        tunnel.kill_tunnel();
        terminate_sm = true; // Just die already, the requester is gone
        set_ua_abort(HttpTransact::ABORTED, event);
      }
      if (_ua.get_entry()) {
        _ua.get_entry()->eos = true;
      }
    }
    break;
  }
  case VC_EVENT_ERROR:
  case VC_EVENT_ACTIVE_TIMEOUT:
  case VC_EVENT_INACTIVITY_TIMEOUT: {
    if (tunnel.is_tunnel_active()) {
      // Check to see if the user agent is part of the tunnel.
      //  If so forward the event to the tunnel.  Otherwise,
      //  kill the tunnel and fallthrough to the case
      //  where the tunnel is not active
      HttpTunnelConsumer *c = tunnel.get_consumer(_ua.get_txn());
      if (c && c->alive) {
        SMDbg(dbg_ctl_http, "forwarding event %s to tunnel", HttpDebugNames::get_event_name(event));
        tunnel.handleEvent(event, c->write_vio);
        return 0;
      } else {
        tunnel.kill_tunnel();
      }
    }
    // Disable further I/O on the client
    if (_ua.get_entry()->read_vio) {
      _ua.get_entry()->read_vio->nbytes = _ua.get_entry()->read_vio->ndone;
    }
    ATS_PROBE1(milestone_ua_close, sm_id);
    milestones[TS_MILESTONE_UA_CLOSE] = ink_get_hrtime();
    set_ua_abort(HttpTransact::ABORTED, event);

    terminate_sm = true;
    break;
  }
  case VC_EVENT_READ_COMPLETE:
  // XXX Work around for TS-1233.
  case VC_EVENT_READ_READY:
    //  Ignore.  Could be a pipelined request.  We'll get to  it
    //    when we finish the current transaction
    break;
  case VC_EVENT_WRITE_READY:
    // 100-continue handler
    ink_assert(t_state.hdr_info.client_request.m_100_continue_required || t_state.http_config_param->send_100_continue_response);
    _ua.get_entry()->write_vio->reenable();
    break;
  case VC_EVENT_WRITE_COMPLETE:
    // 100-continue handler
    ink_assert(t_state.hdr_info.client_request.m_100_continue_required || t_state.http_config_param->send_100_continue_response);
    if (_ua.get_entry()->write_buffer) {
      ink_assert(_ua.get_entry()->write_vio && !_ua.get_entry()->write_vio->ntodo());
      free_MIOBuffer(_ua.get_entry()->write_buffer);
      _ua.get_entry()->write_buffer = nullptr;
    }
    break;
  default:
    ink_release_assert(0);
    break;
  }

  return 0;
}

void
HttpSM::setup_push_read_response_header()
{
  ink_assert(server_txn == nullptr);
  ink_assert(server_entry == nullptr);
  ink_assert(_ua.get_txn() != nullptr);
  ink_assert(t_state.method == HTTP_WKSIDX_PUSH);

  // Set the handler to read the pushed response hdr
  _ua.get_entry()->vc_read_handler = &HttpSM::state_read_push_response_header;

  // We record both the total payload size as
  //  client_request_body_bytes and the bytes for the individual
  //  pushed hdr and body components
  pushed_response_hdr_bytes = 0;
  client_request_body_bytes = 0;

  // Note: we must use destroy() here since clear()
  //  does not free the memory from the header
  t_state.hdr_info.server_response.destroy();
  t_state.hdr_info.server_response.create(HTTPType::RESPONSE);
  http_parser_clear(&http_parser);

  // We already done the READ when we read the client
  //  request header
  ink_assert(_ua.get_entry()->read_vio);

  // If there is anything in the buffer call the parsing routines
  //  since if the response is finished, we won't get any
  //  additional callbacks
  int resp_hdr_state = VC_EVENT_CONT;
  if (_ua.get_txn()->get_remote_reader()->read_avail() > 0) {
    if (_ua.get_entry()->eos) {
      resp_hdr_state = state_read_push_response_header(VC_EVENT_EOS, _ua.get_entry()->read_vio);
    } else {
      resp_hdr_state = state_read_push_response_header(VC_EVENT_READ_READY, _ua.get_entry()->read_vio);
    }
  }
  // It is possible that the entire PUSHed response header was already
  //  in the buffer.  In this case we don't want to fire off any more
  //  IO since we are going to switch buffers when we go to tunnel to
  //  the cache
  if (resp_hdr_state == VC_EVENT_CONT) {
    ink_assert(_ua.get_entry()->eos == false);
    _ua.get_entry()->read_vio = _ua.get_txn()->do_io_read(this, INT64_MAX, _ua.get_txn()->get_remote_reader()->mbuf);
  }
}

int
HttpSM::state_read_push_response_header(int event, void *data)
{
  STATE_ENTER(&HttpSM::state_read_push_response_header, event);
  ink_assert(_ua.get_entry()->read_vio == (VIO *)data);
  ink_assert(t_state.current.server == nullptr);

  switch (event) {
  case VC_EVENT_EOS:
    _ua.get_entry()->eos = true;
    // Fall through

  case VC_EVENT_READ_READY:
  case VC_EVENT_READ_COMPLETE:
    // More data to parse
    break;

  case VC_EVENT_ERROR:
  case VC_EVENT_INACTIVITY_TIMEOUT:
  case VC_EVENT_ACTIVE_TIMEOUT:
    // The user agent is hosed.  Send an error
    set_ua_abort(HttpTransact::ABORTED, event);
    call_transact_and_set_next_state(HttpTransact::HandleBadPushRespHdr);
    return 0;
  }

  auto state = ParseResult::CONT;
  while (_ua.get_txn()->get_remote_reader()->read_avail() && state == ParseResult::CONT) {
    const char *start     = _ua.get_txn()->get_remote_reader()->start();
    const char *tmp       = start;
    int64_t     data_size = _ua.get_txn()->get_remote_reader()->block_read_avail();
    ink_assert(data_size >= 0);

    /////////////////////
    // tokenize header //
    /////////////////////
    state = t_state.hdr_info.server_response.parse_resp(&http_parser, &tmp, tmp + data_size,
                                                        false); // Only call w/ eof when data exhausted

    int64_t bytes_used = tmp - start;

    ink_release_assert(bytes_used <= data_size);
    _ua.get_txn()->get_remote_reader()->consume(bytes_used);
    pushed_response_hdr_bytes += bytes_used;
    client_request_body_bytes += bytes_used;
  }

  // We are out of data.  If we've received an EOS we need to
  //  call the parser with (eof == true) so it can determine
  //  whether to use the response as is or declare a parse error
  if (_ua.get_entry()->eos) {
    const char *end = _ua.get_txn()->get_remote_reader()->start();
    state = t_state.hdr_info.server_response.parse_resp(&http_parser, &end, end, true // We are out of data after server eos
    );
    ink_release_assert(state == ParseResult::DONE || state == ParseResult::ERROR);
  }
  // Don't allow 0.9 (unparsable headers) since TS doesn't
  //   cache 0.9 responses
  if (state == ParseResult::DONE && t_state.hdr_info.server_response.version_get() == HTTP_0_9) {
    state = ParseResult::ERROR;
  }

  if (state != ParseResult::CONT) {
    // Disable further IO
    _ua.get_entry()->read_vio->nbytes = _ua.get_entry()->read_vio->ndone;
    http_parser_clear(&http_parser);
    ATS_PROBE1(milestone_server_read_header_done, sm_id);
    milestones[TS_MILESTONE_SERVER_READ_HEADER_DONE] = ink_get_hrtime();
  }

  switch (state) {
  case ParseResult::ERROR:
    SMDbg(dbg_ctl_http, "error parsing push response header");
    call_transact_and_set_next_state(HttpTransact::HandleBadPushRespHdr);
    break;

  case ParseResult::CONT:
    _ua.get_entry()->read_vio->reenable();
    return VC_EVENT_CONT;

  case ParseResult::DONE:
    SMDbg(dbg_ctl_http, "done parsing push response header");
    call_transact_and_set_next_state(HttpTransact::HandlePushResponseHdr);
    break;
  default:
    ink_assert(!"not reached");
  }

  return VC_EVENT_DONE;
}

//////////////////////////////////////////////////////////////////////////////
//
//  HttpSM::state_raw_http_server_open()
//
//////////////////////////////////////////////////////////////////////////////
int
HttpSM::state_raw_http_server_open(int event, void *data)
{
  STATE_ENTER(&HttpSM::state_raw_http_server_open, event);
  ink_assert(server_entry == nullptr);
  ATS_PROBE1(milestone_server_connect_end, sm_id);
  milestones[TS_MILESTONE_SERVER_CONNECT_END] = ink_get_hrtime();
  NetVConnection *netvc                       = nullptr;

  pending_action = nullptr;
  switch (event) {
  case NET_EVENT_OPEN: {
    // Record the VC in our table
    server_entry     = vc_table.new_entry();
    server_entry->vc = netvc = static_cast<NetVConnection *>(data);
    server_entry->vc_type    = HttpVC_t::RAW_SERVER_VC;
    t_state.current.state    = HttpTransact::CONNECTION_ALIVE;
    ats_ip_copy(&t_state.server_info.src_addr, netvc->get_local_addr());

    netvc->set_inactivity_timeout(get_server_inactivity_timeout());
    netvc->set_active_timeout(get_server_active_timeout());
    t_state.current.server->clear_connect_fail();

    if (get_tunnel_type() != SNIRoutingType::NONE) {
      tunnel.mark_tls_tunnel_active();
    }

    break;
  }
  case VC_EVENT_ERROR:
  case VC_EVENT_EOS:
  case NET_EVENT_OPEN_FAILED:
    t_state.current.state = HttpTransact::OPEN_RAW_ERROR;
    // use this value just to get around other values
    t_state.hdr_info.response_error = HttpTransact::ResponseError_t::STATUS_CODE_SERVER_ERROR;
    break;
  case EVENT_INTERVAL:
    // If we get EVENT_INTERNAL it means that we moved the transaction
    // to a different thread in do_http_server_open.  Since we didn't
    // do any of the actual work in do_http_server_open, we have to
    // go back and do it now.
    do_http_server_open(true);
    return 0;
  default:
    ink_release_assert(0);
    break;
  }

  call_transact_and_set_next_state(HttpTransact::OriginServerRawOpen);
  return 0;
}

// int HttpSM::state_request_wait_for_transform_read(int event, void* data)
//
//   We've done a successful transform open and issued a do_io_write
//    to the transform.  We are now ready for the transform  to tell
//    us it is now ready to be read from and it done modifying the
//    server request header
//
int
HttpSM::state_request_wait_for_transform_read(int event, void *data)
{
  STATE_ENTER(&HttpSM::state_request_wait_for_transform_read, event);
  int64_t size;

  switch (event) {
  case TRANSFORM_READ_READY:
    size = *(static_cast<int64_t *>(data));
    if (size != INT64_MAX && size >= 0) {
      // We got a content length so update our internal
      //   data as well as fix up the request header
      t_state.hdr_info.transform_request_cl = size;
      t_state.hdr_info.server_request.value_set_int64(static_cast<std::string_view>(MIME_FIELD_CONTENT_LENGTH), size);
      setup_server_send_request_api();
      break;
    } else {
      // No content length from the post.  This is a no go
      //  since http spec requires content length when
      //  sending a request message body.  Change the event
      //  to an error and fall through
      event = VC_EVENT_ERROR;
      Log::error("Request transformation failed to set content length");
    }
  // FALLTHROUGH
  default:
    state_common_wait_for_transform_read(&post_transform_info, &HttpSM::tunnel_handler_post, event, data);
    break;
  }

  return 0;
}

// int HttpSM::state_response_wait_for_transform_read(int event, void* data)
//
//   We've done a successful transform open and issued a do_io_write
//    to the transform.  We are now ready for the transform  to tell
//    us it is now ready to be read from and it done modifying the
//    user agent response header
//
int
HttpSM::state_response_wait_for_transform_read(int event, void *data)
{
  STATE_ENTER(&HttpSM::state_response_wait_for_transform_read, event);
  int64_t size = *(static_cast<int64_t *>(data));

  switch (event) {
  case TRANSFORM_READ_READY:
    if (size != INT64_MAX && size >= 0) {
      // We got a content length so update our internal state
      t_state.hdr_info.transform_response_cl = size;
      t_state.hdr_info.transform_response.value_set_int64(static_cast<std::string_view>(MIME_FIELD_CONTENT_LENGTH), size);
    } else {
      t_state.hdr_info.transform_response_cl = HTTP_UNDEFINED_CL;
    }
    call_transact_and_set_next_state(HttpTransact::handle_transform_ready);
    break;
  default:
    state_common_wait_for_transform_read(&transform_info, &HttpSM::tunnel_handler, event, data);
    break;
  }

  return 0;
}

// int HttpSM::state_common_wait_for_transform_read(...)
//
//   This function handles the overlapping cases between request and response
//     transforms which prevents code duplication
//
int
HttpSM::state_common_wait_for_transform_read(HttpTransformInfo *t_info, HttpSMHandler tunnel_handler, int event, void *data)
{
  STATE_ENTER(&HttpSM::state_common_wait_for_transform_read, event);
  HttpTunnelConsumer *c = nullptr;

  switch (event) {
  case HTTP_TUNNEL_EVENT_DONE:
    // There are three reasons why the tunnel could signal completed
    //   1) there was error from the transform write
    //   2) there was an error from the data source
    //   3) the transform write completed before it sent
    //      TRANSFORM_READ_READY which is legal and in which
    //      case we should just wait for the transform read ready
    c = tunnel.get_consumer(t_info->vc);
    ink_assert(c != nullptr);
    ink_assert(c->vc == t_info->entry->vc);

    if (c->handler_state == HTTP_SM_TRANSFORM_FAIL) {
      // Case 1 we failed to complete the write to the
      //  transform fall through to vc event error case
      ink_assert(c->write_success == false);
    } else if (c->producer->read_success == false) {
      // Case 2 - error from data source
      if (c->producer->vc_type == HttpTunnelType_t::HTTP_CLIENT) {
        // Our source is the client.  POST can't
        //   be truncated so forward to the tunnel
        //   handler to clean this mess up
        ink_assert(t_info == &post_transform_info);
        return (this->*tunnel_handler)(event, data);
      } else {
        // On the response side, we just forward as much
        //   as we can of truncated documents so
        //   just don't cache the result
        ink_assert(t_info == &transform_info);
        t_state.api_info.cache_transformed = false;
        return 0;
      }
    } else {
      // Case 3 - wait for transform read ready
      return 0;
    }
  // FALLTHROUGH
  case VC_EVENT_ERROR:
  case VC_EVENT_EOS:
  case VC_EVENT_INACTIVITY_TIMEOUT:
    // Transform VC sends NULL on error conditions
    if (!c) {
      c = tunnel.get_consumer(t_info->vc);
      ink_assert(c != nullptr);
    }
    vc_table.cleanup_entry(t_info->entry);
    t_info->entry = nullptr;
    // In Case 1: error due to transform write,
    // we need to keep the original t_info->vc for transform_cleanup()
    // to skip do_io_close(); otherwise, set it to NULL.
    if (c->handler_state != HTTP_SM_TRANSFORM_FAIL) {
      t_info->vc = nullptr;
    }
    if (c->producer->vc_type == HttpTunnelType_t::HTTP_CLIENT) {
      /* Producer was the user agent and there was a failure transforming the POST.
         Handling this is challenging and this isn't the best way but it at least
         avoids a crash due to trying to send a response to a NULL'd out user agent.
         The problem with not closing the user agent is handling draining of the
         rest of the POST - the user agent may well not check for a response until that's
         done in which case we can get a deadlock where the user agent never reads the
         error response because the POST wasn't drained and the buffers filled up.
         Draining has a potential bad impact on any pipelining which must be considered.
         If we're not going to drain properly the next best choice is to shut down the
         entire state machine since (1) there's no point in finishing the POST to the
         origin and (2) there's no user agent connection to which to send the error response.
      */
      terminate_sm = true;
    } else {
      tunnel.kill_tunnel();
      call_transact_and_set_next_state(HttpTransact::HandleApiErrorJump);
    }
    break;
  default:
    ink_release_assert(0);
  }

  return 0;
}

// int HttpSM::state_api_callback(int event, void *data)

//   InkAPI.cc calls us directly here to avoid problems
//    with setting and changing the default_handler
//    function.  As such, this is an entry point
//    and needs to handle the reentrancy counter and
//    deallocation of the state machine if necessary
//
int
HttpSM::state_api_callback(int event, void *data)
{
  ink_release_assert(magic == HttpSmMagic_t::ALIVE);

  ink_assert(reentrancy_count >= 0);
  reentrancy_count++;

  this->milestone_update_api_time();

  STATE_ENTER(&HttpSM::state_api_callback, event);

  state_api_callout(event, data);

  // The sub-handler signals when it is time for the state
  //  machine to exit.  We can only exit if we are not reentrantly
  //  called otherwise when the our call unwinds, we will be
  //  running on a dead state machine
  //
  // Because of the need for an api shutdown hook, kill_this()
  //  is also reentrant.  As such, we don't want to decrement
  //  the reentrancy count until after we run kill_this()
  //
  if (terminate_sm == true && reentrancy_count == 1) {
    kill_this();
  } else {
    reentrancy_count--;
    ink_assert(reentrancy_count >= 0);
  }

  return VC_EVENT_CONT;
}

int
HttpSM::state_api_callout(int event, void * /* data ATS_UNUSED */)
{
  // enum and variable for figuring out what the next action is after
  //   after we've finished the api state
  enum class AfterApiReturn_t {
    UNKNOWN = 0,
    CONTINUE,
    DEFERED_CLOSE,
    DEFERED_SERVER_ERROR,
    ERROR_JUMP,
    SHUTDOWN,
    INVALIDATE_ERROR
  };
  AfterApiReturn_t api_next = AfterApiReturn_t::UNKNOWN;

  if (event != EVENT_NONE) {
    STATE_ENTER(&HttpSM::state_api_callout, event);
  }

  if (api_timer < 0) {
    // This happens when either the plugin lock was missed and the hook rescheduled or
    // the transaction got an event without the plugin calling TsHttpTxnReenable().
    // The call chain does not recurse here if @a api_timer < 0 which means this call
    // is the first from an event dispatch in this case.
    this->milestone_update_api_time();
  }

  switch (event) {
  case HTTP_TUNNEL_EVENT_DONE:
  // This is a reschedule via the tunnel.  Just fall through
  //
  case EVENT_INTERVAL:
    pending_action = nullptr;
  // FALLTHROUGH
  case EVENT_NONE:
    if (cur_hook_id == TS_HTTP_TXN_START_HOOK && t_state.client_info.port_attribute == HttpProxyPort::TRANSPORT_BLIND_TUNNEL) {
      /* Creating the request object early to set the host header and port for blind tunneling here for the
plugins required to work with sni_routing.
*/
      // Plugins triggered on txn_start_hook will get the host and port at that point
      // We've received a request on a port which we blind forward
      URL u;

      t_state.hdr_info.client_request.create(HTTPType::REQUEST);
      t_state.hdr_info.client_request.method_set(static_cast<std::string_view>(HTTP_METHOD_CONNECT));
      t_state.hdr_info.client_request.url_create(&u);
      u.scheme_set(std::string_view{URL_SCHEME_TUNNEL});
      t_state.hdr_info.client_request.url_set(&u);

      NetVConnection *netvc = _ua.get_txn()->get_netvc();
      if (auto tts = netvc->get_service<TLSTunnelSupport>(); tts) {
        if (tts->has_tunnel_destination()) {
          auto tunnel_host = tts->get_tunnel_host();
          t_state.hdr_info.client_request.url_get()->host_set(tunnel_host);
          ushort tunnel_port = tts->get_tunnel_port();
          if (tunnel_port > 0) {
            t_state.hdr_info.client_request.url_get()->port_set(tunnel_port);
          } else {
            t_state.hdr_info.client_request.url_get()->port_set(netvc->get_local_port());
          }
        } else {
          const char *server_name = "";
          if (auto *snis = netvc->get_service<TLSSNISupport>(); snis) {
            server_name = snis->get_sni_server_name();
          }
          t_state.hdr_info.client_request.url_get()->host_set({server_name});
          t_state.hdr_info.client_request.url_get()->port_set(netvc->get_local_port());
        }
      }
    }
  // FALLTHROUGH
  case HTTP_API_CONTINUE:
    if (nullptr == cur_hook) {
      cur_hook = hook_state.getNext();
    }
    if (cur_hook) {
      if (callout_state == HttpApiState_t::NO_CALLOUT) {
        callout_state = HttpApiState_t::IN_CALLOUT;
      }

      WEAK_MUTEX_TRY_LOCK(lock, cur_hook->m_cont->mutex, mutex->thread_holding);

      // Have a mutex but didn't get the lock, reschedule
      if (!lock.is_locked()) {
        api_timer = -ink_get_hrtime();
        HTTP_SM_SET_DEFAULT_HANDLER(&HttpSM::state_api_callout);
        ink_release_assert(pending_action.empty());
        pending_action = this_ethread()->schedule_in(this, HRTIME_MSECONDS(10));
        return -1;
      }

      SMDbg(dbg_ctl_http, "calling plugin on hook %s at hook %p", HttpDebugNames::get_api_hook_name(cur_hook_id), cur_hook);

      APIHook const *hook = cur_hook;
      // Need to delay the next hook update until after this hook is called to handle dynamic
      // callback manipulation. cur_hook isn't needed to track state (in hook_state).
      cur_hook = nullptr;

      if (!api_timer) {
        api_timer = ink_get_hrtime();
      }

      hook->invoke(TS_EVENT_HTTP_READ_REQUEST_HDR + static_cast<int>(cur_hook_id), this);
      if (api_timer > 0) { // true if the hook did not call TxnReenable()
        this->milestone_update_api_time();
        api_timer = -ink_get_hrtime(); // set in order to track non-active callout duration
        // which means that if we get back from the invoke with api_timer < 0 we're already
        // tracking a non-complete callout from a chain so just let it ride. It will get cleaned
        // up in state_api_callback when the plugin re-enables this transaction.
      }
      return 0;
    }
    // Map the callout state into api_next
    switch (callout_state) {
    case HttpApiState_t::NO_CALLOUT:
    case HttpApiState_t::IN_CALLOUT:
      if (t_state.api_modifiable_cached_resp && t_state.api_update_cached_object == HttpTransact::UpdateCachedObject_t::PREPARE) {
        t_state.api_update_cached_object = HttpTransact::UpdateCachedObject_t::CONTINUE;
      }
      api_next = AfterApiReturn_t::CONTINUE;
      break;
    case HttpApiState_t::DEFERED_CLOSE:
      api_next = AfterApiReturn_t::DEFERED_CLOSE;
      break;
    case HttpApiState_t::DEFERED_SERVER_ERROR:
      api_next = AfterApiReturn_t::DEFERED_SERVER_ERROR;
      break;
    case HttpApiState_t::REWIND_STATE_MACHINE:
      SMDbg(dbg_ctl_http, "REWIND");
      callout_state = HttpApiState_t::NO_CALLOUT;
      set_next_state();
      return 0;
    default:
      ink_release_assert(0);
    }
    break;

  case HTTP_API_ERROR:
    if (callout_state == HttpApiState_t::DEFERED_CLOSE) {
      api_next = AfterApiReturn_t::DEFERED_CLOSE;
    } else if (cur_hook_id == TS_HTTP_TXN_CLOSE_HOOK) {
      // If we are closing the state machine, we can't
      //   jump to an error state so just continue
      api_next = AfterApiReturn_t::CONTINUE;
    } else if (t_state.api_http_sm_shutdown) {
      t_state.api_http_sm_shutdown   = false;
      t_state.cache_info.object_read = nullptr;
      cache_sm.close_read();
      transform_cache_sm.close_read();
      release_server_session();
      terminate_sm                 = true;
      api_next                     = AfterApiReturn_t::SHUTDOWN;
      t_state.squid_codes.log_code = SquidLogCode::TCP_DENIED;
    } else if (t_state.api_modifiable_cached_resp &&
               t_state.api_update_cached_object == HttpTransact::UpdateCachedObject_t::PREPARE) {
      t_state.api_update_cached_object = HttpTransact::UpdateCachedObject_t::ERROR;
      api_next                         = AfterApiReturn_t::INVALIDATE_ERROR;
    } else {
      api_next = AfterApiReturn_t::ERROR_JUMP;
    }
    break;

  // Eat the EOS while we are waiting for any locks to complete the transaction
  case VC_EVENT_EOS:
    return 0;

  default:
    ink_assert(false);
    terminate_sm = true;
    return 0;
  }

  // Now that we're completed with the api state and figured out what
  //   to do next, do it
  callout_state = HttpApiState_t::NO_CALLOUT;
  api_timer     = 0;
  switch (api_next) {
  case AfterApiReturn_t::CONTINUE:
    handle_api_return();
    break;
  case AfterApiReturn_t::DEFERED_CLOSE:
    ink_assert(t_state.api_next_action == HttpTransact::StateMachineAction_t::API_SM_SHUTDOWN);
    do_api_callout();
    break;
  case AfterApiReturn_t::DEFERED_SERVER_ERROR:
    ink_assert(t_state.api_next_action == HttpTransact::StateMachineAction_t::API_SEND_REQUEST_HDR);
    ink_assert(t_state.current.state != HttpTransact::CONNECTION_ALIVE);
    call_transact_and_set_next_state(HttpTransact::HandleResponse);
    break;
  case AfterApiReturn_t::ERROR_JUMP:
    call_transact_and_set_next_state(HttpTransact::HandleApiErrorJump);
    break;
  case AfterApiReturn_t::SHUTDOWN:
    break;
  case AfterApiReturn_t::INVALIDATE_ERROR:
    do_cache_prepare_update();
    break;
  default:
  case AfterApiReturn_t::UNKNOWN:
    ink_release_assert(0);
  }

  return 0;
}

// void HttpSM::handle_api_return()
//
//   Figures out what to do after calling api callouts
//    have finished.  This is messy and I would like
//    to come up with a cleaner way to handle the api
//    return.  The way we are doing things also makes a
//    mess of set_next_state()
//
void
HttpSM::handle_api_return()
{
  switch (t_state.api_next_action) {
  case HttpTransact::StateMachineAction_t::API_SM_START: {
    NetVConnection *netvc        = _ua.get_txn()->get_netvc();
    auto           *tts          = netvc->get_service<TLSTunnelSupport>();
    bool            forward_dest = tts != nullptr && tts->is_decryption_needed();
    if (t_state.client_info.port_attribute == HttpProxyPort::TRANSPORT_BLIND_TUNNEL || forward_dest) {
      setup_blind_tunnel_port();
    } else {
      setup_client_read_request_header();
    }
    return;
  }
  case HttpTransact::StateMachineAction_t::API_CACHE_LOOKUP_COMPLETE:
  case HttpTransact::StateMachineAction_t::API_READ_CACHE_HDR:
    if (t_state.api_cleanup_cache_read && t_state.api_update_cached_object != HttpTransact::UpdateCachedObject_t::PREPARE) {
      t_state.api_cleanup_cache_read = false;
      t_state.cache_info.object_read = nullptr;
      t_state.request_sent_time      = UNDEFINED_TIME;
      t_state.response_received_time = UNDEFINED_TIME;
      cache_sm.close_read();
      transform_cache_sm.close_read();
    }
    // fallthrough

  case HttpTransact::StateMachineAction_t::API_PRE_REMAP:
  case HttpTransact::StateMachineAction_t::API_POST_REMAP:
  case HttpTransact::StateMachineAction_t::API_READ_REQUEST_HDR:
  case HttpTransact::StateMachineAction_t::REQUEST_BUFFER_READ_COMPLETE:
  case HttpTransact::StateMachineAction_t::API_OS_DNS:
  case HttpTransact::StateMachineAction_t::API_READ_RESPONSE_HDR:
    call_transact_and_set_next_state(nullptr);
    return;
  case HttpTransact::StateMachineAction_t::API_TUNNEL_START:
    // Finished the Tunnel start callback.  Go ahead and do the HandleBlindTunnel
    call_transact_and_set_next_state(HttpTransact::HandleBlindTunnel);
    return;
  case HttpTransact::StateMachineAction_t::API_SEND_REQUEST_HDR:
    setup_server_send_request();
    return;
  case HttpTransact::StateMachineAction_t::API_SEND_RESPONSE_HDR:
    // Set back the inactivity timeout
    if (_ua.get_txn()) {
      _ua.get_txn()->set_inactivity_timeout(HRTIME_SECONDS(t_state.txn_conf->transaction_no_activity_timeout_in));
    }

    // We only follow 3xx when redirect_in_process == false. Otherwise the redirection has already been launched (in
    // StateMachineAction_t::SERVER_READ).redirect_in_process is set before this logic if we need more direction.
    // This redirection is only used with the build_error_response. Then, the redirection_tries will be increased by
    // state_read_server_response_header and never get into this logic again.
    if (enable_redirection && !t_state.redirect_info.redirect_in_process && is_redirect_required()) {
      do_redirect();
    }
    // we have further processing to do
    //  based on what t_state.next_action is
    break;
  case HttpTransact::StateMachineAction_t::API_SM_SHUTDOWN:
    state_remove_from_list(EVENT_NONE, nullptr);
    return;
  default:
    ink_release_assert(!"Not reached");
    break;
  }

  switch (t_state.next_action) {
  case HttpTransact::StateMachineAction_t::TRANSFORM_READ: {
    HttpTunnelProducer *p = setup_transfer_from_transform();
    perform_transform_cache_write_action();
    tunnel.tunnel_run(p);
    break;
  }
  case HttpTransact::StateMachineAction_t::SERVER_READ: {
    if (unlikely(t_state.did_upgrade_succeed)) {
      // We've successfully handled the upgrade, let's now setup
      // a blind tunnel.
      IOBufferReader *initial_data = nullptr;
      if (t_state.is_websocket) {
        Metrics::Gauge::increment(http_rsb.websocket_current_active_client_connections);
        if (server_txn) {
          initial_data = server_txn->get_remote_reader();
        }

        if (_ua.get_txn()) {
          SMDbg(dbg_ctl_http_websocket,
                "(client session) Setting websocket active timeout=%" PRId64 "s and inactive timeout=%" PRId64 "s",
                t_state.txn_conf->websocket_active_timeout, t_state.txn_conf->websocket_inactive_timeout);
          _ua.get_txn()->set_active_timeout(HRTIME_SECONDS(t_state.txn_conf->websocket_active_timeout));
          _ua.get_txn()->set_inactivity_timeout(HRTIME_SECONDS(t_state.txn_conf->websocket_inactive_timeout));
        }

        if (server_txn) {
          SMDbg(dbg_ctl_http_websocket,
                "(server session) Setting websocket active timeout=%" PRId64 "s and inactive timeout=%" PRId64 "s",
                t_state.txn_conf->websocket_active_timeout, t_state.txn_conf->websocket_inactive_timeout);
          server_txn->set_active_timeout(HRTIME_SECONDS(t_state.txn_conf->websocket_active_timeout));
          server_txn->set_inactivity_timeout(HRTIME_SECONDS(t_state.txn_conf->websocket_inactive_timeout));
        }
      }

      setup_blind_tunnel(true, initial_data);
    } else {
      HttpTunnelProducer *p = setup_server_transfer();
      perform_cache_write_action();
      tunnel.tunnel_run(p);
    }
    break;
  }
  case HttpTransact::StateMachineAction_t::SERVE_FROM_CACHE: {
    HttpTunnelProducer *p = setup_cache_read_transfer();
    tunnel.tunnel_run(p);
    break;
  }

  case HttpTransact::StateMachineAction_t::INTERNAL_CACHE_WRITE: {
    if (cache_sm.cache_write_vc) {
      setup_internal_transfer(&HttpSM::tunnel_handler_cache_fill);
    } else {
      setup_internal_transfer(&HttpSM::tunnel_handler);
    }
    break;
  }

  case HttpTransact::StateMachineAction_t::INTERNAL_CACHE_NOOP:
  case HttpTransact::StateMachineAction_t::INTERNAL_CACHE_DELETE:
  case HttpTransact::StateMachineAction_t::INTERNAL_CACHE_UPDATE_HEADERS:
  case HttpTransact::StateMachineAction_t::SEND_ERROR_CACHE_NOOP: {
    setup_internal_transfer(&HttpSM::tunnel_handler);
    break;
  }

  case HttpTransact::StateMachineAction_t::REDIRECT_READ: {
    // Clean up from any communication with previous servers
    release_server_session();

    call_transact_and_set_next_state(HttpTransact::HandleRequest);
    break;
  }
  case HttpTransact::StateMachineAction_t::SSL_TUNNEL: {
    setup_blind_tunnel(true);
    break;
  }
  default: {
    ink_release_assert(!"Should not get here");
  }
  }
}

PoolableSession *
HttpSM::create_server_session(NetVConnection &netvc, MIOBuffer *netvc_read_buffer, IOBufferReader *netvc_reader)
{
  // Figure out what protocol was negotiated
  int proto_index = SessionProtocolNameRegistry::INVALID;
  if (auto const alpn = netvc.get_service<ALPNSupport>(); alpn) {
    proto_index = alpn->get_negotiated_protocol_id();
  }
  // No ALPN occurred. Assume it was HTTP/1.x and hope for the best
  if (proto_index == SessionProtocolNameRegistry::INVALID) {
    proto_index = TS_ALPN_PROTOCOL_INDEX_HTTP_1_1;
  }

  PoolableSession *retval = ProxySession::create_outbound_session(proto_index);

  retval->sharing_pool  = static_cast<TSServerSessionSharingPoolType>(t_state.http_config_param->server_session_sharing_pool);
  retval->sharing_match = static_cast<TSServerSessionSharingMatchMask>(t_state.txn_conf->server_session_sharing_match);
  retval->attach_hostname(t_state.current.server->name);
  retval->new_connection(&netvc, netvc_read_buffer, netvc_reader);

  ATS_PROBE1(new_origin_server_connection, t_state.current.server->name);
  retval->set_active();

  ats_ip_copy(&t_state.server_info.src_addr, netvc.get_local_addr());

  // If origin_max_connections or origin_min_keep_alive_connections is set then we are metering
  // the max and or min number of connections per host. Transfer responsibility for this to the
  // session object.
  if (t_state.outbound_conn_track_state.is_active()) {
    SMDbg(dbg_ctl_http_connect, "max number of outbound connections: %d", t_state.txn_conf->connection_tracker_config.server_max);
    retval->enable_outbound_connection_tracking(t_state.outbound_conn_track_state.drop());
  }
  return retval;
}

bool
HttpSM::create_server_txn(PoolableSession *new_session)
{
  ink_assert(new_session != nullptr);
  bool retval = false;

  server_txn = new_session->new_transaction();
  if (server_txn) {
    retval = true;
    server_txn->attach_transaction(this);
    if (t_state.current.request_to == ResolveInfo::PARENT_PROXY) {
      new_session->to_parent_proxy = true;
      if (server_txn->get_proxy_ssn()->get_transact_count() == 1) {
        // These are connection-level metrics, so only increment them for the
        // first transaction lest they get overcounted.
        Metrics::Gauge::increment(http_rsb.current_parent_proxy_connections);
        Metrics::Counter::increment(http_rsb.total_parent_proxy_connections);
      }
    } else {
      new_session->to_parent_proxy = false;
    }
    server_txn->do_io_write(this, 0, nullptr);
    attach_server_session();
  }
  _netvc             = nullptr;
  _netvc_read_buffer = nullptr;
  _netvc_reader      = nullptr;
  return retval;
}

//////////////////////////////////////////////////////////////////////////////
//
//  HttpSM::state_http_server_open()
//
//////////////////////////////////////////////////////////////////////////////
int
HttpSM::state_http_server_open(int event, void *data)
{
  SMDbg(dbg_ctl_http_track, "entered inside state_http_server_open: %s", HttpDebugNames::get_event_name(event));
  STATE_ENTER(&HttpSM::state_http_server_open, event);
  ink_release_assert(event == EVENT_INTERVAL || event == NET_EVENT_OPEN || event == NET_EVENT_OPEN_FAILED ||
                     pending_action.empty());
  if (event != NET_EVENT_OPEN) {
    pending_action = nullptr;
  }
  ATS_PROBE1(milestone_server_connect_end, sm_id);
  milestones[TS_MILESTONE_SERVER_CONNECT_END] = ink_get_hrtime();

  switch (event) {
  case NET_EVENT_OPEN: {
    // Since the UnixNetVConnection::action_ or SocksEntry::action_ may be returned from netProcessor.connect_re, and the
    // SocksEntry::action_ will be copied into UnixNetVConnection::action_ before call back NET_EVENT_OPEN from
    // SocksEntry::free(), so we just compare the Continuation between pending_action and VC's action_.
    _netvc                 = static_cast<NetVConnection *>(data);
    _netvc_read_buffer     = new_MIOBuffer(HTTP_SERVER_RESP_HDR_BUFFER_INDEX);
    _netvc_reader          = _netvc_read_buffer->alloc_reader();
    UnixNetVConnection *vc = static_cast<UnixNetVConnection *>(_netvc);
    ink_release_assert(pending_action.empty() || pending_action.get_continuation() == vc->get_action()->continuation);
    pending_action = nullptr;

    if (this->plugin_tunnel_type == HttpPluginTunnel_t::NONE) {
      SMDbg(dbg_ctl_http_connect, "setting handler for connection handshake timeout %" PRId64, this->get_server_connect_timeout());
      // Just want to get a write-ready event so we know that the connection handshake is complete.
      // The buffer we create will be handed over to the eventually created server session
      _netvc->do_io_write(this, 1, _netvc_reader);
      _netvc->set_inactivity_timeout(this->get_server_connect_timeout());

    } else { // in the case of an intercept plugin don't to the connect timeout change
      SMDbg(dbg_ctl_http_connect, "not setting handler for connection handshake");
      this->create_server_txn(this->create_server_session(*_netvc, _netvc_read_buffer, _netvc_reader));
      handle_http_server_open();
    }
    ink_assert(pending_action.empty());
    return 0;
  }
  case CONNECT_EVENT_DIRECT:
    // Try it again, but direct this time
    do_http_server_open(false, true);
    break;
  case CONNECT_EVENT_TXN:
    SMDbg(dbg_ctl_http, "Connection handshake complete via CONNECT_EVENT_TXN");
    if (this->create_server_txn(static_cast<PoolableSession *>(data))) {
      handle_http_server_open();
    } else { // Failed to create transaction.  Maybe too many active transactions already
      // Try again (probably need a bounding counter here)
      do_http_server_open(false);
    }
    return 0;
  case VC_EVENT_READ_COMPLETE:
  case VC_EVENT_WRITE_READY:
  case VC_EVENT_WRITE_COMPLETE:
    // Update the time out to the regular connection timeout.
    SMDbg(dbg_ctl_http_ss, "Connection handshake complete");
    this->create_server_txn(this->create_server_session(*_netvc, _netvc_read_buffer, _netvc_reader));
    t_state.current.server->clear_connect_fail();
    handle_http_server_open();
    return 0;
  case VC_EVENT_INACTIVITY_TIMEOUT:
  case VC_EVENT_ACTIVE_TIMEOUT:
    t_state.set_connect_fail(ETIMEDOUT);
  /* fallthrough */
  case VC_EVENT_ERROR:
  case VC_EVENT_EOS:
  case NET_EVENT_OPEN_FAILED: {
    t_state.current.state = HttpTransact::CONNECTION_ERROR;
    t_state.outbound_conn_track_state.clear();
    if (_netvc != nullptr) {
      if (event == VC_EVENT_ERROR || event == NET_EVENT_OPEN_FAILED) {
        t_state.set_connect_fail(_netvc->lerrno);
      }
      this->server_connection_provided_cert = _netvc->provided_cert();
      _netvc->do_io_write(nullptr, 0, nullptr);
      _netvc->do_io_close();
      _netvc = nullptr;
    }
    if (t_state.cause_of_death_errno == -UNKNOWN_INTERNAL_ERROR) {
      // We set this to 0 because otherwise
      // HttpTransact::retry_server_connection_not_open will raise an assertion
      // if the value is the default UNKNOWN_INTERNAL_ERROR.
      t_state.cause_of_death_errno = 0;
    }

    /* If we get this error in transparent mode, then we simply can't bind to the 4-tuple to make the connection.  There's no hope
       of retries succeeding in the near future. The best option is to just shut down the connection without further comment. The
       only known cause for this is outbound transparency combined with use client target address / source port, as noted in
       TS-1424. If the keep alives desync the current connection can be attempting to rebind the 4 tuple simultaneously with the
       shut down of an existing connection. Dropping the client side will cause it to pick a new source port and recover from this
       issue.
    */
    if (EADDRNOTAVAIL == t_state.current.server->connect_result && t_state.client_info.is_transparent) {
      if (dbg_ctl_http_tproxy.on()) {
        ip_port_text_buffer ip_c, ip_s;
        SMDbg(dbg_ctl_http_tproxy, "Force close of client connect (%s->%s) due to EADDRNOTAVAIL",
              ats_ip_nptop(&t_state.client_info.src_addr.sa, ip_c, sizeof ip_c),
              ats_ip_nptop(&t_state.server_info.dst_addr.sa, ip_s, sizeof ip_s));
      }
      t_state.client_info.keep_alive = HTTPKeepAlive::NO_KEEPALIVE; // part of the problem, clear it.
      terminate_sm                   = true;
    } else if (ENET_THROTTLING == t_state.current.server->connect_result) {
      Metrics::Counter::increment(http_rsb.origin_connections_throttled);
      send_origin_throttled_response();
    } else {
      // Go ahead and release the failed server session.  Since it didn't receive a response, the release logic will
      // see that it didn't get a valid response and it will close it rather than returning it to the server session pool
      release_server_session();
      call_transact_and_set_next_state(HttpTransact::HandleResponse);
    }
    return 0;
  }
  case EVENT_INTERVAL: // Delayed call from another thread
    if (server_txn == nullptr) {
      do_http_server_open();
    }
    break;
  default:
    Error("[HttpSM::state_http_server_open] Unknown event: %d", event);
    ink_release_assert(0);
    return 0;
  }

  return 0;
}

int
HttpSM::state_read_server_response_header(int event, void *data)
{
  STATE_ENTER(&HttpSM::state_read_server_response_header, event);
  // If we had already received EOS, just go away. We would sometimes see
  // a WRITE event appear after receiving EOS from the server connection
  if (server_entry->eos) {
    return 0;
  }

  ink_assert(server_entry->eos != true);
  ink_assert(server_entry->read_vio == (VIO *)data);
  ink_assert(t_state.current.server->state == HttpTransact::STATE_UNDEFINED);
  ink_assert(t_state.current.state == HttpTransact::STATE_UNDEFINED);

  int bytes_used = 0;

  switch (event) {
  case VC_EVENT_EOS:
    server_entry->eos = true;

  // Fall through
  case VC_EVENT_READ_READY:
  case VC_EVENT_READ_COMPLETE:
    // More data to parse
    // Got some data, won't retry origin connection on error
    t_state.current.retry_attempts.maximize(t_state.configured_connect_attempts_max_retries());
    break;

  case VC_EVENT_ERROR:
  case VC_EVENT_INACTIVITY_TIMEOUT:
  case VC_EVENT_ACTIVE_TIMEOUT:
    // Error handling function
    handle_server_setup_error(event, data);
    return 0;
  }

  // Reset the inactivity timeout if this is the first
  //   time we've been called.  The timeout had been set to
  //   the connect timeout when we set up to read the header
  //
  if (server_response_hdr_bytes == 0) {
    ATS_PROBE1(milestone_server_first_read, sm_id);
    milestones[TS_MILESTONE_SERVER_FIRST_READ] = ink_get_hrtime();

    server_txn->set_inactivity_timeout(get_server_inactivity_timeout());

    // For requests that contain a body, we can cancel the ua inactivity timeout.
    if (_ua.get_txn() &&
        _ua.get_txn()->has_request_body(t_state.hdr_info.request_content_length,
                                        t_state.client_info.transfer_encoding == HttpTransact::TransferEncoding_t::CHUNKED)) {
      _ua.get_txn()->cancel_inactivity_timeout();
    }
  }
  /////////////////////
  // tokenize header //
  /////////////////////
  ParseResult state =
    t_state.hdr_info.server_response.parse_resp(&http_parser, server_txn->get_remote_reader(), &bytes_used, server_entry->eos);

  server_response_hdr_bytes += bytes_used;

  // Don't allow HTTP 0.9 (unparsable headers) on resued connections.
  // And don't allow empty headers from closed connections
  if ((state == ParseResult::DONE && t_state.hdr_info.server_response.version_get() == HTTP_0_9 &&
       server_txn->get_transaction_id() > 1) ||
      (server_entry->eos && state == ParseResult::CONT)) { // No more data will be coming
    state = ParseResult::ERROR;
  }
  // Check to see if we are over the hdr size limit
  if (server_response_hdr_bytes > t_state.txn_conf->response_hdr_max_size) {
    state = ParseResult::ERROR;
  }

  if (state != ParseResult::CONT) {
    // Disable further IO
    server_entry->read_vio->nbytes = server_entry->read_vio->ndone;
    http_parser_clear(&http_parser);
    ATS_PROBE1(milestone_server_read_header_done, sm_id);
    milestones[TS_MILESTONE_SERVER_READ_HEADER_DONE] = ink_get_hrtime();

    // Any other events to the end
    if (server_entry->vc_type == HttpVC_t::SERVER_VC) {
      server_entry->vc_read_handler  = &HttpSM::tunnel_handler;
      server_entry->vc_write_handler = &HttpSM::tunnel_handler;
    }

    // If there is a post body in transit, give up on it
    if (tunnel.is_tunnel_alive()) {
      tunnel.abort_tunnel();
      // Make sure client connection is closed when we are done in case there is cruft left over
      t_state.client_info.keep_alive = HTTPKeepAlive::NO_KEEPALIVE;
      // Similarly the server connection should also be closed
      t_state.current.server->keep_alive = HTTPKeepAlive::NO_KEEPALIVE;
    }
  }

  switch (state) {
  case ParseResult::ERROR: {
    // Many broken servers send really badly formed 302 redirects.
    //  Even if the parser doesn't like the redirect forward
    //  if it's got a Location header.  We check the type of the
    //  response to make sure that the parser was able to parse
    //  something  and didn't just throw up it's hands (INKqa05339)
    bool allow_error = false;
    if (t_state.hdr_info.server_response.type_get() == HTTPType::RESPONSE &&
        t_state.hdr_info.server_response.status_get() == HTTPStatus::MOVED_TEMPORARILY) {
      if (t_state.hdr_info.server_response.field_find(static_cast<std::string_view>(MIME_FIELD_LOCATION))) {
        allow_error = true;
      }
    }

    if (allow_error == false) {
      SMDbg(dbg_ctl_http_seq, "Error parsing server response header");
      t_state.current.state = HttpTransact::PARSE_ERROR;
      // We set this to 0 because otherwise HttpTransact::retry_server_connection_not_open
      // will raise an assertion if the value is the default UNKNOWN_INTERNAL_ERROR.
      t_state.cause_of_death_errno = 0;

      // If the server closed prematurely on us, use the
      //   server setup error routine since it will forward
      //   error to a POST tunnel if any
      if (event == VC_EVENT_EOS) {
        handle_server_setup_error(VC_EVENT_EOS, data);
      } else {
        call_transact_and_set_next_state(HttpTransact::HandleResponse);
      }
      break;
    }
    // FALLTHROUGH (since we are allowing the parse error)
  }
    // fallthrough

  case ParseResult::DONE:

    if (!t_state.hdr_info.server_response.check_hdr_implements()) {
      t_state.http_return_code = HTTPStatus::BAD_GATEWAY;
      call_transact_and_set_next_state(HttpTransact::BadRequest);
      break;
    }

    SMDbg(dbg_ctl_http_seq, "Done parsing server response header");

    // Now that we know that we have all of the origin server
    // response headers, we can reset the client inactivity
    // timeout.
    // we now reset the client inactivity timeout only
    // when we are ready to send the response headers. In the
    // case of transform plugin, this is after the transform
    // outputs the 1st byte, which can take a long time if the
    // plugin buffers the whole response.
    _ua.get_txn()->set_inactivity_timeout(HRTIME_SECONDS(t_state.txn_conf->transaction_no_activity_timeout_in));

    t_state.current.state         = HttpTransact::CONNECTION_ALIVE;
    t_state.transact_return_point = HttpTransact::HandleResponse;
    t_state.api_next_action       = HttpTransact::StateMachineAction_t::API_READ_RESPONSE_HDR;

    // if exceeded limit deallocate postdata buffers and disable redirection
    if (!(enable_redirection && (redirection_tries < t_state.txn_conf->number_of_redirections))) {
      this->disable_redirect();
    }

    // Go ahead and process the hooks assuming any body tunnel has already completed
    if (!tunnel.is_tunnel_alive()) {
      SMDbg(dbg_ctl_http_seq, "Continue processing response");
      do_api_callout();
    } else {
      SMDbg(dbg_ctl_http_seq, "Defer processing response until post body is processed");
      server_entry->read_vio->disable(); // Disable the read until we finish the tunnel
    }
    break;
  case ParseResult::CONT:
    ink_assert(server_entry->eos == false);
    server_entry->read_vio->reenable();
    return VC_EVENT_CONT;

  default:
    ink_assert(!"not reached");
  }

  return 0;
}

int
HttpSM::state_send_server_request_header(int event, void *data)
{
  ink_assert(server_entry != nullptr);
  ink_assert(server_entry->eos == false);
  ink_assert(server_entry->write_vio == (VIO *)data);
  STATE_ENTER(&HttpSM::state_send_server_request_header, event);

  int method;

  switch (event) {
  case VC_EVENT_WRITE_READY:
    server_entry->write_vio->reenable();
    break;

  case VC_EVENT_WRITE_COMPLETE:
    // We are done sending the request header, deallocate
    //  our buffer and then decide what to do next
    if (server_entry->write_buffer) {
      free_MIOBuffer(server_entry->write_buffer);
      server_entry->write_buffer = nullptr;
      method                     = t_state.hdr_info.server_request.method_get_wksidx();
      if (!t_state.api_server_request_body_set && method != HTTP_WKSIDX_TRACE &&
          _ua.get_txn()->has_request_body(t_state.hdr_info.request_content_length,
                                          t_state.client_info.transfer_encoding == HttpTransact::TransferEncoding_t::CHUNKED)) {
        if (post_transform_info.vc) {
          setup_transform_to_server_transfer();
        } else {
          // Go ahead and set up the post tunnel if we are not waiting for a 100 response
          if (!t_state.hdr_info.client_request.m_100_continue_required) {
            do_setup_client_request_body_tunnel(HttpVC_t::SERVER_VC);
          }
        }
      }
      // Any other events to these read response
      if (server_entry->vc_type == HttpVC_t::SERVER_VC) {
        server_entry->vc_read_handler = &HttpSM::state_read_server_response_header;
      }
    }

    break;

  case VC_EVENT_EOS:
    // EOS of stream comes from the read side.  Treat it as
    //  as error if there is nothing in the read buffer.  If
    //  there is something the server may have blasted back
    //  the response before receiving the request.  Happens
    //  often with redirects
    //
    //  If we are in the middle of an api callout, it
    //    means we haven't actually sent the request yet
    //    so the stuff in the buffer is garbage and we
    //    want to ignore it
    //
    server_entry->eos = true;

    // I'm not sure about the above comment, but if EOS is received on read and we are
    // still in this state, we must have not gotten WRITE_COMPLETE.  With epoll we might not receive EOS
    // from both read and write sides of a connection so it should be handled correctly (close tunnels,
    // deallocate, etc) here with handle_server_setup_error().  Otherwise we might hang due to not shutting
    // down and never receiving another event again.
    /*if (server_txn->get_remote_reader()->read_avail() > 0 && callout_state == HttpApiState_t::NO_CALLOUT) {
       break;
       } */

    // Nothing in the buffer
    // proceed to error
    // fallthrough

  case VC_EVENT_ERROR:
  case VC_EVENT_ACTIVE_TIMEOUT:
  case VC_EVENT_INACTIVITY_TIMEOUT:
    handle_server_setup_error(event, data);
    break;

  case VC_EVENT_READ_COMPLETE:
    // new event expected due to TS-3189
    SMDbg(dbg_ctl_http_ss, "read complete due to 0 byte do_io_read");
    break;

  default:
    ink_release_assert(0);
    break;
  }

  return 0;
}

bool
HttpSM::origin_multiplexed() const
{
  return (t_state.dns_info.http_version == HTTP_2_0 || t_state.dns_info.http_version == HTTP_INVALID);
}

void
HttpSM::cancel_pending_server_connection()
{
  EThread *ethread = this_ethread();
  if (nullptr == ethread->connecting_pool || !t_state.current.server) {
    return; // No pending requests
  }
  IpEndpoint ip;
  ip.assign(&this->t_state.current.server->dst_addr.sa);
  auto [iter_start, iter_end] = ethread->connecting_pool->m_ip_pool.equal_range(ip);
  for (auto ip_iter = iter_start; ip_iter != iter_end; ++ip_iter) {
    ConnectingEntry *connecting_entry = ip_iter->second;
    // Found a match, look for our sm in the queue.
    auto entry = connecting_entry->connect_sms.find(this);
    if (entry != connecting_entry->connect_sms.end()) {
      // Found the sm, remove it.
      connecting_entry->connect_sms.erase(entry);
      if (connecting_entry->connect_sms.empty()) {
        if (connecting_entry->netvc) {
          connecting_entry->netvc->do_io_write(nullptr, 0, nullptr);
          connecting_entry->netvc->do_io_close();
        }
        ethread->connecting_pool->m_ip_pool.erase(ip_iter);
        delete connecting_entry;
        break;
      } else {
        //  Leave the shared entry remaining alone
      }
    }
  }
}

// Returns true if there was a matching entry that we
// queued this request on
bool
HttpSM::add_to_existing_request()
{
  HttpTransact::State &s       = this->t_state;
  bool                 retval  = false;
  EThread             *ethread = this_ethread();

  if (this->plugin_tunnel_type != HttpPluginTunnel_t::NONE) {
    return false;
  }

  if (nullptr == ethread->connecting_pool) {
    initialize_thread_for_connecting_pools(ethread);
  }
  NetVConnection *vc = _ua.get_txn()->get_netvc();
  ink_assert(dynamic_cast<UnixNetVConnection *>(vc) == nullptr /* PluginVC */ ||
             dynamic_cast<UnixNetVConnection *>(vc)->nh == get_NetHandler(this_ethread()));

  HTTP_SM_SET_DEFAULT_HANDLER(&HttpSM::state_http_server_open);

  IpEndpoint ip;
  ip.assign(&s.current.server->dst_addr.sa);
  std::string_view proposed_sni      = this->get_outbound_sni();
  std::string_view proposed_cert     = this->get_outbound_cert();
  std::string_view proposed_hostname = this->t_state.current.server->name;
  auto [iter_start, iter_end]        = ethread->connecting_pool->m_ip_pool.equal_range(ip);

  for (auto ip_iter = iter_start; ip_iter != iter_end; ++ip_iter) {
    // Check that entry matches sni, hostname, and cert.
    if (proposed_hostname == ip_iter->second->hostname && proposed_sni == ip_iter->second->sni &&
        proposed_cert == ip_iter->second->cert_name && ip_iter->second->connect_sms.size() < 50) {
      // Pre-emptively set a server connect failure that will be cleared once a
      // WRITE_READY is received from origin or bytes are received back.
      this->t_state.set_connect_fail(EIO);
      ip_iter->second->connect_sms.insert(this);
      Dbg(dbg_ctl_http_connect, "Add entry to connection queue. size=%zd", ip_iter->second->connect_sms.size());
      retval = true;
      break;
    }
  }
  return retval;
}

void
HttpSM::process_srv_info(HostDBRecord *record)
{
  SMDbg(dbg_ctl_dns_srv, "beginning process_srv_info");
  t_state.dns_info.record = record;

  /* we didn't get any SRV records, continue w normal lookup */
  if (!record || !record->is_srv()) {
    t_state.dns_info.srv_hostname[0]  = '\0';
    t_state.dns_info.resolved_p       = false;
    t_state.my_txn_conf().srv_enabled = false;
    SMDbg(dbg_ctl_dns_srv, "No SRV records were available, continuing to lookup %s", t_state.dns_info.lookup_name);
  } else {
    HostDBInfo *srv = record->select_best_srv(t_state.dns_info.srv_hostname, &mutex->thread_holding->generator, ts_clock::now(),
                                              t_state.txn_conf->down_server_timeout);
    if (!srv) {
      //      t_state.dns_info.srv_lookup_success = false;
      t_state.dns_info.srv_hostname[0]  = '\0';
      t_state.my_txn_conf().srv_enabled = false;
      SMDbg(dbg_ctl_dns_srv, "SRV records empty for %s", t_state.dns_info.lookup_name);
    } else {
      t_state.dns_info.resolved_p = false;
      t_state.dns_info.srv_port   = srv->data.srv.srv_port;
      ink_assert(srv->data.srv.key == makeHostHash(t_state.dns_info.srv_hostname));
      SMDbg(dbg_ctl_dns_srv, "select SRV records %s", t_state.dns_info.srv_hostname);
    }
  }
  return;
}

void
HttpSM::process_hostdb_info(HostDBRecord *record)
{
  t_state.dns_info.record = record; // protect record.

  bool use_client_addr = t_state.http_config_param->use_client_target_addr == 1 && t_state.client_info.is_transparent &&
                         t_state.dns_info.os_addr_style == ResolveInfo::OS_Addr::TRY_DEFAULT;

  t_state.dns_info.set_active(nullptr);

  if (use_client_addr) {
    NetVConnection *vc = _ua.get_txn() ? _ua.get_txn()->get_netvc() : nullptr;
    if (vc) {
      t_state.dns_info.set_upstream_address(vc->get_local_addr());
      t_state.dns_info.os_addr_style = ResolveInfo::OS_Addr::TRY_CLIENT;
    }
  }

  if (record && !record->is_failed()) {
    t_state.dns_info.inbound_remote_addr = &t_state.client_info.src_addr.sa;
    if (!use_client_addr) {
      t_state.dns_info.set_active(
        record->select_best_http(ts_clock::now(), t_state.txn_conf->down_server_timeout, t_state.dns_info.inbound_remote_addr));
    } else {
      // if use_client_target_addr is set, make sure the client addr is in the results pool
      t_state.dns_info.cta_validated_p = true;
      t_state.dns_info.record          = record; // Cache this but do not make it active.
      if (record->find(t_state.dns_info.addr) == nullptr) {
        SMDbg(dbg_ctl_http, "use_client_target_addr == 1. Client specified address is not in the pool, not validated.");
        t_state.dns_info.cta_validated_p = false;
      }
    }
  } else {
    SMDbg(dbg_ctl_http, "DNS lookup failed for '%s'", t_state.dns_info.lookup_name);
  }

  if (!t_state.dns_info.resolved_p) {
    SMDbg(dbg_ctl_http, "[%" PRId64 "] resolution failed for '%s'", sm_id, t_state.dns_info.lookup_name);
  }

  ATS_PROBE1(milestone_dns_lookup_end, sm_id);
  milestones[TS_MILESTONE_DNS_LOOKUP_END] = ink_get_hrtime();

  if (dbg_ctl_http_timeout.on()) {
    if (t_state.api_txn_dns_timeout_value != -1) {
      int foo = static_cast<int>(milestones.difference_msec(TS_MILESTONE_DNS_LOOKUP_BEGIN, TS_MILESTONE_DNS_LOOKUP_END));
      SMDbg(dbg_ctl_http_timeout, "DNS took: %d msec", foo);
    }
  }
}

int
HttpSM::state_pre_resolve(int event, void * /* data ATS_UNUSED */)
{
  STATE_ENTER(&HttpSM::state_hostdb_lookup, event);
  return 0;
}

//////////////////////////////////////////////////////////////////////////////
//
//  HttpSM::state_hostdb_lookup()
//
//////////////////////////////////////////////////////////////////////////////
int
HttpSM::state_hostdb_lookup(int event, void *data)
{
  STATE_ENTER(&HttpSM::state_hostdb_lookup, event);

  switch (event) {
  case EVENT_HOST_DB_LOOKUP:
    pending_action = nullptr;
    process_hostdb_info(static_cast<HostDBRecord *>(data));
    call_transact_and_set_next_state(nullptr);
    break;
  case EVENT_SRV_LOOKUP: {
    pending_action = nullptr;
    process_srv_info(static_cast<HostDBRecord *>(data));

    char const              *host_name = t_state.dns_info.is_srv() ? t_state.dns_info.srv_hostname : t_state.dns_info.lookup_name;
    HostDBProcessor::Options opt;
    opt.port    = t_state.dns_info.is_srv() ? t_state.dns_info.srv_port : t_state.server_info.dst_addr.host_order_port();
    opt.flags   = (t_state.cache_info.directives.does_client_permit_dns_storing) ? HostDBProcessor::HOSTDB_DO_NOT_FORCE_DNS :
                                                                                   HostDBProcessor::HOSTDB_FORCE_DNS_RELOAD;
    opt.timeout = (t_state.api_txn_dns_timeout_value != -1) ? t_state.api_txn_dns_timeout_value : 0;
    opt.host_res_style =
      ats_host_res_from(_ua.get_txn()->get_netvc()->get_local_addr()->sa_family, t_state.txn_conf->host_res_data.order);

    pending_action = hostDBProcessor.getbyname_imm(this, (cb_process_result_pfn)&HttpSM::process_hostdb_info, host_name, 0, opt);
    if (pending_action.empty()) {
      call_transact_and_set_next_state(nullptr);
    }
  } break;
  case EVENT_HOST_DB_IP_REMOVED:
    ink_assert(!"Unexpected event from HostDB");
    break;
  default:
    ink_assert(!"Unexpected event");
  }
  return 0;
}

int
HttpSM::state_hostdb_reverse_lookup(int event, void *data)
{
  STATE_ENTER(&HttpSM::state_hostdb_reverse_lookup, event);

  // HttpRequestFlavor_t::SCHEDULED_UPDATE can be transformed into
  // HttpRequestFlavor_t::REVPROXY
  ink_assert(t_state.req_flavor == HttpTransact::HttpRequestFlavor_t::SCHEDULED_UPDATE ||
             t_state.req_flavor == HttpTransact::HttpRequestFlavor_t::REVPROXY || _ua.get_entry()->vc != nullptr);

  switch (event) {
  case EVENT_HOST_DB_LOOKUP:
    pending_action = nullptr;
    if (data) {
      t_state.request_data.hostname_str = (static_cast<HostDBRecord *>(data))->name();
    } else {
      SMDbg(dbg_ctl_http, "reverse DNS lookup failed for '%s'", t_state.dns_info.lookup_name);
    }
    call_transact_and_set_next_state(nullptr);
    break;
  default:
    ink_assert(!"Unexpected event");
  }

  return 0;
}

//////////////////////////////////////////////////////////////////////////////
//
//  HttpSM:state_mark_os_down()
//
//////////////////////////////////////////////////////////////////////////////
int
HttpSM::state_mark_os_down(int event, void *data)
{
  STATE_ENTER(&HttpSM::state_mark_os_down, event);

  if (event == EVENT_HOST_DB_LOOKUP && data) {
    auto r = static_cast<HostDBRecord *>(data);

    // Look for the entry we need mark down in the round robin
    ink_assert(t_state.current.server != nullptr);
    ink_assert(t_state.dns_info.looking_up == ResolveInfo::ORIGIN_SERVER);
    if (auto *info = r->find(&t_state.dns_info.addr.sa); info != nullptr) {
      info->mark_down(ts_clock::now());
    }
  }
  // We either found our entry or we did not.  Either way find
  //  the entry we should use now
  return state_hostdb_lookup(event, data);
}

/////////////////////////////////////////////////////////////////////////////////
//  HttpSM::state_cache_open_write()
//
//  This state is set by set_next_state() for a cache open write
//  (SERVER_READ_CACHE_WRITE)
//
//////////////////////////////////////////////////////////////////////////
int
HttpSM::state_cache_open_write(int event, void *data)
{
  STATE_ENTER(&HttpSM : state_cache_open_write, event);

  // Make sure we are on the "right" thread
  if (_ua.get_txn()) {
    pending_action = _ua.get_txn()->adjust_thread(this, event, data);
    if (!pending_action.empty()) {
      Metrics::Counter::increment(http_rsb.cache_open_write_adjust_thread);
      return 0; // Go away if we reschedule
    }
    NetVConnection *vc = _ua.get_txn()->get_netvc();
    ink_release_assert(vc && vc->thread == this_ethread());
  }

  pending_action.clear_if_action_is(reinterpret_cast<Action *>(data));

  ATS_PROBE1(milestone_cache_open_write_end, sm_id);
  milestones[TS_MILESTONE_CACHE_OPEN_WRITE_END] = ink_get_hrtime();
  pending_action                                = nullptr;

  switch (event) {
  case CACHE_EVENT_OPEN_WRITE:
    //////////////////////////////
    // OPEN WRITE is successful //
    //////////////////////////////
    t_state.cache_info.write_lock_state = HttpTransact::CacheWriteLock_t::SUCCESS;
    break;

  case CACHE_EVENT_OPEN_WRITE_FAILED:
    // Failed on the write lock and retrying the vector
    //  for reading
    if (t_state.redirect_info.redirect_in_process) {
      SMDbg(dbg_ctl_http_redirect, "CACHE_EVENT_OPEN_WRITE_FAILED during redirect follow");
      t_state.cache_open_write_fail_action = static_cast<MgmtByte>(CacheOpenWriteFailAction_t::DEFAULT);
      t_state.cache_info.write_lock_state  = HttpTransact::CacheWriteLock_t::FAIL;
      break;
    }
    if (t_state.txn_conf->cache_open_write_fail_action == static_cast<MgmtByte>(CacheOpenWriteFailAction_t::DEFAULT)) {
      t_state.cache_info.write_lock_state = HttpTransact::CacheWriteLock_t::FAIL;
      break;
    } else {
      t_state.cache_open_write_fail_action = t_state.txn_conf->cache_open_write_fail_action;
      if (!t_state.cache_info.object_read || (t_state.cache_open_write_fail_action ==
                                              static_cast<MgmtByte>(CacheOpenWriteFailAction_t::ERROR_ON_MISS_OR_REVALIDATE))) {
        // cache miss, set wl_state to fail
        SMDbg(dbg_ctl_http, "cache object read %p, cache_wl_fail_action %d", t_state.cache_info.object_read,
              t_state.cache_open_write_fail_action);
        t_state.cache_info.write_lock_state = HttpTransact::CacheWriteLock_t::FAIL;
        break;
      }
    }
  // INTENTIONAL FALL THROUGH
  // Allow for stale object to be served
  case CACHE_EVENT_OPEN_READ:
    if (!t_state.cache_info.object_read) {
      t_state.cache_open_write_fail_action = t_state.txn_conf->cache_open_write_fail_action;
      // Note that CACHE_LOOKUP_COMPLETE may be invoked more than once
      // if CacheOpenWriteFailAction_t::READ_RETRY is configured
      ink_assert(t_state.cache_open_write_fail_action == static_cast<MgmtByte>(CacheOpenWriteFailAction_t::READ_RETRY));
      t_state.cache_lookup_result         = HttpTransact::CacheLookupResult_t::NONE;
      t_state.cache_info.write_lock_state = HttpTransact::CacheWriteLock_t::READ_RETRY;
      break;
    }
    // The write vector was locked and the cache_sm retried
    // and got the read vector again.
    cache_sm.cache_read_vc->get_http_info(&t_state.cache_info.object_read);
    // ToDo: Should support other levels of cache hits here, but the cache does not support it (yet)
    if (cache_sm.cache_read_vc->is_ram_cache_hit()) {
      t_state.cache_info.hit_miss_code = SQUID_HIT_RAM;
    } else {
      t_state.cache_info.hit_miss_code = SQUID_HIT_DISK;
    }

    ink_assert(t_state.cache_info.object_read != nullptr);
    t_state.source = HttpTransact::Source_t::CACHE;
    // clear up CacheLookupResult_t::MISS, let Freshness function decide
    // hit status
    t_state.cache_lookup_result         = HttpTransact::CacheLookupResult_t::NONE;
    t_state.cache_info.write_lock_state = HttpTransact::CacheWriteLock_t::READ_RETRY;
    break;

  case HTTP_TUNNEL_EVENT_DONE:
    // In the case where we have issued a cache write for the
    //  transformed copy, the tunnel from the origin server to
    //  the transform may complete while we are waiting for
    //  the cache write.  If this is the case, forward the event
    //  to the transform read state as it will know how to
    //  handle it
    if (t_state.next_action == HttpTransact::StateMachineAction_t::CACHE_ISSUE_WRITE_TRANSFORM) {
      state_common_wait_for_transform_read(&transform_info, &HttpSM::tunnel_handler, event, data);

      return 0;
    }
  // Fallthrough
  default:
    ink_release_assert(0);
  }

  // The write either succeeded or failed, notify transact
  call_transact_and_set_next_state(nullptr);

  return 0;
}

inline void
HttpSM::setup_cache_lookup_complete_api()
{
  t_state.api_next_action = HttpTransact::StateMachineAction_t::API_CACHE_LOOKUP_COMPLETE;
  do_api_callout();
}

//////////////////////////////////////////////////////////////////////////
//
//  HttpSM::state_cache_open_read()
//
//  This state handles the result of CacheProcessor::open_read()
//  that attempts to do cache lookup and open a particular cached
//  object for reading.
//
//////////////////////////////////////////////////////////////////////////
int
HttpSM::state_cache_open_read(int event, void *data)
{
  STATE_ENTER(&HttpSM::state_cache_open_read, event);

  pending_action.clear_if_action_is(reinterpret_cast<Action *>(data));

  ink_assert(server_entry == nullptr);
  ink_assert(t_state.cache_info.object_read == nullptr);

  switch (event) {
  case CACHE_EVENT_OPEN_READ: {
    pending_action = nullptr;

    SMDbg(dbg_ctl_http, "cache_open_read - CACHE_EVENT_OPEN_READ");

    /////////////////////////////////
    // lookup/open is successful. //
    /////////////////////////////////
    ink_assert(cache_sm.cache_read_vc != nullptr);
    t_state.source = HttpTransact::Source_t::CACHE;

    cache_sm.cache_read_vc->get_http_info(&t_state.cache_info.object_read);
    // ToDo: Should support other levels of cache hits here, but the cache does not support it (yet)
    if (cache_sm.cache_read_vc->is_ram_cache_hit()) {
      t_state.cache_info.hit_miss_code = SQUID_HIT_RAM;
    } else {
      t_state.cache_info.hit_miss_code = SQUID_HIT_DISK;
    }

    ink_assert(t_state.cache_info.object_read != nullptr);
    call_transact_and_set_next_state(HttpTransact::HandleCacheOpenRead);
    break;
  }
  case CACHE_EVENT_OPEN_READ_FAILED:
    pending_action = nullptr;

    SMDbg(dbg_ctl_http, "cache_open_read - CACHE_EVENT_OPEN_READ_FAILED with %s (%d)", InkStrerror(-cache_sm.get_last_error()),
          -cache_sm.get_last_error());

    SMDbg(dbg_ctl_http, "open read failed.");
    // Inform HttpTransact somebody else is updating the document
    // HttpCacheSM already waited so transact should go ahead.
    if (cache_sm.get_last_error() == -ECACHE_DOC_BUSY) {
      t_state.cache_lookup_result = HttpTransact::CacheLookupResult_t::DOC_BUSY;
    } else {
      t_state.cache_lookup_result = HttpTransact::CacheLookupResult_t::MISS;
    }

    ink_assert(t_state.transact_return_point == nullptr);
    t_state.transact_return_point = HttpTransact::HandleCacheOpenRead;
    setup_cache_lookup_complete_api();
    break;

  default:
    ink_release_assert(!"Unknown event");
    break;
  }

  ATS_PROBE1(milestone_cache_open_read_end, sm_id);
  milestones[TS_MILESTONE_CACHE_OPEN_READ_END] = ink_get_hrtime();

  return 0;
}

int
HttpSM::main_handler(int event, void *data)
{
  ink_release_assert(magic == HttpSmMagic_t::ALIVE);

  HttpSMHandler jump_point = nullptr;
  ink_assert(reentrancy_count >= 0);
  reentrancy_count++;

  // Don't use the state enter macro since it uses history
  //  space that we don't care about
  SMDbg(dbg_ctl_http, "%s, %d", HttpDebugNames::get_event_name(event), event);

  HttpVCTableEntry *vc_entry = nullptr;

  if (data != nullptr) {
    // Only search the VC table if the event could have to
    //  do with a VIO to save a few cycles

    if (event < VC_EVENT_EVENTS_START + 100) {
      vc_entry = vc_table.find_entry(static_cast<VIO *>(data));
    }
  }

  if (vc_entry) {
    jump_point = (static_cast<VIO *>(data) == vc_entry->read_vio) ? vc_entry->vc_read_handler : vc_entry->vc_write_handler;
    ink_assert(jump_point != (HttpSMHandler) nullptr);
    ink_assert(vc_entry->vc != (VConnection *)nullptr);
    (this->*jump_point)(event, data);
  } else {
    ink_assert(default_handler != (HttpSMHandler) nullptr);
    (this->*default_handler)(event, data);
  }

  // The sub-handler signals when it is time for the state
  //  machine to exit.  We can only exit if we are not reentrantly
  //  called otherwise when the our call unwinds, we will be
  //  running on a dead state machine
  //
  // Because of the need for an api shutdown hook, kill_this()
  // is also reentrant.  As such, we don't want to decrement
  // the reentrancy count until after we run kill_this()
  //
  if (terminate_sm == true && reentrancy_count == 1) {
    kill_this();
  } else {
    reentrancy_count--;
    ink_assert(reentrancy_count >= 0);
  }

  return (VC_EVENT_CONT);
}

// void HttpSM::tunnel_handler_post_or_put()
//
//   Handles the common cleanup tasks for Http post/put
//   to prevent code duplication
//
void
HttpSM::tunnel_handler_post_or_put(HttpTunnelProducer *p)
{
  ink_assert(p->vc_type == HttpTunnelType_t::HTTP_CLIENT ||
             (static_cast<HttpSmPost_t>(p->handler_state) == HttpSmPost_t::UA_FAIL && p->vc_type == HttpTunnelType_t::BUFFER_READ));
  HttpTunnelConsumer *c;

  // If there is a post transform, remove it's entry from the State
  //  Machine's VC table
  //
  // MUST NOT clear the vc pointer from post_transform_info
  //    as this causes a double close of the transform vc in transform_cleanup
  //
  if (post_transform_info.vc != nullptr) {
    ink_assert(post_transform_info.entry->in_tunnel == true);
    ink_assert(post_transform_info.vc == post_transform_info.entry->vc);
    vc_table.cleanup_entry(post_transform_info.entry);
    post_transform_info.entry = nullptr;
  }

  switch (static_cast<HttpSmPost_t>(p->handler_state)) {
  case HttpSmPost_t::SERVER_FAIL:
    c = tunnel.get_consumer(server_entry->vc);
    ink_assert(c->write_success == false);
    break;
  case HttpSmPost_t::UA_FAIL:
    // UA quit - shutdown the SM
    ink_assert(p->read_success == false);
    terminate_sm = true;
    break;
  case HttpSmPost_t::SUCCESS:
    // The post succeeded
    ink_assert(p->read_success == true);
    ink_assert(p->consumer_list.head->write_success == true);
    tunnel.deallocate_buffers();
    tunnel.reset();
    // When the ua completed sending it's data we must have
    //  removed it from the tunnel
    _ua.get_entry()->in_tunnel = false;
    server_entry->in_tunnel    = false;

    break;
  default:
    ink_release_assert(0);
  }
}

// int HttpSM::tunnel_handler_post(int event, void* data)
//
//   Handles completion of any http request body tunnel
//     Having 'post' in its name is a misnomer
//
int
HttpSM::tunnel_handler_post(int event, void *data)
{
  STATE_ENTER(&HttpSM::tunnel_handler_post, event);

  HttpTunnelProducer *p =
    _ua.get_txn() != nullptr ? tunnel.get_producer(_ua.get_txn()) : tunnel.get_producer(HttpTunnelType_t::HTTP_CLIENT);
  if (!p) {
    return 0; // Cannot do anything if there is no producer
  }

  switch (event) {
  case HTTP_TUNNEL_EVENT_DONE: // Tunnel done.
    if (static_cast<HttpSmPost_t>(p->handler_state) == HttpSmPost_t::UA_FAIL) {
      // post failed
      switch (t_state.client_info.state) {
      case HttpTransact::ACTIVE_TIMEOUT:
        call_transact_and_set_next_state(HttpTransact::PostActiveTimeoutResponse);
        return 0;
      case HttpTransact::INACTIVE_TIMEOUT:
        call_transact_and_set_next_state(HttpTransact::PostInactiveTimeoutResponse);
        return 0;
      case HttpTransact::PARSE_ERROR:
        call_transact_and_set_next_state(HttpTransact::BadRequest);
        return 0;
      default:
        break;
      }
    }
    break;
  case VC_EVENT_WRITE_READY: // iocore may callback first before send.
    return 0;
  case VC_EVENT_EOS:                // SSLNetVC may callback EOS during write error (6.0.x or early)
  case VC_EVENT_ERROR:              // Send HTTP 408 error
  case VC_EVENT_WRITE_COMPLETE:     // tunnel_handler_post_ua has sent HTTP 408 response
  case VC_EVENT_INACTIVITY_TIMEOUT: // _ua.get_txn() timeout during sending the HTTP 408 response
  case VC_EVENT_ACTIVE_TIMEOUT:     // _ua.get_txn() timeout
    if (_ua.get_entry()->write_buffer) {
      free_MIOBuffer(_ua.get_entry()->write_buffer);
      _ua.get_entry()->write_buffer = nullptr;
    }
    if (p->handler_state == static_cast<int>(HttpSmPost_t::UNKNOWN)) {
      p->handler_state = static_cast<int>(HttpSmPost_t::UA_FAIL);
    }
    break;
  case VC_EVENT_READ_READY:
  case VC_EVENT_READ_COMPLETE:
  default:
    ink_assert(!"not reached");
    return 0;
  }

  ink_assert(event == HTTP_TUNNEL_EVENT_DONE);
  ink_assert(data == &tunnel);
  // The tunnel calls this when it is done

  int p_handler_state = p->handler_state;
  if (is_waiting_for_full_body && !this->is_postbuf_valid()) {
    p_handler_state = static_cast<int>(HttpSmPost_t::SERVER_FAIL);
  }
  if (p->vc_type != HttpTunnelType_t::BUFFER_READ) {
    tunnel_handler_post_or_put(p);
  }

  switch (static_cast<HttpSmPost_t>(p_handler_state)) {
  case HttpSmPost_t::SERVER_FAIL:
    handle_post_failure();
    break;
  case HttpSmPost_t::UA_FAIL:
    // Client side failed.  Shutdown and go home.  No need to communicate back to UA
    terminate_sm = true;
    break;
  case HttpSmPost_t::SUCCESS:
    // It's time to start reading the response
    if (is_waiting_for_full_body) {
      is_waiting_for_full_body  = false;
      is_buffering_request_body = true;
      client_request_body_bytes = this->postbuf_buffer_avail();

      call_transact_and_set_next_state(HttpTransact::HandleRequestBufferDone);
      break;
    }
    // Is the response header ready and waiting?
    // If so, go ahead and do the hook processing
    if (milestones[TS_MILESTONE_SERVER_READ_HEADER_DONE] != 0) {
      t_state.current.state         = HttpTransact::CONNECTION_ALIVE;
      t_state.transact_return_point = HttpTransact::HandleResponse;
      t_state.api_next_action       = HttpTransact::StateMachineAction_t::API_READ_RESPONSE_HDR;
      do_api_callout();
    }
    break;
  default:
    ink_release_assert(0);
  }

  return 0;
}

void
HttpSM::setup_tunnel_handler_trailer(HttpTunnelProducer *p)
{
  p->read_success               = true;
  t_state.current.server->state = HttpTransact::TRANSACTION_COMPLETE;
  t_state.current.server->abort = HttpTransact::DIDNOT_ABORT;

  SMDbg(dbg_ctl_http, "Wait for the trailing header");

  // Swap out the default hander to set up the new tunnel for the trailer exchange.
  HTTP_SM_SET_DEFAULT_HANDLER(&HttpSM::tunnel_handler_trailer);
  if (_ua.get_txn()) {
    _ua.get_txn()->set_expect_send_trailer();
  }
  tunnel.local_finish_all(p);
}

int
HttpSM::tunnel_handler_trailer(int event, void *data)
{
  STATE_ENTER(&HttpSM::tunnel_handler_trailer, event);

  switch (event) {
  case HTTP_TUNNEL_EVENT_DONE: // Response tunnel done.
    break;

  default:
    // If the response tunnel did not succeed, just clean up as in the default case
    return tunnel_handler(event, data);
  }

  ink_assert(event == HTTP_TUNNEL_EVENT_DONE);

  // Set up a new tunnel to transport the trailing header to the UA
  HTTP_SM_SET_DEFAULT_HANDLER(&HttpSM::tunnel_handler);

  MIOBuffer      *trailer_buffer = new_MIOBuffer(HTTP_HEADER_BUFFER_SIZE_INDEX);
  IOBufferReader *buf_start      = trailer_buffer->alloc_reader();

  size_t nbytes      = INT64_MAX;
  int    start_bytes = trailer_buffer->write(server_txn->get_remote_reader(), server_txn->get_remote_reader()->read_avail());
  server_txn->get_remote_reader()->consume(start_bytes);
  // The server has already sent all it has
  if (server_txn->is_read_closed()) {
    nbytes = start_bytes;
  }
  // Signal the _ua.get_txn() to get ready for a trailer
  _ua.get_txn()->set_expect_send_trailer();
  tunnel.deallocate_buffers();
  tunnel.reset();
  HttpTunnelProducer *p = tunnel.add_producer(server_entry->vc, nbytes, buf_start, &HttpSM::tunnel_handler_trailer_server,
                                              HttpTunnelType_t::HTTP_SERVER, "http server trailer");
  tunnel.add_consumer(_ua.get_entry()->vc, server_entry->vc, &HttpSM::tunnel_handler_trailer_ua, HttpTunnelType_t::HTTP_CLIENT,
                      "user agent trailer");

  _ua.get_entry()->in_tunnel = true;
  server_entry->in_tunnel    = true;

  tunnel.tunnel_run(p);

  return 0;
}

int
HttpSM::tunnel_handler_cache_fill(int event, void *data)
{
  STATE_ENTER(&HttpSM::tunnel_handler_cache_fill, event);

  ink_assert(event == HTTP_TUNNEL_EVENT_DONE);
  ink_assert(data == &tunnel);

  ink_release_assert(cache_sm.cache_write_vc);

  int64_t         alloc_index = find_server_buffer_size();
  MIOBuffer      *buf         = new_MIOBuffer(alloc_index);
  IOBufferReader *buf_start   = buf->alloc_reader();

  TunnelChunkingAction_t action =
    (t_state.current.server && t_state.current.server->transfer_encoding == HttpTransact::TransferEncoding_t::CHUNKED) ?
      TunnelChunkingAction_t::DECHUNK_CONTENT :
      TunnelChunkingAction_t::PASSTHRU_DECHUNKED_CONTENT;

  int64_t nbytes = server_transfer_init(buf, 0);

  HTTP_SM_SET_DEFAULT_HANDLER(&HttpSM::tunnel_handler);

  server_entry->vc      = server_txn;
  HttpTunnelProducer *p = tunnel.add_producer(server_entry->vc, nbytes, buf_start, &HttpSM::tunnel_handler_server,
                                              HttpTunnelType_t::HTTP_SERVER, "http server");

  bool const drop_chunked_trailers = t_state.http_config_param->oride.http_drop_chunked_trailers == 1;
  bool const parse_chunk_strictly  = t_state.http_config_param->oride.http_strict_chunk_parsing == 1;
  tunnel.set_producer_chunking_action(p, 0, action, drop_chunked_trailers, parse_chunk_strictly);
  tunnel.set_producer_chunking_size(p, t_state.txn_conf->http_chunking_size);

  setup_cache_write_transfer(&cache_sm, server_entry->vc, &t_state.cache_info.object_store, 0, "cache write");

  server_entry->in_tunnel = true;
  // Kick off the new producer
  tunnel.tunnel_run(p);

  return 0;
}

int
HttpSM::tunnel_handler_100_continue(int event, void *data)
{
  STATE_ENTER(&HttpSM::tunnel_handler_100_continue, event);

  ink_assert(event == HTTP_TUNNEL_EVENT_DONE);
  ink_assert(data == &tunnel);

  // We're done sending the 100 continue.  If we succeeded,
  //   we set up to parse the next server response.  If we
  //   failed, shutdown the state machine
  HttpTunnelConsumer *c = tunnel.get_consumer(_ua.get_txn());

  if (c->write_success) {
    // Note: we must use destroy() here since clear()
    //  does not free the memory from the header
    t_state.hdr_info.client_response.destroy();
    tunnel.deallocate_buffers();
    this->postbuf_clear();
    tunnel.reset();

    if (server_entry->eos) {
      // if the server closed while sending the
      //    100 continue header, handle it here so we
      //    don't assert later
      SMDbg(dbg_ctl_http, "server already closed, terminating connection");

      // Since 100 isn't a final (loggable) response header
      //   kill the 100 continue header and create an empty one
      t_state.hdr_info.server_response.destroy();
      t_state.hdr_info.server_response.create(HTTPType::RESPONSE);
      handle_server_setup_error(VC_EVENT_EOS, server_entry->read_vio);
    } else {
      do_setup_client_request_body_tunnel(HttpVC_t::SERVER_VC);
    }
  } else {
    terminate_sm = true;
  }

  return 0;
}

int
HttpSM::tunnel_handler_push(int event, void *data)
{
  STATE_ENTER(&HttpSM::tunnel_handler_push, event);

  ink_assert(event == HTTP_TUNNEL_EVENT_DONE);
  ink_assert(data == &tunnel);

  // Check to see if the client is still around
  HttpTunnelProducer *ua =
    (_ua.get_txn()) ? tunnel.get_producer(_ua.get_txn()) : tunnel.get_producer(HttpTunnelType_t::HTTP_CLIENT);

  if (ua == nullptr || !ua->read_success) {
    // Client failed to send the body, it's gone.  Kill the
    // state machine
    terminate_sm = true;
    return 0;
  }

  HttpTunnelConsumer *cache = ua->consumer_list.head;
  ink_release_assert(cache->vc_type == HttpTunnelType_t::CACHE_WRITE);
  bool cache_write_success = cache->write_success;

  // Reset tunneling state since we need to send a response
  //  to client as whether we succeeded
  tunnel.deallocate_buffers();
  this->postbuf_clear();
  tunnel.reset();

  if (cache_write_success) {
    call_transact_and_set_next_state(HttpTransact::HandlePushTunnelSuccess);
  } else {
    call_transact_and_set_next_state(HttpTransact::HandlePushTunnelFailure);
  }

  return 0;
}

int
HttpSM::tunnel_handler(int event, void * /* data ATS_UNUSED */)
{
  STATE_ENTER(&HttpSM::tunnel_handler, event);

  // If we had already received EOS, just go away. We would sometimes see
  // a WRITE event appear after receiving EOS from the server connection
  if ((event == VC_EVENT_WRITE_READY || event == VC_EVENT_WRITE_COMPLETE) && server_entry->eos) {
    return 0;
  }

  ink_assert(event == HTTP_TUNNEL_EVENT_DONE || event == VC_EVENT_INACTIVITY_TIMEOUT);
  // The tunnel calls this when it is done
  terminate_sm = true;

  if (unlikely(t_state.is_websocket)) {
    Metrics::Gauge::decrement(http_rsb.websocket_current_active_client_connections);
  }

  return 0;
}

/****************************************************
   TUNNELING HANDLERS
   ******************************************************/

bool
HttpSM::is_http_server_eos_truncation(HttpTunnelProducer *p)
{
  if ((p->do_dechunking || p->do_chunked_passthru) && p->chunked_handler.truncation) {
    return true;
  }

  //////////////////////////////////////////////////////////////
  // If we did not get or did not trust the origin server's   //
  //  content-length, read_content_length is unset.  The      //
  //  only way the end of the document is signaled is the     //
  //  origin server closing the connection.  However, we      //
  //  need to protect against the document getting truncated  //
  //  because the origin server crashed.  The following       //
  //  tabled outlines when we mark the server read as failed  //
  //                                                          //
  //    No C-L               :  read success                  //
  //    Received byts < C-L  :  read failed (=> Cache Abort)  //
  //    Received byts == C-L :  read success                  //
  //    Received byts > C-L  :  read success                  //
  //////////////////////////////////////////////////////////////
  int64_t cl = t_state.hdr_info.server_response.get_content_length();

  if (cl != UNDEFINED_COUNT && cl > server_response_body_bytes) {
    SMDbg(dbg_ctl_http, "server EOS after %" PRId64 " bytes, expected %" PRId64, server_response_body_bytes, cl);
    return true;
  } else {
    return false;
  }
}

int
HttpSM::tunnel_handler_server(int event, HttpTunnelProducer *p)
{
  STATE_ENTER(&HttpSM::tunnel_handler_server, event);

  // An intercept handler may not set TS_MILESTONE_SERVER_CONNECT
  // by default. Therefore we only set TS_MILESTONE_SERVER_CLOSE if
  // TS_MILESTONE_SERVER_CONNECT is set (non-zero), lest certain time
  // statistics are calculated from epoch time.
  if (0 != milestones[TS_MILESTONE_SERVER_CONNECT]) {
    ATS_PROBE1(milestone_server_close, sm_id);
    milestones[TS_MILESTONE_SERVER_CLOSE] = ink_get_hrtime();
  }

  bool close_connection = false;

  if (t_state.current.server->keep_alive == HTTPKeepAlive::KEEPALIVE && server_entry->eos == false &&
      plugin_tunnel_type == HttpPluginTunnel_t::NONE && t_state.txn_conf->keep_alive_enabled_out == 1) {
    close_connection = false;
  } else {
    if (t_state.current.server->keep_alive != HTTPKeepAlive::KEEPALIVE) {
      Metrics::Counter::increment(http_rsb.origin_shutdown_tunnel_server_no_keep_alive);
    } else if (server_entry->eos == true) {
      Metrics::Counter::increment(http_rsb.origin_shutdown_tunnel_server_eos);
    } else {
      Metrics::Counter::increment(http_rsb.origin_shutdown_tunnel_server_plugin_tunnel);
    }
    close_connection = true;
  }

  switch (event) {
  case VC_EVENT_INACTIVITY_TIMEOUT:
  case VC_EVENT_ACTIVE_TIMEOUT:
  case VC_EVENT_ERROR:
    t_state.squid_codes.log_code  = SquidLogCode::ERR_READ_TIMEOUT;
    t_state.squid_codes.hier_code = SquidHierarchyCode::TIMEOUT_DIRECT;
    /* fallthru */

  case VC_EVENT_EOS:
  case HTTP_TUNNEL_EVENT_PARSE_ERROR:

    switch (event) {
    case VC_EVENT_INACTIVITY_TIMEOUT:
      t_state.current.server->state = HttpTransact::INACTIVE_TIMEOUT;
      break;
    case VC_EVENT_ACTIVE_TIMEOUT:
      t_state.current.server->state = HttpTransact::ACTIVE_TIMEOUT;
      break;
    case VC_EVENT_ERROR:
      t_state.current.server->state = HttpTransact::CONNECTION_ERROR;
      break;
    case VC_EVENT_EOS:
      t_state.current.server->state = HttpTransact::TRANSACTION_COMPLETE;
      break;
    case HTTP_TUNNEL_EVENT_PARSE_ERROR:
      t_state.current.server->state = HttpTransact::PARSE_ERROR;
      break;
    }
    Metrics::Counter::increment(http_rsb.origin_shutdown_tunnel_server);
    close_connection = true;

    ink_assert(p->vc_type == HttpTunnelType_t::HTTP_SERVER);

    if (is_http_server_eos_truncation(p)) {
      SMDbg(dbg_ctl_http, "aborting HTTP tunnel due to server truncation");
      tunnel.chain_abort_all(p);
      // UA session may not be in the tunnel yet, don't NULL out the pointer in that case.
      // Note: This is a hack. The correct solution is for the UA session to signal back to the SM
      // when the UA is about to be destroyed and clean up the pointer there. That should be done once
      // the TS-3612 changes are in place (and similarly for the server session).
      /*if (_ua.get_entry()->in_tunnel)
        _ua.set_txn(nullptr); */

      t_state.current.server->abort      = HttpTransact::ABORTED;
      t_state.client_info.keep_alive     = HTTPKeepAlive::NO_KEEPALIVE;
      t_state.current.server->keep_alive = HTTPKeepAlive::NO_KEEPALIVE;
      if (event == VC_EVENT_EOS) {
        t_state.squid_codes.log_code = SquidLogCode::ERR_READ_ERROR;
      }
    } else {
      SMDbg(dbg_ctl_http, "finishing HTTP tunnel");
      p->read_success               = true;
      t_state.current.server->abort = HttpTransact::DIDNOT_ABORT;
      // Appending reason to a response without Content-Length will result in
      // the reason string being written to the client and a bad CL when reading from cache.
      // I didn't find anywhere this appended reason is being used, so commenting it out.
      /*
        if (t_state.is_cacheable_due_to_negative_caching_configuration && p->bytes_read == 0) {
        int reason_len;
        const char *reason = t_state.hdr_info.server_response.reason_get(&reason_len);
        if (reason == NULL)
        tunnel.append_message_to_producer_buffer(p, "Negative Response", sizeof("Negative Response") - 1);
        else
        tunnel.append_message_to_producer_buffer(p, reason, reason_len);
        }
      */
      if (server_txn->expect_receive_trailer()) {
        setup_tunnel_handler_trailer(p);
        return 0;
      }
      tunnel.local_finish_all(p);
    }
    break;

  case HTTP_TUNNEL_EVENT_PRECOMPLETE:
  case VC_EVENT_READ_COMPLETE:
    //
    // The transfer completed successfully
    //    If there is still data in the buffer, the server
    //    sent too much indicating a failed transfer
    p->read_success               = true;
    t_state.current.server->state = HttpTransact::TRANSACTION_COMPLETE;
    t_state.current.server->abort = HttpTransact::DIDNOT_ABORT;

    if (p->do_dechunking || p->do_chunked_passthru) {
      if (p->chunked_handler.truncation) {
        tunnel.abort_cache_write_finish_others(p);
        // We couldn't read all chunks successfully:
        // Disable keep-alive.
        t_state.client_info.keep_alive     = HTTPKeepAlive::NO_KEEPALIVE;
        t_state.current.server->keep_alive = HTTPKeepAlive::NO_KEEPALIVE;
      } else {
        tunnel.local_finish_all(p);
      }
    }
    if (server_txn->expect_receive_trailer()) {
      setup_tunnel_handler_trailer(p);
      return 0;
    }
    break;

  case HTTP_TUNNEL_EVENT_CONSUMER_DETACH:
    // All consumers are prematurely gone.  Shutdown
    //    the server connection
    p->read_success               = true;
    t_state.current.server->state = HttpTransact::TRANSACTION_COMPLETE;
    t_state.current.server->abort = HttpTransact::DIDNOT_ABORT;
    Metrics::Counter::increment(http_rsb.origin_shutdown_tunnel_server_detach);
    close_connection = true;
    break;

  case VC_EVENT_READ_READY:
  case VC_EVENT_WRITE_READY:
  case VC_EVENT_WRITE_COMPLETE:
  default:
    // None of these events should ever come our way
    ink_assert(0);
    break;
  }

  // turn off negative caching in case there are multiple server contacts
  if (t_state.is_cacheable_due_to_negative_caching_configuration) {
    t_state.is_cacheable_due_to_negative_caching_configuration = false;
  }

  // If we had a ground fill, check update our status
  if (background_fill == BackgroundFill_t::STARTED) {
    background_fill = p->read_success ? BackgroundFill_t::COMPLETED : BackgroundFill_t::ABORTED;
    Metrics::Gauge::decrement(http_rsb.background_fill_current_count);
  }
  // We handled the event.  Now either shutdown the connection or
  //   setup it up for keep-alive
  ink_assert(p->vc_type == HttpTunnelType_t::HTTP_SERVER);
  ink_assert(p->vc == server_txn);

  // The server session has been released. Clean all pointer
  // Calling remove_entry instead of server_entry because we don't
  // want to close the server VC at this point
  vc_table.remove_entry(server_entry);

  if (close_connection) {
    p->vc->do_io_close();
    p->read_vio = nullptr;
    /* TS-1424: if we're outbound transparent and using the client
       source port for the outbound connection we must effectively
       propagate server closes back to the client. Part of that is
       disabling KeepAlive if the server closes.
    */
    if (_ua.get_txn() && _ua.get_txn()->is_outbound_transparent() && t_state.http_config_param->use_client_source_port) {
      t_state.client_info.keep_alive = HTTPKeepAlive::NO_KEEPALIVE;
    }
  } else {
    // If the option to attach the server session to the client session is set
    // and if the client is still around and the client is keep-alive, attach the
    // server session to so the next ka request can use it.  Server sessions will
    // be placed into the shared pool if the next incoming request is for a different
    // origin server
    bool release_origin_connection = true;
    if (t_state.txn_conf->attach_server_session_to_client == 1 && _ua.get_txn() &&
        t_state.client_info.keep_alive == HTTPKeepAlive::KEEPALIVE) {
      SMDbg(dbg_ctl_http, "attaching server session to the client");
      if (_ua.get_txn()->attach_server_session(static_cast<PoolableSession *>(server_txn->get_proxy_ssn()))) {
        release_origin_connection = false;
      }
    }
    if (release_origin_connection) {
      // Release the session back into the shared session pool
      server_txn->set_inactivity_timeout(HRTIME_SECONDS(t_state.txn_conf->keep_alive_no_activity_timeout_out));
      server_txn->release();
    }
  }

  return 0;
}

int
HttpSM::tunnel_handler_trailer_server(int event, HttpTunnelProducer *p)
{
  STATE_ENTER(&HttpSM::tunnel_handler_trailer_server, event);

  switch (event) {
  case VC_EVENT_INACTIVITY_TIMEOUT:
  case VC_EVENT_ACTIVE_TIMEOUT:
  case VC_EVENT_ERROR:
    t_state.squid_codes.log_code  = SquidLogCode::ERR_READ_TIMEOUT;
    t_state.squid_codes.hier_code = SquidHierarchyCode::TIMEOUT_DIRECT;
    /* fallthru */

  case VC_EVENT_EOS:

    switch (event) {
    case VC_EVENT_INACTIVITY_TIMEOUT:
      t_state.current.server->state = HttpTransact::INACTIVE_TIMEOUT;
      break;
    case VC_EVENT_ACTIVE_TIMEOUT:
      t_state.current.server->state = HttpTransact::ACTIVE_TIMEOUT;
      break;
    case VC_EVENT_ERROR:
      t_state.current.server->state = HttpTransact::CONNECTION_ERROR;
      break;
    case VC_EVENT_EOS:
      t_state.current.server->state = HttpTransact::TRANSACTION_COMPLETE;
      break;
    }

    ink_assert(p->vc_type == HttpTunnelType_t::HTTP_SERVER);

    SMDbg(dbg_ctl_http, "aborting HTTP tunnel due to server truncation");
    tunnel.chain_abort_all(p);

    t_state.current.server->abort      = HttpTransact::ABORTED;
    t_state.client_info.keep_alive     = HTTPKeepAlive::NO_KEEPALIVE;
    t_state.current.server->keep_alive = HTTPKeepAlive::NO_KEEPALIVE;
    t_state.squid_codes.log_code       = SquidLogCode::ERR_READ_ERROR;
    break;

  case HTTP_TUNNEL_EVENT_PRECOMPLETE:
  case VC_EVENT_READ_COMPLETE:
    //
    // The transfer completed successfully
    p->read_success               = true;
    t_state.current.server->state = HttpTransact::TRANSACTION_COMPLETE;
    t_state.current.server->abort = HttpTransact::DIDNOT_ABORT;
    break;

  case HTTP_TUNNEL_EVENT_CONSUMER_DETACH:
  case VC_EVENT_READ_READY:
  case VC_EVENT_WRITE_READY:
  case VC_EVENT_WRITE_COMPLETE:
  default:
    // None of these events should ever come our way
    ink_assert(0);
    break;
  }

  // We handled the event.  Now either shutdown server transaction
  ink_assert(server_entry->vc == p->vc);
  ink_assert(p->vc_type == HttpTunnelType_t::HTTP_SERVER);
  ink_assert(p->vc == server_txn);

  // The server session has been released. Clean all pointer
  // Calling remove_entry instead of server_entry because we don't
  // want to close the server VC at this point
  vc_table.remove_entry(server_entry);

  p->vc->do_io_close();
  p->read_vio = nullptr;

  server_entry = nullptr;

  return 0;
}

// int HttpSM::tunnel_handler_100_continue_ua(int event, HttpTunnelConsumer* c)
//
//     Used for tunneling the 100 continue response.  The tunnel
//       should not close or release the user agent unless there is
//       an error since the real response is yet to come
//
int
HttpSM::tunnel_handler_100_continue_ua(int event, HttpTunnelConsumer *c)
{
  STATE_ENTER(&HttpSM::tunnel_handler_100_continue_ua, event);

  ink_assert(c->vc == _ua.get_txn());

  switch (event) {
  case VC_EVENT_EOS:
    _ua.get_entry()->eos = true;
  // FALL-THROUGH
  case VC_EVENT_INACTIVITY_TIMEOUT:
  case VC_EVENT_ACTIVE_TIMEOUT:
  case VC_EVENT_ERROR:
    set_ua_abort(HttpTransact::ABORTED, event);
    vc_table.remove_entry(_ua.get_entry());
    c->vc->do_io_close();
    break;
  case VC_EVENT_WRITE_COMPLETE:
    // mark the vc as no longer in tunnel
    //   so we don't get hosed if the ua abort before
    //   real response header is received
    _ua.get_entry()->in_tunnel = false;
    c->write_success           = true;

    // remove the buffer reader from the consumer's vc
    if (c->vc != nullptr) {
      c->vc->do_io_write();
    }
  }

  return 0;
}

bool
HttpSM::is_bg_fill_necessary(HttpTunnelConsumer *c)
{
  ink_assert(c->vc_type == HttpTunnelType_t::HTTP_CLIENT);

  if (c->producer->alive &&          // something there to read
                                     //      server_entry && server_entry->vc &&              // from an origin server
                                     //      server_txn && server_txn->get_netvc() && // which is still open and valid
      c->producer->num_consumers > 1 // with someone else reading it
  ) {
    HttpTunnelProducer *p = nullptr;

    if (!server_txn || !server_txn->get_netvc()) {
      // return true if we have finished the reading from OS when client aborted
      p = c->producer->self_consumer ? c->producer->self_consumer->producer : c->producer;
      if (p->vc_type == HttpTunnelType_t::HTTP_SERVER && p->read_success) {
        return true;
      } else {
        return false;
      }
    }
    // If threshold is 0.0 or negative then do background
    //   fill regardless of the content length.  Since this
    //   is floating point just make sure the number is near zero
    if (t_state.txn_conf->background_fill_threshold <= 0.001) {
      return true;
    }

    int64_t ua_cl = t_state.hdr_info.client_response.get_content_length();

    if (ua_cl > 0) {
      int64_t ua_body_done = c->bytes_written - client_response_hdr_bytes;
      float   pDone        = static_cast<float>(ua_body_done) / ua_cl;

      // If we got a good content length.  Check to make sure that we haven't already
      //  done more the content length since that would indicate the content-length
      //  is bogus.  If we've done more than the threshold, continue the background fill
      if (pDone <= 1.0 && pDone > t_state.txn_conf->background_fill_threshold) {
        return true;
      } else {
        SMDbg(dbg_ctl_http, "no background.  Only %%%f of %%%f done [%" PRId64 " / %" PRId64 " ]", pDone,
              t_state.txn_conf->background_fill_threshold, ua_body_done, ua_cl);
      }
    }
  }

  return false;
}

int
HttpSM::tunnel_handler_ua(int event, HttpTunnelConsumer *c)
{
  bool                close_connection = true;
  HttpTunnelProducer *p                = nullptr;
  HttpTunnelConsumer *selfc            = nullptr;

  STATE_ENTER(&HttpSM::tunnel_handler_ua, event);
  ink_assert(c->vc == _ua.get_txn());
  ATS_PROBE1(milestone_ua_close, sm_id);
  milestones[TS_MILESTONE_UA_CLOSE] = ink_get_hrtime();

  switch (event) {
  case VC_EVENT_EOS:
    _ua.get_entry()->eos = true;

  // FALL-THROUGH
  case VC_EVENT_INACTIVITY_TIMEOUT:
  case VC_EVENT_ACTIVE_TIMEOUT:
  case VC_EVENT_ERROR:

    // The user agent died or aborted.  Check to
    //  see if we should setup a background fill
    set_ua_abort(HttpTransact::ABORTED, event);

    if (is_bg_fill_necessary(c)) {
      p = c->producer->self_consumer ? c->producer->self_consumer->producer : c->producer;
      SMDbg(dbg_ctl_http, "Initiating background fill");
      // check whether to finish the reading.
      background_fill = p->read_success ? BackgroundFill_t::COMPLETED : BackgroundFill_t::STARTED;

      // There is another consumer (cache write) so
      //  detach the user agent
      if (background_fill == BackgroundFill_t::STARTED) {
        Metrics::Gauge::increment(http_rsb.background_fill_current_count);
        Metrics::Counter::increment(http_rsb.background_fill_total_count);

        ink_assert(c->is_downstream_from(server_txn));
        server_txn->set_active_timeout(HRTIME_SECONDS(t_state.txn_conf->background_fill_active_timeout));
      }

      // Even with the background fill, the client side should go down
      c->write_vio = nullptr;
      vc_table.remove_entry(_ua.get_entry());
      c->vc->do_io_close(EHTTP_ERROR);
      c->alive = false;

    } else {
      // No background fill
      p = c->producer;
      tunnel.chain_abort_all(c->producer);
      selfc = p->self_consumer;
      if (selfc) {
        // This is the case where there is a transformation between ua and os
        p = selfc->producer;
        // if producer is the cache or OS, close the producer.
        // Otherwise in case of large docs, producer iobuffer gets filled up,
        // waiting for a consumer to consume data and the connection is never closed.
        if (p->alive && ((p->vc_type == HttpTunnelType_t::CACHE_READ) || (p->vc_type == HttpTunnelType_t::HTTP_SERVER))) {
          tunnel.chain_abort_all(p);
        }
      }
    }
    break;

  case VC_EVENT_WRITE_COMPLETE:
    c->write_success          = true;
    t_state.client_info.abort = HttpTransact::DIDNOT_ABORT;
    if (t_state.client_info.keep_alive == HTTPKeepAlive::KEEPALIVE) {
      if (t_state.www_auth_content != HttpTransact::CacheAuth_t::SERVE || _ua.get_txn()->get_server_session()) {
        // successful keep-alive
        close_connection = false;
      }
    }
    break;
  case VC_EVENT_WRITE_READY:
  case VC_EVENT_READ_READY:
  case VC_EVENT_READ_COMPLETE:
  default:
    // None of these events should ever come our way
    ink_assert(0);
    break;
  }

  client_response_body_bytes = c->bytes_written - client_response_hdr_bytes;

  if (client_response_body_bytes < 0) {
    client_response_body_bytes = 0;
  }

  // attribute the size written to the client from various sources
  // NOTE: responses that go through a range transform are attributed
  // to their original sources
  // all other transforms attribute the total number of input bytes
  // to a source in HttpSM::tunnel_handler_transform_write
  //
  HttpTransact::Source_t original_source = t_state.source;
  if (HttpTransact::Source_t::TRANSFORM == original_source && t_state.range_setup != HttpTransact::RangeSetup_t::NONE) {
    original_source = t_state.pre_transform_source;
  }

  switch (original_source) {
  case HttpTransact::Source_t::HTTP_ORIGIN_SERVER:
    server_response_body_bytes = client_response_body_bytes;
    break;
  case HttpTransact::Source_t::CACHE:
    cache_response_body_bytes = client_response_body_bytes;
    break;
  default:
    break;
  }

  if (event == VC_EVENT_WRITE_COMPLETE && server_txn && server_txn->expect_receive_trailer()) {
    // Don't shutdown if we are still expecting a trailer
  } else if (close_connection) {
    // If the client could be pipelining or is doing a POST, we need to
    //   set the _ua.get_txn() into half close mode

    // only external POSTs should be subject to this logic; ruling out internal POSTs here
    bool is_eligible_post_request = ((t_state.method == HTTP_WKSIDX_POST) && !is_internal);

    if (is_eligible_post_request && c->producer->vc_type != HttpTunnelType_t::STATIC && event == VC_EVENT_WRITE_COMPLETE) {
      _ua.get_txn()->set_half_close_flag(true);
    }

    vc_table.remove_entry(this->_ua.get_entry());
    ink_release_assert(vc_table.find_entry(_ua.get_txn()) == nullptr);
    _ua.get_txn()->do_io_close();
  } else {
    ink_assert(_ua.get_txn()->get_remote_reader() != nullptr);
    vc_table.remove_entry(this->_ua.get_entry());
    _ua.get_txn()->release();
  }

  return 0;
}

int
HttpSM::tunnel_handler_trailer_ua(int event, HttpTunnelConsumer *c)
{
  HttpTunnelProducer *p     = nullptr;
  HttpTunnelConsumer *selfc = nullptr;

  STATE_ENTER(&HttpSM::tunnel_handler_trailer_ua, event);
  ink_assert(c->vc == _ua.get_txn());
  ATS_PROBE1(milestone_ua_close, sm_id);
  milestones[TS_MILESTONE_UA_CLOSE] = ink_get_hrtime();

  switch (event) {
  case VC_EVENT_EOS:
    _ua.get_entry()->eos = true;

  // FALL-THROUGH
  case VC_EVENT_INACTIVITY_TIMEOUT:
  case VC_EVENT_ACTIVE_TIMEOUT:
  case VC_EVENT_ERROR:

    // The user agent died or aborted.  Check to
    //  see if we should setup a background fill
    set_ua_abort(HttpTransact::ABORTED, event);

    // Should not be processing trailer headers in the background fill case
    ink_assert(!is_bg_fill_necessary(c));
    p = c->producer;
    tunnel.chain_abort_all(c->producer);
    selfc = p->self_consumer;
    if (selfc) {
      // This is the case where there is a transformation between ua and os
      p = selfc->producer;
      // if producer is the cache or OS, close the producer.
      // Otherwise in case of large docs, producer iobuffer gets filled up,
      // waiting for a consumer to consume data and the connection is never closed.
      if (p->alive && ((p->vc_type == HttpTunnelType_t::CACHE_READ) || (p->vc_type == HttpTunnelType_t::HTTP_SERVER))) {
        tunnel.chain_abort_all(p);
      }
    }
    break;

  case VC_EVENT_WRITE_COMPLETE:
    c->write_success          = true;
    t_state.client_info.abort = HttpTransact::DIDNOT_ABORT;
    break;
  case VC_EVENT_WRITE_READY:
  case VC_EVENT_READ_READY:
  case VC_EVENT_READ_COMPLETE:
  default:
    // None of these events should ever come our way
    ink_assert(0);
    break;
  }

  ink_assert(_ua.get_entry()->vc == c->vc);
  vc_table.remove_entry(this->_ua.get_entry());
  _ua.get_txn()->do_io_close();
  ink_release_assert(vc_table.find_entry(_ua.get_txn()) == nullptr);
  return 0;
}

int
HttpSM::tunnel_handler_ua_push(int event, HttpTunnelProducer *p)
{
  STATE_ENTER(&HttpSM::tunnel_handler_ua_push, event);

  pushed_response_body_bytes += p->bytes_read;
  client_request_body_bytes  += p->bytes_read;

  switch (event) {
  case VC_EVENT_INACTIVITY_TIMEOUT:
  case VC_EVENT_ACTIVE_TIMEOUT:
  case VC_EVENT_ERROR:
  case VC_EVENT_EOS:
    // Transfer terminated.  Bail on the cache write.
    set_ua_abort(HttpTransact::ABORTED, event);
    p->vc->do_io_close(EHTTP_ERROR);
    p->read_vio = nullptr;
    tunnel.chain_abort_all(p);
    break;

  case HTTP_TUNNEL_EVENT_PRECOMPLETE:
  case VC_EVENT_READ_COMPLETE:
    //
    // The transfer completed successfully
    p->read_success            = true;
    _ua.get_entry()->in_tunnel = false;
    break;

  case VC_EVENT_READ_READY:
  case VC_EVENT_WRITE_READY:
  case VC_EVENT_WRITE_COMPLETE:
  default:
    // None of these events should ever come our way
    ink_assert(0);
    break;
  }

  return 0;
}

int
HttpSM::tunnel_handler_cache_read(int event, HttpTunnelProducer *p)
{
  STATE_ENTER(&HttpSM::tunnel_handler_cache_read, event);

  switch (event) {
  case VC_EVENT_ERROR:
  case VC_EVENT_EOS:
    ink_assert(t_state.cache_info.object_read->valid());
    if (t_state.cache_info.object_read->object_size_get() != INT64_MAX || event == VC_EVENT_ERROR) {
      // Abnormal termination
      t_state.squid_codes.log_code = SquidLogCode::TCP_SWAPFAIL;
      p->vc->do_io_close(EHTTP_ERROR);
      p->read_vio = nullptr;
      tunnel.chain_abort_all(p);
      Metrics::Counter::increment(http_rsb.cache_read_errors);
      break;
    } else {
      tunnel.local_finish_all(p);
      // fall through for the case INT64_MAX read with VC_EVENT_EOS
      // callback (read successful)
    }
    // fallthrough

  case VC_EVENT_READ_COMPLETE:
  case HTTP_TUNNEL_EVENT_PRECOMPLETE:
  case HTTP_TUNNEL_EVENT_CONSUMER_DETACH:
    p->read_success = true;
    p->vc->do_io_close();
    p->read_vio = nullptr;
    break;
  default:
    ink_release_assert(0);
    break;
  }

  Metrics::Gauge::decrement(http_rsb.current_cache_connections);
  return 0;
}

int
HttpSM::tunnel_handler_cache_write(int event, HttpTunnelConsumer *c)
{
  STATE_ENTER(&HttpSM::tunnel_handler_cache_write, event);
  SMDbg(dbg_ctl_http, "handling cache event: %s", HttpDebugNames::get_event_name(event));

  HttpTransact::CacheWriteStatus_t *status_ptr = (c->producer->vc_type == HttpTunnelType_t::TRANSFORM) ?
                                                   &t_state.cache_info.transform_write_status :
                                                   &t_state.cache_info.write_status;

  switch (event) {
  case VC_EVENT_ERROR:
  case VC_EVENT_EOS:
    // Abnormal termination
    *status_ptr  = HttpTransact::CacheWriteStatus_t::ERROR;
    c->write_vio = nullptr;
    c->vc->do_io_close(EHTTP_ERROR);

    Metrics::Counter::increment(http_rsb.cache_write_errors);
    SMDbg(dbg_ctl_http, "aborting cache write due %s event from cache", HttpDebugNames::get_event_name(event));
    // abort the producer if the cache_writevc is the only consumer.
    if (c->producer->alive && c->producer->num_consumers == 1) {
      tunnel.chain_abort_all(c->producer);
    }
    break;
  case VC_EVENT_WRITE_COMPLETE:
    // if we've never initiated a cache write
    //   abort the cache since it's finicky about a close
    //   in this case.  This case can only occur
    //   we got a truncated header from the origin server
    //   but decided to accept it anyways
    if (c->write_vio == nullptr) {
      *status_ptr      = HttpTransact::CacheWriteStatus_t::ERROR;
      c->write_success = false;
      c->vc->do_io_close(EHTTP_ERROR);
    } else {
      *status_ptr      = HttpTransact::CacheWriteStatus_t::COMPLETE;
      c->write_success = true;
      c->vc->do_io_close();
      c->write_vio = nullptr;
    }
    break;
  default:
    // All other events indicate problems
    ink_assert(0);
    break;
  }

  if (background_fill != BackgroundFill_t::NONE) {
    server_response_body_bytes = c->bytes_written;
  }

  Metrics::Gauge::decrement(http_rsb.current_cache_connections);
  return 0;
}

int
HttpSM::tunnel_handler_post_ua(int event, HttpTunnelProducer *p)
{
  STATE_ENTER(&HttpSM::tunnel_handler_post_ua, event);

  // Now that the tunnel is done, it can tell us how many bytes were in the
  // body.
  if (client_request_body_bytes == 0) {
    // This is invoked multiple times for a transaction when buffering request
    // body data, so we only call this the first time when
    // client_request_body_bytes is 0.
    client_request_body_bytes     = p->bytes_consumed;
    IOBufferReader *client_reader = _ua.get_txn()->get_remote_reader();
    // p->bytes_consumed represents the number of body bytes the tunnel parsed
    // and consumed from the client. However, not all those bytes may have been
    // written to our _ua client transaction reader. We must not consume past
    // the number of bytes available.
    int64_t const bytes_to_consume = std::min(p->bytes_consumed, client_reader->read_avail());
    SMDbg(dbg_ctl_http_tunnel,
          "Consuming %" PRId64 " bytes from client reader with p->bytes_consumed: %" PRId64 " available: %" PRId64,
          bytes_to_consume, p->bytes_consumed, client_reader->read_avail());
    client_reader->consume(bytes_to_consume);
  }

  switch (event) {
  case VC_EVENT_INACTIVITY_TIMEOUT:
  case VC_EVENT_ACTIVE_TIMEOUT:
  case HTTP_TUNNEL_EVENT_PARSE_ERROR:
    if (client_response_hdr_bytes == 0) {
      p->handler_state = static_cast<int>(HttpSmPost_t::UA_FAIL);
      set_ua_abort(HttpTransact::ABORTED, event);

      SMDbg(dbg_ctl_http_tunnel, "send error response to client to vc %p, tunnel vc %p", _ua.get_txn()->get_netvc(), p->vc);

      tunnel.chain_abort_all(p);
      // Reset the inactivity timeout, otherwise the InactivityCop will callback again in the next second.
      _ua.get_txn()->set_inactivity_timeout(HRTIME_SECONDS(t_state.txn_conf->transaction_no_activity_timeout_in));
      // if it is active timeout case, we need to give another chance to send 408 response;
      _ua.get_txn()->set_active_timeout(HRTIME_SECONDS(t_state.txn_conf->transaction_active_timeout_in));

      return 0;
    }
  // fall through
  case VC_EVENT_EOS:
  // My reading of spec says that user agents can not terminate
  //  posts with a half close so this is an error
  case VC_EVENT_ERROR:
    //  Did not complete post tunneling.  Abort the
    //   server and close the ua
    p->handler_state = static_cast<int>(HttpSmPost_t::UA_FAIL);
    set_ua_abort(HttpTransact::ABORTED, event);
    tunnel.chain_abort_all(p);
    // the in_tunnel status on both the ua & and
    //   it's consumer must already be set to true.  Previously
    //   we were setting it again to true but incorrectly in
    //   the case of a transform
    hsm_release_assert(_ua.get_entry()->in_tunnel == true);
    if (p->consumer_list.head && p->consumer_list.head->vc_type == HttpTunnelType_t::TRANSFORM) {
      hsm_release_assert(post_transform_info.entry->in_tunnel == true);
    } // server side may have completed before the user agent side, so it may no longer be in tunnel

    // In the error case, start to take down the client session. There should
    // be no reuse here
    vc_table.remove_entry(this->_ua.get_entry());
    _ua.get_txn()->do_io_close();
    break;

  case VC_EVENT_READ_COMPLETE:
  case HTTP_TUNNEL_EVENT_PRECOMPLETE:
    p->handler_state           = static_cast<int>(HttpSmPost_t::SUCCESS);
    p->read_success            = true;
    _ua.get_entry()->in_tunnel = false;

    if (p->do_dechunking || p->do_chunked_passthru) {
      if (p->chunked_handler.truncation) {
        tunnel.abort_cache_write_finish_others(p);
      } else {
        tunnel.local_finish_all(p);
      }
    }

    // Now that we have communicated the post body, turn off the inactivity timeout
    // until the server starts sending data back
    if (_ua.get_txn()) {
      _ua.get_txn()->cancel_inactivity_timeout();

      // Initiate another read to catch aborts
      _ua.get_entry()->vc_read_handler  = &HttpSM::state_watch_for_client_abort;
      _ua.get_entry()->vc_write_handler = &HttpSM::state_watch_for_client_abort;
      _ua.get_entry()->read_vio         = p->vc->do_io_read(this, INT64_MAX, _ua.get_txn()->get_remote_reader()->mbuf);
    }
    break;
  default:
    ink_release_assert(0);
  }

  return 0;
}

// YTS Team, yamsat Plugin
// Tunnel handler to deallocate the tunnel buffers and
// set redirect_in_process=false
// Copy partial POST data to buffers. Check for the various parameters including
// the maximum configured post data size
int
HttpSM::tunnel_handler_for_partial_post(int event, void * /* data ATS_UNUSED */)
{
  STATE_ENTER(&HttpSM::tunnel_handler_for_partial_post, event);
  tunnel.deallocate_buffers();
  tunnel.reset();

  t_state.redirect_info.redirect_in_process = false;
  is_buffering_request_body                 = false;

  if (post_failed) {
    post_failed = false;
    handle_post_failure();
  } else {
    do_setup_client_request_body_tunnel(HttpVC_t::SERVER_VC);
  }

  return 0;
}

int
HttpSM::tunnel_handler_post_server(int event, HttpTunnelConsumer *c)
{
  STATE_ENTER(&HttpSM::tunnel_handler_post_server, event);

  // If is_using_post_buffer has been used, then this handler gets called
  // twice, once with the buffered request body bytes and a second time with
  // the (now) zero length user agent buffer. See wait_for_full_body where
  // these bytes are read. Don't clobber the server_request_body_bytes with
  // zero on that second read.
  if (server_request_body_bytes == 0) {
    server_request_body_bytes = c->bytes_written;
  }

  switch (event) {
  case VC_EVENT_EOS:
  case VC_EVENT_ERROR:
  case VC_EVENT_INACTIVITY_TIMEOUT:
  case VC_EVENT_ACTIVE_TIMEOUT:

    switch (event) {
    case VC_EVENT_INACTIVITY_TIMEOUT:
      t_state.current.state = HttpTransact::INACTIVE_TIMEOUT;
      t_state.set_connect_fail(ETIMEDOUT);
      break;
    case VC_EVENT_ACTIVE_TIMEOUT:
      t_state.current.state = HttpTransact::ACTIVE_TIMEOUT;
      t_state.set_connect_fail(ETIMEDOUT);
      break;
    case VC_EVENT_EOS:
      t_state.current.state = HttpTransact::CONNECTION_CLOSED;
      t_state.set_connect_fail(EPIPE);
      break;
    case VC_EVENT_ERROR:
      t_state.current.state = HttpTransact::CONNECTION_CLOSED;
      t_state.set_connect_fail(server_txn->get_netvc()->lerrno);
      break;
    default:
      break;
    }

    //  Did not complete post tunneling
    //
    //    In the http case, we don't want to close
    //    the connection because the
    //    destroys the header buffer which may
    //    a response even though the tunnel failed.

    // Shutdown both sides of the connection.  This prevents us
    //  from getting any further events and signals to client
    //  that POST data will not be forwarded to the server.  Doing
    //  shutdown on the write side will likely generate a TCP
    //  reset to the client but if the proxy wasn't here this is
    //  exactly what would happen.
    // we should wait to shutdown read side of the
    // client to prevent sending a reset
    server_entry->eos = true;
    c->vc->do_io_shutdown(IO_SHUTDOWN_WRITE);

    // We may be reading from a transform.  In that case, we
    //   want to close the transform
    HttpTunnelProducer *ua_producer;
    if (c->producer->vc_type == HttpTunnelType_t::TRANSFORM) {
      if (c->producer->handler_state == HTTP_SM_TRANSFORM_OPEN) {
        ink_assert(c->producer->vc == post_transform_info.vc);
        c->producer->vc->do_io_close();
        c->producer->alive                = false;
        c->producer->self_consumer->alive = false;
      }
      ua_producer = c->producer->self_consumer->producer;
    } else {
      ua_producer = c->producer;
    }
    ink_assert(ua_producer->vc_type == HttpTunnelType_t::HTTP_CLIENT);
    ink_assert(ua_producer->vc == _ua.get_txn());
    ink_assert(ua_producer->vc == _ua.get_entry()->vc);

    // Before shutting down, initiate another read
    //  on the user agent in order to get timeouts
    //  coming to the state machine and not the tunnel
    _ua.get_entry()->vc_read_handler  = &HttpSM::state_watch_for_client_abort;
    _ua.get_entry()->vc_write_handler = &HttpSM::state_watch_for_client_abort;

    // YTS Team, yamsat Plugin
    // When event is VC_EVENT_ERROR,and when redirection is enabled
    // do not shut down the client read
    if (enable_redirection) {
      if (ua_producer->vc_type == HttpTunnelType_t::STATIC && event != VC_EVENT_ERROR && event != VC_EVENT_EOS) {
        _ua.get_entry()->read_vio = ua_producer->vc->do_io_read(this, INT64_MAX, _ua.get_txn()->get_remote_reader()->mbuf);
        // ua_producer->vc->do_io_shutdown(IO_SHUTDOWN_READ);
      } else {
        if (ua_producer->vc_type == HttpTunnelType_t::STATIC && t_state.redirect_info.redirect_in_process) {
          post_failed = true;
        }
      }
    } else {
      _ua.get_entry()->read_vio = ua_producer->vc->do_io_read(this, INT64_MAX, _ua.get_txn()->get_remote_reader()->mbuf);
      // we should not shutdown read side of the client here to prevent sending a reset
      // ua_producer->vc->do_io_shutdown(IO_SHUTDOWN_READ);
    } // end of added logic

    // We want to shutdown the tunnel here and see if there
    //   is a response on from the server.  Mark the user
    //   agent as down so that tunnel concludes.
    ua_producer->alive         = false;
    ua_producer->handler_state = static_cast<int>(HttpSmPost_t::SERVER_FAIL);
    ink_assert(tunnel.is_tunnel_alive() == false);
    break;

  case VC_EVENT_WRITE_COMPLETE:
    // Completed successfully
    c->write_success        = true;
    server_entry->in_tunnel = false;
    break;
  default:
    ink_release_assert(0);
  }

  return 0;
}

int
HttpSM::tunnel_handler_ssl_producer(int event, HttpTunnelProducer *p)
{
  STATE_ENTER(&HttpSM::tunnel_handler_ssl_producer, event);

  switch (event) {
  case VC_EVENT_READ_READY:
    // This event is triggered when receiving DATA frames without the END_STREAM
    // flag set in a HTTP/2 CONNECT request. Breaking as there are more DATA
    // frames to come.
    break;
  case VC_EVENT_READ_COMPLETE:
    // This event is triggered during an HTTP/2 CONNECT request when a DATA
    // frame with the END_STREAM flag set is received, indicating the end of the
    // stream.
    [[fallthrough]];
  case VC_EVENT_EOS:
    // The write side of this connection is still alive
    //  so half-close the read
    if (p->self_consumer->alive) {
      p->vc->do_io_shutdown(IO_SHUTDOWN_READ);
      tunnel.local_finish_all(p);
      break;
    }
  // FALL THROUGH - both sides of the tunnel are dea
  case VC_EVENT_ERROR:
  case VC_EVENT_INACTIVITY_TIMEOUT:
  case VC_EVENT_ACTIVE_TIMEOUT:
    // The other side of the connection is either already dead
    //   or rendered inoperative by the error on the connection
    //   Note: use tunnel close vc so the tunnel knows we are
    //    nuking the of the connection as well
    tunnel.close_vc(p);
    tunnel.local_finish_all(p);

    // Because we've closed the net vc this error came in, it's write
    //  direction is now dead as well.  If that side still being fed data,
    //  we need to kill that pipe as well
    if (p->self_consumer->producer->alive) {
      p->self_consumer->producer->alive = false;
      if (p->self_consumer->producer->self_consumer->alive) {
        p->self_consumer->producer->vc->do_io_shutdown(IO_SHUTDOWN_READ);
      } else {
        tunnel.close_vc(p->self_consumer->producer);
      }
    }
    break;
  case HTTP_TUNNEL_EVENT_PRECOMPLETE:
  // We should never get these event since we don't know
  //  how long the stream is
  default:
    ink_release_assert(0);
  }

  // Update stats
  switch (p->vc_type) {
  case HttpTunnelType_t::HTTP_SERVER:
    server_response_body_bytes += p->bytes_read;
    break;
  case HttpTunnelType_t::HTTP_CLIENT:
    client_request_body_bytes += p->bytes_read;
    break;
  default:
    // Covered here:
    // HttpTunnelType_t::CACHE_READ, HttpTunnelType_t::CACHE_WRITE,
    // HttpTunnelType_t::TRANSFORM, HttpTunnelType_t::STATIC.
    break;
  }

  return 0;
}

int
HttpSM::tunnel_handler_ssl_consumer(int event, HttpTunnelConsumer *c)
{
  STATE_ENTER(&HttpSM::tunnel_handler_ssl_consumer, event);

  switch (event) {
  case VC_EVENT_ERROR:
  case VC_EVENT_EOS:
  case VC_EVENT_INACTIVITY_TIMEOUT:
  case VC_EVENT_ACTIVE_TIMEOUT:
    // we need to mark the producer dead
    // otherwise it can stay alive forever.
    if (c->producer->alive) {
      c->producer->alive = false;
      if (c->producer->self_consumer->alive) {
        c->producer->vc->do_io_shutdown(IO_SHUTDOWN_READ);
      } else {
        tunnel.close_vc(c->producer);
      }
    }
    // Since we are changing the state of the self_producer
    //  we must have the tunnel shutdown the vc
    tunnel.close_vc(c);
    tunnel.local_finish_all(c->self_producer);
    break;

  case VC_EVENT_WRITE_COMPLETE:
    // If we get this event, it means that the producer
    //  has finished and we wrote the remaining data
    //  to the consumer
    //
    // If the read side of this connection has not yet
    //  closed, do a write half-close and then wait for
    //  read side to close so that we don't cut off
    //  pipelined responses with TCP resets
    //
    // ink_assert(c->producer->alive == false);
    c->write_success = true;
    if (c->self_producer->alive == true) {
      c->vc->do_io_shutdown(IO_SHUTDOWN_WRITE);
    } else {
      c->vc->do_io_close();
    }
    break;

  default:
    ink_release_assert(0);
  }

  // Update stats
  switch (c->vc_type) {
  case HttpTunnelType_t::HTTP_SERVER:
    server_request_body_bytes += c->bytes_written;
    break;
  case HttpTunnelType_t::HTTP_CLIENT:
    client_response_body_bytes += c->bytes_written;
    break;
  default:
    // Handled here:
    // HttpTunnelType_t::CACHE_READ, HttpTunnelType_t::CACHE_WRITE, HttpTunnelType_t::TRANSFORM,
    // HttpTunnelType_t::STATIC
    break;
  }

  return 0;
}

int
HttpSM::tunnel_handler_transform_write(int event, HttpTunnelConsumer *c)
{
  STATE_ENTER(&HttpSM::tunnel_handler_transform_write, event);

  HttpTransformInfo *i;

  // Figure out if this the request or response transform
  // : use post_transform_info.entry because post_transform_info.vc
  // is not set to NULL after the post transform is done.
  if (post_transform_info.entry && post_transform_info.entry->vc == c->vc) {
    i = &post_transform_info;
  } else {
    i = &transform_info;
    ink_assert(c->vc == i->vc);
    ink_assert(c->vc == i->entry->vc);
  }

  switch (event) {
  case VC_EVENT_ERROR:
    // Transform error
    tunnel.chain_abort_all(c->producer);
    c->handler_state = HTTP_SM_TRANSFORM_FAIL;
    c->vc->do_io_close(EHTTP_ERROR);
    break;
  case VC_EVENT_EOS:
    //   It possible the transform quit
    //   before the producer finished.  If this is true
    //   we need shut  down the producer if it doesn't
    //   have other consumers to serve or else it will
    //   fill up buffer and get hung
    if (c->producer->alive && c->producer->num_consumers == 1) {
      // Send a tunnel detach event to the producer
      //   to shut it down but indicates it should not abort
      //   downstream (on the other side of the transform)
      //   cache writes
      tunnel.producer_handler(HTTP_TUNNEL_EVENT_CONSUMER_DETACH, c->producer);
    }
  // FALLTHROUGH
  case VC_EVENT_WRITE_COMPLETE:
    // write to transform complete - shutdown the write side
    c->write_success = true;
    c->vc->do_io_shutdown(IO_SHUTDOWN_WRITE);

    // If the read side has not started up yet, then the
    //  this transform_vc is no longer owned by the tunnel
    if (c->self_producer == nullptr) {
      i->entry->in_tunnel = false;
    } else if (c->self_producer->alive == false) {
      // The read side of the Transform
      //   has already completed (possible when the
      //   transform intentionally truncates the response).
      //   So close it
      c->vc->do_io_close();
    }
    break;
  default:
    ink_release_assert(0);
  }

  // attribute the size written to the transform from various sources
  // NOTE: the range transform is excluded from this accounting and
  // is instead handled in HttpSM::tunnel_handler_ua
  //
  // the reasoning is that the range transform is internal functionality
  // in support of HTTP 1.1 compliance, therefore part of "normal" operation
  // all other transforms are plugin driven and the difference between
  // source data and final data should represent the transformation delta
  //
  if (t_state.range_setup == HttpTransact::RangeSetup_t::NONE) {
    switch (t_state.pre_transform_source) {
    case HttpTransact::Source_t::HTTP_ORIGIN_SERVER:
      server_response_body_bytes = client_response_body_bytes;
      break;
    case HttpTransact::Source_t::CACHE:
      cache_response_body_bytes = client_response_body_bytes;
      break;
    default:
      break;
    }
  }

  return 0;
}

int
HttpSM::tunnel_handler_transform_read(int event, HttpTunnelProducer *p)
{
  STATE_ENTER(&HttpSM::tunnel_handler_transform_read, event);

  ink_assert(p->vc == transform_info.vc || p->vc == post_transform_info.vc);

  switch (event) {
  case VC_EVENT_ERROR:
    // Transform error
    tunnel.chain_abort_all(p->self_consumer->producer);
    break;
  case VC_EVENT_EOS:
    // If we did not get enough data from the transform abort the
    //    cache write otherwise fallthrough to the transform
    //    completing successfully
    if (t_state.hdr_info.transform_response_cl != HTTP_UNDEFINED_CL &&
        p->read_vio->nbytes < t_state.hdr_info.transform_response_cl) {
      tunnel.abort_cache_write_finish_others(p);
      break;
    }
  // FALL-THROUGH
  case VC_EVENT_READ_COMPLETE:
  case HTTP_TUNNEL_EVENT_PRECOMPLETE:
    // Transform complete
    p->read_success = true;
    tunnel.local_finish_all(p);
    break;
  default:
    ink_release_assert(0);
  }

  // it's possible that the write side of the
  //  transform hasn't detached yet.  If it is still alive,
  //  don't close the transform vc
  if (p->self_consumer->alive == false) {
    Metrics::Counter::increment(http_rsb.origin_shutdown_tunnel_transform_read);
    p->vc->do_io_close();
  }
  p->handler_state = HTTP_SM_TRANSFORM_CLOSED;

  return 0;
}

int
HttpSM::tunnel_handler_plugin_agent(int event, HttpTunnelConsumer *c)
{
  STATE_ENTER(&HttpSM::tunnel_handler_plugin_client, event);

  switch (event) {
  case VC_EVENT_ERROR:
    c->vc->do_io_close(EHTTP_ERROR); // close up
    // Signal producer if we're the last consumer.
    if (c->producer->alive && c->producer->num_consumers == 1) {
      tunnel.producer_handler(HTTP_TUNNEL_EVENT_CONSUMER_DETACH, c->producer);
    }
    break;
  case VC_EVENT_EOS:
    if (c->producer->alive && c->producer->num_consumers == 1) {
      tunnel.producer_handler(HTTP_TUNNEL_EVENT_CONSUMER_DETACH, c->producer);
    }
  // FALLTHROUGH
  case VC_EVENT_WRITE_COMPLETE:
    c->write_success = true;
    c->vc->do_io_close();
    break;
  default:
    ink_release_assert(0);
  }

  return 0;
}

int
HttpSM::state_remap_request(int event, void * /* data ATS_UNUSED */)
{
  STATE_ENTER(&HttpSM::state_remap_request, event);

  switch (event) {
  case EVENT_REMAP_ERROR: {
    ink_assert(!"this doesn't happen");
    pending_action = nullptr;
    Error("error remapping request [see previous errors]");
    call_transact_and_set_next_state(HttpTransact::HandleRequest); // HandleRequest skips EndRemapRequest
    break;
  }

  case EVENT_REMAP_COMPLETE: {
    pending_action = nullptr;
    SMDbg(dbg_ctl_url_rewrite, "completed processor-based remapping request");
    t_state.url_remap_success = remapProcessor.finish_remap(&t_state, m_remap);
    call_transact_and_set_next_state(nullptr);
    break;
  }

  default:
    ink_assert(!"Unexpected event inside state_remap_request");
    break;
  }

  return 0;
}

// This check must be called before remap.  Otherwise, the client_request host
// name may be changed.
void
HttpSM::check_sni_host()
{
  // Check that the SNI and host name fields match, if it matters
  // Issue warning or mark the transaction to be terminated as necessary
  auto host_name{t_state.hdr_info.client_request.host_get()};
  auto host_len{static_cast<int>(host_name.length())};

  if (host_name.empty()) {
    return;
  }

  auto *netvc = _ua.get_txn()->get_netvc();
  if (netvc == nullptr) {
    return;
  }

  auto *snis = netvc->get_service<TLSSNISupport>();
  if (snis == nullptr) {
    return;
  }

  int host_sni_policy = t_state.http_config_param->http_host_sni_policy;
  if (snis->would_have_actions_for(std::string{host_name}.c_str(), netvc->get_remote_endpoint(), host_sni_policy) &&
      host_sni_policy > 0) {
    // In a SNI/Host mismatch where the Host would have triggered SNI policy, mark the transaction
    // to be considered for rejection after the remap phase passes.  Gives the opportunity to conf_remap
    // override the policy to be rejected in the end_remap logic
    const char *sni_value    = snis->get_sni_server_name();
    const char *action_value = host_sni_policy == 2 ? "terminate" : "continue";
    if (!sni_value || sni_value[0] == '\0') { // No SNI
      Warning("No SNI for TLS request with hostname %.*s action=%s", host_len, host_name.data(), action_value);
      SMDbg(dbg_ctl_ssl_sni, "No SNI for TLS request with hostname %.*s action=%s", host_len, host_name.data(), action_value);
      if (host_sni_policy == 2) {
        swoc::bwprint(error_bw_buffer, "No SNI for TLS request: connecting to {} for host='{}', returning a 403",
                      t_state.client_info.dst_addr, host_name);
        Log::error("%s", error_bw_buffer.c_str());
        this->t_state.client_connection_allowed = false;
      }
    } else if (strncasecmp(host_name.data(), sni_value, host_len) != 0) { // Name mismatch
      Warning("SNI/hostname mismatch sni=%s host=%.*s action=%s", sni_value, host_len, host_name.data(), action_value);
      SMDbg(dbg_ctl_ssl_sni, "SNI/hostname mismatch sni=%s host=%.*s action=%s", sni_value, host_len, host_name.data(),
            action_value);
      if (host_sni_policy == 2) {
        swoc::bwprint(error_bw_buffer, "SNI/hostname mismatch: connecting to {} for host='{}' sni='{}', returning a 403",
                      t_state.client_info.dst_addr, host_name, sni_value);
        Log::error("%s", error_bw_buffer.c_str());
        this->t_state.client_connection_allowed = false;
      }
    } else {
      SMDbg(dbg_ctl_ssl_sni, "SNI/hostname successfully match sni=%s host=%.*s", sni_value, host_len, host_name.data());
    }
  } else {
    SMDbg(dbg_ctl_ssl_sni, "No SNI/hostname check configured for host=%.*s", host_len, host_name.data());
  }
}

void
HttpSM::do_remap_request(bool run_inline)
{
  SMDbg(dbg_ctl_http_seq, "Remapping request");
  SMDbg(dbg_ctl_url_rewrite, "Starting a possible remapping for request");
  bool ret = remapProcessor.setup_for_remap(&t_state, m_remap);

  check_sni_host();

  // Depending on a variety of factors the HOST field may or may not have been promoted to the
  // client request URL. The unmapped URL should always have that promotion done. If the HOST field
  // is not already there, promote it only in the unmapped_url. This avoids breaking any logic that
  // depends on the lack of promotion in the client request URL.
  if (!t_state.unmapped_url.m_url_impl->m_ptr_host) {
    MIMEField *host_field = t_state.hdr_info.client_request.field_find(static_cast<std::string_view>(MIME_FIELD_HOST));
    if (host_field) {
      auto host_name{host_field->value_get()};
      if (!host_name.empty()) {
        int port = -1;
        // Host header can contain port number, and if it does we need to set host and port separately to unmapped_url.
        // If header value starts with '[', the value must contain an IPv6 address, and it may contain a port number as well.
        if (host_name.starts_with("["sv)) { // IPv6
          host_name.remove_prefix(1);       // Skip '['
          // If header value ends with ']', the value must only contain an IPv6 address (no port number).
          if (host_name.ends_with("]"sv)) { // Without port number
            host_name.remove_suffix(1);     // Exclude ']'
          } else {                          // With port number
            for (int idx = host_name.length() - 1; idx > 0; idx--) {
              if (host_name[idx] == ':') {
                port      = ink_atoi(host_name.data() + idx + 1, host_name.length() - (idx + 1));
                host_name = host_name.substr(0, idx);
                break;
              }
            }
          }
        } else { // Anything else (Hostname or IPv4 address)
          // If the value contains ':' where it does not have IPv6 address, there must be port number
          if (const char *colon = static_cast<const char *>(memchr(host_name.data(), ':', host_name.length()));
              colon == nullptr) { // Without port number
            // Nothing to adjust. Entire value should be used as hostname.
          } else { // With port number
            port      = ink_atoi(colon + 1, host_name.length() - ((colon + 1) - host_name.data()));
            host_name = host_name.substr(0, colon - host_name.data());
          }
        }

        // Set values
        t_state.unmapped_url.host_set(host_name);
        if (port >= 0) {
          t_state.unmapped_url.port_set(port);
        }
      }
    }
  }

  if (!ret) {
    SMDbg(dbg_ctl_url_rewrite, "Could not find a valid remapping entry for this request");
    Metrics::Counter::increment(http_rsb.no_remap_matched);
    if (!run_inline) {
      handleEvent(EVENT_REMAP_COMPLETE, nullptr);
    }
    return;
  }

  SMDbg(dbg_ctl_url_rewrite, "Found a remap map entry, attempting to remap request and call any plugins");
  pending_action = remapProcessor.perform_remap(this, &t_state);

  return;
}

void
HttpSM::do_hostdb_lookup()
{
  ink_assert(t_state.dns_info.lookup_name != nullptr);
  ink_assert(pending_action.empty());

  ATS_PROBE1(milestone_dns_lookup_begin, sm_id);
  milestones[TS_MILESTONE_DNS_LOOKUP_BEGIN] = ink_get_hrtime();

  // If directed to not look up fqdns then mark as resolved
  if (t_state.txn_conf->no_dns_forward_to_parent && t_state.parent_result.result == ParentResultType::UNDEFINED) {
    t_state.dns_info.resolved_p = true;
    call_transact_and_set_next_state(nullptr);
    return;
  } else if (t_state.txn_conf->srv_enabled) {
    char d[MAXDNAME];

    // Look at the next_hop_scheme to determine what scheme to put in the SRV lookup
    unsigned int scheme_len = snprintf(d, sizeof(d), "_%s._tcp.", hdrtoken_index_to_wks(t_state.next_hop_scheme));
    ink_strlcpy(d + scheme_len, t_state.server_info.name, sizeof(d) - scheme_len);

    SMDbg(dbg_ctl_dns_srv, "Beginning lookup of SRV records for origin %s", d);

    HostDBProcessor::Options opt;
    if (t_state.api_txn_dns_timeout_value != -1) {
      opt.timeout = t_state.api_txn_dns_timeout_value;
    }
    pending_action = hostDBProcessor.getSRVbyname_imm(this, (cb_process_result_pfn)&HttpSM::process_srv_info, d, 0, opt);
    if (pending_action.empty()) {
      char const *host_name = t_state.dns_info.is_srv() ? t_state.dns_info.srv_hostname : t_state.dns_info.lookup_name;
      opt.port              = t_state.dns_info.is_srv()              ? t_state.dns_info.srv_port :
                              t_state.server_info.dst_addr.isValid() ? t_state.server_info.dst_addr.host_order_port() :
                                                                       t_state.hdr_info.client_request.port_get();
      opt.flags   = (t_state.cache_info.directives.does_client_permit_dns_storing) ? HostDBProcessor::HOSTDB_DO_NOT_FORCE_DNS :
                                                                                     HostDBProcessor::HOSTDB_FORCE_DNS_RELOAD;
      opt.timeout = (t_state.api_txn_dns_timeout_value != -1) ? t_state.api_txn_dns_timeout_value : 0;
      opt.host_res_style =
        ats_host_res_from(_ua.get_txn()->get_netvc()->get_local_addr()->sa_family, t_state.txn_conf->host_res_data.order);

      pending_action = hostDBProcessor.getbyname_imm(this, (cb_process_result_pfn)&HttpSM::process_hostdb_info, host_name, 0, opt);
      if (pending_action.empty()) {
        call_transact_and_set_next_state(nullptr);
      }
    }
    return;
  } else { /* we aren't using SRV stuff... */
    SMDbg(dbg_ctl_http_seq, "Doing DNS Lookup");

    // If there is not a current server, we must be looking up the origin
    //  server at the beginning of the transaction
    int server_port = 0;
    if (t_state.current.server && t_state.current.server->dst_addr.isValid()) {
      server_port = t_state.current.server->dst_addr.host_order_port();
    } else if (t_state.server_info.dst_addr.isValid()) {
      server_port = t_state.server_info.dst_addr.host_order_port();
    } else {
      server_port = t_state.hdr_info.client_request.port_get();
    }

    if (t_state.api_txn_dns_timeout_value != -1) {
      SMDbg(dbg_ctl_http_timeout, "beginning DNS lookup. allowing %d mseconds for DNS lookup", t_state.api_txn_dns_timeout_value);
    }

    HostDBProcessor::Options opt;
    opt.port    = server_port;
    opt.flags   = (t_state.cache_info.directives.does_client_permit_dns_storing) ? HostDBProcessor::HOSTDB_DO_NOT_FORCE_DNS :
                                                                                   HostDBProcessor::HOSTDB_FORCE_DNS_RELOAD;
    opt.timeout = (t_state.api_txn_dns_timeout_value != -1) ? t_state.api_txn_dns_timeout_value : 0;

    opt.host_res_style =
      ats_host_res_from(_ua.get_txn()->get_netvc()->get_local_addr()->sa_family, t_state.txn_conf->host_res_data.order);

    pending_action = hostDBProcessor.getbyname_imm(this, (cb_process_result_pfn)&HttpSM::process_hostdb_info,
                                                   t_state.dns_info.lookup_name, 0, opt);
    if (pending_action.empty()) {
      call_transact_and_set_next_state(nullptr);
    }
    return;
  }
  ink_assert(!"not reached");
  return;
}

void
HttpSM::do_hostdb_reverse_lookup()
{
  ink_assert(t_state.dns_info.lookup_name != nullptr);
  ink_assert(pending_action.empty());

  SMDbg(dbg_ctl_http_seq, "Doing reverse DNS Lookup");

  IpEndpoint addr;
  ats_ip_pton(t_state.dns_info.lookup_name, &addr.sa);
  pending_action = hostDBProcessor.getbyaddr_re(this, &addr.sa);

  return;
}

bool
HttpSM::track_connect_fail() const
{
  bool retval = false;
  if (t_state.current.server->had_connect_fail()) {
    // What does our policy say?
    if (t_state.txn_conf->connect_down_policy == 2) { // Any connection error through TLS handshake
      retval = true;
    } else if (t_state.txn_conf->connect_down_policy == 1) { // Any connection error through TCP
      retval = t_state.current.server->connect_result != -ENET_SSL_CONNECT_FAILED;
    }
  }
  return retval;
}

void
HttpSM::do_hostdb_update_if_necessary()
{
  if (t_state.current.server == nullptr || plugin_tunnel_type != HttpPluginTunnel_t::NONE || t_state.dns_info.active == nullptr) {
    // No server, so update is not necessary
    return;
  }

  if (t_state.updated_server_version != HTTP_INVALID) {
    // we may have incorrectly assumed that the hostdb had the wrong version of
    // http for the server because our first few connect attempts to the server
    // failed, causing us to downgrade our requests to a lower version and changing
    // our information about the server version.
    //
    // This test therefore just issues the update only if the hostdb version is
    // in fact different from the version we want the value to be updated to.
    t_state.updated_server_version        = HTTP_INVALID;
    t_state.dns_info.active->http_version = t_state.updated_server_version;
  }

  // Check to see if we need to report or clear a connection failure
  if (track_connect_fail()) {
    this->mark_host_failure(&t_state.dns_info, ts_clock::from_time_t(t_state.client_request_time));
  } else {
    if (t_state.dns_info.mark_active_server_alive()) {
      char addrbuf[INET6_ADDRPORTSTRLEN];
      ats_ip_nptop(&t_state.current.server->dst_addr.sa, addrbuf, sizeof(addrbuf));
      ATS_PROBE2(mark_active_server_alive, sm_id, addrbuf);
      if (t_state.dns_info.record->is_srv()) {
        SMDbg(dbg_ctl_http, "[%" PRId64 "] hostdb update marking SRV: %s(%s) as up", sm_id, t_state.dns_info.record->name(),
              addrbuf);
      } else {
        SMDbg(dbg_ctl_http, "[%" PRId64 "] hostdb update marking IP: %s as up", sm_id, addrbuf);
      }
    }
  }

  char addrbuf[INET6_ADDRPORTSTRLEN];
  SMDbg(dbg_ctl_http, "server info = %s", ats_ip_nptop(&t_state.current.server->dst_addr.sa, addrbuf, sizeof(addrbuf)));
  return;
}

/*
 * range entry valid [a,b] (a >= 0 and b >= 0 and a <= b)
 * HttpTransact::RangeSetup_t::NONE if the content length of cached copy is zero or
 * no range entry
 * HttpTransact::RangeSetup_t::NOT_SATISFIABLE iff all range entries are valid but
 * none overlap the current extent of the cached copy
 * HttpTransact::RangeSetup_t::NOT_HANDLED if out-of-order Range entries or
 * the cached copy`s content_length is INT64_MAX (e.g. read_from_writer and trunked)
 * HttpTransact::RangeSetup_t::REQUESTED if all sub range entries are valid and
 * in order (remove the entries that not overlap the extent of cache copy)
 */
void
HttpSM::parse_range_and_compare(MIMEField *field, int64_t content_length)
{
  int          prev_good_range = -1;
  const char  *value;
  int          value_len;
  int          n_values;
  int          nr          = 0; // number of valid ranges, also index to range array.
  int          not_satisfy = 0;
  HdrCsvIter   csv;
  const char  *s, *e, *tmp;
  RangeRecord *ranges = nullptr;
  int64_t      start, end;

  ink_assert(field != nullptr && t_state.range_setup == HttpTransact::RangeSetup_t::NONE && t_state.ranges == nullptr);

  if (content_length <= 0) {
    return;
  }

  // ToDo: Can this really happen?
  if (content_length == INT64_MAX) {
    t_state.range_setup = HttpTransact::RangeSetup_t::NOT_HANDLED;
    return;
  }

  if (parse_range_done) {
    SMDbg(dbg_ctl_http_range, "parse_range already done, t_state.range_setup %d", static_cast<int>(t_state.range_setup));
    return;
  }
  parse_range_done = true;

  n_values = 0;
  value    = csv.get_first(field, &value_len);
  while (value) {
    ++n_values;
    value = csv.get_next(&value_len);
  }

  value = csv.get_first(field, &value_len);
  if (n_values <= 0 || ptr_len_ncmp(value, value_len, "bytes=", 6)) {
    return;
  }

  ranges     = new RangeRecord[n_values];
  value     += 6; // skip leading 'bytes='
  value_len -= 6;

  // assume range_in_cache
  t_state.range_in_cache = true;

  for (; value; value = csv.get_next(&value_len)) {
    if (!(tmp = static_cast<const char *>(memchr(value, '-', value_len)))) {
      t_state.range_setup = HttpTransact::RangeSetup_t::NONE;
      goto Lfaild;
    }

    // process start value
    s = value;
    e = tmp;
    // skip leading white spaces
    for (; s < e && ParseRules::is_ws(*s); ++s) {
      ;
    }

    if (s >= e) {
      start = -1;
    } else {
      for (start = 0; s < e && *s >= '0' && *s <= '9'; ++s) {
        // check the int64 overflow in case of high gcc with O3 option
        // thinking the start is always positive
        int64_t new_start = start * 10 + (*s - '0');

        if (new_start < start) { // Overflow
          t_state.range_setup = HttpTransact::RangeSetup_t::NONE;
          goto Lfaild;
        }
        start = new_start;
      }
      // skip last white spaces
      for (; s < e && ParseRules::is_ws(*s); ++s) {
        ;
      }

      if (s < e) {
        t_state.range_setup = HttpTransact::RangeSetup_t::NONE;
        goto Lfaild;
      }
    }

    // process end value
    s = tmp + 1;
    e = value + value_len;
    // skip leading white spaces
    for (; s < e && ParseRules::is_ws(*s); ++s) {
      ;
    }

    if (s >= e) {
      if (start < 0) {
        t_state.range_setup = HttpTransact::RangeSetup_t::NONE;
        goto Lfaild;
      } else if (start >= content_length) {
        not_satisfy++;
        continue;
      }
      end = content_length - 1;
    } else {
      for (end = 0; s < e && *s >= '0' && *s <= '9'; ++s) {
        // check the int64 overflow in case of high gcc with O3 option
        // thinking the start is always positive
        int64_t new_end = end * 10 + (*s - '0');

        if (new_end < end) { // Overflow
          t_state.range_setup = HttpTransact::RangeSetup_t::NONE;
          goto Lfaild;
        }
        end = new_end;
      }
      // skip last white spaces
      for (; s < e && ParseRules::is_ws(*s); ++s) {
        ;
      }

      if (s < e) {
        t_state.range_setup = HttpTransact::RangeSetup_t::NONE;
        goto Lfaild;
      }

      if (start < 0) {
        if (end >= content_length) {
          end = content_length;
        }
        start = content_length - end;
        end   = content_length - 1;
      } else if (start >= content_length && start <= end) {
        not_satisfy++;
        continue;
      }

      if (end >= content_length) {
        end = content_length - 1;
      }
    }

    if (start > end) {
      t_state.range_setup = HttpTransact::RangeSetup_t::NONE;
      goto Lfaild;
    }

    if (prev_good_range >= 0 && start <= ranges[prev_good_range]._end) {
      t_state.range_setup = HttpTransact::RangeSetup_t::NOT_HANDLED;
      goto Lfaild;
    }

    ink_assert(start >= 0 && end >= 0 && start < content_length && end < content_length);

    prev_good_range   = nr;
    ranges[nr]._start = start;
    ranges[nr]._end   = end;
    ++nr;

    if (cache_sm.cache_read_vc && t_state.cache_info.object_read) {
      if (!cache_sm.cache_read_vc->is_pread_capable() && cache_config_read_while_writer == 2) {
        // write in progress, check if request range not in cache yet
        HTTPInfo::FragOffset *frag_offset_tbl = t_state.cache_info.object_read->get_frag_table();
        int                   frag_offset_cnt = t_state.cache_info.object_read->get_frag_offset_count();

        if (!frag_offset_tbl || !frag_offset_cnt || (frag_offset_tbl[frag_offset_cnt - 1] < static_cast<uint64_t>(end))) {
          SMDbg(dbg_ctl_http_range, "request range in cache, end %" PRId64 ", frg_offset_cnt %d" PRId64, end, frag_offset_cnt);
          t_state.range_in_cache = false;
        }
      }
    } else {
      t_state.range_in_cache = false;
    }
  }

  if (nr > 0) {
    t_state.range_setup      = HttpTransact::RangeSetup_t::REQUESTED;
    t_state.ranges           = ranges;
    t_state.num_range_fields = nr;
    return;
  }

  if (not_satisfy) {
    t_state.range_setup = HttpTransact::RangeSetup_t::NOT_SATISFIABLE;
  }

Lfaild:
  t_state.range_in_cache   = false;
  t_state.num_range_fields = -1;
  delete[] ranges;
  return;
}

void
HttpSM::calculate_output_cl(int64_t num_chars_for_ct, int64_t num_chars_for_cl)
{
  if (t_state.range_setup != HttpTransact::RangeSetup_t::REQUESTED &&
      t_state.range_setup != HttpTransact::RangeSetup_t::NOT_TRANSFORM_REQUESTED) {
    return;
  }

  ink_assert(t_state.ranges);

  if (t_state.num_range_fields == 1) {
    t_state.range_output_cl = t_state.ranges[0]._end - t_state.ranges[0]._start + 1;
  } else {
    for (int i = 0; i < t_state.num_range_fields; i++) {
      if (t_state.ranges[i]._start >= 0) {
        t_state.range_output_cl += boundary_size;
        t_state.range_output_cl += sub_header_size + num_chars_for_ct;
        t_state.range_output_cl +=
          num_chars_for_int(t_state.ranges[i]._start) + num_chars_for_int(t_state.ranges[i]._end) + num_chars_for_cl + 2;
        t_state.range_output_cl += t_state.ranges[i]._end - t_state.ranges[i]._start + 1;
        t_state.range_output_cl += 2;
      }
    }

    t_state.range_output_cl += boundary_size + 2;
  }

  SMDbg(dbg_ctl_http_range, "Pre-calculated Content-Length for Range response is %" PRId64, t_state.range_output_cl);
}

void
HttpSM::do_range_parse(MIMEField *range_field)
{
  std::string_view content_type{};
  int64_t          content_length = 0;

  if (t_state.cache_info.object_read != nullptr) {
    content_type =
      t_state.cache_info.object_read->response_get()->value_get(static_cast<std::string_view>(MIME_FIELD_CONTENT_TYPE));
    content_length = t_state.cache_info.object_read->object_size_get();
  } else {
    content_length = t_state.hdr_info.server_response.get_content_length();
    content_type   = t_state.hdr_info.server_response.value_get(static_cast<std::string_view>(MIME_FIELD_CONTENT_TYPE));
  }
  int64_t num_chars_for_cl = num_chars_for_int(content_length);

  parse_range_and_compare(range_field, content_length);
  calculate_output_cl(content_type.length(), num_chars_for_cl);
}

// this function looks for any Range: headers, parses them and either
// sets up a transform processor to handle the request OR defers to the
// HttpTunnel
void
HttpSM::do_range_setup_if_necessary()
{
  MIMEField *field;

  field = t_state.hdr_info.client_request.field_find(static_cast<std::string_view>(MIME_FIELD_RANGE));
  ink_assert(field != nullptr);

  t_state.range_setup = HttpTransact::RangeSetup_t::NONE;

  if (t_state.method == HTTP_WKSIDX_GET && t_state.hdr_info.client_request.version_get() == HTTP_1_1) {
    do_range_parse(field);

    if (t_state.range_setup == HttpTransact::RangeSetup_t::REQUESTED) {
      bool do_transform = false;

      if (!t_state.range_in_cache && t_state.cache_info.object_read) {
        SMDbg(dbg_ctl_http_range, "range can't be satisfied from cache, force origin request");
        t_state.cache_lookup_result = HttpTransact::CacheLookupResult_t::MISS;
        return;
      }

      if (t_state.num_range_fields > 1) {
        if (0 == t_state.txn_conf->allow_multi_range) {
          t_state.range_setup = HttpTransact::RangeSetup_t::NONE; // No Range required (not allowed)
          t_state.hdr_info.client_request.field_delete(
            static_cast<std::string_view>(MIME_FIELD_RANGE)); // ... and nuke the Range header too
          t_state.num_range_fields = 0;
        } else if (1 == t_state.txn_conf->allow_multi_range) {
          do_transform = true;
        } else {
          t_state.num_range_fields = 0;
          t_state.range_setup      = HttpTransact::RangeSetup_t::NOT_SATISFIABLE;
        }
      } else {
        // if revalidating and cache is stale we want to transform
        if (t_state.cache_info.action == HttpTransact::CacheAction_t::REPLACE) {
          if (t_state.hdr_info.server_response.status_get() == HTTPStatus::OK) {
            Dbg(dbg_ctl_http_range, "Serving transform after stale cache re-serve");
            do_transform = true;
          } else {
            Dbg(dbg_ctl_http_range, "Not transforming after revalidate");
          }
        } else if (cache_sm.cache_read_vc && cache_sm.cache_read_vc->is_pread_capable()) {
          // If only one range entry and pread is capable, no need transform range
          t_state.range_setup = HttpTransact::RangeSetup_t::NOT_TRANSFORM_REQUESTED;
        } else {
          do_transform = true;
        }
      }

      // We have to do the transform on (allowed) multi-range request, *or* if the VC is not pread capable
      if (do_transform) {
        if (api_hooks.get(TS_HTTP_RESPONSE_TRANSFORM_HOOK) == nullptr) {
          std::string_view content_type{};
          int64_t          content_length = 0;

          if (t_state.cache_info.object_read && t_state.cache_info.action != HttpTransact::CacheAction_t::REPLACE) {
            content_type =
              t_state.cache_info.object_read->response_get()->value_get(static_cast<std::string_view>(MIME_FIELD_CONTENT_TYPE));
            content_length = t_state.cache_info.object_read->object_size_get();
          } else {
            // We don't want to transform a range request if the server response has a content encoding.
            if (t_state.hdr_info.server_response.presence(MIME_PRESENCE_CONTENT_ENCODING)) {
              Dbg(dbg_ctl_http_trans, "Cannot setup range transform for server response with content encoding");
              t_state.range_setup = HttpTransact::RangeSetup_t::NONE;
              return;
            }

            // Since we are transforming the range from the server, we want to cache the original response
            t_state.api_info.cache_untransformed = true;
            content_type   = t_state.hdr_info.server_response.value_get(static_cast<std::string_view>(MIME_FIELD_CONTENT_TYPE));
            content_length = t_state.hdr_info.server_response.get_content_length();
          }

          SMDbg(dbg_ctl_http_trans, "Unable to accelerate range request, fallback to transform");

          // create a Range: transform processor for requests of type Range: bytes=1-2,4-5,10-100 (eg. multiple ranges)
          INKVConnInternal *range_trans = transformProcessor.range_transform(
            mutex.get(), t_state.ranges, t_state.num_range_fields, &t_state.hdr_info.transform_response, content_type.data(),
            static_cast<int>(content_type.length()), content_length);
          api_hooks.append(TS_HTTP_RESPONSE_TRANSFORM_HOOK, range_trans);
        } else {
          // ToDo: Do we do something here? The theory is that multiple transforms do not behave well with
          // the range transform needed here.
        }
      }
    }
  }
}

void
HttpSM::do_cache_lookup_and_read()
{
  // TODO decide whether to uncomment after finish testing redirect
  // ink_assert(server_txn == NULL);
  ink_assert(pending_action.empty());

  t_state.request_sent_time      = UNDEFINED_TIME;
  t_state.response_received_time = UNDEFINED_TIME;

  Metrics::Counter::increment(http_rsb.cache_lookups);

  ATS_PROBE1(milestone_cache_open_read_begin, sm_id);
  milestones[TS_MILESTONE_CACHE_OPEN_READ_BEGIN] = ink_get_hrtime();
  t_state.cache_lookup_result                    = HttpTransact::CacheLookupResult_t::NONE;
  t_state.cache_info.lookup_count++;
  // YTS Team, yamsat Plugin
  // Changed the lookup_url to c_url which enables even
  // the new redirect url to perform a CACHE_LOOKUP
  URL *c_url;
  if (t_state.redirect_info.redirect_in_process && !t_state.txn_conf->redirect_use_orig_cache_key) {
    c_url = t_state.hdr_info.client_request.url_get();
  } else {
    c_url = t_state.cache_info.lookup_url;
  }

  SMDbg(dbg_ctl_http_seq, "Issuing cache lookup for URL %s", c_url->string_get(&t_state.arena));

  HttpCacheKey key;
  Cache::generate_key(&key, c_url, t_state.txn_conf->cache_ignore_query, t_state.txn_conf->cache_generation_number);

  t_state.hdr_info.cache_request.copy(&t_state.hdr_info.client_request);
  HttpTransactHeaders::normalize_accept_encoding(t_state.txn_conf, &t_state.hdr_info.cache_request);
  pending_action = cache_sm.open_read(
    &key, c_url, &t_state.hdr_info.cache_request, t_state.txn_conf,
    static_cast<time_t>((t_state.cache_control.pin_in_cache_for < 0) ? 0 : t_state.cache_control.pin_in_cache_for));
  //
  // pin_in_cache value is an open_write parameter.
  // It is passed in open_read to allow the cluster to
  // optimize the typical open_read/open_read failed/open_write
  // sequence.
  //
  REMEMBER((long)pending_action.get(), reentrancy_count);

  return;
}

void
HttpSM::do_cache_delete_all_alts(Continuation *cont)
{
  // Do not delete a non-existent object.
  ink_assert(t_state.cache_info.object_read);

  SMDbg(dbg_ctl_http_seq, "Issuing cache delete for %s", t_state.cache_info.lookup_url->string_get_ref());

  HttpCacheKey key;
  Cache::generate_key(&key, t_state.cache_info.lookup_url, t_state.txn_conf->cache_ignore_query,
                      t_state.txn_conf->cache_generation_number);
  pending_action = cacheProcessor.remove(cont, &key);

  return;
}

inline void
HttpSM::do_cache_prepare_write()
{
  ATS_PROBE1(milestone_cache_open_write_begin, sm_id);
  milestones[TS_MILESTONE_CACHE_OPEN_WRITE_BEGIN] = ink_get_hrtime();
  do_cache_prepare_action(&cache_sm, t_state.cache_info.object_read, true);
}

inline void
HttpSM::do_cache_prepare_write_transform()
{
  if (cache_sm.cache_write_vc != nullptr || tunnel.has_cache_writer()) {
    do_cache_prepare_action(&transform_cache_sm, nullptr, false, true);
  } else {
    do_cache_prepare_action(&transform_cache_sm, nullptr, false);
  }
}

void
HttpSM::do_cache_prepare_update()
{
  if (t_state.cache_info.object_read != nullptr && t_state.cache_info.object_read->valid() &&
      t_state.cache_info.object_store.valid() && t_state.cache_info.object_store.response_get() != nullptr &&
      t_state.cache_info.object_store.response_get()->valid() &&
      t_state.hdr_info.client_request.method_get_wksidx() == HTTP_WKSIDX_GET) {
    t_state.cache_info.object_store.request_set(t_state.cache_info.object_read->request_get());
    // t_state.cache_info.object_read = NULL;
    // cache_sm.close_read();

    t_state.transact_return_point = HttpTransact::HandleUpdateCachedObject;
    ink_assert(cache_sm.cache_write_vc == nullptr);
    HTTP_SM_SET_DEFAULT_HANDLER(&HttpSM::state_cache_open_write);
    // don't retry read for update
    do_cache_prepare_action(&cache_sm, t_state.cache_info.object_read, false);
  } else {
    t_state.api_modifiable_cached_resp = false;
    call_transact_and_set_next_state(HttpTransact::HandleApiErrorJump);
  }
}

void
HttpSM::do_cache_prepare_action(HttpCacheSM *c_sm, CacheHTTPInfo *object_read_info, bool retry, bool allow_multiple)
{
  URL *o_url, *s_url;
  bool restore_client_request = false;

  ink_assert(pending_action.empty());

  if (t_state.redirect_info.redirect_in_process) {
    o_url = &(t_state.redirect_info.original_url);
    ink_assert(o_url->valid());
    restore_client_request = true;
    s_url                  = o_url;
  } else {
    o_url = &(t_state.cache_info.original_url);
    if (o_url->valid()) {
      s_url = o_url;
    } else {
      s_url = t_state.cache_info.lookup_url;
    }
  }

  // modify client request to make it have the url we are going to
  // store into the cache
  if (restore_client_request) {
    URL *c_url = t_state.hdr_info.client_request.url_get();
    s_url->copy(c_url);
  }

  ink_assert(s_url != nullptr && s_url->valid());
  SMDbg(dbg_ctl_http_cache_write, "writing to cache with URL %s", s_url->string_get(&t_state.arena));

  HttpCacheKey key;
  Cache::generate_key(&key, s_url, t_state.txn_conf->cache_ignore_query, t_state.txn_conf->cache_generation_number);

  pending_action =
    c_sm->open_write(&key, s_url, &t_state.hdr_info.cache_request, object_read_info,
                     static_cast<time_t>((t_state.cache_control.pin_in_cache_for < 0) ? 0 : t_state.cache_control.pin_in_cache_for),
                     retry, allow_multiple);
}

void
HttpSM::send_origin_throttled_response()
{
  // if the request is to a parent proxy, do not reset
  // t_state.current.retry_attempts so that another parent or
  // NextHop may be tried.
  if (t_state.dns_info.looking_up != ResolveInfo::PARENT_PROXY) {
    t_state.current.retry_attempts.maximize(t_state.configured_connect_attempts_max_retries());
  }
  t_state.current.state = HttpTransact::OUTBOUND_CONGESTION;
  call_transact_and_set_next_state(HttpTransact::HandleResponse);
}

static void
set_tls_options(NetVCOptions &opt, const OverridableHttpConfigParams *txn_conf)
{
  char *verify_server = nullptr;
  if (txn_conf->ssl_client_verify_server_policy == nullptr) {
    opt.verifyServerPolicy = YamlSNIConfig::Policy::UNSET;
  } else {
    verify_server = txn_conf->ssl_client_verify_server_policy;
    if (strcmp(verify_server, "DISABLED") == 0) {
      opt.verifyServerPolicy = YamlSNIConfig::Policy::DISABLED;
    } else if (strcmp(verify_server, "PERMISSIVE") == 0) {
      opt.verifyServerPolicy = YamlSNIConfig::Policy::PERMISSIVE;
    } else if (strcmp(verify_server, "ENFORCED") == 0) {
      opt.verifyServerPolicy = YamlSNIConfig::Policy::ENFORCED;
    } else {
      Warning("%s is invalid for proxy.config.ssl.client.verify.server.policy.  Should be one of DISABLED, PERMISSIVE, or ENFORCED",
              verify_server);
      opt.verifyServerPolicy = YamlSNIConfig::Policy::UNSET;
    }
  }
  if (txn_conf->ssl_client_verify_server_properties == nullptr) {
    opt.verifyServerProperties = YamlSNIConfig::Property::UNSET;
  } else {
    verify_server = txn_conf->ssl_client_verify_server_properties;
    if (strcmp(verify_server, "SIGNATURE") == 0) {
      opt.verifyServerProperties = YamlSNIConfig::Property::SIGNATURE_MASK;
    } else if (strcmp(verify_server, "NAME") == 0) {
      opt.verifyServerProperties = YamlSNIConfig::Property::NAME_MASK;
    } else if (strcmp(verify_server, "ALL") == 0) {
      opt.verifyServerProperties = YamlSNIConfig::Property::ALL_MASK;
    } else if (strcmp(verify_server, "NONE") == 0) {
      opt.verifyServerProperties = YamlSNIConfig::Property::NONE;
    } else {
      Warning("%s is invalid for proxy.config.ssl.client.verify.server.properties.  Should be one of SIGNATURE, NAME, or ALL",
              verify_server);
      opt.verifyServerProperties = YamlSNIConfig::Property::NONE;
    }
  }
}

std::string_view
HttpSM::get_outbound_cert() const
{
  const char *cert_name = t_state.txn_conf->ssl_client_cert_filename;
  if (cert_name == nullptr) {
    cert_name = "";
  }
  return std::string_view(cert_name);
}

std::string_view
HttpSM::get_outbound_sni() const
{
  using namespace swoc::literals;
  swoc::TextView zret;
  swoc::TextView policy{t_state.txn_conf->ssl_client_sni_policy, swoc::TextView::npos};

  TLSSNISupport *snis = nullptr;
  if (_ua.get_txn()) {
    if (auto *netvc = _ua.get_txn()->get_netvc(); netvc) {
      snis = netvc->get_service<TLSSNISupport>();
      if (snis && snis->hints_from_sni.outbound_sni_policy.has_value()) {
        policy.assign(snis->hints_from_sni.outbound_sni_policy->data(), swoc::TextView::npos);
      }
    }
  }

  if (policy.empty() || policy == "host"_tv) {
    // By default the host header field value is used for the SNI.
    zret = t_state.hdr_info.server_request.host_get();
  } else if (_ua.get_txn() && policy == "server_name"_tv) {
    const char *server_name = snis->get_sni_server_name();
    if (server_name[0] == '\0') {
      zret.assign(nullptr, swoc::TextView::npos);
    } else {
      zret.assign(snis->get_sni_server_name(), swoc::TextView::npos);
    }
  } else if (policy.front() == '@') { // guaranteed non-empty from previous clause
    zret = policy.remove_prefix(1);
  } else {
    // If other is specified, like "remap" and "verify_with_name_source", the remapped origin name is used for the SNI value
    zret.assign(t_state.server_info.name, swoc::TextView::npos);
  }
  return zret;
}

bool
HttpSM::apply_ip_allow_filter()
{
  // Method allowed on dest IP address check
  IpAllow::ACL acl = IpAllow::match(this->get_server_remote_addr(), IpAllow::match_key_t::DST_ADDR);

  if (ip_allow_is_request_forbidden(acl)) {
    ip_allow_deny_request(acl);
    return false;
  }
  return true;
}

bool
HttpSM::ip_allow_is_request_forbidden(const IpAllow::ACL &acl)
{
  bool result{false};
  if (acl.isValid()) {
    if (acl.isDenyAll()) {
      result = true;
    } else if (!acl.isAllowAll()) {
      if (this->get_request_method_wksidx() != -1) {
        result = !acl.isMethodAllowed(this->get_request_method_wksidx());
      } else {
        auto method{t_state.hdr_info.server_request.method_get()};
        result = !acl.isNonstandardMethodAllowed(method);
      }
    }
  }

  return result;
}

void
HttpSM::ip_allow_deny_request(const IpAllow::ACL &acl)
{
  if (dbg_ctl_ip_allow.on()) {
    ip_text_buffer ipb;
    auto           method{t_state.hdr_info.client_request.method_get()};

    const char *ntop_formatted = ats_ip_ntop(this->get_server_remote_addr(), ipb, sizeof(ipb));
    Warning("server '%s' prohibited by ip-allow policy at line %d", ntop_formatted, acl.source_line());
    SMDbg(dbg_ctl_ip_allow, "Line %d denial for '%.*s' from %s", acl.source_line(), static_cast<int>(method.length()),
          method.data(), ntop_formatted);
  }

  t_state.current.retry_attempts.maximize(
    t_state.configured_connect_attempts_max_retries()); // prevent any more retries with this IP
  call_transact_and_set_next_state(HttpTransact::Forbidden);
}

bool
HttpSM::grab_pre_warmed_net_v_connection_if_possible(const TLSTunnelSupport &tts, int pid)
{
  bool result{false};

  if (is_prewarm_enabled_or_sni_overridden(tts)) {
    EThread *ethread = this_ethread();
    _prewarm_sm      = ethread->prewarm_queue->dequeue(tts.create_dst(pid));

    if (_prewarm_sm != nullptr) {
      open_prewarmed_connection();
      result = true;
    } else {
      SMDbg(dbg_ctl_http_ss, "no pre-warmed tunnel");
    }
  }

  return result;
}

bool
HttpSM::is_prewarm_enabled_or_sni_overridden(const TLSTunnelSupport &tts) const
{
  PreWarmConfig::scoped_config prewarm_conf;
  bool                         result = prewarm_conf->enabled;

  if (YamlSNIConfig::TunnelPreWarm sni_use_prewarm = tts.get_tunnel_prewarm_configuration();
      sni_use_prewarm != YamlSNIConfig::TunnelPreWarm::UNSET) {
    result = static_cast<bool>(sni_use_prewarm);
  }

  return result;
}

void
HttpSM::open_prewarmed_connection()
{
  NetVConnection *netvc = _prewarm_sm->move_netvc();
  ink_release_assert(_prewarm_sm->handler == &PreWarmSM::state_closed);

  SMDbg(dbg_ctl_http_ss, "using pre-warmed tunnel netvc=%p", netvc);

  t_state.current.retry_attempts.clear();

  ink_release_assert(default_handler == HttpSM::default_handler);
  handleEvent(NET_EVENT_OPEN, netvc);
}

//////////////////////////////////////////////////////////////////////////
//
//  HttpSM::do_http_server_open()
//
//////////////////////////////////////////////////////////////////////////
void
HttpSM::do_http_server_open(bool raw, bool only_direct)
{
  int  ip_family = t_state.current.server->dst_addr.sa.sa_family;
  auto fam_name  = ats_ip_family_name(ip_family);
  SMDbg(dbg_ctl_http_track, "[%.*s]", static_cast<int>(fam_name.size()), fam_name.data());

  NetVConnection *vc = _ua.get_txn()->get_netvc();
  ink_release_assert(vc && vc->thread == this_ethread());
  pending_action = nullptr;

  // Clean up connection tracking info if any. Need to do it now so the selected group
  // is consistent with the actual upstream in case of retry.
  t_state.outbound_conn_track_state.clear();

  // Make sure any previous attempts are cleaned out
  if (server_txn) {
    tunnel.reset();
    server_txn->transaction_done();
    server_txn = nullptr;
  }

  // _ua.get_entry() can be null if a scheduled update is also a reverse proxy
  // request. Added REVPROXY to the assert below, and then changed checks
  // to be based on _ua.get_txn() != NULL instead of req_flavor value.
  ink_assert(_ua.get_entry() != nullptr || t_state.req_flavor == HttpTransact::HttpRequestFlavor_t::SCHEDULED_UPDATE ||
             t_state.req_flavor == HttpTransact::HttpRequestFlavor_t::REVPROXY);

  ink_assert(pending_action.empty());
  ink_assert(t_state.current.server->dst_addr.network_order_port() != 0);

  char addrbuf[INET6_ADDRPORTSTRLEN];
  SMDbg(dbg_ctl_http, "open connection to %s: %s", t_state.current.server->name,
        ats_ip_nptop(&t_state.current.server->dst_addr.sa, addrbuf, sizeof(addrbuf)));

  SMDbg(dbg_ctl_http_seq, "Sending request to server");

  // set the server first connect milestone here in case we return in the plugin_tunnel case that follows
  ATS_PROBE1(milestone_server_connect, sm_id);
  milestones[TS_MILESTONE_SERVER_CONNECT] = ink_get_hrtime();
  if (milestones[TS_MILESTONE_SERVER_FIRST_CONNECT] == 0) {
    ATS_PROBE1(milestone_server_first_connect, sm_id);
    milestones[TS_MILESTONE_SERVER_FIRST_CONNECT] = milestones[TS_MILESTONE_SERVER_CONNECT];
  }

  if (plugin_tunnel) {
    PluginVCCore *t           = plugin_tunnel;
    plugin_tunnel             = nullptr;
    Action *pvc_action_handle = t->connect_re(this);

    // This connect call is always reentrant
    ink_release_assert(pvc_action_handle == ACTION_RESULT_DONE);
    return;
  }

  // Check for remap rule. If so, only apply ip_allow filter if it is activated (ip_allow_check_enabled_p set).
  // Otherwise, if no remap rule is defined, apply the ip_allow filter.
  if (!t_state.url_remap_success || t_state.url_map.getMapping()->ip_allow_check_enabled_p) {
    if (!apply_ip_allow_filter()) {
      return;
    }
  }
  if (HttpTransact::is_server_negative_cached(&t_state) == true &&
      t_state.txn_conf->connect_attempts_max_retries_down_server <= 0) {
    SMDbg(dbg_ctl_http_seq, "Not connecting to the server because it is marked down.");
    call_transact_and_set_next_state(HttpTransact::OriginDown);
    return;
  }

  // Check for self loop.
  if (!_ua.get_txn()->is_outbound_transparent() && HttpTransact::will_this_request_self_loop(&t_state)) {
    call_transact_and_set_next_state(HttpTransact::SelfLoop);
    return;
  }

  // If this is not a raw connection, we try to get a session from the
  //  shared session pool.  Raw connections are for SSLs tunnel and
  //  require a new connection
  //

  // This problem with POST requests is a bug.  Because of the issue of the
  // race with us sending a request after server has closed but before the FIN
  // gets to us, we should open a new connection for POST.  I believe TS used
  // to do this but as far I can tell the code that prevented keep-alive if
  // there is a request body has been removed.

  // If we are sending authorizations headers, mark the connection private
  //
  // We do this here because it means that we will not waste a connection from the pool if we already
  // know that the session will be private. This is overridable meaning that if a plugin later decides
  // it shouldn't be private it can still be returned to a shared pool.
  if (t_state.txn_conf->auth_server_session_private == 1 &&
      t_state.hdr_info.server_request.presence(MIME_PRESENCE_AUTHORIZATION | MIME_PRESENCE_PROXY_AUTHORIZATION |
                                               MIME_PRESENCE_WWW_AUTHENTICATE)) {
    SMDbg(dbg_ctl_http_ss_auth, "Setting server session to private for authorization headers");
    will_be_private_ss = true;
  } else if (t_state.txn_conf->auth_server_session_private == 2 &&
             t_state.hdr_info.server_request.presence(MIME_PRESENCE_PROXY_AUTHORIZATION | MIME_PRESENCE_WWW_AUTHENTICATE)) {
    SMDbg(dbg_ctl_http_ss_auth, "Setting server session to private for Proxy-Authorization or WWW-Authenticate header");
    will_be_private_ss = true;
  }

  if (t_state.method == HTTP_WKSIDX_POST || t_state.method == HTTP_WKSIDX_PUT) {
    // don't share the session if keep-alive for post is not on
    if (t_state.txn_conf->keep_alive_post_out == 0) {
      SMDbg(dbg_ctl_http_ss, "Setting server session to private because of keep-alive post out");
      will_be_private_ss = true;
    }
  }

  bool try_reuse = false;
  if ((raw == false) && TS_SERVER_SESSION_SHARING_MATCH_NONE != t_state.txn_conf->server_session_sharing_match &&
      (t_state.txn_conf->keep_alive_post_out == 1 || t_state.hdr_info.request_content_length <= 0) && !is_private() &&
      _ua.get_txn() != nullptr) {
    HSMresult_t shared_result;
    SMDbg(dbg_ctl_http_ss, "Try to acquire_session for %s", t_state.current.server->name);
    shared_result = httpSessionManager.acquire_session(this,                                 // state machine
                                                       &t_state.current.server->dst_addr.sa, // ip + port
                                                       t_state.current.server->name,         // hostname
                                                       _ua.get_txn()                         // has ptr to bound ua sessions
    );
    try_reuse     = true;

    switch (shared_result) {
    case HSMresult_t::DONE:
      Metrics::Counter::increment(http_rsb.origin_reuse);
      hsm_release_assert(server_txn != nullptr);
      handle_http_server_open();
      return;
    case HSMresult_t::NOT_FOUND:
      Metrics::Counter::increment(http_rsb.origin_not_found);
      hsm_release_assert(server_txn == nullptr);
      break;
    case HSMresult_t::RETRY:
      Metrics::Counter::increment(http_rsb.origin_reuse_fail);
      //  Could not get shared pool lock
      //   FIX: should retry lock
      break;
    default:
      hsm_release_assert(0);
    }
  }
  // Avoid a problem where server session sharing is disabled and we have keep-alive, we are trying to open a new server
  // session when we already have an attached server session.
  else if ((TS_SERVER_SESSION_SHARING_MATCH_NONE == t_state.txn_conf->server_session_sharing_match || is_private()) &&
           (_ua.get_txn() != nullptr)) {
    PoolableSession *existing_ss = _ua.get_txn()->get_server_session();

    if (existing_ss) {
      // [amc] Not sure if this is the best option, but we don't get here unless session sharing is disabled
      // so there's no point in further checking on the match or pool values. But why check anything? The
      // client has already exchanged a request with this specific origin server and has sent another one
      // shouldn't we just automatically keep the association?
      if (ats_ip_addr_port_eq(existing_ss->get_remote_addr(), &t_state.current.server->dst_addr.sa)) {
        _ua.get_txn()->attach_server_session(nullptr);
        existing_ss->set_active();
        this->create_server_txn(existing_ss);
        hsm_release_assert(server_txn != nullptr);
        handle_http_server_open();
        return;
      } else {
        // As this is in the non-sharing configuration, we want to close
        // the existing connection and call connect_re to get a new one
        existing_ss->set_inactivity_timeout(HRTIME_SECONDS(t_state.txn_conf->keep_alive_no_activity_timeout_out));
        existing_ss->release(server_txn);
        _ua.get_txn()->attach_server_session(nullptr);
      }
    }
  }
  // Otherwise, we release the existing connection and call connect_re
  // to get a new one.
  // _ua.get_txn() is null when t_state.req_flavor == HttpRequestFlavor_t::SCHEDULED_UPDATE
  else if (_ua.get_txn() != nullptr) {
    PoolableSession *existing_ss = _ua.get_txn()->get_server_session();
    if (existing_ss) {
      existing_ss->set_inactivity_timeout(HRTIME_SECONDS(t_state.txn_conf->keep_alive_no_activity_timeout_out));
      existing_ss->release(server_txn);
      _ua.get_txn()->attach_server_session(nullptr);
    }
  }

  if (!try_reuse) {
    Metrics::Counter::increment(http_rsb.origin_make_new);
    if (TS_SERVER_SESSION_SHARING_MATCH_NONE == t_state.txn_conf->server_session_sharing_match) {
      Metrics::Counter::increment(http_rsb.origin_no_sharing);
    } else if ((t_state.txn_conf->keep_alive_post_out != 1 && t_state.hdr_info.request_content_length > 0)) {
      Metrics::Counter::increment(http_rsb.origin_body);
    } else if (is_private()) {
      Metrics::Counter::increment(http_rsb.origin_private);
    } else if (raw) {
      Metrics::Counter::increment(http_rsb.origin_raw);
    } else {
      ink_release_assert(_ua.get_txn() == nullptr);
    }
  }

  bool multiplexed_origin = !only_direct && !raw && this->origin_multiplexed() && !is_private();
  if (multiplexed_origin) {
    SMDbg(dbg_ctl_http_ss, "Check for existing connect request");
    if (this->add_to_existing_request()) {
      SMDbg(dbg_ctl_http_ss, "Queue behind existing request");
      // We are queued up behind an existing connect request
      // Go away and wait.
      return;
    }
  }

  // Check to see if we have reached the max number of connections.
  // Atomically read the current number of connections and check to see
  // if we have gone above the max allowed.
  if (t_state.http_config_param->server_max_connections > 0) {
    if (Metrics::Gauge::load(http_rsb.current_server_connections) >= t_state.http_config_param->server_max_connections) {
      httpSessionManager.purge_keepalives();
      // Eventually may want to have a queue as the origin_max_connection does to allow for a combination
      // of retries and errors.  But at this point, we are just going to allow the error case.
      t_state.current.state = HttpTransact::CONNECTION_ERROR;
      call_transact_and_set_next_state(HttpTransact::HandleResponse);
      return;
    }
  }

  // See if the outbound connection tracker data is needed. If so, get it here for consistency.
  if (t_state.txn_conf->connection_tracker_config.server_max > 0 || t_state.txn_conf->connection_tracker_config.server_min > 0) {
    t_state.outbound_conn_track_state =
      ConnectionTracker::obtain_outbound(t_state.txn_conf->connection_tracker_config,
                                         std::string_view{t_state.current.server->name}, t_state.current.server->dst_addr);
  }

  // Check to see if we have reached the max number of connections on this upstream host.
  if (t_state.txn_conf->connection_tracker_config.server_max > 0) {
    auto     &ct_state   = t_state.outbound_conn_track_state;
    auto      ccount     = ct_state.reserve();
    int const server_max = t_state.txn_conf->connection_tracker_config.server_max;
    if (ccount > server_max) {
      ct_state.release();

      ink_assert(pending_action.empty()); // in case of reschedule must not have already pending.

      ct_state.blocked();
      Metrics::Counter::increment(http_rsb.origin_connections_throttled);
      ct_state.Warn_Blocked(server_max, sm_id, ccount - 1, &t_state.current.server->dst_addr.sa,
                            debug_on && dbg_ctl_http.on() ? &dbg_ctl_http : nullptr);
      send_origin_throttled_response();
      return;
    } else {
      ct_state.Note_Unblocked(&t_state.txn_conf->connection_tracker_config, ccount, &t_state.current.server->dst_addr.sa);
    }

    ct_state.update_max_count(ccount);
  }

  // We did not manage to get an existing session and need to open a new connection
  NetVCOptions opt;
  opt.f_blocking_connect = false;
  opt.set_sock_param(t_state.txn_conf->sock_recv_buffer_size_out, t_state.txn_conf->sock_send_buffer_size_out,
                     t_state.txn_conf->sock_option_flag_out, t_state.txn_conf->sock_packet_mark_out,
                     t_state.txn_conf->sock_packet_tos_out, t_state.txn_conf->sock_packet_notsent_lowat);

  set_tls_options(opt, t_state.txn_conf);

  opt.ip_family = ip_family;

  int  scheme_to_use = t_state.scheme; // get initial scheme
  bool tls_upstream  = scheme_to_use == URL_WKSIDX_HTTPS;
  if (_ua.get_txn()) {
    auto tts = _ua.get_txn()->get_netvc()->get_service<TLSTunnelSupport>();
    if (tts && raw) {
      tls_upstream = tts->is_upstream_tls();
      _tunnel_type = tts->get_tunnel_type();

      // ALPN on TLS Partial Blind Tunnel - set negotiated ALPN id
      int pid = SessionProtocolNameRegistry::INVALID;
      if (tts->get_tunnel_type() == SNIRoutingType::PARTIAL_BLIND) {
        auto alpns = _ua.get_txn()->get_netvc()->get_service<ALPNSupport>();
        ink_assert(alpns);
        pid = alpns->get_negotiated_protocol_id();
        if (pid != SessionProtocolNameRegistry::INVALID) {
          opt.alpn_protos = SessionProtocolNameRegistry::convert_openssl_alpn_wire_format(pid);
        }
      }

      if (grab_pre_warmed_net_v_connection_if_possible(*tts, pid)) {
        return;
      }
    }
    opt.local_port = _ua.get_txn()->get_outbound_port();

    const IpAddr &outbound_ip = AF_INET6 == opt.ip_family ? _ua.get_txn()->get_outbound_ip6() : _ua.get_txn()->get_outbound_ip4();
    if (outbound_ip.isValid()) {
      opt.addr_binding = NetVCOptions::INTF_ADDR;
      opt.local_ip     = outbound_ip;
    } else if (_ua.get_txn()->is_outbound_transparent()) {
      opt.addr_binding = NetVCOptions::FOREIGN_ADDR;
      opt.local_ip     = t_state.client_info.src_addr;
      /* If the connection is server side transparent, we can bind to the
         port that the client chose instead of randomly assigning one at
         the proxy.  This is controlled by the 'use_client_source_port'
         configuration parameter.
      */

      NetVConnection *client_vc = _ua.get_txn()->get_netvc();
      if (t_state.http_config_param->use_client_source_port && nullptr != client_vc) {
        opt.local_port = client_vc->get_remote_port();
      }
    }
  }

  if (!t_state.is_websocket) { // if not websocket, then get scheme from server request
    int new_scheme_to_use = t_state.hdr_info.server_request.url_get()->scheme_get_wksidx();
    // if the server_request url scheme was never set, try the client_request
    if (new_scheme_to_use < 0) {
      new_scheme_to_use = t_state.hdr_info.client_request.url_get()->scheme_get_wksidx();
    }
    if (new_scheme_to_use >= 0) { // found a new scheme, use it
      scheme_to_use = new_scheme_to_use;
    }
    if (!raw || !tls_upstream) {
      tls_upstream = scheme_to_use == URL_WKSIDX_HTTPS;
    }
  }

  // draft-stenberg-httpbis-tcp recommends only enabling TFO on idempotent methods or
  // those with intervening protocol layers (eg. TLS).

  if (tls_upstream || HttpTransactHeaders::is_method_idempotent(t_state.method)) {
    opt.f_tcp_fastopen = (t_state.txn_conf->sock_option_flag_out & NetVCOptions::SOCK_OPT_TCP_FAST_OPEN);
  }

  opt.set_ssl_client_cert_name(t_state.txn_conf->ssl_client_cert_filename);
  opt.ssl_client_private_key_name = t_state.txn_conf->ssl_client_private_key_filename;
  opt.ssl_client_ca_cert_name     = t_state.txn_conf->ssl_client_ca_cert_filename;
  if (is_private()) {
    // If the connection to origin is private, don't try to negotiate the higher overhead H2
    opt.alpn_protocols_array_size = -1;
    SMDbg(dbg_ctl_ssl_alpn, "Clear ALPN for private session");
  } else if (t_state.txn_conf->ssl_client_alpn_protocols != nullptr) {
    opt.alpn_protocols_array_size = MAX_ALPN_STRING;
    SMDbg(dbg_ctl_ssl_alpn, "Setting ALPN to: %s", t_state.txn_conf->ssl_client_alpn_protocols);
    convert_alpn_to_wire_format(t_state.txn_conf->ssl_client_alpn_protocols, opt.alpn_protocols_array,
                                opt.alpn_protocols_array_size);
  }

  ConnectingEntry *new_entry = nullptr;
  if (multiplexed_origin) {
    EThread *ethread = this_ethread();
    if (nullptr != ethread->connecting_pool) {
      SMDbg(dbg_ctl_http_ss, "Queue multiplexed request");
      new_entry          = new ConnectingEntry();
      new_entry->mutex   = this->mutex;
      new_entry->ua_txn  = _ua.get_txn();
      new_entry->handler = (ContinuationHandler)&ConnectingEntry::state_http_server_open;
      new_entry->ipaddr.assign(&t_state.current.server->dst_addr.sa);
      new_entry->hostname            = t_state.current.server->name;
      new_entry->sni                 = this->get_outbound_sni();
      new_entry->cert_name           = this->get_outbound_cert();
      new_entry->is_no_plugin_tunnel = plugin_tunnel_type == HttpPluginTunnel_t::NONE;
      this->t_state.set_connect_fail(EIO);
      new_entry->connect_sms.insert(this);
      ethread->connecting_pool->m_ip_pool.insert(std::make_pair(new_entry->ipaddr, new_entry));
    }
  }

  Continuation *cont = new_entry;
  if (!cont) {
    cont = this;
  }
  if (tls_upstream) {
    SMDbg(dbg_ctl_http, "calling sslNetProcessor.connect_re");

    std::string_view sni_name = this->get_outbound_sni();
    if (sni_name.length() > 0) {
      opt.set_sni_servername(sni_name.data(), sni_name.length());
    }
    if (t_state.txn_conf->ssl_client_sni_policy != nullptr &&
        !strcmp(t_state.txn_conf->ssl_client_sni_policy, "verify_with_name_source")) {
      // also set sni_hostname with host header from server request in this policy
      auto host{t_state.hdr_info.server_request.host_get()};
      if (!host.empty()) {
        opt.set_sni_hostname(host.data(), static_cast<int>(host.length()));
      }
    }
    if (t_state.server_info.name) {
      opt.set_ssl_servername(t_state.server_info.name);
    }

    pending_action = sslNetProcessor.connect_re(cont,                                 // state machine or ConnectingEntry
                                                &t_state.current.server->dst_addr.sa, // addr + port
                                                opt);
  } else {
    SMDbg(dbg_ctl_http, "calling netProcessor.connect_re");
    pending_action = netProcessor.connect_re(cont,                                 // state machine or ConnectingEntry
                                             &t_state.current.server->dst_addr.sa, // addr + port
                                             opt);
  }

  return;
}

int
HttpSM::do_api_callout_internal()
{
  switch (t_state.api_next_action) {
  case HttpTransact::StateMachineAction_t::API_SM_START:
    cur_hook_id = TS_HTTP_TXN_START_HOOK;
    break;
  case HttpTransact::StateMachineAction_t::API_PRE_REMAP:
    cur_hook_id = TS_HTTP_PRE_REMAP_HOOK;
    break;
  case HttpTransact::StateMachineAction_t::API_POST_REMAP:
    cur_hook_id = TS_HTTP_POST_REMAP_HOOK;
    break;
  case HttpTransact::StateMachineAction_t::API_TUNNEL_START:
    cur_hook_id = TS_HTTP_TUNNEL_START_HOOK;
    break;
  case HttpTransact::StateMachineAction_t::API_READ_REQUEST_HDR:
    cur_hook_id = TS_HTTP_READ_REQUEST_HDR_HOOK;
    break;
  case HttpTransact::StateMachineAction_t::REQUEST_BUFFER_READ_COMPLETE:
    cur_hook_id = TS_HTTP_REQUEST_BUFFER_READ_COMPLETE_HOOK;
    break;
  case HttpTransact::StateMachineAction_t::API_OS_DNS:
    cur_hook_id = TS_HTTP_OS_DNS_HOOK;
    break;
  case HttpTransact::StateMachineAction_t::API_SEND_REQUEST_HDR:
    cur_hook_id = TS_HTTP_SEND_REQUEST_HDR_HOOK;
    break;
  case HttpTransact::StateMachineAction_t::API_READ_CACHE_HDR:
    cur_hook_id = TS_HTTP_READ_CACHE_HDR_HOOK;
    break;
  case HttpTransact::StateMachineAction_t::API_CACHE_LOOKUP_COMPLETE:
    cur_hook_id = TS_HTTP_CACHE_LOOKUP_COMPLETE_HOOK;
    break;
  case HttpTransact::StateMachineAction_t::API_READ_RESPONSE_HDR:
    cur_hook_id = TS_HTTP_READ_RESPONSE_HDR_HOOK;
    break;
  case HttpTransact::StateMachineAction_t::API_SEND_RESPONSE_HDR:
    cur_hook_id = TS_HTTP_SEND_RESPONSE_HDR_HOOK;
    ATS_PROBE1(milestone_ua_begin_write, sm_id);
    milestones[TS_MILESTONE_UA_BEGIN_WRITE] = ink_get_hrtime();
    break;
  case HttpTransact::StateMachineAction_t::API_SM_SHUTDOWN:
    if (callout_state == HttpApiState_t::IN_CALLOUT || callout_state == HttpApiState_t::DEFERED_SERVER_ERROR) {
      callout_state = HttpApiState_t::DEFERED_CLOSE;
      return 0;
    } else {
      cur_hook_id = TS_HTTP_TXN_CLOSE_HOOK;
    }
    break;
  default:
    cur_hook_id = static_cast<TSHttpHookID>(-1);
    ink_assert(!"not reached");
  }

  hook_state.init(cur_hook_id, http_global_hooks, _ua.get_txn() ? _ua.get_txn()->feature_hooks() : nullptr, &api_hooks);
  cur_hook  = nullptr;
  cur_hooks = 0;
  return state_api_callout(0, nullptr);
}

VConnection *
HttpSM::do_post_transform_open()
{
  ink_assert(post_transform_info.vc == nullptr);

  if (is_action_tag_set("http_post_nullt")) {
    txn_hook_add(TS_HTTP_REQUEST_TRANSFORM_HOOK, transformProcessor.null_transform(mutex.get()));
  }

  post_transform_info.vc = transformProcessor.open(this, api_hooks.get(TS_HTTP_REQUEST_TRANSFORM_HOOK));
  if (post_transform_info.vc) {
    // Record the transform VC in our table
    post_transform_info.entry          = vc_table.new_entry();
    post_transform_info.entry->vc      = post_transform_info.vc;
    post_transform_info.entry->vc_type = HttpVC_t::TRANSFORM_VC;
  }

  return post_transform_info.vc;
}

VConnection *
HttpSM::do_transform_open()
{
  ink_assert(transform_info.vc == nullptr);
  APIHook *hooks;

  if (is_action_tag_set("http_nullt")) {
    txn_hook_add(TS_HTTP_RESPONSE_TRANSFORM_HOOK, transformProcessor.null_transform(mutex.get()));
  }

  hooks = api_hooks.get(TS_HTTP_RESPONSE_TRANSFORM_HOOK);
  if (hooks) {
    transform_info.vc = transformProcessor.open(this, hooks);

    // Record the transform VC in our table
    transform_info.entry          = vc_table.new_entry();
    transform_info.entry->vc      = transform_info.vc;
    transform_info.entry->vc_type = HttpVC_t::TRANSFORM_VC;
  } else {
    transform_info.vc = nullptr;
  }

  return transform_info.vc;
}

void
HttpSM::mark_host_failure(ResolveInfo *info, ts_time time_down)
{
  char addrbuf[INET6_ADDRPORTSTRLEN];

  if (info->active) {
    if (time_down != TS_TIME_ZERO) {
      ats_ip_nptop(&t_state.current.server->dst_addr.sa, addrbuf, sizeof(addrbuf));
      // Increment the fail_count
      if (auto [down, fail_count] = info->active->increment_fail_count(time_down, t_state.txn_conf->connect_attempts_rr_retries);
          down) {
        char            *url_str = t_state.hdr_info.client_request.url_string_get_ref(nullptr);
        std::string_view host_name{t_state.unmapped_url.host_get()};
        swoc::bwprint(error_bw_buffer, "CONNECT : {::s} connecting to {} for host='{}' url='{}' fail_count='{}' marking down",
                      swoc::bwf::Errno(t_state.current.server->connect_result), t_state.current.server->dst_addr, host_name,
                      swoc::bwf::FirstOf(url_str, "<none>"), fail_count);
        Log::error("%s", error_bw_buffer.c_str());
        SMDbg(dbg_ctl_http, "hostdb update marking IP: %s as down", addrbuf);
        ATS_PROBE2(hostdb_mark_ip_as_down, sm_id, addrbuf);
      } else {
        ATS_PROBE3(hostdb_inc_ip_failcount, sm_id, addrbuf, fail_count);
        SMDbg(dbg_ctl_http, "hostdb increment IP failcount %s to %d", addrbuf, fail_count);
      }
    } else { // Clear the failure
      info->active->mark_up();
    }
  }
#ifdef DEBUG
  ink_assert(std::chrono::system_clock::now() + t_state.txn_conf->down_server_timeout > time_down);
#endif
}

void
HttpSM::set_ua_abort(HttpTransact::AbortState_t ua_abort, int event)
{
  t_state.client_info.abort = ua_abort;

  switch (ua_abort) {
  case HttpTransact::ABORTED:
    // More detailed client side abort logging based on event
    switch (event) {
    case VC_EVENT_ERROR:
      t_state.squid_codes.log_code = SquidLogCode::ERR_CLIENT_READ_ERROR;
      break;
    case VC_EVENT_EOS:
    case VC_EVENT_ACTIVE_TIMEOUT:     // Won't matter. Server will hangup
    case VC_EVENT_INACTIVITY_TIMEOUT: // Won't matter. Send back 408
    // Fall-through
    default:
      t_state.squid_codes.log_code = SquidLogCode::ERR_CLIENT_ABORT;
      break;
    }
    break;
  default:
    // Handled here:
    // HttpTransact::ABORT_UNDEFINED, HttpTransact::DIDNOT_ABORT
    break;
  }

  // Set the connection attribute code for the client so that
  //   we log the client finish code correctly
  switch (event) {
  case VC_EVENT_ACTIVE_TIMEOUT:
    t_state.client_info.state = HttpTransact::ACTIVE_TIMEOUT;
    break;
  case VC_EVENT_INACTIVITY_TIMEOUT:
    t_state.client_info.state = HttpTransact::INACTIVE_TIMEOUT;
    break;
  case VC_EVENT_ERROR:
    t_state.client_info.state = HttpTransact::CONNECTION_ERROR;
    break;
  case HTTP_TUNNEL_EVENT_PARSE_ERROR:
    t_state.client_info.state = HttpTransact::PARSE_ERROR;
    break;
  }
}

// void HttpSM::release_server_session()
//
//  Called when we are not tunneling a response from the
//   server.  If the session is keep alive, release it back to the
//   shared pool, otherwise close it
//
void
HttpSM::release_server_session(bool serve_from_cache)
{
  if (server_txn == nullptr) {
    return;
  }

  if (TS_SERVER_SESSION_SHARING_MATCH_NONE != t_state.txn_conf->server_session_sharing_match && t_state.current.server != nullptr &&
      t_state.current.server->keep_alive == HTTPKeepAlive::KEEPALIVE && t_state.hdr_info.server_response.valid() &&
      t_state.hdr_info.server_request.valid() &&
      (t_state.hdr_info.server_response.status_get() == HTTPStatus::NOT_MODIFIED ||
       (t_state.hdr_info.server_request.method_get_wksidx() == HTTP_WKSIDX_HEAD &&
        t_state.www_auth_content != HttpTransact::CacheAuth_t::NONE)) &&
      plugin_tunnel_type == HttpPluginTunnel_t::NONE && (!server_entry || !server_entry->eos)) {
    if (t_state.www_auth_content == HttpTransact::CacheAuth_t::NONE || serve_from_cache == false) {
      // Must explicitly set the keep_alive_no_activity time before doing the release
      server_txn->set_inactivity_timeout(HRTIME_SECONDS(t_state.txn_conf->keep_alive_no_activity_timeout_out));
      server_txn->release();
    } else {
      // an authenticated server connection - attach to the local client
      // we are serving from cache for the current transaction
      t_state.www_auth_content = HttpTransact::CacheAuth_t::SERVE;
      _ua.get_txn()->attach_server_session(static_cast<PoolableSession *>(server_txn->get_proxy_ssn()), false);
    }
  } else {
    server_txn->do_io_close();
    if (TS_SERVER_SESSION_SHARING_MATCH_NONE == t_state.txn_conf->server_session_sharing_match) {
      Metrics::Counter::increment(http_rsb.origin_shutdown_release_no_sharing);
    } else if (t_state.current.server == nullptr) {
      Metrics::Counter::increment(http_rsb.origin_shutdown_release_no_server);
    } else if (t_state.current.server->keep_alive != HTTPKeepAlive::KEEPALIVE) {
      Metrics::Counter::increment(http_rsb.origin_shutdown_release_no_keep_alive);
    } else if (!t_state.hdr_info.server_response.valid()) {
      Metrics::Counter::increment(http_rsb.origin_shutdown_release_invalid_response);
    } else if (!t_state.hdr_info.server_request.valid()) {
      Metrics::Counter::increment(http_rsb.origin_shutdown_release_invalid_request);
    } else if (t_state.hdr_info.server_response.status_get() != HTTPStatus::NOT_MODIFIED &&
               (t_state.hdr_info.server_request.method_get_wksidx() != HTTP_WKSIDX_HEAD ||
                t_state.www_auth_content == HttpTransact::CacheAuth_t::NONE)) {
      Metrics::Counter::increment(http_rsb.origin_shutdown_release_modified);
    } else {
      Metrics::Counter::increment(http_rsb.origin_shutdown_release_misc);
    }
  }

  if (server_entry) {
    server_entry->vc        = nullptr;
    server_entry->read_vio  = nullptr;
    server_entry->write_vio = nullptr;
    server_entry            = nullptr;
  }
}

// void HttpSM::handle_post_failure()
//
//   We failed in our attempt post (or put) a document
//    to the server.  Two cases happen here.  The normal
//    one is the server is down, in which case we ought to
//    return an error to the client.  The second one is
//    stupid.  The server returned a response without reading
//    all the post data.  In order to be as transparent as
//    possible process the server's response
void
HttpSM::handle_post_failure()
{
  STATE_ENTER(&HttpSM::handle_post_failure, VC_EVENT_NONE);

  ink_assert(_ua.get_entry()->vc == _ua.get_txn());
  ink_assert(is_waiting_for_full_body || server_entry->eos == true);

  if (is_waiting_for_full_body) {
    call_transact_and_set_next_state(HttpTransact::Forbidden);
    return;
  }
  // First order of business is to clean up from
  //  the tunnel
  // note: since the tunnel is providing the buffer for a lingering
  // client read (for abort watching purposes), we need to stop
  // the read
  if (false == t_state.redirect_info.redirect_in_process) {
    _ua.get_entry()->read_vio = _ua.get_txn()->do_io_read(this, 0, nullptr);
  }
  _ua.get_entry()->in_tunnel = false;
  server_entry->in_tunnel    = false;

  // disable redirection in case we got a partial response and then EOS, because the buffer might not
  // have the full post and it's deallocating the post buffers here
  this->disable_redirect();

  // Don't even think about doing keep-alive after this debacle
  t_state.client_info.keep_alive     = HTTPKeepAlive::NO_KEEPALIVE;
  t_state.current.server->keep_alive = HTTPKeepAlive::NO_KEEPALIVE;

  tunnel.deallocate_buffers();
  tunnel.reset();
  // Server is down
  if (t_state.current.state == HttpTransact::STATE_UNDEFINED || t_state.current.state == HttpTransact::CONNECTION_ALIVE) {
    t_state.set_connect_fail(server_txn->get_netvc()->lerrno);
    t_state.current.state = HttpTransact::CONNECTION_CLOSED;
  }
  call_transact_and_set_next_state(HttpTransact::HandleResponse);
}

// void HttpSM::handle_http_server_open()
//
//   The server connection is now open.  If there is a POST or PUT,
//    we need setup a transform is there is one otherwise we need
//    to send the request header
//
void
HttpSM::handle_http_server_open()
{
  // [bwyatt] applying per-transaction OS netVC options here
  //          IFF they differ from the netVC's current options.
  //          This should keep this from being redundant on a
  //          server session's first transaction.
  if (nullptr != server_txn) {
    NetVConnection *vc = server_txn->get_netvc();
    if (vc) {
      server_connection_provided_cert = vc->provided_cert();
      if (vc->options.sockopt_flags != t_state.txn_conf->sock_option_flag_out ||
          vc->options.packet_mark != t_state.txn_conf->sock_packet_mark_out ||
          vc->options.packet_tos != t_state.txn_conf->sock_packet_tos_out ||
          vc->options.packet_notsent_lowat != t_state.txn_conf->sock_packet_notsent_lowat) {
        vc->options.sockopt_flags        = t_state.txn_conf->sock_option_flag_out;
        vc->options.packet_mark          = t_state.txn_conf->sock_packet_mark_out;
        vc->options.packet_tos           = t_state.txn_conf->sock_packet_tos_out;
        vc->options.packet_notsent_lowat = t_state.txn_conf->sock_packet_notsent_lowat;
        vc->apply_options();
      }
    }
    server_txn->set_inactivity_timeout(get_server_inactivity_timeout());

    int method = t_state.hdr_info.server_request.method_get_wksidx();
    if (method != HTTP_WKSIDX_TRACE &&
        server_txn->has_request_body(t_state.hdr_info.request_content_length,
                                     t_state.client_info.transfer_encoding == HttpTransact::TransferEncoding_t::CHUNKED) &&
        do_post_transform_open()) {
      do_setup_client_request_body_tunnel(
        HttpVC_t::TRANSFORM_VC); /* This doesn't seem quite right.  Should be sending the request header */
    } else {
      setup_server_send_request_api();
    }
  } else {
    ink_release_assert(!"No server_txn");
  }
}

// void HttpSM::handle_server_setup_error(int event, void* data)
//
//   Handles setting t_state.current.state and calling
//    Transact in between opening an origin server connection
//    and receiving the response header (in the case of the
//    POST, a post tunnel happens in between the sending
//    request header and reading the response header
//
void
HttpSM::handle_server_setup_error(int event, void *data)
{
  VIO *vio = static_cast<VIO *>(data);
  ink_assert(vio != nullptr);

  STATE_ENTER(&HttpSM::handle_server_setup_error, event);

  // If there is POST or PUT tunnel wait for the tunnel
  //  to figure out that things have gone to hell

  if (tunnel.is_tunnel_active()) {
    ink_assert(server_entry->read_vio == data || server_entry->write_vio == data);
    SMDbg(dbg_ctl_http, "forwarding event %s to post tunnel", HttpDebugNames::get_event_name(event));
    HttpTunnelConsumer *c = tunnel.get_consumer(server_entry->vc);
    // it is possible only user agent post->post transform is set up
    // this happened for Linux iocore where NET_EVENT_OPEN was returned
    // for a non-existing listening port. the hack is to pass the error
    // event for server connection to post_transform_info
    if (c == nullptr && post_transform_info.vc) {
      c = tunnel.get_consumer(post_transform_info.vc);
      // c->handler_state = HTTP_SM_TRANSFORM_FAIL;

      // No point in proceeding if there is no consumer
      // Do we need to do additional clean up in the c == NULL case?
      if (c != nullptr) {
        HttpTunnelProducer *ua_producer = c->producer;
        ink_assert(_ua.get_entry()->vc == ua_producer->vc);

        _ua.get_entry()->vc_read_handler  = &HttpSM::state_watch_for_client_abort;
        _ua.get_entry()->vc_write_handler = &HttpSM::state_watch_for_client_abort;
        _ua.get_entry()->read_vio         = ua_producer->vc->do_io_read(this, INT64_MAX, c->producer->read_buffer);
        ua_producer->vc->do_io_shutdown(IO_SHUTDOWN_READ);

        ua_producer->alive         = false;
        ua_producer->handler_state = static_cast<int>(HttpSmPost_t::SERVER_FAIL);
        tunnel.handleEvent(VC_EVENT_ERROR, c->write_vio);
        return;
      }
    } else {
      // c could be null here as well
      if (c != nullptr) {
        tunnel.handleEvent(event, c->write_vio);
        return;
      }
    }
    // If there is no consumer, let the event pass through to shutdown
  } else {
    if (post_transform_info.vc) {
      HttpTunnelConsumer *c = tunnel.get_consumer(post_transform_info.vc);
      if (c && c->handler_state == HTTP_SM_TRANSFORM_OPEN) {
        vc_table.cleanup_entry(post_transform_info.entry);
        post_transform_info.entry = nullptr;
        tunnel.deallocate_buffers();
        tunnel.reset();
      }
    }
  }

  [[maybe_unused]] UnixNetVConnection *dbg_vc = nullptr;
  switch (event) {
  case VC_EVENT_EOS:
    t_state.current.state = HttpTransact::CONNECTION_CLOSED;
    t_state.set_connect_fail(EPIPE);
    break;
  case VC_EVENT_ERROR:
    t_state.current.state = HttpTransact::CONNECTION_ERROR;
    t_state.set_connect_fail(server_txn->get_netvc()->lerrno);
    break;
  case VC_EVENT_ACTIVE_TIMEOUT:
    t_state.set_connect_fail(ETIMEDOUT);
    t_state.current.state = HttpTransact::ACTIVE_TIMEOUT;
    break;

  case VC_EVENT_INACTIVITY_TIMEOUT:
    // If we're writing the request and get an inactivity timeout
    //   before any bytes are written, the connection to the
    //   server failed
    // In case of TIMEOUT, the iocore sends back
    // server_entry->read_vio instead of the write_vio
    t_state.set_connect_fail(ETIMEDOUT);
    if (server_entry->write_vio && server_entry->write_vio->nbytes > 0 && server_entry->write_vio->ndone == 0) {
      t_state.current.state = HttpTransact::CONNECTION_ERROR;
    } else {
      t_state.current.state = HttpTransact::INACTIVE_TIMEOUT;
    }
    break;
  default:
    ink_release_assert(0);
  }

  if (event == VC_EVENT_INACTIVITY_TIMEOUT || event == VC_EVENT_ERROR || event == VC_EVENT_EOS) {
    // Clean up the vc_table entry so any events in play to the timed out server vio
    // don't get handled.  The connection isn't there.
    if (server_entry) {
      ink_assert(server_entry->vc_type == HttpVC_t::SERVER_VC);
      vc_table.cleanup_entry(server_entry);
      server_entry = nullptr;
    }
  }

  // Closedown server connection and deallocate buffers
  ink_assert(!server_entry || server_entry->in_tunnel == false);

  // if we are waiting on a plugin callout for
  //   HTTP_API_SEND_REQUEST_HDR defer calling transact until
  //   after we've finished processing the plugin callout
  switch (callout_state) {
  case HttpApiState_t::NO_CALLOUT:
    // Normal fast path case, no api callouts in progress
    break;
  case HttpApiState_t::IN_CALLOUT:
  case HttpApiState_t::DEFERED_SERVER_ERROR:
    // Callout in progress note that we are in deferring
    //   the server error
    callout_state = HttpApiState_t::DEFERED_SERVER_ERROR;
    return;
  case HttpApiState_t::DEFERED_CLOSE:
    // The user agent has shutdown killing the sm
    //   but we are stuck waiting for the server callout
    //   to finish so do nothing here.  We don't care
    //   about the server connection at this and are
    //   just waiting till we can execute the close hook
    return;
  default:
    ink_release_assert(0);
  }

  call_transact_and_set_next_state(HttpTransact::HandleResponse);
}

void
HttpSM::setup_transform_to_server_transfer()
{
  ink_assert(post_transform_info.vc != nullptr);
  ink_assert(post_transform_info.entry->vc == post_transform_info.vc);

  int64_t         nbytes      = t_state.hdr_info.transform_request_cl;
  int64_t         alloc_index = buffer_size_to_index(nbytes, t_state.http_config_param->max_payload_iobuf_index);
  MIOBuffer      *post_buffer = new_MIOBuffer(alloc_index);
  IOBufferReader *buf_start   = post_buffer->alloc_reader();

  HTTP_SM_SET_DEFAULT_HANDLER(&HttpSM::tunnel_handler_post);

  HttpTunnelConsumer *c = tunnel.get_consumer(post_transform_info.vc);

  HttpTunnelProducer *p = tunnel.add_producer(post_transform_info.vc, nbytes, buf_start, &HttpSM::tunnel_handler_transform_read,
                                              HttpTunnelType_t::TRANSFORM, "post transform");
  tunnel.chain(c, p);
  post_transform_info.entry->in_tunnel = true;

  tunnel.add_consumer(server_entry->vc, post_transform_info.vc, &HttpSM::tunnel_handler_post_server, HttpTunnelType_t::HTTP_SERVER,
                      "http server post");
  server_entry->in_tunnel = true;

  tunnel.tunnel_run(p);
}

void
HttpSM::do_drain_request_body(HTTPHdr &response)
{
  int64_t content_length = t_state.hdr_info.client_request.get_content_length();
  int64_t avail          = _ua.get_txn()->get_remote_reader()->read_avail();

  if (t_state.client_info.transfer_encoding == HttpTransact::TransferEncoding_t::CHUNKED) {
    SMDbg(dbg_ctl_http, "Chunked body, setting the response to non-keepalive");
    goto close_connection;
  }

  if (content_length > 0) {
    if (avail >= content_length) {
      SMDbg(dbg_ctl_http, "entire body is in the buffer, consuming");
      int64_t act_on            = (avail < content_length) ? avail : content_length;
      client_request_body_bytes = act_on;
      _ua.get_txn()->get_remote_reader()->consume(act_on);
    } else {
      SMDbg(dbg_ctl_http, "entire body is not in the buffer, setting the response to non-keepalive");
      goto close_connection;
    }
  }
  return;

close_connection:
  t_state.client_info.keep_alive = HTTPKeepAlive::NO_KEEPALIVE;
  _ua.get_txn()->set_close_connection(response);
}

void
HttpSM::do_setup_client_request_body_tunnel(HttpVC_t to_vc_type)
{
  if (t_state.hdr_info.request_content_length == 0) {
    // No tunnel is needed to transfer 0 bytes. Simply return without setting up
    // a tunnel nor any of the other related logic around request bodies.
    return;
  }
  bool chunked = t_state.client_info.transfer_encoding == HttpTransact::TransferEncoding_t::CHUNKED ||
                 t_state.hdr_info.request_content_length == HTTP_UNDEFINED_CL;
  bool post_redirect = false;

  HttpTunnelProducer *p = nullptr;
  // YTS Team, yamsat Plugin
  // if redirect_in_process and redirection is enabled add static producer

  if (is_buffering_request_body ||
      (t_state.redirect_info.redirect_in_process && enable_redirection && this->_postbuf.postdata_copy_buffer_start != nullptr)) {
    post_redirect = true;
    // copy the post data into a new producer buffer for static producer
    MIOBuffer      *postdata_producer_buffer = new_empty_MIOBuffer(t_state.http_config_param->max_payload_iobuf_index);
    IOBufferReader *postdata_producer_reader = postdata_producer_buffer->alloc_reader();

    postdata_producer_buffer->write(this->_postbuf.postdata_copy_buffer_start);
    int64_t post_bytes = chunked ? INT64_MAX : t_state.hdr_info.request_content_length;
    transferred_bytes  = post_bytes;
    p = tunnel.add_producer(HTTP_TUNNEL_STATIC_PRODUCER, post_bytes, postdata_producer_reader, (HttpProducerHandler) nullptr,
                            HttpTunnelType_t::STATIC, "redirect static agent post");
  } else {
    int64_t alloc_index;
    // content length is undefined, use default buffer size
    if (t_state.hdr_info.request_content_length == HTTP_UNDEFINED_CL) {
      alloc_index = static_cast<int>(t_state.txn_conf->default_buffer_size_index);
      if (alloc_index < MIN_CONFIG_BUFFER_SIZE_INDEX || alloc_index > MAX_BUFFER_SIZE_INDEX) {
        alloc_index = DEFAULT_REQUEST_BUFFER_SIZE_INDEX;
      }
    } else {
      alloc_index =
        buffer_size_to_index(t_state.hdr_info.request_content_length, t_state.http_config_param->max_payload_iobuf_index);
    }
    MIOBuffer      *post_buffer = new_MIOBuffer(alloc_index);
    IOBufferReader *buf_start   = post_buffer->alloc_reader();
    int64_t         post_bytes  = chunked ? INT64_MAX : t_state.hdr_info.request_content_length;

    if (enable_redirection) {
      this->_postbuf.init(post_buffer->clone_reader(buf_start));
    }

    // Note: Many browsers, Netscape and IE included send two extra
    //  bytes (CRLF) at the end of the post.  We just ignore those
    //  bytes since the sending them is not spec

    // Next order of business if copy the remaining data from the
    //  header buffer into new buffer

    int64_t num_body_bytes = 0;
    // If is_using_post_buffer has been used, then client_request_body_bytes
    // will have already been sent in wait_for_full_body and there will be
    // zero bytes in this user agent buffer. We don't want to clobber
    // client_request_body_bytes with a zero value here in those cases.
    if (client_request_body_bytes > 0) {
      num_body_bytes = client_request_body_bytes;
    } else {
      num_body_bytes = post_buffer->write(_ua.get_txn()->get_remote_reader(),
                                          chunked ? _ua.get_txn()->get_remote_reader()->read_avail() : post_bytes);
    }
    // Don't consume post_bytes here from _ua.get_txn()->get_remote_reader() since
    // we are not sure how many bytes the tunnel will use yet. Wait until
    // HttpSM::tunnel_handler_post_ua to consume the bytes.
    // The user agent has already sent all it has
    if (_ua.get_txn()->is_read_closed()) {
      post_bytes = num_body_bytes;
    }
    p = tunnel.add_producer(_ua.get_entry()->vc, post_bytes - transferred_bytes, buf_start, &HttpSM::tunnel_handler_post_ua,
                            HttpTunnelType_t::HTTP_CLIENT, "user agent post");
  }
  _ua.get_entry()->in_tunnel = true;

  switch (to_vc_type) {
  case HttpVC_t::TRANSFORM_VC:
    HTTP_SM_SET_DEFAULT_HANDLER(&HttpSM::state_request_wait_for_transform_read);
    ink_assert(post_transform_info.entry != nullptr);
    ink_assert(post_transform_info.entry->vc == post_transform_info.vc);
    tunnel.add_consumer(post_transform_info.entry->vc, _ua.get_entry()->vc, &HttpSM::tunnel_handler_transform_write,
                        HttpTunnelType_t::TRANSFORM, "post transform");
    post_transform_info.entry->in_tunnel = true;
    break;
  case HttpVC_t::SERVER_VC:
    // YTS Team, yamsat Plugin
    // When redirect in process is true and redirection is enabled
    // add http server as the consumer
    if (post_redirect) {
      chunked = false;
      HTTP_SM_SET_DEFAULT_HANDLER(&HttpSM::tunnel_handler_for_partial_post);
      tunnel.add_consumer(server_entry->vc, HTTP_TUNNEL_STATIC_PRODUCER, &HttpSM::tunnel_handler_post_server,
                          HttpTunnelType_t::HTTP_SERVER, "redirect http server post");
    } else {
      HTTP_SM_SET_DEFAULT_HANDLER(&HttpSM::tunnel_handler_post);
      tunnel.add_consumer(server_entry->vc, _ua.get_entry()->vc, &HttpSM::tunnel_handler_post_server, HttpTunnelType_t::HTTP_SERVER,
                          "http server post");
    }
    server_entry->in_tunnel = true;
    break;
  default:
    ink_release_assert(0);
    break;
  }

  this->setup_client_request_plugin_agents(p);

  // The user agent and origin  may support chunked (HTTP/1.1) or not (HTTP/2)
  if (chunked) {
    bool const drop_chunked_trailers = t_state.http_config_param->oride.http_drop_chunked_trailers == 1;
    bool const parse_chunk_strictly  = t_state.http_config_param->oride.http_strict_chunk_parsing == 1;
    if (_ua.get_txn()->is_chunked_encoding_supported()) {
      if (server_txn->is_chunked_encoding_supported()) {
        tunnel.set_producer_chunking_action(p, 0, TunnelChunkingAction_t::PASSTHRU_CHUNKED_CONTENT, drop_chunked_trailers,
                                            parse_chunk_strictly);
      } else {
        tunnel.set_producer_chunking_action(p, 0, TunnelChunkingAction_t::DECHUNK_CONTENT, drop_chunked_trailers,
                                            parse_chunk_strictly);
        tunnel.set_producer_chunking_size(p, 0);
      }
    } else {
      if (server_txn->is_chunked_encoding_supported()) {
        tunnel.set_producer_chunking_action(p, 0, TunnelChunkingAction_t::CHUNK_CONTENT, drop_chunked_trailers,
                                            parse_chunk_strictly);
        tunnel.set_producer_chunking_size(p, 0);
      } else {
        tunnel.set_producer_chunking_action(p, 0, TunnelChunkingAction_t::PASSTHRU_DECHUNKED_CONTENT, drop_chunked_trailers,
                                            parse_chunk_strictly);
      }
    }
  }

  _ua.get_txn()->set_inactivity_timeout(HRTIME_SECONDS(t_state.txn_conf->transaction_no_activity_timeout_in));
  server_txn->set_inactivity_timeout(get_server_inactivity_timeout());

  tunnel.tunnel_run(p);

  // If we're half closed, we got a FIN from the client. Forward it on to the origin server
  // now that we have the tunnel operational.
  // HttpTunnel could broken due to bad chunked data and close all vc by chain_abort_all().
  if (static_cast<HttpSmPost_t>(p->handler_state) != HttpSmPost_t::UA_FAIL && _ua.get_txn()->get_half_close_flag()) {
    p->vc->do_io_shutdown(IO_SHUTDOWN_READ);
  }
}

// void HttpSM::perform_transform_cache_write_action()
//
//   Called to do cache write from the transform
//
void
HttpSM::perform_transform_cache_write_action()
{
  SMDbg(dbg_ctl_http, "%s", HttpDebugNames::get_cache_action_name(t_state.cache_info.action));

  if (t_state.range_setup != HttpTransact::RangeSetup_t::NONE) {
    SMDbg(dbg_ctl_http, "perform_transform_cache_write_action %s (with range setup)",
          HttpDebugNames::get_cache_action_name(t_state.cache_info.action));
  }

  switch (t_state.cache_info.transform_action) {
  case HttpTransact::CacheAction_t::NO_ACTION: {
    // Nothing to do
    transform_cache_sm.end_both();
    break;
  }

  case HttpTransact::CacheAction_t::WRITE: {
    if (t_state.api_info.cache_untransformed == false) {
      transform_cache_sm.close_read();
      t_state.cache_info.transform_write_status = HttpTransact::CacheWriteStatus_t::IN_PROGRESS;
      setup_cache_write_transfer(&transform_cache_sm, transform_info.entry->vc, &t_state.cache_info.transform_store,
                                 client_response_hdr_bytes, "cache write t");
    }
    break;
  }

  default:
    ink_release_assert(0);
    break;
  }
}

// void HttpSM::perform_cache_write_action()
//
//   Called to do cache write, delete and updates based
//    on s->cache_info.action.  Does not setup cache
//    read tunnels
//
void
HttpSM::perform_cache_write_action()
{
  SMDbg(dbg_ctl_http, "%s", HttpDebugNames::get_cache_action_name(t_state.cache_info.action));

  switch (t_state.cache_info.action) {
  case HttpTransact::CacheAction_t::NO_ACTION:

  {
    // Nothing to do
    cache_sm.end_both();
    break;
  }

  case HttpTransact::CacheAction_t::SERVE: {
    cache_sm.abort_write();
    break;
  }

  case HttpTransact::CacheAction_t::DELETE: {
    // Write close deletes the old alternate
    cache_sm.close_write();
    cache_sm.close_read();
    t_state.cache_info.write_lock_state = HttpTransact::CacheWriteLock_t::INIT;
    break;
  }

  case HttpTransact::CacheAction_t::SERVE_AND_DELETE: {
    // FIX ME: need to set up delete for after cache write has
    //   completed
    break;
  }

  case HttpTransact::CacheAction_t::SERVE_AND_UPDATE: {
    issue_cache_update();
    break;
  }

  case HttpTransact::CacheAction_t::UPDATE: {
    cache_sm.close_read();
    issue_cache_update();
    break;
  }

  case HttpTransact::CacheAction_t::WRITE:
  case HttpTransact::CacheAction_t::REPLACE:
    // Fix need to set up delete for after cache write has
    //   completed
    if (transform_info.entry == nullptr || t_state.api_info.cache_untransformed == true) {
      cache_sm.close_read();
      t_state.cache_info.write_status = HttpTransact::CacheWriteStatus_t::IN_PROGRESS;
      setup_cache_write_transfer(&cache_sm, server_entry->vc, &t_state.cache_info.object_store, client_response_hdr_bytes,
                                 "cache write");
    } else {
      // We are not caching the untransformed.  We might want to
      //  use the cache writevc to cache the transformed copy
      ink_assert(transform_cache_sm.cache_write_vc == nullptr);
      transform_cache_sm.cache_write_vc = cache_sm.cache_write_vc;
      cache_sm.cache_write_vc           = nullptr;
    }
    break;

  default:
    ink_release_assert(0);
    break;
  }
}

void
HttpSM::issue_cache_update()
{
  ink_assert(cache_sm.cache_write_vc != nullptr);
  if (cache_sm.cache_write_vc) {
    t_state.cache_info.object_store.request_sent_time_set(t_state.request_sent_time);
    t_state.cache_info.object_store.response_received_time_set(t_state.response_received_time);
    ink_assert(t_state.cache_info.object_store.request_sent_time_get() > 0);
    ink_assert(t_state.cache_info.object_store.response_received_time_get() > 0);
    cache_sm.cache_write_vc->set_http_info(&t_state.cache_info.object_store);
    t_state.cache_info.object_store.clear();
  }
  // Now close the write which commits the update
  cache_sm.close_write();
  t_state.cache_info.write_lock_state = HttpTransact::CacheWriteLock_t::INIT;
}

int
HttpSM::write_header_into_buffer(HTTPHdr *h, MIOBuffer *b)
{
  int dumpoffset;
  int done;

  dumpoffset = 0;
  do {
    IOBufferBlock *block    = b->get_current_block();
    int            bufindex = 0;
    int            tmp      = dumpoffset;

    ink_assert(block->write_avail() > 0);
    done        = h->print(block->start(), block->write_avail(), &bufindex, &tmp);
    dumpoffset += bufindex;
    ink_assert(bufindex > 0);
    b->fill(bufindex);
    if (!done) {
      b->add_block();
    }
  } while (!done);

  return dumpoffset;
}

void
HttpSM::attach_server_session()
{
  hsm_release_assert(server_entry == nullptr);
  // In the h1 only origin version, the transact_count was updated after making this assignment.
  // The SSN-TXN-COUNT option in header rewrite relies on this fact, so we decrement here so the
  // plugin API interface is consistent as we move to more protocols to origin
  server_transact_count = server_txn->get_proxy_ssn()->get_transact_count() - 1;

  // update the dst_addr when using an existing session
  // for e.g using Host based session pools may ignore the DNS IP
  IpEndpoint addr;
  addr.assign(server_txn->get_remote_addr());
  if (!ats_ip_addr_eq(&t_state.current.server->dst_addr, &addr)) {
    ip_port_text_buffer ipb1, ipb2;
    SMDbg(dbg_ctl_http_ss, "updating ip when attaching server session from %s to %s",
          ats_ip_ntop(&t_state.current.server->dst_addr.sa, ipb1, sizeof(ipb1)),
          ats_ip_ntop(server_txn->get_remote_addr(), ipb2, sizeof(ipb2)));
    ats_ip_copy(&t_state.current.server->dst_addr, server_txn->get_remote_addr());
  }

  // Propagate the per client IP debugging
  if (_ua.get_txn()) {
    server_txn->get_netvc()->control_flags.set_flags(get_cont_flags().get_flags());
  } else { // If there is no _ua.get_txn() no sense in continuing to attach the server session
    return;
  }

  // Set the mutex so that we have something to update
  //   stats with
  server_txn->mutex = this->mutex;

  server_txn->increment_transactions_stat();

  // Record the VC in our table
  server_entry                   = vc_table.new_entry();
  server_entry->vc               = server_txn;
  server_entry->vc_type          = HttpVC_t::SERVER_VC;
  server_entry->vc_write_handler = &HttpSM::state_send_server_request_header;

  UnixNetVConnection *server_vc = static_cast<UnixNetVConnection *>(server_txn->get_netvc());

  // set flag for server session is SSL
  if (server_vc->get_service<TLSBasicSupport>()) {
    server_connection_is_ssl = true;
  }

  if (auto tsrs = server_vc->get_service<TLSSessionResumptionSupport>(); tsrs) {
    server_ssl_reused = tsrs->getSSLOriginSessionCacheHit();
  }

  server_protocol = server_txn->get_protocol_string();

  // Initiate a read on the session so that the SM and not
  //  session manager will get called back if the timeout occurs
  //  or the server closes on us.  The IO Core now requires us to
  //  do the read with a buffer and a size so preallocate the
  //  buffer

  // ts-3189 We are only setting up an empty read at this point.  This
  // is sufficient to have the timeout errors directed to the appropriate
  // SM handler, but we don't want to read any data until the tunnel has
  // been set up.  This isn't such a big deal with GET results, since
  // if no tunnels are set up, there is no danger of data being delivered
  // to the wrong tunnel's consumer handler.  But for post and other
  // methods that send data after the request, two tunnels are created in
  // series, and with a full read set up at this point, the EOS from the
  // first tunnel was sometimes behind handled by the consumer of the
  // first tunnel instead of the producer of the second tunnel.
  // The real read is setup in setup_server_read_response_header()
  server_entry->read_vio = server_txn->do_io_read(this, 0, server_txn->get_remote_reader()->mbuf);

  // Transfer control of the write side as well
  server_entry->write_vio = server_txn->do_io_write(this, 0, nullptr);

  // Setup the timeouts
  // Set the inactivity timeout to the connect timeout so that we
  //   we fail this server if it doesn't start sending the response
  //   header
  server_txn->set_inactivity_timeout(get_server_connect_timeout());
  server_txn->set_active_timeout(get_server_active_timeout());

  // Do we need Transfer_Encoding?
  if (_ua.get_txn()->has_request_body(t_state.hdr_info.request_content_length,
                                      t_state.client_info.transfer_encoding == HttpTransact::TransferEncoding_t::CHUNKED)) {
    if (server_txn->is_chunked_encoding_supported()) {
      // See if we need to insert a chunked header
      if (!t_state.hdr_info.server_request.presence(MIME_PRESENCE_CONTENT_LENGTH) &&
          !t_state.hdr_info.server_request.presence(MIME_PRESENCE_TRANSFER_ENCODING)) {
        // Stuff in a TE setting so we treat this as chunked, sort of.
        t_state.server_info.transfer_encoding = HttpTransact::TransferEncoding_t::CHUNKED;
        t_state.hdr_info.server_request.value_append(static_cast<std::string_view>(MIME_FIELD_TRANSFER_ENCODING),
                                                     static_cast<std::string_view>(HTTP_VALUE_CHUNKED), true);
      }
    }
  }

  if (plugin_tunnel_type != HttpPluginTunnel_t::NONE || is_private()) {
    this->set_server_session_private(true);
  }
}

void
HttpSM::setup_server_send_request_api()
{
  // Make sure the VC is on the correct timeout
  server_txn->set_inactivity_timeout(get_server_inactivity_timeout());
  t_state.api_next_action = HttpTransact::StateMachineAction_t::API_SEND_REQUEST_HDR;
  do_api_callout();
}

void
HttpSM::setup_server_send_request()
{
  int     hdr_length;
  int64_t msg_len = 0; /* lv: just make gcc happy */

  hsm_release_assert(server_entry != nullptr);
  hsm_release_assert(server_txn != nullptr);
  hsm_release_assert(server_entry->vc == server_txn);

  // Send the request header
  server_entry->vc_write_handler = &HttpSM::state_send_server_request_header;
  server_entry->write_buffer     = new_MIOBuffer(HTTP_HEADER_BUFFER_SIZE_INDEX);

  if (t_state.api_server_request_body_set) {
    msg_len = t_state.internal_msg_buffer_size;
    t_state.hdr_info.server_request.value_set_int64(static_cast<std::string_view>(MIME_FIELD_CONTENT_LENGTH), msg_len);
  }

  dump_header(dbg_ctl_http_hdrs, &(t_state.hdr_info.server_request), sm_id, "Proxy's Request after hooks");

  // We need a reader so bytes don't fall off the end of
  //  the buffer
  IOBufferReader *buf_start = server_entry->write_buffer->alloc_reader();
  server_request_hdr_bytes = hdr_length = write_header_into_buffer(&t_state.hdr_info.server_request, server_entry->write_buffer);

  // the plugin decided to append a message to the request
  if (t_state.api_server_request_body_set) {
    SMDbg(dbg_ctl_http, "appending msg of %" PRId64 " bytes to request %s", msg_len, t_state.internal_msg_buffer);
    hdr_length                += server_entry->write_buffer->write(t_state.internal_msg_buffer, msg_len);
    server_request_body_bytes  = msg_len;
  }

  ATS_PROBE1(milestone_server_begin_write, sm_id);
  milestones[TS_MILESTONE_SERVER_BEGIN_WRITE] = ink_get_hrtime();
  server_entry->write_vio                     = server_entry->vc->do_io_write(this, hdr_length, buf_start);

  // Make sure the VC is using correct timeouts.  We may be reusing a previously used server session
  server_txn->set_inactivity_timeout(get_server_inactivity_timeout());

  // Go on and set up the read response header too
  setup_server_read_response_header();
}

void
HttpSM::setup_server_read_response_header()
{
  ink_assert(server_txn != nullptr);
  ink_assert(server_entry != nullptr);
  // HttpRequestFlavor_t::SCHEDULED_UPDATE can be transformed in HttpRequestFlavor_t::REVPROXY
  ink_assert(_ua.get_txn() != nullptr || t_state.req_flavor == HttpTransact::HttpRequestFlavor_t::SCHEDULED_UPDATE ||
             t_state.req_flavor == HttpTransact::HttpRequestFlavor_t::REVPROXY);

  ink_assert(server_txn != nullptr && server_txn->get_remote_reader() != nullptr);

  SMDbg(dbg_ctl_http, "Setting up the header read");

  // Now that we've got the ability to read from the
  //  server, setup to read the response header
  server_entry->vc_read_handler = &HttpSM::state_read_server_response_header;
  server_entry->vc              = server_txn;

  t_state.current.state         = HttpTransact::STATE_UNDEFINED;
  t_state.current.server->state = HttpTransact::STATE_UNDEFINED;

  // Note: we must use destroy() here since clear()
  //  does not free the memory from the header
  t_state.hdr_info.server_response.destroy();
  t_state.hdr_info.server_response.create(HTTPType::RESPONSE);
  http_parser_clear(&http_parser);
  server_response_hdr_bytes                        = 0;
  milestones[TS_MILESTONE_SERVER_READ_HEADER_DONE] = 0;

  // The tunnel from OS to UA is now setup.  Ready to read the response
  server_entry->read_vio = server_txn->do_io_read(this, INT64_MAX, server_txn->get_remote_reader()->mbuf);

  // If there is anything in the buffer call the parsing routines
  //  since if the response is finished, we won't get any
  //  additional callbacks

  if (server_txn->get_remote_reader()->read_avail() > 0) {
    state_read_server_response_header((server_entry->eos) ? VC_EVENT_EOS : VC_EVENT_READ_READY, server_entry->read_vio);
  }
}

HttpTunnelProducer *
HttpSM::setup_cache_read_transfer()
{
  int64_t alloc_index, hdr_size;
  int64_t doc_size;

  ink_assert(cache_sm.cache_read_vc != nullptr);

  doc_size    = t_state.cache_info.object_read->object_size_get();
  alloc_index = buffer_size_to_index(doc_size + index_to_buffer_size(HTTP_HEADER_BUFFER_SIZE_INDEX),
                                     t_state.http_config_param->max_payload_iobuf_index);

#ifndef USE_NEW_EMPTY_MIOBUFFER
  MIOBuffer *buf = new_MIOBuffer(alloc_index);
#else
  MIOBuffer *buf = new_empty_MIOBuffer(alloc_index);
  buf->append_block(HTTP_HEADER_BUFFER_SIZE_INDEX);
#endif

  buf->water_mark = static_cast<int>(t_state.txn_conf->default_buffer_water_mark);

  IOBufferReader *buf_start = buf->alloc_reader();

  // Now dump the header into the buffer
  ink_assert(t_state.hdr_info.client_response.status_get() != HTTPStatus::NOT_MODIFIED);
  client_response_hdr_bytes = hdr_size = write_response_header_into_buffer(&t_state.hdr_info.client_response, buf);
  cache_response_hdr_bytes             = client_response_hdr_bytes;

  HTTP_SM_SET_DEFAULT_HANDLER(&HttpSM::tunnel_handler);

  if (doc_size != INT64_MAX) {
    doc_size += hdr_size;
  }

  HttpTunnelProducer *p = tunnel.add_producer(cache_sm.cache_read_vc, doc_size, buf_start, &HttpSM::tunnel_handler_cache_read,
                                              HttpTunnelType_t::CACHE_READ, "cache read");
  tunnel.add_consumer(_ua.get_entry()->vc, cache_sm.cache_read_vc, &HttpSM::tunnel_handler_ua, HttpTunnelType_t::HTTP_CLIENT,
                      "user agent");
  // if size of a cached item is not known, we'll do chunking for keep-alive HTTP/1.1 clients
  // this only applies to read-while-write cases where origin server sends a dynamically generated chunked content
  // w/o providing a Content-Length header
  if (t_state.client_info.receive_chunked_response) {
    bool const drop_chunked_trailers = t_state.http_config_param->oride.http_drop_chunked_trailers == 1;
    bool const parse_chunk_strictly  = t_state.http_config_param->oride.http_strict_chunk_parsing == 1;
    tunnel.set_producer_chunking_action(p, client_response_hdr_bytes, TunnelChunkingAction_t::CHUNK_CONTENT, drop_chunked_trailers,
                                        parse_chunk_strictly);
    tunnel.set_producer_chunking_size(p, t_state.txn_conf->http_chunking_size);
  }
  _ua.get_entry()->in_tunnel = true;
  cache_sm.cache_read_vc     = nullptr;
  return p;
}

HttpTunnelProducer *
HttpSM::setup_cache_transfer_to_transform()
{
  int64_t alloc_index;
  int64_t doc_size;

  ink_assert(cache_sm.cache_read_vc != nullptr);
  ink_assert(transform_info.vc != nullptr);
  ink_assert(transform_info.entry->vc == transform_info.vc);

  // grab this here
  cache_response_hdr_bytes = t_state.hdr_info.cache_response.length_get();

  doc_size                  = t_state.cache_info.object_read->object_size_get();
  alloc_index               = buffer_size_to_index(doc_size, t_state.http_config_param->max_payload_iobuf_index);
  MIOBuffer      *buf       = new_MIOBuffer(alloc_index);
  IOBufferReader *buf_start = buf->alloc_reader();

  HTTP_SM_SET_DEFAULT_HANDLER(&HttpSM::state_response_wait_for_transform_read);

  HttpTunnelProducer *p = tunnel.add_producer(cache_sm.cache_read_vc, doc_size, buf_start, &HttpSM::tunnel_handler_cache_read,
                                              HttpTunnelType_t::CACHE_READ, "cache read");

  tunnel.add_consumer(transform_info.vc, cache_sm.cache_read_vc, &HttpSM::tunnel_handler_transform_write,
                      HttpTunnelType_t::TRANSFORM, "transform write");
  transform_info.entry->in_tunnel = true;
  cache_sm.cache_read_vc          = nullptr;

  return p;
}

void
HttpSM::setup_cache_write_transfer(HttpCacheSM *c_sm, VConnection *source_vc, HTTPInfo *store_info, int64_t skip_bytes,
                                   const char *name)
{
  ink_assert(c_sm->cache_write_vc != nullptr);
  ink_assert(t_state.request_sent_time > 0);
  ink_assert(t_state.response_received_time > 0);

  store_info->request_sent_time_set(t_state.request_sent_time);
  store_info->response_received_time_set(t_state.response_received_time);

  c_sm->cache_write_vc->set_http_info(store_info);
  store_info->clear();

  tunnel.add_consumer(c_sm->cache_write_vc, source_vc, &HttpSM::tunnel_handler_cache_write, HttpTunnelType_t::CACHE_WRITE, name,
                      skip_bytes);

  c_sm->cache_write_vc = nullptr;
}

void
HttpSM::setup_100_continue_transfer()
{
  MIOBuffer      *buf       = new_MIOBuffer(HTTP_HEADER_BUFFER_SIZE_INDEX);
  IOBufferReader *buf_start = buf->alloc_reader();

  // First write the client response header into the buffer
  ink_assert(t_state.client_info.http_version != HTTP_0_9);
  client_response_hdr_bytes = write_header_into_buffer(&t_state.hdr_info.client_response, buf);
  ink_assert(client_response_hdr_bytes > 0);

  HTTP_SM_SET_DEFAULT_HANDLER(&HttpSM::tunnel_handler_100_continue);

  // Clear the decks before we set up new producers.  As things stand, we cannot have two static operators
  // at once
  tunnel.reset();

  // Setup the tunnel to the client
  HttpTunnelProducer *p =
    tunnel.add_producer(HTTP_TUNNEL_STATIC_PRODUCER, client_response_hdr_bytes, buf_start, (HttpProducerHandler) nullptr,
                        HttpTunnelType_t::STATIC, "internal msg - 100 continue");
  tunnel.add_consumer(_ua.get_entry()->vc, HTTP_TUNNEL_STATIC_PRODUCER, &HttpSM::tunnel_handler_100_continue_ua,
                      HttpTunnelType_t::HTTP_CLIENT, "user agent");

  // Make sure the half_close is not set.
  _ua.get_txn()->set_half_close_flag(false);
  _ua.get_entry()->in_tunnel = true;
  tunnel.tunnel_run(p);

  // Set up the header response read again.  Already processed the 100 response
  setup_server_read_response_header();
}

//////////////////////////////////////////////////////////////////////////
//
//  HttpSM::setup_error_transfer()
//
//  The proxy has generated an error message which it
//  is sending to the client. For some cases, however,
//  such as when the proxy is transparent, returning
//  a proxy-generated error message exposes the proxy,
//  destroying transparency. The HttpBodyFactory code,
//  therefore, does not generate an error message body
//  in such cases. This function checks for the presence
//  of an error body. If its not present, it closes the
//  connection to the user, else it simply calls
//  setup_write_proxy_internal, which is the standard
//  routine for setting up proxy-generated responses.
//
//////////////////////////////////////////////////////////////////////////
void
HttpSM::setup_error_transfer()
{
  if (body_factory->is_response_suppressed(&t_state) || t_state.internal_msg_buffer ||
      is_response_body_precluded(t_state.http_return_code)) {
    // Since we need to send the error message, call the API
    //   function
    ink_assert(t_state.internal_msg_buffer_size > 0 || is_response_body_precluded(t_state.http_return_code));
    t_state.api_next_action = HttpTransact::StateMachineAction_t::API_SEND_RESPONSE_HDR;
    do_api_callout();
  } else {
    SMDbg(dbg_ctl_http, "Now closing connection ...");
    vc_table.cleanup_entry(_ua.get_entry());
    _ua.set_entry(nullptr);
    // _ua.get_txn()     = NULL;
    terminate_sm   = true;
    t_state.source = HttpTransact::Source_t::INTERNAL;
  }
}

void
HttpSM::setup_internal_transfer(HttpSMHandler handler_arg)
{
  bool is_msg_buf_present;

  if (t_state.internal_msg_buffer) {
    is_msg_buf_present = true;
    ink_assert(t_state.internal_msg_buffer_size > 0);

    // Set the content length here since a plugin
    //   may have changed the error body
    t_state.hdr_info.client_response.set_content_length(t_state.internal_msg_buffer_size);
    t_state.hdr_info.client_response.field_delete(static_cast<std::string_view>(MIME_FIELD_TRANSFER_ENCODING));

    // set internal_msg_buffer_type if available
    if (t_state.internal_msg_buffer_type) {
      int len = strlen(t_state.internal_msg_buffer_type);

      if (len > 0) {
        t_state.hdr_info.client_response.value_set(
          static_cast<std::string_view>(MIME_FIELD_CONTENT_TYPE),
          std::string_view{t_state.internal_msg_buffer_type, static_cast<std::string_view::size_type>(len)});
      }
      ats_free(t_state.internal_msg_buffer_type);
      t_state.internal_msg_buffer_type = nullptr;
    } else {
      t_state.hdr_info.client_response.value_set(static_cast<std::string_view>(MIME_FIELD_CONTENT_TYPE), "text/html"sv);
    }
  } else {
    is_msg_buf_present = false;

    // If we are sending a response that can have a body
    //   but doesn't have a body add a content-length of zero.
    //   Needed for keep-alive on PURGE requests
    if (!is_response_body_precluded(t_state.hdr_info.client_response.status_get(), t_state.method)) {
      t_state.hdr_info.client_response.set_content_length(0);
      t_state.hdr_info.client_response.field_delete(static_cast<std::string_view>(MIME_FIELD_TRANSFER_ENCODING));
    }
  }

  t_state.source = HttpTransact::Source_t::INTERNAL;

  int64_t buf_size =
    index_to_buffer_size(HTTP_HEADER_BUFFER_SIZE_INDEX) + (is_msg_buf_present ? t_state.internal_msg_buffer_size : 0);

  MIOBuffer      *buf       = new_MIOBuffer(buffer_size_to_index(buf_size, t_state.http_config_param->max_payload_iobuf_index));
  IOBufferReader *buf_start = buf->alloc_reader();

  // First write the client response header into the buffer
  client_response_hdr_bytes = write_response_header_into_buffer(&t_state.hdr_info.client_response, buf);
  int64_t nbytes            = client_response_hdr_bytes;

  // Next append the message onto the MIOBuffer

  // From HTTP/1.1 RFC:
  // "The HEAD method is identical to GET except that the server
  // MUST NOT return a message-body in the response. The metainformation
  // in the HTTP headers in response to a HEAD request SHOULD be
  // identical to the information sent in response to a GET request."
  // --> do not append the message onto the MIOBuffer and keep our pointer
  // to it so that it can be freed.

  if (is_msg_buf_present && t_state.method != HTTP_WKSIDX_HEAD) {
    nbytes += t_state.internal_msg_buffer_size;

    if (t_state.internal_msg_buffer_fast_allocator_size < 0) {
      buf->append_xmalloced(t_state.internal_msg_buffer, t_state.internal_msg_buffer_size);
    } else {
      buf->append_fast_allocated(t_state.internal_msg_buffer, t_state.internal_msg_buffer_size,
                                 t_state.internal_msg_buffer_fast_allocator_size);
    }

    // The IOBufferBlock will free the msg buffer when necessary so
    //  eliminate our pointer to it
    t_state.internal_msg_buffer      = nullptr;
    t_state.internal_msg_buffer_size = 0;
  }

  HTTP_SM_SET_DEFAULT_HANDLER(handler_arg);

  if (_ua.get_entry() && _ua.get_entry()->vc) {
    // Clear the decks before we setup the new producers
    // As things stand, we cannot have two static producers operating at
    // once
    tunnel.reset();

    // Setup the tunnel to the client
    HttpTunnelProducer *p = tunnel.add_producer(HTTP_TUNNEL_STATIC_PRODUCER, nbytes, buf_start, (HttpProducerHandler) nullptr,
                                                HttpTunnelType_t::STATIC, "internal msg");
    tunnel.add_consumer(_ua.get_entry()->vc, HTTP_TUNNEL_STATIC_PRODUCER, &HttpSM::tunnel_handler_ua, HttpTunnelType_t::HTTP_CLIENT,
                        "user agent");

    _ua.get_entry()->in_tunnel = true;
    tunnel.tunnel_run(p);
  } else {
    (this->*default_handler)(HTTP_TUNNEL_EVENT_DONE, &tunnel);
  }
}

// int HttpSM::find_http_resp_buffer_size(int cl)
//
//   Returns the allocation index for the buffer for
//     a response based on the content length
//
int
HttpSM::find_http_resp_buffer_size(int64_t content_length)
{
  int64_t alloc_index;

  if (content_length == HTTP_UNDEFINED_CL) {
    // Try use our configured default size.  Otherwise pick
    //   the default size
    alloc_index = static_cast<int>(t_state.txn_conf->default_buffer_size_index);
    if (alloc_index < MIN_CONFIG_BUFFER_SIZE_INDEX || alloc_index > DEFAULT_MAX_BUFFER_SIZE) {
      alloc_index = DEFAULT_RESPONSE_BUFFER_SIZE_INDEX;
    }
  } else {
    int64_t buf_size = index_to_buffer_size(HTTP_HEADER_BUFFER_SIZE_INDEX) + content_length;
    alloc_index      = buffer_size_to_index(buf_size, t_state.http_config_param->max_payload_iobuf_index);
  }

  return alloc_index;
}

// int HttpSM::server_transfer_init()
//
//    Moves data from the header buffer into the reply buffer
//      and return the number of bytes we should use for initiating the
//      tunnel
//
int64_t
HttpSM::server_transfer_init(MIOBuffer *buf, int hdr_size)
{
  int64_t nbytes;
  int64_t to_copy = INT64_MAX;

  ink_assert(t_state.current.server != nullptr); // should have been set up if we're doing a transfer.

  if (server_entry->eos == true) {
    // The server has shutdown on us already so the only data
    //  we'll get is already in the buffer
    nbytes = server_txn->get_remote_reader()->read_avail() + hdr_size;
  } else if (t_state.hdr_info.response_content_length == HTTP_UNDEFINED_CL) {
    // Chunked or otherwise, no length is defined. Pass -1 to tell the
    // tunnel that the size is unknown.
    nbytes = -1;
  } else {
    //  Set to copy to the number of bytes we want to write as
    //  if the server is sending us a bogus response we have to
    //  truncate it as we've already decided to trust the content
    //  length
    to_copy = t_state.hdr_info.response_content_length;
    nbytes  = t_state.hdr_info.response_content_length + hdr_size;
  }

  // Next order of business if copy the remaining data from the header buffer into new buffer.
  int64_t server_response_pre_read_bytes = buf->write(server_txn->get_remote_reader(), to_copy);
  server_txn->get_remote_reader()->consume(server_response_pre_read_bytes);

  //  If we know the length & copied the entire body
  //   of the document out of the header buffer make
  //   sure the server isn't screwing us by having sent too
  //   much.  If it did, we want to close the server connection
  if (server_response_pre_read_bytes == to_copy && server_txn->get_remote_reader()->read_avail() > 0) {
    t_state.current.server->keep_alive = HTTPKeepAlive::NO_KEEPALIVE;
  }

  return nbytes;
}

HttpTunnelProducer *
HttpSM::setup_server_transfer_to_transform()
{
  int64_t alloc_index;
  int64_t nbytes;

  alloc_index               = find_server_buffer_size();
  MIOBuffer      *buf       = new_MIOBuffer(alloc_index);
  IOBufferReader *buf_start = buf->alloc_reader();
  nbytes                    = server_transfer_init(buf, 0);

  HTTP_SM_SET_DEFAULT_HANDLER(&HttpSM::state_response_wait_for_transform_read);

  HttpTunnelProducer *p = tunnel.add_producer(server_entry->vc, nbytes, buf_start, &HttpSM::tunnel_handler_server,
                                              HttpTunnelType_t::HTTP_SERVER, "http server");

  tunnel.add_consumer(transform_info.vc, server_entry->vc, &HttpSM::tunnel_handler_transform_write, HttpTunnelType_t::TRANSFORM,
                      "transform write");

  server_entry->in_tunnel         = true;
  transform_info.entry->in_tunnel = true;

  if (t_state.current.server->transfer_encoding == HttpTransact::TransferEncoding_t::CHUNKED) {
    client_response_hdr_bytes       = 0; // fixed by YTS Team, yamsat
    bool const parse_chunk_strictly = t_state.http_config_param->oride.http_strict_chunk_parsing == 1;
    tunnel.set_producer_chunking_action(p, client_response_hdr_bytes, TunnelChunkingAction_t::DECHUNK_CONTENT,
                                        HttpTunnel::DROP_CHUNKED_TRAILERS, parse_chunk_strictly);
  }

  return p;
}

HttpTunnelProducer *
HttpSM::setup_transfer_from_transform()
{
  int64_t alloc_index = find_server_buffer_size();

  // TODO change this call to new_empty_MIOBuffer()
  MIOBuffer *buf            = new_MIOBuffer(alloc_index);
  buf->water_mark           = static_cast<int>(t_state.txn_conf->default_buffer_water_mark);
  IOBufferReader *buf_start = buf->alloc_reader();

  HttpTunnelConsumer *c = tunnel.get_consumer(transform_info.vc);
  ink_assert(c != nullptr);
  ink_assert(c->vc == transform_info.vc);
  ink_assert(c->vc_type == HttpTunnelType_t::TRANSFORM);

  // Now dump the header into the buffer
  ink_assert(t_state.hdr_info.client_response.status_get() != HTTPStatus::NOT_MODIFIED);
  client_response_hdr_bytes = write_response_header_into_buffer(&t_state.hdr_info.client_response, buf);

  HTTP_SM_SET_DEFAULT_HANDLER(&HttpSM::tunnel_handler);

  HttpTunnelProducer *p = tunnel.add_producer(transform_info.vc, INT64_MAX, buf_start, &HttpSM::tunnel_handler_transform_read,
                                              HttpTunnelType_t::TRANSFORM, "transform read");
  tunnel.chain(c, p);

  tunnel.add_consumer(_ua.get_entry()->vc, transform_info.vc, &HttpSM::tunnel_handler_ua, HttpTunnelType_t::HTTP_CLIENT,
                      "user agent");

  transform_info.entry->in_tunnel = true;
  _ua.get_entry()->in_tunnel      = true;

  this->setup_client_response_plugin_agents(p, client_response_hdr_bytes);

  if (t_state.client_info.receive_chunked_response) {
    bool const drop_chunked_trailers = t_state.http_config_param->oride.http_drop_chunked_trailers == 1;
    bool const parse_chunk_strictly  = t_state.http_config_param->oride.http_strict_chunk_parsing == 1;
    tunnel.set_producer_chunking_action(p, client_response_hdr_bytes, TunnelChunkingAction_t::CHUNK_CONTENT, drop_chunked_trailers,
                                        parse_chunk_strictly);
    tunnel.set_producer_chunking_size(p, t_state.txn_conf->http_chunking_size);
  }

  return p;
}

HttpTunnelProducer *
HttpSM::setup_server_transfer()
{
  SMDbg(dbg_ctl_http, "Setup Server Transfer");
  int64_t alloc_index, hdr_size;
  int64_t nbytes;

  alloc_index = find_server_buffer_size();
#ifndef USE_NEW_EMPTY_MIOBUFFER
  MIOBuffer *buf = new_MIOBuffer(alloc_index);
#else
  MIOBuffer *buf = new_empty_MIOBuffer(alloc_index);
  buf->append_block(HTTP_HEADER_BUFFER_SIZE_INDEX);
#endif
  buf->water_mark           = static_cast<int>(t_state.txn_conf->default_buffer_water_mark);
  IOBufferReader *buf_start = buf->alloc_reader();

  // we need to know if we are going to chunk the response or not
  // before we write the response header into buffer
  TunnelChunkingAction_t action;
  if (t_state.client_info.receive_chunked_response == false) {
    if (t_state.current.server->transfer_encoding == HttpTransact::TransferEncoding_t::CHUNKED) {
      action = TunnelChunkingAction_t::DECHUNK_CONTENT;
    } else {
      action = TunnelChunkingAction_t::PASSTHRU_DECHUNKED_CONTENT;
    }
  } else {
    if (t_state.current.server->transfer_encoding != HttpTransact::TransferEncoding_t::CHUNKED) {
      if (t_state.client_info.http_version == HTTP_0_9) {
        action = TunnelChunkingAction_t::PASSTHRU_DECHUNKED_CONTENT; // send as-is
      } else {
        action = TunnelChunkingAction_t::CHUNK_CONTENT;
      }
    } else {
      action = TunnelChunkingAction_t::PASSTHRU_CHUNKED_CONTENT;
    }
  }
  if (action == TunnelChunkingAction_t::CHUNK_CONTENT ||
      action == TunnelChunkingAction_t::PASSTHRU_CHUNKED_CONTENT) { // remove Content-Length
    t_state.hdr_info.client_response.field_delete(static_cast<std::string_view>(MIME_FIELD_CONTENT_LENGTH));
  }
  // Now dump the header into the buffer
  ink_assert(t_state.hdr_info.client_response.status_get() != HTTPStatus::NOT_MODIFIED);
  client_response_hdr_bytes = hdr_size = write_response_header_into_buffer(&t_state.hdr_info.client_response, buf);

  nbytes = server_transfer_init(buf, hdr_size);

  HTTP_SM_SET_DEFAULT_HANDLER(&HttpSM::tunnel_handler);

  HttpTunnelProducer *p = tunnel.add_producer(server_entry->vc, nbytes, buf_start, &HttpSM::tunnel_handler_server,
                                              HttpTunnelType_t::HTTP_SERVER, "http server");

  tunnel.add_consumer(_ua.get_entry()->vc, server_entry->vc, &HttpSM::tunnel_handler_ua, HttpTunnelType_t::HTTP_CLIENT,
                      "user agent");

  _ua.get_entry()->in_tunnel = true;
  server_entry->in_tunnel    = true;

  this->setup_client_response_plugin_agents(p, client_response_hdr_bytes);

  bool const drop_chunked_trailers = t_state.http_config_param->oride.http_drop_chunked_trailers == 1;
  bool const parse_chunk_strictly  = t_state.http_config_param->oride.http_strict_chunk_parsing == 1;
  tunnel.set_producer_chunking_action(p, client_response_hdr_bytes, action, drop_chunked_trailers, parse_chunk_strictly);
  tunnel.set_producer_chunking_size(p, t_state.txn_conf->http_chunking_size);
  return p;
}

HttpTunnelProducer *
HttpSM::setup_push_transfer_to_cache()
{
  int64_t nbytes, alloc_index;

  alloc_index               = find_http_resp_buffer_size(t_state.hdr_info.request_content_length);
  MIOBuffer      *buf       = new_MIOBuffer(alloc_index);
  IOBufferReader *buf_start = buf->alloc_reader();

  ink_release_assert(t_state.hdr_info.request_content_length != HTTP_UNDEFINED_CL);
  nbytes = t_state.hdr_info.request_content_length - pushed_response_hdr_bytes;
  ink_release_assert(nbytes >= 0);

  if (_ua.get_entry()->eos == true) {
    // The ua has shutdown on us already so the only data
    //  we'll get is already in the buffer.  Make sure it
    //  fulfills the stated length
    int64_t avail = _ua.get_txn()->get_remote_reader()->read_avail();

    if (avail < nbytes) {
      // Client failed to send the body, it's gone.  Kill the
      // state machine
      terminate_sm = true;
      return nullptr;
    }
  }
  // Next order of business is copy the remaining data from the
  //  header buffer into new buffer.
  pushed_response_body_bytes = buf->write(_ua.get_txn()->get_remote_reader(), nbytes);
  _ua.get_txn()->get_remote_reader()->consume(pushed_response_body_bytes);
  client_request_body_bytes += pushed_response_body_bytes;

  HTTP_SM_SET_DEFAULT_HANDLER(&HttpSM::tunnel_handler_push);

  HttpTunnelProducer *p = tunnel.add_producer(_ua.get_entry()->vc, nbytes, buf_start, &HttpSM::tunnel_handler_ua_push,
                                              HttpTunnelType_t::HTTP_CLIENT, "user_agent");
  setup_cache_write_transfer(&cache_sm, _ua.get_entry()->vc, &t_state.cache_info.object_store, 0, "cache write");

  _ua.get_entry()->in_tunnel = true;
  return p;
}

void
HttpSM::setup_blind_tunnel(bool send_response_hdr, IOBufferReader *initial)
{
  ink_assert(server_entry->vc != nullptr);

  HttpTunnelConsumer *c_ua;
  HttpTunnelConsumer *c_os;
  HttpTunnelProducer *p_ua;
  HttpTunnelProducer *p_os;
  MIOBuffer          *from_ua_buf = new_MIOBuffer(BUFFER_SIZE_INDEX_32K);
  MIOBuffer          *to_ua_buf   = new_MIOBuffer(BUFFER_SIZE_INDEX_32K);
  IOBufferReader     *r_from      = from_ua_buf->alloc_reader();
  IOBufferReader     *r_to        = to_ua_buf->alloc_reader();

  ATS_PROBE1(milestone_server_begin_write, sm_id);
  milestones[TS_MILESTONE_SERVER_BEGIN_WRITE] = ink_get_hrtime();
  if (send_response_hdr) {
    client_response_hdr_bytes = write_response_header_into_buffer(&t_state.hdr_info.client_response, to_ua_buf);
    if (initial && initial->read_avail()) {
      int64_t avail = initial->read_avail();
      to_ua_buf->write(initial, avail);
      initial->consume(avail);
    }
  } else {
    client_response_hdr_bytes = 0;
  }

  int64_t nbytes = 0;
  if (t_state.txn_conf->proxy_protocol_out >= 0) {
    nbytes = do_outbound_proxy_protocol(from_ua_buf, static_cast<NetVConnection *>(server_entry->vc), _ua.get_txn()->get_netvc(),
                                        t_state.txn_conf->proxy_protocol_out);
  }

  client_request_body_bytes = nbytes;
  if (_ua.get_raw_buffer_reader() != nullptr) {
    client_request_body_bytes += from_ua_buf->write(_ua.get_raw_buffer_reader(), client_request_hdr_bytes);
    _ua.get_raw_buffer_reader()->dealloc();
    _ua.set_raw_buffer_reader(nullptr);
  }

  // if pre-warmed connection is used and it has data from origin server, forward it to ua
  if (_prewarm_sm && _prewarm_sm->has_data_from_origin_server()) {
    ink_release_assert(_prewarm_sm->handler == &PreWarmSM::state_closed);
    client_response_hdr_bytes += to_ua_buf->write(_prewarm_sm->server_buf_reader());
  }

  // Next order of business if copy the remaining data from the
  //  header buffer into new buffer
  client_request_body_bytes += from_ua_buf->write(_ua.get_txn()->get_remote_reader());

  HTTP_SM_SET_DEFAULT_HANDLER(&HttpSM::tunnel_handler);

  this->do_transform_open();
  this->do_post_transform_open();

  p_os = tunnel.add_producer(server_entry->vc, -1, r_to, &HttpSM::tunnel_handler_ssl_producer, HttpTunnelType_t::HTTP_SERVER,
                             "http server - tunnel");

  if (this->transform_info.vc != nullptr) {
    HttpTunnelConsumer *c_trans = tunnel.add_consumer(transform_info.vc, server_entry->vc, &HttpSM::tunnel_handler_transform_write,
                                                      HttpTunnelType_t::TRANSFORM, "server tunnel - transform");
    MIOBuffer          *trans_buf = new_MIOBuffer(BUFFER_SIZE_INDEX_32K);
    IOBufferReader     *trans_to  = trans_buf->alloc_reader();
    HttpTunnelProducer *p_trans   = tunnel.add_producer(transform_info.vc, -1, trans_to, &HttpSM::tunnel_handler_transform_read,
                                                        HttpTunnelType_t::TRANSFORM, "server tunnel - transform");
    c_ua = tunnel.add_consumer(_ua.get_entry()->vc, transform_info.vc, &HttpSM::tunnel_handler_ssl_consumer,
                               HttpTunnelType_t::HTTP_CLIENT, "user agent - tunnel");
    tunnel.chain(c_trans, p_trans);
    transform_info.entry->in_tunnel = true;
  } else {
    c_ua = tunnel.add_consumer(_ua.get_entry()->vc, server_entry->vc, &HttpSM::tunnel_handler_ssl_consumer,
                               HttpTunnelType_t::HTTP_CLIENT, "user agent - tunnel");
  }

  p_ua = tunnel.add_producer(_ua.get_entry()->vc, -1, r_from, &HttpSM::tunnel_handler_ssl_producer, HttpTunnelType_t::HTTP_CLIENT,
                             "user agent - tunnel");

  if (this->post_transform_info.vc != nullptr) {
    HttpTunnelConsumer *c_trans =
      tunnel.add_consumer(post_transform_info.vc, _ua.get_entry()->vc, &HttpSM::tunnel_handler_transform_write,
                          HttpTunnelType_t::TRANSFORM, "ua tunnel - transform");
    MIOBuffer          *trans_buf = new_MIOBuffer(BUFFER_SIZE_INDEX_32K);
    IOBufferReader     *trans_to  = trans_buf->alloc_reader();
    HttpTunnelProducer *p_trans = tunnel.add_producer(post_transform_info.vc, -1, trans_to, &HttpSM::tunnel_handler_transform_read,
                                                      HttpTunnelType_t::TRANSFORM, "ua tunnel - transform");
    c_os = tunnel.add_consumer(server_entry->vc, post_transform_info.vc, &HttpSM::tunnel_handler_ssl_consumer,
                               HttpTunnelType_t::HTTP_SERVER, "http server - tunnel");
    tunnel.chain(c_trans, p_trans);
    post_transform_info.entry->in_tunnel = true;
  } else {
    c_os = tunnel.add_consumer(server_entry->vc, _ua.get_entry()->vc, &HttpSM::tunnel_handler_ssl_consumer,
                               HttpTunnelType_t::HTTP_SERVER, "http server - tunnel");
  }

  _ua.get_entry()->vc->mark_as_tunnel_endpoint();
  server_entry->vc->mark_as_tunnel_endpoint();

  // Make the tunnel aware that the entries are bi-directional
  tunnel.chain(c_os, p_os);
  tunnel.chain(c_ua, p_ua);

  _ua.get_entry()->in_tunnel = true;
  server_entry->in_tunnel    = true;

  tunnel.tunnel_run();

  // If we're half closed, we got a FIN from the client. Forward it on to the origin server
  // now that we have the tunnel operational.
  if (_ua.get_txn() && _ua.get_txn()->get_half_close_flag()) {
    p_ua->vc->do_io_shutdown(IO_SHUTDOWN_READ);
  }
}

void
HttpSM::setup_client_response_plugin_agents(HttpTunnelProducer *p, int num_header_bytes)
{
  APIHook *agent                    = txn_hook_get(TS_HTTP_RESPONSE_CLIENT_HOOK);
  has_active_response_plugin_agents = agent != nullptr;
  while (agent) {
    INKVConnInternal *contp = static_cast<INKVConnInternal *>(agent->m_cont);
    tunnel.add_consumer(contp, p->vc, &HttpSM::tunnel_handler_plugin_agent, HttpTunnelType_t::HTTP_CLIENT, "response plugin agent",
                        num_header_bytes);
    // We don't put these in the SM VC table because the tunnel
    // will clean them up in do_io_close().
    agent = agent->next();
  }
}

void
HttpSM::setup_client_request_plugin_agents(HttpTunnelProducer *p, int num_header_bytes)
{
  APIHook *agent                   = txn_hook_get(TS_HTTP_REQUEST_CLIENT_HOOK);
  has_active_request_plugin_agents = agent != nullptr;
  while (agent) {
    INKVConnInternal *contp = static_cast<INKVConnInternal *>(agent->m_cont);
    tunnel.add_consumer(contp, p->vc, &HttpSM::tunnel_handler_plugin_agent, HttpTunnelType_t::HTTP_CLIENT, "request plugin agent",
                        num_header_bytes);
    // We don't put these in the SM VC table because the tunnel
    // will clean them up in do_io_close().
    agent = agent->next();
  }
}

inline void
HttpSM::transform_cleanup(TSHttpHookID hook, HttpTransformInfo *info)
{
  APIHook *t_hook = api_hooks.get(hook);
  if (t_hook && info->vc == nullptr) {
    do {
      VConnection *t_vcon = t_hook->m_cont;
      t_vcon->do_io_close();
      t_hook = t_hook->m_link.next;
    } while (t_hook != nullptr);
  }
}

void
HttpSM::plugin_agents_cleanup()
{
  // If this is set then all of the plugin agent VCs were put in
  // the VC table and cleaned up there. This handles the case where
  // something went wrong early.
  if (!has_active_response_plugin_agents) {
    APIHook *agent = txn_hook_get(TS_HTTP_RESPONSE_CLIENT_HOOK);
    while (agent) {
      INKVConnInternal *contp = static_cast<INKVConnInternal *>(agent->m_cont);
      contp->do_io_close();
      agent = agent->next();
    }
  }
  if (!has_active_request_plugin_agents) {
    APIHook *agent = txn_hook_get(TS_HTTP_REQUEST_CLIENT_HOOK);
    while (agent) {
      INKVConnInternal *contp = static_cast<INKVConnInternal *>(agent->m_cont);
      contp->do_io_close();
      agent = agent->next();
    }
  }
}

//////////////////////////////////////////////////////////////////////////
//
//  HttpSM::kill_this()
//
//  This function has two phases.  One before we call the asynchronous
//    clean up routines (api and list removal) and one after.
//    The state about which phase we are in is kept in
//    HttpSM::kill_this_async_done
//
//////////////////////////////////////////////////////////////////////////
void
HttpSM::kill_this()
{
  ink_release_assert(reentrancy_count == 1);
  this->postbuf_clear();
  enable_redirection = false;

  if (kill_this_async_done == false) {
    ////////////////////////////////
    // cancel uncompleted actions //
    ////////////////////////////////
    // The action should be cancelled only if
    // the state machine is in HttpApiState_t::NO_CALLOUT
    // state. This is because we are depending on the
    // callout to complete for the state machine to
    // get killed.
    if (callout_state == HttpApiState_t::NO_CALLOUT && !pending_action.empty()) {
      pending_action = nullptr;
    } else if (!pending_action.empty()) {
      ink_assert(pending_action.empty());
    }

    cache_sm.end_both();
    transform_cache_sm.end_both();
    vc_table.cleanup_all();

    // Clean up the tunnel resources. Take
    // it down if it is still active
    tunnel.kill_tunnel();

    if (_netvc) {
      _netvc->do_io_close();
      free_MIOBuffer(_netvc_read_buffer);
    } else if (server_txn == nullptr) {
      this->cancel_pending_server_connection();
    }

    // It possible that a plugin added transform hook
    //   but the hook never executed due to a client abort
    //   In that case, we need to manually close all the
    //   transforms to prevent memory leaks (INKqa06147)
    if (hooks_set) {
      transform_cleanup(TS_HTTP_RESPONSE_TRANSFORM_HOOK, &transform_info);
      transform_cleanup(TS_HTTP_REQUEST_TRANSFORM_HOOK, &post_transform_info);
      plugin_agents_cleanup();
    }
    // It's also possible that the plugin_tunnel vc was never
    //   executed due to not contacting the server
    if (plugin_tunnel) {
      plugin_tunnel->kill_no_connect();
      plugin_tunnel = nullptr;
    }

    // So we don't try to nuke the state machine
    //  if the plugin receives event we must reset
    //  the terminate_flag
    terminate_sm            = false;
    t_state.api_next_action = HttpTransact::StateMachineAction_t::API_SM_SHUTDOWN;
    if (do_api_callout() < 0) { // Failed to get a continuation lock
      // Need to hang out until we can complete the TXN_CLOSE hook
      terminate_sm = false;
      reentrancy_count--;
      return;
    }
  }
  // The reentrancy_count is still valid up to this point since
  //   the api shutdown hook is asynchronous and double frees can
  //   happen if the reentrancy count is not still valid until
  //   after all asynch callouts have completed
  //
  // Once we get to this point, we could be waiting for async
  //   completion in which case we need to decrement the reentrancy
  //   count since the entry points can't do it for us since they
  //   don't know if the state machine has been destroyed.  In the
  //   case we really are done with asynch callouts, decrement the
  //   reentrancy count since it seems tacky to destruct a state
  //   machine with non-zero count
  reentrancy_count--;
  ink_release_assert(reentrancy_count == 0);

  // If the api shutdown & list removal was synchronous
  //   then the value of kill_this_async_done has changed so
  //   we must check it again
  if (kill_this_async_done == true) {
    pending_action = nullptr;
    if (t_state.http_config_param->enable_http_stats) {
      update_stats();
    }

    //////////////
    // Log Data //
    //////////////
    SMDbg(dbg_ctl_http_seq, "Logging transaction");
    if (Log::transaction_logging_enabled() && t_state.api_info.logging_enabled) {
      LogAccess accessor(this);

      int ret = Log::access(&accessor);

      if (ret & Log::FULL) {
        SMDbg(dbg_ctl_http, "Logging system indicates FULL.");
      }
      if (ret & Log::FAIL) {
        Log::error("failed to log transaction for at least one log object");
      }
    }

    if (server_txn) {
      server_txn->transaction_done();
      server_txn = nullptr;
    }
    if (_ua.get_txn()) {
      if (_ua.get_txn()->get_server_session() != nullptr) {
        _ua.get_txn()->attach_server_session(nullptr);
      }
      _ua.get_txn()->transaction_done();
    }

    // In the async state, the plugin could have been
    // called resulting in the creation of a plugin_tunnel.
    // So it needs to be deleted now.
    if (plugin_tunnel) {
      plugin_tunnel->kill_no_connect();
      plugin_tunnel = nullptr;
    }

    ink_assert(pending_action.empty());
    ink_release_assert(vc_table.is_table_clear() == true);
    ink_release_assert(tunnel.is_tunnel_active() == false);

    HTTP_SM_SET_DEFAULT_HANDLER(nullptr);

    ats_free(redirect_url);
    redirect_url     = nullptr;
    redirect_url_len = 0;

#ifdef USE_HTTP_DEBUG_LISTS
    ink_mutex_acquire(&debug_sm_list_mutex);
    debug_sm_list.remove(this);
    ink_mutex_release(&debug_sm_list_mutex);
#endif

    SMDbg(dbg_ctl_http, "deallocating sm");
    destroy();
  }
}

void
HttpSM::update_stats()
{
  ATS_PROBE1(milestone_sm_finish, sm_id);
  milestones[TS_MILESTONE_SM_FINISH] = ink_get_hrtime();

  if (is_action_tag_set("bad_length_state_dump")) {
    if (t_state.hdr_info.client_response.valid() && t_state.hdr_info.client_response.status_get() == HTTPStatus::OK) {
      int64_t p_resp_cl = t_state.hdr_info.client_response.get_content_length();
      int64_t resp_size = client_response_body_bytes;
      if (!((p_resp_cl == -1 || p_resp_cl == resp_size || resp_size == 0))) {
        Error("[%" PRId64 "] Truncated content detected", sm_id);
        dump_state_on_assert();
      }
    } else if (client_request_hdr_bytes == 0) {
      Error("[%" PRId64 "] Zero length request header received", sm_id);
      dump_state_on_assert();
    }
  }

  ink_hrtime total_time = milestones.elapsed(TS_MILESTONE_SM_START, TS_MILESTONE_SM_FINISH);

  // ua_close will not be assigned properly in some exceptional situation.
  // TODO: Assign ua_close with suitable value when HttpTunnel terminates abnormally.
  if (milestones[TS_MILESTONE_UA_CLOSE] == 0 && milestones[TS_MILESTONE_UA_READ_HEADER_DONE] > 0) {
    ATS_PROBE1(milestone_ua_close, sm_id);
    milestones[TS_MILESTONE_UA_CLOSE] = ink_get_hrtime();
  }

  // request_process_time  = The time after the header is parsed to the completion of the transaction
  ink_hrtime request_process_time = milestones[TS_MILESTONE_UA_CLOSE] - milestones[TS_MILESTONE_UA_READ_HEADER_DONE];

  HttpTransact::client_result_stat(&t_state, total_time, request_process_time);

  ink_hrtime ua_write_time;
  if (milestones[TS_MILESTONE_UA_BEGIN_WRITE] != 0 && milestones[TS_MILESTONE_UA_CLOSE] != 0) {
    ua_write_time = milestones.elapsed(TS_MILESTONE_UA_BEGIN_WRITE, TS_MILESTONE_UA_CLOSE);
  } else {
    ua_write_time = -1;
  }

  ink_hrtime os_read_time;
  if (milestones[TS_MILESTONE_SERVER_READ_HEADER_DONE] != 0 && milestones[TS_MILESTONE_SERVER_CLOSE] != 0) {
    os_read_time = milestones.elapsed(TS_MILESTONE_SERVER_READ_HEADER_DONE, TS_MILESTONE_SERVER_CLOSE);
  } else {
    os_read_time = -1;
  }

  HttpTransact::update_size_and_time_stats(
    &t_state, total_time, ua_write_time, os_read_time, client_request_hdr_bytes, client_request_body_bytes,
    client_response_hdr_bytes, client_response_body_bytes, server_request_hdr_bytes, server_request_body_bytes,
    server_response_hdr_bytes, server_response_body_bytes, pushed_response_hdr_bytes, pushed_response_body_bytes, milestones);
  /*
      if (is_action_tag_set("http_handler_times")) {
          print_all_http_handler_times();
      }
  */

  // print slow requests if the threshold is set (> 0) and if we are over the time threshold
  if (t_state.txn_conf->slow_log_threshold != 0 && ink_hrtime_from_msec(t_state.txn_conf->slow_log_threshold) < total_time) {
    char url_string[256] = "";
    int  offset          = 0;
    int  skip            = 0;

    t_state.hdr_info.client_request.url_print(url_string, sizeof(url_string) - 1, &offset, &skip);
    url_string[offset] = 0; // NULL terminate the string

    // unique id
    char unique_id_string[128] = "";
    if (auto field{t_state.hdr_info.client_request.value_get(static_cast<std::string_view>(MIME_FIELD_X_ID))}; !field.empty()) {
      auto length{std::min(field.length(), sizeof(unique_id_string) - 1)};
      memcpy(unique_id_string, field.data(), length);
      unique_id_string[length] = 0; // NULL terminate the string
    }

    // set the fd for the request
    int             fd = 0;
    NetVConnection *vc = nullptr;
    if (_ua.get_txn() != nullptr) {
      vc = _ua.get_txn()->get_netvc();
      if (vc != nullptr) {
        fd = vc->get_socket();
      } else {
        fd = -1;
      }
    }
    // get the status code, lame that we have to check to see if it is valid or we will assert in the method call
    int status = 0;
    if (t_state.hdr_info.client_response.valid()) {
      status = static_cast<int>(t_state.hdr_info.client_response.status_get());
    }
    char client_ip[INET6_ADDRSTRLEN];
    ats_ip_ntop(&t_state.client_info.src_addr, client_ip, sizeof(client_ip));
    Error("[%" PRId64 "] Slow Request: "
          "client_ip: %s:%u "
          "protocol: %s "
          "url: %s "
          "status: %d "
          "unique id: %s "
          "redirection_tries: %d "
          "bytes: %" PRId64 " "
          "fd: %d "
          "client state: %d "
          "server state: %d "
          "tls_handshake: %.3f "
          "ua_begin: %.3f "
          "ua_first_read: %.3f "
          "ua_read_header_done: %.3f "
          "cache_open_read_begin: %.3f "
          "cache_open_read_end: %.3f "
          "cache_open_write_begin: %.3f "
          "cache_open_write_end: %.3f "
          "dns_lookup_begin: %.3f "
          "dns_lookup_end: %.3f "
          "server_connect: %.3f "
          "server_connect_end: %.3f "
          "server_first_read: %.3f "
          "server_read_header_done: %.3f "
          "server_close: %.3f "
          "ua_write: %.3f "
          "ua_close: %.3f "
          "sm_finish: %.3f "
          "plugin_active: %.3f "
          "plugin_total: %.3f",
          sm_id, client_ip, t_state.client_info.src_addr.host_order_port(),
          _ua.get_txn() ? _ua.get_txn()->get_protocol_string() : "-1", url_string, status, unique_id_string, redirection_tries,
          client_response_body_bytes, fd, t_state.client_info.state, t_state.server_info.state,
          milestones.difference_sec(TS_MILESTONE_TLS_HANDSHAKE_START, TS_MILESTONE_TLS_HANDSHAKE_END),
          milestones.difference_sec(TS_MILESTONE_SM_START, TS_MILESTONE_UA_BEGIN),
          milestones.difference_sec(TS_MILESTONE_SM_START, TS_MILESTONE_UA_FIRST_READ),
          milestones.difference_sec(TS_MILESTONE_SM_START, TS_MILESTONE_UA_READ_HEADER_DONE),
          milestones.difference_sec(TS_MILESTONE_SM_START, TS_MILESTONE_CACHE_OPEN_READ_BEGIN),
          milestones.difference_sec(TS_MILESTONE_SM_START, TS_MILESTONE_CACHE_OPEN_READ_END),
          milestones.difference_sec(TS_MILESTONE_SM_START, TS_MILESTONE_CACHE_OPEN_WRITE_BEGIN),
          milestones.difference_sec(TS_MILESTONE_SM_START, TS_MILESTONE_CACHE_OPEN_WRITE_END),
          milestones.difference_sec(TS_MILESTONE_SM_START, TS_MILESTONE_DNS_LOOKUP_BEGIN),
          milestones.difference_sec(TS_MILESTONE_SM_START, TS_MILESTONE_DNS_LOOKUP_END),
          milestones.difference_sec(TS_MILESTONE_SM_START, TS_MILESTONE_SERVER_CONNECT),
          milestones.difference_sec(TS_MILESTONE_SM_START, TS_MILESTONE_SERVER_CONNECT_END),
          milestones.difference_sec(TS_MILESTONE_SM_START, TS_MILESTONE_SERVER_FIRST_READ),
          milestones.difference_sec(TS_MILESTONE_SM_START, TS_MILESTONE_SERVER_READ_HEADER_DONE),
          milestones.difference_sec(TS_MILESTONE_SM_START, TS_MILESTONE_SERVER_CLOSE),
          milestones.difference_sec(TS_MILESTONE_SM_START, TS_MILESTONE_UA_BEGIN_WRITE),
          milestones.difference_sec(TS_MILESTONE_SM_START, TS_MILESTONE_UA_CLOSE),
          milestones.difference_sec(TS_MILESTONE_SM_START, TS_MILESTONE_SM_FINISH),
          milestones.difference_sec(TS_MILESTONE_SM_START, TS_MILESTONE_PLUGIN_ACTIVE),
          milestones.difference_sec(TS_MILESTONE_SM_START, TS_MILESTONE_PLUGIN_TOTAL));
  }
}

//
// void HttpSM::dump_state_on_assert
//    Debugging routine to dump the state machine's history
//     and other state on an assertion failure
//    We use Diags::Status instead of stderr since
//     Diags works both on UNIX & NT
//
void
HttpSM::dump_state_on_assert()
{
  Error("[%" PRId64 "] ------- begin http state dump -------", sm_id);

  if (history.overflowed()) {
    Error("   History Wrap around. history size: %d", history.size());
  }
  // Loop through the history and dump it
  for (unsigned int i = 0; i < history.size(); i++) {
    char buf[256];
    int  r = history[i].reentrancy;
    int  e = history[i].event;
    Error("%d   %d   %s", e, r, history[i].location.str(buf, sizeof(buf)));
  }

  // Dump the via string
  Error("Via String: [%s]\n", t_state.via_string);

  // Dump header info
  dump_state_hdr(&t_state.hdr_info.client_request, "Client Request");
  dump_state_hdr(&t_state.hdr_info.server_request, "Server Request");
  dump_state_hdr(&t_state.hdr_info.server_response, "Server Response");
  dump_state_hdr(&t_state.hdr_info.transform_response, "Transform Response");
  dump_state_hdr(&t_state.hdr_info.client_response, "Client Response");

  Error("[%" PRId64 "] ------- end http state dump ---------", sm_id);
}

void
HttpSM::dump_state_hdr(HTTPHdr *h, const char *s)
{
  // Dump the client request if available
  if (h->valid()) {
    int   l       = h->length_get();
    char *hdr_buf = static_cast<char *>(ats_malloc(l + 1));
    int   index   = 0;
    int   offset  = 0;

    h->print(hdr_buf, l, &index, &offset);

    hdr_buf[l] = '\0';
    Error("  ----  %s [%" PRId64 "] ----\n%s\n", s, sm_id, hdr_buf);
    ats_free(hdr_buf);
  }
}

/*****************************************************************************
 *****************************************************************************
 ****                                                                     ****
 ****                       HttpTransact Interface                        ****
 ****                                                                     ****
 *****************************************************************************
 *****************************************************************************/
//////////////////////////////////////////////////////////////////////////
//
//      HttpSM::call_transact_and_set_next_state(f)
//
//      This routine takes an HttpTransact function <f>, calls the function
//      to perform some actions on the current HttpTransact::State, and
//      then uses the HttpTransact return action code to set the next
//      handler (state) for the state machine.  HttpTransact could have
//      returned the handler directly, but returns action codes in hopes of
//      making a cleaner separation between the state machine and the
//      HttpTransact logic.
//
//////////////////////////////////////////////////////////////////////////

// Where is the goatherd?

void
HttpSM::call_transact_and_set_next_state(TransactEntryFunc_t f)
{
  last_action = t_state.next_action; // remember where we were

  // The callee can either specify a method to call in to Transact,
  //   or call with NULL which indicates that Transact should use
  //   its stored entry point.
  if (f == nullptr) {
    ink_release_assert(t_state.transact_return_point != nullptr);
    t_state.transact_return_point(&t_state);
  } else {
    f(&t_state);
  }

  SMDbg(dbg_ctl_http, "State Transition: %s -> %s", HttpDebugNames::get_action_name(last_action),
        HttpDebugNames::get_action_name(t_state.next_action));

  set_next_state();

  return;
}

//////////////////////////////////////////////////////////////////////////////
//
//  HttpSM::set_next_state()
//
//  call_transact_and_set_next_state() was broken into two parts, one
//  which calls the HttpTransact method and the second which sets the
//  next state. In a case which set_next_state() was not completed,
//  the state function calls set_next_state() to retry setting the
//  state.
//
//////////////////////////////////////////////////////////////////////////////
void
HttpSM::set_next_state()
{
  ///////////////////////////////////////////////////////////////////////
  // Use the returned "next action" code to set the next state handler //
  ///////////////////////////////////////////////////////////////////////
  switch (t_state.next_action) {
  case HttpTransact::StateMachineAction_t::API_PRE_REMAP:
  case HttpTransact::StateMachineAction_t::API_POST_REMAP:
  case HttpTransact::StateMachineAction_t::API_READ_REQUEST_HDR:
  case HttpTransact::StateMachineAction_t::REQUEST_BUFFER_READ_COMPLETE:
  case HttpTransact::StateMachineAction_t::API_OS_DNS:
  case HttpTransact::StateMachineAction_t::API_SEND_REQUEST_HDR:
  case HttpTransact::StateMachineAction_t::API_READ_CACHE_HDR:
  case HttpTransact::StateMachineAction_t::API_READ_RESPONSE_HDR:
  case HttpTransact::StateMachineAction_t::API_SEND_RESPONSE_HDR:
  case HttpTransact::StateMachineAction_t::API_CACHE_LOOKUP_COMPLETE: {
    t_state.api_next_action = t_state.next_action;
    do_api_callout();
    break;
  }

  case HttpTransact::StateMachineAction_t::POST_REMAP_SKIP: {
    call_transact_and_set_next_state(nullptr);
    break;
  }

  case HttpTransact::StateMachineAction_t::REMAP_REQUEST: {
    do_remap_request(true); /* run inline */
    SMDbg(dbg_ctl_url_rewrite, "completed inline remapping request");
    t_state.url_remap_success = remapProcessor.finish_remap(&t_state, m_remap);
    if (t_state.next_action == HttpTransact::StateMachineAction_t::SEND_ERROR_CACHE_NOOP &&
        t_state.transact_return_point == nullptr) {
      // It appears that we can now set the next_action to error and transact_return_point to nullptr when
      // going through do_remap_request presumably due to a plugin setting an error.  In that case, it seems
      // that the error message has already been setup, so we can just return and avoid the further
      // call_transact_and_set_next_state
    } else {
      call_transact_and_set_next_state(nullptr);
    }
    break;
  }

  case HttpTransact::StateMachineAction_t::DNS_LOOKUP: {
    if (sockaddr const *addr; t_state.http_config_param->use_client_target_addr == 2 &&              // no CTA verification
                              !t_state.url_remap_success &&                                          // wasn't remapped
                              t_state.parent_result.result != ParentResultType::SPECIFIED &&         // no parent.
                              t_state.client_info.is_transparent &&                                  // inbound transparent
                              t_state.dns_info.os_addr_style == ResolveInfo::OS_Addr::TRY_DEFAULT && // haven't tried anything yet.
                              ats_is_ip(addr = _ua.get_txn()->get_netvc()->get_local_addr()))        // valid inbound remote address
    {
      /* If the connection is client side transparent and the URL was not remapped/directed to
       * parent proxy, we can use the client destination IP address instead of doing a DNS lookup.
       * This is controlled by the 'use_client_target_addr' configuration parameter.
       */
      if (dbg_ctl_dns.on()) {
        ip_text_buffer ipb;
        SMDbg(dbg_ctl_dns, "Skipping DNS lookup for client supplied target %s.", ats_ip_ntop(addr, ipb, sizeof(ipb)));
      }

      t_state.dns_info.set_upstream_address(addr);

      // Make a note the CTA is being used - don't do this case again.
      t_state.dns_info.os_addr_style = ResolveInfo::OS_Addr::TRY_CLIENT;

      if (t_state.hdr_info.client_request.version_get() == HTTPVersion(1, 1)) {
        t_state.dns_info.http_version = HTTP_1_1;
      } else if (t_state.hdr_info.client_request.version_get() == HTTPVersion(1, 0)) {
        t_state.dns_info.http_version = HTTP_1_0;
      } else if (t_state.hdr_info.client_request.version_get() == HTTPVersion(0, 9)) {
        t_state.dns_info.http_version = HTTP_0_9;
      } else {
        t_state.dns_info.http_version = HTTP_1_1;
      }

      call_transact_and_set_next_state(nullptr);
      break;
    } else if (t_state.dns_info.looking_up == ResolveInfo::ORIGIN_SERVER && t_state.txn_conf->no_dns_forward_to_parent &&
               t_state.parent_result.result != ParentResultType::UNDEFINED) {
      t_state.dns_info.resolved_p = true; // seems dangerous - where's the IP address?
      call_transact_and_set_next_state(nullptr);
      break;
    } else if (t_state.dns_info.resolved_p) {
      SMDbg(dbg_ctl_dns, "Skipping DNS lookup because the address is already set.");
      call_transact_and_set_next_state(nullptr);
      break;
    }
    // else have to do DNS.
    HTTP_SM_SET_DEFAULT_HANDLER(&HttpSM::state_hostdb_lookup);

    // We need to close the previous attempt
    // Because it could be a server side retry by DNS rr
    if (server_entry) {
      ink_assert(server_entry->vc_type == HttpVC_t::SERVER_VC);
      vc_table.cleanup_entry(server_entry);
      server_entry = nullptr;
    } else {
      // Now that we have gotten the user agent request, we can cancel
      // the inactivity timeout associated with it.  Note, however, that
      // we must not cancel the inactivity timeout if the message
      // contains a body. This indicates that a POST operation is taking place and
      // that the client is still sending data to the origin server.  The
      // origin server cannot reply until the entire request is received.  In
      // light of this dependency, TS must ensure that the client finishes
      // sending its request and for this reason, the inactivity timeout
      // cannot be cancelled.
      if (_ua.get_txn() &&
          !_ua.get_txn()->has_request_body(t_state.hdr_info.request_content_length,
                                           t_state.client_info.transfer_encoding == HttpTransact::TransferEncoding_t::CHUNKED)) {
        _ua.get_txn()->cancel_inactivity_timeout();
      } else if (!_ua.get_txn() || _ua.get_txn()->get_netvc() == nullptr) {
        terminate_sm = true;
        return; // Give up if there is no session
      }
    }

    ink_assert(t_state.dns_info.looking_up != ResolveInfo::UNDEFINED_LOOKUP);
    do_hostdb_lookup();
    break;
  }

  case HttpTransact::StateMachineAction_t::DNS_REVERSE_LOOKUP: {
    HTTP_SM_SET_DEFAULT_HANDLER(&HttpSM::state_hostdb_reverse_lookup);
    do_hostdb_reverse_lookup();
    break;
  }

  case HttpTransact::StateMachineAction_t::CACHE_LOOKUP: {
    HTTP_SM_SET_DEFAULT_HANDLER(&HttpSM::state_cache_open_read);
    do_cache_lookup_and_read();
    break;
  }

  case HttpTransact::StateMachineAction_t::ORIGIN_SERVER_OPEN: {
    // Pre-emptively set a server connect failure that will be cleared once a WRITE_READY is received from origin or
    // bytes are received back
    t_state.set_connect_fail(EIO);
    HTTP_SM_SET_DEFAULT_HANDLER(&HttpSM::state_http_server_open);

    // We need to close the previous attempt
    if (server_entry) {
      ink_assert(server_entry->vc_type == HttpVC_t::SERVER_VC);
      vc_table.cleanup_entry(server_entry);
      server_entry = nullptr;
    } else {
      // Now that we have gotten the user agent request, we can cancel
      // the inactivity timeout associated with it.  Note, however, that
      // we must not cancel the inactivity timeout if the message
      // contains a body. This indicates that a POST operation is taking place and
      // that the client is still sending data to the origin server.  The
      // origin server cannot reply until the entire request is received.  In
      // light of this dependency, TS must ensure that the client finishes
      // sending its request and for this reason, the inactivity timeout
      // cannot be cancelled.
      if (_ua.get_txn() &&
          !_ua.get_txn()->has_request_body(t_state.hdr_info.request_content_length,
                                           t_state.client_info.transfer_encoding == HttpTransact::TransferEncoding_t::CHUNKED)) {
        _ua.get_txn()->cancel_inactivity_timeout();
      } else if (!_ua.get_txn()) {
        terminate_sm = true;
        return; // Give up if there is no session
      }
    }

    do_http_server_open();
    break;
  }

  // This is called in some case if the 100 continue header is from a HTTP/1.0 server
  // Likely an obsolete case now and should probably return an error
  case HttpTransact::StateMachineAction_t::SERVER_PARSE_NEXT_HDR: {
    setup_server_read_response_header();
    break;
  }

  case HttpTransact::StateMachineAction_t::INTERNAL_100_RESPONSE: {
    setup_100_continue_transfer();
    break;
  }

  case HttpTransact::StateMachineAction_t::SERVER_READ: {
    t_state.source = HttpTransact::Source_t::HTTP_ORIGIN_SERVER;

    if (transform_info.vc) {
      ink_assert(t_state.hdr_info.client_response.valid() == 0);
      ink_assert((t_state.hdr_info.transform_response.valid() ? true : false) == true);
      HttpTunnelProducer *p = setup_server_transfer_to_transform();
      perform_cache_write_action();
      tunnel.tunnel_run(p);
    } else {
      ink_assert((t_state.hdr_info.client_response.valid() ? true : false) == true);
      t_state.api_next_action = HttpTransact::StateMachineAction_t::API_SEND_RESPONSE_HDR;

      // check to see if we are going to handle the redirection from server response and if there is a plugin hook set
      if (hooks_set && is_redirect_required() == false) {
        do_api_callout_internal();
      } else {
        do_redirect();
        handle_api_return();
      }
    }
    break;
  }

  case HttpTransact::StateMachineAction_t::SERVE_FROM_CACHE: {
    ink_assert(t_state.cache_info.action == HttpTransact::CacheAction_t::SERVE ||
               t_state.cache_info.action == HttpTransact::CacheAction_t::SERVE_AND_DELETE ||
               t_state.cache_info.action == HttpTransact::CacheAction_t::SERVE_AND_UPDATE);
    release_server_session(true);
    t_state.source = HttpTransact::Source_t::CACHE;

    if (transform_info.vc) {
      ink_assert(t_state.hdr_info.client_response.valid() == 0);
      ink_assert((t_state.hdr_info.transform_response.valid() ? true : false) == true);
      do_drain_request_body(t_state.hdr_info.transform_response);
      t_state.hdr_info.cache_response.create(HTTPType::RESPONSE);
      t_state.hdr_info.cache_response.copy(&t_state.hdr_info.transform_response);

      HttpTunnelProducer *p = setup_cache_transfer_to_transform();
      perform_cache_write_action();
      tunnel.tunnel_run(p);
    } else {
      ink_assert((t_state.hdr_info.client_response.valid() ? true : false) == true);
      do_drain_request_body(t_state.hdr_info.client_response);
      t_state.hdr_info.cache_response.create(HTTPType::RESPONSE);
      t_state.hdr_info.cache_response.copy(&t_state.hdr_info.client_response);

      perform_cache_write_action();
      t_state.api_next_action = HttpTransact::StateMachineAction_t::API_SEND_RESPONSE_HDR;

      // check to see if there is a plugin hook set
      if (hooks_set) {
        do_api_callout_internal();
      } else {
        handle_api_return();
      }
    }
    break;
  }

  case HttpTransact::StateMachineAction_t::CACHE_ISSUE_WRITE: {
    ink_assert(cache_sm.cache_write_vc == nullptr);
    HTTP_SM_SET_DEFAULT_HANDLER(&HttpSM::state_cache_open_write);
    do_cache_prepare_write();
    break;
  }

  case HttpTransact::StateMachineAction_t::INTERNAL_CACHE_WRITE: {
    t_state.api_next_action = HttpTransact::StateMachineAction_t::API_SEND_RESPONSE_HDR;
    do_api_callout();
    break;
  }

  case HttpTransact::StateMachineAction_t::INTERNAL_CACHE_NOOP: {
    if (server_entry != nullptr && server_entry->in_tunnel == false) {
      release_server_session();
    }

    do_drain_request_body(t_state.hdr_info.client_response);

    // If we're in state SEND_API_RESPONSE_HDR, it means functions
    // registered to hook SEND_RESPONSE_HDR have already been called. So we do not
    // need to call do_api_callout. Otherwise TS loops infinitely in this state !
    if (t_state.api_next_action == HttpTransact::StateMachineAction_t::API_SEND_RESPONSE_HDR) {
      handle_api_return();
    } else {
      t_state.api_next_action = HttpTransact::StateMachineAction_t::API_SEND_RESPONSE_HDR;
      do_api_callout();
    }
    break;
  }

  case HttpTransact::StateMachineAction_t::INTERNAL_CACHE_DELETE: {
    // Nuke all the alternates since this is mostly likely
    //   the result of a delete method
    cache_sm.end_both();
    do_cache_delete_all_alts(nullptr);

    release_server_session();
    t_state.api_next_action = HttpTransact::StateMachineAction_t::API_SEND_RESPONSE_HDR;
    do_api_callout();
    break;
  }

  case HttpTransact::StateMachineAction_t::INTERNAL_CACHE_UPDATE_HEADERS: {
    issue_cache_update();
    cache_sm.close_read();

    release_server_session();
    t_state.api_next_action = HttpTransact::StateMachineAction_t::API_SEND_RESPONSE_HDR;
    do_api_callout();
    break;
  }

  case HttpTransact::StateMachineAction_t::SEND_ERROR_CACHE_NOOP: {
    setup_error_transfer();
    break;
  }

  case HttpTransact::StateMachineAction_t::ORIGIN_SERVER_RR_MARK_DOWN: {
    HTTP_SM_SET_DEFAULT_HANDLER(&HttpSM::state_mark_os_down);
    ATS_PROBE(next_state_SM_ACTION_ORIGIN_SERVER_RR_MARK_DOWN);

    ink_assert(t_state.dns_info.looking_up == ResolveInfo::ORIGIN_SERVER);

    // TODO: This might not be optimal (or perhaps even correct), but it will
    // effectively mark the host as down. What's odd is that state_mark_os_down
    // above isn't triggering.
    HttpSM::do_hostdb_update_if_necessary();

    do_hostdb_lookup();
    break;
  }

  case HttpTransact::StateMachineAction_t::SSL_TUNNEL: {
    t_state.api_next_action = HttpTransact::StateMachineAction_t::API_SEND_RESPONSE_HDR;
    do_api_callout();
    break;
  }

  case HttpTransact::StateMachineAction_t::ORIGIN_SERVER_RAW_OPEN: {
    // Pre-emptively set a server connect failure that will be cleared once a WRITE_READY is received from origin or
    // bytes are received back
    t_state.set_connect_fail(EIO);
    HTTP_SM_SET_DEFAULT_HANDLER(&HttpSM::state_raw_http_server_open);

    ink_assert(server_entry == nullptr);
    do_http_server_open(true);
    break;
  }

  case HttpTransact::StateMachineAction_t::CACHE_ISSUE_WRITE_TRANSFORM: {
    ink_assert(t_state.cache_info.transform_action == HttpTransact::CacheAction_t::PREPARE_TO_WRITE);

    if (transform_cache_sm.cache_write_vc) {
      // We've already got the write_vc that
      //  didn't use for the untransformed copy
      ink_assert(cache_sm.cache_write_vc == nullptr);
      ink_assert(t_state.api_info.cache_untransformed == false);
      t_state.cache_info.write_lock_state = HttpTransact::CacheWriteLock_t::SUCCESS;
      call_transact_and_set_next_state(nullptr);
    } else {
      HTTP_SM_SET_DEFAULT_HANDLER(&HttpSM::state_cache_open_write);

      do_cache_prepare_write_transform();
    }
    break;
  }

  case HttpTransact::StateMachineAction_t::TRANSFORM_READ: {
    t_state.api_next_action = HttpTransact::StateMachineAction_t::API_SEND_RESPONSE_HDR;
    do_api_callout();
    break;
  }

  case HttpTransact::StateMachineAction_t::READ_PUSH_HDR: {
    setup_push_read_response_header();
    break;
  }

  case HttpTransact::StateMachineAction_t::STORE_PUSH_BODY: {
    // This can return NULL - do we really want to run the tunnel in that case?
    // But that's how it was before this change.
    HttpTunnelProducer *p = setup_push_transfer_to_cache();
    tunnel.tunnel_run(p);
    break;
  }

  case HttpTransact::StateMachineAction_t::CACHE_PREPARE_UPDATE: {
    ink_assert(t_state.api_update_cached_object == HttpTransact::UpdateCachedObject_t::CONTINUE);
    do_cache_prepare_update();
    break;
  }
  case HttpTransact::StateMachineAction_t::CACHE_ISSUE_UPDATE: {
    if (t_state.api_update_cached_object == HttpTransact::UpdateCachedObject_t::ERROR) {
      t_state.cache_info.object_read = nullptr;
      cache_sm.close_read();
    }
    issue_cache_update();
    call_transact_and_set_next_state(nullptr);
    break;
  }

  case HttpTransact::StateMachineAction_t::WAIT_FOR_FULL_BODY: {
    wait_for_full_body();
    break;
  }

  case HttpTransact::StateMachineAction_t::CONTINUE: {
    ink_release_assert(!"Not implemented");
    break;
  }

  default: {
    ink_release_assert(!"Unknown next action");
  }
  }
}

void
HttpSM::do_redirect()
{
  SMDbg(dbg_ctl_http_redirect, "enable_redirection %u", enable_redirection);
  if (!enable_redirection || redirection_tries >= t_state.txn_conf->number_of_redirections) {
    this->postbuf_clear();

    if (enable_redirection && redirection_tries >= t_state.txn_conf->number_of_redirections) {
      t_state.squid_codes.subcode = SquidSubcode::NUM_REDIRECTIONS_EXCEEDED;
    }

    return;
  }

  // if redirect_url is set by an user's plugin, yts will redirect to this url anyway.
  if (is_redirect_required()) {
    if (redirect_url != nullptr ||
        t_state.hdr_info.client_response.field_find(static_cast<std::string_view>(MIME_FIELD_LOCATION))) {
      if (Log::transaction_logging_enabled() && t_state.api_info.logging_enabled) {
        LogAccess accessor(this);
        if (redirect_url == nullptr) {
          if (t_state.squid_codes.log_code == SquidLogCode::TCP_HIT) {
            t_state.squid_codes.log_code = SquidLogCode::TCP_HIT_REDIRECT;
          } else {
            t_state.squid_codes.log_code = SquidLogCode::TCP_MISS_REDIRECT;
          }
        } else {
          if (t_state.squid_codes.log_code == SquidLogCode::TCP_HIT) {
            t_state.squid_codes.log_code = SquidLogCode::TCP_HIT_X_REDIRECT;
          } else {
            t_state.squid_codes.log_code = SquidLogCode::TCP_MISS_X_REDIRECT;
          }
        }

        int ret = Log::access(&accessor);

        if (ret & Log::FULL) {
          SMDbg(dbg_ctl_http, "Logging system indicates FULL.");
        }
        if (ret & Log::FAIL) {
          Log::error("failed to log transaction for at least one log object");
        }
      }

      ++redirection_tries;
      if (redirect_url != nullptr) {
        redirect_request(redirect_url, redirect_url_len);
        ats_free((void *)redirect_url);
        redirect_url     = nullptr;
        redirect_url_len = 0;
        Metrics::Counter::increment(http_rsb.total_x_redirect);
      } else {
        // get the location header and setup the redirect
        auto redir_url{t_state.hdr_info.client_response.value_get(static_cast<std::string_view>(MIME_FIELD_LOCATION))};
        redirect_request(redir_url.data(), static_cast<int>(redir_url.length()));
      }

    } else {
      enable_redirection = false;
    }
  } else {
    enable_redirection = false;
  }
}

void
HttpSM::redirect_request(const char *arg_redirect_url, const int arg_redirect_len)
{
  SMDbg(dbg_ctl_http_redirect, "redirect url: %.*s", arg_redirect_len, arg_redirect_url);
  // get a reference to the client request header and client url and check to see if the url is valid
  HTTPHdr &clientRequestHeader = t_state.hdr_info.client_request;
  URL     &clientUrl           = *clientRequestHeader.url_get();
  if (!clientUrl.valid()) {
    return;
  }

  bool valid_origHost = true;
  int  origMethod_len{0};
  char origHost[MAXDNAME];
  char origMethod[255];
  int  origPort = 80;

  if (t_state.hdr_info.server_request.valid()) {
    origPort = t_state.hdr_info.server_request.port_get();

    if (auto tmpOrigHost{t_state.hdr_info.server_request.value_get(static_cast<std::string_view>(MIME_FIELD_HOST))};
        !tmpOrigHost.empty()) {
      memcpy(origHost, tmpOrigHost.data(), tmpOrigHost.length());
      origHost[std::min(tmpOrigHost.length(), sizeof(origHost) - 1)] = '\0';
    } else {
      valid_origHost = false;
    }

    auto tmpOrigMethod{t_state.hdr_info.server_request.method_get()};
    origMethod_len = tmpOrigMethod.length();
    if (!tmpOrigMethod.empty()) {
      memcpy(origMethod, tmpOrigMethod.data(), std::min(origMethod_len, static_cast<int>(sizeof(origMethod))));
    } else {
      valid_origHost = false;
    }
  } else {
    SMDbg(dbg_ctl_http_redir_error, "t_state.hdr_info.server_request not valid");
    valid_origHost = false;
  }

  t_state.redirect_info.redirect_in_process = true;

  // set the passed in location url and parse it
  URL redirectUrl;
  redirectUrl.create(nullptr);

  redirectUrl.parse(arg_redirect_url, arg_redirect_len);
  {
    if (redirectUrl.scheme_get().empty() && !redirectUrl.host_get().empty() && arg_redirect_url[0] != '/') {
      // RFC7230 § 5.5
      // The redirect URL lacked a scheme and so it is a relative URL.
      // The redirect URL did not begin with a slash, so we parsed some or all
      // of the relative URI path as the host.
      // Prepend a slash and parse again.
      std::string redirect_url_leading_slash(arg_redirect_len + 1, '\0');
      redirect_url_leading_slash[0] = '/';
      if (arg_redirect_len > 0) {
        memcpy(redirect_url_leading_slash.data() + 1, arg_redirect_url, arg_redirect_len);
      }
      url_nuke_proxy_stuff(redirectUrl.m_url_impl);
      redirectUrl.parse(redirect_url_leading_slash.c_str(), arg_redirect_len + 1);
    }
  }

  // copy the client url to the original url
  URL &origUrl = t_state.redirect_info.original_url;
  if (!origUrl.valid()) {
    origUrl.create(nullptr);
    origUrl.copy(&clientUrl);
  }
  // copy the redirect url to the client url
  clientUrl.copy(&redirectUrl);

  redirectUrl.destroy();

  //(bug 2540703) Clear the previous response if we will attempt the redirect
  if (t_state.hdr_info.client_response.valid()) {
    // XXX - doing a destroy() for now, we can do a fileds_clear() if we have performance issue
    t_state.hdr_info.client_response.destroy();
  }

  int         scheme          = t_state.next_hop_scheme;
  int         scheme_len      = hdrtoken_index_to_length(scheme);
  const char *next_hop_scheme = hdrtoken_index_to_wks(scheme);
  char        scheme_str[scheme_len + 1];

  if (next_hop_scheme) {
    memcpy(scheme_str, next_hop_scheme, scheme_len);
  } else {
    valid_origHost = false;
  }

  t_state.hdr_info.server_request.destroy();

  // we want to close the server session
  // will do that in handle_api_return under the
  // HttpTransact::StateMachineAction_t::REDIRECT_READ state
  t_state.parent_result.reset();
  t_state.request_sent_time      = 0;
  t_state.response_received_time = 0;
  t_state.next_action            = HttpTransact::StateMachineAction_t::REDIRECT_READ;
  // we have a new OS and need to have DNS lookup the new OS
  t_state.dns_info.resolved_p = false;
  t_state.force_dns           = false;
  t_state.server_info.clear();
  t_state.parent_info.clear();

  // Must reset whether the InkAPI has set the destination address
  //  t_state.dns_info.api_addr_set_p = false;

  if (t_state.txn_conf->cache_http) {
    t_state.cache_info.object_read = nullptr;
  }

  bool noPortInHost = HttpConfig::m_master.redirection_host_no_port;

  bool isRedirectUrlOriginForm = !clientUrl.m_url_impl->m_len_scheme && !clientUrl.m_url_impl->m_len_user &&
                                 !clientUrl.m_url_impl->m_len_password && !clientUrl.m_url_impl->m_len_host &&
                                 !clientUrl.m_url_impl->m_len_port;

  // check to see if the client request passed a host header, if so copy the host and port from the redirect url and
  // make a new host header
  if (t_state.hdr_info.client_request.presence(MIME_PRESENCE_HOST)) {
    auto host{clientUrl.host_get()};
    auto host_len{static_cast<int>(host.length())};

    if (!host.empty()) {
      int port = clientUrl.port_get();

      if (auto redirectScheme{clientUrl.scheme_get()}; redirectScheme.empty()) {
        clientUrl.scheme_set({scheme_str, static_cast<std::string_view::size_type>(scheme_len)});
        SMDbg(dbg_ctl_http_redirect, "URL without scheme");
      }

      if (noPortInHost) {
        int  redirectSchemeIdx = clientUrl.scheme_get_wksidx();
        bool defaultPort =
          (((redirectSchemeIdx == URL_WKSIDX_HTTP) && (port == 80)) || ((redirectSchemeIdx == URL_WKSIDX_HTTPS) && (port == 443)));

        if (!defaultPort) {
          noPortInHost = false;
        }
      }

      if (!noPortInHost) {
        char buf[host_len + 7]; // 5 + 1 + 1 ("12345" + ':' + '\0')

        host_len = snprintf(buf, host_len + 7, "%.*s:%d", host_len, host.data(), port);
        t_state.hdr_info.client_request.value_set(static_cast<std::string_view>(MIME_FIELD_HOST),
                                                  std::string_view{buf, static_cast<std::string_view::size_type>(host_len)});
      } else {
        t_state.hdr_info.client_request.value_set(static_cast<std::string_view>(MIME_FIELD_HOST), host);
      }
      t_state.hdr_info.client_request.m_target_cached = false;
      t_state.hdr_info.server_request.m_target_cached = false;
    } else {
      // the client request didn't have a host, so use the current origin host
      if (valid_origHost) {
        char *saveptr = nullptr;

        // the client request didn't have a host, so use the current origin host
        SMDbg(dbg_ctl_http_redirect, "keeping client request host %s://%s", next_hop_scheme, origHost);
        char *origHostNoPort = strtok_r(origHost, ":", &saveptr);

        if (origHostNoPort == nullptr) {
          goto LhostError;
        }

        host_len = strlen(origHostNoPort);
        if (noPortInHost) {
          int  redirectSchemeIdx = t_state.next_hop_scheme;
          bool defaultPort       = (((redirectSchemeIdx == URL_WKSIDX_HTTP) && (origPort == 80)) ||
                              ((redirectSchemeIdx == URL_WKSIDX_HTTPS) && (origPort == 443)));

          if (!defaultPort) {
            noPortInHost = false;
          }
        }

        if (!noPortInHost) {
          char buf[host_len + 7]; // 5 + 1 + 1 ("12345" + ':' + '\0')

          host_len = snprintf(buf, host_len + 7, "%s:%d", origHostNoPort, origPort);
          t_state.hdr_info.client_request.value_set(static_cast<std::string_view>(MIME_FIELD_HOST),
                                                    std::string_view{buf, static_cast<std::string_view::size_type>(host_len)});
        } else {
          t_state.hdr_info.client_request.value_set(
            static_cast<std::string_view>(MIME_FIELD_HOST),
            std::string_view{origHostNoPort, static_cast<std::string_view::size_type>(host_len)});
        }

        // Cleanup of state etc.
        url_nuke_proxy_stuff(clientUrl.m_url_impl);
        url_nuke_proxy_stuff(t_state.hdr_info.client_request.m_url_cached.m_url_impl);
        t_state.hdr_info.client_request.method_set(
          std::string_view{origMethod, std::min(static_cast<std::string_view::size_type>(origMethod_len), sizeof(origMethod))});
        t_state.hdr_info.client_request.m_target_cached = false;
        t_state.hdr_info.server_request.m_target_cached = false;
        clientUrl.scheme_set({scheme_str, static_cast<std::string_view::size_type>(scheme_len)});
        if (isRedirectUrlOriginForm) {
          // build the rest of the effictive URL: the authority part
          clientUrl.user_set(
            {origUrl.m_url_impl->m_ptr_user, static_cast<std::string_view::size_type>(origUrl.m_url_impl->m_len_user)});
          clientUrl.password_set(
            {origUrl.m_url_impl->m_ptr_password, static_cast<std::string_view::size_type>(origUrl.m_url_impl->m_len_password)});
          clientUrl.host_set(
            {origUrl.m_url_impl->m_ptr_host, static_cast<std::string_view::size_type>(origUrl.m_url_impl->m_len_host)});
          clientUrl.port_set(origUrl.port_get());
        }
      } else {
      LhostError:
        // the server request didn't have a host, so remove it from the headers
        t_state.hdr_info.client_request.field_delete(static_cast<std::string_view>(MIME_FIELD_HOST));
      }
    }
  }

  dump_header(dbg_ctl_http_hdrs, &t_state.hdr_info.client_request, sm_id, "Framed Client Request..checking");

  // Reset HttpCacheSM for new cache operations
  cache_sm.reset();
}

void
HttpSM::set_http_schedule(Continuation *contp)
{
  HTTP_SM_SET_DEFAULT_HANDLER(&HttpSM::get_http_schedule);
  schedule_cont = contp;
}

int
HttpSM::get_http_schedule(int event, void * /* data ATS_UNUSED */)
{
  bool            plugin_lock;
  Ptr<ProxyMutex> plugin_mutex;
  if (schedule_cont->mutex) {
    plugin_mutex = schedule_cont->mutex;
    plugin_lock  = MUTEX_TAKE_TRY_LOCK(schedule_cont->mutex, mutex->thread_holding);

    if (!plugin_lock) {
      HTTP_SM_SET_DEFAULT_HANDLER(&HttpSM::get_http_schedule);
      ink_assert(pending_action.empty());
      pending_action = mutex->thread_holding->schedule_in(this, HRTIME_MSECONDS(10));
      return 0;
    } else {
      pending_action = nullptr; // if there was a pending action, it'll get freed after this returns so clear it.
    }
  } else {
    plugin_lock = false;
  }

  // handle Mutex;
  schedule_cont->handleEvent(event, this);
  if (plugin_lock) {
    Mutex_unlock(plugin_mutex, mutex->thread_holding);
  }

  return 0;
}

/*
 * Used from an InkAPI
 */
bool
HttpSM::set_server_session_private(bool private_session)
{
  if (server_txn != nullptr) {
    static_cast<PoolableSession *>(server_txn->get_proxy_ssn())->set_private(private_session);
    return true;
  }
  return false;
}

bool
HttpSM::is_private() const
{
  bool res = false;
  if (will_be_private_ss) {
    res = will_be_private_ss;
  }
  return res;
}

// check to see if redirection is enabled and less than max redirections tries or if a plugin enabled redirection
inline bool
HttpSM::is_redirect_required()
{
  bool redirect_required = (enable_redirection && (redirection_tries < t_state.txn_conf->number_of_redirections) &&
                            !HttpTransact::is_fresh_cache_hit(t_state.cache_lookup_result));

  SMDbg(dbg_ctl_http_redirect, "redirect_required: %u", redirect_required);

  if (redirect_required == true) {
    HTTPStatus status = t_state.hdr_info.client_response.status_get();
    // check to see if the response from the origin was a 301, 302, or 303
    switch (status) {
    case HTTPStatus::MULTIPLE_CHOICES:   // 300
    case HTTPStatus::MOVED_PERMANENTLY:  // 301
    case HTTPStatus::MOVED_TEMPORARILY:  // 302
    case HTTPStatus::SEE_OTHER:          // 303
    case HTTPStatus::USE_PROXY:          // 305
    case HTTPStatus::TEMPORARY_REDIRECT: // 307
    case HTTPStatus::PERMANENT_REDIRECT: // 308
      redirect_required = true;
      break;
    default:
      redirect_required = false;
      break;
    }

    // if redirect_url is set by an user's plugin, ats will redirect to this url anyway.
    if (redirect_url != nullptr) {
      redirect_required = true;
    }
  }
  return redirect_required;
}

SNIRoutingType
HttpSM::get_tunnel_type() const
{
  return _tunnel_type;
}

// Fill in the client protocols used.  Return the number of entries populated.
int
HttpSM::populate_client_protocol(std::string_view *result, int n) const
{
  int retval = 0;
  if (n > 0) {
    std::string_view proto = HttpSM::find_proto_string(t_state.hdr_info.client_request.version_get());
    if (!proto.empty()) {
      result[retval++] = proto;
      if (n > retval && _ua.get_txn()) {
        retval += _ua.get_txn()->populate_protocol(result + retval, n - retval);
      }
    }
  }
  return retval;
}

// Look for a specific client protocol
const char *
HttpSM::client_protocol_contains(std::string_view tag_prefix) const
{
  const char      *retval = nullptr;
  std::string_view proto  = HttpSM::find_proto_string(t_state.hdr_info.client_request.version_get());
  if (!proto.empty()) {
    std::string_view prefix(tag_prefix);
    if (prefix.size() <= proto.size() && 0 == strncmp(proto.data(), prefix.data(), prefix.size())) {
      retval = proto.data();
    } else if (_ua.get_txn()) {
      retval = _ua.get_txn()->protocol_contains(prefix);
    }
  }
  return retval;
}

// Fill in the server protocols used.  Return the number of entries populated.
int
HttpSM::populate_server_protocol(std::string_view *result, int n) const
{
  int retval = 0;
  if (!t_state.hdr_info.server_request.valid()) {
    return retval;
  }
  if (n > 0) {
    std::string_view proto = HttpSM::find_proto_string(t_state.hdr_info.server_request.version_get());
    if (!proto.empty()) {
      result[retval++] = proto;
      if (n > retval && server_txn) {
        retval += server_txn->populate_protocol(result + retval, n - retval);
      }
    }
  }
  return retval;
}

// Look for a specific server protocol
const char *
HttpSM::server_protocol_contains(std::string_view tag_prefix) const
{
  const char      *retval = nullptr;
  std::string_view proto  = HttpSM::find_proto_string(t_state.hdr_info.server_request.version_get());
  if (!proto.empty()) {
    std::string_view prefix(tag_prefix);
    if (prefix.size() <= proto.size() && 0 == strncmp(proto.data(), prefix.data(), prefix.size())) {
      retval = proto.data();
    } else {
      if (server_txn) {
        retval = server_txn->protocol_contains(prefix);
      }
    }
  }
  return retval;
}

std::string_view
HttpSM::find_proto_string(HTTPVersion version) const
{
  if (version == HTTP_2_0) {
    return IP_PROTO_TAG_HTTP_2_0;
  } else if (version == HTTP_1_1) {
    return IP_PROTO_TAG_HTTP_1_1;
  } else if (version == HTTP_1_0) {
    return IP_PROTO_TAG_HTTP_1_0;
  } else if (version == HTTP_0_9) {
    return IP_PROTO_TAG_HTTP_0_9;
  }
  return {};
}

void
HttpSM::rewind_state_machine()
{
  callout_state = HttpApiState_t::REWIND_STATE_MACHINE;
}

// YTS Team, yamsat Plugin
// Function to copy the partial Post data while tunnelling
int64_t
PostDataBuffers::copy_partial_post_data(int64_t consumed_bytes)
{
  if (post_data_buffer_done) {
    return 0;
  }
  int64_t const bytes_to_copy = std::min(consumed_bytes, this->ua_buffer_reader->read_avail());
  Dbg(dbg_ctl_http_redirect,
      "given %" PRId64 " bytes consumed, copying %" PRId64 " bytes to buffers with %" PRId64 " available bytes", consumed_bytes,
      bytes_to_copy, this->ua_buffer_reader->read_avail());
  this->postdata_copy_buffer->write(this->ua_buffer_reader, bytes_to_copy);
  this->ua_buffer_reader->consume(bytes_to_copy);
  return bytes_to_copy;
}

IOBufferReader *
PostDataBuffers::get_post_data_buffer_clone_reader()
{
  return this->postdata_copy_buffer->clone_reader(this->postdata_copy_buffer_start);
}

// YTS Team, yamsat Plugin
// Allocating the post data buffers
void
PostDataBuffers::init(IOBufferReader *ua_reader)
{
  Dbg(dbg_ctl_http_redirect, "[PostDataBuffers::init]");

  this->ua_buffer_reader = ua_reader;

  if (this->postdata_copy_buffer == nullptr) {
    this->post_data_buffer_done = false;
    ink_assert(this->postdata_copy_buffer_start == nullptr);
    this->postdata_copy_buffer       = new_empty_MIOBuffer(BUFFER_SIZE_INDEX_4K);
    this->postdata_copy_buffer_start = this->postdata_copy_buffer->alloc_reader();
  }

  ink_assert(this->ua_buffer_reader != nullptr);
}

// YTS Team, yamsat Plugin
// Deallocating the post data buffers
void
PostDataBuffers::clear()
{
  Dbg(dbg_ctl_http_redirect, "[PostDataBuffers::clear]");

  if (this->postdata_copy_buffer != nullptr) {
    free_MIOBuffer(this->postdata_copy_buffer);
    this->postdata_copy_buffer       = nullptr;
    this->postdata_copy_buffer_start = nullptr; // deallocated by the buffer
  }
  this->post_data_buffer_done = false;
}

PostDataBuffers::~PostDataBuffers()
{
  this->clear();
}

HTTPVersion
HttpSM::get_server_version(HTTPHdr &hdr) const
{
  return this->server_txn->get_proxy_ssn()->get_version(hdr);
}

/// Update the milestone state given the milestones and timer.
void
HttpSM::milestone_update_api_time()
{
  // Bit of funkiness - we set @a api_timer to be the negative value when we're tracking
  // non-active API time. In that case we need to make a note of it and flip the value back
  // to positive.
  if (api_timer) {
    ink_hrtime delta;
    bool       active = api_timer >= 0;
    if (!active) {
      api_timer = -api_timer;
    }
    // Zero or negative time is a problem because we want to signal *something* happened
    // vs. no API activity at all. This can happen due to graininess or real time
    // clock adjustment.
    delta     = std::max<ink_hrtime>(1, ink_get_hrtime() - api_timer);
    api_timer = 0;

    if (0 == milestones[TS_MILESTONE_PLUGIN_TOTAL]) {
      milestones[TS_MILESTONE_PLUGIN_TOTAL] = milestones[TS_MILESTONE_SM_START];
    }
    milestones[TS_MILESTONE_PLUGIN_TOTAL] += delta;
    if (active) {
      if (0 == milestones[TS_MILESTONE_PLUGIN_ACTIVE]) {
        milestones[TS_MILESTONE_PLUGIN_ACTIVE] = milestones[TS_MILESTONE_SM_START];
      }
      milestones[TS_MILESTONE_PLUGIN_ACTIVE] += delta;
    }
    this_ethread()->metrics.record_api_time(delta);
  }
}
