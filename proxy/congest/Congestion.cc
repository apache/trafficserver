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

/*****************************************************************************
 *
 *  Congestion.cc - Content and User Access Control
 *
 *
 ****************************************************************************/
#include "ts/ink_platform.h"
#include "I_Net.h"
#include "CongestionDB.h"
#include "Congestion.h"
#include "ControlMatcher.h"
#include "ProxyConfig.h"

RecRawStatBlock *congest_rsb;

InkRand CongestionRand(123);

static const char *congestPrefix = "[CongestionControl]";

static const matcher_tags congest_dest_tags = {"dest_host", "dest_domain", "dest_ip", NULL, NULL, "host_regex", true};

/* default congestion control values */

char *DEFAULT_error_page            = NULL;
int DEFAULT_max_connection_failures = 5;
int DEFAULT_fail_window             = 120;
int DEFAULT_proxy_retry_interval    = 10;
int DEFAULT_client_wait_interval    = 300;
int DEFAULT_wait_interval_alpha     = 30;
int DEFAULT_live_os_conn_timeout    = 60;
int DEFAULT_live_os_conn_retries    = 2;
int DEFAULT_dead_os_conn_timeout    = 15;
int DEFAULT_dead_os_conn_retries    = 1;
int DEFAULT_max_connection          = -1;
char *DEFAULT_congestion_scheme_str = NULL;
int DEFAULT_congestion_scheme       = PER_IP;

/* congestion control limits */
#define CONG_RULE_MAX_max_connection_failures (1 << (sizeof(cong_hist_t) * 8))

#define CONG_RULE_ULIMITED_max_connection_failures -1
#define CONG_RULE_ULIMITED_mac_connection -1

struct CongestionMatcherTable : public ControlMatcher<CongestionControlRecord, CongestionControlRule>, public ConfigInfo {
  CongestionMatcherTable(const char *file_var, const char *name, const matcher_tags *tags)
    : ControlMatcher<CongestionControlRecord, CongestionControlRule>(file_var, name, tags)
  {
  }

  static void reconfigure();

  static int configid;
};

int CongestionMatcherTable::configid = 0;

static CongestionMatcherTable *CongestionMatcher = NULL;
static ConfigUpdateHandler<CongestionMatcherTable> *CongestionControlUpdate;
int congestionControlEnabled   = 0;
int congestionControlLocalTime = 0;

CongestionControlRecord::CongestionControlRecord(const CongestionControlRecord &rec)
{
  prefix                  = ats_strdup(rec.prefix);
  prefix_len              = rec.prefix_len;
  port                    = rec.port;
  congestion_scheme       = rec.congestion_scheme;
  error_page              = ats_strdup(rec.error_page);
  max_connection_failures = rec.max_connection_failures;
  fail_window             = rec.fail_window;
  proxy_retry_interval    = rec.proxy_retry_interval;
  client_wait_interval    = rec.client_wait_interval;
  wait_interval_alpha     = rec.wait_interval_alpha;
  live_os_conn_timeout    = rec.live_os_conn_timeout;
  live_os_conn_retries    = rec.live_os_conn_retries;
  dead_os_conn_timeout    = rec.dead_os_conn_timeout;
  dead_os_conn_retries    = rec.dead_os_conn_retries;
  max_connection          = rec.max_connection;
  pRecord                 = NULL;
  ref_count               = 1;
  line_num                = rec.line_num;
  rank                    = 0;
}

void
CongestionControlRecord::setdefault()
{
  cleanup();
  congestion_scheme       = DEFAULT_congestion_scheme;
  port                    = 0;
  prefix_len              = 0;
  rank                    = 0;
  max_connection_failures = DEFAULT_max_connection_failures;
  fail_window             = DEFAULT_fail_window;
  proxy_retry_interval    = DEFAULT_proxy_retry_interval;
  client_wait_interval    = DEFAULT_client_wait_interval;
  wait_interval_alpha     = DEFAULT_wait_interval_alpha;
  live_os_conn_timeout    = DEFAULT_live_os_conn_timeout;
  live_os_conn_retries    = DEFAULT_live_os_conn_retries;
  dead_os_conn_timeout    = DEFAULT_dead_os_conn_timeout;
  dead_os_conn_retries    = DEFAULT_dead_os_conn_retries;
  max_connection          = DEFAULT_max_connection;
}

config_parse_error
CongestionControlRecord::validate()
{
#define IsGt0(var)                                                                                \
  if (var < 1) {                                                                                  \
    config_parse_error error("line %d: invalid %s = %d, %s must > 0", line_num, #var, var, #var); \
    cleanup();                                                                                    \
    return error;                                                                                 \
  }

  if (error_page == NULL)
    error_page = ats_strdup(DEFAULT_error_page);
  if (max_connection_failures >= CONG_RULE_MAX_max_connection_failures ||
      (max_connection_failures <= 0 && max_connection_failures != CONG_RULE_ULIMITED_max_connection_failures)) {
    config_parse_error error("line %d: invalid %s = %d not in [1, %d) range", line_num, "max_connection_failures",
                             max_connection_failures, CONG_RULE_MAX_max_connection_failures);
    cleanup();
    return error;
  }

  IsGt0(fail_window);
  IsGt0(proxy_retry_interval);
  IsGt0(client_wait_interval);
  IsGt0(wait_interval_alpha);
  IsGt0(live_os_conn_timeout);
  IsGt0(live_os_conn_retries);
  IsGt0(dead_os_conn_timeout);
  IsGt0(dead_os_conn_retries);
// max_connection_failures <= 0  no failure num control
// max_connection == -1 no max_connection control
// max_connection_failures <= 0 && max_connection == -1 no congestion control for the rule
// max_connection == 0, no connection allow to the origin server for the rule
#undef IsGt0

  return config_parse_error::ok();
}

config_parse_error
CongestionControlRecord::Init(matcher_line *line_info)
{
  const char *tmp;
  char *label;
  char *val;
  line_num = line_info->line_num;

  /* initialize the rule to defaults */
  setdefault();

  for (int i = 0; i < MATCHER_MAX_TOKENS; i++) {
    label = line_info->line[0][i];
    val   = line_info->line[1][i];

    if (label == NULL) {
      continue;
    }
    if (strcasecmp(label, "max_connection_failures") == 0) {
      max_connection_failures = atoi(val);
    } else if (strcasecmp(label, "fail_window") == 0) {
      fail_window = atoi(val);
    } else if (strcasecmp(label, "proxy_retry_interval") == 0) {
      proxy_retry_interval = atoi(val);
    } else if (strcasecmp(label, "client_wait_interval") == 0) {
      client_wait_interval = atoi(val);
    } else if (strcasecmp(label, "wait_interval_alpha") == 0) {
      wait_interval_alpha = atoi(val);
    } else if (strcasecmp(label, "live_os_conn_timeout") == 0) {
      live_os_conn_timeout = atoi(val);
    } else if (strcasecmp(label, "live_os_conn_retries") == 0) {
      live_os_conn_retries = atoi(val);
    } else if (strcasecmp(label, "dead_os_conn_timeout") == 0) {
      dead_os_conn_timeout = atoi(val);
    } else if (strcasecmp(label, "dead_os_conn_retries") == 0) {
      dead_os_conn_retries = atoi(val);
    } else if (strcasecmp(label, "max_connection") == 0) {
      max_connection = atoi(val);
    } else if (strcasecmp(label, "congestion_scheme") == 0) {
      if (!strcasecmp(val, "per_ip")) {
        congestion_scheme = PER_IP;
      } else if (!strcasecmp(val, "per_host")) {
        congestion_scheme = PER_HOST;
      } else {
        congestion_scheme = PER_IP;
      }
    } else if (strcasecmp(label, "error_page") == 0) {
      error_page = ats_strdup(val);
    } else if (strcasecmp(label, "prefix") == 0) {
      prefix     = ats_strdup(val);
      prefix_len = strlen(prefix);
      rank += 1;
      // prefix will be used in the ControlBase
      continue;
    } else if (strcasecmp(label, "port") == 0) {
      port = atoi(val);
      rank += 2;
      // port will be used in the ControlBase;
      continue;
    } else
      continue;
    // Consume the label/value pair we used
    line_info->line[0][i] = NULL;
    line_info->num_el--;
  }
  if (line_info->num_el > 0) {
    tmp = ProcessModifiers(line_info);

    if (tmp != NULL) {
      return config_parse_error("%s %s at line %d in congestion.config", congestPrefix, tmp, line_num);
    }
  }

  config_parse_error error = validate();
  if (!error) {
    pRecord = new CongestionControlRecord(*this);
  }

  return error;
}

void
CongestionControlRecord::UpdateMatch(CongestionControlRule *pRule, RequestData *rdata)
{
  /*
   * Select the first matching rule specified in congestion.config
   * rank     Matches
   *   3       dest && prefix && port
   *   2       dest && port
   *   1       dest && prefix
   *   0       dest
   */
  if (pRule->record == 0 || pRule->record->rank < rank || (pRule->record->line_num > line_num && pRule->record->rank == rank)) {
    if (rank > 0) {
      CongestionEntry *entry = dynamic_cast<CongestionEntry *>(rdata);
      if (entry) {
        // Enforce the same port and prefix
        if (port != 0 && port != entry->pRecord->port)
          return;
        if (prefix != NULL && entry->pRecord->prefix == NULL)
          return;
        if (prefix != NULL && strncmp(prefix, entry->pRecord->prefix, prefix_len))
          return;
      } else {
        HttpRequestData *h = dynamic_cast<HttpRequestData *>(rdata);
        if (h && !this->CheckModifiers(h)) {
          return;
        }
      }
    }
    pRule->record = this;
    Debug("congestion_config", "Matched with record %p at line %d", this, line_num);
  }
}

void
CongestionControlRecord::Print()
{
#define PrintNUM(var) Debug("congestion_config", "%30s = %d", #var, var);
#define PrintSTR(var) Debug("congestion_config", "%30s = %s", #var, (var == NULL ? "NULL" : var));

  PrintNUM(line_num);
  PrintSTR(prefix);
  PrintNUM(congestion_scheme);
  PrintSTR(error_page);
  PrintNUM(max_connection_failures);
  PrintNUM(fail_window);
  PrintNUM(proxy_retry_interval);
  PrintNUM(client_wait_interval);
  PrintNUM(wait_interval_alpha);
  PrintNUM(live_os_conn_timeout);
  PrintNUM(live_os_conn_retries);
  PrintNUM(dead_os_conn_timeout);
  PrintNUM(dead_os_conn_retries);
  PrintNUM(max_connection);
#undef PrintNUM
#undef PrintSTR
}

extern void initCongestionDB();

// place holder for congestion control enable config
static int
CongestionControlEnabledChanged(const char * /* name ATS_UNUSED */, RecDataT /* data_type ATS_UNUSED */,
                                RecData /* data ATS_UNUSED */, void * /* cookie ATS_UNUSED */)
{
  if (congestionControlEnabled == 1 || congestionControlEnabled == 2) {
    revalidateCongestionDB();
  }
  return 0;
}

static int
CongestionControlDefaultSchemeChanged(const char * /* name ATS_UNUSED */, RecDataT /* data_type ATS_UNUSED */,
                                      RecData /* data ATS_UNUSED */, void * /* cookie ATS_UNUSED */)
{
  if (strcasecmp(DEFAULT_congestion_scheme_str, "per_host") == 0) {
    DEFAULT_congestion_scheme = PER_HOST;
  } else {
    DEFAULT_congestion_scheme = PER_IP;
  }
  return 0;
}

//-----------------------------------------------
// hack for link the RegressionTest into the
//  TS binary
//-----------------------------------------------
extern void init_CongestionRegressionTest();

void
initCongestionControl()
{
// TODO: This is very, very strange, we run the regression tests even on a normal startup??
#if TS_HAS_TESTS
  init_CongestionRegressionTest();
#endif
  ink_assert(CongestionMatcher == NULL);
  // register the stats variables
  register_congest_stats();

  CongestionControlUpdate = new ConfigUpdateHandler<CongestionMatcherTable>();

  // register config variables
  REC_EstablishStaticConfigInt32(congestionControlEnabled, "proxy.config.http.congestion_control.enabled");
  REC_EstablishStaticConfigInt32(DEFAULT_max_connection_failures,
                                 "proxy.config.http.congestion_control.default.max_connection_failures");
  REC_EstablishStaticConfigInt32(DEFAULT_fail_window, "proxy.config.http.congestion_control.default.fail_window");
  REC_EstablishStaticConfigInt32(DEFAULT_proxy_retry_interval, "proxy.config.http.congestion_control.default.proxy_retry_interval");
  REC_EstablishStaticConfigInt32(DEFAULT_client_wait_interval, "proxy.config.http.congestion_control.default.client_wait_interval");
  REC_EstablishStaticConfigInt32(DEFAULT_wait_interval_alpha, "proxy.config.http.congestion_control.default.wait_interval_alpha");
  REC_EstablishStaticConfigInt32(DEFAULT_live_os_conn_timeout, "proxy.config.http.congestion_control.default.live_os_conn_timeout");
  REC_EstablishStaticConfigInt32(DEFAULT_live_os_conn_retries, "proxy.config.http.congestion_control.default.live_os_conn_retries");
  REC_EstablishStaticConfigInt32(DEFAULT_dead_os_conn_timeout, "proxy.config.http.congestion_control.default.dead_os_conn_timeout");
  REC_EstablishStaticConfigInt32(DEFAULT_dead_os_conn_retries, "proxy.config.http.congestion_control.default.dead_os_conn_retries");
  REC_EstablishStaticConfigInt32(DEFAULT_max_connection, "proxy.config.http.congestion_control.default.max_connection");
  REC_EstablishStaticConfigStringAlloc(DEFAULT_congestion_scheme_str,
                                       "proxy.config.http.congestion_control.default.congestion_scheme");
  REC_EstablishStaticConfigStringAlloc(DEFAULT_error_page, "proxy.config.http.congestion_control.default.error_page");
  REC_EstablishStaticConfigInt32(congestionControlLocalTime, "proxy.config.http.congestion_control.localtime");
  {
    RecData recdata;
    recdata.rec_int = 0;
    CongestionControlDefaultSchemeChanged(NULL, RECD_NULL, recdata, NULL);
  }

  if (congestionControlEnabled) {
    CongestionMatcherTable::reconfigure();
  } else {
    Debug("congestion_config", "congestion control disabled");
  }

  RecRegisterConfigUpdateCb("proxy.config.http.congestion_control.default.congestion_scheme",
                            &CongestionControlDefaultSchemeChanged, NULL);
  RecRegisterConfigUpdateCb("proxy.config.http.congestion_control.enabled", &CongestionControlEnabledChanged, NULL);

  CongestionControlUpdate->attach("proxy.config.http.congestion_control.filename");
}

void
CongestionMatcherTable::reconfigure()
{
  Note("congestion control config changed, reloading");
  CongestionMatcher =
    new CongestionMatcherTable("proxy.config.http.congestion_control.filename", congestPrefix, &congest_dest_tags);

#ifdef DEBUG_CONGESTION_MATCHER
  CongestionMatcher->Print();
#endif

  configid = configProcessor.set(configid, CongestionMatcher);
  if (congestionControlEnabled) {
    revalidateCongestionDB();
  }
}

CongestionControlRecord *
CongestionControlled(RequestData *rdata)
{
  if (congestionControlEnabled) {
    CongestionControlRule result;
    CongestionMatcher->Match(rdata, &result);
    if (result.record) {
      return result.record->pRecord;
    }
  } else {
    return NULL;
  }
  return NULL;
}

uint64_t
make_key(char *hostname, sockaddr const *ip, CongestionControlRecord *record)
{
  int host_len = 0;
  if (hostname) {
    host_len = strlen(hostname);
  }
  return make_key(hostname, host_len, ip, record);
}

uint64_t
make_key(char *hostname, int len, sockaddr const *ip, CongestionControlRecord *record)
{
  INK_MD5 md5;
  INK_DIGEST_CTX ctx;
  ink_code_incr_md5_init(&ctx);
  if (record->congestion_scheme == PER_HOST && len > 0)
    ink_code_incr_md5_update(&ctx, hostname, len);
  else
    ink_code_incr_md5_update(&ctx, reinterpret_cast<char const *>(ats_ip_addr8_cast(ip)), ats_ip_addr_size(ip));
  if (record->port != 0) {
    unsigned short p = record->port;
    p                = htons(p);
    ink_code_incr_md5_update(&ctx, (char *)&p, 2);
  }
  if (record->prefix != NULL) {
    ink_code_incr_md5_update(&ctx, record->prefix, record->prefix_len);
  }
  ink_code_incr_md5_final((char *)&md5, &ctx);

  return md5.fold();
}

uint64_t
make_key(char *hostname, int len, sockaddr const *ip, char *prefix, int prelen, short port)
{
  /* if the hostname != NULL, use hostname, else, use ip */
  INK_MD5 md5;
  INK_DIGEST_CTX ctx;
  ink_code_incr_md5_init(&ctx);
  if (hostname && len > 0)
    ink_code_incr_md5_update(&ctx, hostname, len);
  else
    ink_code_incr_md5_update(&ctx, reinterpret_cast<char const *>(ats_ip_addr8_cast(ip)), ats_ip_addr_size(ip));
  if (port != 0) {
    unsigned short p = port;
    p                = htons(p);
    ink_code_incr_md5_update(&ctx, (char *)&p, 2);
  }
  if (prefix != NULL) {
    ink_code_incr_md5_update(&ctx, prefix, prelen);
  }
  ink_code_incr_md5_final((char *)&md5, &ctx);

  return md5.fold();
}

//----------------------------------------------------------
// FailHistory Implementation
//----------------------------------------------------------
void
FailHistory::init(int window)
{
  bin_len = (window + CONG_HIST_ENTRIES) / CONG_HIST_ENTRIES;
  if (bin_len <= 0)
    bin_len = 1;
  length    = bin_len * CONG_HIST_ENTRIES;
  for (int i = 0; i < CONG_HIST_ENTRIES; i++) {
    bins[i] = 0;
  }
  last_event = 0;
  cur_index  = 0;
  events     = 0;
  start      = 0;
}

void
FailHistory::init_event(long t, int n)
{
  last_event = t;
  cur_index  = 0;
  events     = n;
  bins[0]    = n;
  for (int i = 1; i < CONG_HIST_ENTRIES; i++) {
    bins[i] = 0;
  }
  start = (last_event + bin_len) - last_event % bin_len - length;
}

int
FailHistory::regist_event(long t, int n)
{
  if (t < start)
    return events;
  if (t > last_event + length) {
    init_event(t, n);
    return events;
  }
  if (t < start + length) {
    bins[((t - start) / bin_len + 1 + cur_index) % CONG_HIST_ENTRIES] += n;
  } else {
    do {
      start += bin_len;
      cur_index++;
      if (cur_index == CONG_HIST_ENTRIES)
        cur_index = 0;
      events -= bins[cur_index];
      bins[cur_index] = 0;
    } while (start + length < t);
    bins[cur_index] = n;
  }
  events += n;
  if (last_event < t)
    last_event = t;
  return events;
}

//----------------------------------------------------------
// CongestionEntry Implementation
//----------------------------------------------------------
CongestionEntry::CongestionEntry(const char *hostname, sockaddr const *ip, CongestionControlRecord *rule, uint64_t key)
  : m_key(key),
    m_last_congested(0),
    m_congested(0),
    m_stat_congested_conn_failures(0),
    m_M_congested(0),
    m_last_M_congested(0),
    m_num_connections(0),
    m_stat_congested_max_conn(0),
    m_ref_count(1)
{
  memset(&m_ip, 0, sizeof(m_ip));
  if (ip != NULL) {
    ats_ip_copy(&m_ip.sa, ip);
  }
  m_hostname = ats_strdup(hostname);
  rule->get();
  pRecord = rule;
  clearFailHistory();
  m_hist_lock = new_ProxyMutex();
}

void
CongestionEntry::init(CongestionControlRecord *rule)
{
  if (pRecord)
    pRecord->put();
  rule->get();
  pRecord = rule;
  clearFailHistory();

  // TODO: This used to signal via SNMP
  if ((pRecord->max_connection > m_num_connections) && ink_atomic_swap(&m_M_congested, 0)) {
    // action not congested?
  }
}

bool
CongestionEntry::validate()
{
  CongestionControlRecord *p = CongestionControlled(this);
  if (p == NULL) {
    return false;
  }

  uint64_t key = make_key(m_hostname, &m_ip.sa, p);
  if (key != m_key) {
    return false;
  }
  applyNewRule(p);
  return true;
}

void
CongestionEntry::applyNewRule(CongestionControlRecord *rule)
{
  if (pRecord->fail_window != rule->fail_window) {
    init(rule);
    return;
  }
  int mcf = pRecord->max_connection_failures;
  pRecord->put();
  rule->get();
  pRecord = rule;
  // TODO: This used to signal via SNMP
  if (((pRecord->max_connection < 0) || (pRecord->max_connection > m_num_connections)) && ink_atomic_swap(&m_M_congested, 0)) {
    // action not congested ?
  }
  // TODO: This used to signal via SNMP
  if (pRecord->max_connection_failures < 0) {
    if (ink_atomic_swap(&m_congested, 0)) {
      // action not congested ?
    }
    return;
  }
  // TODO: This used to signal via SNMP
  if (mcf < pRecord->max_connection_failures) {
    if (ink_atomic_swap(&m_congested, 0)) {
      // action not congested?
    }
  } else if (mcf > pRecord->max_connection_failures && m_history.events >= pRecord->max_connection_failures) {
    if (!ink_atomic_swap(&m_congested, 1)) {
      // action congested?
    }
  }
}

int
CongestionEntry::sprint(char *buf, int buflen, int format)
{
  char str_time[100] = " ";
  char addrbuf[INET6_ADDRSTRLEN];
  int len              = 0;
  ink_hrtime timestamp = 0;
  char state;
  if (pRecord->max_connection >= 0 && m_num_connections >= pRecord->max_connection) {
    timestamp = ink_hrtime_to_sec(Thread::get_hrtime());
    state     = 'M';
  } else {
    timestamp = m_last_congested;
    state     = (m_congested ? 'F' : ' ');
  }
  len += snprintf(buf + len, buflen - len, "%" PRId64 "|%d|%s|%s", timestamp, pRecord->line_num, (m_hostname ? m_hostname : " "),
                  (ats_is_ip(&m_ip) ? ats_ip_ntop(&m_ip.sa, addrbuf, sizeof(addrbuf)) : " "));

  len += snprintf(buf + len, buflen - len, "|%s|%s|%c", (pRecord->congestion_scheme == PER_IP ? "per_ip" : "per_host"),
                  (pRecord->prefix ? pRecord->prefix : " "), state);

  len += snprintf(buf + len, buflen - len, "|%d|%d", m_stat_congested_conn_failures, m_stat_congested_max_conn);

  if (format > 0) {
    if (m_congested) {
      struct tm time;
      time_t seconds = m_last_congested;
      if (congestionControlLocalTime) {
        ink_localtime_r(&seconds, &time);
      } else {
        gmtime_r(&seconds, &time);
      }
      snprintf(str_time, sizeof(str_time), "%04d/%02d/%02d %02d:%02d:%02d", time.tm_year + 1900, time.tm_mon + 1, time.tm_mday,
               time.tm_hour, time.tm_min, time.tm_sec);
    }
    len += snprintf(buf + len, buflen - len, "|%s", str_time);

    if (format > 1) {
      len += snprintf(buf + len, buflen - len, "|%" PRIu64 "", m_key);

      if (format > 2) {
        len += snprintf(buf + len, buflen - len, "|%ld", m_history.last_event);

        if (format > 3) {
          len += snprintf(buf + len, buflen - len, "|%d|%d|%d", m_history.events, m_ref_count, m_num_connections);
        }
      }
    }
  }
  len += snprintf(buf + len, buflen - len, "\n");
  return len;
}

//-------------------------------------------------------------
// When a connection failure happened, try to get the lock
//  first and change register the event, if we can not get
//  the lock, discard the event
//-------------------------------------------------------------
void
CongestionEntry::failed_at(ink_hrtime t)
{
  if (pRecord->max_connection_failures == -1)
    return;
  // long time = ink_hrtime_to_sec(t);
  long time = t;
  Debug("congestion_control", "failed_at: %ld", time);
  MUTEX_TRY_LOCK(lock, m_hist_lock, this_ethread());
  if (lock.is_locked()) {
    m_history.regist_event(time);
    if (!m_congested) {
      int32_t new_congested = compCongested();
      // TODO: This used to signal via SNMP
      if (new_congested && !ink_atomic_swap(&m_congested, 1)) {
        m_last_congested = m_history.last_event;
        // action congested ?
      }
    }
  } else {
    Debug("congestion_control", "failure info lost due to lock contention(Entry: %p, Time: %ld)", (void *)this, time);
  }
}

void
CongestionEntry::go_alive()
{
  // TODO: This used to signal via SNMP
  if (ink_atomic_swap(&m_congested, 0)) {
    // Action not congested ?
  }
}

#define SERVER_CONGESTED_SIG REC_SIGNAL_HTTP_CONGESTED_SERVER
#define SERVER_ALLEVIATED_SIG REC_SIGNAL_HTTP_ALLEVIATED_SERVER
