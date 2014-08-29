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

#ifndef _HTTP_SM_H_
#define _HTTP_SM_H_

#include "libts.h"
#include "P_EventSystem.h"
#include "HttpCacheSM.h"
#include "HttpTransact.h"
#include "HttpTunnel.h"
#include "InkAPIInternal.h"
#include "StatSystem.h"
#include "HttpClientSession.h"
#include "HdrUtils.h"
//#include "AuthHttpAdapter.h"

/* Enable LAZY_BUF_ALLOC to delay allocation of buffers until they
 * are actually required.
 * Enabling LAZY_BUF_ALLOC, stop Http code from allocation space
 * for header buffer and tunnel buffer. The allocation is done by
 * the net code in read_from_net when data is actually written into
 * the buffer. By allocating memory only when it is required we can
 * reduce the memory consumed by TS process.
 *
 * IMPORTANT NOTE: enable/disable LAZY_BUF_ALLOC in HttpServerSession.h
 * as well.
 */
#define LAZY_BUF_ALLOC

#define HTTP_API_CONTINUE   (INK_API_EVENT_EVENTS_START + 0)
#define HTTP_API_ERROR      (INK_API_EVENT_EVENTS_START + 1)

// The default size for http header buffers when we don't
//   need to include extra space for the document
static size_t const HTTP_HEADER_BUFFER_SIZE_INDEX = CLIENT_CONNECTION_FIRST_READ_BUFFER_SIZE_INDEX;

// We want to use a larger buffer size when reading response
//   headers from the origin server since we want to get
//   as much of the document as possible on the first read
//   Marco benchmarked about 3% ops/second improvement using
//   the larger buffer size
static size_t const HTTP_SERVER_RESP_HDR_BUFFER_INDEX = BUFFER_SIZE_INDEX_8K;

class HttpServerSession;
class AuthHttpAdapter;

class HttpSM;
typedef int (HttpSM::*HttpSMHandler) (int event, void *data);

enum HttpVC_t
{ HTTP_UNKNOWN = 0, HTTP_UA_VC, HTTP_SERVER_VC,
  HTTP_TRANSFORM_VC, HTTP_CACHE_READ_VC,
  HTTP_CACHE_WRITE_VC, HTTP_RAW_SERVER_VC
};

enum BackgroundFill_t
{
  BACKGROUND_FILL_NONE = 0,
  BACKGROUND_FILL_STARTED,
  BACKGROUND_FILL_ABORTED,
  BACKGROUND_FILL_COMPLETED
};

extern ink_mutex debug_sm_list_mutex;

struct HttpVCTableEntry
{
  VConnection *vc;
  MIOBuffer *read_buffer;
  MIOBuffer *write_buffer;
  VIO *read_vio;
  VIO *write_vio;
  HttpSMHandler vc_handler;
  HttpVC_t vc_type;
  bool eos;
  bool in_tunnel;
};

struct HttpVCTable
{
  static const int vc_table_max_entries = 4;
  HttpVCTable();

  HttpVCTableEntry *new_entry();
  HttpVCTableEntry *find_entry(VConnection *);
  HttpVCTableEntry *find_entry(VIO *);
  void remove_entry(HttpVCTableEntry *);
  void cleanup_entry(HttpVCTableEntry *);
  void cleanup_all();
  bool is_table_clear() const;

private:
  HttpVCTableEntry vc_table[vc_table_max_entries];
};

inline bool
HttpVCTable::is_table_clear() const
{
  for (int i = 0; i < vc_table_max_entries; i++) {
    if (vc_table[i].vc != NULL) {
      return false;
    }
  }
  return true;
}

struct HttpTransformInfo
{
  HttpVCTableEntry *entry;
  VConnection *vc;

    HttpTransformInfo():entry(NULL), vc(NULL)
  {
  }
};
#define HISTORY_SIZE  64

enum
{
  HTTP_SM_MAGIC_ALIVE = 0x0000FEED,
  HTTP_SM_MAGIC_DEAD = 0xDEADFEED
};

enum
{
  HTTP_SM_POST_UNKNOWN = 0,
  HTTP_SM_POST_UA_FAIL = 1,
  HTTP_SM_POST_SERVER_FAIL = 2,
  HTTP_SM_POST_SUCCESS = 3
};

enum
{
  HTTP_SM_TRANSFORM_OPEN = 0,
  HTTP_SM_TRANSFORM_CLOSED = 1,
  HTTP_SM_TRANSFORM_FAIL = 2
};

enum HttpApiState_t
{
  HTTP_API_NO_CALLOUT,
  HTTP_API_IN_CALLOUT,
  HTTP_API_DEFERED_CLOSE,
  HTTP_API_DEFERED_SERVER_ERROR
};


enum HttpPluginTunnel_t
{
  HTTP_NO_PLUGIN_TUNNEL = 0,
  HTTP_PLUGIN_AS_SERVER,
  HTTP_PLUGIN_AS_INTERCEPT
};

class CoreUtils;
class PluginVCCore;

class HttpSM: public Continuation
{
  friend class HttpPagesHandler;
  friend class CoreUtils;
public:
  HttpSM();
  void cleanup();
  virtual void destroy();

  static HttpSM *allocate();
  HttpCacheSM & get_cache_sm();       //Added to get the object of CacheSM YTS Team, yamsat
  HttpVCTableEntry *get_ua_entry();     //Added to get the ua_entry pointer  - YTS-TEAM
  static void _instantiate_func(HttpSM * prototype, HttpSM * new_instance);
  static void _make_scatter_list(HttpSM * prototype);

  void init();

  void attach_client_session(HttpClientSession * client_vc_arg, IOBufferReader * buffer_reader);

  // Called by httpSessionManager so that we can reset
  //  the session timeouts and initiate a read while
  //  holding the lock for the server session
  void attach_server_session(HttpServerSession * s);

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
  void parse_range_and_compare(MIMEField*, int64_t);
  
  // Called by transact to prevent reset problems
  //  failed PUSH requests
  void set_ua_half_close_flag();

  // Called by either state_hostdb_lookup() or directly
  //   by the HostDB in the case of inline completion
  // Handles the setting of all state necessary before
  //   calling transact to process the hostdb lookup
  // A NULL 'r' argument indicates the hostdb lookup failed
  void process_hostdb_info(HostDBInfo * r);
  void process_srv_info(HostDBInfo * r);

  // Called by transact.  Synchronous.
  VConnection *do_transform_open();
  VConnection *do_post_transform_open();

  // Called from InkAPI.cc which acquires the state machine lock
  //  before calling
  int state_api_callback(int event, void *data);
  int state_api_callout(int event, void *data);

  // Used for Http Stat Pages
  HttpTunnel *get_tunnel()
  {
    return &tunnel;
  };

  // Debugging routines to dump the SM history, hdrs
  void dump_state_on_assert();
  void dump_state_hdr(HTTPHdr *h, const char *s);

  // Functions for manipulating api hooks
  void txn_hook_append(TSHttpHookID id, INKContInternal * cont);
  void txn_hook_prepend(TSHttpHookID id, INKContInternal * cont);
  APIHook *txn_hook_get(TSHttpHookID id);

  void add_history_entry(const char *fileline, int event, int reentrant);
  void add_cache_sm();
  bool is_private();
  bool is_redirect_required();

  int64_t sm_id;
  unsigned int magic;

  //YTS Team, yamsat Plugin
  bool enable_redirection;      //To check if redirection is enabled
  char *redirect_url;     //url for force redirect (provide users a functionality to redirect to another url when needed)
  int redirect_url_len;
  int redirection_tries;        //To monitor number of redirections
  int64_t transfered_bytes;     //Added to calculate POST data
  bool post_failed;             //Added to identify post failure
  bool debug_on;               //Transaction specific debug flag

  // Tunneling request to plugin
  HttpPluginTunnel_t plugin_tunnel_type;
  PluginVCCore *plugin_tunnel;

  HttpTransact::State t_state;

protected:
  int reentrancy_count;

  struct History
  {
    const char *fileline;
    unsigned short event;
    short reentrancy;
  };
  History history[HISTORY_SIZE];
  int history_pos;

  HttpTunnel tunnel;

  HttpVCTable vc_table;

  HttpVCTableEntry *ua_entry;
  void remove_ua_entry();

public:
  HttpClientSession *ua_session;
  BackgroundFill_t background_fill;
  //AuthHttpAdapter authAdapter;
  void set_http_schedule(Continuation *);
  int get_http_schedule(int event, void *data);

protected:
  IOBufferReader * ua_buffer_reader;
  IOBufferReader * ua_raw_buffer_reader;

  HttpVCTableEntry *server_entry;
  HttpServerSession *server_session;
  int shared_session_retries;
  IOBufferReader *server_buffer_reader;
  void remove_server_entry();

  HttpTransformInfo transform_info;
  HttpTransformInfo post_transform_info;
  /// Set if plugin client / user agents are active.
  /// Need primarily for cleanup.
  bool has_active_plugin_agents;

  HttpCacheSM cache_sm;
  HttpCacheSM transform_cache_sm;
  HttpCacheSM *second_cache_sm;

  HttpSMHandler default_handler;
  Action *pending_action;
  Action *historical_action;
  Continuation *schedule_cont;

  HTTPParser http_parser;
  void start_sub_sm();

  int main_handler(int event, void *data);
  int tunnel_handler(int event, void *data);
  int tunnel_handler_push(int event, void *data);
  int tunnel_handler_post(int event, void *data);

  //YTS Team, yamsat Plugin
  int tunnel_handler_for_partial_post(int event, void *data);

  void tunnel_handler_post_or_put(HttpTunnelProducer * p);

  int tunnel_handler_100_continue(int event, void *data);
  int tunnel_handler_cache_fill(int event, void *data);
#ifdef PROXY_DRAIN
  int state_drain_client_request_body(int event, void *data);
#endif /* PROXY_DRAIN */
  int state_read_client_request_header(int event, void *data);
  int state_watch_for_client_abort(int event, void *data);
  int state_read_push_response_header(int event, void *data);
  int state_srv_lookup(int event, void *data);
  int state_hostdb_lookup(int event, void *data);
  int state_hostdb_reverse_lookup(int event, void *data);
  int state_mark_os_down(int event, void *data);
  int state_handle_stat_page(int event, void *data);
  int state_icp_lookup(int event, void *data);
  int state_auth_callback(int event, void *data);
  int state_add_to_list(int event, void *data);
  int state_remove_from_list(int event, void *data);
  int state_congestion_control_lookup(int event, void *data);

//Y! ebalsa: remap handlers
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
  int state_common_wait_for_transform_read(HttpTransformInfo * t_info, HttpSMHandler tunnel_handler, int event, void *data);

  // Tunnel event handlers
  int tunnel_handler_server(int event, HttpTunnelProducer * p);
  int tunnel_handler_ua(int event, HttpTunnelConsumer * c);
  int tunnel_handler_ua_push(int event, HttpTunnelProducer * p);
  int tunnel_handler_100_continue_ua(int event, HttpTunnelConsumer * c);
  int tunnel_handler_cache_write(int event, HttpTunnelConsumer * c);
  int tunnel_handler_cache_read(int event, HttpTunnelProducer * p);
  int tunnel_handler_post_ua(int event, HttpTunnelProducer * c);
  int tunnel_handler_post_server(int event, HttpTunnelConsumer * c);
  int tunnel_handler_ssl_producer(int event, HttpTunnelProducer * p);
  int tunnel_handler_ssl_consumer(int event, HttpTunnelConsumer * p);
  int tunnel_handler_transform_write(int event, HttpTunnelConsumer * c);
  int tunnel_handler_transform_read(int event, HttpTunnelProducer * p);
  int tunnel_handler_plugin_agent(int event, HttpTunnelConsumer * c);

  void do_hostdb_lookup();
  void do_hostdb_reverse_lookup();
  void do_cache_lookup_and_read();
  void do_http_server_open(bool raw = false);
  void do_setup_post_tunnel(HttpVC_t to_vc_type);
  void do_cache_prepare_write();
  void do_cache_prepare_write_transform();
  void do_cache_prepare_update();
  void do_cache_prepare_action(HttpCacheSM * c_sm,
                               CacheHTTPInfo * object_read_info, bool retry, bool allow_multiple = false);
  void do_cache_delete_all_alts(Continuation * cont);
  void do_icp_lookup();
  void do_auth_callout();
  void do_api_callout();
  void do_api_callout_internal();
  void do_redirect();
  void redirect_request(const char *redirect_url, const int redirect_len);
#ifdef PROXY_DRAIN
  void do_drain_request_body();
#endif

  bool do_congestion_control_lookup();

  virtual void handle_api_return();
  void handle_server_setup_error(int event, void *data);
  void handle_http_server_open();
  void handle_post_failure();
  void mark_host_failure(HostDBInfo * info, time_t time_down);
  void mark_server_down_on_client_abort();
  void release_server_session(bool serve_from_cache = false);
  void set_ua_abort(HttpTransact::AbortState_t ua_abort, int event);
  int write_header_into_buffer(HTTPHdr * h, MIOBuffer * b);
  int write_response_header_into_buffer(HTTPHdr * h, MIOBuffer * b);
  void setup_blind_tunnel_port();
  void setup_client_header_nca();
  void setup_client_read_request_header();
  void setup_push_read_response_header();
  void setup_server_read_response_header();
  void setup_cache_lookup_complete_api();
  void setup_server_send_request();
  void setup_server_send_request_api();
  void setup_server_transfer();
  void setup_server_transfer_to_cache_only();
  void setup_cache_read_transfer();
  void setup_internal_transfer(HttpSMHandler handler);
  void setup_error_transfer();
  void setup_100_continue_transfer();
  void setup_push_transfer_to_cache();
  void setup_transform_to_server_transfer();
  void setup_cache_write_transfer(HttpCacheSM * c_sm,
                                  VConnection * source_vc, HTTPInfo * store_info, int64_t skip_bytes, const char *name);
  void issue_cache_update();
  void perform_cache_write_action();
  void perform_transform_cache_write_action();
  void perform_nca_cache_action();
  void setup_blind_tunnel(bool send_response_hdr);
  HttpTunnelProducer *setup_server_transfer_to_transform();
  HttpTunnelProducer *setup_transfer_from_transform();
  HttpTunnelProducer *setup_cache_transfer_to_transform();
  HttpTunnelProducer *setup_transfer_from_transform_to_cache_only();
  void setup_plugin_agents(HttpTunnelProducer* p);

  HttpTransact::StateMachineAction_t last_action;
  int (HttpSM::*m_last_state) (int event, void *data);
  virtual void set_next_state();
  void call_transact_and_set_next_state(TransactEntryFunc_t f);

  bool is_http_server_eos_truncation(HttpTunnelProducer *);
  bool is_bg_fill_necessary(HttpTunnelConsumer * c);
  int find_server_buffer_size();
  int find_http_resp_buffer_size(int64_t cl);
  int64_t server_transfer_init(MIOBuffer * buf, int hdr_size);

public:
  // Stats & Logging Info
  int client_request_hdr_bytes;
  int64_t client_request_body_bytes;
  int server_request_hdr_bytes;
  int64_t server_request_body_bytes;
  int server_response_hdr_bytes;
  int64_t server_response_body_bytes;
  int client_response_hdr_bytes;
  int64_t client_response_body_bytes;
  int cache_response_hdr_bytes;
  int64_t cache_response_body_bytes;
  int pushed_response_hdr_bytes;
  int64_t pushed_response_body_bytes;
  TransactionMilestones milestones;
  // The next two enable plugins to tag the state machine for
  // the purposes of logging so the instances can be correlated
  // with the source plugin.
  char const* plugin_tag;
  int64_t plugin_id;

  // hooks_set records whether there are any hooks relevant
  //  to this transaction.  Used to avoid costly calls
  //  do_api_callout_internal()
  bool hooks_set;

protected:
  TSHttpHookID cur_hook_id;
  APIHook *cur_hook;

  //
  // Continuation time keeper
  int64_t prev_hook_start_time;

  int cur_hooks;
  HttpApiState_t callout_state;

  // api_hooks must not be changed directly
  //  Use txn_hook_{ap,pre}pend so hooks_set is
  //  updated
  HttpAPIHooks api_hooks;

  // The terminate flag is set by handlers and checked by the
  //   main handler who will terminate the state machine
  //   when the flag is set
  bool terminate_sm;
  bool kill_this_async_done;
  virtual int kill_this_async_hook(int event, void *data);
  void kill_this();
  void update_stats();
  void transform_cleanup(TSHttpHookID hook, HttpTransformInfo * info);
  bool is_transparent_passthrough_allowed();
  void plugin_agents_cleanup();

public:
  LINK(HttpSM, debug_link);

public:
  bool set_server_session_private(bool private_session);
};

//Function to get the cache_sm object - YTS Team, yamsat
inline HttpCacheSM &
HttpSM::get_cache_sm()
{
  return cache_sm;
}

//Function to get the ua_entry pointer - YTS Team, yamsat
inline HttpVCTableEntry *
HttpSM::get_ua_entry()
{
  return ua_entry;
}

inline HttpSM *
HttpSM::allocate()
{
  extern SparseClassAllocator<HttpSM> httpSMAllocator;
  return httpSMAllocator.alloc();
}

inline void
HttpSM::remove_ua_entry()
{
  vc_table.remove_entry(ua_entry);
  ua_entry = NULL;
}

inline void
HttpSM::remove_server_entry()
{
  if (server_entry) {
    vc_table.remove_entry(server_entry);
    server_entry = NULL;
  }
}

inline int
HttpSM::write_response_header_into_buffer(HTTPHdr * h, MIOBuffer * b)
{
  if (t_state.client_info.http_version == HTTPVersion(0, 9)) {
    return 0;
  } else {
    return write_header_into_buffer(h, b);
  }
}

inline void
HttpSM::add_history_entry(const char *fileline, int event, int reentrant)
{
  int pos = history_pos++ % HISTORY_SIZE;
  history[pos].fileline = fileline;
  history[pos].event = (unsigned short) event;
  history[pos].reentrancy = (short) reentrant;
}

inline int
HttpSM::find_server_buffer_size()
{
  return find_http_resp_buffer_size(t_state.hdr_info.response_content_length);
}

inline void
HttpSM::txn_hook_append(TSHttpHookID id, INKContInternal * cont)
{
  api_hooks.append(id, cont);
  hooks_set = 1;
}

inline void
HttpSM::txn_hook_prepend(TSHttpHookID id, INKContInternal * cont)
{
  api_hooks.prepend(id, cont);
  hooks_set = 1;
}

inline APIHook *
HttpSM::txn_hook_get(TSHttpHookID id)
{
  return api_hooks.get(id);
}

inline void
HttpSM::add_cache_sm()
{
  if (second_cache_sm == NULL) {
    second_cache_sm = new HttpCacheSM;
    second_cache_sm->init(this, mutex);
    second_cache_sm->set_lookup_url(cache_sm.get_lookup_url());
    if (t_state.cache_info.object_read != NULL) {
      second_cache_sm->cache_read_vc = cache_sm.cache_read_vc;
      cache_sm.cache_read_vc = NULL;
      second_cache_sm->read_locked = cache_sm.read_locked;
      t_state.cache_info.second_object_read = t_state.cache_info.object_read;
      t_state.cache_info.object_read = NULL;
    }
  }
}

inline bool
HttpSM::is_transparent_passthrough_allowed()
{
  return (t_state.client_info.is_transparent &&
          ua_session->f_transparent_passthrough &&
          ua_session->get_transact_count() == 1);
}

#endif
