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

/****************************************************************************

   HttpSM.h

   Description:


 ****************************************************************************/
#pragma once

#include <string_view>
#include <optional>

#include "tscore/ink_platform.h"
#include "iocore/eventsystem/EventSystem.h"
#include "proxy/http/HttpCacheSM.h"
#include "proxy/http/HttpTransact.h"
#include "proxy/http/HttpUserAgent.h"
#include "proxy/http/HttpVCTable.h"
#include "proxy/http/remap/UrlRewrite.h"
#include "proxy/http/HttpTunnel.h"
#include "api/InkAPIInternal.h"
#include "proxy/ProxyTransaction.h"
#include "proxy/hdrs/HdrUtils.h"

// inknet
#include "proxy/http/PreWarmManager.h"
#include "iocore/net/TLSTunnelSupport.h"

#include "tscore/History.h"
#include "tscore/PendingAction.h"

#define HTTP_API_CONTINUE (INK_API_EVENT_EVENTS_START + 0)
#define HTTP_API_ERROR    (INK_API_EVENT_EVENTS_START + 1)

#define CONNECT_EVENT_TXN    (HTTP_NET_CONNECTION_EVENT_EVENTS_START) + 0
#define CONNECT_EVENT_DIRECT (HTTP_NET_CONNECTION_EVENT_EVENTS_START) + 1

// The default size for http header buffers when we don't
//   need to include extra space for the document
static size_t const HTTP_HEADER_BUFFER_SIZE_INDEX = BUFFER_SIZE_INDEX_4K;

// We want to use a larger buffer size when reading response
//   headers from the origin server since we want to get
//   as much of the document as possible on the first read
//   Marco benchmarked about 3% ops/second improvement using
//   the larger buffer size
static size_t const HTTP_SERVER_RESP_HDR_BUFFER_INDEX = BUFFER_SIZE_INDEX_8K;

class PoolableSession;
class AuthHttpAdapter;

class HttpSM;
using HttpSMHandler = int (HttpSM::*)(int, void *);

/** Write Proxy Protocol to the first block of given MIOBuffer.
 *
 * @param[in] miob The MIOBuffer to write the Proxy Protocol to.
 * @param[in] vc_out The outbound (server-side) VC.
 * @param[in] vc_in The inbound (client-side) VC.
 * @param[in] conf The configured Proxy Protocol version to write.
 *
 * @return The number of bytes written on the socket to write the Proxy
 * Protocol.
 */
int64_t do_outbound_proxy_protocol(MIOBuffer *miob, NetVConnection *vc_out, NetVConnection *vc_in, int conf);

enum class BackgroundFill_t {
  NONE = 0,
  STARTED,
  ABORTED,
  COMPLETED,
};

extern ink_mutex debug_sm_list_mutex;

struct HttpTransformInfo {
  HttpVCTableEntry *entry = nullptr;
  VConnection      *vc    = nullptr;

  HttpTransformInfo() {}
};

enum class HttpSmMagic_t : uint32_t {
  ALIVE = 0x0000FEED,
  DEAD  = 0xDEADFEED,
};

enum class HttpSmPost_t {
  UNKNOWN     = 0,
  UA_FAIL     = 1,
  SERVER_FAIL = 2,
  SUCCESS     = 3,
};

enum {
  HTTP_SM_TRANSFORM_OPEN   = 0,
  HTTP_SM_TRANSFORM_CLOSED = 1,
  HTTP_SM_TRANSFORM_FAIL   = 2,
};

enum class HttpApiState_t {
  NO_CALLOUT,
  IN_CALLOUT,
  DEFERED_CLOSE,
  DEFERED_SERVER_ERROR,
  REWIND_STATE_MACHINE,
};

enum class HttpPluginTunnel_t {
  NONE = 0,
  AS_SERVER,
  AS_INTERCEPT,
};

class PluginVCCore;

class PostDataBuffers
{
public:
  PostDataBuffers()
  {
    static DbgCtl dc{"http_redirect"};
    Dbg(dc, "[PostDataBuffers::PostDataBuffers]");
  }
  MIOBuffer      *postdata_copy_buffer       = nullptr;
  IOBufferReader *postdata_copy_buffer_start = nullptr;
  IOBufferReader *ua_buffer_reader           = nullptr;
  bool            post_data_buffer_done      = false;

  void            clear();
  void            init(IOBufferReader *ua_reader);
  int64_t         copy_partial_post_data(int64_t consumed_bytes);
  IOBufferReader *get_post_data_buffer_clone_reader();
  void
  set_post_data_buffer_done(bool done)
  {
    post_data_buffer_done = done;
  }
  bool
  get_post_data_buffer_done()
  {
    return post_data_buffer_done;
  }
  bool
  is_valid()
  {
    return postdata_copy_buffer_start != nullptr;
  }

  ~PostDataBuffers();
};

class HttpSM : public Continuation, public PluginUserArgs<TS_USER_ARGS_TXN>
{
  friend class HttpTransact;

public:
  HttpSM();
  void         cleanup();
  virtual void destroy();

  static HttpSM   *allocate();
  HttpCacheSM     &get_cache_sm(); // Added to get the object of CacheSM YTS Team, yamsat
  std::string_view get_outbound_sni() const;
  std::string_view get_outbound_cert() const;

  void init(bool from_early_data = false);

  void attach_client_session(ProxyTransaction *txn);

  // Called after the network connection has been completed
  //  to set the session timeouts and initiate a read while
  //  holding the lock for the server session
  void attach_server_session();

  PoolableSession *create_server_session(NetVConnection &netvc, MIOBuffer *netvc_read_buffer, IOBufferReader *netvc_reader);
  bool             create_server_txn(PoolableSession *new_session);

  HTTPVersion get_server_version(HTTPHdr &hdr) const;

  HttpUserAgent const &get_user_agent() const;
  ProxyTransaction    *get_ua_txn();
  ProxyTransaction    *get_server_txn();

  // Called by transact.  Updates are fire and forget
  //  so there are no callbacks and are safe to do
  //  directly from transact
  void do_hostdb_update_if_necessary();

  // Look at the configured policy and the current server connect_result
  // to determine whether this connection attempt should contribute to the
  // dead server count
  bool track_connect_fail() const;

  // Called by transact. Decide if cached response supports Range and
  // setup Range transfomration if so.
  // return true when the Range is unsatisfiable
  void do_range_setup_if_necessary();

  void do_range_parse(MIMEField *range_field);
  void calculate_output_cl(int64_t, int64_t);
  void parse_range_and_compare(MIMEField *, int64_t);

  // Called by transact to prevent reset problems
  //  failed PUSH requests
  void set_ua_half_close_flag();

  // Called by either state_hostdb_lookup() or directly
  //   by the HostDB in the case of inline completion
  // Handles the setting of all state necessary before
  //   calling transact to process the hostdb lookup
  // A NULL 'r' argument indicates the hostdb lookup failed
  void process_hostdb_info(HostDBRecord *record);
  void process_srv_info(HostDBRecord *record);
  bool origin_multiplexed() const;
  bool add_to_existing_request();

  // Called by transact.  Synchronous.
  VConnection *do_transform_open();
  VConnection *do_post_transform_open();

  // Called by transact(HttpTransact::is_request_retryable), temporarily.
  // This function should be remove after #1994 fixed.
  bool is_post_transform_request();

  // Called from InkAPI.cc which acquires the state machine lock
  //  before calling
  int state_api_callback(int event, void *data);
  int state_api_callout(int event, void *data);

  // Debugging routines to dump the SM history, hdrs
  void dump_state_on_assert();
  void dump_state_hdr(HTTPHdr *h, const char *s);

  // Functions for manipulating api hooks
  void     txn_hook_add(TSHttpHookID id, INKContInternal *cont);
  APIHook *txn_hook_get(TSHttpHookID id);

  bool is_private() const;
  bool is_redirect_required();

  /// Get the protocol stack for the inbound (client, user agent) connection.
  /// @arg result [out] Array to store the results
  /// @arg n [in] Size of the array @a result.
  int         populate_client_protocol(std::string_view *result, int n) const;
  const char *client_protocol_contains(std::string_view tag_prefix) const;

  /// Get the protocol stack for the outbound (origin server) connection.
  /// @arg result [out] Array to store the results
  /// @arg n [in] Size of the array @a result.
  int         populate_server_protocol(std::string_view *result, int n) const;
  const char *server_protocol_contains(std::string_view tag_prefix) const;

  std::string_view find_proto_string(HTTPVersion version) const;

  int64_t       sm_id = -1;
  HttpSmMagic_t magic = HttpSmMagic_t::DEAD;

  // YTS Team, yamsat Plugin
  bool    enable_redirection = false; // To check if redirection is enabled
  bool    post_failed        = false; // Added to identify post failure
  bool    debug_on           = false; // Transaction specific debug flag
  char   *redirect_url = nullptr; // url for force redirect (provide users a functionality to redirect to another url when needed)
  int     redirect_url_len  = 0;
  int     redirection_tries = 0; // To monitor number of redirections
  int64_t transferred_bytes = 0; // For handling buffering of request body data.

  BackgroundFill_t background_fill = BackgroundFill_t::NONE;

  // Tunneling request to plugin
  HttpPluginTunnel_t plugin_tunnel_type = HttpPluginTunnel_t::NONE;
  PluginVCCore      *plugin_tunnel      = nullptr;

  HttpTransact::State t_state;

  // This unfortunately can't go into the t_state, because of circular dependencies. We could perhaps refactor
  // this, with a lot of work, but this is easier for now.
  UrlRewrite *m_remap = nullptr;

  History<HISTORY_DEFAULT_SIZE> history;
  NetVConnection *
  get_server_vc()
  {
    if (server_entry != nullptr) {
      return dynamic_cast<NetVConnection *>(server_entry->vc);
    } else {
      return nullptr;
    }
  }

  // _postbuf api
  int64_t         postbuf_reader_avail();
  int64_t         postbuf_buffer_avail();
  void            postbuf_clear();
  void            disable_redirect();
  int64_t         postbuf_copy_partial_data(int64_t consumed_bytes);
  void            postbuf_init(IOBufferReader *ua_reader);
  void            set_postbuf_done(bool done);
  IOBufferReader *get_postbuf_clone_reader();
  bool            get_postbuf_done();
  bool            is_postbuf_valid();

  // See if we should allow the transaction
  // based on sni and host name header values
  void           check_sni_host();
  SNIRoutingType get_tunnel_type() const;
  void           set_http_schedule(Continuation *);
  int            get_http_schedule(int event, void *data);

private:
  void start_sub_sm();

  int main_handler(int event, void *data);
  int tunnel_handler(int event, void *data);
  int tunnel_handler_push(int event, void *data);
  int tunnel_handler_post(int event, void *data);
  int tunnel_handler_trailer(int event, void *data);

  // YTS Team, yamsat Plugin
  int tunnel_handler_for_partial_post(int event, void *data);

  void tunnel_handler_post_or_put(HttpTunnelProducer *p);

  int tunnel_handler_100_continue(int event, void *data);
  int tunnel_handler_cache_fill(int event, void *data);
  int state_read_client_request_header(int event, void *data);
  int state_watch_for_client_abort(int event, void *data);
  int state_read_push_response_header(int event, void *data);
  int state_pre_resolve(int event, void *data);
  int state_hostdb_lookup(int event, void *data);
  int state_hostdb_reverse_lookup(int event, void *data);
  int state_mark_os_down(int event, void *data);
  int state_auth_callback(int event, void *data);
  int state_add_to_list(int event, void *data);
  int state_remove_from_list(int event, void *data);

  // Y! ebalsa: remap handlers
  int  state_remap_request(int event, void *data);
  void do_remap_request(bool);

  // Cache Handlers
  int state_cache_open_read(int event, void *data);
  int state_cache_open_write(int event, void *data);

  // Http Server Handlers
  int state_http_server_open(int event, void *data);
  int state_raw_http_server_open(int event, void *data);
  int state_send_server_request_header(int event, void *data);
  int state_read_server_response_header(int event, void *data);

  // API
  int state_request_wait_for_transform_read(int event, void *data);
  int state_response_wait_for_transform_read(int event, void *data);
  int state_common_wait_for_transform_read(HttpTransformInfo *t_info, HttpSMHandler tunnel_handler, int event, void *data);

  // Tunnel event handlers
  int tunnel_handler_server(int event, HttpTunnelProducer *p);
  int tunnel_handler_ua(int event, HttpTunnelConsumer *c);
  int tunnel_handler_ua_push(int event, HttpTunnelProducer *p);
  int tunnel_handler_100_continue_ua(int event, HttpTunnelConsumer *c);
  int tunnel_handler_cache_write(int event, HttpTunnelConsumer *c);
  int tunnel_handler_cache_read(int event, HttpTunnelProducer *p);
  int tunnel_handler_post_ua(int event, HttpTunnelProducer *c);
  int tunnel_handler_post_server(int event, HttpTunnelConsumer *c);
  int tunnel_handler_trailer_ua(int event, HttpTunnelConsumer *c);
  int tunnel_handler_trailer_server(int event, HttpTunnelProducer *c);
  int tunnel_handler_ssl_producer(int event, HttpTunnelProducer *p);
  int tunnel_handler_ssl_consumer(int event, HttpTunnelConsumer *p);
  int tunnel_handler_transform_write(int event, HttpTunnelConsumer *c);
  int tunnel_handler_transform_read(int event, HttpTunnelProducer *p);
  int tunnel_handler_plugin_agent(int event, HttpTunnelConsumer *c);

  void do_hostdb_lookup();
  void do_hostdb_reverse_lookup();
  void do_cache_lookup_and_read();
  void do_http_server_open(bool raw = false, bool only_direct = false);
  bool apply_ip_allow_filter();
  bool ip_allow_is_request_forbidden(const IpAllow::ACL &acl);
  void ip_allow_deny_request(const IpAllow::ACL &acl);
  bool grab_pre_warmed_net_v_connection_if_possible(const TLSTunnelSupport &tts, int pid);
  bool is_prewarm_enabled_or_sni_overridden(const TLSTunnelSupport &tts) const;
  void open_prewarmed_connection();
  void send_origin_throttled_response();
  void do_setup_client_request_body_tunnel(HttpVC_t to_vc_type);
  void do_cache_prepare_write();
  void do_cache_prepare_write_transform();
  void do_cache_prepare_update();
  void do_cache_prepare_action(HttpCacheSM *c_sm, CacheHTTPInfo *object_read_info, bool retry, bool allow_multiple = false);
  void do_cache_delete_all_alts(Continuation *cont);
  void do_auth_callout();
  int  do_api_callout();
  int  do_api_callout_internal();
  void do_redirect();
  void redirect_request(const char *redirect_url, const int redirect_len);
  void do_drain_request_body(HTTPHdr &response);

  void wait_for_full_body();

  virtual void        handle_api_return();
  void                handle_server_setup_error(int event, void *data);
  void                handle_http_server_open();
  void                handle_post_failure();
  void                mark_host_failure(ResolveInfo *info, ts_time time_down);
  void                release_server_session(bool serve_from_cache = false);
  void                set_ua_abort(HttpTransact::AbortState_t ua_abort, int event);
  int                 write_header_into_buffer(HTTPHdr *h, MIOBuffer *b);
  int                 write_response_header_into_buffer(HTTPHdr *h, MIOBuffer *b);
  void                setup_blind_tunnel_port();
  void                setup_client_read_request_header();
  void                setup_push_read_response_header();
  void                setup_server_read_response_header();
  void                setup_cache_lookup_complete_api();
  void                setup_server_send_request();
  void                setup_server_send_request_api();
  HttpTunnelProducer *setup_server_transfer();
  HttpTunnelProducer *setup_cache_read_transfer();
  void                setup_internal_transfer(HttpSMHandler handler);
  void                setup_error_transfer();

  /** Prepare for sending both the 100 Continue and the second response header.
   *
   * This function sets up the tunnel to send the 100 Continue response and
   * then prepares the state machine to send the second response that comes
   * after the body is sent.
   */
  void                setup_100_continue_transfer();
  HttpTunnelProducer *setup_push_transfer_to_cache();
  void                setup_transform_to_server_transfer();
  void setup_cache_write_transfer(HttpCacheSM *c_sm, VConnection *source_vc, HTTPInfo *store_info, int64_t skip_bytes,
                                  const char *name);
  void issue_cache_update();
  void perform_cache_write_action();
  void perform_transform_cache_write_action();
  void setup_blind_tunnel(bool send_response_hdr, IOBufferReader *initial = nullptr);
  void setup_tunnel_handler_trailer(HttpTunnelProducer *p);
  HttpTunnelProducer *setup_server_transfer_to_transform();
  HttpTunnelProducer *setup_transfer_from_transform();
  HttpTunnelProducer *setup_cache_transfer_to_transform();

  /** Configure consumers for client response transform plugins.
   *
   * @param[in] p The Tunnel's producer for whom transform plugins' consumers
   *   will be configured.
   * @param[in] num_header_bytes The number of header bytes in the stream.
   *   These will be skipped and not passed to the consumers of the data sink.
   */
  void setup_client_response_plugin_agents(HttpTunnelProducer *p, int num_header_bytes = 0);

  /** Configure consumers for client request transform plugins.
   *
   * @param[in] p The Tunnel's producer for whom transform plugins' consumers
   *   will be configured.
   * @param[in] num_header_bytes The number of header bytes in the stream.
   *   These will be skipped and not passed to the consumers of the data sink.
   */
  void setup_client_request_plugin_agents(HttpTunnelProducer *p, int num_header_bytes = 0);

  HttpTransact::StateMachineAction_t last_action     = HttpTransact::StateMachineAction_t::UNDEFINED;
  int (HttpSM::*m_last_state)(int event, void *data) = nullptr;
  virtual void set_next_state();
  void         call_transact_and_set_next_state(TransactEntryFunc_t f);

  bool    is_http_server_eos_truncation(HttpTunnelProducer *);
  bool    is_bg_fill_necessary(HttpTunnelConsumer *c);
  int     find_server_buffer_size();
  int     find_http_resp_buffer_size(int64_t cl);
  int64_t server_transfer_init(MIOBuffer *buf, int hdr_size);

  /// Update the milestones to track time spent in the plugin API.
  void milestone_update_api_time();

  sockaddr *get_server_remote_addr() const;
  int       get_request_method_wksidx() const;

public:
  // TODO:  Now that bodies can be empty, should the body counters be set to -1 ? TS-2213
  // Stats & Logging Info
  int     client_request_hdr_bytes        = 0;
  int     server_request_hdr_bytes        = 0;
  int     server_response_hdr_bytes       = 0;
  int     client_response_hdr_bytes       = 0;
  int     cache_response_hdr_bytes        = 0;
  int     pushed_response_hdr_bytes       = 0;
  int     server_connection_provided_cert = 0;
  int64_t client_request_body_bytes       = 0;
  int64_t server_request_body_bytes       = 0;
  int64_t server_response_body_bytes      = 0;
  int64_t client_response_body_bytes      = 0;
  int64_t cache_response_body_bytes       = 0;
  int64_t pushed_response_body_bytes      = 0;
  bool    is_internal                     = false;
  bool    server_ssl_reused               = false;
  bool    server_connection_is_ssl        = false;
  bool    is_waiting_for_full_body        = false;
  bool    is_buffering_request_body       = false;
  // hooks_set records whether there are any hooks relevant
  //  to this transaction.  Used to avoid costly calls
  //  do_api_callout_internal()
  bool                hooks_set = false;
  std::optional<bool> mptcp_state; // Don't initialize, that marks it as "not defined".
  const char         *server_protocol       = "-";
  int                 server_transact_count = 0;

  TransactionMilestones milestones;
  ink_hrtime            api_timer = 0;
  // The next two enable plugins to tag the state machine for
  // the purposes of logging so the instances can be correlated
  // with the source plugin.
  const char *plugin_tag = nullptr;
  int64_t     plugin_id  = 0;

private:
  HttpTunnel tunnel;

  HttpVCTable vc_table;

  HttpUserAgent     _ua{};
  HttpVCTableEntry *server_entry = nullptr;
  ProxyTransaction *server_txn   = nullptr;

  HttpTransformInfo transform_info;
  HttpTransformInfo post_transform_info;

  HttpCacheSM cache_sm;
  HttpCacheSM transform_cache_sm;

  HttpSMHandler default_handler = nullptr;
  PendingAction pending_action;
  Continuation *schedule_cont = nullptr;

  HTTPParser http_parser;

  TSHttpHookID   cur_hook_id = TS_HTTP_LAST_HOOK;
  APIHook const *cur_hook    = nullptr;
  HttpHookState  hook_state;

  // Continuation time keeper
  int64_t prev_hook_start_time = 0;

  int            reentrancy_count = 0;
  int            cur_hooks        = 0;
  HttpApiState_t callout_state    = HttpApiState_t::NO_CALLOUT;

  // api_hooks must not be changed directly
  //  Use txn_hook_{ap,pre}pend so hooks_set is
  //  updated
  HttpAPIHooks api_hooks;

  // The terminate flag is set by handlers and checked by the
  //   main handler who will terminate the state machine
  //   when the flag is set
  bool terminate_sm         = false;
  bool kill_this_async_done = false;
  bool parse_range_done     = false;
  bool _from_early_data     = false;
  /* Because we don't want to take a session from a shared pool if we know that it will be private,
   * but we cannot set it to private until we have an attached server session.
   * So we use this variable to indicate that
   * we should create a new connection and then once we attach the session we'll mark it as private.
   */
  bool will_be_private_ss = false;

  /** A flag to keep track of whether there are active request plugin agents.
   *
   * This is used to guide plugin agent cleanup.
   */
  bool has_active_request_plugin_agents = false;

  /** A flag to keep track of whether there are active response plugin agents.
   *
   * This is used to guide plugin agent cleanup.
   */
  bool has_active_response_plugin_agents = false;

  SNIRoutingType  _tunnel_type = SNIRoutingType::NONE;
  PreWarmSM      *_prewarm_sm  = nullptr;
  PostDataBuffers _postbuf;
  NetVConnection *_netvc             = nullptr;
  IOBufferReader *_netvc_reader      = nullptr;
  MIOBuffer      *_netvc_read_buffer = nullptr;

  void kill_this();
  void update_stats();
  void transform_cleanup(TSHttpHookID hook, HttpTransformInfo *info);
  bool is_transparent_passthrough_allowed();
  void plugin_agents_cleanup();

public:
  LINK(HttpSM, debug_link);
  bool set_server_session_private(bool private_session);
  bool is_dying() const;

  int client_connection_id() const;
  int client_transaction_id() const;
  int client_transaction_priority_weight() const;
  int client_transaction_priority_dependence() const;

  ink_hrtime get_server_inactivity_timeout();
  ink_hrtime get_server_active_timeout();
  ink_hrtime get_server_connect_timeout();
  void       rewind_state_machine();

private:
  void cancel_pending_server_connection();
};

////
// Inline Functions
//

// This can't be defined until HttpSM is defined.
//
inline int64_t
HttpTransact::State::state_machine_id() const
{
  ink_assert(state_machine != nullptr);

  return state_machine->sm_id;
}

inline HttpUserAgent const &
HttpSM::get_user_agent() const
{
  return _ua;
}

inline ProxyTransaction *
HttpSM::get_ua_txn()
{
  return _ua.get_txn();
}

inline ProxyTransaction *
HttpSM::get_server_txn()
{
  return server_txn;
}

inline bool
HttpSM::is_post_transform_request()
{
  return t_state.method == HTTP_WKSIDX_POST && post_transform_info.vc;
}

inline bool
HttpSM::is_dying() const
{
  return terminate_sm;
}

inline int
HttpSM::client_connection_id() const
{
  return _ua.get_client_connection_id();
}

inline int
HttpSM::client_transaction_id() const
{
  return _ua.get_client_transaction_id();
}

inline int
HttpSM::client_transaction_priority_weight() const
{
  return _ua.get_client_transaction_priority_weight();
}

inline int
HttpSM::client_transaction_priority_dependence() const
{
  return _ua.get_client_transaction_priority_dependence();
}

// Function to get the cache_sm object - YTS Team, yamsat
inline HttpCacheSM &
HttpSM::get_cache_sm()
{
  return cache_sm;
}

inline int
HttpSM::write_response_header_into_buffer(HTTPHdr *h, MIOBuffer *b)
{
  if (t_state.client_info.http_version == HTTPVersion(0, 9)) {
    return 0;
  } else {
    return write_header_into_buffer(h, b);
  }
}

inline int
HttpSM::find_server_buffer_size()
{
  return find_http_resp_buffer_size(t_state.hdr_info.response_content_length);
}

inline void
HttpSM::txn_hook_add(TSHttpHookID id, INKContInternal *cont)
{
  api_hooks.append(id, cont);
  hooks_set = true;
}

inline APIHook *
HttpSM::txn_hook_get(TSHttpHookID id)
{
  return api_hooks.get(id);
}

inline bool
HttpSM::is_transparent_passthrough_allowed()
{
  return (t_state.client_info.is_transparent && _ua.get_txn()->is_transparent_passthrough_allowed() &&
          _ua.get_txn()->is_first_transaction());
}

inline int64_t
HttpSM::postbuf_reader_avail()
{
  return this->_postbuf.ua_buffer_reader->read_avail();
}

inline int64_t
HttpSM::postbuf_buffer_avail()
{
  return this->_postbuf.postdata_copy_buffer_start->read_avail();
}

inline void
HttpSM::postbuf_clear()
{
  this->_postbuf.clear();
}

inline void
HttpSM::disable_redirect()
{
  this->enable_redirection = false;
  this->_postbuf.clear();
}

inline int64_t
HttpSM::postbuf_copy_partial_data(int64_t consumed_bytes)
{
  return this->_postbuf.copy_partial_post_data(consumed_bytes);
}

inline void
HttpSM::postbuf_init(IOBufferReader *ua_reader)
{
  this->_postbuf.init(ua_reader);
}

inline void
HttpSM::set_postbuf_done(bool done)
{
  this->_postbuf.set_post_data_buffer_done(done);
}

inline bool
HttpSM::get_postbuf_done()
{
  return this->_postbuf.get_post_data_buffer_done();
}

inline bool
HttpSM::is_postbuf_valid()
{
  return this->_postbuf.is_valid();
}

inline IOBufferReader *
HttpSM::get_postbuf_clone_reader()
{
  return this->_postbuf.get_post_data_buffer_clone_reader();
}

inline sockaddr *
HttpSM::get_server_remote_addr() const
{
  return &t_state.current.server->dst_addr.sa;
};

inline int
HttpSM::get_request_method_wksidx() const
{
  return t_state.hdr_info.server_request.method_get_wksidx();
};
