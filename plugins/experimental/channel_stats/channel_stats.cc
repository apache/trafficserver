/*
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

#include "ts/ink_platform.h"
#include "ts/ink_defs.h"

#include <cstdio>
#include <cstring>
#include <cctype>
#include <string>
#include <map> // may optimize by using hash_map, but mind compiler portability
#include <vector>
#include <algorithm>
#include <sstream>
#include <arpa/inet.h>
#ifdef HAVE_NETINET_IN_H
#include <netinet/in.h>
#endif

#include <ts/ts.h>
#if (TS_VERSION_NUMBER < 3003001)
// get TSHttpTxnStartTimeGet
#include <ts/experimental.h>
#endif

#include "debug_macros.h"

#define PLUGIN_NAME "channel_stats"
#define PLUGIN_VERSION "0.2"

#define MAX_SPEED 999999999

/* limit the number of channels (items) to avoid potential attack,
   regex_map rule can also generate infinite channels (hosts) */
#define MAX_MAP_SIZE 100000

static std::string api_path("_cstats");

// global stats
static uint64_t global_response_count_2xx_get = 0; // 2XX GET response count
static uint64_t global_response_bytes_content = 0; // transferred bytes

// channel stats
struct channel_stat {
  channel_stat() : response_bytes_content(0), response_count_2xx(0), response_count_5xx(0), speed_ua_bytes_per_sec_64k(0) {}
  inline void
  increment(uint64_t rbc, uint64_t rc2, uint64_t rc5, uint64_t sbps6)
  {
    if (rbc)
      __sync_fetch_and_add(&response_bytes_content, rbc);
    if (rc2)
      __sync_fetch_and_add(&response_count_2xx, rc2);
    if (rc5)
      __sync_fetch_and_add(&response_count_5xx, rc5);
    if (sbps6)
      __sync_fetch_and_add(&speed_ua_bytes_per_sec_64k, sbps6);
  }

  inline void
  debug_channel()
  {
    debug("response.bytes.content: %" PRIu64 "", response_bytes_content);
    debug("response.count.2xx: %" PRIu64 "", response_count_2xx);
    debug("response.count.5xx: %" PRIu64 "", response_count_5xx);
    debug("speed.ua.bytes_per_sec_64k: %" PRIu64 "", speed_ua_bytes_per_sec_64k);
  }

  uint64_t response_bytes_content;
  uint64_t response_count_2xx;
  uint64_t response_count_5xx;
  uint64_t speed_ua_bytes_per_sec_64k;
};

typedef std::map<std::string, channel_stat *> stats_map_t;
typedef stats_map_t::iterator smap_iterator;

static stats_map_t channel_stats;
static TSMutex stats_map_mutex;

// api Intercept Data
typedef struct intercept_state_t {
  TSVConn net_vc;
  TSVIO read_vio;
  TSVIO write_vio;

  TSIOBuffer req_buffer;
  TSIOBuffer resp_buffer;
  TSIOBufferReader resp_reader;

  int output_bytes;
  int body_written;

  int show_global; // default 0
  char *channel;   // default ""
  int topn;        // default -1
  int deny;        // default 0
} intercept_state;

struct private_seg_t {
  const in_addr_t net;
  const in_addr_t mask;
};

// don't put inet_addr("255.255.255.255"), see BUGS in 'man 3 inet_addr'
static struct private_seg_t private_segs[] = {{inet_addr("10.0.0.0"), inet_addr("255.0.0.0")},
                                              {inet_addr("127.0.0.0"), inet_addr("255.0.0.0")},
                                              {inet_addr("172.16.0.0"), inet_addr("255.240.0.0")},
                                              {inet_addr("192.168.0.0"), inet_addr("255.255.0.0")}};

static int num_private_segs = sizeof(private_segs) / sizeof(private_seg_t);

// all parameters are in network byte order
static bool
is_in_net(const in_addr_t addr, const in_addr_t netaddr, const in_addr_t netmask)
{
  return (addr & netmask) == (netaddr & netmask);
}

static bool
is_private_ip(const in_addr_t addr)
{
  for (int i = 0; i < num_private_segs; i++) {
    if (is_in_net(addr, private_segs[i].net, private_segs[i].mask))
      return true;
  }
  return false;
}

static int handle_event(TSCont contp, TSEvent event, void *edata);
static int api_handle_event(TSCont contp, TSEvent event, void *edata);

/*
  Get the value of parameter in url querystring
  Return 0 and a null string if not find the parameter.
  Return 1 and a value string, normally
  Return 2 and a max_length value string if the length of the value exceeds.

  Possible appearance: ?param=value&fake_param=value&param=value
*/
static int
get_query_param(const char *query, const char *param, char *result, int max_length)
{
  const char *pos = 0;

  pos = strstr(query, param); // try to find in querystring of url
  if (pos != query) {
    // if param is not prefix of querystring
    while (pos && *(pos - 1) != '&') {          // param must be after '&'
      pos = strstr(pos + strlen(param), param); // try next
    }
  }

  if (!pos) {
    // set it null string if not found
    result[0] = '\0';
    return 0;
  }

  pos += strlen(param); // skip 'param='

  // copy value of param
  int now = 0;
  while (*pos != '\0' && *pos != '&' && now < max_length) {
    result[now++] = *pos;
    pos++;
  }
  result[now] = '\0'; // make sure null-terminated

  if (*pos != '\0' && *pos != '&' && now == max_length)
    return 2;
  else
    return 1;
}

/*
  check if exist param in query string

  Possible querystring: ?param1=value1&param2
  (param2 is a param which "has_no_value")
*/
static int
has_query_param(const char *query, const char *param, int has_no_value)
{
  const char *pos = 0;

  pos = strstr(query, param); // try to find in querystring of url
  if (pos != query) {
    // if param is not prefix of querystring
    while (pos && *(pos - 1) != '&') {          // param must be after '&'
      pos = strstr(pos + strlen(param), param); // try next
    }
  }

  if (!pos)
    return 0;

  pos += strlen(param); // skip 'param='

  if (has_no_value) {
    if (*pos == '\0' || *pos == '&')
      return 1;
  } else {
    if (*pos == '=')
      return 1;
  }

  return 0;
}

static void
get_api_params(TSMBuffer bufp, TSMLoc url_loc, int *show_global, char **channel, int *topn)
{
  const char *query;      // not null-terminated, get from TS api
  char *tmp_query = NULL; // null-terminated
  int query_len   = 0;

  *show_global = 0;
  *topn        = -1;

  query = TSUrlHttpQueryGet(bufp, url_loc, &query_len);
  if (query_len == 0)
    return;
  tmp_query = TSstrndup(query, query_len);
  debug_api("querystring: %s", tmp_query);

  if (has_query_param(tmp_query, "global", 1)) {
    debug_api("found 'global' param");
    *show_global = 1;
  }

  *channel = (char *)TSmalloc(query_len);
  if (get_query_param(tmp_query, "channel=", *channel, query_len)) {
    debug_api("found 'channel' param: %s", *channel);
  }

  std::stringstream ss;
  char *tmp_topn = (char *)TSmalloc(query_len);
  if (get_query_param(tmp_query, "topn=", tmp_topn, 10)) {
    if (strlen(tmp_topn) > 0) {
      ss.str(tmp_topn);
      ss >> *topn;
    }
    debug_api("found 'topn' param: %d", *topn);
  }

  TSfree(tmp_query);
  TSfree(tmp_topn);
}

static void
handle_read_req(TSCont /* contp ATS_UNUSED */, TSHttpTxn txnp)
{
  TSMBuffer bufp;
  TSMLoc hdr_loc = NULL;
  TSMLoc url_loc = NULL;
  const char *method;
  int method_length = 0;
  TSCont txn_contp;

  const char *path;
  int path_len;
  struct sockaddr *client_addr;
  struct sockaddr_in *client_addr4;
  TSCont api_contp;
  char *client_ip;
  intercept_state *api_state;

  if (TSHttpTxnClientReqGet(txnp, &bufp, &hdr_loc) != TS_SUCCESS) {
    error("couldn't retrieve client's request");
    goto cleanup;
  }

  method = TSHttpHdrMethodGet(bufp, hdr_loc, &method_length);
  if (0 != strncmp(method, TS_HTTP_METHOD_GET, method_length)) {
    debug("do not count %.*s method", method_length, method);
    goto cleanup;
  }

  if (TSHttpHdrUrlGet(bufp, hdr_loc, &url_loc) != TS_SUCCESS)
    goto cleanup;

  path = TSUrlPathGet(bufp, url_loc, &path_len);
  if (path_len == 0 || (unsigned)path_len != api_path.length() || strncmp(api_path.c_str(), path, path_len) != 0) {
    goto not_api;
  }

  // register our intercept
  debug_api("Intercepting request");
  api_state = (intercept_state *)TSmalloc(sizeof(*api_state));
  memset(api_state, 0, sizeof(*api_state));
  get_api_params(bufp, url_loc, &api_state->show_global, &api_state->channel, &api_state->topn);

  // check private ip
  client_addr = (struct sockaddr *)TSHttpTxnClientAddrGet(txnp);
  if (client_addr->sa_family == AF_INET) {
    client_addr4 = (struct sockaddr_in *)client_addr;
    if (!is_private_ip(client_addr4->sin_addr.s_addr)) {
      client_ip = (char *)TSmalloc(INET_ADDRSTRLEN);
      inet_ntop(AF_INET, &client_addr4->sin_addr, client_ip, INET_ADDRSTRLEN);
      debug_api("%s is not a private IP, request denied", client_ip);
      api_state->deny = 1;
      TSfree(client_ip);
    }
  } else {
    debug_api("not IPv4, request denied"); // TODO check AF_INET6's private IP?
    api_state->deny = 1;
  }

  TSSkipRemappingSet(txnp, 1); // not strictly necessary

  api_contp = TSContCreate(api_handle_event, TSMutexCreate());
  TSContDataSet(api_contp, api_state);
  TSHttpTxnIntercept(api_contp, txnp);

  goto cleanup;

not_api:
  txn_contp = TSContCreate(handle_event, NULL); // reuse global handler
  TSHttpTxnHookAdd(txnp, TS_HTTP_TXN_CLOSE_HOOK, txn_contp);

cleanup:
  if (url_loc)
    TSHandleMLocRelease(bufp, hdr_loc, url_loc);
  if (hdr_loc)
    TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc);
}

static bool
get_pristine_host(TSHttpTxn txnp, TSMBuffer bufp, std::string &host)
{
  TSMLoc purl_loc;
  const char *pristine_host;
  int pristine_host_len = 0;
  int pristine_port;

  if (TSHttpTxnPristineUrlGet(txnp, &bufp, &purl_loc) != TS_SUCCESS) {
    debug("couldn't retrieve pristine url");
    return false;
  }

  pristine_host = TSUrlHostGet(bufp, purl_loc, &pristine_host_len);
  if (pristine_host_len == 0) {
    debug("couldn't retrieve pristine host");
    return false;
  }

  pristine_port = TSUrlPortGet(bufp, purl_loc);
  TSHandleMLocRelease(bufp, TS_NULL_MLOC, purl_loc);
  host = std::string(pristine_host, pristine_host_len);
  if (pristine_port != 80) {
    char buf[12];
    if (!sprintf(buf, ":%d", pristine_port))
      return false;
    host.append(buf);
  }

  debug("pristine host: %.*s", pristine_host_len, pristine_host);
  debug("pristine port: %d", pristine_port);
  debug("host to lookup: %s", host.c_str());

  return true;
}

static bool
get_channel_stat(const std::string &host, channel_stat *&stat, int status_code_type)
{
  smap_iterator stat_it;

  stat_it = channel_stats.find(host);

  if (stat_it != channel_stats.end()) {
    stat = stat_it->second;
  } else {
    if (status_code_type != 2) {
      // if request's host isn't in your remap.config, response code will be 404
      // we should not count that channel in this situation
      debug("not 2xx response, do not create stat for this channel now");
      return false;
    }
    if (channel_stats.size() >= MAX_MAP_SIZE) {
      warning("channel_stats map exceeds max size");
      return false;
    }

    stat = new channel_stat();
    std::pair<smap_iterator, bool> insert_ret;
    TSMutexLock(stats_map_mutex);
    insert_ret = channel_stats.insert(std::make_pair(host, stat));
    TSMutexUnlock(stats_map_mutex);
    if (insert_ret.second == true) {
      // insert successfully
      debug("******** new channel(#%zu) ********", channel_stats.size());
    } else {
      warning("stat of this channel already existed");
      delete stat;
      stat = insert_ret.first->second;
    }
  }

  return true;
}

static uint64_t
get_txn_user_speed(TSHttpTxn txnp, uint64_t body_bytes)
{
  uint64_t user_speed    = 0;
  TSHRTime start_time    = 0;
  TSHRTime end_time      = 0;
  TSHRTime interval_time = 0;

#if (TS_VERSION_NUMBER < 3003001)
  TSHttpTxnStartTimeGet(txnp, &start_time);
  TSHttpTxnEndTimeGet(txnp, &end_time);
#else
  TSHttpTxnMilestoneGet(txnp, TS_MILESTONE_UA_BEGIN, &start_time);
  TSHttpTxnMilestoneGet(txnp, TS_MILESTONE_UA_CLOSE, &end_time);
#endif

  if (start_time != 0 && end_time != 0 && end_time >= start_time) {
    interval_time = end_time - start_time;
  } else {
    warning("invalid time, start: %" PRId64 ", end: %" PRId64 "", start_time, end_time);
    return 0;
  }

  if (interval_time == 0 || body_bytes == 0)
    user_speed = MAX_SPEED;
  else
    user_speed = (uint64_t)((float)body_bytes / interval_time * HRTIME_SECOND);

  debug("start time: %" PRId64 "", start_time);
  debug("end time: %" PRId64 "", end_time);
  debug("interval time: %" PRId64 "", interval_time);
  debug("interval seconds: %.5f", interval_time / (float)HRTIME_SECOND);
  debug("speed bytes per second: %" PRIu64 "", user_speed);

  return user_speed;
}

static void
handle_txn_close(TSCont /* contp ATS_UNUSED */, TSHttpTxn txnp)
{
  TSMBuffer bufp;
  TSMLoc hdr_loc;
  TSHttpStatus status_code;
  int status_code_type;
  uint64_t user_speed;
  uint64_t body_bytes;
  channel_stat *stat;
  std::string host;

  if (TSHttpTxnClientRespGet(txnp, &bufp, &hdr_loc) != TS_SUCCESS) {
    debug("couldn't retrieve final response");
    return;
  }

  status_code      = TSHttpHdrStatusGet(bufp, hdr_loc);
  status_code_type = status_code / 100;
  body_bytes       = TSHttpTxnClientRespBodyBytesGet(txnp);

  __sync_fetch_and_add(&global_response_bytes_content, body_bytes);
  if (status_code_type == 2)
    __sync_fetch_and_add(&global_response_count_2xx_get, 1);

  debug("body bytes: %" PRIu64 "", body_bytes);
  debug("2xx req count: %" PRIu64 "", global_response_count_2xx_get);

  if (!get_pristine_host(txnp, bufp, host))
    goto cleanup;

  // get or create the stat
  if (!get_channel_stat(host, stat, status_code_type))
    goto cleanup;

  user_speed = get_txn_user_speed(txnp, body_bytes);

  stat->increment(body_bytes, status_code_type == 2 ? 1 : 0, status_code_type == 5 ? 1 : 0,
                  (user_speed < 64000 && user_speed > 0) ? 1 : 0);
  stat->debug_channel();

cleanup:
  TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc);
}

static int
handle_event(TSCont contp, TSEvent event, void *edata)
{
  TSHttpTxn txnp = (TSHttpTxn)edata;

  switch (event) {
  case TS_EVENT_HTTP_READ_REQUEST_HDR: // for global contp
    debug("---------- new request ----------");
    handle_read_req(contp, txnp);
    break;
  case TS_EVENT_HTTP_TXN_CLOSE: // for txn contp
    handle_txn_close(contp, txnp);
    TSContDestroy(contp);
    break;
  default:
    error("unknown event for this plugin");
  }

  TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);

  return 0;
}

// below is api part

static void
stats_cleanup(TSCont contp, intercept_state *api_state)
{
  if (api_state->req_buffer) {
    TSIOBufferDestroy(api_state->req_buffer);
    api_state->req_buffer = NULL;
  }

  if (api_state->resp_buffer) {
    TSIOBufferDestroy(api_state->resp_buffer);
    api_state->resp_buffer = NULL;
  }

  TSfree(api_state->channel);
  TSVConnClose(api_state->net_vc);
  TSfree(api_state);
  TSContDestroy(contp);
}

static void
stats_process_accept(TSCont contp, intercept_state *api_state)
{
  api_state->req_buffer  = TSIOBufferCreate();
  api_state->resp_buffer = TSIOBufferCreate();
  api_state->resp_reader = TSIOBufferReaderAlloc(api_state->resp_buffer);
  api_state->read_vio    = TSVConnRead(api_state->net_vc, contp, api_state->req_buffer, INT64_MAX);
}

static int
stats_add_data_to_resp_buffer(const char *s, intercept_state *api_state)
{
  int s_len = strlen(s);

  TSIOBufferWrite(api_state->resp_buffer, s, s_len);

  return s_len;
}

static const char RESP_HEADER[] = "HTTP/1.0 200 Ok\r\nContent-Type: application/json\r\nCache-Control: no-cache\r\n\r\n";

static int
stats_add_resp_header(intercept_state *api_state)
{
  return stats_add_data_to_resp_buffer(RESP_HEADER, api_state);
}

static void
stats_process_read(TSCont contp, TSEvent event, intercept_state *api_state)
{
  debug_api("stats_process_read(%d)", event);
  if (event == TS_EVENT_VCONN_READ_READY) {
    api_state->output_bytes = stats_add_resp_header(api_state);
    TSVConnShutdown(api_state->net_vc, 1, 0);
    api_state->write_vio = TSVConnWrite(api_state->net_vc, contp, api_state->resp_reader, INT64_MAX);
  } else if (event == TS_EVENT_ERROR) {
    error_api("stats_process_read: Received TS_EVENT_ERROR\n");
  } else if (event == TS_EVENT_VCONN_EOS) {
    // client may end the connection, simply return
    return;
  } else if (event == TS_EVENT_NET_ACCEPT_FAILED) {
    error_api("stats_process_read: Received TS_EVENT_NET_ACCEPT_FAILED\n");
  } else {
    error_api("Unexpected Event %d\n", event);
    // TSReleaseAssert(!"Unexpected Event");
  }
}

#define APPEND(a) api_state->output_bytes += stats_add_data_to_resp_buffer(a, api_state)
#define APPEND_STAT(a, fmt, v)                                                      \
  do {                                                                              \
    char b[256];                                                                    \
    if (snprintf(b, sizeof(b), "\"%s\": \"" fmt "\",\n", a, v) < (signed)sizeof(b)) \
      APPEND(b);                                                                    \
  } while (0)
#define APPEND_END_STAT(a, fmt, v)                                                 \
  do {                                                                             \
    char b[256];                                                                   \
    if (snprintf(b, sizeof(b), "\"%s\": \"" fmt "\"\n", a, v) < (signed)sizeof(b)) \
      APPEND(b);                                                                   \
  } while (0)
#define APPEND_DICT_NAME(a)                                           \
  do {                                                                \
    char b[256];                                                      \
    if (snprintf(b, sizeof(b), "\"%s\": {\n", a) < (signed)sizeof(b)) \
      APPEND(b);                                                      \
  } while (0)

static void
json_out_stat(TSRecordType /* rec_type ATS_UNUSED */, void *edata, int /* registered ATS_UNUSED */, const char *name,
              TSRecordDataType data_type, TSRecordData *datum)
{
  intercept_state *api_state = (intercept_state *)edata;

  switch (data_type) {
  case TS_RECORDDATATYPE_COUNTER:
    APPEND_STAT(name, "%" PRId64, datum->rec_counter);
    break;
  case TS_RECORDDATATYPE_INT:
    APPEND_STAT(name, "%" PRIu64, datum->rec_int);
    break;
  case TS_RECORDDATATYPE_FLOAT:
    APPEND_STAT(name, "%f", datum->rec_float);
    break;
  case TS_RECORDDATATYPE_STRING:
    APPEND_STAT(name, "%s", datum->rec_string);
    break;
  default:
    debug_api("unknown type for %s: %d", name, data_type);
    break;
  }
}

template <class T> struct compare : std::binary_function<T, T, bool> {
  inline bool
  operator()(const T &lhs, const T &rhs)
  {
    return lhs.second->response_count_2xx > rhs.second->response_count_2xx;
  }
};

static void
append_channel_stat(intercept_state *api_state, const std::string channel, channel_stat *cs, int is_last)
{
  APPEND_DICT_NAME(channel.c_str());
  APPEND_STAT("response.bytes.content", "%" PRIu64, cs->response_bytes_content);
  APPEND_STAT("response.count.2xx.get", "%" PRIu64, cs->response_count_2xx);
  APPEND_STAT("response.count.5xx.get", "%" PRIu64, cs->response_count_5xx);
  APPEND_END_STAT("speed.ua.bytes_per_sec_64k", "%" PRIu64, cs->speed_ua_bytes_per_sec_64k);
  if (is_last)
    APPEND("}\n");
  else
    APPEND("},\n");
}

static void
json_out_channel_stats(intercept_state *api_state)
{
  if (channel_stats.empty())
    return;

  typedef std::pair<std::string, channel_stat *> data_pair;
  typedef std::vector<data_pair> stats_vec_t;
  smap_iterator it;

  debug("appending channel stats");

  if (api_state->topn > -1 || (api_state->channel && strlen(api_state->channel) > 0)) {
    // will use vector to output

    if (api_state->topn == 0)
      return;

    stats_vec_t stats_vec; // a tmp vector to sort or filter
    if (strlen(api_state->channel) > 0) {
      // filter by channel
      size_t found;
      for (it = channel_stats.begin(); it != channel_stats.end(); it++) {
        found = it->first.find(api_state->channel);
        if (found != std::string::npos)
          stats_vec.push_back(*it);
      }
    } else {
      for (it = channel_stats.begin(); it != channel_stats.end(); it++)
        stats_vec.push_back(*it);
      /* stats_vec.assign is not safe when map is being inserted concurrently */
    }

    if (stats_vec.empty())
      return;

    stats_vec_t::size_type out_st = stats_vec.size();
    if (api_state->topn > 0) { // need sort and limit output size
      if ((unsigned)api_state->topn < stats_vec.size())
        out_st = (unsigned)api_state->topn;
      else
        api_state->topn = stats_vec.size();
      std::partial_sort(stats_vec.begin(), stats_vec.begin() + api_state->topn, stats_vec.end(), compare<data_pair>());
    } // else will output whole vector without sort

    stats_vec_t::size_type i;
    for (i = 0; i < out_st - 1; i++) {
      append_channel_stat(api_state, stats_vec[i].first, stats_vec[i].second, 0);
    }
    append_channel_stat(api_state, stats_vec[i].first, stats_vec[i].second, 1);

  } else {
    smap_iterator last_it = channel_stats.end();
    last_it--;
    for (it = channel_stats.begin(); it != last_it; it++) {
      append_channel_stat(api_state, it->first, it->second, 0);
    }
    append_channel_stat(api_state, it->first, it->second, 1);
  }
}

static void
json_out_stats(intercept_state *api_state)
{
  const char *version;

  APPEND("{ \"channel\": {\n");
  json_out_channel_stats(api_state);
  APPEND("  },\n");

  APPEND(" \"global\": {\n");
  APPEND_STAT("response.count.2xx.get", "%" PRIu64, global_response_count_2xx_get);
  APPEND_STAT("response.bytes.content", "%" PRIu64, global_response_bytes_content);
  APPEND_STAT("channel.count", "%zu", channel_stats.size());

  if (api_state->show_global)
    TSRecordDump(TS_RECORDTYPE_PROCESS, json_out_stat, api_state); // internal stats

  version = TSTrafficServerVersionGet();
  APPEND("\"server\": \"");
  APPEND(version);
  APPEND("\"\n");

  APPEND("  }\n}\n");
}

static void
stats_process_write(TSCont contp, TSEvent event, intercept_state *api_state)
{
  if (event == TS_EVENT_VCONN_WRITE_READY) {
    if (api_state->body_written == 0) {
      debug_api("plugin adding response body");
      api_state->body_written = 1;
      if (!api_state->deny)
        json_out_stats(api_state);
      else
        APPEND("forbidden");
      TSVIONBytesSet(api_state->write_vio, api_state->output_bytes);
    }
    TSVIOReenable(api_state->write_vio);
  } else if (TS_EVENT_VCONN_WRITE_COMPLETE) {
    stats_cleanup(contp, api_state);
  } else if (event == TS_EVENT_ERROR) {
    error_api("stats_process_write: Received TS_EVENT_ERROR\n");
  } else {
    error_api("Unexpected Event %d\n", event);
    // TSReleaseAssert(!"Unexpected Event");
  }
}

static int
api_handle_event(TSCont contp, TSEvent event, void *edata)
{
  intercept_state *api_state = (intercept_state *)TSContDataGet(contp);
  if (event == TS_EVENT_NET_ACCEPT) {
    api_state->net_vc = (TSVConn)edata;
    stats_process_accept(contp, api_state);
  } else if (edata == api_state->read_vio) {
    stats_process_read(contp, event, api_state);
  } else if (edata == api_state->write_vio) {
    stats_process_write(contp, event, api_state);
  } else {
    error_api("Unexpected Event %d\n", event);
    // TSReleaseAssert(!"Unexpected Event");
  }
  return 0;
}

// initial part

void
TSPluginInit(int argc, const char *argv[])
{
  if (argc > 2) {
    fatal("plugin does not accept more than 1 argument");
  } else if (argc == 2) {
    api_path = std::string(argv[1]);
    debug_api("stats api path: %s", api_path.c_str());
  }

  TSPluginRegistrationInfo info;

  info.plugin_name   = (char *)PLUGIN_NAME;
  info.vendor_name   = (char *)"Apache Software Foundation";
  info.support_email = (char *)"dev@trafficserver.apache.org";

  if (TSPluginRegister(&info) != TS_SUCCESS) {
    fatal("plugin registration failed.");
  }

  info("%s(%s) plugin starting...", PLUGIN_NAME, PLUGIN_VERSION);

  stats_map_mutex = TSMutexCreate();

  TSCont cont = TSContCreate(handle_event, NULL);
  TSHttpHookAdd(TS_HTTP_READ_REQUEST_HDR_HOOK, cont);
}

/* vim: set sw=2 tw=79 ts=2 et ai : */
