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
 *  Congestion.h - Implementation of Congestion Control
 *
 *
 ****************************************************************************/

#ifndef CONGESTION_H_
#define CONGESTION_H_

#include "ts/ink_platform.h"
#include "P_EventSystem.h"
#include "ControlBase.h"
#include "ControlMatcher.h"
#include "CongestionStats.h"

#define CONGESTION_EVENT_CONGESTED_ON_M (CONGESTION_EVENT_EVENTS_START + 1)
#define CONGESTION_EVENT_CONGESTED_ON_F (CONGESTION_EVENT_EVENTS_START + 2)
#define CONGESTION_EVENT_CONGESTED_LIST_DONE (CONGESTION_EVENT_EVENTS_START + 3)
#define CONGESTION_EVENT_CONTROL_LOOKUP_DONE (CONGESTION_EVENT_EVENTS_START + 4)

struct RequestData;

extern InkRand CongestionRand;

enum {
  PER_IP,
  PER_HOST,
};

class CongestionControlRecord;

struct CongestionControlRule {
  CongestionControlRule();
  ~CongestionControlRule();
  CongestionControlRecord *record;
};

class CongestionControlRecord : public ControlBase
{
public:
  CongestionControlRecord();
  CongestionControlRecord(const CongestionControlRecord &rec);
  ~CongestionControlRecord();
  config_parse_error Init(matcher_line *line_info);
  void UpdateMatch(CongestionControlRule *pRule, RequestData *rdata);
  void Print();

  void cleanup();
  void setdefault();
  config_parse_error validate();

  int rank; // matching preference
            /*
             * Select the first matching rule specified in congestion.config
             * rank     Matches
             *   3       dest && prefix && port
             *   2       dest && port
             *   1       dest && prefix
             *   0       dest
             */

  char *prefix;
  int prefix_len;
  unsigned short port;
  int congestion_scheme;
  char *error_page;

  int max_connection_failures;
  int fail_window;
  int proxy_retry_interval;
  int client_wait_interval;
  int wait_interval_alpha;
  int live_os_conn_timeout;
  int live_os_conn_retries;
  int dead_os_conn_timeout;
  int dead_os_conn_retries;
  int max_connection;

  CongestionControlRecord *pRecord;
  int32_t ref_count;

  void
  get()
  {
    ink_atomic_increment(&ref_count, 1);
  }
  void
  put()
  {
    if (ink_atomic_increment(&ref_count, -1) == 1)
      delete this;
  }
};

inline CongestionControlRule::CongestionControlRule() : record(NULL)
{
}

inline CongestionControlRule::~CongestionControlRule()
{
  record = NULL;
}

inline CongestionControlRecord::CongestionControlRecord()
  : rank(0),
    prefix(NULL),
    prefix_len(0),
    port(0),
    congestion_scheme(PER_IP),
    error_page(NULL),
    max_connection_failures(5),
    fail_window(120),
    proxy_retry_interval(10),
    client_wait_interval(300),
    wait_interval_alpha(30),
    live_os_conn_timeout(60),
    live_os_conn_retries(2),
    dead_os_conn_timeout(15),
    dead_os_conn_retries(1),
    max_connection(-1),
    pRecord(NULL),
    ref_count(0)
{
}

inline CongestionControlRecord::~CongestionControlRecord()
{
  cleanup();
}
inline void
CongestionControlRecord::cleanup()
{
  if (pRecord) {
    pRecord->put();
    pRecord = NULL;
  }
  ats_free(prefix), prefix         = NULL;
  ats_free(error_page), error_page = NULL;
}

typedef unsigned short cong_hist_t;
#define CONG_HIST_ENTRIES 17

// CongestionEntry
struct FailHistory {
  long start;
  int bin_len;
  int length;
  cong_hist_t bins[CONG_HIST_ENTRIES];
  int cur_index;
  long last_event;
  int events;

  FailHistory() : start(0), bin_len(0), length(0), cur_index(0), last_event(0), events(0) { bzero((void *)&bins, sizeof(bins)); }
  void init(int window);
  void init_event(long t, int n = 1);
  int regist_event(long t, int n = 1);
  int
  get_bin_events(int index)
  {
    return bins[(index + 1 + cur_index) % CONG_HIST_ENTRIES];
  }
};

struct CongestionEntry : public RequestData {
  // key in the hash table;
  uint64_t m_key;
  // host info
  IpEndpoint m_ip;
  char *m_hostname;

  // Pointer to the congestion.config entry
  // Remember to update the refcount of pRecord
  CongestionControlRecord *pRecord;

  // State -- connection failures
  FailHistory m_history;
  Ptr<ProxyMutex> m_hist_lock;
  ink_hrtime m_last_congested;
  volatile int m_congested; // 0 | 1
  int m_stat_congested_conn_failures;

  volatile int m_M_congested;
  ink_hrtime m_last_M_congested;

  // State -- concorrent connections
  int m_num_connections;
  int m_stat_congested_max_conn;

  // Reference count
  int m_ref_count;

  CongestionEntry(const char *hostname, sockaddr const *ip, CongestionControlRecord *rule, uint64_t key);
  CongestionEntry();
  virtual ~CongestionEntry();

  /* RequestData virtural functions */
  virtual char *
  get_string()
  {
    return pRecord->prefix;
  }
  virtual const char *
  get_host()
  {
    return m_hostname;
  }
  virtual sockaddr const *
  get_ip()
  {
    return &m_ip.sa;
  }
  virtual const sockaddr *
  get_client_ip()
  {
    return NULL;
  }

  /* print the entry into the congested list output buffer */
  int sprint(char *buf, int buflen, int format = 0);

  /* reference counter manipulation */
  void get();
  void put();

  /* congestion control functions */
  // Is the server congested?
  bool F_congested();
  bool M_congested(ink_hrtime t);
  bool congested();

  // Update state info
  void go_alive();
  void failed_at(ink_hrtime t);
  void connection_opened();
  void connection_closed();

  // Connection controls
  bool proxy_retry(ink_hrtime t);
  int client_retry_after();
  int connect_retries();
  int connect_timeout();
  char *
  getErrorPage()
  {
    return pRecord->error_page;
  }

  // stats
  void stat_inc_F();
  void stat_inc_M();

  // fail history operations
  void clearFailHistory();
  bool compCongested();

  // CongestionEntry and CongestionControl rules interaction helper functions
  bool usefulInfo(ink_hrtime t);
  bool validate();
  void applyNewRule(CongestionControlRecord *rule);
  void init(CongestionControlRecord *rule);
};

inline bool
CongestionEntry::usefulInfo(ink_hrtime t)
{
  return (m_ref_count > 1 || m_congested != 0 || m_num_connections > 0 ||
          (m_history.last_event + pRecord->fail_window > t && m_history.events > 0));
}

inline int
CongestionEntry::client_retry_after()
{
  int prat = 0;
  if (F_congested()) {
    prat = pRecord->proxy_retry_interval + m_history.last_event - ink_hrtime_to_sec(Thread::get_hrtime());
    if (prat < 0)
      prat = 0;
  }
  return (prat + pRecord->client_wait_interval + CongestionRand.random() % pRecord->wait_interval_alpha);
}

inline bool
CongestionEntry::proxy_retry(ink_hrtime t)
{
  return ((ink_hrtime_to_sec(t) - m_history.last_event) >= pRecord->proxy_retry_interval);
}

inline bool
CongestionEntry::F_congested()
{
  return m_congested == 1;
}

inline bool
CongestionEntry::M_congested(ink_hrtime t)
{
  if (pRecord->max_connection >= 0 && m_num_connections >= pRecord->max_connection) {
    if (ink_atomic_swap(&m_M_congested, 1) == 0) {
      m_last_M_congested = t;
      // TODO: Used to signal congestions
    }
    return true;
  }
  return false;
}

inline bool
CongestionEntry::congested()
{
  return (F_congested() || m_M_congested == 1);
}

inline int
CongestionEntry::connect_retries()
{
  if (F_congested()) {
    return pRecord->dead_os_conn_retries;
  } else {
    return pRecord->live_os_conn_retries;
  }
}

inline int
CongestionEntry::connect_timeout()
{
  if (F_congested()) {
    return pRecord->dead_os_conn_timeout;
  } else {
    return pRecord->live_os_conn_timeout;
  }
}

inline void
CongestionEntry::stat_inc_F()
{
  ink_atomic_increment(&m_stat_congested_conn_failures, 1);
}

inline void
CongestionEntry::stat_inc_M()
{
  ink_atomic_increment(&m_stat_congested_max_conn, 1);
}

inline bool
CongestionEntry::compCongested()
{
  if (m_congested)
    return true;
  if (pRecord->max_connection_failures == -1)
    return false;
  return pRecord->max_connection_failures <= m_history.events;
}

// return true when max_conn state changed
inline void
CongestionEntry::connection_opened()
{
  ink_atomic_increment(&m_num_connections, 1);
}

// return true when max_conn state changed
inline void
CongestionEntry::connection_closed()
{
  ink_atomic_increment(&m_num_connections, -1);
  if (ink_atomic_swap(&m_M_congested, 0) == 1) {
    // TODO: Used to signal not congested
  }
}

inline void
CongestionEntry::clearFailHistory()
{
  m_history.init(pRecord->fail_window);
  m_congested = 0;
}

inline CongestionEntry::CongestionEntry()
  : m_key(0),
    m_hostname(NULL),
    pRecord(NULL),
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
  m_hist_lock = new_ProxyMutex();
}

inline CongestionEntry::~CongestionEntry()
{
  if (m_hostname)
    ats_free(m_hostname), m_hostname = NULL;
  m_hist_lock = NULL;
  if (pRecord)
    pRecord->put(), pRecord = NULL;
}

inline void
CongestionEntry::get()
{
  ink_atomic_increment(&m_ref_count, 1);
}

inline void
CongestionEntry::put()
{
  if (ink_atomic_increment(&m_ref_count, -1) == 1) {
    delete this;
  }
}

// API to outside world

extern int congestionControlEnabled;
extern int congestionControlLocalTime;

void initCongestionControl();
CongestionControlRecord *CongestionControlled(RequestData *rdata);

uint64_t make_key(char *hostname, int len, sockaddr const *ip, CongestionControlRecord *record);
uint64_t make_key(char *hostname, sockaddr const *ip, CongestionControlRecord *record);
uint64_t make_key(char *hostname, int len, sockaddr const *ip, char *prefix, int prelen, short port = 0);

//----------------------------------------------------
// the following functions are actually declared in
// CongestionDB.h and defined in CongestionDB.cc
// They are included here only to make the
// editing & compiling process faster
//----------------------------------------------------
extern Action *get_congest_entry(Continuation *cont, HttpRequestData *data, CongestionEntry **ppEntry);
extern Action *get_congest_list(Continuation *cont, MIOBuffer *buffer, int format);

extern void remove_congested_entry(uint64_t key);
extern void remove_all_congested_entry(void);
extern void remove_congested_entry(char *buf, MIOBuffer *out_buffer);

#endif /* CONGESTTION_H_ */
