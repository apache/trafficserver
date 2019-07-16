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

#include "../ProxyTransaction.h"
#include "HttpSM.h"
#include "HttpTransact.h"
#include "HttpTransactHeaders.h"
#include "ProxyConfig.h"
#include "Http1ServerSession.h"
#include "HttpDebugNames.h"
#include "HttpSessionManager.h"
#include "P_Cache.h"
#include "P_Net.h"
#include "StatPages.h"
#include "Log.h"
#include "LogAccess.h"
#include "PluginVC.h"
#include "ReverseProxy.h"
#include "RemapProcessor.h"
#include "Transform.h"
#include "P_SSLConfig.h"
#include "HttpPages.h"
#include "IPAllow.h"
#include "tscore/I_Layout.h"
#include "tscore/bwf_std_format.h"

#include <openssl/ossl_typ.h>
#include <openssl/ssl.h>
#include <algorithm>
#include <atomic>
#include <logging/Log.h>

#define DEFAULT_RESPONSE_BUFFER_SIZE_INDEX 6 // 8K
#define DEFAULT_REQUEST_BUFFER_SIZE_INDEX 6  // 8K
#define MIN_CONFIG_BUFFER_SIZE_INDEX 5       // 4K

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

// We have a debugging list that can use to find stuck
//  state machines
DList(HttpSM, debug_link) debug_sm_list;
ink_mutex debug_sm_list_mutex;

static const int sub_header_size = sizeof("Content-type: ") - 1 + 2 + sizeof("Content-range: bytes ") - 1 + 4;
static const int boundary_size   = 2 + sizeof("RANGE_SEPARATOR") - 1 + 2;

static const char *str_100_continue_response = "HTTP/1.1 100 Continue\r\n\r\n";
static const int len_100_continue_response   = strlen(str_100_continue_response);

// Handy typedef for short (single line) message generation.
using lbw = ts::LocalBufferWriter<256>;

namespace
{
/// Update the milestone state given the milestones and timer.
inline void
milestone_update_api_time(TransactionMilestones &milestones, ink_hrtime &api_timer)
{
  // Bit of funkiness - we set @a api_timer to be the negative value when we're tracking
  // non-active API time. In that case we need to make a note of it and flip the value back
  // to positive.
  if (api_timer) {
    ink_hrtime delta;
    bool active = api_timer >= 0;
    if (!active) {
      api_timer = -api_timer;
    }
    delta     = Thread::get_hrtime_updated() - api_timer;
    api_timer = 0;
    // Zero or negative time is a problem because we want to signal *something* happened
    // vs. no API activity at all. This can happen due to graininess or real time
    // clock adjustment.
    if (delta <= 0) {
      delta = 1;
    }

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
  }
}

// Unique state machine identifier
std::atomic<int64_t> next_sm_id(0);
} // namespace

ClassAllocator<HttpSM> httpSMAllocator("httpSMAllocator");

HttpVCTable::HttpVCTable(HttpSM *mysm)
{
  memset(&vc_table, 0, sizeof(vc_table));
  sm = mysm;
}

HttpVCTableEntry *
HttpVCTable::new_entry()
{
  for (int i = 0; i < vc_table_max_entries; i++) {
    if (vc_table[i].vc == nullptr) {
      vc_table[i].sm = sm;
      return vc_table + i;
    }
  }

  ink_release_assert(0);
  return nullptr;
}

HttpVCTableEntry *
HttpVCTable::find_entry(VConnection *vc)
{
  for (int i = 0; i < vc_table_max_entries; i++) {
    if (vc_table[i].vc == vc) {
      return vc_table + i;
    }
  }

  return nullptr;
}

HttpVCTableEntry *
HttpVCTable::find_entry(VIO *vio)
{
  for (int i = 0; i < vc_table_max_entries; i++) {
    if (vc_table[i].read_vio == vio || vc_table[i].write_vio == vio) {
      ink_assert(vc_table[i].vc != nullptr);
      return vc_table + i;
    }
  }

  return nullptr;
}

// bool HttpVCTable::remove_entry(HttpVCEntry* e)
//
//    Deallocates all buffers from the associated
//      entry and re-initializes it's other fields
//      for reuse
//
void
HttpVCTable::remove_entry(HttpVCTableEntry *e)
{
  ink_assert(e->vc == nullptr || e->in_tunnel);
  e->vc  = nullptr;
  e->eos = false;
  if (e->read_buffer) {
    free_MIOBuffer(e->read_buffer);
    e->read_buffer = nullptr;
  }
  if (e->write_buffer) {
    free_MIOBuffer(e->write_buffer);
    e->write_buffer = nullptr;
  }
  if (e->read_vio != nullptr && e->read_vio->cont == sm) {
    // Cleanup dangling i/o
    if (e == sm->get_ua_entry() && sm->get_ua_txn() != nullptr) {
      e->read_vio = sm->get_ua_txn()->do_io_read(nullptr, 0, nullptr);
    } else if (e == sm->get_server_entry() && sm->get_server_session()) {
      e->read_vio = sm->get_server_session()->do_io_read(nullptr, 0, nullptr);
    } else {
      ink_release_assert(false);
    }
  }
  if (e->write_vio != nullptr && e->write_vio->cont == sm) {
    // Cleanup dangling i/o
    if (e == sm->get_ua_entry() && sm->get_ua_txn()) {
      e->write_vio = sm->get_ua_txn()->do_io_write(nullptr, 0, nullptr);
    } else if (e == sm->get_server_entry() && sm->get_server_session()) {
      e->write_vio = sm->get_server_session()->do_io_write(nullptr, 0, nullptr);
    } else {
      ink_release_assert(false);
    }
  }
  e->read_vio   = nullptr;
  e->write_vio  = nullptr;
  e->vc_handler = nullptr;
  e->vc_type    = HTTP_UNKNOWN;
  e->in_tunnel  = false;
}

// bool HttpVCTable::cleanup_entry(HttpVCEntry* e)
//
//    Closes the associate vc for the entry,
//     and the call remove_entry
//
void
HttpVCTable::cleanup_entry(HttpVCTableEntry *e)
{
  ink_assert(e->vc);
  if (e->in_tunnel == false) {
    // Update stats
    switch (e->vc_type) {
    case HTTP_UA_VC:
      // proxy.process.http.current_client_transactions is decremented in HttpSM::destroy
      break;
    default:
      // This covers:
      // HTTP_UNKNOWN, HTTP_SERVER_VC, HTTP_TRANSFORM_VC, HTTP_CACHE_READ_VC,
      // HTTP_CACHE_WRITE_VC, HTTP_RAW_SERVER_VC
      break;
    }

    e->vc->do_io_close();
    e->vc = nullptr;
  }
  remove_entry(e);
}

void
HttpVCTable::cleanup_all()
{
  for (int i = 0; i < vc_table_max_entries; i++) {
    if (vc_table[i].vc != nullptr) {
      cleanup_entry(vc_table + i);
    }
  }
}

#define SMDebug(tag, ...) SpecificDebug(debug_on, tag, __VA_ARGS__)

#define REMEMBER(e, r)                             \
  {                                                \
    history.push_back(MakeSourceLocation(), e, r); \
  }

#ifdef STATE_ENTER
#undef STATE_ENTER
#endif
#define STATE_ENTER(state_name, event)                                                                    \
  {                                                                                                       \
    /*ink_assert (magic == HTTP_SM_MAGIC_ALIVE); */ REMEMBER(event, reentrancy_count);                    \
    SMDebug("http", "[%" PRId64 "] [%s, %s]", sm_id, #state_name, HttpDebugNames::get_event_name(event)); \
  }

#define HTTP_SM_SET_DEFAULT_HANDLER(_h)   \
  {                                       \
    REMEMBER(NO_EVENT, reentrancy_count); \
    default_handler = _h;                 \
  }

HttpSM::HttpSM() : Continuation(nullptr), vc_table(this)
{
  ink_zero(http_parser);
}

void
HttpSM::cleanup()
{
  t_state.destroy();
  api_hooks.clear();
  http_parser_clear(&http_parser);

  // t_state.content_control.cleanup();

  HttpConfig::release(t_state.http_config_param);
  m_remap->release();

  mutex.clear();
  tunnel.mutex.clear();
  cache_sm.mutex.clear();
  transform_cache_sm.mutex.clear();
  magic    = HTTP_SM_MAGIC_DEAD;
  debug_on = false;
}

void
HttpSM::destroy()
{
  cleanup();
  httpSMAllocator.free(this);
}

void
HttpSM::init()
{
  milestones[TS_MILESTONE_SM_START] = Thread::get_hrtime();

  magic = HTTP_SM_MAGIC_ALIVE;

  // Unique state machine identifier
  sm_id                    = next_sm_id++;
  t_state.state_machine_id = sm_id;
  t_state.state_machine    = this;

  t_state.http_config_param = HttpConfig::acquire();
  // Acquire a lease on the global remap / rewrite table (stupid global name ...)
  m_remap = rewrite_table->acquire();

  // Simply point to the global config for the time being, no need to copy this
  // entire struct if nothing is going to change it.
  t_state.txn_conf = &t_state.http_config_param->oride;

  t_state.init();

  // Added to skip dns if the document is in cache. DNS will be forced if there is a ip based ACL in
  // cache control or parent.config or if the doc_in_cache_skip_dns is disabled or if http caching is disabled
  // TODO: This probably doesn't honor this as a per-transaction overridable config.
  t_state.force_dns = (ip_rule_in_CacheControlTable() || t_state.parent_params->parent_table->ipMatch ||
                       !(t_state.txn_conf->doc_in_cache_skip_dns) || !(t_state.txn_conf->cache_http));

  http_parser_init(&http_parser);

  SET_HANDLER(&HttpSM::main_handler);

#ifdef USE_HTTP_DEBUG_LISTS
  ink_mutex_acquire(&debug_sm_list_mutex);
  debug_sm_list.push(this);
  ink_mutex_release(&debug_sm_list_mutex);
#endif
}

void
HttpSM::set_ua_half_close_flag()
{
  ua_txn->set_half_close_flag(true);
}

inline void
HttpSM::do_api_callout()
{
  if (hooks_set) {
    do_api_callout_internal();
  } else {
    handle_api_return();
  }
}

int
HttpSM::state_add_to_list(int event, void * /* data ATS_UNUSED */)
{
  // The list if for stat pages and general debugging
  //   The config variable exists mostly to allow us to
  //   measure an performance drop during benchmark runs
  if (t_state.http_config_param->enable_http_info) {
    STATE_ENTER(&HttpSM::state_add_to_list, event);
    ink_assert(event == EVENT_NONE || event == EVENT_INTERVAL);

    int bucket = ((unsigned int)sm_id % HTTP_LIST_BUCKETS);

    MUTEX_TRY_LOCK(lock, HttpSMList[bucket].mutex, mutex->thread_holding);
    // the client_vc`s timeout events can be triggered, so we should not
    // reschedule the http_sm when the lock is not acquired.
    // FIXME: the sm_list may miss some http_sms when the lock contention
    if (lock.is_locked()) {
      HttpSMList[bucket].sm_list.push(this);
    }
  }

  t_state.api_next_action = HttpTransact::SM_ACTION_API_SM_START;
  do_api_callout();
  return EVENT_DONE;
}

int
HttpSM::state_remove_from_list(int event, void * /* data ATS_UNUSED */)
{
  // The config parameters are guaranteed not change
  //   across the life of a transaction so it safe to
  //   check the config here and use it determine
  //   whether we need to strip ourselves off of the
  //   state page list
  if (t_state.http_config_param->enable_http_info) {
    STATE_ENTER(&HttpSM::state_remove_from_list, event);
    ink_assert(event == EVENT_NONE || event == EVENT_INTERVAL);

    int bucket = ((unsigned int)sm_id % HTTP_LIST_BUCKETS);

    MUTEX_TRY_LOCK(lock, HttpSMList[bucket].mutex, mutex->thread_holding);
    if (!lock.is_locked()) {
      HTTP_SM_SET_DEFAULT_HANDLER(&HttpSM::state_remove_from_list);
      mutex->thread_holding->schedule_in(this, HTTP_LIST_RETRY);
      return EVENT_DONE;
    }

    HttpSMList[bucket].sm_list.remove(this);
  }

  return this->kill_this_async_hook(EVENT_NONE, nullptr);
}

int
HttpSM::kill_this_async_hook(int /* event ATS_UNUSED */, void * /* data ATS_UNUSED */)
{
  // In the base HttpSM, we don't have anything to
  //   do here.  subclasses can override this function
  //   to do their own asynchronous cleanup
  // So We're now ready to finish off the state machine
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
HttpSM::attach_client_session(ProxyTransaction *client_vc, IOBufferReader *buffer_reader)
{
  milestones[TS_MILESTONE_UA_BEGIN] = Thread::get_hrtime();
  ink_assert(client_vc != nullptr);

  NetVConnection *netvc = client_vc->get_netvc();
  if (!netvc) {
    return;
  }
  ua_txn = client_vc;

  // It seems to be possible that the ua_txn pointer will go stale before log entries for this HTTP transaction are
  // generated.  Therefore, collect information that may be needed for logging from the ua_txn object at this point.
  //
  _client_transaction_id = ua_txn->get_transaction_id();
  {
    auto p = ua_txn->get_proxy_ssn();

    if (p) {
      _client_connection_id = p->connection_id();
    }
  }

  // Collect log & stats information. We've already verified that the netvc is !nullptr above,
  // and netvc == ua_txn->get_netvc().
  SSLNetVConnection *ssl_vc = dynamic_cast<SSLNetVConnection *>(netvc);

  is_internal       = netvc->get_is_internal_request();
  mptcp_state       = netvc->get_mptcp_state();
  client_tcp_reused = !(ua_txn->is_first_transaction());

  if (ssl_vc != nullptr) {
    client_connection_is_ssl = true;
    client_ssl_reused        = ssl_vc->getSSLSessionCacheHit();
    const char *protocol     = ssl_vc->getSSLProtocol();
    client_sec_protocol      = protocol ? protocol : "-";
    const char *cipher       = ssl_vc->getSSLCipherSuite();
    client_cipher_suite      = cipher ? cipher : "-";
    const char *curve        = ssl_vc->getSSLCurve();
    client_curve             = curve ? curve : "-";
    if (!client_tcp_reused) {
      // Copy along the TLS handshake timings
      milestones[TS_MILESTONE_TLS_HANDSHAKE_START] = ssl_vc->sslHandshakeBeginTime;
      milestones[TS_MILESTONE_TLS_HANDSHAKE_END]   = ssl_vc->sslHandshakeEndTime;
    }
  }
  const char *protocol_str = client_vc->get_protocol_string();
  client_protocol          = protocol_str ? protocol_str : "-";

  ink_release_assert(ua_txn->get_half_close_flag() == false);
  mutex = client_vc->mutex;
  if (ua_txn->debug()) {
    debug_on = true;
  }

  start_sub_sm();

  // Allocate a user agent entry in the state machine's
  //   vc table
  ua_entry          = vc_table.new_entry();
  ua_entry->vc      = client_vc;
  ua_entry->vc_type = HTTP_UA_VC;

  ats_ip_copy(&t_state.client_info.src_addr, netvc->get_remote_addr());
  ats_ip_copy(&t_state.client_info.dst_addr, netvc->get_local_addr());
  t_state.client_info.dst_addr.port() = netvc->get_local_port();
  t_state.client_info.is_transparent  = netvc->get_is_transparent();
  t_state.client_info.port_attribute  = static_cast<HttpProxyPort::TransportType>(netvc->attributes);

  // Record api hook set state
  hooks_set = client_vc->has_hooks();

  // Setup for parsing the header
  ua_buffer_reader     = buffer_reader;
  ua_entry->vc_handler = &HttpSM::state_read_client_request_header;
  t_state.hdr_info.client_request.destroy();
  t_state.hdr_info.client_request.create(HTTP_TYPE_REQUEST);
  http_parser_init(&http_parser);

  // Prepare raw reader which will live until we are sure this is HTTP indeed
  if (is_transparent_passthrough_allowed() || (ssl_vc && ssl_vc->decrypt_tunnel())) {
    ua_raw_buffer_reader = buffer_reader->clone();
  }

  // We first need to run the transaction start hook.  Since
  //  this hook maybe asynchronous, we need to disable IO on
  //  client but set the continuation to be the state machine
  //  so if we get an timeout events the sm handles them
  ua_entry->read_vio = client_vc->do_io_read(this, 0, buffer_reader->mbuf);

  /////////////////////////
  // set up timeouts     //
  /////////////////////////
  client_vc->set_inactivity_timeout(HRTIME_SECONDS(t_state.http_config_param->accept_no_activity_timeout));
  client_vc->set_active_timeout(HRTIME_SECONDS(t_state.txn_conf->transaction_active_timeout_in));

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
  ink_assert(ua_entry->vc_handler == &HttpSM::state_read_client_request_header);

  ua_entry->read_vio = ua_txn->do_io_read(this, INT64_MAX, ua_buffer_reader->mbuf);
  // The header may already be in the buffer if this
  //  a request from a keep-alive connection
  handleEvent(VC_EVENT_READ_READY, ua_entry->read_vio);
}

void
HttpSM::setup_blind_tunnel_port()
{
  NetVConnection *netvc     = ua_txn->get_netvc();
  SSLNetVConnection *ssl_vc = dynamic_cast<SSLNetVConnection *>(netvc);
  int host_len;
  if (ssl_vc) {
    if (!t_state.hdr_info.client_request.url_get()->host_get(&host_len)) {
      // the URL object has not been created in the start of the transaction. Hence, we need to create the URL here
      URL u;

      t_state.hdr_info.client_request.create(HTTP_TYPE_REQUEST);
      t_state.hdr_info.client_request.method_set(HTTP_METHOD_CONNECT, HTTP_LEN_CONNECT);
      t_state.hdr_info.client_request.url_create(&u);
      u.scheme_set(URL_SCHEME_TUNNEL, URL_LEN_TUNNEL);
      t_state.hdr_info.client_request.url_set(&u);
      if (ssl_vc->has_tunnel_destination()) {
        const char *tunnel_host = ssl_vc->get_tunnel_host();
        t_state.hdr_info.client_request.url_get()->host_set(tunnel_host, strlen(tunnel_host));
        if (ssl_vc->get_tunnel_port() > 0) {
          t_state.hdr_info.client_request.url_get()->port_set(ssl_vc->get_tunnel_port());
        } else {
          t_state.hdr_info.client_request.url_get()->port_set(t_state.state_machine->ua_txn->get_netvc()->get_local_port());
        }
      } else {
        t_state.hdr_info.client_request.url_get()->host_set(ssl_vc->serverName, strlen(ssl_vc->serverName));
        t_state.hdr_info.client_request.url_get()->port_set(t_state.state_machine->ua_txn->get_netvc()->get_local_port());
      }
    }
  } else {
    char new_host[INET6_ADDRSTRLEN];
    ats_ip_ntop(t_state.state_machine->ua_txn->get_netvc()->get_local_addr(), new_host, sizeof(new_host));

    t_state.hdr_info.client_request.url_get()->host_set(new_host, strlen(new_host));
    t_state.hdr_info.client_request.url_get()->port_set(t_state.state_machine->ua_txn->get_netvc()->get_local_port());
  }
  call_transact_and_set_next_state(HttpTransact::HandleBlindTunnel);
}

int
HttpSM::state_read_client_request_header(int event, void *data)
{
  STATE_ENTER(&HttpSM::state_read_client_request_header, event);

  ink_assert(ua_entry->read_vio == (VIO *)data);
  ink_assert(server_entry == nullptr);
  ink_assert(server_session == nullptr);

  int bytes_used = 0;
  ink_assert(ua_entry->eos == false);

  NetVConnection *netvc = ua_txn->get_netvc();
  if (!netvc && event != VC_EVENT_EOS) {
    return 0;
  }

  switch (event) {
  case VC_EVENT_READ_READY:
  case VC_EVENT_READ_COMPLETE:
    // More data to parse
    break;

  case VC_EVENT_EOS:
    ua_entry->eos = true;
    if ((client_request_hdr_bytes > 0) && is_transparent_passthrough_allowed() && (ua_raw_buffer_reader != nullptr)) {
      break;
    }
  // Fall through
  case VC_EVENT_ERROR:
  case VC_EVENT_INACTIVITY_TIMEOUT:
  case VC_EVENT_ACTIVE_TIMEOUT:
    // The user agent is hosed.  Close it &
    //   bail on the state machine
    vc_table.cleanup_entry(ua_entry);
    ua_entry = nullptr;
    set_ua_abort(HttpTransact::ABORTED, event);
    terminate_sm = true;
    return 0;
  }

  // Reset the inactivity timeout if this is the first
  //   time we've been called.  The timeout had been set to
  //   the accept timeout by the ProxyTransaction
  //
  if ((ua_buffer_reader->read_avail() > 0) && (client_request_hdr_bytes == 0)) {
    milestones[TS_MILESTONE_UA_FIRST_READ] = Thread::get_hrtime();
    ua_txn->set_inactivity_timeout(HRTIME_SECONDS(t_state.txn_conf->transaction_no_activity_timeout_in));
  }
  /////////////////////
  // tokenize header //
  /////////////////////

  ParseResult state = t_state.hdr_info.client_request.parse_req(&http_parser, ua_buffer_reader, &bytes_used, ua_entry->eos,
                                                                t_state.http_config_param->strict_uri_parsing);

  client_request_hdr_bytes += bytes_used;

  // Check to see if we are over the hdr size limit
  if (client_request_hdr_bytes > t_state.txn_conf->request_hdr_max_size) {
    SMDebug("http", "client header bytes were over max header size; treating as a bad request");
    state = PARSE_RESULT_ERROR;
  }

  // We need to handle EOS as well as READ_READY because the client
  // may have sent all of the data already followed by a fIN and that
  // should be OK.
  if (ua_raw_buffer_reader != nullptr) {
    bool do_blind_tunnel = false;
    // If we had a parse error and we're done reading data
    // blind tunnel
    if ((event == VC_EVENT_READ_READY || event == VC_EVENT_EOS) && state == PARSE_RESULT_ERROR) {
      do_blind_tunnel = true;

      // If we had a GET request that has data after the
      // get request, do blind tunnel
    } else if (state == PARSE_RESULT_DONE && t_state.hdr_info.client_request.method_get_wksidx() == HTTP_WKSIDX_GET &&
               ua_buffer_reader->read_avail() > 0 && !t_state.hdr_info.client_request.is_keep_alive_set()) {
      do_blind_tunnel = true;
    }
    if (do_blind_tunnel) {
      SMDebug("http", "[%" PRId64 "] first request on connection failed parsing, switching to passthrough.", sm_id);

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
        t_state.client_info.keep_alive = HTTP_NO_KEEPALIVE;
      }
      return 0;
    }
  }

  // Check to see if we are done parsing the header
  if (state != PARSE_RESULT_CONT || ua_entry->eos || (state == PARSE_RESULT_CONT && event == VC_EVENT_READ_COMPLETE)) {
    if (ua_raw_buffer_reader != nullptr) {
      ua_raw_buffer_reader->dealloc();
      ua_raw_buffer_reader = nullptr;
    }
    http_parser_clear(&http_parser);
    ua_entry->vc_handler                         = &HttpSM::state_watch_for_client_abort;
    milestones[TS_MILESTONE_UA_READ_HEADER_DONE] = Thread::get_hrtime();
  }

  switch (state) {
  case PARSE_RESULT_ERROR:
    SMDebug("http", "[%" PRId64 "] error parsing client request header", sm_id);

    // Disable further I/O on the client
    ua_entry->read_vio->nbytes = ua_entry->read_vio->ndone;

    call_transact_and_set_next_state(HttpTransact::BadRequest);
    break;

  case PARSE_RESULT_CONT:
    if (ua_entry->eos) {
      SMDebug("http_seq", "[%" PRId64 "] EOS before client request parsing finished", sm_id);
      set_ua_abort(HttpTransact::ABORTED, event);

      // Disable further I/O on the client
      ua_entry->read_vio->nbytes = ua_entry->read_vio->ndone;

      call_transact_and_set_next_state(HttpTransact::BadRequest);
      break;
    } else if (event == VC_EVENT_READ_COMPLETE) {
      SMDebug("http_parse", "[%" PRId64 "] VC_EVENT_READ_COMPLETE and PARSE CONT state", sm_id);
      break;
    } else {
      if (is_transparent_passthrough_allowed() && ua_raw_buffer_reader != nullptr &&
          ua_raw_buffer_reader->get_current_block()->write_avail() <= 0) {
        // Disable passthrough regardless of eventual parsing failure or success -- otherwise
        // we either have to consume some data or risk blocking the writer.
        ua_raw_buffer_reader->dealloc();
        ua_raw_buffer_reader = nullptr;
      }
      ua_entry->read_vio->reenable();
      return VC_EVENT_CONT;
    }
  case PARSE_RESULT_DONE:
    SMDebug("http", "[%" PRId64 "] done parsing client request header", sm_id);

    ua_txn->set_session_active();

    if (t_state.hdr_info.client_request.version_get() == HTTPVersion(1, 1) &&
        (t_state.hdr_info.client_request.method_get_wksidx() == HTTP_WKSIDX_POST ||
         t_state.hdr_info.client_request.method_get_wksidx() == HTTP_WKSIDX_PUT) &&
        t_state.http_config_param->send_100_continue_response) {
      int len            = 0;
      const char *expect = t_state.hdr_info.client_request.value_get(MIME_FIELD_EXPECT, MIME_LEN_EXPECT, &len);
      // When receive an "Expect: 100-continue" request from client, ATS sends a "100 Continue" response to client
      // immediately, before receive the real response from original server.
      if ((len == HTTP_LEN_100_CONTINUE) && (strncasecmp(expect, HTTP_VALUE_100_CONTINUE, HTTP_LEN_100_CONTINUE) == 0)) {
        int64_t alloc_index = buffer_size_to_index(len_100_continue_response);
        if (ua_entry->write_buffer) {
          free_MIOBuffer(ua_entry->write_buffer);
          ua_entry->write_buffer = nullptr;
        }
        ua_entry->write_buffer    = new_MIOBuffer(alloc_index);
        IOBufferReader *buf_start = ua_entry->write_buffer->alloc_reader();

        t_state.hdr_info.client_request.m_100_continue_required = true;

        SMDebug("http_seq", "send 100 Continue response to client");
        int64_t nbytes      = ua_entry->write_buffer->write(str_100_continue_response, len_100_continue_response);
        ua_entry->write_vio = ua_txn->do_io_write(this, nbytes, buf_start);
      }
    }

    if (t_state.hdr_info.client_request.method_get_wksidx() == HTTP_WKSIDX_TRACE ||
        (t_state.hdr_info.client_request.get_content_length() == 0 &&
         t_state.client_info.transfer_encoding != HttpTransact::CHUNKED_ENCODING)) {
      // Enable further IO to watch for client aborts
      ua_entry->read_vio->reenable();
    } else {
      // Disable further I/O on the client since there could
      //  be body that we are tunneling POST/PUT/CONNECT or
      //  extension methods and we can't issue another
      //  another IO later for the body with a different buffer
      ua_entry->read_vio->nbytes = ua_entry->read_vio->ndone;
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
  bool chunked = (t_state.client_info.transfer_encoding == HttpTransact::CHUNKED_ENCODING);
  int64_t alloc_index;
  HttpTunnelProducer *p = nullptr;

  // content length is undefined, use default buffer size
  if (t_state.hdr_info.request_content_length == HTTP_UNDEFINED_CL) {
    alloc_index = (int)t_state.txn_conf->default_buffer_size_index;
    if (alloc_index < MIN_CONFIG_BUFFER_SIZE_INDEX || alloc_index > MAX_BUFFER_SIZE_INDEX) {
      alloc_index = DEFAULT_REQUEST_BUFFER_SIZE_INDEX;
    }
  } else {
    alloc_index = buffer_size_to_index(t_state.hdr_info.request_content_length);
  }
  MIOBuffer *post_buffer    = new_MIOBuffer(alloc_index);
  IOBufferReader *buf_start = post_buffer->alloc_reader();

  this->_postbuf.init(post_buffer->clone_reader(buf_start));

  // Note: Many browsers, Netscape and IE included send two extra
  //  bytes (CRLF) at the end of the post.  We just ignore those
  //  bytes since the sending them is not spec

  // Next order of business if copy the remaining data from the
  //  header buffer into new buffer
  int64_t post_bytes        = chunked ? INT64_MAX : t_state.hdr_info.request_content_length;
  client_request_body_bytes = post_buffer->write(ua_buffer_reader, chunked ? ua_buffer_reader->read_avail() : post_bytes);

  ua_buffer_reader->consume(client_request_body_bytes);
  p = tunnel.add_producer(ua_entry->vc, post_bytes, buf_start, &HttpSM::tunnel_handler_post_ua, HT_BUFFER_READ, "ua post buffer");
  if (chunked) {
    tunnel.set_producer_chunking_action(p, 0, TCA_PASSTHRU_CHUNKED_CONTENT);
  }
  ua_entry->in_tunnel = true;
  ua_txn->set_inactivity_timeout(HRTIME_SECONDS(t_state.txn_conf->transaction_no_activity_timeout_in));
  tunnel.tunnel_run(p);
}

int
HttpSM::state_watch_for_client_abort(int event, void *data)
{
  STATE_ENTER(&HttpSM::state_watch_for_client_abort, event);

  ink_assert(ua_entry->read_vio == (VIO *)data || ua_entry->write_vio == (VIO *)data);
  ink_assert(ua_entry->vc == ua_txn);

  switch (event) {
  /* EOS means that the client has initiated the connection shut down.
   * Only half close the client connection so ATS can read additional
   * data that may still be sent from the server and send it to the
   * client.
   */
  case VC_EVENT_EOS: {
    // We got an early EOS.
    NetVConnection *netvc = ua_txn->get_netvc();
    if (ua_txn->allow_half_open()) {
      if (netvc) {
        netvc->do_io_shutdown(IO_SHUTDOWN_READ);
      }
      ua_entry->eos = true;
    } else {
      ua_txn->do_io_close();
      ua_buffer_reader = nullptr;
      vc_table.cleanup_entry(ua_entry);
      ua_entry = nullptr;
      tunnel.kill_tunnel();
      terminate_sm = true; // Just die already, the requester is gone
      set_ua_abort(HttpTransact::ABORTED, event);
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
      HttpTunnelConsumer *c = tunnel.get_consumer(ua_txn);
      if (c && c->alive) {
        SMDebug("http",
                "[%" PRId64 "] [watch_for_client_abort] "
                "forwarding event %s to tunnel",
                sm_id, HttpDebugNames::get_event_name(event));
        tunnel.handleEvent(event, c->write_vio);
        return 0;
      } else {
        tunnel.kill_tunnel();
      }
    }
    // Disable further I/O on the client
    if (ua_entry->read_vio) {
      ua_entry->read_vio->nbytes = ua_entry->read_vio->ndone;
    }
    mark_server_down_on_client_abort();
    milestones[TS_MILESTONE_UA_CLOSE] = Thread::get_hrtime();
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
    ink_assert(t_state.hdr_info.client_request.m_100_continue_required);
    ua_entry->write_vio->reenable();
    break;
  case VC_EVENT_WRITE_COMPLETE:
    // 100-continue handler
    ink_assert(t_state.hdr_info.client_request.m_100_continue_required);
    if (ua_entry->write_buffer) {
      ink_assert(ua_entry->write_vio && !ua_entry->write_vio->ntodo());
      free_MIOBuffer(ua_entry->write_buffer);
      ua_entry->write_buffer = nullptr;
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
  ink_assert(server_session == nullptr);
  ink_assert(server_entry == nullptr);
  ink_assert(ua_txn != nullptr);
  ink_assert(t_state.method == HTTP_WKSIDX_PUSH);

  // Set the handler to read the pushed response hdr
  ua_entry->vc_handler = &HttpSM::state_read_push_response_header;

  // We record both the total payload size as
  //  client_request_body_bytes and the bytes for the individual
  //  pushed hdr and body components
  pushed_response_hdr_bytes = 0;
  client_request_body_bytes = 0;

  // Note: we must use destroy() here since clear()
  //  does not free the memory from the header
  t_state.hdr_info.server_response.destroy();
  t_state.hdr_info.server_response.create(HTTP_TYPE_RESPONSE);
  http_parser_clear(&http_parser);

  // We already done the READ when we read the client
  //  request header
  ink_assert(ua_entry->read_vio);

  // If there is anything in the buffer call the parsing routines
  //  since if the response is finished, we won't get any
  //  additional callbacks
  int resp_hdr_state = VC_EVENT_CONT;
  if (ua_buffer_reader->read_avail() > 0) {
    if (ua_entry->eos) {
      resp_hdr_state = state_read_push_response_header(VC_EVENT_EOS, ua_entry->read_vio);
    } else {
      resp_hdr_state = state_read_push_response_header(VC_EVENT_READ_READY, ua_entry->read_vio);
    }
  }
  // It is possible that the entire PUSHed response header was already
  //  in the buffer.  In this case we don't want to fire off any more
  //  IO since we are going to switch buffers when we go to tunnel to
  //  the cache
  if (resp_hdr_state == VC_EVENT_CONT) {
    ink_assert(ua_entry->eos == false);
    ua_entry->read_vio = ua_txn->do_io_read(this, INT64_MAX, ua_buffer_reader->mbuf);
  }
}

int
HttpSM::state_read_push_response_header(int event, void *data)
{
  STATE_ENTER(&HttpSM::state_read_push_response_header, event);
  ink_assert(ua_entry->read_vio == (VIO *)data);
  ink_assert(t_state.current.server == nullptr);

  switch (event) {
  case VC_EVENT_EOS:
    ua_entry->eos = true;
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

  int state = PARSE_RESULT_CONT;
  while (ua_buffer_reader->read_avail() && state == PARSE_RESULT_CONT) {
    const char *start = ua_buffer_reader->start();
    const char *tmp   = start;
    int64_t data_size = ua_buffer_reader->block_read_avail();
    ink_assert(data_size >= 0);

    /////////////////////
    // tokenize header //
    /////////////////////
    state = t_state.hdr_info.server_response.parse_resp(&http_parser, &tmp, tmp + data_size,
                                                        false); // Only call w/ eof when data exhausted

    int64_t bytes_used = tmp - start;

    ink_release_assert(bytes_used <= data_size);
    ua_buffer_reader->consume(bytes_used);
    pushed_response_hdr_bytes += bytes_used;
    client_request_body_bytes += bytes_used;
  }

  // We are out of data.  If we've received an EOS we need to
  //  call the parser with (eof == true) so it can determine
  //  whether to use the response as is or declare a parse error
  if (ua_entry->eos) {
    const char *end = ua_buffer_reader->start();
    state = t_state.hdr_info.server_response.parse_resp(&http_parser, &end, end, true // We are out of data after server eos
    );
    ink_release_assert(state == PARSE_RESULT_DONE || state == PARSE_RESULT_ERROR);
  }
  // Don't allow 0.9 (unparsable headers) since TS doesn't
  //   cache 0.9 responses
  if (state == PARSE_RESULT_DONE && t_state.hdr_info.server_response.version_get() == HTTPVersion(0, 9)) {
    state = PARSE_RESULT_ERROR;
  }

  if (state != PARSE_RESULT_CONT) {
    // Disable further IO
    ua_entry->read_vio->nbytes = ua_entry->read_vio->ndone;
    http_parser_clear(&http_parser);
    milestones[TS_MILESTONE_SERVER_READ_HEADER_DONE] = Thread::get_hrtime();
  }

  switch (state) {
  case PARSE_RESULT_ERROR:
    SMDebug("http", "[%" PRId64 "] error parsing push response header", sm_id);
    call_transact_and_set_next_state(HttpTransact::HandleBadPushRespHdr);
    break;

  case PARSE_RESULT_CONT:
    ua_entry->read_vio->reenable();
    return VC_EVENT_CONT;

  case PARSE_RESULT_DONE:
    SMDebug("http", "[%" PRId64 "] done parsing push response header", sm_id);
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
  milestones[TS_MILESTONE_SERVER_CONNECT_END] = Thread::get_hrtime();
  NetVConnection *netvc                       = nullptr;

  pending_action = nullptr;
  switch (event) {
  case EVENT_INTERVAL:
    // If we get EVENT_INTERNAL it means that we moved the transaction
    // to a different thread in do_http_server_open.  Since we didn't
    // do any of the actual work in do_http_server_open, we have to
    // go back and do it now.
    do_http_server_open(true);
    return 0;

  case NET_EVENT_OPEN:

    // Record the VC in our table
    server_entry     = vc_table.new_entry();
    server_entry->vc = netvc = (NetVConnection *)data;
    server_entry->vc_type    = HTTP_RAW_SERVER_VC;
    t_state.current.state    = HttpTransact::CONNECTION_ALIVE;
    ats_ip_copy(&t_state.server_info.src_addr, netvc->get_local_addr());

    netvc->set_inactivity_timeout(HRTIME_SECONDS(t_state.txn_conf->transaction_no_activity_timeout_out));
    netvc->set_active_timeout(HRTIME_SECONDS(t_state.txn_conf->transaction_active_timeout_out));
    break;

  case VC_EVENT_ERROR:
  case NET_EVENT_OPEN_FAILED:
    t_state.current.state = HttpTransact::OPEN_RAW_ERROR;
    // use this value just to get around other values
    t_state.hdr_info.response_error = HttpTransact::STATUS_CODE_SERVER_ERROR;
    break;

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
    size = *((int64_t *)data);
    if (size != INT64_MAX && size >= 0) {
      // We got a content length so update our internal
      //   data as well as fix up the request header
      t_state.hdr_info.transform_request_cl = size;
      t_state.hdr_info.server_request.value_set_int64(MIME_FIELD_CONTENT_LENGTH, MIME_LEN_CONTENT_LENGTH, size);
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
  int64_t size = *((int64_t *)data);

  switch (event) {
  case TRANSFORM_READ_READY:
    if (size != INT64_MAX && size >= 0) {
      // We got a content length so update our internal state
      t_state.hdr_info.transform_response_cl = size;
      t_state.hdr_info.transform_response.value_set_int64(MIME_FIELD_CONTENT_LENGTH, MIME_LEN_CONTENT_LENGTH, size);
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
    // There are three reasons why the the tunnel could signal completed
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
      if (c->producer->vc_type == HT_HTTP_CLIENT) {
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
    if (c->producer->vc_type == HT_HTTP_CLIENT) {
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
//    deallocation the state machine if necessary
//
int
HttpSM::state_api_callback(int event, void *data)
{
  ink_release_assert(magic == HTTP_SM_MAGIC_ALIVE);

  ink_assert(reentrancy_count >= 0);
  reentrancy_count++;

  milestone_update_api_time(milestones, api_timer);

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
HttpSM::state_api_callout(int event, void *data)
{
  // enum and variable for figuring out what the next action is after
  //   after we've finished the api state
  enum AfterApiReturn_t {
    API_RETURN_UNKNOWN = 0,
    API_RETURN_CONTINUE,
    API_RETURN_DEFERED_CLOSE,
    API_RETURN_DEFERED_SERVER_ERROR,
    API_RETURN_ERROR_JUMP,
    API_RETURN_SHUTDOWN,
    API_RETURN_INVALIDATE_ERROR
  };
  AfterApiReturn_t api_next = API_RETURN_UNKNOWN;

  if (event != EVENT_NONE) {
    STATE_ENTER(&HttpSM::state_api_callout, event);
  }

  if (api_timer < 0) {
    // This happens when either the plugin lock was missed and the hook rescheduled or
    // the transaction got an event without the plugin calling TsHttpTxnReenable().
    // The call chain does not recurse here if @a api_timer < 0 which means this call
    // is the first from an event dispatch in this case.
    milestone_update_api_time(milestones, api_timer);
  }

  switch (event) {
  case HTTP_TUNNEL_EVENT_DONE:
  // This is a reschedule via the tunnel.  Just fall through
  //
  case EVENT_INTERVAL:
    if (data != pending_action) {
      pending_action->cancel();
    }
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

      t_state.hdr_info.client_request.create(HTTP_TYPE_REQUEST);
      t_state.hdr_info.client_request.method_set(HTTP_METHOD_CONNECT, HTTP_LEN_CONNECT);
      t_state.hdr_info.client_request.url_create(&u);
      u.scheme_set(URL_SCHEME_TUNNEL, URL_LEN_TUNNEL);
      t_state.hdr_info.client_request.url_set(&u);

      NetVConnection *netvc     = ua_txn->get_netvc();
      SSLNetVConnection *ssl_vc = dynamic_cast<SSLNetVConnection *>(netvc);

      if (ssl_vc && ssl_vc->has_tunnel_destination()) {
        const char *tunnel_host = ssl_vc->get_tunnel_host();
        t_state.hdr_info.client_request.url_get()->host_set(tunnel_host, strlen(tunnel_host));
        ushort tunnel_port = ssl_vc->get_tunnel_port();
        if (tunnel_port > 0) {
          t_state.hdr_info.client_request.url_get()->port_set(tunnel_port);
        } else {
          t_state.hdr_info.client_request.url_get()->port_set(t_state.state_machine->ua_txn->get_netvc()->get_local_port());
        }
      } else if (ssl_vc) {
        t_state.hdr_info.client_request.url_get()->host_set(ssl_vc->serverName, strlen(ssl_vc->serverName));
        t_state.hdr_info.client_request.url_get()->port_set(t_state.state_machine->ua_txn->get_netvc()->get_local_port());
      }
    }
  // FALLTHROUGH
  case HTTP_API_CONTINUE:
    if ((cur_hook_id >= 0) && (cur_hook_id < TS_HTTP_LAST_HOOK)) {
      if (!cur_hook) {
        if (cur_hooks == 0) {
          cur_hook = http_global_hooks->get(cur_hook_id);
          cur_hooks++;
        }
      }
      // even if ua_txn is NULL, cur_hooks must
      // be incremented otherwise cur_hooks is not set to 2 and
      // transaction hooks (stored in api_hooks object) are not called.
      if (!cur_hook) {
        if (cur_hooks == 1) {
          if (ua_txn) {
            cur_hook = ua_txn->ssn_hook_get(cur_hook_id);
          }
          cur_hooks++;
        }
      }
      if (!cur_hook) {
        if (cur_hooks == 2) {
          cur_hook = api_hooks.get(cur_hook_id);
          cur_hooks++;
        }
      }
      if (cur_hook) {
        if (callout_state == HTTP_API_NO_CALLOUT) {
          callout_state = HTTP_API_IN_CALLOUT;
        }

        MUTEX_TRY_LOCK(lock, cur_hook->m_cont->mutex, mutex->thread_holding);
        // Have a mutex but didn't get the lock, reschedule
        if (!lock.is_locked()) {
          api_timer = -Thread::get_hrtime_updated();
          HTTP_SM_SET_DEFAULT_HANDLER(&HttpSM::state_api_callout);
          ink_assert(pending_action == nullptr);
          pending_action = mutex->thread_holding->schedule_in(this, HRTIME_MSECONDS(10));
          // Should @a callout_state be reset back to HTTP_API_NO_CALLOUT here? Because the default
          // handler has been changed the value isn't important to the rest of the state machine
          // but not resetting means there is no way to reliably detect re-entrance to this state with an
          // outstanding callout.
          return 0;
        }
        SMDebug("http", "[%" PRId64 "] calling plugin on hook %s at hook %p", sm_id, HttpDebugNames::get_api_hook_name(cur_hook_id),
                cur_hook);

        APIHook *hook = cur_hook;
        cur_hook      = cur_hook->next();

        if (!api_timer) {
          api_timer = Thread::get_hrtime_updated();
        }
        hook->invoke(TS_EVENT_HTTP_READ_REQUEST_HDR + cur_hook_id, this);
        if (api_timer > 0) { // true if the hook did not call TxnReenable()
          milestone_update_api_time(milestones, api_timer);
          api_timer = -Thread::get_hrtime_updated(); // set in order to track non-active callout duration
          // which means that if we get back from the invoke with api_timer < 0 we're already
          // tracking a non-complete callout from a chain so just let it ride. It will get cleaned
          // up in state_api_callback when the plugin re-enables this transaction.
        }
        return 0;
      }
    }
    // Map the callout state into api_next
    switch (callout_state) {
    case HTTP_API_NO_CALLOUT:
    case HTTP_API_IN_CALLOUT:
      if (t_state.api_modifiable_cached_resp && t_state.api_update_cached_object == HttpTransact::UPDATE_CACHED_OBJECT_PREPARE) {
        t_state.api_update_cached_object = HttpTransact::UPDATE_CACHED_OBJECT_CONTINUE;
      }
      api_next = API_RETURN_CONTINUE;
      break;
    case HTTP_API_DEFERED_CLOSE:
      api_next = API_RETURN_DEFERED_CLOSE;
      break;
    case HTTP_API_DEFERED_SERVER_ERROR:
      api_next = API_RETURN_DEFERED_SERVER_ERROR;
      break;
    default:
      ink_release_assert(0);
    }
    break;

  case HTTP_API_ERROR:
    if (callout_state == HTTP_API_DEFERED_CLOSE) {
      api_next = API_RETURN_DEFERED_CLOSE;
    } else if (cur_hook_id == TS_HTTP_TXN_CLOSE_HOOK) {
      // If we are closing the state machine, we can't
      //   jump to an error state so just continue
      api_next = API_RETURN_CONTINUE;
    } else if (t_state.api_http_sm_shutdown) {
      t_state.api_http_sm_shutdown   = false;
      t_state.cache_info.object_read = nullptr;
      cache_sm.close_read();
      transform_cache_sm.close_read();
      release_server_session();
      terminate_sm                 = true;
      api_next                     = API_RETURN_SHUTDOWN;
      t_state.squid_codes.log_code = SQUID_LOG_TCP_DENIED;
    } else if (t_state.api_modifiable_cached_resp &&
               t_state.api_update_cached_object == HttpTransact::UPDATE_CACHED_OBJECT_PREPARE) {
      t_state.api_update_cached_object = HttpTransact::UPDATE_CACHED_OBJECT_ERROR;
      api_next                         = API_RETURN_INVALIDATE_ERROR;
    } else {
      api_next = API_RETURN_ERROR_JUMP;
    }
    break;

  default:
    ink_assert(false);
    terminate_sm = true;
    return 0;
  }

  // Now that we're completed with the api state and figured out what
  //   to do next, do it
  callout_state = HTTP_API_NO_CALLOUT;
  api_timer     = 0;
  switch (api_next) {
  case API_RETURN_CONTINUE:
    handle_api_return();
    break;
  case API_RETURN_DEFERED_CLOSE:
    ink_assert(t_state.api_next_action == HttpTransact::SM_ACTION_API_SM_SHUTDOWN);
    do_api_callout();
    break;
  case API_RETURN_DEFERED_SERVER_ERROR:
    ink_assert(t_state.api_next_action == HttpTransact::SM_ACTION_API_SEND_REQUEST_HDR);
    ink_assert(t_state.current.state != HttpTransact::CONNECTION_ALIVE);
    call_transact_and_set_next_state(HttpTransact::HandleResponse);
    break;
  case API_RETURN_ERROR_JUMP:
    call_transact_and_set_next_state(HttpTransact::HandleApiErrorJump);
    break;
  case API_RETURN_SHUTDOWN:
    break;
  case API_RETURN_INVALIDATE_ERROR:
    do_cache_prepare_update();
    break;
  default:
  case API_RETURN_UNKNOWN:
    ink_release_assert(0);
  }

  return 0;
}

// void HttpSM::handle_api_return()
//
//   Figures out what to do after calling api callouts
//    have finished.  This messy and I would like
//    to come up with a cleaner way to handle the api
//    return.  The way we are doing things also makes a
//    mess of set_next_state()
//
void
HttpSM::handle_api_return()
{
  switch (t_state.api_next_action) {
  case HttpTransact::SM_ACTION_API_SM_START: {
    NetVConnection *netvc     = ua_txn->get_netvc();
    SSLNetVConnection *ssl_vc = dynamic_cast<SSLNetVConnection *>(netvc);
    bool forward_dest         = ssl_vc != nullptr && ssl_vc->decrypt_tunnel();
    if (t_state.client_info.port_attribute == HttpProxyPort::TRANSPORT_BLIND_TUNNEL || forward_dest) {
      setup_blind_tunnel_port();
    } else {
      setup_client_read_request_header();
    }
    return;
  }
  case HttpTransact::SM_ACTION_API_CACHE_LOOKUP_COMPLETE:
  case HttpTransact::SM_ACTION_API_READ_CACHE_HDR:
    if (t_state.api_cleanup_cache_read && t_state.api_update_cached_object != HttpTransact::UPDATE_CACHED_OBJECT_PREPARE) {
      t_state.api_cleanup_cache_read = false;
      t_state.cache_info.object_read = nullptr;
      t_state.request_sent_time      = UNDEFINED_TIME;
      t_state.response_received_time = UNDEFINED_TIME;
      cache_sm.close_read();
      transform_cache_sm.close_read();
    }
    // fallthrough

  case HttpTransact::SM_ACTION_API_PRE_REMAP:
  case HttpTransact::SM_ACTION_API_POST_REMAP:
  case HttpTransact::SM_ACTION_API_READ_REQUEST_HDR:
  case HttpTransact::SM_ACTION_REQUEST_BUFFER_READ_COMPLETE:
  case HttpTransact::SM_ACTION_API_OS_DNS:
  case HttpTransact::SM_ACTION_API_READ_RESPONSE_HDR:
    call_transact_and_set_next_state(nullptr);
    return;
  case HttpTransact::SM_ACTION_API_SEND_REQUEST_HDR:
    setup_server_send_request();
    return;
  case HttpTransact::SM_ACTION_API_SEND_RESPONSE_HDR:
    // Set back the inactivity timeout
    if (ua_txn) {
      ua_txn->set_inactivity_timeout(HRTIME_SECONDS(t_state.txn_conf->transaction_no_activity_timeout_in));
    }

    // We only follow 3xx when redirect_in_process == false. Otherwise the redirection has already been launched (in
    // SM_ACTION_SERVER_READ).redirect_in_process is set before this logic if we need more direction.
    // This redirection is only used with the build_error_response. Then, the redirection_tries will be increased by
    // state_read_server_response_header and never get into this logic again.
    if (enable_redirection && !t_state.redirect_info.redirect_in_process && is_redirect_required()) {
      do_redirect();
    }
    // we have further processing to do
    //  based on what t_state.next_action is
    break;
  case HttpTransact::SM_ACTION_API_SM_SHUTDOWN:
    state_remove_from_list(EVENT_NONE, nullptr);
    return;
  default:
    ink_release_assert(!"Not reached");
    break;
  }

  switch (t_state.next_action) {
  case HttpTransact::SM_ACTION_TRANSFORM_READ: {
    HttpTunnelProducer *p = setup_transfer_from_transform();
    perform_transform_cache_write_action();
    tunnel.tunnel_run(p);
    break;
  }
  case HttpTransact::SM_ACTION_SERVER_READ: {
    if (unlikely(t_state.did_upgrade_succeed)) {
      // We've successfully handled the upgrade, let's now setup
      // a blind tunnel.
      IOBufferReader *initial_data = nullptr;
      if (t_state.is_websocket) {
        HTTP_INCREMENT_DYN_STAT(http_websocket_current_active_client_connections_stat);
        if (server_session) {
          initial_data = server_session->get_reader();
        }

        if (ua_txn) {
          SMDebug("http_websocket",
                  "(client session) Setting websocket active timeout=%" PRId64 "s and inactive timeout=%" PRId64 "s",
                  t_state.txn_conf->websocket_active_timeout, t_state.txn_conf->websocket_inactive_timeout);
          ua_txn->set_active_timeout(HRTIME_SECONDS(t_state.txn_conf->websocket_active_timeout));
          ua_txn->set_inactivity_timeout(HRTIME_SECONDS(t_state.txn_conf->websocket_inactive_timeout));
        }

        if (server_session) {
          SMDebug("http_websocket",
                  "(server session) Setting websocket active timeout=%" PRId64 "s and inactive timeout=%" PRId64 "s",
                  t_state.txn_conf->websocket_active_timeout, t_state.txn_conf->websocket_inactive_timeout);
          server_session->get_netvc()->set_active_timeout(HRTIME_SECONDS(t_state.txn_conf->websocket_active_timeout));
          server_session->get_netvc()->set_inactivity_timeout(HRTIME_SECONDS(t_state.txn_conf->websocket_inactive_timeout));
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
  case HttpTransact::SM_ACTION_SERVE_FROM_CACHE: {
    HttpTunnelProducer *p = setup_cache_read_transfer();
    tunnel.tunnel_run(p);
    break;
  }

  case HttpTransact::SM_ACTION_INTERNAL_CACHE_WRITE: {
    if (cache_sm.cache_write_vc) {
      setup_internal_transfer(&HttpSM::tunnel_handler_cache_fill);
    } else {
      setup_internal_transfer(&HttpSM::tunnel_handler);
    }
    break;
  }

  case HttpTransact::SM_ACTION_INTERNAL_CACHE_NOOP:
  case HttpTransact::SM_ACTION_INTERNAL_CACHE_DELETE:
  case HttpTransact::SM_ACTION_INTERNAL_CACHE_UPDATE_HEADERS:
  case HttpTransact::SM_ACTION_SEND_ERROR_CACHE_NOOP: {
    setup_internal_transfer(&HttpSM::tunnel_handler);
    break;
  }

  case HttpTransact::SM_ACTION_REDIRECT_READ: {
    // Clean up from any communication with previous servers
    release_server_session();

    call_transact_and_set_next_state(HttpTransact::HandleRequest);
    break;
  }
  case HttpTransact::SM_ACTION_SSL_TUNNEL: {
    setup_blind_tunnel(true);
    break;
  }
  default: {
    ink_release_assert(!"Should not get here");
  }
  }
}

//////////////////////////////////////////////////////////////////////////////
//
//  HttpSM::state_http_server_open()
//
//////////////////////////////////////////////////////////////////////////////
int
HttpSM::state_http_server_open(int event, void *data)
{
  SMDebug("http_track", "entered inside state_http_server_open");
  STATE_ENTER(&HttpSM::state_http_server_open, event);
  ink_release_assert(event == EVENT_INTERVAL || event == NET_EVENT_OPEN || event == NET_EVENT_OPEN_FAILED ||
                     pending_action == nullptr);
  if (event != NET_EVENT_OPEN) {
    pending_action = nullptr;
  }
  milestones[TS_MILESTONE_SERVER_CONNECT_END] = Thread::get_hrtime();
  NetVConnection *netvc                       = nullptr;

  switch (event) {
  case NET_EVENT_OPEN: {
    Http1ServerSession *session =
      (TS_SERVER_SESSION_SHARING_POOL_THREAD == t_state.http_config_param->server_session_sharing_pool) ?
        THREAD_ALLOC_INIT(httpServerSessionAllocator, mutex->thread_holding) :
        httpServerSessionAllocator.alloc();
    session->sharing_pool  = static_cast<TSServerSessionSharingPoolType>(t_state.http_config_param->server_session_sharing_pool);
    session->sharing_match = static_cast<TSServerSessionSharingMatchType>(t_state.txn_conf->server_session_sharing_match);

    netvc = static_cast<NetVConnection *>(data);
    session->attach_hostname(t_state.current.server->name);
    UnixNetVConnection *vc = static_cast<UnixNetVConnection *>(data);
    // Since the UnixNetVConnection::action_ or SocksEntry::action_ may be returned from netProcessor.connect_re, and the
    // SocksEntry::action_ will be copied into UnixNetVConnection::action_ before call back NET_EVENT_OPEN from SocksEntry::free(),
    // so we just compare the Continuation between pending_action and VC's action_.
    ink_release_assert(pending_action == nullptr || pending_action->continuation == vc->get_action()->continuation);
    pending_action = nullptr;

    session->new_connection(vc);

    session->state = HSS_ACTIVE;
    ats_ip_copy(&t_state.server_info.src_addr, netvc->get_local_addr());

    // If origin_max_connections or origin_min_keep_alive_connections is set then we are metering
    // the max and or min number of connections per host. Transfer responsibility for this to the
    // session object.
    if (t_state.outbound_conn_track_state.is_active()) {
      SMDebug("http_ss", "[%" PRId64 "] max number of outbound connections: %d", sm_id, t_state.txn_conf->outbound_conntrack.max);
      session->enable_outbound_connection_tracking(t_state.outbound_conn_track_state.drop());
    }

    attach_server_session(session);
    if (t_state.current.request_to == HttpTransact::PARENT_PROXY) {
      session->to_parent_proxy = true;
      HTTP_INCREMENT_DYN_STAT(http_current_parent_proxy_connections_stat);
      HTTP_INCREMENT_DYN_STAT(http_total_parent_proxy_connections_stat);

    } else {
      session->to_parent_proxy = false;
    }
    if (plugin_tunnel_type == HTTP_NO_PLUGIN_TUNNEL) {
      SMDebug("http", "[%" PRId64 "] setting handler for TCP handshake", sm_id);
      // Just want to get a write-ready event so we know that the TCP handshake is complete.
      server_entry->vc_handler = &HttpSM::state_http_server_open;
      server_entry->write_vio  = server_session->do_io_write(this, 1, server_session->get_reader());
    } else { // in the case of an intercept plugin don't to the connect timeout change
      SMDebug("http", "[%" PRId64 "] not setting handler for TCP handshake", sm_id);
      handle_http_server_open();
    }
    return 0;
  }
  case VC_EVENT_READ_COMPLETE:
  case VC_EVENT_WRITE_READY:
  case VC_EVENT_WRITE_COMPLETE:
    // Update the time out to the regular connection timeout.
    SMDebug("http_ss", "[%" PRId64 "] TCP Handshake complete", sm_id);
    server_entry->vc_handler = &HttpSM::state_send_server_request_header;

    // Reset the timeout to the non-connect timeout
    if (t_state.api_txn_no_activity_timeout_value != -1) {
      server_session->get_netvc()->set_inactivity_timeout(HRTIME_MSECONDS(t_state.api_txn_no_activity_timeout_value));
    } else {
      server_session->get_netvc()->set_inactivity_timeout(HRTIME_SECONDS(t_state.txn_conf->transaction_no_activity_timeout_out));
    }
    handle_http_server_open();
    return 0;
  case EVENT_INTERVAL: // Delayed call from another thread
    if (server_session == nullptr) {
      do_http_server_open();
    }
    break;
  case VC_EVENT_INACTIVITY_TIMEOUT:
  case VC_EVENT_ACTIVE_TIMEOUT:
  case VC_EVENT_ERROR:
  case NET_EVENT_OPEN_FAILED:
    t_state.current.state = HttpTransact::CONNECTION_ERROR;
    // save the errno from the connect fail for future use (passed as negative value, flip back)
    t_state.current.server->set_connect_fail(event == NET_EVENT_OPEN_FAILED ? -reinterpret_cast<intptr_t>(data) : ECONNABORTED);
    t_state.outbound_conn_track_state.clear();

    /* If we get this error in transparent mode, then we simply can't bind to the 4-tuple to make the connection.  There's no hope
       of retries succeeding in the near future. The best option is to just shut down the connection without further comment. The
       only known cause for this is outbound transparency combined with use client target address / source port, as noted in
       TS-1424. If the keep alives desync the current connection can be attempting to rebind the 4 tuple simultaneously with the
       shut down of an existing connection. Dropping the client side will cause it to pick a new source port and recover from this
       issue.
    */
    if (EADDRNOTAVAIL == t_state.current.server->connect_result && t_state.client_info.is_transparent) {
      if (is_debug_tag_set("http_tproxy")) {
        ip_port_text_buffer ip_c, ip_s;
        Debug("http_tproxy", "Force close of client connect (%s->%s) due to EADDRNOTAVAIL [%" PRId64 "]",
              ats_ip_nptop(&t_state.client_info.src_addr.sa, ip_c, sizeof ip_c),
              ats_ip_nptop(&t_state.server_info.dst_addr.sa, ip_s, sizeof ip_s), sm_id);
      }
      t_state.client_info.keep_alive = HTTP_NO_KEEPALIVE; // part of the problem, clear it.
      terminate_sm                   = true;
    } else if (ENET_THROTTLING == t_state.current.server->connect_result) {
      HTTP_INCREMENT_DYN_STAT(http_origin_connections_throttled_stat);
      send_origin_throttled_response();
    } else {
      // Go ahead and release the failed server session.  Since it didn't receive a response, the release logic will
      // see that it didn't get a valid response and it will close it rather than returning it to the server session pool
      release_server_session();
      call_transact_and_set_next_state(HttpTransact::HandleResponse);
    }
    return 0;

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
  ink_assert(server_entry->read_vio == (VIO *)data);
  ink_assert(t_state.current.server->state == HttpTransact::STATE_UNDEFINED);
  ink_assert(t_state.current.state == HttpTransact::STATE_UNDEFINED);

  int bytes_used = 0;
  VIO *vio       = (VIO *)data;

  switch (event) {
  case VC_EVENT_EOS:
    server_entry->eos = true;

  // Fall through
  case VC_EVENT_READ_READY:
  case VC_EVENT_READ_COMPLETE:
    // More data to parse
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
    milestones[TS_MILESTONE_SERVER_FIRST_READ] = Thread::get_hrtime();

    if (t_state.api_txn_no_activity_timeout_value != -1) {
      server_session->get_netvc()->set_inactivity_timeout(HRTIME_MSECONDS(t_state.api_txn_no_activity_timeout_value));
    } else {
      server_session->get_netvc()->set_inactivity_timeout(HRTIME_SECONDS(t_state.txn_conf->transaction_no_activity_timeout_out));
    }

    // For requests that contain a body, we can cancel the ua inactivity timeout.
    if (ua_txn && t_state.hdr_info.request_content_length) {
      ua_txn->cancel_inactivity_timeout();
    }
  }
  /////////////////////
  // tokenize header //
  /////////////////////
  ParseResult state =
    t_state.hdr_info.server_response.parse_resp(&http_parser, server_buffer_reader, &bytes_used, server_entry->eos);

  server_response_hdr_bytes += bytes_used;

  // Don't allow HTTP 0.9 (unparsable headers) on resued connections.
  // And don't allow empty headers from closed connections
  if ((state == PARSE_RESULT_DONE && t_state.hdr_info.server_response.version_get() == HTTPVersion(0, 9) &&
       server_session->transact_count > 1) ||
      (server_entry->eos && vio->ndone == 0)) {
    state = PARSE_RESULT_ERROR;
  }
  // Check to see if we are over the hdr size limit
  if (server_response_hdr_bytes > t_state.txn_conf->response_hdr_max_size) {
    state = PARSE_RESULT_ERROR;
  }

  if (state != PARSE_RESULT_CONT) {
    // Disable further IO
    server_entry->read_vio->nbytes = server_entry->read_vio->ndone;
    http_parser_clear(&http_parser);
    milestones[TS_MILESTONE_SERVER_READ_HEADER_DONE] = Thread::get_hrtime();
  }

  switch (state) {
  case PARSE_RESULT_ERROR: {
    // Many broken servers send really badly formed 302 redirects.
    //  Even if the parser doesn't like the redirect forward
    //  if it's got a Location header.  We check the type of the
    //  response to make sure that the parser was able to parse
    //  something  and didn't just throw up it's hands (INKqa05339)
    bool allow_error = false;
    if (t_state.hdr_info.server_response.type_get() == HTTP_TYPE_RESPONSE &&
        t_state.hdr_info.server_response.status_get() == HTTP_STATUS_MOVED_TEMPORARILY) {
      if (t_state.hdr_info.server_response.field_find(MIME_FIELD_LOCATION, MIME_LEN_LOCATION)) {
        allow_error = true;
      }
    }

    if (allow_error == false) {
      SMDebug("http_seq", "Error parsing server response header");
      t_state.current.state = HttpTransact::PARSE_ERROR;

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

  case PARSE_RESULT_DONE:
    SMDebug("http_seq", "Done parsing server response header");

    // Now that we know that we have all of the origin server
    // response headers, we can reset the client inactivity
    // timeout.  This is unlikely to cause a recurrence of
    // old bug because there will be no more retries now that
    // the connection has been established.  It is possible
    // however.  We do not need to reset the inactivity timeout
    // if the request contains a body (noted by the
    // request_content_length field) because it was never
    // canceled.
    //

    // we now reset the client inactivity timeout only
    // when we are ready to send the response headers. In the
    // case of transform plugin, this is after the transform
    // outputs the 1st byte, which can take a long time if the
    // plugin buffers the whole response.
    // Also, if the request contains a body, we cancel the timeout
    // when we read the 1st byte of the origin server response.
    /*
       if (ua_txn && !t_state.hdr_info.request_content_length) {
       ua_txn->get_netvc()->set_inactivity_timeout(HRTIME_SECONDS(
       HttpConfig::m_master.accept_no_activity_timeout));
       }
     */

    t_state.current.state         = HttpTransact::CONNECTION_ALIVE;
    t_state.transact_return_point = HttpTransact::HandleResponse;
    t_state.api_next_action       = HttpTransact::SM_ACTION_API_READ_RESPONSE_HDR;

    // if exceeded limit deallocate postdata buffers and disable redirection
    if (!(enable_redirection && (redirection_tries <= t_state.txn_conf->number_of_redirections))) {
      this->disable_redirect();
    }

    do_api_callout();
    break;
  case PARSE_RESULT_CONT:
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
  STATE_ENTER(&HttpSM::state_send_server_request_header, event);
  ink_assert(server_entry != nullptr);
  ink_assert(server_entry->write_vio == (VIO *)data || server_entry->read_vio == (VIO *)data);

  int method;

  switch (event) {
  case VC_EVENT_WRITE_READY:
    server_entry->write_vio->reenable();
    break;

  case VC_EVENT_WRITE_COMPLETE:
    // We are done sending the request header, deallocate
    //  our buffer and then decide what to do next
    free_MIOBuffer(server_entry->write_buffer);
    server_entry->write_buffer = nullptr;
    method                     = t_state.hdr_info.server_request.method_get_wksidx();
    if (!t_state.api_server_request_body_set && method != HTTP_WKSIDX_TRACE &&
        (t_state.hdr_info.request_content_length > 0 || t_state.client_info.transfer_encoding == HttpTransact::CHUNKED_ENCODING)) {
      if (post_transform_info.vc) {
        setup_transform_to_server_transfer();
      } else {
        do_setup_post_tunnel(HTTP_SERVER_VC);
      }
    } else {
      // It's time to start reading the response
      setup_server_read_response_header();
    }

    break;

  case VC_EVENT_READ_READY:
    // We already did the read for the response header and
    //  we got some data.  Wait for the request header
    //  send before dealing with it.  However, we need to
    //  disable further IO here since the whole response
    //  may be in the buffer and we can not switch buffers
    //  on the io core later
    ink_assert(server_entry->read_vio == (VIO *)data);
    // setting nbytes to ndone would disable reads and remove it from the read queue.
    // We can't do this in the epoll paradigm because we may be missing epoll errors that would
    // prevent us from leaving this state.
    // setup_server_read_response_header will trigger READ_READY to itself if there is data in the buffer.

    // server_entry->read_vio->nbytes = server_entry->read_vio->ndone;

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
    /*if (server_buffer_reader->read_avail() > 0 && callout_state == HTTP_API_NO_CALLOUT) {
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
    SMDebug("http_ss", "read complete due to 0 byte do_io_read");
    break;

  default:
    ink_release_assert(0);
    break;
  }

  return 0;
}

void
HttpSM::process_srv_info(HostDBInfo *r)
{
  SMDebug("dns_srv", "beginning process_srv_info");
  t_state.hostdb_entry = Ptr<HostDBInfo>(r);

  /* we didn't get any SRV records, continue w normal lookup */
  if (!r || !r->is_srv || !r->round_robin) {
    t_state.dns_info.srv_hostname[0]    = '\0';
    t_state.dns_info.srv_lookup_success = false;
    t_state.txn_conf->srv_enabled       = false;
    SMDebug("dns_srv", "No SRV records were available, continuing to lookup %s", t_state.dns_info.lookup_name);
  } else {
    HostDBRoundRobin *rr = r->rr();
    HostDBInfo *srv      = nullptr;
    if (rr) {
      srv = rr->select_best_srv(t_state.dns_info.srv_hostname, &mutex->thread_holding->generator, ink_local_time(),
                                (int)t_state.txn_conf->down_server_timeout);
    }
    if (!srv) {
      t_state.dns_info.srv_lookup_success = false;
      t_state.dns_info.srv_hostname[0]    = '\0';
      t_state.txn_conf->srv_enabled       = false;
      SMDebug("dns_srv", "SRV records empty for %s", t_state.dns_info.lookup_name);
    } else {
      t_state.dns_info.srv_lookup_success = true;
      t_state.dns_info.srv_port           = srv->data.srv.srv_port;
      t_state.dns_info.srv_app            = srv->app;
      // t_state.dns_info.single_srv = (rr->good == 1);
      ink_assert(srv->data.srv.key == makeHostHash(t_state.dns_info.srv_hostname));
      SMDebug("dns_srv", "select SRV records %s", t_state.dns_info.srv_hostname);
    }
  }
  return;
}

void
HttpSM::process_hostdb_info(HostDBInfo *r)
{
  // Increment the refcount to our item, since we are pointing at it
  t_state.hostdb_entry = Ptr<HostDBInfo>(r);

  sockaddr const *client_addr = nullptr;
  bool use_client_addr        = t_state.http_config_param->use_client_target_addr == 1 && t_state.client_info.is_transparent &&
                         t_state.dns_info.os_addr_style == HttpTransact::DNSLookupInfo::OS_Addr::OS_ADDR_TRY_DEFAULT;
  if (use_client_addr) {
    NetVConnection *vc = t_state.state_machine->ua_txn ? t_state.state_machine->ua_txn->get_netvc() : nullptr;
    if (vc) {
      client_addr = vc->get_local_addr();
      // Regardless of whether the client address matches the DNS record or not,
      // we want to use that address.  Therefore, we copy over the client address
      // info and skip the assignment from the DNS cache
      ats_ip_copy(t_state.host_db_info.ip(), client_addr);
      t_state.dns_info.os_addr_style  = HttpTransact::DNSLookupInfo::OS_Addr::OS_ADDR_TRY_CLIENT;
      t_state.dns_info.lookup_success = true;
      // Leave ret unassigned, so we don't overwrite the host_db_info
    } else {
      use_client_addr = false;
    }
  }

  if (r && !r->is_failed()) {
    ink_time_t now                    = ink_local_time();
    HostDBInfo *ret                   = nullptr;
    t_state.dns_info.lookup_success   = true;
    t_state.dns_info.lookup_validated = true;

    HostDBRoundRobin *rr = r->round_robin ? r->rr() : nullptr;
    if (rr) {
      // if use_client_target_addr is set, make sure the client addr is in the results pool
      if (use_client_addr && rr->find_ip(client_addr) == nullptr) {
        SMDebug("http", "use_client_target_addr == 1. Client specified address is not in the pool, not validated.");
        t_state.dns_info.lookup_validated = false;
      } else {
        // Since the time elapsed between current time and client_request_time
        // may be very large, we cannot use client_request_time to approximate
        // current time when calling select_best_http().
        ret = rr->select_best_http(&t_state.client_info.src_addr.sa, now, static_cast<int>(t_state.txn_conf->down_server_timeout));
        // set the srv target`s last_failure
        if (t_state.dns_info.srv_lookup_success) {
          uint32_t last_failure = 0xFFFFFFFF;
          for (int i = 0; i < rr->rrcount && last_failure != 0; ++i) {
            if (last_failure > rr->info(i).app.http_data.last_failure) {
              last_failure = rr->info(i).app.http_data.last_failure;
            }
          }

          if (last_failure != 0 && (uint32_t)(now - t_state.txn_conf->down_server_timeout) < last_failure) {
            HostDBApplicationInfo app;
            app.allotment.application1 = 0;
            app.allotment.application2 = 0;
            app.http_data.last_failure = last_failure;
            hostDBProcessor.setby_srv(t_state.dns_info.lookup_name, 0, t_state.dns_info.srv_hostname, &app);
          }
        }
      }
    } else {
      if (use_client_addr && !ats_ip_addr_eq(client_addr, &r->data.ip.sa)) {
        SMDebug("http", "use_client_target_addr == 1. Comparing single addresses failed, not validated.");
        t_state.dns_info.lookup_validated = false;
      } else {
        ret = r;
      }
    }
    if (ret) {
      t_state.host_db_info = *ret;
      ink_release_assert(!t_state.host_db_info.reverse_dns);
      ink_release_assert(ats_is_ip(t_state.host_db_info.ip()));
    }
  } else {
    SMDebug("http", "[%" PRId64 "] DNS lookup failed for '%s'", sm_id, t_state.dns_info.lookup_name);

    if (!use_client_addr) {
      t_state.dns_info.lookup_success = false;
    }
    t_state.host_db_info.app.allotment.application1 = 0;
    t_state.host_db_info.app.allotment.application2 = 0;
    ink_assert(!t_state.host_db_info.round_robin);
  }

  milestones[TS_MILESTONE_DNS_LOOKUP_END] = Thread::get_hrtime();

  if (is_debug_tag_set("http_timeout")) {
    if (t_state.api_txn_dns_timeout_value != -1) {
      int foo = (int)(milestones.difference_msec(TS_MILESTONE_DNS_LOOKUP_BEGIN, TS_MILESTONE_DNS_LOOKUP_END));
      SMDebug("http_timeout", "DNS took: %d msec", foo);
    }
  }
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

  //    ink_assert (m_origin_server_vc == 0);
  // REQ_FLAVOR_SCHEDULED_UPDATE can be transformed into
  // REQ_FLAVOR_REVPROXY
  ink_assert(t_state.req_flavor == HttpTransact::REQ_FLAVOR_SCHEDULED_UPDATE ||
             t_state.req_flavor == HttpTransact::REQ_FLAVOR_REVPROXY || ua_entry->vc != nullptr);

  switch (event) {
  case EVENT_HOST_DB_LOOKUP:
    pending_action = nullptr;
    process_hostdb_info((HostDBInfo *)data);
    call_transact_and_set_next_state(nullptr);
    break;
  case EVENT_SRV_LOOKUP: {
    pending_action = nullptr;
    process_srv_info((HostDBInfo *)data);

    char *host_name = t_state.dns_info.srv_lookup_success ? t_state.dns_info.srv_hostname : t_state.dns_info.lookup_name;
    HostDBProcessor::Options opt;
    opt.port  = t_state.dns_info.srv_lookup_success ? t_state.dns_info.srv_port : t_state.server_info.dst_addr.host_order_port();
    opt.flags = (t_state.cache_info.directives.does_client_permit_dns_storing) ? HostDBProcessor::HOSTDB_DO_NOT_FORCE_DNS :
                                                                                 HostDBProcessor::HOSTDB_FORCE_DNS_RELOAD;
    opt.timeout        = (t_state.api_txn_dns_timeout_value != -1) ? t_state.api_txn_dns_timeout_value : 0;
    opt.host_res_style = ua_txn->get_host_res_style();

    Action *dns_lookup_action_handle =
      hostDBProcessor.getbyname_imm(this, (cb_process_result_pfn)&HttpSM::process_hostdb_info, host_name, 0, opt);
    if (dns_lookup_action_handle != ACTION_RESULT_DONE) {
      ink_assert(!pending_action);
      pending_action = dns_lookup_action_handle;
    } else {
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

  // REQ_FLAVOR_SCHEDULED_UPDATE can be transformed into
  // REQ_FLAVOR_REVPROXY
  ink_assert(t_state.req_flavor == HttpTransact::REQ_FLAVOR_SCHEDULED_UPDATE ||
             t_state.req_flavor == HttpTransact::REQ_FLAVOR_REVPROXY || ua_entry->vc != nullptr);

  switch (event) {
  case EVENT_HOST_DB_LOOKUP:
    pending_action = nullptr;
    if (data) {
      t_state.request_data.hostname_str = ((HostDBInfo *)data)->hostname();
    } else {
      SMDebug("http", "[%" PRId64 "] reverse DNS lookup failed for '%s'", sm_id, t_state.dns_info.lookup_name);
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
  HostDBInfo *mark_down = nullptr;

  if (event == EVENT_HOST_DB_LOOKUP && data) {
    HostDBInfo *r = (HostDBInfo *)data;

    if (r->round_robin) {
      // Look for the entry we need mark down in the round robin
      ink_assert(t_state.current.server != nullptr);
      ink_assert(t_state.current.request_to == HttpTransact::ORIGIN_SERVER);
      if (t_state.current.server) {
        mark_down = r->rr()->find_ip(&t_state.current.server->dst_addr.sa);
      }
    } else {
      // No longer a round robin, check to see if our address is the same
      if (ats_ip_addr_eq(t_state.host_db_info.ip(), r->ip())) {
        mark_down = r;
      }
    }

    if (mark_down) {
      mark_host_failure(mark_down, t_state.request_sent_time);
    }
  }
  // We either found our entry or we did not.  Either way find
  //  the entry we should use now
  return state_hostdb_lookup(event, data);
}

//////////////////////////////////////////////////////////////////////////
//
//  HttpSM::state_handle_stat_page()
//
//////////////////////////////////////////////////////////////////////////
int
HttpSM::state_handle_stat_page(int event, void *data)
{
  STATE_ENTER(&HttpSM::state_handle_stat_page, event);
  switch (event) {
  case STAT_PAGE_SUCCESS:
    pending_action = nullptr;

    if (data) {
      StatPageData *spd = (StatPageData *)data;

      t_state.internal_msg_buffer = spd->data;
      if (spd->type) {
        t_state.internal_msg_buffer_type = spd->type;
      } else {
        t_state.internal_msg_buffer_type = nullptr; // Defaults to text/html
      }
      t_state.internal_msg_buffer_size                = spd->length;
      t_state.internal_msg_buffer_fast_allocator_size = -1;
    }

    call_transact_and_set_next_state(HttpTransact::HandleStatPage);
    break;

  case STAT_PAGE_FAILURE:
    pending_action = nullptr;
    call_transact_and_set_next_state(HttpTransact::HandleStatPage);
    break;

  default:
    ink_release_assert(0);
    break;
  }

  return 0;
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
  if (ua_txn) {
    if (pending_action) {
      pending_action->cancel();
    }
    if ((pending_action = ua_txn->adjust_thread(this, event, data))) {
      return 0; // Go away if we reschedule
    }
  }

  milestones[TS_MILESTONE_CACHE_OPEN_WRITE_END] = Thread::get_hrtime();
  pending_action                                = nullptr;

  switch (event) {
  case CACHE_EVENT_OPEN_WRITE:
    //////////////////////////////
    // OPEN WRITE is successful //
    //////////////////////////////
    t_state.cache_info.write_lock_state = HttpTransact::CACHE_WL_SUCCESS;
    break;

  case CACHE_EVENT_OPEN_WRITE_FAILED:
    // Failed on the write lock and retrying the vector
    //  for reading
    if (t_state.redirect_info.redirect_in_process) {
      SMDebug("http_redirect", "[%" PRId64 "] CACHE_EVENT_OPEN_WRITE_FAILED during redirect follow", sm_id);
      t_state.cache_open_write_fail_action = HttpTransact::CACHE_WL_FAIL_ACTION_DEFAULT;
      t_state.cache_info.write_lock_state  = HttpTransact::CACHE_WL_FAIL;
      break;
    }
    if (t_state.txn_conf->cache_open_write_fail_action == HttpTransact::CACHE_WL_FAIL_ACTION_DEFAULT) {
      t_state.cache_info.write_lock_state = HttpTransact::CACHE_WL_FAIL;
      break;
    } else {
      t_state.cache_open_write_fail_action = t_state.txn_conf->cache_open_write_fail_action;
      if (!t_state.cache_info.object_read ||
          (t_state.cache_open_write_fail_action == HttpTransact::CACHE_WL_FAIL_ACTION_ERROR_ON_MISS_OR_REVALIDATE)) {
        // cache miss, set wl_state to fail
        SMDebug("http", "[%" PRId64 "] cache object read %p, cache_wl_fail_action %d", sm_id, t_state.cache_info.object_read,
                t_state.cache_open_write_fail_action);
        t_state.cache_info.write_lock_state = HttpTransact::CACHE_WL_FAIL;
        break;
      }
    }
  // INTENTIONAL FALL THROUGH
  // Allow for stale object to be served
  case CACHE_EVENT_OPEN_READ:
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
    t_state.source = HttpTransact::SOURCE_CACHE;
    // clear up CACHE_LOOKUP_MISS, let Freshness function decide
    // hit status
    t_state.cache_lookup_result         = HttpTransact::CACHE_LOOKUP_NONE;
    t_state.cache_info.write_lock_state = HttpTransact::CACHE_WL_READ_RETRY;
    break;

  case HTTP_TUNNEL_EVENT_DONE:
    // In the case where we have issued a cache write for the
    //  transformed copy, the tunnel from the origin server to
    //  the transform may complete while we are waiting for
    //  the cache write.  If this is the case, forward the event
    //  to the transform read state as it will know how to
    //  handle it
    if (t_state.next_action == HttpTransact::SM_ACTION_CACHE_ISSUE_WRITE_TRANSFORM) {
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
  t_state.api_next_action = HttpTransact::SM_ACTION_API_CACHE_LOOKUP_COMPLETE;
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
  milestones[TS_MILESTONE_CACHE_OPEN_READ_END] = Thread::get_hrtime();

  ink_assert(server_entry == nullptr);
  ink_assert(t_state.cache_info.object_read == nullptr);

  switch (event) {
  case CACHE_EVENT_OPEN_READ: {
    pending_action = nullptr;

    SMDebug("http", "[%" PRId64 "] cache_open_read - CACHE_EVENT_OPEN_READ", sm_id);

    /////////////////////////////////
    // lookup/open is successful. //
    /////////////////////////////////
    ink_assert(cache_sm.cache_read_vc != nullptr);
    t_state.source = HttpTransact::SOURCE_CACHE;

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

    SMDebug("http", "[%" PRId64 "] cache_open_read - CACHE_EVENT_OPEN_READ_FAILED with %s (%d)", sm_id,
            InkStrerror(-(intptr_t)data), (int)(intptr_t)data);

    SMDebug("http", "[state_cache_open_read] open read failed.");
    // Inform HttpTransact somebody else is updating the document
    // HttpCacheSM already waited so transact should go ahead.
    if ((intptr_t)data == -ECACHE_DOC_BUSY) {
      t_state.cache_lookup_result = HttpTransact::CACHE_LOOKUP_DOC_BUSY;
    } else {
      t_state.cache_lookup_result = HttpTransact::CACHE_LOOKUP_MISS;
    }

    ink_assert(t_state.transact_return_point == nullptr);
    t_state.transact_return_point = HttpTransact::HandleCacheOpenRead;
    setup_cache_lookup_complete_api();
    break;

  default:
    ink_release_assert(!"Unknown event");
    break;
  }

  return 0;
}

int
HttpSM::main_handler(int event, void *data)
{
  ink_release_assert(magic == HTTP_SM_MAGIC_ALIVE);

  HttpSMHandler jump_point = nullptr;
  ink_assert(reentrancy_count >= 0);
  reentrancy_count++;

  // Don't use the state enter macro since it uses history
  //  space that we don't care about
  SMDebug("http", "[%" PRId64 "] [HttpSM::main_handler, %s]", sm_id, HttpDebugNames::get_event_name(event));

  HttpVCTableEntry *vc_entry = nullptr;

  if (data != nullptr) {
    // Only search the VC table if the event could have to
    //  do with a VIO to save a few cycles

    if (event < VC_EVENT_EVENTS_START + 100) {
      vc_entry = vc_table.find_entry((VIO *)data);
    }
  }

  if (vc_entry) {
    jump_point = vc_entry->vc_handler;
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
  ink_assert(p->vc_type == HT_HTTP_CLIENT || (p->handler_state == HTTP_SM_POST_UA_FAIL && p->vc_type == HT_BUFFER_READ));
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

  switch (p->handler_state) {
  case HTTP_SM_POST_SERVER_FAIL:
    c = tunnel.get_consumer(server_entry->vc);
    ink_assert(c->write_success == false);
    break;
  case HTTP_SM_POST_UA_FAIL:
    // UA quit - shutdown the SM
    ink_assert(p->read_success == false);
    terminate_sm = true;
    break;
  case HTTP_SM_POST_SUCCESS:
    // The post succeeded
    ink_assert(p->read_success == true);
    ink_assert(p->consumer_list.head->write_success == true);
    tunnel.deallocate_buffers();
    tunnel.reset();
    // When the ua completed sending it's data we must have
    //  removed it from the tunnel
    ink_release_assert(ua_entry->in_tunnel == false);
    server_entry->in_tunnel = false;

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

  HttpTunnelProducer *p = ua_txn != nullptr ? tunnel.get_producer(ua_txn) : tunnel.get_producer(HT_HTTP_CLIENT);
  if (!p) {
    return 0; // Cannot do anything if there is no producer
  }

  switch (event) {
  case HTTP_TUNNEL_EVENT_DONE: // Tunnel done.
    if (p->handler_state == HTTP_SM_POST_UA_FAIL) {
      // post failed
      switch (t_state.client_info.state) {
      case HttpTransact::ACTIVE_TIMEOUT:
        call_transact_and_set_next_state(HttpTransact::PostActiveTimeoutResponse);
        return 0;
      case HttpTransact::INACTIVE_TIMEOUT:
        call_transact_and_set_next_state(HttpTransact::PostInactiveTimeoutResponse);
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
  case VC_EVENT_INACTIVITY_TIMEOUT: // ua_txn timeout during sending the HTTP 408 response
  case VC_EVENT_ACTIVE_TIMEOUT:     // ua_txn timeout
    if (ua_entry->write_buffer) {
      free_MIOBuffer(ua_entry->write_buffer);
      ua_entry->write_buffer = nullptr;
      ua_entry->vc->do_io_write(nullptr, 0, nullptr);
    }
    if (!p->handler_state) {
      p->handler_state = HTTP_SM_POST_UA_FAIL;
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
    p_handler_state = HTTP_SM_POST_SERVER_FAIL;
  }
  if (p->vc_type != HT_BUFFER_READ) {
    tunnel_handler_post_or_put(p);
  }

  switch (p_handler_state) {
  case HTTP_SM_POST_SERVER_FAIL:
    handle_post_failure();
    break;
  case HTTP_SM_POST_UA_FAIL:
    break;
  case HTTP_SM_POST_SUCCESS:
    // It's time to start reading the response
    if (is_waiting_for_full_body) {
      is_waiting_for_full_body  = false;
      is_using_post_buffer      = true;
      client_request_body_bytes = this->postbuf_buffer_avail();

      call_transact_and_set_next_state(HttpTransact::HandleRequestBufferDone);
      break;
    }
    setup_server_read_response_header();
    break;
  default:
    ink_release_assert(0);
  }

  return 0;
}

int
HttpSM::tunnel_handler_cache_fill(int event, void *data)
{
  STATE_ENTER(&HttpSM::tunnel_handler_cache_fill, event);

  ink_assert(event == HTTP_TUNNEL_EVENT_DONE);
  ink_assert(data == &tunnel);

  ink_release_assert(cache_sm.cache_write_vc);

  tunnel.deallocate_buffers();
  this->postbuf_clear();
  tunnel.reset();

  setup_server_transfer_to_cache_only();
  tunnel.tunnel_run();

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
  HttpTunnelConsumer *c = tunnel.get_consumer(ua_txn);

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
      SMDebug("http",
              "[%" PRId64 "] tunnel_handler_100_continue - server already "
              "closed, terminating connection",
              sm_id);

      // Since 100 isn't a final (loggable) response header
      //   kill the 100 continue header and create an empty one
      t_state.hdr_info.server_response.destroy();
      t_state.hdr_info.server_response.create(HTTP_TYPE_RESPONSE);
      handle_server_setup_error(VC_EVENT_EOS, server_entry->read_vio);
    } else {
      setup_server_read_response_header();
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
  HttpTunnelProducer *ua = (ua_txn) ? tunnel.get_producer(ua_txn) : tunnel.get_producer(HT_HTTP_CLIENT);

  if (ua && !ua->read_success) {
    // Client failed to send the body, it's gone.  Kill the
    // state machine
    terminate_sm = true;
    return 0;
  }

  HttpTunnelConsumer *cache = ua->consumer_list.head;
  ink_release_assert(cache->vc_type == HT_CACHE_WRITE);
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
HttpSM::tunnel_handler(int event, void *data)
{
  STATE_ENTER(&HttpSM::tunnel_handler, event);

  ink_assert(event == HTTP_TUNNEL_EVENT_DONE);
  // The tunnel calls this when it is done
  terminate_sm = true;

  if (unlikely(t_state.is_websocket)) {
    HTTP_DECREMENT_DYN_STAT(http_websocket_current_active_client_connections_stat);
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
    SMDebug("http", "[%" PRId64 "] server EOS after %" PRId64 " bytes, expected %" PRId64, sm_id, server_response_body_bytes, cl);
    return true;
  } else {
    return false;
  }
}

int
HttpSM::tunnel_handler_server(int event, HttpTunnelProducer *p)
{
  STATE_ENTER(&HttpSM::tunnel_handler_server, event);

  milestones[TS_MILESTONE_SERVER_CLOSE] = Thread::get_hrtime();

  bool close_connection = false;

  if (t_state.current.server->keep_alive == HTTP_KEEPALIVE && server_entry->eos == false &&
      plugin_tunnel_type == HTTP_NO_PLUGIN_TUNNEL && t_state.txn_conf->keep_alive_enabled_out == 1) {
    close_connection = false;
  } else {
    close_connection = true;
  }

  switch (event) {
  case VC_EVENT_INACTIVITY_TIMEOUT:
  case VC_EVENT_ACTIVE_TIMEOUT:
  case VC_EVENT_ERROR:
    t_state.squid_codes.log_code  = SQUID_LOG_ERR_READ_TIMEOUT;
    t_state.squid_codes.hier_code = SQUID_HIER_TIMEOUT_DIRECT;
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

    close_connection = true;

    ink_assert(p->vc_type == HT_HTTP_SERVER);

    if (is_http_server_eos_truncation(p)) {
      SMDebug("http", "[%" PRId64 "] [HttpSM::tunnel_handler_server] aborting HTTP tunnel due to server truncation", sm_id);
      tunnel.chain_abort_all(p);
      // UA session may not be in the tunnel yet, don't NULL out the pointer in that case.
      // Note: This is a hack. The correct solution is for the UA session to signal back to the SM
      // when the UA is about to be destroyed and clean up the pointer there. That should be done once
      // the TS-3612 changes are in place (and similarly for the server session).
      /*if (ua_entry->in_tunnel)
        ua_txn = NULL; */

      t_state.current.server->abort      = HttpTransact::ABORTED;
      t_state.client_info.keep_alive     = HTTP_NO_KEEPALIVE;
      t_state.current.server->keep_alive = HTTP_NO_KEEPALIVE;
      t_state.squid_codes.log_code       = SQUID_LOG_ERR_READ_ERROR;
    } else {
      SMDebug("http", "[%" PRId64 "] [HttpSM::tunnel_handler_server] finishing HTTP tunnel", sm_id);
      p->read_success               = true;
      t_state.current.server->abort = HttpTransact::DIDNOT_ABORT;
      // Appending reason to a response without Content-Length will result in
      // the reason string being written to the client and a bad CL when reading from cache.
      // I didn't find anywhere this appended reason is being used, so commenting it out.
      /*
        if (t_state.negative_caching && p->bytes_read == 0) {
        int reason_len;
        const char *reason = t_state.hdr_info.server_response.reason_get(&reason_len);
        if (reason == NULL)
        tunnel.append_message_to_producer_buffer(p, "Negative Response", sizeof("Negative Response") - 1);
        else
        tunnel.append_message_to_producer_buffer(p, reason, reason_len);
        }
      */
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
        t_state.client_info.keep_alive     = HTTP_NO_KEEPALIVE;
        t_state.current.server->keep_alive = HTTP_NO_KEEPALIVE;
      } else {
        tunnel.local_finish_all(p);
      }
    }
    break;

  case HTTP_TUNNEL_EVENT_CONSUMER_DETACH:
    // All consumers are prematurely gone.  Shutdown
    //    the server connection
    p->read_success               = true;
    t_state.current.server->state = HttpTransact::TRANSACTION_COMPLETE;
    t_state.current.server->abort = HttpTransact::DIDNOT_ABORT;
    close_connection              = true;
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
  if (t_state.negative_caching) {
    t_state.negative_caching = false;
  }

  // If we had a ground fill, check update our status
  if (background_fill == BACKGROUND_FILL_STARTED) {
    background_fill = p->read_success ? BACKGROUND_FILL_COMPLETED : BACKGROUND_FILL_ABORTED;
    HTTP_DECREMENT_DYN_STAT(http_background_fill_current_count_stat);
  }
  // We handled the event.  Now either shutdown the connection or
  //   setup it up for keep-alive
  ink_assert(server_entry->vc == p->vc);
  ink_assert(p->vc_type == HT_HTTP_SERVER);
  ink_assert(p->vc == server_session);

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
    if (ua_txn && ua_txn->is_outbound_transparent() && t_state.http_config_param->use_client_source_port) {
      t_state.client_info.keep_alive = HTTP_NO_KEEPALIVE;
    }
  } else {
    server_session->attach_hostname(t_state.current.server->name);
    server_session->server_trans_stat--;
    HTTP_DECREMENT_DYN_STAT(http_current_server_transactions_stat);

    // If the option to attach the server session to the client session is set
    // and if the client is still around and the client is keep-alive, attach the
    // server session to so the next ka request can use it.  Server sessions will
    // be placed into the shared pool if the next incoming request is for a different
    // origin server
    if (t_state.txn_conf->attach_server_session_to_client == 1 && ua_txn && t_state.client_info.keep_alive == HTTP_KEEPALIVE) {
      Debug("http", "attaching server session to the client");
      ua_txn->attach_server_session(server_session);
    } else {
      // Release the session back into the shared session pool
      server_session->get_netvc()->set_inactivity_timeout(HRTIME_SECONDS(t_state.txn_conf->keep_alive_no_activity_timeout_out));
      server_session->release();
    }
  }

  server_session = nullptr; // Because p->vc == server_session
  server_entry   = nullptr;

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

  ink_assert(c->vc == ua_txn);

  switch (event) {
  case VC_EVENT_EOS:
    ua_entry->eos = true;
  // FALL-THROUGH
  case VC_EVENT_INACTIVITY_TIMEOUT:
  case VC_EVENT_ACTIVE_TIMEOUT:
  case VC_EVENT_ERROR:
    set_ua_abort(HttpTransact::ABORTED, event);
    c->vc->do_io_close();
    break;
  case VC_EVENT_WRITE_COMPLETE:
    // mark the vc as no longer in tunnel
    //   so we don't get hosed if the ua abort before
    //   real response header is received
    ua_entry->in_tunnel = false;
    c->write_success    = true;
  }

  return 0;
}

bool
HttpSM::is_bg_fill_necessary(HttpTunnelConsumer *c)
{
  ink_assert(c->vc_type == HT_HTTP_CLIENT);

  if (c->producer->alive &&          // something there to read
                                     //      server_entry && server_entry->vc &&              // from an origin server
                                     //      server_session && server_session->get_netvc() && // which is still open and valid
      c->producer->num_consumers > 1 // with someone else reading it
  ) {
    HttpTunnelProducer *p = nullptr;

    if (!server_entry || !server_entry->vc || !server_session || !server_session->get_netvc()) {
      // return true if we have finished the reading from OS when client aborted
      p = c->producer->self_consumer ? c->producer->self_consumer->producer : c->producer;
      if (p->vc_type == HT_HTTP_SERVER && p->read_success) {
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
      float pDone          = (float)ua_body_done / ua_cl;

      // If we got a good content length.  Check to make sure that we haven't already
      //  done more the content length since that would indicate the content-length
      //  is bogus.  If we've done more than the threshold, continue the background fill
      if (pDone <= 1.0 && pDone > t_state.txn_conf->background_fill_threshold) {
        return true;
      } else {
        SMDebug("http", "[%" PRId64 "] no background.  Only %%%f of %%%f done [%" PRId64 " / %" PRId64 " ]", sm_id, pDone,
                t_state.txn_conf->background_fill_threshold, ua_body_done, ua_cl);
      }
    }
  }

  return false;
}

int
HttpSM::tunnel_handler_ua(int event, HttpTunnelConsumer *c)
{
  bool close_connection     = true;
  HttpTunnelProducer *p     = nullptr;
  HttpTunnelConsumer *selfc = nullptr;

  STATE_ENTER(&HttpSM::tunnel_handler_ua, event);
  ink_assert(c->vc == ua_txn);
  milestones[TS_MILESTONE_UA_CLOSE] = Thread::get_hrtime();

  switch (event) {
  case VC_EVENT_EOS:
    ua_entry->eos = true;

  // FALL-THROUGH
  case VC_EVENT_INACTIVITY_TIMEOUT:
  case VC_EVENT_ACTIVE_TIMEOUT:
  case VC_EVENT_ERROR:

    // The user agent died or aborted.  Check to
    //  see if we should setup a background fill
    set_ua_abort(HttpTransact::ABORTED, event);

    if (is_bg_fill_necessary(c)) {
      p = c->producer->self_consumer ? c->producer->self_consumer->producer : c->producer;
      SMDebug("http", "[%" PRId64 "] Initiating background fill", sm_id);
      // check whether to finish the reading.
      background_fill = p->read_success ? BACKGROUND_FILL_COMPLETED : BACKGROUND_FILL_STARTED;

      // There is another consumer (cache write) so
      //  detach the user agent
      if (background_fill == BACKGROUND_FILL_STARTED) {
        HTTP_INCREMENT_DYN_STAT(http_background_fill_current_count_stat);
        ink_assert(server_entry->vc == server_session);
        ink_assert(c->is_downstream_from(server_session));
        server_session->get_netvc()->set_active_timeout(HRTIME_SECONDS(t_state.txn_conf->background_fill_active_timeout));
      }

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
        if (p->alive && ((p->vc_type == HT_CACHE_READ) || (p->vc_type == HT_HTTP_SERVER))) {
          tunnel.chain_abort_all(p);
        }
      }
    }
    break;

  case VC_EVENT_WRITE_COMPLETE:
    c->write_success          = true;
    t_state.client_info.abort = HttpTransact::DIDNOT_ABORT;
    if (t_state.client_info.keep_alive == HTTP_KEEPALIVE) {
      if (t_state.www_auth_content != HttpTransact::CACHE_AUTH_SERVE || ua_txn->get_server_session()) {
        // successful keep-alive
        close_connection = false;
      }
      // else { the authenticated server connection (cache
      // authenticated feature) closed during the serve-from-cache.
      // We want the client to issue a new connection for the
      // session based authenticated mechanism like NTLM, instead
      // of still using the existing client connection. }
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
  if (HttpTransact::SOURCE_TRANSFORM == original_source && t_state.range_setup != HttpTransact::RANGE_NONE) {
    original_source = t_state.pre_transform_source;
  }

  switch (original_source) {
  case HttpTransact::SOURCE_HTTP_ORIGIN_SERVER:
    server_response_body_bytes = client_response_body_bytes;
    break;
  case HttpTransact::SOURCE_CACHE:
    cache_response_body_bytes = client_response_body_bytes;
    break;
  default:
    break;
  }

  ink_assert(ua_entry->vc == c->vc);
  if (close_connection) {
    // If the client could be pipelining or is doing a POST, we need to
    //   set the ua_txn into half close mode

    // only external POSTs should be subject to this logic; ruling out internal POSTs here
    bool is_eligible_post_request = ((t_state.method == HTTP_WKSIDX_POST) && !is_internal);

    if ((is_eligible_post_request || t_state.client_info.pipeline_possible == true) && c->producer->vc_type != HT_STATIC &&
        event == VC_EVENT_WRITE_COMPLETE) {
      ua_txn->set_half_close_flag(true);
    }

    vc_table.remove_entry(this->ua_entry);
    ua_txn->do_io_close();
  } else {
    ink_assert(ua_buffer_reader != nullptr);
    ua_txn->release(ua_buffer_reader);
    ua_buffer_reader = nullptr;
    // ua_txn       = NULL;
  }

  return 0;
}

int
HttpSM::tunnel_handler_ua_push(int event, HttpTunnelProducer *p)
{
  STATE_ENTER(&HttpSM::tunnel_handler_ua_push, event);

  pushed_response_body_bytes += p->bytes_read;
  client_request_body_bytes += p->bytes_read;

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
    p->read_success     = true;
    ua_entry->in_tunnel = false;
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
      t_state.squid_codes.log_code = SQUID_LOG_TCP_SWAPFAIL;
      p->vc->do_io_close(EHTTP_ERROR);
      p->read_vio = nullptr;
      tunnel.chain_abort_all(p);
      HTTP_INCREMENT_DYN_STAT(http_cache_read_errors);
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

  HTTP_DECREMENT_DYN_STAT(http_current_cache_connections_stat);
  return 0;
}

int
HttpSM::tunnel_handler_cache_write(int event, HttpTunnelConsumer *c)
{
  STATE_ENTER(&HttpSM::tunnel_handler_cache_write, event);

  HttpTransact::CacheWriteStatus_t *status_ptr =
    (c->producer->vc_type == HT_TRANSFORM) ? &t_state.cache_info.transform_write_status : &t_state.cache_info.write_status;

  switch (event) {
  case VC_EVENT_ERROR:
  case VC_EVENT_EOS:
    // Abnormal termination
    *status_ptr  = HttpTransact::CACHE_WRITE_ERROR;
    c->write_vio = nullptr;
    c->vc->do_io_close(EHTTP_ERROR);

    HTTP_INCREMENT_DYN_STAT(http_cache_write_errors);
    SMDebug("http", "[%" PRId64 "] aborting cache write due %s event from cache", sm_id, HttpDebugNames::get_event_name(event));
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
      *status_ptr      = HttpTransact::CACHE_WRITE_ERROR;
      c->write_success = false;
      c->vc->do_io_close(EHTTP_ERROR);
    } else {
      *status_ptr      = HttpTransact::CACHE_WRITE_COMPLETE;
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

  HTTP_DECREMENT_DYN_STAT(http_current_cache_connections_stat);
  return 0;
}

int
HttpSM::tunnel_handler_post_ua(int event, HttpTunnelProducer *p)
{
  STATE_ENTER(&HttpSM::tunnel_handler_post_ua, event);
  client_request_body_bytes = p->init_bytes_done + p->bytes_read;

  switch (event) {
  case VC_EVENT_INACTIVITY_TIMEOUT:
  case VC_EVENT_ACTIVE_TIMEOUT:
    if (client_response_hdr_bytes == 0) {
      p->handler_state = HTTP_SM_POST_UA_FAIL;
      set_ua_abort(HttpTransact::ABORTED, event);

      SMDebug("http_tunnel", "send 408 response to client to vc %p, tunnel vc %p", ua_txn->get_netvc(), p->vc);

      tunnel.chain_abort_all(p);
      server_session = nullptr;
      // Reset the inactivity timeout, otherwise the InactivityCop will callback again in the next second.
      ua_txn->set_inactivity_timeout(HRTIME_SECONDS(t_state.txn_conf->transaction_no_activity_timeout_in));
      // if it is active timeout case, we need to give another chance to send 408 response;
      ua_txn->set_active_timeout(HRTIME_SECONDS(t_state.txn_conf->transaction_active_timeout_in));

      p->vc->do_io_write(nullptr, 0, nullptr);
      p->vc->do_io_shutdown(IO_SHUTDOWN_READ);

      return 0;
    }
  // fall through
  case VC_EVENT_EOS:
  // My reading of spec says that user agents can not terminate
  //  posts with a half close so this is an error
  case VC_EVENT_ERROR:
    //  Did not complete post tunneling.  Abort the
    //   server and close the ua
    p->handler_state = HTTP_SM_POST_UA_FAIL;
    set_ua_abort(HttpTransact::ABORTED, event);

    tunnel.chain_abort_all(p);
    server_session = nullptr;
    p->read_vio    = nullptr;
    p->vc->do_io_close(EHTTP_ERROR);

    // the in_tunnel status on both the ua & and
    //   it's consumer must already be set to true.  Previously
    //   we were setting it again to true but incorrectly in
    //   the case of a transform
    hsm_release_assert(ua_entry->in_tunnel == true);
    if (p->consumer_list.head && p->consumer_list.head->vc_type == HT_TRANSFORM) {
      hsm_release_assert(post_transform_info.entry->in_tunnel == true);
    } else if (server_entry != nullptr) {
      hsm_release_assert(server_entry->in_tunnel == true);
    }
    break;

  case VC_EVENT_READ_COMPLETE:
  case HTTP_TUNNEL_EVENT_PRECOMPLETE:
    p->handler_state    = HTTP_SM_POST_SUCCESS;
    p->read_success     = true;
    ua_entry->in_tunnel = false;

    if (p->do_dechunking || p->do_chunked_passthru) {
      if (p->chunked_handler.truncation) {
        tunnel.abort_cache_write_finish_others(p);
      } else {
        tunnel.local_finish_all(p);
      }
    }

    // Initiate another read to watch catch aborts and
    //   timeouts
    ua_entry->vc_handler = &HttpSM::state_watch_for_client_abort;
    ua_entry->read_vio   = p->vc->do_io_read(this, INT64_MAX, ua_buffer_reader->mbuf);
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
  is_using_post_buffer                      = false;

  if (post_failed) {
    post_failed = false;
    handle_post_failure();
  } else {
    do_setup_post_tunnel(HTTP_SERVER_VC);
  }

  return 0;
}

int
HttpSM::tunnel_handler_post_server(int event, HttpTunnelConsumer *c)
{
  STATE_ENTER(&HttpSM::tunnel_handler_post_server, event);

  server_request_body_bytes = c->bytes_written;

  switch (event) {
  case VC_EVENT_EOS:
  case VC_EVENT_ERROR:
  case VC_EVENT_INACTIVITY_TIMEOUT:
  case VC_EVENT_ACTIVE_TIMEOUT:
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
    if (c->producer->vc_type == HT_TRANSFORM) {
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
    ink_assert(ua_producer->vc_type == HT_HTTP_CLIENT);
    ink_assert(ua_producer->vc == ua_txn);
    ink_assert(ua_producer->vc == ua_entry->vc);

    // Before shutting down, initiate another read
    //  on the user agent in order to get timeouts
    //  coming to the state machine and not the tunnel
    ua_entry->vc_handler = &HttpSM::state_watch_for_client_abort;

    // YTS Team, yamsat Plugin
    // When event is VC_EVENT_ERROR,and when redirection is enabled
    // do not shut down the client read
    if (enable_redirection) {
      if (ua_producer->vc_type == HT_STATIC && event != VC_EVENT_ERROR && event != VC_EVENT_EOS) {
        ua_entry->read_vio = ua_producer->vc->do_io_read(this, INT64_MAX, c->producer->read_buffer);
        // ua_producer->vc->do_io_shutdown(IO_SHUTDOWN_READ);
        t_state.client_info.pipeline_possible = false;
      } else {
        if (ua_producer->vc_type == HT_STATIC && t_state.redirect_info.redirect_in_process) {
          post_failed = true;
        }
      }
    } else {
      ua_entry->read_vio = ua_producer->vc->do_io_read(this, INT64_MAX, c->producer->read_buffer);
      // we should not shutdown read side of the client here to prevent sending a reset
      // ua_producer->vc->do_io_shutdown(IO_SHUTDOWN_READ);
      t_state.client_info.pipeline_possible = false;
    } // end of added logic

    // We want to shutdown the tunnel here and see if there
    //   is a response on from the server.  Mark the user
    //   agent as down so that tunnel concludes.
    ua_producer->alive         = false;
    ua_producer->handler_state = HTTP_SM_POST_SERVER_FAIL;
    ink_assert(tunnel.is_tunnel_alive() == false);
    break;

  case VC_EVENT_WRITE_COMPLETE:
    // Completed successfully
    c->write_success = true;
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
  case VC_EVENT_READ_COMPLETE:
  case HTTP_TUNNEL_EVENT_PRECOMPLETE:
  // We should never get these event since we don't know
  //  how long the stream is
  default:
    ink_release_assert(0);
  }

  // Update stats
  switch (p->vc_type) {
  case HT_HTTP_SERVER:
    server_response_body_bytes += p->bytes_read;
    break;
  case HT_HTTP_CLIENT:
    client_request_body_bytes += p->bytes_read;
    break;
  default:
    // Covered here:
    // HT_CACHE_READ, HT_CACHE_WRITE,
    // HT_TRANSFORM, HT_STATIC.
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
  case HT_HTTP_SERVER:
    server_request_body_bytes += c->bytes_written;
    break;
  case HT_HTTP_CLIENT:
    client_response_body_bytes += c->bytes_written;
    break;
  default:
    // Handled here:
    // HT_CACHE_READ, HT_CACHE_WRITE, HT_TRANSFORM,
    // HT_STATIC
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
  if (post_transform_info.entry) {
    i = &post_transform_info;
    ink_assert(c->vc == i->entry->vc);
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
  if (t_state.range_setup == HttpTransact::RANGE_NONE) {
    switch (t_state.pre_transform_source) {
    case HttpTransact::SOURCE_HTTP_ORIGIN_SERVER:
      server_response_body_bytes = client_response_body_bytes;
      break;
    case HttpTransact::SOURCE_CACHE:
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
HttpSM::state_srv_lookup(int event, void *data)
{
  STATE_ENTER(&HttpSM::state_srv_lookup, event);

  ink_assert(t_state.req_flavor == HttpTransact::REQ_FLAVOR_SCHEDULED_UPDATE ||
             t_state.req_flavor == HttpTransact::REQ_FLAVOR_REVPROXY || ua_entry->vc != nullptr);

  switch (event) {
  case EVENT_SRV_LOOKUP:
    pending_action = nullptr;
    process_srv_info((HostDBInfo *)data);
    break;
  case EVENT_SRV_IP_REMOVED:
    ink_assert(!"Unexpected SRV event from HostDB. What up, Eric?");
    break;
  default:
    ink_assert(!"Unexpected event");
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
    SMDebug("url_rewrite", "completed processor-based remapping request for [%" PRId64 "]", sm_id);
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

void
HttpSM::do_remap_request(bool run_inline)
{
  SMDebug("http_seq", "[HttpSM::do_remap_request] Remapping request");
  SMDebug("url_rewrite", "Starting a possible remapping for request [%" PRId64 "]", sm_id);
  bool ret = remapProcessor.setup_for_remap(&t_state, m_remap);

  // Preserve effective url before remap
  t_state.unmapped_url.create(t_state.hdr_info.client_request.url_get()->m_heap);
  t_state.unmapped_url.copy(t_state.hdr_info.client_request.url_get());
  // Depending on a variety of factors the HOST field may or may not have been promoted to the
  // client request URL. The unmapped URL should always have that promotion done. If the HOST field
  // is not already there, promote it only in the unmapped_url. This avoids breaking any logic that
  // depends on the lack of promotion in the client request URL.
  if (!t_state.unmapped_url.m_url_impl->m_ptr_host) {
    MIMEField *host_field = t_state.hdr_info.client_request.field_find(MIME_FIELD_HOST, MIME_LEN_HOST);
    if (host_field) {
      int host_len;
      const char *host_name = host_field->value_get(&host_len);
      if (host_name && host_len) {
        t_state.unmapped_url.host_set(host_name, host_len);
      }
    }
  }

  if (!ret) {
    SMDebug("url_rewrite", "Could not find a valid remapping entry for this request [%" PRId64 "]", sm_id);
    if (!run_inline) {
      handleEvent(EVENT_REMAP_COMPLETE, nullptr);
    }
    return;
  }

  SMDebug("url_rewrite", "Found a remap map entry for [%" PRId64 "], attempting to remap request and call any plugins", sm_id);
  Action *remap_action_handle = remapProcessor.perform_remap(this, &t_state);

  if (remap_action_handle != ACTION_RESULT_DONE) {
    SMDebug("url_rewrite", "Still more remapping needed for [%" PRId64 "]", sm_id);
    ink_assert(!pending_action);
    pending_action = remap_action_handle;
  }

  return;
}

void
HttpSM::do_hostdb_lookup()
{
  /*
      //////////////////////////////////////////
      // if a connection to the origin server //
      // is currently opened --- close it.    //
      //////////////////////////////////////////
      if (m_origin_server_vc != 0) {
     origin_server_close(CLOSE_CONNECTION);
     if (m_response_body_tunnel_buffer_.buf() != 0)
         m_response_body_tunnel_buffer_.reset();
      }
      */

  ink_assert(t_state.dns_info.lookup_name != nullptr);
  ink_assert(pending_action == nullptr);

  milestones[TS_MILESTONE_DNS_LOOKUP_BEGIN] = Thread::get_hrtime();

  if (t_state.txn_conf->srv_enabled) {
    char d[MAXDNAME];

    // Look at the next_hop_scheme to determine what scheme to put in the SRV lookup
    unsigned int scheme_len = sprintf(d, "_%s._tcp.", hdrtoken_index_to_wks(t_state.next_hop_scheme));
    ink_strlcpy(d + scheme_len, t_state.server_info.name, sizeof(d) - scheme_len);

    SMDebug("dns_srv", "Beginning lookup of SRV records for origin %s", d);

    HostDBProcessor::Options opt;
    if (t_state.api_txn_dns_timeout_value != -1) {
      opt.timeout = t_state.api_txn_dns_timeout_value;
    }
    Action *srv_lookup_action_handle =
      hostDBProcessor.getSRVbyname_imm(this, (cb_process_result_pfn)&HttpSM::process_srv_info, d, 0, opt);

    if (srv_lookup_action_handle != ACTION_RESULT_DONE) {
      ink_assert(!pending_action);
      pending_action = srv_lookup_action_handle;
    } else {
      char *host_name = t_state.dns_info.srv_lookup_success ? t_state.dns_info.srv_hostname : t_state.dns_info.lookup_name;
      opt.port        = t_state.dns_info.srv_lookup_success ?
                   t_state.dns_info.srv_port :
                   t_state.server_info.dst_addr.isValid() ? t_state.server_info.dst_addr.host_order_port() :
                                                            t_state.hdr_info.client_request.port_get();
      opt.flags = (t_state.cache_info.directives.does_client_permit_dns_storing) ? HostDBProcessor::HOSTDB_DO_NOT_FORCE_DNS :
                                                                                   HostDBProcessor::HOSTDB_FORCE_DNS_RELOAD;
      opt.timeout        = (t_state.api_txn_dns_timeout_value != -1) ? t_state.api_txn_dns_timeout_value : 0;
      opt.host_res_style = ua_txn->get_host_res_style();

      Action *dns_lookup_action_handle =
        hostDBProcessor.getbyname_imm(this, (cb_process_result_pfn)&HttpSM::process_hostdb_info, host_name, 0, opt);
      if (dns_lookup_action_handle != ACTION_RESULT_DONE) {
        ink_assert(!pending_action);
        pending_action = dns_lookup_action_handle;
      } else {
        call_transact_and_set_next_state(nullptr);
      }
    }
    return;
  } else { /* we aren't using SRV stuff... */
    SMDebug("http_seq", "[HttpSM::do_hostdb_lookup] Doing DNS Lookup");

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
      SMDebug("http_timeout", "beginning DNS lookup. allowing %d mseconds for DNS lookup", t_state.api_txn_dns_timeout_value);
    }

    HostDBProcessor::Options opt;
    opt.port  = server_port;
    opt.flags = (t_state.cache_info.directives.does_client_permit_dns_storing) ? HostDBProcessor::HOSTDB_DO_NOT_FORCE_DNS :
                                                                                 HostDBProcessor::HOSTDB_FORCE_DNS_RELOAD;
    opt.timeout        = (t_state.api_txn_dns_timeout_value != -1) ? t_state.api_txn_dns_timeout_value : 0;
    opt.host_res_style = ua_txn->get_host_res_style();

    Action *dns_lookup_action_handle = hostDBProcessor.getbyname_imm(this, (cb_process_result_pfn)&HttpSM::process_hostdb_info,
                                                                     t_state.dns_info.lookup_name, 0, opt);

    if (dns_lookup_action_handle != ACTION_RESULT_DONE) {
      ink_assert(!pending_action);
      pending_action = dns_lookup_action_handle;
    } else {
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
  ink_assert(pending_action == nullptr);

  SMDebug("http_seq", "[HttpSM::do_hostdb_reverse_lookup] Doing reverse DNS Lookup");

  IpEndpoint addr;
  ats_ip_pton(t_state.dns_info.lookup_name, &addr.sa);
  Action *dns_lookup_action_handle = hostDBProcessor.getbyaddr_re(this, &addr.sa);

  if (dns_lookup_action_handle != ACTION_RESULT_DONE) {
    ink_assert(!pending_action);
    pending_action = dns_lookup_action_handle;
  }
  return;
}

void
HttpSM::do_hostdb_update_if_necessary()
{
  int issue_update = 0;

  if (t_state.current.server == nullptr || plugin_tunnel_type != HTTP_NO_PLUGIN_TUNNEL) {
    // No server, so update is not necessary
    return;
  }
  // If we failed back over to the origin server, we don't have our
  //   hostdb information anymore which means we shouldn't update the hostdb
  if (!ats_ip_addr_eq(&t_state.current.server->dst_addr.sa, t_state.host_db_info.ip())) {
    SMDebug("http", "[%" PRId64 "] skipping hostdb update due to server failover", sm_id);
    return;
  }

  if (t_state.updated_server_version != HostDBApplicationInfo::HTTP_VERSION_UNDEFINED) {
    // we may have incorrectly assumed that the hostdb had the wrong version of
    // http for the server because our first few connect attempts to the server
    // failed, causing us to downgrade our requests to a lower version and changing
    // our information about the server version.
    //
    // This test therefore just issues the update only if the hostdb version is
    // in fact different from the version we want the value to be updated to.
    if (t_state.host_db_info.app.http_data.http_version != t_state.updated_server_version) {
      t_state.host_db_info.app.http_data.http_version = t_state.updated_server_version;
      issue_update |= 1;
    }

    t_state.updated_server_version = HostDBApplicationInfo::HTTP_VERSION_UNDEFINED;
  }
  // Check to see if we need to report or clear a connection failure
  if (t_state.current.server->had_connect_fail()) {
    issue_update |= 1;
    mark_host_failure(&t_state.host_db_info, t_state.client_request_time);
  } else {
    if (t_state.host_db_info.app.http_data.last_failure != 0) {
      t_state.host_db_info.app.http_data.last_failure = 0;
      issue_update |= 1;
      char addrbuf[INET6_ADDRPORTSTRLEN];
      SMDebug("http", "[%" PRId64 "] hostdb update marking IP: %s as up", sm_id,
              ats_ip_nptop(&t_state.current.server->dst_addr.sa, addrbuf, sizeof(addrbuf)));
    }

    if (t_state.dns_info.srv_lookup_success && t_state.dns_info.srv_app.http_data.last_failure != 0) {
      t_state.dns_info.srv_app.http_data.last_failure = 0;
      hostDBProcessor.setby_srv(t_state.dns_info.lookup_name, 0, t_state.dns_info.srv_hostname, &t_state.dns_info.srv_app);
      SMDebug("http", "[%" PRId64 "] hostdb update marking SRV: %s as up", sm_id, t_state.dns_info.srv_hostname);
    }
  }

  if (issue_update) {
    hostDBProcessor.setby(t_state.current.server->name, strlen(t_state.current.server->name), &t_state.current.server->dst_addr.sa,
                          &t_state.host_db_info.app);
  }

  char addrbuf[INET6_ADDRPORTSTRLEN];
  SMDebug("http", "server info = %s", ats_ip_nptop(&t_state.current.server->dst_addr.sa, addrbuf, sizeof(addrbuf)));
  return;
}

/*
 * range entry valid [a,b] (a >= 0 and b >= 0 and a <= b)
 * HttpTransact::RANGE_NONE if the content length of cached copy is zero or
 * no range entry
 * HttpTransact::RANGE_NOT_SATISFIABLE iff all range entries are valid but
 * none overlap the current extent of the cached copy
 * HttpTransact::RANGE_NOT_HANDLED if out-of-order Range entries or
 * the cached copy`s content_length is INT64_MAX (e.g. read_from_writer and trunked)
 * HttpTransact::RANGE_REQUESTED if all sub range entries are valid and
 * in order (remove the entries that not overlap the extent of cache copy)
 */
void
HttpSM::parse_range_and_compare(MIMEField *field, int64_t content_length)
{
  int prev_good_range = -1;
  const char *value;
  int value_len;
  int n_values;
  int nr          = 0; // number of valid ranges, also index to range array.
  int not_satisfy = 0;
  HdrCsvIter csv;
  const char *s, *e, *tmp;
  RangeRecord *ranges = nullptr;
  int64_t start, end;

  ink_assert(field != nullptr && t_state.range_setup == HttpTransact::RANGE_NONE && t_state.ranges == nullptr);

  if (content_length <= 0) {
    return;
  }

  // ToDo: Can this really happen?
  if (content_length == INT64_MAX) {
    t_state.range_setup = HttpTransact::RANGE_NOT_HANDLED;
    return;
  }

  if (parse_range_done) {
    Debug("http_range", "parse_range already done, t_state.range_setup %d", t_state.range_setup);
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

  ranges = new RangeRecord[n_values];
  value += 6; // skip leading 'bytes='
  value_len -= 6;

  // assume range_in_cache
  t_state.range_in_cache = true;

  for (; value; value = csv.get_next(&value_len)) {
    if (!(tmp = (const char *)memchr(value, '-', value_len))) {
      t_state.range_setup = HttpTransact::RANGE_NONE;
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
          t_state.range_setup = HttpTransact::RANGE_NONE;
          goto Lfaild;
        }
        start = new_start;
      }
      // skip last white spaces
      for (; s < e && ParseRules::is_ws(*s); ++s) {
        ;
      }

      if (s < e || start < 0) {
        t_state.range_setup = HttpTransact::RANGE_NONE;
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
        t_state.range_setup = HttpTransact::RANGE_NONE;
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
          t_state.range_setup = HttpTransact::RANGE_NONE;
          goto Lfaild;
        }
        end = new_end;
      }
      // skip last white spaces
      for (; s < e && ParseRules::is_ws(*s); ++s) {
        ;
      }

      if (s < e || end < 0) {
        t_state.range_setup = HttpTransact::RANGE_NONE;
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
      t_state.range_setup = HttpTransact::RANGE_NONE;
      goto Lfaild;
    }

    if (prev_good_range >= 0 && start <= ranges[prev_good_range]._end) {
      t_state.range_setup = HttpTransact::RANGE_NOT_HANDLED;
      goto Lfaild;
    }

    ink_assert(start >= 0 && end >= 0 && start < content_length && end < content_length);

    prev_good_range   = nr;
    ranges[nr]._start = start;
    ranges[nr]._end   = end;
    ++nr;

    if (!cache_sm.cache_read_vc->is_pread_capable() && cache_config_read_while_writer == 2) {
      // write in progress, check if request range not in cache yet
      HTTPInfo::FragOffset *frag_offset_tbl = t_state.cache_info.object_read->get_frag_table();
      int frag_offset_cnt                   = t_state.cache_info.object_read->get_frag_offset_count();

      if (!frag_offset_tbl || !frag_offset_cnt || (frag_offset_tbl[frag_offset_cnt - 1] < (uint64_t)end)) {
        Debug("http_range", "request range in cache, end %" PRId64 ", frg_offset_cnt %d" PRId64, end, frag_offset_cnt);
        t_state.range_in_cache = false;
      }
    }
  }

  if (nr > 0) {
    t_state.range_setup      = HttpTransact::RANGE_REQUESTED;
    t_state.ranges           = ranges;
    t_state.num_range_fields = nr;
    return;
  }

  if (not_satisfy) {
    t_state.range_setup = HttpTransact::RANGE_NOT_SATISFIABLE;
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
  if (t_state.range_setup != HttpTransact::RANGE_REQUESTED && t_state.range_setup != HttpTransact::RANGE_NOT_TRANSFORM_REQUESTED) {
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

  Debug("http_range", "Pre-calculated Content-Length for Range response is %" PRId64, t_state.range_output_cl);
}

void
HttpSM::do_range_parse(MIMEField *range_field)
{
  int num_chars_for_ct = 0;
  t_state.cache_info.object_read->response_get()->value_get(MIME_FIELD_CONTENT_TYPE, MIME_LEN_CONTENT_TYPE, &num_chars_for_ct);

  int64_t content_length   = t_state.cache_info.object_read->object_size_get();
  int64_t num_chars_for_cl = num_chars_for_int(content_length);

  parse_range_and_compare(range_field, content_length);
  calculate_output_cl(num_chars_for_ct, num_chars_for_cl);
}

// this function looks for any Range: headers, parses them and either
// sets up a transform processor to handle the request OR defers to the
// HttpTunnel
void
HttpSM::do_range_setup_if_necessary()
{
  MIMEField *field;

  ink_assert(t_state.cache_info.object_read != nullptr);

  field = t_state.hdr_info.client_request.field_find(MIME_FIELD_RANGE, MIME_LEN_RANGE);
  ink_assert(field != nullptr);

  t_state.range_setup = HttpTransact::RANGE_NONE;

  if (t_state.method == HTTP_WKSIDX_GET && t_state.hdr_info.client_request.version_get() == HTTPVersion(1, 1)) {
    do_range_parse(field);

    if (t_state.range_setup == HttpTransact::RANGE_REQUESTED) {
      bool do_transform = false;

      if (!t_state.range_in_cache) {
        Debug("http_range", "range can't be satisfied from cache, force origin request");
        t_state.cache_lookup_result = HttpTransact::CACHE_LOOKUP_MISS;
        return;
      }

      if (t_state.num_range_fields > 1) {
        if (0 == t_state.txn_conf->allow_multi_range) {
          t_state.range_setup = HttpTransact::RANGE_NONE;                                 // No Range required (not allowed)
          t_state.hdr_info.client_request.field_delete(MIME_FIELD_RANGE, MIME_LEN_RANGE); // ... and nuke the Range header too
          t_state.num_range_fields = 0;
        } else if (1 == t_state.txn_conf->allow_multi_range) {
          do_transform = true;
        } else {
          t_state.num_range_fields = 0;
          t_state.range_setup      = HttpTransact::RANGE_NOT_SATISFIABLE;
        }
      } else {
        if (cache_sm.cache_read_vc->is_pread_capable()) {
          // If only one range entry and pread is capable, no need transform range
          t_state.range_setup = HttpTransact::RANGE_NOT_TRANSFORM_REQUESTED;
        } else {
          do_transform = true;
        }
      }

      // We have to do the transform on (allowed) multi-range request, *or* if the VC is not pread capable
      if (do_transform) {
        if (api_hooks.get(TS_HTTP_RESPONSE_TRANSFORM_HOOK) == nullptr) {
          int field_content_type_len = -1;
          const char *content_type   = t_state.cache_info.object_read->response_get()->value_get(
            MIME_FIELD_CONTENT_TYPE, MIME_LEN_CONTENT_TYPE, &field_content_type_len);

          Debug("http_trans", "Unable to accelerate range request, fallback to transform");

          // create a Range: transform processor for requests of type Range: bytes=1-2,4-5,10-100 (eg. multiple ranges)
          INKVConnInternal *range_trans = transformProcessor.range_transform(
            mutex.get(), t_state.ranges, t_state.num_range_fields, &t_state.hdr_info.transform_response, content_type,
            field_content_type_len, t_state.cache_info.object_read->object_size_get());
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
  // ink_assert(server_session == NULL);
  ink_assert(pending_action == nullptr);

  HTTP_INCREMENT_DYN_STAT(http_cache_lookups_stat);

  milestones[TS_MILESTONE_CACHE_OPEN_READ_BEGIN] = Thread::get_hrtime();
  t_state.cache_lookup_result                    = HttpTransact::CACHE_LOOKUP_NONE;
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

  SMDebug("http_seq", "[HttpSM::do_cache_lookup_and_read] [%" PRId64 "] Issuing cache lookup for URL %s", sm_id,
          c_url->string_get(&t_state.arena));

  HttpCacheKey key;
  Cache::generate_key(&key, c_url, t_state.txn_conf->cache_generation_number);

  Action *cache_action_handle =
    cache_sm.open_read(&key, c_url, &t_state.hdr_info.client_request, t_state.txn_conf,
                       (time_t)((t_state.cache_control.pin_in_cache_for < 0) ? 0 : t_state.cache_control.pin_in_cache_for));
  //
  // pin_in_cache value is an open_write parameter.
  // It is passed in open_read to allow the cluster to
  // optimize the typical open_read/open_read failed/open_write
  // sequence.
  //
  if (cache_action_handle != ACTION_RESULT_DONE) {
    ink_assert(!pending_action);
    pending_action = cache_action_handle;
  }
  REMEMBER((long)pending_action, reentrancy_count);

  return;
}

void
HttpSM::do_cache_delete_all_alts(Continuation *cont)
{
  // Do not delete a non-existant object.
  ink_assert(t_state.cache_info.object_read);

  SMDebug("http_seq", "[HttpSM::do_cache_delete_all_alts] Issuing cache delete for %s",
          t_state.cache_info.lookup_url->string_get_ref());

  Action *cache_action_handle = nullptr;

  HttpCacheKey key;
  Cache::generate_key(&key, t_state.cache_info.lookup_url, t_state.txn_conf->cache_generation_number);
  cache_action_handle = cacheProcessor.remove(cont, &key);
  if (cont != nullptr) {
    if (cache_action_handle != ACTION_RESULT_DONE) {
      ink_assert(!pending_action);
      pending_action = cache_action_handle;
    }
  }

  return;
}

inline void
HttpSM::do_cache_prepare_write()
{
  milestones[TS_MILESTONE_CACHE_OPEN_WRITE_BEGIN] = Thread::get_hrtime();
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

  ink_assert(!pending_action);

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
  SMDebug("http_cache_write", "[%" PRId64 "] writing to cache with URL %s", sm_id, s_url->string_get(&t_state.arena));

  HttpCacheKey key;
  Cache::generate_key(&key, s_url, t_state.txn_conf->cache_generation_number);

  Action *cache_action_handle = c_sm->open_write(
    &key, s_url, &t_state.hdr_info.client_request, object_read_info,
    (time_t)((t_state.cache_control.pin_in_cache_for < 0) ? 0 : t_state.cache_control.pin_in_cache_for), retry, allow_multiple);

  if (cache_action_handle != ACTION_RESULT_DONE) {
    ink_assert(!pending_action);
    pending_action = cache_action_handle;
  }
}

void
HttpSM::send_origin_throttled_response()
{
  // if the request is to a parent proxy, do not reset
  // t_state.current.attempts so that another parent or
  // NextHop may be tried.
  if (t_state.current.request_to != HttpTransact::PARENT_PROXY) {
    t_state.current.attempts = t_state.txn_conf->connect_attempts_max_retries;
  }
  t_state.current.state = HttpTransact::OUTBOUND_CONGESTION;
  call_transact_and_set_next_state(HttpTransact::HandleResponse);
}

static void
set_tls_options(NetVCOptions &opt, OverridableHttpConfigParams *txn_conf)
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

//////////////////////////////////////////////////////////////////////////
//
//  HttpSM::do_http_server_open()
//
//////////////////////////////////////////////////////////////////////////
void
HttpSM::do_http_server_open(bool raw)
{
  int ip_family = t_state.current.server->dst_addr.sa.sa_family;
  auto fam_name = ats_ip_family_name(ip_family);
  SMDebug("http_track", "entered inside do_http_server_open ][%.*s]", static_cast<int>(fam_name.size()), fam_name.data());

  // Make sure we are on the "right" thread
  if (ua_txn) {
    if ((pending_action = ua_txn->adjust_thread(this, EVENT_INTERVAL, nullptr))) {
      return; // Go away if we reschedule
    }
  }
  pending_action = nullptr;
  ink_assert(server_entry == nullptr);

  // Clean up connection tracking info if any. Need to do it now so the selected group
  // is consistent with the actual upstream in case of retry.
  t_state.outbound_conn_track_state.clear();

  // ua_entry can be null if a scheduled update is also a reverse proxy
  // request. Added REVPROXY to the assert below, and then changed checks
  // to be based on ua_txn != NULL instead of req_flavor value.
  ink_assert(ua_entry != nullptr || t_state.req_flavor == HttpTransact::REQ_FLAVOR_SCHEDULED_UPDATE ||
             t_state.req_flavor == HttpTransact::REQ_FLAVOR_REVPROXY);

  ink_assert(pending_action == nullptr);

  if (false == t_state.api_server_addr_set) {
    ink_assert(t_state.current.server->dst_addr.host_order_port() > 0);
  } else {
    ink_assert(t_state.current.server->dst_addr.port() != 0); // verify the plugin set it to something.
  }

  char addrbuf[INET6_ADDRPORTSTRLEN];
  SMDebug("http", "[%" PRId64 "] open connection to %s: %s", sm_id, t_state.current.server->name,
          ats_ip_nptop(&t_state.current.server->dst_addr.sa, addrbuf, sizeof(addrbuf)));

  if (plugin_tunnel) {
    PluginVCCore *t           = plugin_tunnel;
    plugin_tunnel             = nullptr;
    Action *pvc_action_handle = t->connect_re(this);

    // This connect call is always reentrant
    ink_release_assert(pvc_action_handle == ACTION_RESULT_DONE);
    return;
  }

  SMDebug("http_seq", "[HttpSM::do_http_server_open] Sending request to server");

  milestones[TS_MILESTONE_SERVER_CONNECT] = Thread::get_hrtime();
  if (milestones[TS_MILESTONE_SERVER_FIRST_CONNECT] == 0) {
    milestones[TS_MILESTONE_SERVER_FIRST_CONNECT] = milestones[TS_MILESTONE_SERVER_CONNECT];
  }

  // Check for remap rule. If so, only apply ip_allow filter if it is activated (ip_allow_check_enabled_p set).
  // Otherwise, if no remap rule is defined, apply the ip_allow filter.
  if (!t_state.url_remap_success || t_state.url_map.getMapping()->ip_allow_check_enabled_p) {
    // Method allowed on dest IP address check
    sockaddr *server_ip    = &t_state.current.server->dst_addr.sa;
    IpAllow::ACL acl       = IpAllow::match(server_ip, IpAllow::DST_ADDR);
    bool deny_request      = false; // default is fail open.
    int method             = t_state.hdr_info.server_request.method_get_wksidx();
    int method_str_len     = 0;
    const char *method_str = nullptr;

    if (acl.isValid()) {
      if (acl.isDenyAll()) {
        deny_request = true;
      } else if (!acl.isAllowAll()) {
        if (method != -1) {
          deny_request = !acl.isMethodAllowed(method);
        } else {
          method_str   = t_state.hdr_info.server_request.method_get(&method_str_len);
          deny_request = !acl.isNonstandardMethodAllowed(std::string_view(method_str, method_str_len));
        }
      }
    }

    if (deny_request) {
      if (is_debug_tag_set("ip-allow")) {
        ip_text_buffer ipb;
        if (method != -1) {
          method_str     = hdrtoken_index_to_wks(method);
          method_str_len = strlen(method_str);
        } else if (!method_str) {
          method_str = t_state.hdr_info.client_request.method_get(&method_str_len);
        }
        Warning("server '%s' prohibited by ip-allow policy at line %d", ats_ip_ntop(server_ip, ipb, sizeof(ipb)),
                acl.source_line());
        Debug("ip-allow", "Line %d denial for '%.*s' from %s", acl.source_line(), method_str_len, method_str,
              ats_ip_ntop(server_ip, ipb, sizeof(ipb)));
      }
      t_state.current.attempts = t_state.txn_conf->connect_attempts_max_retries; // prevent any more retries with this IP
      call_transact_and_set_next_state(HttpTransact::Forbidden);
      return;
    }
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
  //
  if (t_state.txn_conf->auth_server_session_private == 1 &&
      t_state.hdr_info.server_request.presence(MIME_PRESENCE_AUTHORIZATION | MIME_PRESENCE_PROXY_AUTHORIZATION |
                                               MIME_PRESENCE_WWW_AUTHENTICATE)) {
    SMDebug("http_ss_auth", "Setting server session to private for authorization header");
    will_be_private_ss = true;
  }

  if (t_state.method == HTTP_WKSIDX_POST || t_state.method == HTTP_WKSIDX_PUT) {
    // don't share the session if keep-alive for post is not on
    if (t_state.txn_conf->keep_alive_post_out == 0) {
      SMDebug("http_ss", "Setting server session to private because of keep-alive post out");
      will_be_private_ss = true;
    }
  }

  // If there is already an attached server session mark it as private.
  if (server_session != nullptr && will_be_private_ss) {
    set_server_session_private(true);
  }

  if (raw == false && TS_SERVER_SESSION_SHARING_MATCH_NONE != t_state.txn_conf->server_session_sharing_match &&
      (t_state.txn_conf->keep_alive_post_out == 1 || t_state.hdr_info.request_content_length == 0) && !is_private() &&
      ua_txn != nullptr) {
    HSMresult_t shared_result;
    shared_result = httpSessionManager.acquire_session(this,                                 // state machine
                                                       &t_state.current.server->dst_addr.sa, // ip + port
                                                       t_state.current.server->name,         // hostname
                                                       ua_txn,                               // has ptr to bound ua sessions
                                                       this                                  // sm
    );

    switch (shared_result) {
    case HSM_DONE:
      hsm_release_assert(server_session != nullptr);
      handle_http_server_open();
      return;
    case HSM_NOT_FOUND:
      hsm_release_assert(server_session == nullptr);
      break;
    case HSM_RETRY:
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
           (ua_txn != nullptr)) {
    Http1ServerSession *existing_ss = ua_txn->get_server_session();

    if (existing_ss) {
      // [amc] Not sure if this is the best option, but we don't get here unless session sharing is disabled
      // so there's no point in further checking on the match or pool values. But why check anything? The
      // client has already exchanged a request with this specific origin server and has sent another one
      // shouldn't we just automatically keep the association?
      if (ats_ip_addr_port_eq(&existing_ss->get_server_ip().sa, &t_state.current.server->dst_addr.sa)) {
        ua_txn->attach_server_session(nullptr);
        existing_ss->state = HSS_ACTIVE;
        this->attach_server_session(existing_ss);
        hsm_release_assert(server_session != nullptr);
        handle_http_server_open();
        return;
      } else {
        // As this is in the non-sharing configuration, we want to close
        // the existing connection and call connect_re to get a new one
        existing_ss->get_netvc()->set_inactivity_timeout(HRTIME_SECONDS(t_state.txn_conf->keep_alive_no_activity_timeout_out));
        existing_ss->release();
        ua_txn->attach_server_session(nullptr);
      }
    }
  }
  // Otherwise, we release the existing connection and call connect_re
  // to get a new one.
  // ua_txn is null when t_state.req_flavor == REQ_FLAVOR_SCHEDULED_UPDATE
  else if (ua_txn != nullptr) {
    Http1ServerSession *existing_ss = ua_txn->get_server_session();
    if (existing_ss) {
      existing_ss->get_netvc()->set_inactivity_timeout(HRTIME_SECONDS(t_state.txn_conf->keep_alive_no_activity_timeout_out));
      existing_ss->release();
      ua_txn->attach_server_session(nullptr);
    }
  }
  // Check to see if we have reached the max number of connections.
  // Atomically read the current number of connections and check to see
  // if we have gone above the max allowed.
  if (t_state.http_config_param->server_max_connections > 0) {
    int64_t sum;

    HTTP_READ_GLOBAL_DYN_SUM(http_current_server_connections_stat, sum);

    // Note that there is a potential race condition here where
    // the value of the http_current_server_connections_stat gets changed
    // between the statement above and the check below.
    // If this happens, we might go over the max by 1 but this is ok.
    if (sum >= t_state.http_config_param->server_max_connections) {
      httpSessionManager.purge_keepalives();
      // Eventually may want to have a queue as the origin_max_connection does to allow for a combination
      // of retries and errors.  But at this point, we are just going to allow the error case.
      t_state.current.state = HttpTransact::CONNECTION_ERROR;
      call_transact_and_set_next_state(HttpTransact::HandleResponse);
      return;
    }
  }

  // See if the outbound connection tracker data is needed. If so, get it here for consistency.
  if (t_state.txn_conf->outbound_conntrack.max > 0 || t_state.http_config_param->origin_min_keep_alive_connections > 0) {
    t_state.outbound_conn_track_state = OutboundConnTrack::obtain(
      t_state.txn_conf->outbound_conntrack, std::string_view{t_state.current.server->name}, t_state.current.server->dst_addr);
  }

  // Check to see if we have reached the max number of connections on this upstream host.
  if (t_state.txn_conf->outbound_conntrack.max > 0) {
    auto &ct_state = t_state.outbound_conn_track_state;
    auto ccount    = ct_state.reserve();
    if (ccount > t_state.txn_conf->outbound_conntrack.max) {
      ct_state.release();

      ink_assert(pending_action == nullptr); // in case of reschedule must not have already pending.

      // If the queue is disabled, reschedule.
      if (t_state.http_config_param->outbound_conntrack.queue_size < 0) {
        ct_state.enqueue();
        ct_state.rescheduled();
        pending_action =
          eventProcessor.schedule_in(this, HRTIME_MSECONDS(t_state.http_config_param->outbound_conntrack.queue_delay.count()));
      } else if (t_state.http_config_param->outbound_conntrack.queue_size > 0) { // queue enabled, check for a slot
        auto wcount = ct_state.enqueue();
        if (wcount < t_state.http_config_param->outbound_conntrack.queue_size) {
          ct_state.rescheduled();
          SMDebug("http", "%s", lbw().print("[{}] queued for {}\0", sm_id, t_state.current.server->dst_addr).data());
          pending_action =
            eventProcessor.schedule_in(this, HRTIME_MSECONDS(t_state.http_config_param->outbound_conntrack.queue_delay.count()));
        } else {              // the queue is full
          ct_state.dequeue(); // release the queue slot
          ct_state.blocked(); // note the blockage.
          HTTP_INCREMENT_DYN_STAT(http_origin_connections_throttled_stat);
          send_origin_throttled_response();
        }
      } else { // queue size is 0, always block.
        ct_state.blocked();
        HTTP_INCREMENT_DYN_STAT(http_origin_connections_throttled_stat);
        send_origin_throttled_response();
      }

      ct_state.Warn_Blocked(&t_state.txn_conf->outbound_conntrack, sm_id, ccount - 1, &t_state.current.server->dst_addr.sa,
                            debug_on && is_debug_tag_set("http") ? "http" : nullptr);

      return;
    } else {
      ct_state.Note_Unblocked(&t_state.txn_conf->outbound_conntrack, ccount, &t_state.current.server->dst_addr.sa);
    }

    ct_state.update_max_count(ccount);
  }

  // We did not manage to get an existing session and need to open a new connection
  Action *connect_action_handle;

  NetVCOptions opt;
  opt.f_blocking_connect = false;
  opt.set_sock_param(t_state.txn_conf->sock_recv_buffer_size_out, t_state.txn_conf->sock_send_buffer_size_out,
                     t_state.txn_conf->sock_option_flag_out, t_state.txn_conf->sock_packet_mark_out,
                     t_state.txn_conf->sock_packet_tos_out);

  set_tls_options(opt, t_state.txn_conf);

  opt.ip_family = ip_family;

  if (ua_txn) {
    opt.local_port = ua_txn->get_outbound_port();

    const IpAddr &outbound_ip = AF_INET6 == opt.ip_family ? ua_txn->get_outbound_ip6() : ua_txn->get_outbound_ip4();
    if (outbound_ip.isValid()) {
      opt.addr_binding = NetVCOptions::INTF_ADDR;
      opt.local_ip     = outbound_ip;
    } else if (ua_txn->is_outbound_transparent()) {
      opt.addr_binding = NetVCOptions::FOREIGN_ADDR;
      opt.local_ip     = t_state.client_info.src_addr;
      /* If the connection is server side transparent, we can bind to the
         port that the client chose instead of randomly assigning one at
         the proxy.  This is controlled by the 'use_client_source_port'
         configuration parameter.
      */

      NetVConnection *client_vc = ua_txn->get_netvc();
      if (t_state.http_config_param->use_client_source_port && nullptr != client_vc) {
        opt.local_port = client_vc->get_remote_port();
      }
    }
  }

  int scheme_to_use = t_state.scheme; // get initial scheme

  if (!t_state.is_websocket) { // if not websocket, then get scheme from server request
    int new_scheme_to_use = t_state.hdr_info.server_request.url_get()->scheme_get_wksidx();
    // if the server_request url scheme was never set, try the client_request
    if (new_scheme_to_use < 0) {
      new_scheme_to_use = t_state.hdr_info.client_request.url_get()->scheme_get_wksidx();
    }
    if (new_scheme_to_use >= 0) { // found a new scheme, use it
      scheme_to_use = new_scheme_to_use;
    }
  }

  // draft-stenberg-httpbis-tcp recommends only enabling TFO on indempotent methods or
  // those with intervening protocol layers (eg. TLS).

  if (scheme_to_use == URL_WKSIDX_HTTPS || HttpTransactHeaders::is_method_idempotent(t_state.method)) {
    opt.f_tcp_fastopen = (t_state.txn_conf->sock_option_flag_out & NetVCOptions::SOCK_OPT_TCP_FAST_OPEN);
  }
  opt.ssl_client_cert_name        = t_state.txn_conf->ssl_client_cert_filename;
  opt.ssl_client_private_key_name = t_state.txn_conf->ssl_client_private_key_filename;
  opt.ssl_client_ca_cert_name     = t_state.txn_conf->ssl_client_ca_cert_filename;

  if (scheme_to_use == URL_WKSIDX_HTTPS) {
    SMDebug("http", "calling sslNetProcessor.connect_re");

    int len = 0;
    if (t_state.txn_conf->ssl_client_sni_policy != nullptr && !strcmp(t_state.txn_conf->ssl_client_sni_policy, "remap")) {
      len = strlen(t_state.server_info.name);
      opt.set_sni_servername(t_state.server_info.name, len);
    } else { // Do the default of host header for SNI
      const char *host = t_state.hdr_info.server_request.host_get(&len);
      if (host && len > 0) {
        opt.set_sni_servername(host, len);
      }
    }
    if (t_state.server_info.name) {
      opt.set_ssl_servername(t_state.server_info.name);
    }

    connect_action_handle = sslNetProcessor.connect_re(this,                                 // state machine
                                                       &t_state.current.server->dst_addr.sa, // addr + port
                                                       &opt);
  } else {
    SMDebug("http", "calling netProcessor.connect_re");
    connect_action_handle = netProcessor.connect_re(this,                                 // state machine
                                                    &t_state.current.server->dst_addr.sa, // addr + port
                                                    &opt);
  }

  if (connect_action_handle != ACTION_RESULT_DONE) {
    ink_assert(!pending_action);
    pending_action = connect_action_handle;
  }

  return;
}

void
HttpSM::do_api_callout_internal()
{
  switch (t_state.api_next_action) {
  case HttpTransact::SM_ACTION_API_SM_START:
    cur_hook_id = TS_HTTP_TXN_START_HOOK;
    break;
  case HttpTransact::SM_ACTION_API_PRE_REMAP:
    cur_hook_id = TS_HTTP_PRE_REMAP_HOOK;
    break;
  case HttpTransact::SM_ACTION_API_POST_REMAP:
    cur_hook_id = TS_HTTP_POST_REMAP_HOOK;
    break;
  case HttpTransact::SM_ACTION_API_READ_REQUEST_HDR:
    cur_hook_id = TS_HTTP_READ_REQUEST_HDR_HOOK;
    break;
  case HttpTransact::SM_ACTION_REQUEST_BUFFER_READ_COMPLETE:
    cur_hook_id = TS_HTTP_REQUEST_BUFFER_READ_COMPLETE_HOOK;
    break;
  case HttpTransact::SM_ACTION_API_OS_DNS:
    cur_hook_id = TS_HTTP_OS_DNS_HOOK;
    break;
  case HttpTransact::SM_ACTION_API_SEND_REQUEST_HDR:
    cur_hook_id = TS_HTTP_SEND_REQUEST_HDR_HOOK;
    break;
  case HttpTransact::SM_ACTION_API_READ_CACHE_HDR:
    cur_hook_id = TS_HTTP_READ_CACHE_HDR_HOOK;
    break;
  case HttpTransact::SM_ACTION_API_CACHE_LOOKUP_COMPLETE:
    cur_hook_id = TS_HTTP_CACHE_LOOKUP_COMPLETE_HOOK;
    break;
  case HttpTransact::SM_ACTION_API_READ_RESPONSE_HDR:
    cur_hook_id = TS_HTTP_READ_RESPONSE_HDR_HOOK;
    break;
  case HttpTransact::SM_ACTION_API_SEND_RESPONSE_HDR:
    cur_hook_id                             = TS_HTTP_SEND_RESPONSE_HDR_HOOK;
    milestones[TS_MILESTONE_UA_BEGIN_WRITE] = Thread::get_hrtime();
    break;
  case HttpTransact::SM_ACTION_API_SM_SHUTDOWN:
    if (callout_state == HTTP_API_IN_CALLOUT || callout_state == HTTP_API_DEFERED_SERVER_ERROR) {
      callout_state = HTTP_API_DEFERED_CLOSE;
      return;
    } else {
      cur_hook_id = TS_HTTP_TXN_CLOSE_HOOK;
    }
    break;
  default:
    cur_hook_id = (TSHttpHookID)-1;
    ink_assert(!"not reached");
  }

  cur_hook  = nullptr;
  cur_hooks = 0;
  state_api_callout(0, nullptr);
}

VConnection *
HttpSM::do_post_transform_open()
{
  ink_assert(post_transform_info.vc == nullptr);

  if (is_action_tag_set("http_post_nullt")) {
    txn_hook_prepend(TS_HTTP_REQUEST_TRANSFORM_HOOK, transformProcessor.null_transform(mutex.get()));
  }

  post_transform_info.vc = transformProcessor.open(this, api_hooks.get(TS_HTTP_REQUEST_TRANSFORM_HOOK));
  if (post_transform_info.vc) {
    // Record the transform VC in our table
    post_transform_info.entry          = vc_table.new_entry();
    post_transform_info.entry->vc      = post_transform_info.vc;
    post_transform_info.entry->vc_type = HTTP_TRANSFORM_VC;
  }

  return post_transform_info.vc;
}

VConnection *
HttpSM::do_transform_open()
{
  ink_assert(transform_info.vc == nullptr);
  APIHook *hooks;

  if (is_action_tag_set("http_nullt")) {
    txn_hook_prepend(TS_HTTP_RESPONSE_TRANSFORM_HOOK, transformProcessor.null_transform(mutex.get()));
  }

  hooks = api_hooks.get(TS_HTTP_RESPONSE_TRANSFORM_HOOK);
  if (hooks) {
    transform_info.vc = transformProcessor.open(this, hooks);

    // Record the transform VC in our table
    transform_info.entry          = vc_table.new_entry();
    transform_info.entry->vc      = transform_info.vc;
    transform_info.entry->vc_type = HTTP_TRANSFORM_VC;
  } else {
    transform_info.vc = nullptr;
  }

  return transform_info.vc;
}

void
HttpSM::mark_host_failure(HostDBInfo *info, time_t time_down)
{
  char addrbuf[INET6_ADDRPORTSTRLEN];

  if (info->app.http_data.last_failure == 0) {
    char *url_str = t_state.hdr_info.client_request.url_string_get(&t_state.arena, nullptr);
    Log::error("%s", lbw()
                       .clip(1)
                       .print("CONNECT Error: {} connecting to {} for '{}' (setting last failure time)",
                              ts::bwf::Errno(t_state.current.server->connect_result), t_state.current.server->dst_addr,
                              ts::bwf::FirstOf(url_str, "<none>"))
                       .extend(1)
                       .write('\0')
                       .data());

    if (url_str) {
      t_state.arena.str_free(url_str);
    }
  }

  info->app.http_data.last_failure = time_down;

#ifdef DEBUG
  ink_assert(ink_local_time() + t_state.txn_conf->down_server_timeout > time_down);
#endif

  SMDebug("http", "[%" PRId64 "] hostdb update marking IP: %s as down", sm_id,
          ats_ip_nptop(&t_state.current.server->dst_addr.sa, addrbuf, sizeof(addrbuf)));
}

void
HttpSM::set_ua_abort(HttpTransact::AbortState_t ua_abort, int event)
{
  t_state.client_info.abort = ua_abort;

  switch (ua_abort) {
  case HttpTransact::ABORTED:
  case HttpTransact::MAYBE_ABORTED:
    // More detailed client side abort logging based on event
    switch (event) {
    case VC_EVENT_ERROR:
      t_state.squid_codes.log_code = SQUID_LOG_ERR_CLIENT_READ_ERROR;
      break;
    case VC_EVENT_EOS:
    case VC_EVENT_ACTIVE_TIMEOUT:     // Won't matter. Server will hangup
    case VC_EVENT_INACTIVITY_TIMEOUT: // Won't matter. Send back 408
    // Fall-through
    default:
      t_state.squid_codes.log_code = SQUID_LOG_ERR_CLIENT_ABORT;
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
  }
}

void
HttpSM::mark_server_down_on_client_abort()
{
  /////////////////////////////////////////////////////
  //  Check see if the client aborted because the    //
  //  origin server was too slow in sending the      //
  //  response header.  If so, mark that             //
  //  server as down so other clients won't try to   //
  //  for revalidation or select it from a round     //
  //  robin set                                      //
  //                                                 //
  //  Note: we do not want to mark parent            //
  //  proxies as down with this metric because       //
  //  that upstream proxy may be working but         //
  //  the actual origin server is one that is hung   //
  /////////////////////////////////////////////////////
  if (t_state.current.request_to == HttpTransact::ORIGIN_SERVER && t_state.hdr_info.request_content_length <= 0) {
    if (milestones[TS_MILESTONE_SERVER_FIRST_CONNECT] != 0 && milestones[TS_MILESTONE_SERVER_FIRST_READ] == 0) {
      // Check to see if client waited for the threshold
      //  to declare the origin server as down
      ink_hrtime wait = Thread::get_hrtime() - milestones[TS_MILESTONE_SERVER_FIRST_CONNECT];
      if (wait < 0) {
        wait = 0;
      }
      if (ink_hrtime_to_sec(wait) > t_state.txn_conf->client_abort_threshold) {
        t_state.current.server->set_connect_fail(ETIMEDOUT);
        do_hostdb_update_if_necessary();
      }
    }
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
  if (server_session == nullptr) {
    return;
  }

  if (TS_SERVER_SESSION_SHARING_MATCH_NONE != t_state.txn_conf->server_session_sharing_match && t_state.current.server != nullptr &&
      t_state.current.server->keep_alive == HTTP_KEEPALIVE && t_state.hdr_info.server_response.valid() &&
      t_state.hdr_info.server_request.valid() &&
      (t_state.hdr_info.server_response.status_get() == HTTP_STATUS_NOT_MODIFIED ||
       (t_state.hdr_info.server_request.method_get_wksidx() == HTTP_WKSIDX_HEAD &&
        t_state.www_auth_content != HttpTransact::CACHE_AUTH_NONE)) &&
      plugin_tunnel_type == HTTP_NO_PLUGIN_TUNNEL) {
    HTTP_DECREMENT_DYN_STAT(http_current_server_transactions_stat);
    server_session->server_trans_stat--;
    server_session->attach_hostname(t_state.current.server->name);
    if (t_state.www_auth_content == HttpTransact::CACHE_AUTH_NONE || serve_from_cache == false) {
      // Must explicitly set the keep_alive_no_activity time before doing the release
      server_session->get_netvc()->set_inactivity_timeout(HRTIME_SECONDS(t_state.txn_conf->keep_alive_no_activity_timeout_out));
      server_session->release();
    } else {
      // an authenticated server connection - attach to the local client
      // we are serving from cache for the current transaction
      t_state.www_auth_content = HttpTransact::CACHE_AUTH_SERVE;
      ua_txn->attach_server_session(server_session, false);
    }
  } else {
    server_session->do_io_close();
  }

  ink_assert(server_entry->vc == server_session);
  server_entry->in_tunnel = true;
  vc_table.cleanup_entry(server_entry);
  server_entry   = nullptr;
  server_session = nullptr;
}

// void HttpSM::handle_post_failure()
//
//   We failed in our attempt post (or put) a document
//    to the server.  Two cases happen here.  The normal
//    one is the server died, in which case we ought to
//    return an error to the client.  The second one is
//    stupid.  The server returned a response without reading
//    all the post data.  In order to be as transparent as
//    possible process the server's response
void
HttpSM::handle_post_failure()
{
  STATE_ENTER(&HttpSM::handle_post_failure, VC_EVENT_NONE);

  ink_assert(ua_entry->vc == ua_txn);
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
    ua_entry->read_vio = ua_txn->do_io_read(this, 0, nullptr);
  }
  ua_entry->in_tunnel     = false;
  server_entry->in_tunnel = false;

  // disable redirection in case we got a partial response and then EOS, because the buffer might not
  // have the full post and it's deallocating the post buffers here
  this->disable_redirect();

  // Don't even think about doing keep-alive after this debacle
  t_state.client_info.keep_alive     = HTTP_NO_KEEPALIVE;
  t_state.current.server->keep_alive = HTTP_NO_KEEPALIVE;

  if (server_buffer_reader->read_avail() > 0) {
    tunnel.deallocate_buffers();
    tunnel.reset();
    // There's data from the server so try to read the header
    setup_server_read_response_header();
  } else {
    tunnel.deallocate_buffers();
    tunnel.reset();
    // Server died
    t_state.current.state = HttpTransact::CONNECTION_CLOSED;
    call_transact_and_set_next_state(HttpTransact::HandleResponse);
  }
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
  // The request is now not queued. This is important because server retries reuse the t_state.
  t_state.outbound_conn_track_state.dequeue();

  // [bwyatt] applying per-transaction OS netVC options here
  //          IFF they differ from the netVC's current options.
  //          This should keep this from being redundant on a
  //          server session's first transaction.
  if (nullptr != server_session) {
    NetVConnection *vc = server_session->get_netvc();
    if (vc != nullptr && (vc->options.sockopt_flags != t_state.txn_conf->sock_option_flag_out ||
                          vc->options.packet_mark != t_state.txn_conf->sock_packet_mark_out ||
                          vc->options.packet_tos != t_state.txn_conf->sock_packet_tos_out)) {
      vc->options.sockopt_flags = t_state.txn_conf->sock_option_flag_out;
      vc->options.packet_mark   = t_state.txn_conf->sock_packet_mark_out;
      vc->options.packet_tos    = t_state.txn_conf->sock_packet_tos_out;
      vc->apply_options();
    }
  }

  int method = t_state.hdr_info.server_request.method_get_wksidx();
  if (method != HTTP_WKSIDX_TRACE &&
      (t_state.hdr_info.request_content_length > 0 || t_state.client_info.transfer_encoding == HttpTransact::CHUNKED_ENCODING) &&
      do_post_transform_open()) {
    do_setup_post_tunnel(HTTP_TRANSFORM_VC);
  } else if (server_session != nullptr) {
    setup_server_send_request_api();
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
  VIO *vio = (VIO *)data;
  ink_assert(vio != nullptr);

  STATE_ENTER(&HttpSM::handle_server_setup_error, event);

  // If there is POST or PUT tunnel wait for the tunnel
  //  to figure out that things have gone to hell

  if (tunnel.is_tunnel_active()) {
    ink_assert(server_entry->read_vio == data || server_entry->write_vio == data);
    SMDebug("http",
            "[%" PRId64 "] [handle_server_setup_error] "
            "forwarding event %s to post tunnel",
            sm_id, HttpDebugNames::get_event_name(event));
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
        ink_assert(ua_entry->vc == ua_producer->vc);

        ua_entry->vc_handler = &HttpSM::state_watch_for_client_abort;
        ua_entry->read_vio   = ua_producer->vc->do_io_read(this, INT64_MAX, c->producer->read_buffer);
        ua_producer->vc->do_io_shutdown(IO_SHUTDOWN_READ);

        ua_producer->alive         = false;
        ua_producer->handler_state = HTTP_SM_POST_SERVER_FAIL;
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

  switch (event) {
  case VC_EVENT_EOS:
    t_state.current.state = HttpTransact::CONNECTION_CLOSED;
    break;
  case VC_EVENT_ERROR:
    t_state.current.state        = HttpTransact::CONNECTION_ERROR;
    t_state.cause_of_death_errno = server_session->get_netvc()->lerrno;
    break;
  case VC_EVENT_ACTIVE_TIMEOUT:
    t_state.current.state = HttpTransact::ACTIVE_TIMEOUT;
    break;

  case VC_EVENT_INACTIVITY_TIMEOUT:
    // If we're writing the request and get an inactivity timeout
    //   before any bytes are written, the connection to the
    //   server failed
    // In case of TIMEOUT, the iocore sends back
    // server_entry->read_vio instead of the write_vio
    if (server_entry->write_vio && server_entry->write_vio->nbytes > 0 && server_entry->write_vio->ndone == 0) {
      t_state.current.state = HttpTransact::CONNECTION_ERROR;
    } else {
      t_state.current.state = HttpTransact::INACTIVE_TIMEOUT;
    }
    break;
  default:
    ink_release_assert(0);
  }

  if (event == VC_EVENT_INACTIVITY_TIMEOUT || event == VC_EVENT_ERROR) {
    // Clean up the vc_table entry so any events in play to the timed out server vio
    // don't get handled.  The connection isn't there.
    if (server_entry) {
      ink_assert(server_entry->vc_type == HTTP_SERVER_VC);
      vc_table.cleanup_entry(server_entry);
      server_entry   = nullptr;
      server_session = nullptr;
    }
  }

  // Closedown server connection and deallocate buffers
  ink_assert(!server_entry || server_entry->in_tunnel == false);

  // if we are waiting on a plugin callout for
  //   HTTP_API_SEND_REQUEST_HDR defer calling transact until
  //   after we've finished processing the plugin callout
  switch (callout_state) {
  case HTTP_API_NO_CALLOUT:
    // Normal fast path case, no api callouts in progress
    break;
  case HTTP_API_IN_CALLOUT:
  case HTTP_API_DEFERED_SERVER_ERROR:
    // Callout in progress note that we are in deferring
    //   the server error
    callout_state = HTTP_API_DEFERED_SERVER_ERROR;
    return;
  case HTTP_API_DEFERED_CLOSE:
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

  int64_t nbytes            = t_state.hdr_info.transform_request_cl;
  int64_t alloc_index       = buffer_size_to_index(nbytes);
  MIOBuffer *post_buffer    = new_MIOBuffer(alloc_index);
  IOBufferReader *buf_start = post_buffer->alloc_reader();

  HTTP_SM_SET_DEFAULT_HANDLER(&HttpSM::tunnel_handler_post);

  HttpTunnelConsumer *c = tunnel.get_consumer(post_transform_info.vc);

  HttpTunnelProducer *p = tunnel.add_producer(post_transform_info.vc, nbytes, buf_start, &HttpSM::tunnel_handler_transform_read,
                                              HT_TRANSFORM, "post transform");
  tunnel.chain(c, p);
  post_transform_info.entry->in_tunnel = true;

  tunnel.add_consumer(server_entry->vc, post_transform_info.vc, &HttpSM::tunnel_handler_post_server, HT_HTTP_SERVER,
                      "http server post");
  server_entry->in_tunnel = true;

  tunnel.tunnel_run(p);
}

void
HttpSM::do_drain_request_body(HTTPHdr &response)
{
  int64_t content_length = t_state.hdr_info.client_request.get_content_length();
  int64_t avail          = ua_buffer_reader->read_avail();

  if (t_state.client_info.transfer_encoding == HttpTransact::CHUNKED_ENCODING) {
    SMDebug("http", "Chunked body, setting the response to non-keepalive");
    goto close_connection;
  }

  if (content_length > 0) {
    if (avail >= content_length) {
      SMDebug("http", "entire body is in the buffer, consuming");
      int64_t act_on            = (avail < content_length) ? avail : content_length;
      client_request_body_bytes = act_on;
      ua_buffer_reader->consume(act_on);
    } else {
      SMDebug("http", "entire body is not in the buffer, setting the response to non-keepalive");
      goto close_connection;
    }
  }
  return;

close_connection:
  t_state.client_info.keep_alive = HTTP_NO_KEEPALIVE;
  response.value_set(MIME_FIELD_CONNECTION, MIME_LEN_CONNECTION, "close", 5);
}

void
HttpSM::do_setup_post_tunnel(HttpVC_t to_vc_type)
{
  bool chunked       = (t_state.client_info.transfer_encoding == HttpTransact::CHUNKED_ENCODING);
  bool post_redirect = false;

  HttpTunnelProducer *p = nullptr;
  // YTS Team, yamsat Plugin
  // if redirect_in_process and redirection is enabled add static producer

  if (is_using_post_buffer ||
      (t_state.redirect_info.redirect_in_process && enable_redirection && this->_postbuf.postdata_copy_buffer_start != nullptr)) {
    post_redirect = true;
    // copy the post data into a new producer buffer for static producer
    MIOBuffer *postdata_producer_buffer      = new_empty_MIOBuffer();
    IOBufferReader *postdata_producer_reader = postdata_producer_buffer->alloc_reader();

    postdata_producer_buffer->write(this->_postbuf.postdata_copy_buffer_start);
    int64_t post_bytes = postdata_producer_reader->read_avail();
    transfered_bytes   = post_bytes;
    p = tunnel.add_producer(HTTP_TUNNEL_STATIC_PRODUCER, post_bytes, postdata_producer_reader, (HttpProducerHandler) nullptr,
                            HT_STATIC, "redirect static agent post");
  } else {
    int64_t alloc_index;
    // content length is undefined, use default buffer size
    if (t_state.hdr_info.request_content_length == HTTP_UNDEFINED_CL) {
      alloc_index = (int)t_state.txn_conf->default_buffer_size_index;
      if (alloc_index < MIN_CONFIG_BUFFER_SIZE_INDEX || alloc_index > MAX_BUFFER_SIZE_INDEX) {
        alloc_index = DEFAULT_REQUEST_BUFFER_SIZE_INDEX;
      }
    } else {
      alloc_index = buffer_size_to_index(t_state.hdr_info.request_content_length);
    }
    MIOBuffer *post_buffer    = new_MIOBuffer(alloc_index);
    IOBufferReader *buf_start = post_buffer->alloc_reader();
    int64_t post_bytes        = chunked ? INT64_MAX : t_state.hdr_info.request_content_length;

    if (enable_redirection) {
      this->_postbuf.init(post_buffer->clone_reader(buf_start));
    }

    // Note: Many browsers, Netscape and IE included send two extra
    //  bytes (CRLF) at the end of the post.  We just ignore those
    //  bytes since the sending them is not spec

    // Next order of business if copy the remaining data from the
    //  header buffer into new buffer
    client_request_body_bytes = post_buffer->write(ua_buffer_reader, chunked ? ua_buffer_reader->read_avail() : post_bytes);

    ua_buffer_reader->consume(client_request_body_bytes);
    p = tunnel.add_producer(ua_entry->vc, post_bytes - transfered_bytes, buf_start, &HttpSM::tunnel_handler_post_ua, HT_HTTP_CLIENT,
                            "user agent post");
  }
  ua_entry->in_tunnel = true;

  switch (to_vc_type) {
  case HTTP_TRANSFORM_VC:
    HTTP_SM_SET_DEFAULT_HANDLER(&HttpSM::state_request_wait_for_transform_read);
    ink_assert(post_transform_info.entry != nullptr);
    ink_assert(post_transform_info.entry->vc == post_transform_info.vc);
    tunnel.add_consumer(post_transform_info.entry->vc, ua_entry->vc, &HttpSM::tunnel_handler_transform_write, HT_TRANSFORM,
                        "post transform");
    post_transform_info.entry->in_tunnel = true;
    break;
  case HTTP_SERVER_VC:
    // YTS Team, yamsat Plugin
    // When redirect in process is true and redirection is enabled
    // add http server as the consumer
    if (post_redirect) {
      HTTP_SM_SET_DEFAULT_HANDLER(&HttpSM::tunnel_handler_for_partial_post);
      tunnel.add_consumer(server_entry->vc, HTTP_TUNNEL_STATIC_PRODUCER, &HttpSM::tunnel_handler_post_server, HT_HTTP_SERVER,
                          "redirect http server post");
    } else {
      HTTP_SM_SET_DEFAULT_HANDLER(&HttpSM::tunnel_handler_post);
      tunnel.add_consumer(server_entry->vc, ua_entry->vc, &HttpSM::tunnel_handler_post_server, HT_HTTP_SERVER, "http server post");
    }
    server_entry->in_tunnel = true;
    break;
  default:
    ink_release_assert(0);
    break;
  }

  // The user agent may support chunked (HTTP/1.1) or not (HTTP/2)
  // In either case, the server will support chunked (HTTP/1.1)
  if (chunked) {
    if (ua_txn->is_chunked_encoding_supported()) {
      tunnel.set_producer_chunking_action(p, 0, TCA_PASSTHRU_CHUNKED_CONTENT);
    } else {
      tunnel.set_producer_chunking_action(p, 0, TCA_CHUNK_CONTENT);
      tunnel.set_producer_chunking_size(p, 0);
    }
  }

  ua_txn->set_inactivity_timeout(HRTIME_SECONDS(t_state.txn_conf->transaction_no_activity_timeout_in));
  server_session->get_netvc()->set_inactivity_timeout(HRTIME_SECONDS(t_state.txn_conf->transaction_no_activity_timeout_out));

  tunnel.tunnel_run(p);

  // If we're half closed, we got a FIN from the client. Forward it on to the origin server
  // now that we have the tunnel operational.
  // HttpTunnel could broken due to bad chunked data and close all vc by chain_abort_all().
  if (p->handler_state != HTTP_SM_POST_UA_FAIL && ua_txn->get_half_close_flag()) {
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
  SMDebug("http", "[%" PRId64 "] perform_transform_cache_write_action %s", sm_id,
          HttpDebugNames::get_cache_action_name(t_state.cache_info.action));

  if (t_state.range_setup) {
    return;
  }

  switch (t_state.cache_info.transform_action) {
  case HttpTransact::CACHE_DO_NO_ACTION: {
    // Nothing to do
    transform_cache_sm.end_both();
    break;
  }

  case HttpTransact::CACHE_DO_WRITE: {
    transform_cache_sm.close_read();
    t_state.cache_info.transform_write_status = HttpTransact::CACHE_WRITE_IN_PROGRESS;
    setup_cache_write_transfer(&transform_cache_sm, transform_info.entry->vc, &t_state.cache_info.transform_store,
                               client_response_hdr_bytes, "cache write t");
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
  SMDebug("http", "[%" PRId64 "] perform_cache_write_action %s", sm_id,
          HttpDebugNames::get_cache_action_name(t_state.cache_info.action));

  switch (t_state.cache_info.action) {
  case HttpTransact::CACHE_DO_NO_ACTION:

  {
    // Nothing to do
    cache_sm.end_both();
    break;
  }

  case HttpTransact::CACHE_DO_SERVE: {
    cache_sm.abort_write();
    break;
  }

  case HttpTransact::CACHE_DO_DELETE: {
    // Write close deletes the old alternate
    cache_sm.close_write();
    cache_sm.close_read();
    break;
  }

  case HttpTransact::CACHE_DO_SERVE_AND_DELETE: {
    // FIX ME: need to set up delete for after cache write has
    //   completed
    break;
  }

  case HttpTransact::CACHE_DO_SERVE_AND_UPDATE: {
    issue_cache_update();
    break;
  }

  case HttpTransact::CACHE_DO_UPDATE: {
    cache_sm.close_read();
    issue_cache_update();
    break;
  }

  case HttpTransact::CACHE_DO_WRITE:
  case HttpTransact::CACHE_DO_REPLACE:
    // Fix need to set up delete for after cache write has
    //   completed
    if (transform_info.entry == nullptr || t_state.api_info.cache_untransformed == true) {
      cache_sm.close_read();
      t_state.cache_info.write_status = HttpTransact::CACHE_WRITE_IN_PROGRESS;
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
}

int
HttpSM::write_header_into_buffer(HTTPHdr *h, MIOBuffer *b)
{
  int dumpoffset;
  int done;

  dumpoffset = 0;
  do {
    IOBufferBlock *block = b->get_current_block();
    int bufindex         = 0;
    int tmp              = dumpoffset;

    ink_assert(block->write_avail() > 0);
    done = h->print(block->start(), block->write_avail(), &bufindex, &tmp);
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
HttpSM::attach_server_session(Http1ServerSession *s)
{
  hsm_release_assert(server_session == nullptr);
  hsm_release_assert(server_entry == nullptr);
  hsm_release_assert(s->state == HSS_ACTIVE);
  server_session        = s;
  server_transact_count = server_session->transact_count++;
  // Propagate the per client IP debugging
  if (ua_txn) {
    s->get_netvc()->control_flags.set_flags(get_cont_flags().get_flags());
  } else { // If there is no ua_txn no sense in continuing to attach the server session
    return;
  }

  // Set the mutex so that we have something to update
  //   stats with
  server_session->mutex = this->mutex;

  HTTP_INCREMENT_DYN_STAT(http_current_server_transactions_stat);
  ++s->server_trans_stat;

  // Record the VC in our table
  server_entry             = vc_table.new_entry();
  server_entry->vc         = server_session;
  server_entry->vc_type    = HTTP_SERVER_VC;
  server_entry->vc_handler = &HttpSM::state_send_server_request_header;

  // es - is this a concern here in HttpSM?  Does it belong somewhere else?
  // Get server and client connections
  UnixNetVConnection *server_vc = dynamic_cast<UnixNetVConnection *>(server_session->get_netvc());
  UnixNetVConnection *client_vc = (UnixNetVConnection *)(ua_txn->get_netvc());

  // Verifying that the user agent and server sessions/transactions are operating on the same thread.
  ink_release_assert(!server_vc || !client_vc || server_vc->thread == client_vc->thread);

  // set flag for server session is SSL
  SSLNetVConnection *server_ssl_vc = dynamic_cast<SSLNetVConnection *>(server_vc);
  if (server_ssl_vc) {
    server_connection_is_ssl = true;
  }

  // Initiate a read on the session so that the SM and not
  //  session manager will get called back if the timeout occurs
  //  or the server closes on us.  The IO Core now requires us to
  //  do the read with a buffer and a size so preallocate the
  //  buffer
  server_buffer_reader = server_session->get_reader();
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
  //
  server_entry->read_vio = server_session->do_io_read(this, 0, server_session->read_buffer);

  // Transfer control of the write side as well
  server_entry->write_vio = server_session->do_io_write(this, 0, nullptr);

  // Setup the timeouts
  // Set the inactivity timeout to the connect timeout so that we
  //   we fail this server if it doesn't start sending the response
  //   header
  MgmtInt connect_timeout;

  if (t_state.method == HTTP_WKSIDX_POST || t_state.method == HTTP_WKSIDX_PUT) {
    connect_timeout = t_state.txn_conf->post_connect_attempts_timeout;
  } else if (t_state.current.server == &t_state.parent_info) {
    connect_timeout = t_state.txn_conf->parent_connect_timeout;
  } else {
    connect_timeout = t_state.txn_conf->connect_attempts_timeout;
  }

  if (t_state.api_txn_connect_timeout_value != -1) {
    server_session->get_netvc()->set_inactivity_timeout(HRTIME_MSECONDS(t_state.api_txn_connect_timeout_value));
  } else {
    server_session->get_netvc()->set_inactivity_timeout(HRTIME_SECONDS(connect_timeout));
  }

  if (t_state.api_txn_active_timeout_value != -1) {
    server_session->get_netvc()->set_active_timeout(HRTIME_MSECONDS(t_state.api_txn_active_timeout_value));
  } else {
    server_session->get_netvc()->set_active_timeout(HRTIME_SECONDS(t_state.txn_conf->transaction_active_timeout_out));
  }

  if (plugin_tunnel_type != HTTP_NO_PLUGIN_TUNNEL || will_be_private_ss) {
    SMDebug("http_ss", "Setting server session to private");
    set_server_session_private(true);
  }
}

void
HttpSM::setup_server_send_request_api()
{
  // Make sure the VC is on the correct timeout
  server_session->get_netvc()->set_inactivity_timeout(HRTIME_SECONDS(t_state.txn_conf->transaction_no_activity_timeout_out));
  t_state.api_next_action = HttpTransact::SM_ACTION_API_SEND_REQUEST_HDR;
  do_api_callout();
}

void
HttpSM::setup_server_send_request()
{
  int hdr_length;
  int64_t msg_len = 0; /* lv: just make gcc happy */

  hsm_release_assert(server_entry != nullptr);
  hsm_release_assert(server_session != nullptr);
  hsm_release_assert(server_entry->vc == server_session);

  // Send the request header
  server_entry->vc_handler   = &HttpSM::state_send_server_request_header;
  server_entry->write_buffer = new_MIOBuffer(HTTP_HEADER_BUFFER_SIZE_INDEX);

  if (t_state.api_server_request_body_set) {
    msg_len = t_state.internal_msg_buffer_size;
    t_state.hdr_info.server_request.value_set_int64(MIME_FIELD_CONTENT_LENGTH, MIME_LEN_CONTENT_LENGTH, msg_len);
  }

  DUMP_HEADER("http_hdrs", &(t_state.hdr_info.server_request), t_state.state_machine_id, "Proxy's Request after hooks");

  // We need a reader so bytes don't fall off the end of
  //  the buffer
  IOBufferReader *buf_start = server_entry->write_buffer->alloc_reader();
  server_request_hdr_bytes = hdr_length = write_header_into_buffer(&t_state.hdr_info.server_request, server_entry->write_buffer);

  // the plugin decided to append a message to the request
  if (t_state.api_server_request_body_set) {
    SMDebug("http", "[%" PRId64 "] appending msg of %" PRId64 " bytes to request %s", sm_id, msg_len, t_state.internal_msg_buffer);
    hdr_length += server_entry->write_buffer->write(t_state.internal_msg_buffer, msg_len);
    server_request_body_bytes = msg_len;
  }

  milestones[TS_MILESTONE_SERVER_BEGIN_WRITE] = Thread::get_hrtime();
  server_entry->write_vio                     = server_entry->vc->do_io_write(this, hdr_length, buf_start);
}

void
HttpSM::setup_server_read_response_header()
{
  ink_assert(server_session != nullptr);
  ink_assert(server_entry != nullptr);
  // REQ_FLAVOR_SCHEDULED_UPDATE can be transformed in REQ_FLAVOR_REVPROXY
  ink_assert(ua_txn != nullptr || t_state.req_flavor == HttpTransact::REQ_FLAVOR_SCHEDULED_UPDATE ||
             t_state.req_flavor == HttpTransact::REQ_FLAVOR_REVPROXY);

  // We should have set the server_buffer_reader
  //   we sent the request header
  ink_assert(server_buffer_reader != nullptr);

  // Now that we've got the ability to read from the
  //  server, setup to read the response header
  server_entry->vc_handler = &HttpSM::state_read_server_response_header;

  t_state.current.state         = HttpTransact::STATE_UNDEFINED;
  t_state.current.server->state = HttpTransact::STATE_UNDEFINED;

  // Note: we must use destroy() here since clear()
  //  does not free the memory from the header
  t_state.hdr_info.server_response.destroy();
  t_state.hdr_info.server_response.create(HTTP_TYPE_RESPONSE);
  http_parser_clear(&http_parser);
  server_response_hdr_bytes                        = 0;
  milestones[TS_MILESTONE_SERVER_READ_HEADER_DONE] = 0;

  // We already done the READ when we setup the connection to
  //   read the request header
  ink_assert(server_entry->read_vio);

  // The tunnel from OS to UA is now setup.  Ready to read the response
  server_entry->read_vio = server_session->do_io_read(this, INT64_MAX, server_buffer_reader->mbuf);

  // If there is anything in the buffer call the parsing routines
  //  since if the response is finished, we won't get any
  //  additional callbacks

  if (server_buffer_reader->read_avail() > 0) {
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
  alloc_index = buffer_size_to_index(doc_size + index_to_buffer_size(HTTP_HEADER_BUFFER_SIZE_INDEX));

#ifndef USE_NEW_EMPTY_MIOBUFFER
  MIOBuffer *buf = new_MIOBuffer(alloc_index);
#else
  MIOBuffer *buf = new_empty_MIOBuffer(alloc_index);
  buf->append_block(HTTP_HEADER_BUFFER_SIZE_INDEX);
#endif

  buf->water_mark = (int)t_state.txn_conf->default_buffer_water_mark;

  IOBufferReader *buf_start = buf->alloc_reader();

  // Now dump the header into the buffer
  ink_assert(t_state.hdr_info.client_response.status_get() != HTTP_STATUS_NOT_MODIFIED);
  client_response_hdr_bytes = hdr_size = write_response_header_into_buffer(&t_state.hdr_info.client_response, buf);
  cache_response_hdr_bytes             = client_response_hdr_bytes;

  HTTP_SM_SET_DEFAULT_HANDLER(&HttpSM::tunnel_handler);

  if (doc_size != INT64_MAX) {
    doc_size += hdr_size;
  }

  HttpTunnelProducer *p = tunnel.add_producer(cache_sm.cache_read_vc, doc_size, buf_start, &HttpSM::tunnel_handler_cache_read,
                                              HT_CACHE_READ, "cache read");
  tunnel.add_consumer(ua_entry->vc, cache_sm.cache_read_vc, &HttpSM::tunnel_handler_ua, HT_HTTP_CLIENT, "user agent");
  // if size of a cached item is not known, we'll do chunking for keep-alive HTTP/1.1 clients
  // this only applies to read-while-write cases where origin server sends a dynamically generated chunked content
  // w/o providing a Content-Length header
  if (t_state.client_info.receive_chunked_response) {
    tunnel.set_producer_chunking_action(p, client_response_hdr_bytes, TCA_CHUNK_CONTENT);
    tunnel.set_producer_chunking_size(p, t_state.txn_conf->http_chunking_size);
  }
  ua_entry->in_tunnel    = true;
  cache_sm.cache_read_vc = nullptr;
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
  alloc_index               = buffer_size_to_index(doc_size);
  MIOBuffer *buf            = new_MIOBuffer(alloc_index);
  IOBufferReader *buf_start = buf->alloc_reader();

  HTTP_SM_SET_DEFAULT_HANDLER(&HttpSM::state_response_wait_for_transform_read);

  HttpTunnelProducer *p = tunnel.add_producer(cache_sm.cache_read_vc, doc_size, buf_start, &HttpSM::tunnel_handler_cache_read,
                                              HT_CACHE_READ, "cache read");

  tunnel.add_consumer(transform_info.vc, cache_sm.cache_read_vc, &HttpSM::tunnel_handler_transform_write, HT_TRANSFORM,
                      "transform write");
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

  tunnel.add_consumer(c_sm->cache_write_vc, source_vc, &HttpSM::tunnel_handler_cache_write, HT_CACHE_WRITE, name, skip_bytes);

  c_sm->cache_write_vc = nullptr;
}

void
HttpSM::setup_100_continue_transfer()
{
  MIOBuffer *buf            = new_MIOBuffer(HTTP_HEADER_BUFFER_SIZE_INDEX);
  IOBufferReader *buf_start = buf->alloc_reader();

  // First write the client response header into the buffer
  ink_assert(t_state.client_info.http_version != HTTPVersion(0, 9));
  client_response_hdr_bytes = write_header_into_buffer(&t_state.hdr_info.client_response, buf);
  ink_assert(client_response_hdr_bytes > 0);

  HTTP_SM_SET_DEFAULT_HANDLER(&HttpSM::tunnel_handler_100_continue);

  // Clear the decks before we set up new producers.  As things stand, we cannot have two static operators
  // at once
  tunnel.reset();

  // Setup the tunnel to the client
  HttpTunnelProducer *p = tunnel.add_producer(HTTP_TUNNEL_STATIC_PRODUCER, client_response_hdr_bytes, buf_start,
                                              (HttpProducerHandler) nullptr, HT_STATIC, "internal msg - 100 continue");
  tunnel.add_consumer(ua_entry->vc, HTTP_TUNNEL_STATIC_PRODUCER, &HttpSM::tunnel_handler_100_continue_ua, HT_HTTP_CLIENT,
                      "user agent");

  // Make sure the half_close is not set.
  ua_txn->set_half_close_flag(false);
  ua_entry->in_tunnel = true;
  tunnel.tunnel_run(p);
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
  if (t_state.internal_msg_buffer || is_response_body_precluded(t_state.http_return_code)) {
    // Since we need to send the error message, call the API
    //   function
    ink_assert(t_state.internal_msg_buffer_size > 0 || is_response_body_precluded(t_state.http_return_code));
    t_state.api_next_action = HttpTransact::SM_ACTION_API_SEND_RESPONSE_HDR;
    do_api_callout();
  } else {
    SMDebug("http", "[setup_error_transfer] Now closing connection ...");
    vc_table.cleanup_entry(ua_entry);
    ua_entry = nullptr;
    // ua_txn     = NULL;
    terminate_sm   = true;
    t_state.source = HttpTransact::SOURCE_INTERNAL;
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
    t_state.hdr_info.client_response.field_delete(MIME_FIELD_TRANSFER_ENCODING, MIME_LEN_TRANSFER_ENCODING);

    // set internal_msg_buffer_type if available
    if (t_state.internal_msg_buffer_type) {
      int len = strlen(t_state.internal_msg_buffer_type);

      if (len > 0) {
        t_state.hdr_info.client_response.value_set(MIME_FIELD_CONTENT_TYPE, MIME_LEN_CONTENT_TYPE, t_state.internal_msg_buffer_type,
                                                   len);
      }
      ats_free(t_state.internal_msg_buffer_type);
      t_state.internal_msg_buffer_type = nullptr;
    } else {
      t_state.hdr_info.client_response.value_set(MIME_FIELD_CONTENT_TYPE, MIME_LEN_CONTENT_TYPE, "text/html", 9);
    }
  } else {
    is_msg_buf_present = false;

    // If we are sending a response that can have a body
    //   but doesn't have a body add a content-length of zero.
    //   Needed for keep-alive on PURGE requests
    if (!is_response_body_precluded(t_state.hdr_info.client_response.status_get(), t_state.method)) {
      t_state.hdr_info.client_response.set_content_length(0);
      t_state.hdr_info.client_response.field_delete(MIME_FIELD_TRANSFER_ENCODING, MIME_LEN_TRANSFER_ENCODING);
    }
  }

  t_state.source = HttpTransact::SOURCE_INTERNAL;

  int64_t buf_size =
    index_to_buffer_size(HTTP_HEADER_BUFFER_SIZE_INDEX) + (is_msg_buf_present ? t_state.internal_msg_buffer_size : 0);

  MIOBuffer *buf            = new_MIOBuffer(buffer_size_to_index(buf_size));
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

  // Clear the decks before we setup the new producers
  // As things stand, we cannot have two static producers operating at
  // once
  tunnel.reset();

  // Setup the tunnel to the client
  HttpTunnelProducer *p =
    tunnel.add_producer(HTTP_TUNNEL_STATIC_PRODUCER, nbytes, buf_start, (HttpProducerHandler) nullptr, HT_STATIC, "internal msg");
  tunnel.add_consumer(ua_entry->vc, HTTP_TUNNEL_STATIC_PRODUCER, &HttpSM::tunnel_handler_ua, HT_HTTP_CLIENT, "user agent");

  ua_entry->in_tunnel = true;
  tunnel.tunnel_run(p);
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
    alloc_index = (int)t_state.txn_conf->default_buffer_size_index;
    if (alloc_index < MIN_CONFIG_BUFFER_SIZE_INDEX || alloc_index > DEFAULT_MAX_BUFFER_SIZE) {
      alloc_index = DEFAULT_RESPONSE_BUFFER_SIZE_INDEX;
    }
  } else {
    int64_t buf_size;

#ifdef WRITE_AND_TRANSFER
    buf_size = HTTP_HEADER_BUFFER_SIZE + content_length - index_to_buffer_size(HTTP_SERVER_RESP_HDR_BUFFER_INDEX);
#else
    buf_size = index_to_buffer_size(HTTP_HEADER_BUFFER_SIZE_INDEX) + content_length;
#endif
    alloc_index = buffer_size_to_index(buf_size);
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
    nbytes = server_buffer_reader->read_avail() + hdr_size;
  } else if (t_state.hdr_info.response_content_length == HTTP_UNDEFINED_CL) {
    nbytes = -1;
  } else {
    //  Set to copy to the number of bytes we want to write as
    //  if the server is sending us a bogus response we have to
    //  truncate it as we've already decided to trust the content
    //  length
    to_copy = t_state.hdr_info.response_content_length;
    nbytes  = t_state.hdr_info.response_content_length + hdr_size;
  }

  // Next order of business if copy the remaining data from the
  //  header buffer into new buffer.

  int64_t server_response_pre_read_bytes =
#ifdef WRITE_AND_TRANSFER
    /* relinquish the space in server_buffer and let
       the tunnel use the trailing space
     */
    buf->write_and_transfer_left_over_space(server_buffer_reader, to_copy);
#else
    buf->write(server_buffer_reader, to_copy);
#endif
  server_buffer_reader->consume(server_response_pre_read_bytes);

  //  If we know the length & copied the entire body
  //   of the document out of the header buffer make
  //   sure the server isn't screwing us by having sent too
  //   much.  If it did, we want to close the server connection
  if (server_response_pre_read_bytes == to_copy && server_buffer_reader->read_avail() > 0) {
    t_state.current.server->keep_alive = HTTP_NO_KEEPALIVE;
  }

  return nbytes;
}

HttpTunnelProducer *
HttpSM::setup_server_transfer_to_transform()
{
  int64_t alloc_index;
  int64_t nbytes;

  alloc_index               = find_server_buffer_size();
  MIOBuffer *buf            = new_MIOBuffer(alloc_index);
  IOBufferReader *buf_start = buf->alloc_reader();
  nbytes                    = server_transfer_init(buf, 0);

  HTTP_SM_SET_DEFAULT_HANDLER(&HttpSM::state_response_wait_for_transform_read);

  HttpTunnelProducer *p =
    tunnel.add_producer(server_entry->vc, nbytes, buf_start, &HttpSM::tunnel_handler_server, HT_HTTP_SERVER, "http server");

  tunnel.add_consumer(transform_info.vc, server_entry->vc, &HttpSM::tunnel_handler_transform_write, HT_TRANSFORM,
                      "transform write");

  server_entry->in_tunnel         = true;
  transform_info.entry->in_tunnel = true;

  if (t_state.current.server->transfer_encoding == HttpTransact::CHUNKED_ENCODING) {
    client_response_hdr_bytes = 0; // fixed by YTS Team, yamsat
    tunnel.set_producer_chunking_action(p, client_response_hdr_bytes, TCA_DECHUNK_CONTENT);
  }

  return p;
}

HttpTunnelProducer *
HttpSM::setup_transfer_from_transform()
{
  int64_t alloc_index = find_server_buffer_size();

  // TODO change this call to new_empty_MIOBuffer()
  MIOBuffer *buf            = new_MIOBuffer(alloc_index);
  buf->water_mark           = (int)t_state.txn_conf->default_buffer_water_mark;
  IOBufferReader *buf_start = buf->alloc_reader();

  HttpTunnelConsumer *c = tunnel.get_consumer(transform_info.vc);
  ink_assert(c != nullptr);
  ink_assert(c->vc == transform_info.vc);
  ink_assert(c->vc_type == HT_TRANSFORM);

  // Now dump the header into the buffer
  ink_assert(t_state.hdr_info.client_response.status_get() != HTTP_STATUS_NOT_MODIFIED);
  client_response_hdr_bytes = write_response_header_into_buffer(&t_state.hdr_info.client_response, buf);

  HTTP_SM_SET_DEFAULT_HANDLER(&HttpSM::tunnel_handler);

  HttpTunnelProducer *p = tunnel.add_producer(transform_info.vc, INT64_MAX, buf_start, &HttpSM::tunnel_handler_transform_read,
                                              HT_TRANSFORM, "transform read");
  tunnel.chain(c, p);

  tunnel.add_consumer(ua_entry->vc, transform_info.vc, &HttpSM::tunnel_handler_ua, HT_HTTP_CLIENT, "user agent");

  transform_info.entry->in_tunnel = true;
  ua_entry->in_tunnel             = true;

  this->setup_plugin_agents(p);

  if (t_state.client_info.receive_chunked_response) {
    tunnel.set_producer_chunking_action(p, client_response_hdr_bytes, TCA_CHUNK_CONTENT);
    tunnel.set_producer_chunking_size(p, t_state.txn_conf->http_chunking_size);
  }

  return p;
}

HttpTunnelProducer *
HttpSM::setup_transfer_from_transform_to_cache_only()
{
  int64_t alloc_index       = find_server_buffer_size();
  MIOBuffer *buf            = new_MIOBuffer(alloc_index);
  IOBufferReader *buf_start = buf->alloc_reader();

  HttpTunnelConsumer *c = tunnel.get_consumer(transform_info.vc);
  ink_assert(c != nullptr);
  ink_assert(c->vc == transform_info.vc);
  ink_assert(c->vc_type == HT_TRANSFORM);

  HTTP_SM_SET_DEFAULT_HANDLER(&HttpSM::tunnel_handler);

  HttpTunnelProducer *p = tunnel.add_producer(transform_info.vc, INT64_MAX, buf_start, &HttpSM::tunnel_handler_transform_read,
                                              HT_TRANSFORM, "transform read");
  tunnel.chain(c, p);

  transform_info.entry->in_tunnel = true;

  ink_assert(t_state.cache_info.transform_action == HttpTransact::CACHE_DO_WRITE);

  perform_transform_cache_write_action();

  return p;
}

void
HttpSM::setup_server_transfer_to_cache_only()
{
  TunnelChunkingAction_t action;
  int64_t alloc_index;
  int64_t nbytes;

  alloc_index               = find_server_buffer_size();
  MIOBuffer *buf            = new_MIOBuffer(alloc_index);
  IOBufferReader *buf_start = buf->alloc_reader();

  action = (t_state.current.server && t_state.current.server->transfer_encoding == HttpTransact::CHUNKED_ENCODING) ?
             TCA_DECHUNK_CONTENT :
             TCA_PASSTHRU_DECHUNKED_CONTENT;

  nbytes = server_transfer_init(buf, 0);

  HTTP_SM_SET_DEFAULT_HANDLER(&HttpSM::tunnel_handler);

  HttpTunnelProducer *p =
    tunnel.add_producer(server_entry->vc, nbytes, buf_start, &HttpSM::tunnel_handler_server, HT_HTTP_SERVER, "http server");

  tunnel.set_producer_chunking_action(p, 0, action);
  tunnel.set_producer_chunking_size(p, t_state.txn_conf->http_chunking_size);

  setup_cache_write_transfer(&cache_sm, server_entry->vc, &t_state.cache_info.object_store, 0, "cache write");

  server_entry->in_tunnel = true;
}

HttpTunnelProducer *
HttpSM::setup_server_transfer()
{
  SMDebug("http", "Setup Server Transfer");
  int64_t alloc_index, hdr_size;
  int64_t nbytes;

  alloc_index = find_server_buffer_size();
#ifndef USE_NEW_EMPTY_MIOBUFFER
  MIOBuffer *buf = new_MIOBuffer(alloc_index);
#else
  MIOBuffer *buf = new_empty_MIOBuffer(alloc_index);
  buf->append_block(HTTP_HEADER_BUFFER_SIZE_INDEX);
#endif
  buf->water_mark           = (int)t_state.txn_conf->default_buffer_water_mark;
  IOBufferReader *buf_start = buf->alloc_reader();

  // we need to know if we are going to chunk the response or not
  // before we write the response header into buffer
  TunnelChunkingAction_t action;
  if (t_state.client_info.receive_chunked_response == false) {
    if (t_state.current.server->transfer_encoding == HttpTransact::CHUNKED_ENCODING) {
      action = TCA_DECHUNK_CONTENT;
    } else {
      action = TCA_PASSTHRU_DECHUNKED_CONTENT;
    }
  } else {
    if (t_state.current.server->transfer_encoding != HttpTransact::CHUNKED_ENCODING) {
      if (t_state.client_info.http_version == HTTPVersion(0, 9)) {
        action = TCA_PASSTHRU_DECHUNKED_CONTENT; // send as-is
      } else {
        action = TCA_CHUNK_CONTENT;
      }
    } else {
      action = TCA_PASSTHRU_CHUNKED_CONTENT;
    }
  }
  if (action == TCA_CHUNK_CONTENT || action == TCA_PASSTHRU_CHUNKED_CONTENT) { // remove Content-Length
    t_state.hdr_info.client_response.field_delete(MIME_FIELD_CONTENT_LENGTH, MIME_LEN_CONTENT_LENGTH);
  }
  // Now dump the header into the buffer
  ink_assert(t_state.hdr_info.client_response.status_get() != HTTP_STATUS_NOT_MODIFIED);
  client_response_hdr_bytes = hdr_size = write_response_header_into_buffer(&t_state.hdr_info.client_response, buf);

  nbytes = server_transfer_init(buf, hdr_size);

  if (t_state.negative_caching && t_state.hdr_info.server_response.status_get() == HTTP_STATUS_NO_CONTENT) {
    int s = sizeof("No Content") - 1;
    buf->write("No Content", s);
    nbytes += s;
  }

  HTTP_SM_SET_DEFAULT_HANDLER(&HttpSM::tunnel_handler);

  HttpTunnelProducer *p =
    tunnel.add_producer(server_entry->vc, nbytes, buf_start, &HttpSM::tunnel_handler_server, HT_HTTP_SERVER, "http server");

  tunnel.add_consumer(ua_entry->vc, server_entry->vc, &HttpSM::tunnel_handler_ua, HT_HTTP_CLIENT, "user agent");

  ua_entry->in_tunnel     = true;
  server_entry->in_tunnel = true;

  this->setup_plugin_agents(p);

  // If the incoming server response is chunked and the client does not
  // expect a chunked response, then dechunk it.  Otherwise, if the
  // incoming response is not chunked and the client expects a chunked
  // response, then chunk it.
  /*
     // this block is moved up so that we know if we need to remove
     // Content-Length field from response header before writing the
     // response header into buffer bz50730
     TunnelChunkingAction_t action;
     if (t_state.client_info.receive_chunked_response == false) {
     if (t_state.current.server->transfer_encoding ==
     HttpTransact::CHUNKED_ENCODING)
     action = TCA_DECHUNK_CONTENT;
     else action = TCA_PASSTHRU_DECHUNKED_CONTENT;
     }
     else {
     if (t_state.current.server->transfer_encoding !=
     HttpTransact::CHUNKED_ENCODING)
     action = TCA_CHUNK_CONTENT;
     else action = TCA_PASSTHRU_CHUNKED_CONTENT;
     }
   */
  tunnel.set_producer_chunking_action(p, client_response_hdr_bytes, action);
  tunnel.set_producer_chunking_size(p, t_state.txn_conf->http_chunking_size);
  return p;
}

HttpTunnelProducer *
HttpSM::setup_push_transfer_to_cache()
{
  int64_t nbytes, alloc_index;

  alloc_index               = find_http_resp_buffer_size(t_state.hdr_info.request_content_length);
  MIOBuffer *buf            = new_MIOBuffer(alloc_index);
  IOBufferReader *buf_start = buf->alloc_reader();

  ink_release_assert(t_state.hdr_info.request_content_length != HTTP_UNDEFINED_CL);
  nbytes = t_state.hdr_info.request_content_length - pushed_response_hdr_bytes;
  ink_release_assert(nbytes >= 0);

  if (ua_entry->eos == true) {
    // The ua has shutdown on us already so the only data
    //  we'll get is already in the buffer.  Make sure it
    //  fulfills the stated length
    int64_t avail = ua_buffer_reader->read_avail();

    if (avail < nbytes) {
      // Client failed to send the body, it's gone.  Kill the
      // state machine
      terminate_sm = true;
      return nullptr;
    }
  }
  // Next order of business is copy the remaining data from the
  //  header buffer into new buffer.
  pushed_response_body_bytes = buf->write(ua_buffer_reader, nbytes);
  ua_buffer_reader->consume(pushed_response_body_bytes);
  client_request_body_bytes += pushed_response_body_bytes;

  HTTP_SM_SET_DEFAULT_HANDLER(&HttpSM::tunnel_handler_push);

  HttpTunnelProducer *p =
    tunnel.add_producer(ua_entry->vc, nbytes, buf_start, &HttpSM::tunnel_handler_ua_push, HT_HTTP_CLIENT, "user_agent");
  setup_cache_write_transfer(&cache_sm, ua_entry->vc, &t_state.cache_info.object_store, 0, "cache write");

  ua_entry->in_tunnel = true;
  return p;
}

void
HttpSM::setup_blind_tunnel(bool send_response_hdr, IOBufferReader *initial)
{
  HttpTunnelConsumer *c_ua;
  HttpTunnelConsumer *c_os;
  HttpTunnelProducer *p_ua;
  HttpTunnelProducer *p_os;
  MIOBuffer *from_ua_buf = new_MIOBuffer(BUFFER_SIZE_INDEX_32K);
  MIOBuffer *to_ua_buf   = new_MIOBuffer(BUFFER_SIZE_INDEX_32K);
  IOBufferReader *r_from = from_ua_buf->alloc_reader();
  IOBufferReader *r_to   = to_ua_buf->alloc_reader();

  milestones[TS_MILESTONE_SERVER_BEGIN_WRITE] = Thread::get_hrtime();
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

  client_request_body_bytes = 0;
  if (ua_raw_buffer_reader != nullptr) {
    client_request_body_bytes += from_ua_buf->write(ua_raw_buffer_reader, client_request_hdr_bytes);
    ua_raw_buffer_reader->dealloc();
    ua_raw_buffer_reader = nullptr;
  }

  // Next order of business if copy the remaining data from the
  //  header buffer into new buffer
  client_request_body_bytes += from_ua_buf->write(ua_buffer_reader);

  HTTP_SM_SET_DEFAULT_HANDLER(&HttpSM::tunnel_handler);

  p_os =
    tunnel.add_producer(server_entry->vc, -1, r_to, &HttpSM::tunnel_handler_ssl_producer, HT_HTTP_SERVER, "http server - tunnel");

  c_ua = tunnel.add_consumer(ua_entry->vc, server_entry->vc, &HttpSM::tunnel_handler_ssl_consumer, HT_HTTP_CLIENT,
                             "user agent - tunnel");

  p_ua = tunnel.add_producer(ua_entry->vc, -1, r_from, &HttpSM::tunnel_handler_ssl_producer, HT_HTTP_CLIENT, "user agent - tunnel");

  c_os = tunnel.add_consumer(server_entry->vc, ua_entry->vc, &HttpSM::tunnel_handler_ssl_consumer, HT_HTTP_SERVER,
                             "http server - tunnel");

  // Make the tunnel aware that the entries are bi-directional
  tunnel.chain(c_os, p_os);
  tunnel.chain(c_ua, p_ua);

  ua_entry->in_tunnel     = true;
  server_entry->in_tunnel = true;

  tunnel.tunnel_run();

  // If we're half closed, we got a FIN from the client. Forward it on to the origin server
  // now that we have the tunnel operational.
  if (ua_txn && ua_txn->get_half_close_flag()) {
    p_ua->vc->do_io_shutdown(IO_SHUTDOWN_READ);
  }
}

void
HttpSM::setup_plugin_agents(HttpTunnelProducer *p)
{
  APIHook *agent           = txn_hook_get(TS_HTTP_RESPONSE_CLIENT_HOOK);
  has_active_plugin_agents = agent != nullptr;
  while (agent) {
    INKVConnInternal *contp = static_cast<INKVConnInternal *>(agent->m_cont);
    tunnel.add_consumer(contp, p->vc, &HttpSM::tunnel_handler_plugin_agent, HT_HTTP_CLIENT, "plugin agent");
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
  if (!has_active_plugin_agents) {
    APIHook *agent = txn_hook_get(TS_HTTP_RESPONSE_CLIENT_HOOK);
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
    // the state machine is in HTTP_API_NO_CALLOUT
    // state. This is because we are depending on the
    // callout to complete for the state machine to
    // get killed.
    if (callout_state == HTTP_API_NO_CALLOUT && pending_action) {
      pending_action->cancel();
      pending_action = nullptr;
    } else if (pending_action) {
      ink_assert(pending_action == nullptr);
    }

    cache_sm.end_both();
    transform_cache_sm.end_both();
    vc_table.cleanup_all();

    // tunnel.deallocate_buffers();
    // Why don't we just kill the tunnel?  Might still be
    // active if the state machine is going down hard,
    // and we should clean it up.
    tunnel.kill_tunnel();

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

    server_session = nullptr;

    // So we don't try to nuke the state machine
    //  if the plugin receives event we must reset
    //  the terminate_flag
    terminate_sm            = false;
    t_state.api_next_action = HttpTransact::SM_ACTION_API_SM_SHUTDOWN;
    do_api_callout();
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
    if (pending_action) {
      pending_action->cancel();
      pending_action = nullptr;
    }
    if (t_state.http_config_param->enable_http_stats) {
      update_stats();
    }

    if (ua_txn) {
      ua_txn->transaction_done();
    }

    // In the async state, the plugin could have been
    // called resulting in the creation of a plugin_tunnel.
    // So it needs to be deleted now.
    if (plugin_tunnel) {
      plugin_tunnel->kill_no_connect();
      plugin_tunnel = nullptr;
    }

    ink_assert(pending_action == nullptr);
    ink_release_assert(vc_table.is_table_clear() == true);
    ink_release_assert(tunnel.is_tunnel_active() == false);

    HTTP_SM_SET_DEFAULT_HANDLER(nullptr);

    //////////////
    // Log Data //
    //////////////
    SMDebug("http_seq", "[HttpSM::update_stats] Logging transaction");
    if (Log::transaction_logging_enabled() && t_state.api_info.logging_enabled) {
      LogAccess accessor(this);

      int ret = Log::access(&accessor);

      if (ret & Log::FULL) {
        SMDebug("http", "[update_stats] Logging system indicates FULL.");
      }
      if (ret & Log::FAIL) {
        Log::error("failed to log transaction for at least one log object");
      }
    }

    if (redirect_url != nullptr) {
      ats_free((void *)redirect_url);
      redirect_url     = nullptr;
      redirect_url_len = 0;
    }

#ifdef USE_HTTP_DEBUG_LISTS
    ink_mutex_acquire(&debug_sm_list_mutex);
    debug_sm_list.remove(this);
    ink_mutex_release(&debug_sm_list_mutex);
#endif

    SMDebug("http", "[%" PRId64 "] deallocating sm", sm_id);
    //    authAdapter.destroyState();
    destroy();
  }
}

void
HttpSM::update_stats()
{
  milestones[TS_MILESTONE_SM_FINISH] = Thread::get_hrtime();

  if (is_action_tag_set("bad_length_state_dump")) {
    if (t_state.hdr_info.client_response.valid() && t_state.hdr_info.client_response.status_get() == HTTP_STATUS_OK) {
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

  if (is_action_tag_set("assert_jtest_length")) {
    if (t_state.hdr_info.client_response.valid() && t_state.hdr_info.client_response.status_get() == HTTP_STATUS_OK) {
      int64_t p_resp_cl = t_state.hdr_info.client_response.get_content_length();
      int64_t resp_size = client_response_body_bytes;
      ink_release_assert(p_resp_cl == -1 || p_resp_cl == resp_size || resp_size == 0);
    }
  }

  ink_hrtime total_time = milestones.elapsed(TS_MILESTONE_SM_START, TS_MILESTONE_SM_FINISH);

  // ua_close will not be assigned properly in some exceptional situation.
  // TODO: Assign ua_close with suitable value when HttpTunnel terminates abnormally.
  if (milestones[TS_MILESTONE_UA_CLOSE] == 0 && milestones[TS_MILESTONE_UA_READ_HEADER_DONE] > 0) {
    milestones[TS_MILESTONE_UA_CLOSE] = Thread::get_hrtime();
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
    int offset           = 0;
    int skip             = 0;

    t_state.hdr_info.client_request.url_print(url_string, sizeof(url_string) - 1, &offset, &skip);
    url_string[offset] = 0; // NULL terminate the string

    // unique id
    char unique_id_string[128] = "";
    int length                 = 0;
    const char *field          = t_state.hdr_info.client_request.value_get(MIME_FIELD_X_ID, MIME_LEN_X_ID, &length);
    if (field != nullptr && length > 0) {
      length = std::min(length, static_cast<int>(sizeof(unique_id_string)) - 1);
      memcpy(unique_id_string, field, length);
      unique_id_string[length] = 0; // NULL terminate the string
    }

    // set the fd for the request
    int fd             = 0;
    NetVConnection *vc = nullptr;
    if (ua_txn != nullptr) {
      vc = ua_txn->get_netvc();
      if (vc != nullptr) {
        fd = vc->get_socket();
      } else {
        fd = -1;
      }
    }
    // get the status code, lame that we have to check to see if it is valid or we will assert in the method call
    int status = 0;
    if (t_state.hdr_info.client_response.valid()) {
      status = t_state.hdr_info.client_response.status_get();
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
          "ua_begin: %.3f "
          "ua_first_read: %.3f "
          "ua_read_header_done: %.3f "
          "cache_open_read_begin: %.3f "
          "cache_open_read_end: %.3f "
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
          sm_id, client_ip, t_state.client_info.src_addr.host_order_port(), ua_txn ? ua_txn->get_protocol_string() : "-1",
          url_string, status, unique_id_string, redirection_tries, client_response_body_bytes, fd, t_state.client_info.state,
          t_state.server_info.state, milestones.difference_sec(TS_MILESTONE_SM_START, TS_MILESTONE_UA_BEGIN),
          milestones.difference_sec(TS_MILESTONE_SM_START, TS_MILESTONE_UA_FIRST_READ),
          milestones.difference_sec(TS_MILESTONE_SM_START, TS_MILESTONE_UA_READ_HEADER_DONE),
          milestones.difference_sec(TS_MILESTONE_SM_START, TS_MILESTONE_CACHE_OPEN_READ_BEGIN),
          milestones.difference_sec(TS_MILESTONE_SM_START, TS_MILESTONE_CACHE_OPEN_READ_END),
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
    int r = history[i].reentrancy;
    int e = history[i].event;
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
    int l         = h->length_get();
    char *hdr_buf = (char *)ats_malloc(l + 1);
    int index     = 0;
    int offset    = 0;

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

  SMDebug("http", "[%" PRId64 "] State Transition: %s -> %s", sm_id, HttpDebugNames::get_action_name(last_action),
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
  case HttpTransact::SM_ACTION_API_PRE_REMAP:
  case HttpTransact::SM_ACTION_API_POST_REMAP:
  case HttpTransact::SM_ACTION_API_READ_REQUEST_HDR:
  case HttpTransact::SM_ACTION_REQUEST_BUFFER_READ_COMPLETE:
  case HttpTransact::SM_ACTION_API_OS_DNS:
  case HttpTransact::SM_ACTION_API_SEND_REQUEST_HDR:
  case HttpTransact::SM_ACTION_API_READ_CACHE_HDR:
  case HttpTransact::SM_ACTION_API_READ_RESPONSE_HDR:
  case HttpTransact::SM_ACTION_API_SEND_RESPONSE_HDR:
  case HttpTransact::SM_ACTION_API_CACHE_LOOKUP_COMPLETE: {
    t_state.api_next_action = t_state.next_action;
    do_api_callout();
    break;
  }

  case HttpTransact::SM_ACTION_POST_REMAP_SKIP: {
    call_transact_and_set_next_state(nullptr);
    break;
  }

  case HttpTransact::SM_ACTION_REMAP_REQUEST: {
    if (!remapProcessor.using_separate_thread()) {
      do_remap_request(true); /* run inline */
      SMDebug("url_rewrite", "completed inline remapping request for [%" PRId64 "]", sm_id);
      t_state.url_remap_success = remapProcessor.finish_remap(&t_state, m_remap);
      if (t_state.next_action == HttpTransact::SM_ACTION_SEND_ERROR_CACHE_NOOP && t_state.transact_return_point == nullptr) {
        // It appears that we can now set the next_action to error and transact_return_point to nullptr when
        // going through do_remap_request presumably due to a plugin setting an error.  In that case, it seems
        // that the error message has already been setup, so we can just return and avoid the further
        // call_transact_and_set_next_state
      } else {
        call_transact_and_set_next_state(nullptr);
      }
    } else {
      HTTP_SM_SET_DEFAULT_HANDLER(&HttpSM::state_remap_request);
      do_remap_request(false); /* dont run inline (iow on another thread) */
    }
    break;
  }

  case HttpTransact::SM_ACTION_DNS_LOOKUP: {
    sockaddr const *addr;

    if (t_state.api_server_addr_set) {
      /* If the API has set the server address before the OS DNS lookup
       * then we can skip the lookup
       */
      ip_text_buffer ipb;
      SMDebug("dns", "[HttpTransact::HandleRequest] Skipping DNS lookup for API supplied target %s.",
              ats_ip_ntop(&t_state.server_info.dst_addr, ipb, sizeof(ipb)));
      // this seems wasteful as we will just copy it right back
      ats_ip_copy(t_state.host_db_info.ip(), &t_state.server_info.dst_addr);
      t_state.dns_info.lookup_success = true;
      call_transact_and_set_next_state(nullptr);
      break;
    } else if (0 == ats_ip_pton(t_state.dns_info.lookup_name, t_state.host_db_info.ip()) &&
               ats_is_ip_loopback(t_state.host_db_info.ip())) {
      // If it's 127.0.0.1 or ::1 don't bother with hostdb
      SMDebug("dns", "[HttpTransact::HandleRequest] Skipping DNS lookup for %s because it's loopback",
              t_state.dns_info.lookup_name);
      t_state.dns_info.lookup_success = true;
      call_transact_and_set_next_state(nullptr);
      break;
    } else if (t_state.http_config_param->use_client_target_addr == 2 && !t_state.url_remap_success &&
               t_state.parent_result.result != PARENT_SPECIFIED && t_state.client_info.is_transparent &&
               t_state.dns_info.os_addr_style == HttpTransact::DNSLookupInfo::OS_Addr::OS_ADDR_TRY_DEFAULT &&
               ats_is_ip(addr = t_state.state_machine->ua_txn->get_netvc()->get_local_addr())) {
      /* If the connection is client side transparent and the URL
       * was not remapped/directed to parent proxy, we can use the
       * client destination IP address instead of doing a DNS
       * lookup. This is controlled by the 'use_client_target_addr'
       * configuration parameter.
       */
      if (is_debug_tag_set("dns")) {
        ip_text_buffer ipb;
        SMDebug("dns", "[HttpTransact::HandleRequest] Skipping DNS lookup for client supplied target %s.",
                ats_ip_ntop(addr, ipb, sizeof(ipb)));
      }
      ats_ip_copy(t_state.host_db_info.ip(), addr);
      if (t_state.hdr_info.client_request.version_get() == HTTPVersion(0, 9)) {
        t_state.host_db_info.app.http_data.http_version = HostDBApplicationInfo::HTTP_VERSION_09;
      } else if (t_state.hdr_info.client_request.version_get() == HTTPVersion(1, 0)) {
        t_state.host_db_info.app.http_data.http_version = HostDBApplicationInfo::HTTP_VERSION_10;
      } else {
        t_state.host_db_info.app.http_data.http_version = HostDBApplicationInfo::HTTP_VERSION_11;
      }

      t_state.dns_info.lookup_success = true;
      // cache this result so we don't have to unreliably duplicate the
      // logic later if the connect fails.
      t_state.dns_info.os_addr_style = HttpTransact::DNSLookupInfo::OS_Addr::OS_ADDR_TRY_CLIENT;
      call_transact_and_set_next_state(nullptr);
      break;
    } else if (t_state.parent_result.result == PARENT_UNDEFINED && t_state.dns_info.lookup_success) {
      // Already set, and we don't have a parent proxy to lookup
      ink_assert(ats_is_ip(t_state.host_db_info.ip()));
      SMDebug("dns", "[HttpTransact::HandleRequest] Skipping DNS lookup, provided by plugin");
      call_transact_and_set_next_state(nullptr);
      break;
    } else if (t_state.dns_info.looking_up == HttpTransact::ORIGIN_SERVER && t_state.http_config_param->no_dns_forward_to_parent &&
               t_state.parent_result.result != PARENT_UNDEFINED) {
      t_state.dns_info.lookup_success = true;
      call_transact_and_set_next_state(nullptr);
      break;
    }

    HTTP_SM_SET_DEFAULT_HANDLER(&HttpSM::state_hostdb_lookup);

    // We need to close the previous attempt
    // Because it could be a server side retry by DNS rr
    if (server_entry) {
      ink_assert(server_entry->vc_type == HTTP_SERVER_VC);
      vc_table.cleanup_entry(server_entry);
      server_entry   = nullptr;
      server_session = nullptr;
    } else {
      // Now that we have gotten the user agent request, we can cancel
      // the inactivity timeout associated with it.  Note, however, that
      // we must not cancel the inactivity timeout if the message
      // contains a body (as indicated by the non-zero request_content_length
      // field).  This indicates that a POST operation is taking place and
      // that the client is still sending data to the origin server.  The
      // origin server cannot reply until the entire request is received.  In
      // light of this dependency, TS must ensure that the client finishes
      // sending its request and for this reason, the inactivity timeout
      // cannot be cancelled.
      if (ua_txn && !t_state.hdr_info.request_content_length) {
        ua_txn->cancel_inactivity_timeout();
      } else if (!ua_txn) {
        terminate_sm = true;
        return; // Give up if there is no session
      }
    }

    ink_assert(t_state.dns_info.looking_up != HttpTransact::UNDEFINED_LOOKUP);
    do_hostdb_lookup();
    break;
  }

  case HttpTransact::SM_ACTION_DNS_REVERSE_LOOKUP: {
    HTTP_SM_SET_DEFAULT_HANDLER(&HttpSM::state_hostdb_reverse_lookup);
    do_hostdb_reverse_lookup();
    break;
  }

  case HttpTransact::SM_ACTION_CACHE_LOOKUP: {
    HTTP_SM_SET_DEFAULT_HANDLER(&HttpSM::state_cache_open_read);
    do_cache_lookup_and_read();
    break;
  }

  case HttpTransact::SM_ACTION_ORIGIN_SERVER_OPEN: {
    HTTP_SM_SET_DEFAULT_HANDLER(&HttpSM::state_http_server_open);

    // We need to close the previous attempt
    if (server_entry) {
      ink_assert(server_entry->vc_type == HTTP_SERVER_VC);
      vc_table.cleanup_entry(server_entry);
      server_entry   = nullptr;
      server_session = nullptr;
    } else {
      // Now that we have gotten the user agent request, we can cancel
      // the inactivity timeout associated with it.  Note, however, that
      // we must not cancel the inactivity timeout if the message
      // contains a body (as indicated by the non-zero request_content_length
      // field).  This indicates that a POST operation is taking place and
      // that the client is still sending data to the origin server.  The
      // origin server cannot reply until the entire request is received.  In
      // light of this dependency, TS must ensure that the client finishes
      // sending its request and for this reason, the inactivity timeout
      // cannot be cancelled.
      if (ua_txn && !t_state.hdr_info.request_content_length) {
        ua_txn->cancel_inactivity_timeout();
      } else if (!ua_txn) {
        terminate_sm = true;
        return; // Give up if there is no session
      }
    }

    do_http_server_open();
    break;
  }

  case HttpTransact::SM_ACTION_SERVER_PARSE_NEXT_HDR: {
    setup_server_read_response_header();
    break;
  }

  case HttpTransact::SM_ACTION_INTERNAL_100_RESPONSE: {
    setup_100_continue_transfer();
    break;
  }

  case HttpTransact::SM_ACTION_SERVER_READ: {
    t_state.source = HttpTransact::SOURCE_HTTP_ORIGIN_SERVER;

    if (transform_info.vc) {
      ink_assert(t_state.hdr_info.client_response.valid() == 0);
      ink_assert((t_state.hdr_info.transform_response.valid() ? true : false) == true);
      HttpTunnelProducer *p = setup_server_transfer_to_transform();
      perform_cache_write_action();
      tunnel.tunnel_run(p);
    } else {
      ink_assert((t_state.hdr_info.client_response.valid() ? true : false) == true);
      t_state.api_next_action = HttpTransact::SM_ACTION_API_SEND_RESPONSE_HDR;

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

  case HttpTransact::SM_ACTION_SERVE_FROM_CACHE: {
    ink_assert(t_state.cache_info.action == HttpTransact::CACHE_DO_SERVE ||
               t_state.cache_info.action == HttpTransact::CACHE_DO_SERVE_AND_DELETE ||
               t_state.cache_info.action == HttpTransact::CACHE_DO_SERVE_AND_UPDATE);
    release_server_session(true);
    t_state.source = HttpTransact::SOURCE_CACHE;

    if (transform_info.vc) {
      ink_assert(t_state.hdr_info.client_response.valid() == 0);
      ink_assert((t_state.hdr_info.transform_response.valid() ? true : false) == true);
      do_drain_request_body(t_state.hdr_info.transform_response);
      t_state.hdr_info.cache_response.create(HTTP_TYPE_RESPONSE);
      t_state.hdr_info.cache_response.copy(&t_state.hdr_info.transform_response);

      HttpTunnelProducer *p = setup_cache_transfer_to_transform();
      perform_cache_write_action();
      tunnel.tunnel_run(p);
    } else {
      ink_assert((t_state.hdr_info.client_response.valid() ? true : false) == true);
      do_drain_request_body(t_state.hdr_info.client_response);
      t_state.hdr_info.cache_response.create(HTTP_TYPE_RESPONSE);
      t_state.hdr_info.cache_response.copy(&t_state.hdr_info.client_response);

      perform_cache_write_action();
      t_state.api_next_action = HttpTransact::SM_ACTION_API_SEND_RESPONSE_HDR;

      // check to see if there is a plugin hook set
      if (hooks_set) {
        do_api_callout_internal();
      } else {
        handle_api_return();
      }
    }
    break;
  }

  case HttpTransact::SM_ACTION_CACHE_ISSUE_WRITE: {
    ink_assert((cache_sm.cache_write_vc == nullptr) || t_state.redirect_info.redirect_in_process);
    HTTP_SM_SET_DEFAULT_HANDLER(&HttpSM::state_cache_open_write);

    do_cache_prepare_write();
    break;
  }

  case HttpTransact::SM_ACTION_INTERNAL_CACHE_WRITE: {
    t_state.api_next_action = HttpTransact::SM_ACTION_API_SEND_RESPONSE_HDR;
    do_api_callout();
    break;
  }

  case HttpTransact::SM_ACTION_INTERNAL_CACHE_NOOP: {
    if (server_entry != nullptr && server_entry->in_tunnel == false) {
      release_server_session();
    }
    // If we're in state SEND_API_RESPONSE_HDR, it means functions
    // registered to hook SEND_RESPONSE_HDR have already been called. So we do not
    // need to call do_api_callout. Otherwise TS loops infinitely in this state !
    if (t_state.api_next_action == HttpTransact::SM_ACTION_API_SEND_RESPONSE_HDR) {
      handle_api_return();
    } else {
      t_state.api_next_action = HttpTransact::SM_ACTION_API_SEND_RESPONSE_HDR;
      do_api_callout();
    }
    break;
  }

  case HttpTransact::SM_ACTION_INTERNAL_CACHE_DELETE: {
    // Nuke all the alternates since this is mostly likely
    //   the result of a delete method
    cache_sm.end_both();
    do_cache_delete_all_alts(nullptr);

    release_server_session();
    t_state.api_next_action = HttpTransact::SM_ACTION_API_SEND_RESPONSE_HDR;
    do_api_callout();
    break;
  }

  case HttpTransact::SM_ACTION_INTERNAL_CACHE_UPDATE_HEADERS: {
    issue_cache_update();
    cache_sm.close_read();

    release_server_session();
    t_state.api_next_action = HttpTransact::SM_ACTION_API_SEND_RESPONSE_HDR;
    do_api_callout();
    break;
  }

  case HttpTransact::SM_ACTION_SEND_ERROR_CACHE_NOOP: {
    setup_error_transfer();
    break;
  }

  case HttpTransact::SM_ACTION_INTERNAL_REQUEST: {
    HTTP_SM_SET_DEFAULT_HANDLER(&HttpSM::state_handle_stat_page);
    Action *action_handle = statPagesManager.handle_http(this, &t_state.hdr_info.client_request);

    if (action_handle != ACTION_RESULT_DONE) {
      ink_assert(pending_action == nullptr);
      pending_action = action_handle;
    }

    break;
  }

  case HttpTransact::SM_ACTION_ORIGIN_SERVER_RR_MARK_DOWN: {
    HTTP_SM_SET_DEFAULT_HANDLER(&HttpSM::state_mark_os_down);

    ink_assert(t_state.dns_info.looking_up == HttpTransact::ORIGIN_SERVER);

    // TODO: This might not be optimal (or perhaps even correct), but it will
    // effectively mark the host as down. What's odd is that state_mark_os_down
    // above isn't triggering.
    HttpSM::do_hostdb_update_if_necessary();

    do_hostdb_lookup();
    break;
  }

  case HttpTransact::SM_ACTION_SSL_TUNNEL: {
    t_state.api_next_action = HttpTransact::SM_ACTION_API_SEND_RESPONSE_HDR;
    do_api_callout();
    break;
  }

  case HttpTransact::SM_ACTION_ORIGIN_SERVER_RAW_OPEN: {
    HTTP_SM_SET_DEFAULT_HANDLER(&HttpSM::state_raw_http_server_open);

    ink_assert(server_entry == nullptr);
    do_http_server_open(true);
    break;
  }

  case HttpTransact::SM_ACTION_CACHE_ISSUE_WRITE_TRANSFORM: {
    ink_assert(t_state.cache_info.transform_action == HttpTransact::CACHE_PREPARE_TO_WRITE);

    if (transform_cache_sm.cache_write_vc) {
      // We've already got the write_vc that
      //  didn't use for the untransformed copy
      ink_assert(cache_sm.cache_write_vc == nullptr);
      ink_assert(t_state.api_info.cache_untransformed == false);
      t_state.cache_info.write_lock_state = HttpTransact::CACHE_WL_SUCCESS;
      call_transact_and_set_next_state(nullptr);
    } else {
      HTTP_SM_SET_DEFAULT_HANDLER(&HttpSM::state_cache_open_write);

      do_cache_prepare_write_transform();
    }
    break;
  }

  case HttpTransact::SM_ACTION_TRANSFORM_READ: {
    t_state.api_next_action = HttpTransact::SM_ACTION_API_SEND_RESPONSE_HDR;
    do_api_callout();
    break;
  }

  case HttpTransact::SM_ACTION_READ_PUSH_HDR: {
    setup_push_read_response_header();
    break;
  }

  case HttpTransact::SM_ACTION_STORE_PUSH_BODY: {
    // This can return NULL - do we really want to run the tunnel in that case?
    // But that's how it was before this change.
    HttpTunnelProducer *p = setup_push_transfer_to_cache();
    tunnel.tunnel_run(p);
    break;
  }

  case HttpTransact::SM_ACTION_CACHE_PREPARE_UPDATE: {
    ink_assert(t_state.api_update_cached_object == HttpTransact::UPDATE_CACHED_OBJECT_CONTINUE);
    do_cache_prepare_update();
    break;
  }
  case HttpTransact::SM_ACTION_CACHE_ISSUE_UPDATE: {
    if (t_state.api_update_cached_object == HttpTransact::UPDATE_CACHED_OBJECT_ERROR) {
      t_state.cache_info.object_read = nullptr;
      cache_sm.close_read();
    }
    issue_cache_update();
    call_transact_and_set_next_state(nullptr);
    break;
  }

  case HttpTransact::SM_ACTION_WAIT_FOR_FULL_BODY: {
    wait_for_full_body();
    break;
  }

  case HttpTransact::SM_ACTION_CONTINUE: {
    ink_release_assert(!"Not implemented");
    break;
  }

  default: {
    ink_release_assert(!"Unknown next action");
  }
  }
}

void
clear_http_handler_times()
{
}

// YTS Team, yamsat Plugin

void
HttpSM::do_redirect()
{
  SMDebug("http_redirect", "[HttpSM::do_redirect]");
  if (!enable_redirection || redirection_tries > t_state.txn_conf->number_of_redirections) {
    this->postbuf_clear();

    if (enable_redirection && redirection_tries > t_state.txn_conf->number_of_redirections) {
      t_state.squid_codes.subcode = SQUID_SUBCODE_NUM_REDIRECTIONS_EXCEEDED;
    }

    return;
  }

  // if redirect_url is set by an user's plugin, yts will redirect to this url anyway.
  if (is_redirect_required()) {
    if (redirect_url != nullptr || t_state.hdr_info.client_response.field_find(MIME_FIELD_LOCATION, MIME_LEN_LOCATION)) {
      if (Log::transaction_logging_enabled() && t_state.api_info.logging_enabled) {
        LogAccess accessor(this);
        if (redirect_url == nullptr) {
          if (t_state.squid_codes.log_code == SQUID_LOG_TCP_HIT) {
            t_state.squid_codes.log_code = SQUID_LOG_TCP_HIT_REDIRECT;
          } else {
            t_state.squid_codes.log_code = SQUID_LOG_TCP_MISS_REDIRECT;
          }
        } else {
          if (t_state.squid_codes.log_code == SQUID_LOG_TCP_HIT) {
            t_state.squid_codes.log_code = SQUID_LOG_TCP_HIT_X_REDIRECT;
          } else {
            t_state.squid_codes.log_code = SQUID_LOG_TCP_MISS_X_REDIRECT;
          }
        }

        int ret = Log::access(&accessor);

        if (ret & Log::FULL) {
          SMDebug("http", "[update_stats] Logging system indicates FULL.");
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
        HTTP_INCREMENT_DYN_STAT(http_total_x_redirect_stat);
      } else {
        // get the location header and setup the redirect
        int redir_len   = 0;
        char *redir_url = (char *)t_state.hdr_info.client_response.value_get(MIME_FIELD_LOCATION, MIME_LEN_LOCATION, &redir_len);
        redirect_request(redir_url, redir_len);
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
  SMDebug("http_redirect", "[HttpSM::redirect_request]");
  // get a reference to the client request header and client url and check to see if the url is valid
  HTTPHdr &clientRequestHeader = t_state.hdr_info.client_request;
  URL &clientUrl               = *clientRequestHeader.url_get();
  if (!clientUrl.valid()) {
    return;
  }

  bool valid_origHost = true;
  int origHost_len, origMethod_len;
  char origHost[MAXDNAME];
  char origMethod[255];
  int origPort = 80;

  if (t_state.hdr_info.server_request.valid()) {
    char *tmpOrigHost;

    origPort    = t_state.hdr_info.server_request.port_get();
    tmpOrigHost = (char *)t_state.hdr_info.server_request.value_get(MIME_FIELD_HOST, MIME_LEN_HOST, &origHost_len);

    if (tmpOrigHost) {
      memcpy(origHost, tmpOrigHost, origHost_len);
      origHost[std::min(origHost_len, MAXDNAME - 1)] = '\0';
    } else {
      valid_origHost = false;
    }

    char *tmpOrigMethod = (char *)t_state.hdr_info.server_request.method_get(&origMethod_len);
    if (tmpOrigMethod) {
      memcpy(origMethod, tmpOrigMethod, std::min(origMethod_len, static_cast<int>(sizeof(origMethod))));
    } else {
      valid_origHost = false;
    }
  } else {
    SMDebug("http_redir_error", "t_state.hdr_info.server_request not valid");
    valid_origHost = false;
  }

  t_state.redirect_info.redirect_in_process = true;

  // set the passed in location url and parse it
  URL redirectUrl;
  redirectUrl.create(nullptr);

  redirectUrl.parse(arg_redirect_url, arg_redirect_len);
  {
    int _scheme_len = -1;
    int _host_len   = -1;
    if (redirectUrl.scheme_get(&_scheme_len) == nullptr && redirectUrl.host_get(&_host_len) != nullptr &&
        arg_redirect_url[0] != '/') {
      // RFC7230 § 5.5
      // The redirect URL lacked a scheme and so it is a relative URL.
      // The redirect URL did not begin with a slash, so we parsed some or all
      // of the the relative URI path as the host.
      // Prepend a slash and parse again.
      char redirect_url_leading_slash[arg_redirect_len + 1];
      redirect_url_leading_slash[0] = '/';
      if (arg_redirect_len > 0) {
        memcpy(redirect_url_leading_slash + 1, arg_redirect_url, arg_redirect_len);
      }
      url_nuke_proxy_stuff(redirectUrl.m_url_impl);
      redirectUrl.parse(redirect_url_leading_slash, arg_redirect_len + 1);
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

  int scheme                  = t_state.next_hop_scheme;
  int scheme_len              = hdrtoken_index_to_length(scheme);
  const char *next_hop_scheme = hdrtoken_index_to_wks(scheme);
  char scheme_str[scheme_len + 1];

  if (next_hop_scheme) {
    memcpy(scheme_str, next_hop_scheme, scheme_len);
  } else {
    valid_origHost = false;
  }

  t_state.hdr_info.server_request.destroy();

  // we want to close the server session
  // will do that in handle_api_return under the
  // HttpTransact::SM_ACTION_REDIRECT_READ state
  t_state.parent_result.reset();
  t_state.request_sent_time           = 0;
  t_state.response_received_time      = 0;
  t_state.cache_info.write_lock_state = HttpTransact::CACHE_WL_INIT;
  t_state.next_action                 = HttpTransact::SM_ACTION_REDIRECT_READ;
  // we have a new OS and need to have DNS lookup the new OS
  t_state.dns_info.lookup_success = false;
  t_state.force_dns               = false;
  t_state.server_info.clear();
  t_state.parent_info.clear();

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
    int host_len;
    const char *host = clientUrl.host_get(&host_len);

    if (host != nullptr) {
      int port = clientUrl.port_get();
      int redirectSchemeLen;
      const char *redirectScheme = clientUrl.scheme_get(&redirectSchemeLen);

      if (redirectScheme == nullptr) {
        clientUrl.scheme_set(scheme_str, scheme_len);
        SMDebug("http_redirect", "[HttpSM::redirect_request] URL without scheme");
      }

      if (noPortInHost) {
        int redirectSchemeIdx = clientUrl.scheme_get_wksidx();
        bool defaultPort =
          (((redirectSchemeIdx == URL_WKSIDX_HTTP) && (port == 80)) || ((redirectSchemeIdx == URL_WKSIDX_HTTPS) && (port == 443)));

        if (!defaultPort) {
          noPortInHost = false;
        }
      }

      if (!noPortInHost) {
        char buf[host_len + 7]; // 5 + 1 + 1 ("12345" + ':' + '\0')

        host_len = snprintf(buf, host_len + 7, "%.*s:%d", host_len, host, port);
        t_state.hdr_info.client_request.value_set(MIME_FIELD_HOST, MIME_LEN_HOST, buf, host_len);
      } else {
        t_state.hdr_info.client_request.value_set(MIME_FIELD_HOST, MIME_LEN_HOST, host, host_len);
      }
      t_state.hdr_info.client_request.m_target_cached = false;
      t_state.hdr_info.server_request.m_target_cached = false;
    } else {
      // the client request didn't have a host, so use the current origin host
      if (valid_origHost) {
        char *saveptr = nullptr;

        // the client request didn't have a host, so use the current origin host
        SMDebug("http_redirect", "[HttpSM::redirect_request] keeping client request host %s://%s", next_hop_scheme, origHost);
        char *origHostNoPort = strtok_r(origHost, ":", &saveptr);

        if (origHostNoPort == nullptr) {
          goto LhostError;
        }

        host_len = strlen(origHostNoPort);
        if (noPortInHost) {
          int redirectSchemeIdx = t_state.next_hop_scheme;
          bool defaultPort      = (((redirectSchemeIdx == URL_WKSIDX_HTTP) && (origPort == 80)) ||
                              ((redirectSchemeIdx == URL_WKSIDX_HTTPS) && (origPort == 443)));

          if (!defaultPort) {
            noPortInHost = false;
          }
        }

        if (!noPortInHost) {
          char buf[host_len + 7]; // 5 + 1 + 1 ("12345" + ':' + '\0')

          host_len = snprintf(buf, host_len + 7, "%s:%d", origHostNoPort, origPort);
          t_state.hdr_info.client_request.value_set(MIME_FIELD_HOST, MIME_LEN_HOST, buf, host_len);
        } else {
          t_state.hdr_info.client_request.value_set(MIME_FIELD_HOST, MIME_LEN_HOST, origHostNoPort, host_len);
        }

        // Cleanup of state etc.
        url_nuke_proxy_stuff(clientUrl.m_url_impl);
        url_nuke_proxy_stuff(t_state.hdr_info.client_request.m_url_cached.m_url_impl);
        t_state.hdr_info.client_request.method_set(origMethod, std::min(origMethod_len, static_cast<int>(sizeof(origMethod))));
        t_state.hdr_info.client_request.m_target_cached = false;
        t_state.hdr_info.server_request.m_target_cached = false;
        clientUrl.scheme_set(scheme_str, scheme_len);
        if (isRedirectUrlOriginForm) {
          // build the rest of the effictive URL: the authority part
          clientUrl.user_set(origUrl.m_url_impl->m_ptr_user, origUrl.m_url_impl->m_len_user);
          clientUrl.password_set(origUrl.m_url_impl->m_ptr_password, origUrl.m_url_impl->m_len_password);
          clientUrl.host_set(origUrl.m_url_impl->m_ptr_host, origUrl.m_url_impl->m_len_host);
          clientUrl.port_set(origUrl.port_get());
        }
      } else {
      LhostError:
        // the server request didn't have a host, so remove it from the headers
        t_state.hdr_info.client_request.field_delete(MIME_FIELD_HOST, MIME_LEN_HOST);
      }
    }
  }

  DUMP_HEADER("http_hdrs", &t_state.hdr_info.client_request, sm_id, "Framed Client Request..checking");
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
  bool plugin_lock;
  Ptr<ProxyMutex> plugin_mutex;
  if (schedule_cont->mutex) {
    plugin_mutex = schedule_cont->mutex;
    plugin_lock  = MUTEX_TAKE_TRY_LOCK(schedule_cont->mutex, mutex->thread_holding);

    if (!plugin_lock) {
      HTTP_SM_SET_DEFAULT_HANDLER(&HttpSM::get_http_schedule);
      ink_assert(pending_action == nullptr);
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

bool
HttpSM::set_server_session_private(bool private_session)
{
  if (server_session != nullptr) {
    server_session->private_session = private_session;
    return true;
  }
  return false;
}

inline bool
HttpSM::is_private()
{
  bool res = false;
  if (server_session) {
    res = server_session->private_session;
  } else if (ua_txn) {
    Http1ServerSession *ss = ua_txn->get_server_session();
    if (ss) {
      res = ss->private_session;
    } else if (will_be_private_ss) {
      res = will_be_private_ss;
    }
  }
  return res;
}

// check to see if redirection is enabled and less than max redirections tries or if a plugin enabled redirection
inline bool
HttpSM::is_redirect_required()
{
  bool redirect_required = (enable_redirection && (redirection_tries <= t_state.txn_conf->number_of_redirections) &&
                            !HttpTransact::is_cache_hit(t_state.cache_lookup_result));

  SMDebug("http_redirect", "is_redirect_required %u", redirect_required);

  if (redirect_required == true) {
    HTTPStatus status = t_state.hdr_info.client_response.status_get();
    // check to see if the response from the origin was a 301, 302, or 303
    switch (status) {
    case HTTP_STATUS_MULTIPLE_CHOICES:   // 300
    case HTTP_STATUS_MOVED_PERMANENTLY:  // 301
    case HTTP_STATUS_MOVED_TEMPORARILY:  // 302
    case HTTP_STATUS_SEE_OTHER:          // 303
    case HTTP_STATUS_USE_PROXY:          // 305
    case HTTP_STATUS_TEMPORARY_REDIRECT: // 307
    case HTTP_STATUS_PERMANENT_REDIRECT: // 308
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

// Fill in the client protocols used.  Return the number of entries returned
int
HttpSM::populate_client_protocol(std::string_view *result, int n) const
{
  int retval = 0;
  if (n > 0) {
    std::string_view proto = HttpSM::find_proto_string(t_state.hdr_info.client_request.version_get());
    if (!proto.empty()) {
      result[retval++] = proto;
      if (n > retval && ua_txn) {
        retval += ua_txn->populate_protocol(result + retval, n - retval);
      }
    }
  }
  return retval;
}

// Look for a specific protocol
const char *
HttpSM::client_protocol_contains(std::string_view tag_prefix) const
{
  const char *retval     = nullptr;
  std::string_view proto = HttpSM::find_proto_string(t_state.hdr_info.client_request.version_get());
  if (!proto.empty()) {
    std::string_view prefix(tag_prefix);
    if (prefix.size() <= proto.size() && 0 == strncmp(proto.data(), prefix.data(), prefix.size())) {
      retval = proto.data();
    } else if (ua_txn) {
      retval = ua_txn->protocol_contains(prefix);
    }
  }
  return retval;
}

std::string_view
HttpSM::find_proto_string(HTTPVersion version) const
{
  if (version == HTTPVersion(1, 1)) {
    return IP_PROTO_TAG_HTTP_1_1;
  } else if (version == HTTPVersion(1, 0)) {
    return IP_PROTO_TAG_HTTP_1_0;
  } else if (version == HTTPVersion(0, 9)) {
    return IP_PROTO_TAG_HTTP_0_9;
  }
  return {};
}

// YTS Team, yamsat Plugin
// Function to copy the partial Post data while tunnelling
void
PostDataBuffers::copy_partial_post_data()
{
  if (post_data_buffer_done) {
    return;
  }
  Debug("http_redirect", "[PostDataBuffers::copy_partial_post_data] wrote %" PRId64 " bytes to buffers %" PRId64 "",
        this->ua_buffer_reader->read_avail(), this->postdata_copy_buffer_start->read_avail());
  this->postdata_copy_buffer->write(this->ua_buffer_reader);
  this->ua_buffer_reader->consume(this->ua_buffer_reader->read_avail());
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
  Debug("http_redirect", "[PostDataBuffers::init]");

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
  Debug("http_redirect", "[PostDataBuffers::clear]");

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
