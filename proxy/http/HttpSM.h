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
#include "P_EventSystem.h"
#include "HttpCacheSM.h"
#include "HttpTransact.h"
#include "UrlRewrite.h"
#include "HttpTunnel.h"
#include "InkAPIInternal.h"
#include "../ProxyTransaction.h"
#include "HdrUtils.h"
#include "tscore/History.h"

#define HTTP_API_CONTINUE (INK_API_EVENT_EVENTS_START + 0)
#define HTTP_API_ERROR (INK_API_EVENT_EVENTS_START + 1)

// The default size for http header buffers when we don't
//   need to include extra space for the document
static size_t const HTTP_HEADER_BUFFER_SIZE_INDEX = CLIENT_CONNECTION_FIRST_READ_BUFFER_SIZE_INDEX;

// We want to use a larger buffer size when reading response
//   headers from the origin server since we want to get
//   as much of the document as possible on the first read
//   Marco benchmarked about 3% ops/second improvement using
//   the larger buffer size
static size_t const HTTP_SERVER_RESP_HDR_BUFFER_INDEX = BUFFER_SIZE_INDEX_8K;

class Http1ServerSession;
class AuthHttpAdapter;

class HttpSM;
typedef int (HttpSM::*HttpSMHandler)(int event, void *data);

enum HttpVC_t {
  HTTP_UNKNOWN = 0,
  HTTP_UA_VC,
  HTTP_SERVER_VC,
  HTTP_TRANSFORM_VC,
  HTTP_CACHE_READ_VC,
  HTTP_CACHE_WRITE_VC,
  HTTP_RAW_SERVER_VC
};

enum BackgroundFill_t {
  BACKGROUND_FILL_NONE = 0,
  BACKGROUND_FILL_STARTED,
  BACKGROUND_FILL_ABORTED,
  BACKGROUND_FILL_COMPLETED,
};

extern ink_mutex debug_sm_list_mutex;

struct HttpVCTableEntry {
  VConnection *vc;
  MIOBuffer *read_buffer;
  MIOBuffer *write_buffer;
  VIO *read_vio;
  VIO *write_vio;
  HttpSMHandler vc_handler;
  HttpVC_t vc_type;
  HttpSM *sm;
  bool eos;
  bool in_tunnel;
};

struct HttpVCTable {
  static const int vc_table_max_entries = 4;
  explicit HttpVCTable(HttpSM *);

  HttpVCTableEntry *new_entry();
  HttpVCTableEntry *find_entry(VConnection *);
  HttpVCTableEntry *find_entry(VIO *);
  void remove_entry(HttpVCTableEntry *);
  void cleanup_entry(HttpVCTableEntry *);
  void cleanup_all();
  bool is_table_clear() const;

private:
  HttpVCTableEntry vc_table[vc_table_max_entries];
  HttpSM *sm = nullptr;
};

inline bool
HttpVCTable::is_table_clear() const
{
  for (const auto &i : vc_table) {
    if (i.vc != nullptr) {
      return false;
    }
  }
  return true;
}

struct HttpTransformInfo {
  HttpVCTableEntry *entry = nullptr;
  VConnection *vc         = nullptr;

  HttpTransformInfo() {}
};

enum {
  HTTP_SM_MAGIC_ALIVE = 0x0000FEED,
  HTTP_SM_MAGIC_DEAD  = 0xDEADFEED,
};

enum {
  HTTP_SM_POST_UNKNOWN     = 0,
  HTTP_SM_POST_UA_FAIL     = 1,
  HTTP_SM_POST_SERVER_FAIL = 2,
  HTTP_SM_POST_SUCCESS     = 3,
};

enum {
  HTTP_SM_TRANSFORM_OPEN   = 0,
  HTTP_SM_TRANSFORM_CLOSED = 1,
  HTTP_SM_TRANSFORM_FAIL   = 2,
};

enum HttpApiState_t {
  HTTP_API_NO_CALLOUT,
  HTTP_API_IN_CALLOUT,
  HTTP_API_DEFERED_CLOSE,
  HTTP_API_DEFERED_SERVER_ERROR,
};

enum HttpPluginTunnel_t {
  HTTP_NO_PLUGIN_TUNNEL = 0,
  HTTP_PLUGIN_AS_SERVER,
  HTTP_PLUGIN_AS_INTERCEPT,
};

class CoreUtils;
class PluginVCCore;

class PostDataBuffers
{
public:
  PostDataBuffers() { Debug("http_redirect", "[PostDataBuffers::PostDataBuffers]"); }
  MIOBuffer *postdata_copy_buffer            = nullptr;
  IOBufferReader *postdata_copy_buffer_start = nullptr;
  IOBufferReader *ua_buffer_reader           = nullptr;
  bool post_data_buffer_done                 = false;

  void clear();
  void init(IOBufferReader *ua_reader);
  void copy_partial_post_data();
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

class HttpSM : public Continuation
{
  friend class HttpPagesHandler;
  friend class CoreUtils;

public:
  HttpSM();
  void cleanup();
  virtual void destroy();

  static HttpSM *allocate();
  HttpCacheSM &get_cache_sm();          // Added to get the object of CacheSM YTS Team, yamsat
  HttpVCTableEntry *get_ua_entry();     // Added to get the ua_entry pointer  - YTS-TEAM
  HttpVCTableEntry *get_server_entry(); // Added to get the server_entry pointer

  void init();

  void attach_client_session(ProxyTransaction *client_vc_arg, IOBufferReader *buffer_reader);

  // Called by httpSessionManager so that we can reset
  //  the session timeouts and initiate a read while
  //  holding the lock for the server session
  void attach_server_session(Http1ServerSession *s);

  // Used to read attributes of
  // the current active server session
  Http1ServerSession *
  get_server_session()
  {
    return server_session;
  }

  ProxyTransaction *
  get_ua_txn()
  {
    return ua_txn;
  }

  // Called by transact.  Updates are fire and forget
  //  so there are no callbacks and are safe to do
  //  directly from transact
  void do_hostdb_update_if_necessary();

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
  void process_hostdb_info(HostDBInfo *r);
  void process_srv_info(HostDBInfo *r);

  // Called by transact.  Synchronous.
  VConnection *do_transform_open();
  VConnection *do_post_transform_open();

  // Called by transact(HttpTransact::is_request_retryable), temporarily.
  // This function should be remove after #1994 fixed.
  bool
  is_post_transform_request()
  {
    return t_state.method == HTTP_WKSIDX_POST && post_transform_info.vc;
  }

  // Called from InkAPI.cc which acquires the state machine lock
  //  before calling
  int state_api_callback(int event, void *data);
  int state_api_callout(int event, void *data);

  // Used for Http Stat Pages
  HttpTunnel *
  get_tunnel()
  {
    return &tunnel;
  }

  // Debugging routines to dump the SM history, hdrs
  void dump_state_on_assert();
  void dump_state_hdr(HTTPHdr *h, const char *s);

  // Functions for manipulating api hooks
  void txn_hook_add(TSHttpHookID id, INKContInternal *cont);
  APIHook *txn_hook_get(TSHttpHookID id);

  bool is_private();
  bool is_redirect_required();

  /// Get the protocol stack for the inbound (client, user agent) connection.
  /// @arg result [out] Array to store the results
  /// @arg n [in] Size of the array @a result.
  int populate_client_protocol(std::string_view *result, int n) const;
  const char *client_protocol_contains(std::string_view tag_prefix) const;
  std::string_view find_proto_string(HTTPVersion version) const;

  int64_t sm_id      = -1;
  unsigned int magic = HTTP_SM_MAGIC_DEAD;

  // YTS Team, yamsat Plugin
  bool enable_redirection = false; // To check if redirection is enabled
  bool post_failed        = false; // Added to identify post failure
  bool debug_on           = false; // Transaction specific debug flag
  char *redirect_url    = nullptr; // url for force redirect (provide users a functionality to redirect to another url when needed)
  int redirect_url_len  = 0;
  int redirection_tries = 0;    // To monitor number of redirections
  int64_t transfered_bytes = 0; // Added to calculate POST data

  // Tunneling request to plugin
  HttpPluginTunnel_t plugin_tunnel_type = HTTP_NO_PLUGIN_TUNNEL;
  PluginVCCore *plugin_tunnel           = nullptr;

  HttpTransact::State t_state;

  // This unfortunately can't go into the t_state, because of circular dependencies. We could perhaps refactor
  // this, with a lot of work, but this is easier for now.
  UrlRewrite *m_remap = nullptr;

  // _postbuf api
  int64_t postbuf_reader_avail();
  int64_t postbuf_buffer_avail();
  void postbuf_clear();
  void disable_redirect();
  void postbuf_copy_partial_data();
  void postbuf_init(IOBufferReader *ua_reader);
  void set_postbuf_done(bool done);
  IOBufferReader *get_postbuf_clone_reader();
  bool get_postbuf_done();
  bool is_postbuf_valid();

protected:
  int reentrancy_count = 0;

  HttpTunnel tunnel;

  HttpVCTable vc_table;

  HttpVCTableEntry *ua_entry = nullptr;
  void remove_ua_entry();

public:
  ProxyTransaction *ua_txn         = nullptr;
  BackgroundFill_t background_fill = BACKGROUND_FILL_NONE;
  void set_http_schedule(Continuation *);
  int get_http_schedule(int event, void *data);

  History<HISTORY_DEFAULT_SIZE> history;

protected:
  IOBufferReader *ua_buffer_reader     = nullptr;
  IOBufferReader *ua_raw_buffer_reader = nullptr;

  HttpVCTableEntry *server_entry     = nullptr;
  Http1ServerSession *server_session = nullptr;

  /* Because we don't want to take a session from a shared pool if we know that it will be private,
   * but we cannot set it to private until we have an attached server session.
   * So we use this variable to indicate that
   * we should create a new connection and then once we attach the session we'll mark it as private.
   */
  bool will_be_private_ss              = false;
  int shared_session_retries           = 0;
  IOBufferReader *server_buffer_reader = nullptr;
  void remove_server_entry();

  HttpTransformInfo transform_info;
  HttpTransformInfo post_transform_info;
  /// Set if plugin client / user agents are active.
  /// Need primarily for cleanup.
  bool has_active_plugin_agents = false;

  HttpCacheSM cache_sm;
  HttpCacheSM transform_cache_sm;

  HttpSMHandler default_handler = nullptr;
  Action *pending_action        = nullptr;
  Continuation *schedule_cont   = nullptr;

  HTTPParser http_parser;
  void start_sub_sm();

  int main_handler(int event, void *data);
  int tunnel_handler(int event, void *data);
  int tunnel_handler_push(int event, void *data);
  int tunnel_handler_post(int event, void *data);

  // YTS Team, yamsat Plugin
  int tunnel_handler_for_partial_post(int event, void *data);

  void tunnel_handler_post_or_put(HttpTunnelProducer *p);

  int tunnel_handler_100_continue(int event, void *data);
  int tunnel_handler_cache_fill(int event, void *data);
  int state_read_client_request_header(int event, void *data);
  int state_watch_for_client_abort(int event, void *data);
  int state_read_push_response_header(int event, void *data);
  int state_hostdb_lookup(int event, void *data);
  int state_hostdb_reverse_lookup(int event, void *data);
  int state_mark_os_down(int event, void *data);
  int state_handle_stat_page(int event, void *data);
  int state_auth_callback(int event, void *data);
  int state_add_to_list(int event, void *data);
  int state_remove_from_list(int event, void *data);

  // Y! ebalsa: remap handlers
  int state_remap_request(int event, void *data);
  void do_remap_request(bool);

  // Cache Handlers
  int state_cache_open_read(int event, void *data);
  int state_cache_open_write(int event, void *data);

  // Http Server Handlers
  int state_http_server_open(int event, void *data);
  int state_raw_http_server_open(int event, void *data);
  int state_send_server_request_header(int event, void *data);
  int state_acquire_server_read(int event, void *data);
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
  int tunnel_handler_ssl_producer(int event, HttpTunnelProducer *p);
  int tunnel_handler_ssl_consumer(int event, HttpTunnelConsumer *p);
  int tunnel_handler_transform_write(int event, HttpTunnelConsumer *c);
  int tunnel_handler_transform_read(int event, HttpTunnelProducer *p);
  int tunnel_handler_plugin_agent(int event, HttpTunnelConsumer *c);

  void do_hostdb_lookup();
  void do_hostdb_reverse_lookup();
  void do_cache_lookup_and_read();
  void do_http_server_open(bool raw = false);
  void send_origin_throttled_response();
  void do_setup_post_tunnel(HttpVC_t to_vc_type);
  void do_cache_prepare_write();
  void do_cache_prepare_write_transform();
  void do_cache_prepare_update();
  void do_cache_prepare_action(HttpCacheSM *c_sm, CacheHTTPInfo *object_read_info, bool retry, bool allow_multiple = false);
  void do_cache_delete_all_alts(Continuation *cont);
  void do_auth_callout();
  void do_api_callout();
  void do_api_callout_internal();
  void do_redirect();
  void redirect_request(const char *redirect_url, const int redirect_len);
  void do_drain_request_body(HTTPHdr &response);

  void wait_for_full_body();

  virtual void handle_api_return();
  void handle_server_setup_error(int event, void *data);
  void handle_http_server_open();
  void handle_post_failure();
  void mark_host_failure(HostDBInfo *info, time_t time_down);
  void mark_server_down_on_client_abort();
  void release_server_session(bool serve_from_cache = false);
  void set_ua_abort(HttpTransact::AbortState_t ua_abort, int event);
  int write_header_into_buffer(HTTPHdr *h, MIOBuffer *b);
  int write_response_header_into_buffer(HTTPHdr *h, MIOBuffer *b);
  void setup_blind_tunnel_port();
  void setup_client_header_nca();
  void setup_client_read_request_header();
  void setup_push_read_response_header();
  void setup_server_read_response_header();
  void setup_cache_lookup_complete_api();
  void setup_server_send_request();
  void setup_server_send_request_api();
  HttpTunnelProducer *setup_server_transfer();
  void setup_server_transfer_to_cache_only();
  HttpTunnelProducer *setup_cache_read_transfer();
  void setup_internal_transfer(HttpSMHandler handler);
  void setup_error_transfer();
  void setup_100_continue_transfer();
  HttpTunnelProducer *setup_push_transfer_to_cache();
  void setup_transform_to_server_transfer();
  void setup_cache_write_transfer(HttpCacheSM *c_sm, VConnection *source_vc, HTTPInfo *store_info, int64_t skip_bytes,
                                  const char *name);
  void issue_cache_update();
  void perform_cache_write_action();
  void perform_transform_cache_write_action();
  void perform_nca_cache_action();
  void setup_blind_tunnel(bool send_response_hdr, IOBufferReader *initial = nullptr);
  HttpTunnelProducer *setup_server_transfer_to_transform();
  HttpTunnelProducer *setup_transfer_from_transform();
  HttpTunnelProducer *setup_cache_transfer_to_transform();
  HttpTunnelProducer *setup_transfer_from_transform_to_cache_only();
  void setup_plugin_agents(HttpTunnelProducer *p);

  HttpTransact::StateMachineAction_t last_action     = HttpTransact::SM_ACTION_UNDEFINED;
  int (HttpSM::*m_last_state)(int event, void *data) = nullptr;
  virtual void set_next_state();
  void call_transact_and_set_next_state(TransactEntryFunc_t f);

  bool is_http_server_eos_truncation(HttpTunnelProducer *);
  bool is_bg_fill_necessary(HttpTunnelConsumer *c);
  int find_server_buffer_size();
  int find_http_resp_buffer_size(int64_t cl);
  int64_t server_transfer_init(MIOBuffer *buf, int hdr_size);

public:
  // TODO:  Now that bodies can be empty, should the body counters be set to -1 ? TS-2213
  // Stats & Logging Info
  int client_request_hdr_bytes       = 0;
  int64_t client_request_body_bytes  = 0;
  int server_request_hdr_bytes       = 0;
  int64_t server_request_body_bytes  = 0;
  int server_response_hdr_bytes      = 0;
  int64_t server_response_body_bytes = 0;
  int client_response_hdr_bytes      = 0;
  int64_t client_response_body_bytes = 0;
  int cache_response_hdr_bytes       = 0;
  int64_t cache_response_body_bytes  = 0;
  int pushed_response_hdr_bytes      = 0;
  int64_t pushed_response_body_bytes = 0;
  bool client_tcp_reused             = false;
  bool client_ssl_reused             = false;
  bool client_connection_is_ssl      = false;
  bool is_internal                   = false;
  bool server_connection_is_ssl      = false;
  bool is_waiting_for_full_body      = false;
  bool is_using_post_buffer          = false;
  std::optional<bool> mptcp_state; // Don't initialize, that marks it as "not defined".
  const char *client_protocol     = "-";
  const char *client_sec_protocol = "-";
  const char *client_cipher_suite = "-";
  const char *client_curve        = "-";
  int server_transact_count       = 0;

  TransactionMilestones milestones;
  ink_hrtime api_timer = 0;
  // The next two enable plugins to tag the state machine for
  // the purposes of logging so the instances can be correlated
  // with the source plugin.
  const char *plugin_tag = nullptr;
  int64_t plugin_id      = 0;

  // hooks_set records whether there are any hooks relevant
  //  to this transaction.  Used to avoid costly calls
  //  do_api_callout_internal()
  bool hooks_set = false;

protected:
  TSHttpHookID cur_hook_id = TS_HTTP_LAST_HOOK;
  APIHook const *cur_hook  = nullptr;
  HttpHookState hook_state;

  //
  // Continuation time keeper
  int64_t prev_hook_start_time = 0;

  int cur_hooks                = 0;
  HttpApiState_t callout_state = HTTP_API_NO_CALLOUT;

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
  virtual int kill_this_async_hook(int event, void *data);
  void kill_this();
  void update_stats();
  void transform_cleanup(TSHttpHookID hook, HttpTransformInfo *info);
  bool is_transparent_passthrough_allowed();
  void plugin_agents_cleanup();

public:
  LINK(HttpSM, debug_link);

public:
  bool set_server_session_private(bool private_session);
  bool
  is_dying() const
  {
    return terminate_sm;
  }

  int
  client_connection_id() const
  {
    return _client_connection_id;
  }

  int
  client_transaction_id() const
  {
    return _client_transaction_id;
  }

  void set_server_netvc_inactivity_timeout(NetVConnection *netvc);
  void set_server_netvc_active_timeout(NetVConnection *netvc);
  void set_server_netvc_connect_timeout(NetVConnection *netvc);

private:
  PostDataBuffers _postbuf;
  int _client_connection_id = -1, _client_transaction_id = -1;
};

// Function to get the cache_sm object - YTS Team, yamsat
inline HttpCacheSM &
HttpSM::get_cache_sm()
{
  return cache_sm;
}

// Function to get the ua_entry pointer - YTS Team, yamsat
inline HttpVCTableEntry *
HttpSM::get_ua_entry()
{
  return ua_entry;
}

inline HttpVCTableEntry *
HttpSM::get_server_entry()
{
  return server_entry;
}

inline HttpSM *
HttpSM::allocate()
{
  extern ClassAllocator<HttpSM> httpSMAllocator;
  return httpSMAllocator.alloc();
}

inline void
HttpSM::remove_ua_entry()
{
  vc_table.remove_entry(ua_entry);
  ua_entry = nullptr;
}

inline void
HttpSM::remove_server_entry()
{
  if (server_entry) {
    vc_table.remove_entry(server_entry);
    server_entry = nullptr;
  }
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
  return (t_state.client_info.is_transparent && ua_txn->is_transparent_passthrough_allowed() && ua_txn->is_first_transaction());
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

inline void
HttpSM::postbuf_copy_partial_data()
{
  this->_postbuf.copy_partial_post_data();
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
