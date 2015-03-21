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

#include "libts.h"

#include "Main.h"
#include "Update.h"
#include "ProxyConfig.h"
#include "StatSystem.h"
#include "HttpUpdateSM.h"
#include "HttpDebugNames.h"
#include "URL.h"
#include "HdrUtils.h"
#include <records/I_RecHttp.h>
#include "I_Layout.h"

RecRawStatBlock *update_rsb;

#define UpdateEstablishStaticConfigInteger(_ix, _n) REC_EstablishStaticConfigInteger(_ix, _n);

#define UPDATE_INCREMENT_DYN_STAT(x) RecIncrRawStat(update_rsb, mutex->thread_holding, (int)x, 1);
#define UPDATE_DECREMENT_DYN_STAT(x) RecIncrRawStat(update_rsb, mutex->thread_holding, (int)x, -1);
#define UPDATE_READ_DYN_STAT(x, C, S)         \
  RecGetRawStatCount(update_rsb, (int)x, &C); \
  RecGetRawStatSum(update_rsb, (int)x, &S);

#define UPDATE_CLEAR_DYN_STAT(x)          \
  do {                                    \
    RecSetRawStatSum(update_rsb, x, 0);   \
    RecSetRawStatCount(update_rsb, x, 0); \
  } while (0);

#define UPDATE_ConfigReadInteger REC_ConfigReadInteger
#define UPDATE_ConfigReadString REC_ConfigReadString
#define UPDATE_RegisterConfigUpdateFunc REC_RegisterConfigUpdateFunc


// Fundamental constants

static const char *const GET_METHOD = "GET ";
static const char *const HTTP_VERSION = " HTTP/1.0";
static const char *const REQUEST_TERMINATOR = "\r\n\r\n";
static const char *const TERMINATOR = "\r\n";
static const char *const HTML_COMMENT_TAG = "!--";
static const char *const HTML_COMMENT_END = "-->";
static const int MAX_LINE_LENGTH = (32 * 1024);

// Fundamental constants initialized by UpdateManager::start()

static int len_GET_METHOD = 0;
static int len_HTTP_VERSION = 0;
static int len_REQUEST_TERMINATOR = 0;
static int len_TERMINATOR = 0;

struct html_tag update_allowable_html_tags[] = {{"a", "href"},
                                                {"img", "src"},
                                                {"img", "href"},
                                                {"body", "background"},
                                                {"frame", "src"},
                                                {"iframe", "src"},
                                                {"fig", "src"},
                                                {"overlay", "src"},
                                                {"applet", "code"},
                                                {"script", "src"},
                                                {"embed", "src"},
                                                {"bgsound", "src"},
                                                {"area", "href"},
                                                {"base", "href"},    // special handling
                                                {"meta", "content"}, // special handling
                                                {NULL, NULL}};

struct schemes_descriptor {
  const char *tag;
  int tag_len;
};

struct schemes_descriptor proto_schemes[] = {{"cid:", 0},
                                             {"clsid:", 0},
                                             {"file:", 0},
                                             {"finger:", 0},
                                             {"ftp:", 0},
                                             {"gopher:", 0},
                                             {"hdl:", 0},
                                             {"http:", 0},
                                             {"https:", 0},
                                             {"ilu:", 0},
                                             {"ior:", 0},
                                             {"irc:", 0},
                                             {"java:", 0},
                                             {"javascript:", 0},
                                             {"lifn:", 0},
                                             {"mailto:", 0},
                                             {"mid:", 0},
                                             {"news:", 0},
                                             {"path:", 0},
                                             {"prospero:", 0},
                                             {"rlogin:", 0},
                                             {"service:", 0},
                                             {"shttp:", 0},
                                             {"snews:", 0},
                                             {"stanf:", 0},
                                             {"telnet:", 0},
                                             {"tn3270:", 0},
                                             {"wais:", 0},
                                             {"whois++:", 0},
                                             {NULL, 0}};

struct schemes_descriptor supported_proto_schemes[] = {{
                                                         "http:",
                                                       },
                                                       {NULL, 0}};

static int global_id = 1;

void
init_proto_schemes()
{
  int n;
  for (n = 0; proto_schemes[n].tag; ++n) {
    proto_schemes[n].tag_len = strlen(proto_schemes[n].tag);
  }
}

void
init_supported_proto_schemes()
{
  int n;
  for (n = 0; supported_proto_schemes[n].tag; ++n) {
    supported_proto_schemes[n].tag_len = strlen(supported_proto_schemes[n].tag);
  }
}

///////////////////////////////////////////////////////////////////////////////
// Class UpdateConfigParams
//      Global subsystem configuration parameters
///////////////////////////////////////////////////////////////////////////////

UpdateConfigParams::UpdateConfigParams()
  : _enabled(0), _immediate_update(0), _retry_count(0), _retry_interval(0), _concurrent_updates(0), _max_update_state_machines(0),
    _memory_use_in_mb(0)
{
}

UpdateConfigParams::UpdateConfigParams(UpdateConfigParams &p)
{
  _enabled = p._enabled;
  _immediate_update = p._immediate_update;
  _retry_count = p._retry_count;
  _retry_interval = p._retry_interval;
  _concurrent_updates = p._concurrent_updates;
  _max_update_state_machines = p._max_update_state_machines;
  _memory_use_in_mb = p._memory_use_in_mb;
}

UpdateConfigParams::~UpdateConfigParams()
{
}

UpdateConfigParams &UpdateConfigParams::operator=(UpdateConfigParams &p)
{
  _enabled = p._enabled;
  _immediate_update = p._immediate_update;
  _retry_count = p._retry_count;
  _retry_interval = p._retry_interval;
  _concurrent_updates = p._concurrent_updates;
  _max_update_state_machines = p._max_update_state_machines;
  _memory_use_in_mb = p._memory_use_in_mb;
  return *this;
}

int UpdateConfigParams::operator==(UpdateConfigParams &p)
{
  if (_enabled != p._enabled)
    return 0;
  if (_immediate_update != p._immediate_update)
    return 0;
  if (_retry_count != p._retry_count)
    return 0;
  if (_retry_interval != p._retry_interval)
    return 0;
  if (_concurrent_updates != p._concurrent_updates)
    return 0;
  if (_max_update_state_machines != p._max_update_state_machines)
    return 0;
  if (_memory_use_in_mb != p._memory_use_in_mb)
    return 0;
  return 1;
}

/////////////////////////////////////////////////////////////////////////////
// Class UpdateEntry
//      Per update object descriptor
/////////////////////////////////////////////////////////////////////////////

UpdateEntry::UpdateEntry()
  : _group_link(0), _hash_link(0), _id(0), _url(0), _URLhandle(), _terminal_url(0), _request_headers(0), _num_request_headers(0),
    _http_hdr(0), _offset_hour(0), _interval(0), _max_depth(0), _start_time(0), _expired(0), _scheme_index(-1),
    _update_event_status(0)
{
  http_parser_init(&_http_parser);
  _http_parser.m_allow_non_http = true;
}

UpdateEntry::~UpdateEntry()
{
  ats_free(_url);
  _url = NULL;

  if (_URLhandle.valid()) {
    _URLhandle.destroy();
  }

  ats_free(_request_headers);
  _request_headers = NULL;

  // INKqa12891: _http_hdr can be NULL
  if (_http_hdr && _http_hdr->valid()) {
    _http_hdr->destroy();
    delete _http_hdr;
    _http_hdr = NULL;
  }
  _indirect_list = NULL;
}

void
UpdateEntry::Init(int derived_url)
{
  _id = ink_atomic_increment(&global_id, 1);
  if (derived_url) {
    return;
  }
  ComputeScheduleTime();

  int scheme_len;
  const char *scheme = _URLhandle.scheme_get(&scheme_len);
  if (scheme != URL_SCHEME_HTTP) {
    // Depth is only valid for scheme "http"
    _max_depth = 0;
  }
}

int
UpdateEntry::ValidURL(char *s, char *e)
{
  // Note: string 's' is null terminated.

  const char *url_start = s;
  char *url_end = e;
  int err;

  _URLhandle.create(NULL);
  err = _URLhandle.parse(&url_start, url_end);
  if (err >= 0) {
    _url = ats_strdup(s);
    return 0; // Valid URL
  } else {
    _URLhandle.destroy();
    return 1; // Invalid URL
  }
  return 0;
}

int
UpdateEntry::ValidHeaders(char *s)
{
  // Note: string 's' is null terminated.

  enum {
    FIND_START_OF_HEADER_NAME = 1,
    SCAN_FOR_HEADER_NAME,
    SCAN_FOR_END_OF_HEADER_VALUE,
  };

  char *p = s;
  char *t;
  int bad_header = 0;
  int end_of_headers = 0;
  int scan_state = FIND_START_OF_HEADER_NAME;

  while (*p) {
    switch (scan_state) {
    case FIND_START_OF_HEADER_NAME: {
      if (!ValidHeaderNameChar(*p)) {
        bad_header = 1;
        break;
      } else {
        scan_state = SCAN_FOR_HEADER_NAME;
        break;
      }
    }
    case SCAN_FOR_HEADER_NAME: {
      if (!ValidHeaderNameChar(*p)) {
        if (*p == ':') {
          scan_state = SCAN_FOR_END_OF_HEADER_VALUE;
          break;
        } else {
          bad_header = 1;
          break;
        }
      } else {
        // Get next char
        break;
      }
    }
    case SCAN_FOR_END_OF_HEADER_VALUE: {
      t = strchr(p, '\r');
      if (t) {
        if (*(t + 1) == '\n') {
          p = t + 1;
          ++_num_request_headers;
          scan_state = FIND_START_OF_HEADER_NAME;
          break;
        } else {
          bad_header = 1;
          break;
        }
      } else {
        t = strchr(p, 0);
        if (t) {
          ++_num_request_headers;
          end_of_headers = 1;
        } else {
          bad_header = 1;
        }
        break;
      }
    }
    } // End of switch

    if (bad_header) {
      if (_num_request_headers) {
        return 1; // Fail; Bad header with > 1 valid headers
      } else {
        if (p == s) {
          return 0; // OK; user specified no headers
        } else {
          return 1; // Fail; first header is invalid
        }
      }
    } else {
      if (end_of_headers) {
        break;
      } else {
        ++p;
      }
    }
  }

  // At least 1 valid header exists

  _request_headers = ats_strdup(s);
  return 0; // OK; > 1 valid headers
}

int
UpdateEntry::BuildHttpRequest()
{
  // Given the HTTP request and associated headers,
  // transform the data into a HTTPHdr object.

  char request[MAX_LINE_LENGTH];
  int request_size;

  request_size = len_GET_METHOD + strlen(_url) + len_HTTP_VERSION +
                 (_request_headers ? len_TERMINATOR + strlen(_request_headers) : 0) + len_REQUEST_TERMINATOR + 1;
  if (request_size > MAX_LINE_LENGTH) {
    return 1;
  }
  if (_request_headers) {
    snprintf(request, sizeof(request), "%s%s%s%s%s%s", GET_METHOD, _url, HTTP_VERSION, TERMINATOR, _request_headers,
             REQUEST_TERMINATOR);
  } else {
    snprintf(request, sizeof(request), "%s%s%s%s", GET_METHOD, _url, HTTP_VERSION, REQUEST_TERMINATOR);
  }
  _http_hdr = new HTTPHdr;
  http_parser_init(&_http_parser);
  _http_hdr->create(HTTP_TYPE_REQUEST);
  int err;
  const char *start = request;
  const char *end = start + request_size - 1;

  while (start < end) {
    err = _http_hdr->parse_req(&_http_parser, &start, end, false);
    if (err != PARSE_CONT) {
      break;
    }
    end = start + strlen(start);
  }
  http_parser_clear(&_http_parser);
  return 0;
}

int
UpdateEntry::ValidHeaderNameChar(char c)
{
  if ((c > 31) && (c < 127)) {
    if (ValidSeparatorChar(c)) {
      return 0; // Invalid
    } else {
      return 1; // Valid
    }
  } else {
    return 0; // Invalid
  }
}

int
UpdateEntry::ValidSeparatorChar(char c)
{
  switch (c) {
  case '(':
  case ')':
  case '<':
  case '>':
  case '@':
  case ',':
  case ';':
  case ':':
  case '\\':
  case '"':
  case '/':
  case '[':
  case ']':
  case '?':
  case '=':
  case '{':
  case '}':
  case ' ':
  case '\t':
    return 1; // Valid separator char
  default:
    return 0;
  }
}

int
UpdateEntry::ValidHour(char *s)
{
  // Note: string 's' is null terminated.

  _offset_hour = atoi(s);
  if ((_offset_hour >= MIN_OFFSET_HOUR) && (_offset_hour <= MAX_OFFSET_HOUR)) {
    return 0; // Valid data
  } else {
    return 1; // Invalid data
  }
}

int
UpdateEntry::ValidInterval(char *s)
{
  // Note: string 's' is null terminated.

  _interval = atoi(s);
  if ((_interval >= MIN_INTERVAL) && (_interval <= MAX_INTERVAL)) {
    return 0; // Valid data
  } else {
    return 1; // Invalid data
  }
  return 0;
}

int
UpdateEntry::ValidDepth(char *s)
{
  // Note: string 's' is null terminated.

  _max_depth = atoi(s);

  if ((_max_depth >= MIN_DEPTH) && (_max_depth <= MAX_DEPTH)) {
    return 0; // Valid data
  } else {
    return 1; // Invalid data
  }
  return 0;
}

void
UpdateEntry::SetTerminalStatus(int term_url)
{
  _terminal_url = term_url;
}

int
UpdateEntry::TerminalURL()
{
  return _terminal_url;
}


void
UpdateEntry::ComputeScheduleTime()
{
  ink_hrtime ht;
  time_t cur_time;
  struct tm cur_tm;

  if (_expired) {
    _expired = 0;
  } else {
    if (_start_time) {
      return;
    }
  }

  ht = ink_get_based_hrtime();
  cur_time = ht / HRTIME_SECOND;

  if (!_start_time) {
    time_t zero_hour; // absolute time of offset hour.

    // Get the current time in a TM struct so we can
    // zero out the minute and second.
    ink_localtime_r(&cur_time, &cur_tm);
    cur_tm.tm_hour = _offset_hour;
    cur_tm.tm_min = 0;
    cur_tm.tm_sec = 0;
    // Now we can find out when the offset hour is today.
    zero_hour = convert_tm(&cur_tm);
    // If it's in the future, back up a day and use that as the base.
    if (zero_hour > cur_time)
      zero_hour -= 24 * SECONDS_PER_HOUR;
    _start_time = cur_time + (_interval - ((cur_time - zero_hour) % _interval));
  } else {
    // Compute next start time
    _start_time += _interval;
  }
}

int
UpdateEntry::ScheduleNow(time_t cur_time)
{
  if (cur_time >= _start_time) {
    _expired = 1;
    return 1;
  } else {
    return 0;
  }
}

/////////////////////////////////////////////////////////////////////////////
// Class UpdateConfigList
//      Container for UpdateEntry objects
/////////////////////////////////////////////////////////////////////////////
UpdateConfigList::UpdateConfigList() : _entry_q_elements(0), _pending_q_elements(0), _hash_table(0)
{
}

UpdateConfigList::~UpdateConfigList()
{
  if (_hash_table) {
    delete[] _hash_table;
    _hash_table = NULL;
  }
}

void
UpdateConfigList::Add(UpdateEntry *e)
{
  _entry_q_elements++;
  _entry_q.enqueue(e);
}

int
UpdateConfigList::HashAdd(UpdateEntry *e)
{
  uint64_t folded64 = e->_url_md5.fold();
  ink_assert(folded64);
  int32_t index = folded64 % HASH_TABLE_SIZE;

  if (!_hash_table) {
    // One time initialization

    _hash_table = new UpdateEntry *[HASH_TABLE_SIZE];
    memset((char *)_hash_table, 0, (sizeof(UpdateEntry *) * HASH_TABLE_SIZE));
  }
  // Add to hash table only if unique

  UpdateEntry *he = _hash_table[index];
  UpdateEntry **last_link = &_hash_table[index];

  while (he) {
    if (e->_url_md5 == he->_url_md5) {
      return 1; // duplicate detected
    } else {
      last_link = &he->_hash_link;
      he = he->_hash_link;
    }
  }

  // Entry is unique, add to hash list

  e->_hash_link = *last_link;
  *last_link = e;

  // Add to entry queue

  Add(e);

  return 0; // Entry added
}

UpdateEntry *
UpdateConfigList::Remove()
{
  UpdateEntry *e = _entry_q.dequeue();
  if (e) {
    _entry_q_elements--;
  }
  return e;
}

void
UpdateConfigList::AddPending(UpdateEntry *e)
{
  _pending_q_elements++;
  _pending_q.enqueue(e);
}

UpdateEntry *
UpdateConfigList::RemovePending()
{
  UpdateEntry *e = _pending_q.dequeue();
  if (e) {
    _pending_q_elements--;
  }
  return e;
}

/////////////////////////////////////////////////////////////////////////////
// Class UpdateManager
//      External interface to Update subsystem
/////////////////////////////////////////////////////////////////////////////

UpdateManager::UpdateManager() : _CM(0), _SCH(0)
{
}

UpdateManager::~UpdateManager()
{
}

int
UpdateManager::start()
{
  // Initialize fundamental constants

  len_GET_METHOD = strlen(GET_METHOD);
  len_HTTP_VERSION = strlen(HTTP_VERSION);
  len_REQUEST_TERMINATOR = strlen(REQUEST_TERMINATOR);
  len_TERMINATOR = strlen(TERMINATOR);
  init_proto_schemes();
  init_supported_proto_schemes();

  _CM = new UpdateConfigManager;
  _CM->init();

  _SCH = new UpdateScheduler(_CM);
  _SCH->Init();

  return 0;
}

UpdateManager updateManager;

typedef int (UpdateConfigManager::*UpdateConfigManagerContHandler)(int, void *);
/////////////////////////////////////////////////////////////////////////////
// Class UpdateConfigManager
//      Handle Update subsystem global configuration and URL list updates
/////////////////////////////////////////////////////////////////////////////
UpdateConfigManager::UpdateConfigManager() : Continuation(new_ProxyMutex()), _periodic_event(0), _filename(0)
{
  SET_HANDLER((UpdateConfigManagerContHandler)&UpdateConfigManager::ProcessUpdate);
}

UpdateConfigManager::~UpdateConfigManager()
{
}

int
UpdateConfigManager::init()
{
  update_rsb = RecAllocateRawStatBlock((int)update_stat_count);

  _CP_actual = new UpdateConfigParams;

  // Setup update handlers for each global configuration parameter

  UpdateEstablishStaticConfigInteger(_CP_actual->_enabled, "proxy.config.update.enabled");

  UpdateEstablishStaticConfigInteger(_CP_actual->_immediate_update, "proxy.config.update.force");

  UpdateEstablishStaticConfigInteger(_CP_actual->_retry_count, "proxy.config.update.retry_count");

  UpdateEstablishStaticConfigInteger(_CP_actual->_retry_interval, "proxy.config.update.retry_interval");

  UpdateEstablishStaticConfigInteger(_CP_actual->_concurrent_updates, "proxy.config.update.concurrent_updates");

  UpdateEstablishStaticConfigInteger(_CP_actual->_max_update_state_machines, "proxy.config.update.max_update_state_machines");

  UpdateEstablishStaticConfigInteger(_CP_actual->_memory_use_in_mb, "proxy.config.update.memory_use_mb");

  // Register Scheduled Update stats

  RecRegisterRawStat(update_rsb, RECT_PROCESS, "proxy.process.update.successes", RECD_INT, RECP_NON_PERSISTENT,
                     (int)update_successes_stat, RecRawStatSyncCount);
  UPDATE_CLEAR_DYN_STAT(update_successes_stat);

  RecRegisterRawStat(update_rsb, RECT_PROCESS, "proxy.process.update.no_actions", RECD_INT, RECP_NON_PERSISTENT,
                     (int)update_no_actions_stat, RecRawStatSyncCount);
  UPDATE_CLEAR_DYN_STAT(update_no_actions_stat);

  RecRegisterRawStat(update_rsb, RECT_PROCESS, "proxy.process.update.fails", RECD_INT, RECP_NON_PERSISTENT, (int)update_fails_stat,
                     RecRawStatSyncCount);
  UPDATE_CLEAR_DYN_STAT(update_fails_stat);

  RecRegisterRawStat(update_rsb, RECT_PROCESS, "proxy.process.update.unknown_status", RECD_INT, RECP_NON_PERSISTENT,
                     (int)update_unknown_status_stat, RecRawStatSyncCount);
  UPDATE_CLEAR_DYN_STAT(update_unknown_status_stat);

  RecRegisterRawStat(update_rsb, RECT_PROCESS, "proxy.process.update.state_machines", RECD_INT, RECP_NON_PERSISTENT,
                     (int)update_state_machines_stat, RecRawStatSyncCount);
  UPDATE_CLEAR_DYN_STAT(update_state_machines_stat);

  Debug("update", "Update params: enable %" PRId64 " force %" PRId64 " rcnt %" PRId64 " rint %" PRId64 " updates %" PRId64 " "
                  "max_sm %" PRId64 " mem %" PRId64 "",
        _CP_actual->_enabled, _CP_actual->_immediate_update, _CP_actual->_retry_count, _CP_actual->_retry_interval,
        _CP_actual->_concurrent_updates, _CP_actual->_max_update_state_machines, _CP_actual->_memory_use_in_mb);

  // Make working and actual global config copies equal

  _CP = new UpdateConfigParams(*_CP_actual);

  // Setup "update.config" update handler

  SetFileName((char *)"update.config");
  REC_RegisterConfigUpdateFunc("proxy.config.update.update_configuration", URL_list_update_callout, (void *)this);

  // Simulate configuration update to sync working and current databases

  handleEvent(EVENT_IMMEDIATE, (Event *)NULL);

  // Setup periodic to detect global config updates

  _periodic_event = eventProcessor.schedule_every(this, HRTIME_SECONDS(10));

  return 0;
}

int
UpdateConfigManager::GetConfigParams(Ptr<UpdateConfigParams> *P)
{
  MUTEX_TRY_LOCK(lock, mutex, this_ethread());
  if (!lock.is_locked()) {
    return 0; // Try again later
  } else {
    *P = _CP;
    return 1; // Success
  }
}

int
UpdateConfigManager::GetConfigList(Ptr<UpdateConfigList> *L)
{
  MUTEX_TRY_LOCK(lock, mutex, this_ethread());
  if (!lock.is_locked()) {
    return 0; // Try again later
  } else {
    *L = _CL;
    return 1; // Success
  }
}

int
UpdateConfigManager::URL_list_update_callout(const char * /* name ATS_UNUSED */, RecDataT /* data_type ATS_UNUSED */, RecData data,
                                             void *cookie)
{
  UpdateConfigManager *cm = (UpdateConfigManager *)cookie;
  cm->SetFileName((char *)data.rec_string);


  // URL update may block in file i/o.
  // Reschedule on ET_CACHE thread.

  eventProcessor.schedule_imm(cm, ET_CACHE);

  return 0;
}

int
UpdateConfigManager::ProcessUpdate(int event, Event *e)
{
  if (event == EVENT_IMMEDIATE) {
    ////////////////////////////////////////////////////////////////////
    // EVENT_IMMEDIATE -- URL list update
    ////////////////////////////////////////////////////////////////////

    UpdateConfigList *l = NULL;

    l = BuildUpdateList();
    if (l) {
      _CL = l;
    }
    return EVENT_DONE;
  }

  if (event == EVENT_INTERVAL) {
    ////////////////////////////////////////////////////////////////////
    // EVENT_INTERVAL  -- Global configuration update check
    ////////////////////////////////////////////////////////////////////

    UpdateConfigParams *p = new UpdateConfigParams(*_CP_actual);

    if (!(*_CP == *p)) {
      _CP = p;
      Debug("update", "enable %" PRId64 " force %" PRId64 " rcnt %" PRId64 " rint %" PRId64 " updates %" PRId64
                      " state machines %" PRId64 " mem %" PRId64 "",
            p->_enabled, p->_immediate_update, p->_retry_count, p->_retry_interval, p->_concurrent_updates,
            p->_max_update_state_machines, p->_memory_use_in_mb);
    } else {
      delete p;
    }
    return EVENT_DONE;
  }
  // Unknown event, ignore it.

  Debug("update", "ProcessUpdate: Unknown event %d %p", event, e);
  return EVENT_DONE;
}

UpdateConfigList *
UpdateConfigManager::BuildUpdateList()
{
  // Build pathname to "update.config" and open file
  ats_scoped_str config_path;

  if (_filename) {
    config_path = RecConfigReadConfigPath(NULL, _filename);
  } else {
    return (UpdateConfigList *)NULL;
  }

  int fd = open(config_path, O_RDONLY);
  if (fd < 0) {
    Warning("read update.config, open failed");
    SignalWarning(MGMT_SIGNAL_CONFIG_ERROR, "read update.config, open failed");
    return (UpdateConfigList *)NULL;
  }
  return ParseConfigFile(fd);
}

int
UpdateConfigManager::GetDataLine(int fd, int bufsize, char *buf, int field_delimiters, int delimiter)
{
  char *line = buf;
  int linesize = bufsize;
  int bytes_read = 0;
  int rlen;

  while ((rlen = ink_file_fd_readline(fd, linesize, line)) > 0) {
    ////////////////////////////////////////////////////////////////////
    // Notes:
    //      1) ink_file_fd_readline() null terminates returned buffer
    //      2) Input processing guarantees that the item delimiter '\'
    //         does not exist in any data field.
    ////////////////////////////////////////////////////////////////////

    if (0 == bytes_read) {
      // A comment line, just return.
      if (*line == '#')
        return rlen;
      else if (1 == rlen)
        continue; // leading blank line, ignore.
    }
    bytes_read += rlen;

    // Determine if we have a complete line.

    char *p = buf;
    int delimiters_found = 0;

    while (*p) {
      if (*p == delimiter) {
        delimiters_found++;
      }
      p++;
    }
    if (delimiters_found == field_delimiters) {
      // We have a complete line.
      return bytes_read;

    } else if ((delimiters_found == (field_delimiters - 1)) && (*(p - 1) == '\n')) {
      // End of line not delimited.
      // Fix it and consider it a complete line.

      *(p - 1) = '\\';
      return bytes_read;
    }
    // Resume read
    line += rlen;
    linesize -= rlen;
  }
  return 0;
}

UpdateConfigList *
UpdateConfigManager::ParseConfigFile(int f)
{
  /*
     "update.config" line syntax:
     <URL>\<Request Headers>\<Offset Hour>\<Interval>\<Recursion depth>\
   */

  enum {
    F_URL,
    F_HEADERS,
    F_HOUR,
    F_INTERVAL,
    F_DEPTH,
    F_ITEMS,
  };
  char *p_start[F_ITEMS];
  char *p_end[F_ITEMS];

  char line[MAX_LINE_LENGTH];
  char *p;

  int ln = 0;
  int i;

  UpdateEntry *e = NULL;
  UpdateConfigList *ul = new UpdateConfigList;

  while (GetDataLine(f, sizeof(line) - 1, line, F_ITEMS, '\\') > 0) {
    ++ln;
    if (*line == '#') {
      continue;
    } else {
      p = line;
    }

    // Extract fields

    for (i = 0; i < F_ITEMS; ++i) {
      p_start[i] = p;
      p_end[i] = strchr(p, '\\');
      *p_end[i] = 0; // Null terminate string

      if (p_end[i]) {
        p = p_end[i] + 1;
      } else {
        Warning("read update.config, invalid syntax, line %d", ln);
        SignalWarning(MGMT_SIGNAL_CONFIG_ERROR, "read update.config, invalid syntax");
        break;
      }
    }
    if (i < F_ITEMS) {
      // Syntax error
      goto abort_processing;
    }
    // Validate data fields

    e = new UpdateEntry;

    ////////////////////////////////////
    // Validate URL
    ////////////////////////////////////
    if (e->ValidURL(p_start[F_URL], p_end[F_URL])) {
      Warning("read update.config, invalid URL field, line %d", ln);
      SignalWarning(MGMT_SIGNAL_CONFIG_ERROR, "read update.config, invalid URL field");
      goto abort_processing;
    }
    ////////////////////////////////////
    // Validate headers
    ////////////////////////////////////
    if (e->ValidHeaders(p_start[F_HEADERS])) {
      Warning("read update.config, invalid headers field, line %d", ln);
      SignalWarning(MGMT_SIGNAL_CONFIG_ERROR, "read update.config, invalid headers field");
      goto abort_processing;
    }
    /////////////////////////////////////////////////////////////
    // Convert request (URL+Headers) into HTTPHdr format.
    /////////////////////////////////////////////////////////////
    if (e->BuildHttpRequest()) {
      Warning("read update.config, header processing error, line %d", ln);
      SignalWarning(MGMT_SIGNAL_CONFIG_ERROR, "read update.config, header processing error");
      goto abort_processing;
    }
    ////////////////////////////////////
    // Validate hour
    ////////////////////////////////////
    if (e->ValidHour(p_start[F_HOUR])) {
      Warning("read update.config, invalid hour field, line %d", ln);
      SignalWarning(MGMT_SIGNAL_CONFIG_ERROR, "read update.config, invalid hour field");
      goto abort_processing;
    }
    ////////////////////////////////////
    // Validate interval
    ////////////////////////////////////
    if (e->ValidInterval(p_start[F_INTERVAL])) {
      Warning("read update.config, invalid interval field, line %d", ln);
      SignalWarning(MGMT_SIGNAL_CONFIG_ERROR, "read update.config, invalid interval field");
      goto abort_processing;
    }
    ////////////////////////////////////
    // Validate recursion depth
    ////////////////////////////////////
    if (e->ValidDepth(p_start[F_DEPTH])) {
      Warning("read update.config, invalid depth field, line %d", ln);
      SignalWarning(MGMT_SIGNAL_CONFIG_ERROR, "read update.config, invalid depth field");
      goto abort_processing;
    }
    // Valid entry, add to list

    e->Init();
    Debug("update", "[%d] [%s] [%s] nhdrs %d hour %d interval %d depth %d", e->_id, e->_url, e->_request_headers,
          e->_num_request_headers, e->_offset_hour, e->_interval, e->_max_depth);
    ul->Add(e);
    e = NULL;
  }

  // All file entries are valid.

  close(f);
  return ul;

abort_processing:
  close(f);
  if (e) {
    delete e;
  }
  if (ul) {
    delete ul;
  }
  return (UpdateConfigList *)NULL;
}

/////////////////////////////////////////////////////////////////////////////
// Class UpdateScheduler
//      Handle scheduling of UpdateEntry objects
/////////////////////////////////////////////////////////////////////////////
UpdateScheduler::UpdateScheduler(UpdateConfigManager *c)
  : Continuation(new_ProxyMutex()), _periodic_event(0), _recursive_update(0), _CM(c), _schedule_event_callbacks(0),
    _update_state_machines(0), _base_EN(0), _parent_US(0)
{
  SET_HANDLER((UpdateSchedulerContHandler)&UpdateScheduler::ScheduleEvent);
}

UpdateScheduler::~UpdateScheduler()
{
}

int
UpdateScheduler::Init()
{
  _recursive_update = 0;
  _periodic_event = eventProcessor.schedule_every(this, HRTIME_SECONDS(10));
  return 0;
}

int
UpdateScheduler::Init(UpdateScheduler *us, UpdateEntry *ue, Ptr<UpdateConfigParams> p)
{
  ink_assert(ue->_indirect_list->Entries());

  _recursive_update = 1;
  _CP = p;
  _CL = ue->_indirect_list;
  _base_EN = ue;
  _parent_US = us;

  // Schedule entries for update by moving entries to pending queue.

  UpdateEntry *e;
  while ((e = _CL->Remove())) {
    _CL->AddPending(e);
  }
  _periodic_event = eventProcessor.schedule_every(this, HRTIME_SECONDS(10));
  return 0;
}

int
UpdateScheduler::ScheduleEvent(int event, void *e)
{
  UpdateEntry *ue = NULL;
  int update_complete = 1;

  if (event == EVENT_IMMEDIATE) {
    //////////////////////////////////////////////////////////////////////
    // Callback on update completion from Update State Machine
    //////////////////////////////////////////////////////////////////////
    ue = (UpdateEntry *)e;

    switch (ue->_update_event_status) {
    case UPDATE_EVENT_SUCCESS: {
      Debug("update", "%s update complete, UPDATE_EVENT_SUCCESS id: %d", (_recursive_update ? "(R)" : ""), ue->_id);
      UPDATE_INCREMENT_DYN_STAT(update_successes_stat);

      if ((ue->_max_depth > 0) && ue->_indirect_list) {
        if (ue->_indirect_list->Entries()) {
          //////////////////////////////////////////////////////////
          // Recursive update case.
          // At this point, we have a list of URLs which was
          // recursively derived from the base URL.
          // Instantiate UpdateScheduler to process this URL list.
          //////////////////////////////////////////////////////////
          Debug("update", "Starting UpdateScheduler for id: %d [%s]", ue->_id, ue->_url);
          UpdateScheduler *us = new UpdateScheduler();
          us->Init(this, ue, _CP);
          update_complete = 0;

        } else {
          ue->_indirect_list = NULL;
        }
      }
      break;
    }
    case UPDATE_EVENT_SUCCESS_NOACTION: {
      Debug("update", "%s update complete, UPDATE_EVENT_SUCCESS_NOACTION id: %d", (_recursive_update ? "(R)" : ""), ue->_id);
      UPDATE_INCREMENT_DYN_STAT(update_no_actions_stat);
      break;
    }
    case UPDATE_EVENT_FAILED: {
      Debug("update", "%s update complete, UPDATE_EVENT_FAILED id: %d", (_recursive_update ? "(R)" : ""), ue->_id);
      UPDATE_INCREMENT_DYN_STAT(update_fails_stat);
      break;
    }
    default: {
      Debug("update", "%s update complete, unknown status %d, id: %d", (_recursive_update ? "(R)" : ""), ue->_update_event_status,
            ue->_id);
      UPDATE_INCREMENT_DYN_STAT(update_unknown_status_stat);
      break;
    }
    } // End of switch

    if (update_complete) {
      if (!_recursive_update) {
        /////////////////////////////////////////////////////////
        // Recompute expire time and place entry back on list
        /////////////////////////////////////////////////////////

        ue->ComputeScheduleTime();
        _CL->Add(ue); // Place back on list

      } else {
        delete ue;
      }
      --_update_state_machines;
      UPDATE_DECREMENT_DYN_STAT(update_state_machines_stat);
    }
    ////////////////////////////////////////////////////////////////
    // Start another update SM if scheduling is allowed
    // and an entry exists on the pending list.
    ////////////////////////////////////////////////////////////////

    if (Schedule() < 0) {
      // Scheduling allowed, but nothing to schedule
      if (_update_state_machines == 0) {
        //////////////////////////////////////////////////////////////
        // No more active updates, deallocate config/entry structures
        //////////////////////////////////////////////////////////////

        _CP = NULL;
        _CL = NULL;

        if (_recursive_update) {
          //
          // Recursive list update is now complete.
          // Callback parent UpdateScheduler.
          //
          _periodic_event->cancel();
          _base_EN->_indirect_list = NULL;
          _base_EN->_update_event_status = UPDATE_EVENT_SUCCESS;

          SET_HANDLER((UpdateSchedulerContHandler)&UpdateScheduler::ChildExitEventHandler);
          handleEvent(EVENT_IMMEDIATE, 0);
        }
      }
    }
    return EVENT_DONE;
  }
  //////////////////////////////////////
  // Periodic event callback
  //////////////////////////////////////
  if (event == EVENT_INTERVAL) {
    ++_schedule_event_callbacks;
  } else {
    // Unknown event, ignore it.
    Debug("update", "UpdateScheduler::ScheduleEvent unknown event %d", event);
    return EVENT_DONE;
  }

  if (!_CP && !_CL) {
    // No updates pending, attempt to schedule any expired updates

    if (!_CM->GetConfigParams(&_CP)) {
      return EVENT_CONT; // Missed lock, try at next event
    }
    if (!_CM->GetConfigList(&_CL)) {
      _CP = NULL;
      return EVENT_CONT; // Missed lock, try at next event
    }
    // Cannot do anything unless we have valid params and list

    if (!_CP || !_CL) {
      _CP = NULL;
      _CL = NULL;
      return EVENT_CONT; // try at next event
    }
    // Determine if the subsystem is enabled

    if (!_CP->IsEnabled()) {
      _CP = NULL;
      _CL = NULL;
      return EVENT_CONT; // try at next event
    }

  } else {
    ///////////////////////////////////////////////////////////////////
    // Updates pending from last schedule event, attempt to restart
    // additional update SM(s).
    ///////////////////////////////////////////////////////////////////

    Schedule();
    return EVENT_CONT;
  }
  ink_release_assert(!_update_state_machines);

  ///////////////////////////////////////////////////////
  // Scan entry list and schedule expired updates
  ///////////////////////////////////////////////////////

  ink_hrtime ht = ink_get_based_hrtime();
  time_t cur_time = ht / HRTIME_SECOND;
  Queue<UpdateEntry> no_action_q;
  int time_expired;

  while ((ue = _CL->Remove())) {
    time_expired = ue->ScheduleNow(cur_time);
    if (time_expired || _CP->ImmediateUpdate()) {
      if (Schedule(ue) > 0) {
        Debug("update", "%s and started id: %d", time_expired ? "expired" : "force expire", ue->_id);
      } else {
        Debug("update", "%s with deferred start id: %d", time_expired ? "expired" : "force expire", ue->_id);
      }

    } else {
      no_action_q.enqueue(ue);
    }
  }

  // Place no_action_q elements back on list

  while ((ue = no_action_q.dequeue())) {
    _CL->Add(ue);
  }

  if (!_update_state_machines && !_CL->_pending_q.head) {
    // Nothing active or pending.
    // Drop references to config/param structures.

    _CP = NULL;
    _CL = NULL;
  }
  return EVENT_DONE;
}

int
UpdateScheduler::ChildExitEventHandler(int event, Event * /* e ATS_UNUSED */)
{
  switch (event) {
  case EVENT_IMMEDIATE:
  case EVENT_INTERVAL: {
    MUTEX_TRY_LOCK(lock, _parent_US->mutex, this_ethread());
    if (lock.is_locked()) {
      Debug("update", "Child UpdateScheduler exit id: %d", _base_EN->_id);
      _parent_US->handleEvent(EVENT_IMMEDIATE, _base_EN);
      delete this;

    } else {
      // Lock miss, try again later.
      eventProcessor.schedule_in(this, HRTIME_MSECONDS(10));
    }
    break;
  }
  default: {
    ink_release_assert(!"UpdateScheduler::ChildExitEventHandler invalid event");
  } // End of case
  } // End of switch

  return EVENT_DONE;
}

int
UpdateScheduler::Schedule(UpdateEntry *e)
{
  // Return > 0,  UpdateEntry scheduled
  // Return == 0, Scheduling not allowed
  // Return < 0,  Scheduling allowed, but nothing to schedule

  UpdateSM *usm;
  UpdateEntry *ue = e;
  int allow_schedule;
  RecInt count, sum;
  int max_concurrent_updates;

  UPDATE_READ_DYN_STAT(update_state_machines_stat, count, sum);
  if (_CP->ConcurrentUpdates() < _CP->MaxUpdateSM()) {
    max_concurrent_updates = _CP->ConcurrentUpdates();
  } else {
    max_concurrent_updates = _CP->MaxUpdateSM();
  }
  allow_schedule = (sum < max_concurrent_updates);

  if (allow_schedule) {
    ue = ue ? ue : _CL->RemovePending();
    if (ue) {
      ++_update_state_machines;
      UPDATE_INCREMENT_DYN_STAT(update_state_machines_stat);
      usm = new UpdateSM(this, _CP, ue);
      usm->Start();

      Debug("update", "%s %s start update id: %d [%s]", (_recursive_update ? "(R)" : ""), (e ? "directed" : "speculative"), ue->_id,
            ue->_url);

      return 1; // UpdateEntry scheduled
    } else {
      return -1; // Scheduling allowed but nothing to schedule
    }

  } else {
    if (ue) {
      _CL->AddPending(ue);
    }
    return 0; // Scheduling not allowed
  }
}

/////////////////////////////////////////////////////////////////////////////
// Class UpdateSM
//      State machine which handles object update action
/////////////////////////////////////////////////////////////////////////////
UpdateSM::UpdateSM(UpdateScheduler *us, Ptr<UpdateConfigParams> p, UpdateEntry *e)
  : Continuation(new_ProxyMutex()), _state(USM_INIT), _return_status(0), _retries(0)
{
  SET_HANDLER((UpdateSMContHandler)&UpdateSM::HandleSMEvent);
  _US = us;
  _CP = p;
  _EN = e;
}

UpdateSM::~UpdateSM()
{
  _CP = NULL; // drop reference
}

void
UpdateSM::Start()
{
  eventProcessor.schedule_imm(this, ET_CACHE);
}

int
UpdateSM::HandleSMEvent(int event, Event * /* e ATS_UNUSED */)
{
  while (1) {
    switch (_state) {
    case USM_INIT: {
      ////////////////////////////////////////////////////////////////////
      // Cluster considerations.
      // For non-recursive URL(s), only process it if the cluster
      // hash returns this node.  Recursive URL(s) are processed by
      // all nodes in the cluster.
      ////////////////////////////////////////////////////////////////////
      if (_EN->_max_depth > 0) {
        // Recursive URL(s) are processed by all nodes.
        _state = USM_PROCESS_URL;
        break;
      }

      INK_MD5 url_md5;

      Cache::generate_key(&url_md5, &_EN->_URLhandle);
      ClusterMachine *m = cluster_machine_at_depth(cache_hash(url_md5));
      if (m) {
        // URL hashed to remote node, do nothing.
        _state = USM_EXIT;
        _EN->_update_event_status = UPDATE_EVENT_SUCCESS_NOACTION;
        break;
      } else {
        // URL hashed to local node, start processing.
        _state = USM_PROCESS_URL;
        break;
      }
    }
    case USM_PROCESS_URL: {
      ///////////////////////////////////
      // Dispatch to target handler
      ///////////////////////////////////
      int n;
      int scheme_len;
      const char *scheme;
      _state = USM_PROCESS_URL_COMPLETION;
      scheme = _EN->_URLhandle.scheme_get(&scheme_len);
      for (n = 0; n < N_SCHEMES; ++n) {
        if (scheme == *scheme_dispatch_table[n].scheme) {
          _EN->_scheme_index = n;
          if ((*scheme_dispatch_table[n].func)(this)) {
            break; // Error in initiation
          }
          return EVENT_CONT;
        }
      }
      // Error in initiation or bad scheme.

      _state = USM_EXIT;
      _EN->_update_event_status = UPDATE_EVENT_FAILED;
      break;
    }
    case USM_PROCESS_URL_COMPLETION: {
      ///////////////////////////////////
      // Await URL update completion
      ///////////////////////////////////
      _state = USM_EXIT;
      _EN->_update_event_status = event;
      (*scheme_post_dispatch_table[_EN->_scheme_index].func)(this);
      break;
    }
    case USM_EXIT: {
      /////////////////////////////////////////////
      // Operation complete
      /////////////////////////////////////////////
      if ((_return_status == UPDATE_EVENT_FAILED) && (_retries < _CP->RetryCount())) {
        // Retry operation

        ++_retries;
        _state = USM_PROCESS_URL;
        eventProcessor.schedule_in(this, HRTIME_SECONDS(_CP->RetryInterval()), ET_CACHE);
        return EVENT_DONE;

      } else {
        MUTEX_TRY_LOCK(lock, _US->mutex, this_ethread());
        if (lock.is_locked()) {
          _US->handleEvent(EVENT_IMMEDIATE, (void *)_EN);
          delete this;
          return EVENT_DONE;

        } else {
          // Missed lock, try again later
          eventProcessor.schedule_in(this, HRTIME_MSECONDS(10), ET_CACHE);
          return EVENT_CONT;
        }
      }
    }
    } // End of switch
  }   // End of while

  return EVENT_CONT;
}

struct dispatch_entry scheme_dispatch_table[UpdateSM::N_SCHEMES] = {
  {&URL_SCHEME_HTTP, UpdateSM::http_scheme},
};

struct dispatch_entry scheme_post_dispatch_table[UpdateSM::N_SCHEMES] = {
  {&URL_SCHEME_HTTP, UpdateSM::http_scheme_postproc},
};

int
UpdateSM::http_scheme(UpdateSM *sm)
{
  if (sm->_EN->_max_depth > 0) {
    ////////////////////////////////////
    // Recursive Update
    ////////////////////////////////////
    Debug("update", "Start recursive HTTP GET id: %d [%s]", sm->_EN->_id, sm->_EN->_url);
    sm->_EN->_indirect_list = new UpdateConfigList;
    RecursiveHttpGet *RHttpGet = new RecursiveHttpGet;

    RHttpGet->Init(sm, sm->_EN->_url, sm->_EN->_request_headers, &sm->_EN->_URLhandle, sm->_EN->_http_hdr, sm->_EN->_max_depth,
                   sm->_EN->_indirect_list, &update_allowable_html_tags[0]);
  } else {
    ////////////////////////////////////
    // One URL update
    ////////////////////////////////////
    Debug("update", "Start HTTP GET id: %d [%s]", sm->_EN->_id, sm->_EN->_url);
    HttpUpdateSM *current_reader;

    current_reader = HttpUpdateSM::allocate();
    current_reader->init();
    // TODO: Do anything with the returned Action* ?
    current_reader->start_scheduled_update(sm, sm->_EN->_http_hdr);
  }
  return 0;
}

int
UpdateSM::http_scheme_postproc(UpdateSM *sm)
{
  // Map HttpUpdateSM return event code to internal status code

  switch (sm->_EN->_update_event_status) {
  case UPDATE_EVENT_SUCCESS:
  case UPDATE_EVENT_FAILED:
    // Returned only by RecursiveHttpGet
    sm->_return_status = sm->_EN->_update_event_status;
    break;

  case HTTP_SCH_UPDATE_EVENT_WRITTEN:
  case HTTP_SCH_UPDATE_EVENT_UPDATED:
  case HTTP_SCH_UPDATE_EVENT_DELETED:
  case HTTP_SCH_UPDATE_EVENT_NOT_CACHED:
  case HTTP_SCH_UPDATE_EVENT_NO_ACTION:
    sm->_EN->_update_event_status = UPDATE_EVENT_SUCCESS;
    sm->_return_status = UPDATE_EVENT_SUCCESS;
    break;

  case HTTP_SCH_UPDATE_EVENT_ERROR:
  default:
    sm->_EN->_update_event_status = UPDATE_EVENT_FAILED;
    sm->_return_status = UPDATE_EVENT_FAILED;
    break;
  }
  return 0;
}

/////////////////////////////////////////////////////////////////////////////
// Class RecursiveHttpGet
//      Generate URL list by recursively traversing non-terminal URL(s)
//      up to the specified depth.
/////////////////////////////////////////////////////////////////////////////
char HtmlParser::default_zero_char = '\0';

RecursiveHttpGet::RecursiveHttpGet()
  : Continuation(new_ProxyMutex()), _id(0), _caller_cont(0), _request_headers(0), _http_hdr(0), _recursion_depth(0), _OL(0),
    _group_link_head(0), _active_child_state_machines(0)
{
  SET_HANDLER((RecursiveHttpGetContHandler)&RecursiveHttpGet::RecursiveHttpGetEvent);
}

RecursiveHttpGet::~RecursiveHttpGet()
{
  _CL = NULL;
}

void
RecursiveHttpGet::Init(Continuation *cont, char *url, char *request_headers, URL *url_data, HTTPHdr *http_hdr, int recursion_depth,
                       Ptr<UpdateConfigList> L, struct html_tag *allowed_html_tags)
{
  /////////////////////////////////////////////////////////////////////////
  // Note: URL and request header data pointers are assumed to be
  //       valid during the life of this class.
  /////////////////////////////////////////////////////////////////////////
  _id = ink_atomic_increment(&global_id, 1);
  _caller_cont = cont;
  _request_headers = request_headers;
  _url_data = url_data;
  _http_hdr = http_hdr;
  _recursion_depth = recursion_depth;
  _CL = L;
  _OL = ObjectReloadContAllocator.alloc();
  _OL->Init(this, url, strlen(url), _request_headers, (_request_headers ? strlen(_request_headers) : 0), 1, 1);

  html_parser.Init(url, allowed_html_tags);

  Debug("update", "Start recursive read rid: %d [%s]", _id, html_parser._url);
}

int
RecursiveHttpGet::RecursiveHttpGetEvent(int event, Event *d)
{
  char *url, *url_end;
  int status;
  UpdateEntry *ue;
  IOBufferReader *r = (IOBufferReader *)d;

  switch (event) {
  case NET_EVENT_OPEN_FAILED: {
    Debug("update", "RecursiveHttpGetEvent connect failed id: %d [%s]", _id, html_parser._url);
    break;
  }
  case VC_EVENT_ERROR: {
    Debug("update", "RecursiveHttpGetEvent connect event error id: %d [%s]", _id, html_parser._url);
    break;
  }
  case VC_EVENT_READ_READY:
  case VC_EVENT_READ_COMPLETE:
  case VC_EVENT_EOS: {
    while ((status = html_parser.ParseHtml(r, &url, &url_end))) {
      // Validate given URL.

      ue = new UpdateEntry;
      if (ue->ValidURL(url, url_end + 1 /* Point to null */)) {
        delete ue;
        ue = NULL;

      } else {
        // Complete remaining UpdateEntry initializations

        ue->_request_headers = ats_strdup(_request_headers);
        ue->BuildHttpRequest();
        ue->Init(1); // Derived URL

        // Discard remote URL(s)
        int ue_host_len;
        const char *ue_host = ue->_URLhandle.host_get(&ue_host_len);
        int url_host_len;
        const char *url_host = _url_data->host_get(&url_host_len);

        if (ue_host == NULL || url_host == NULL || ptr_len_casecmp(ue_host, ue_host_len, url_host, url_host_len)) {
          delete ue;
          ue = NULL;
          continue;
        }
        // I think we're generating the cache key just to get a hash of the URL.
        // Used to use Cache::generate_key that no longer works with vary_on_user_agent
        ue->_URLhandle.hash_get(&ue->_url_md5);

        if (_CL->HashAdd(ue)) {
          // Entry already exists

          delete ue;
          ue = NULL;

        } else {
          // Entry is unique and has been added to hash table.
          // Set terminal URL status and add to current
          // recursion level list.

          ue->SetTerminalStatus(((status < 0) ? 1 : 0));
          Debug("update", "Recursive find rid: %d id: %d %s\n [%s]", _id, ue->_id, (ue->TerminalURL() ? "T " : ""), ue->_url);

          if (_group_link_head) {
            ue->_group_link = _group_link_head;
            _group_link_head = ue;
          } else {
            _group_link_head = ue;
            ue->_group_link = NULL;
          }
        }
      }
    }
    ink_release_assert(r->read_avail() == 0);

    if ((event == VC_EVENT_READ_COMPLETE) || (event == VC_EVENT_EOS)) {
      break;

    } else {
      return EVENT_CONT;
    }
  }
  case UPDATE_EVENT_SUCCESS:
  case UPDATE_EVENT_FAILED: {
    // Child state machine completed.

    ink_release_assert(_active_child_state_machines > 0);
    _active_child_state_machines--;
    break;
  }
  default: {
    ink_release_assert(!"RecursiveHttpGetEvent invalid event");
    return EVENT_DONE;

  } // End of case
  } // End of switch

  if (_group_link_head) {
    // At this point, we have a list of valid terminal
    // and non-terminal URL(s).
    // Sequentially initiate the read on the non-terminal URL(s).

    while (_group_link_head) {
      ue = _group_link_head;
      _group_link_head = ue->_group_link;

      if (!ue->TerminalURL()) {
        if (_recursion_depth <= 1) {
          continue;
        }

        Debug("update", "(R) start non-terminal HTTP GET rid: %d id: %d [%s]", _id, ue->_id, ue->_url);

        _active_child_state_machines++;
        RecursiveHttpGet *RHttpGet = new RecursiveHttpGet();
        RHttpGet->Init(this, ue->_url, _request_headers, _url_data, _http_hdr, (_recursion_depth - 1), _CL,
                       &update_allowable_html_tags[0]);
        return EVENT_CONT;
      }
    }
  }
  // All child state machines have completed, tell our parent
  // and delete ourself.

  SET_HANDLER((RecursiveHttpGetContHandler)&RecursiveHttpGet::ExitEventHandler);
  handleEvent(EVENT_IMMEDIATE, 0);
  return EVENT_DONE;
}

int
RecursiveHttpGet::ExitEventHandler(int event, Event * /* e ATS_UNUSED */)
{
  switch (event) {
  case EVENT_IMMEDIATE:
  case EVENT_INTERVAL: {
    MUTEX_TRY_LOCK(lock, _caller_cont->mutex, this_ethread());
    if (lock.is_locked()) {
      Debug("update", "Exiting recursive read rid: %d [%s]", _id, html_parser._url);
      _caller_cont->handleEvent(UPDATE_EVENT_SUCCESS, 0);
      delete this;

    } else {
      // Lock miss, try again later.
      eventProcessor.schedule_in(this, HRTIME_MSECONDS(10));
    }
    break;
  }
  default: {
    ink_release_assert(!"RecursiveHttpGet::ExitEventHandler invalid event");
  } // End of case
  } // End of switch

  return EVENT_DONE;
}

int
HtmlParser::ParseHtml(IOBufferReader *r, char **url, char **url_end)
{
  int status;
  while (1) {
    if ((status = ScanHtmlForURL(r, url, url_end))) {
      status = ConstructURL(url, url_end);
      if (status)
        return status;
    } else {
      return 0; // No more bytes
    }
  }
}

int
HtmlParser::ScanHtmlForURL(IOBufferReader *r, char **url, char **url_end)
{
  unsigned char c;
  int n = 0;

  while (1) {
    switch (_scan_state) {
    case SCAN_INIT: {
      _tag.clear();

      _attr.clear();
      _attr_value.clear();
      _attr_value_hash_char_index = -1;
      _attr_value_quoted = 0;
      _attr_matched = false;

      _scan_state = SCAN_START;
      n = -1;
      break;
    }
    case SCAN_START: {
      while ((n = r->read((char *)&c, 1))) {
        if (c == '<') {
          _scan_state = FIND_TAG_START;
          break;
        }
      }
      break;
    }
    case FIND_TAG_START: {
      while ((n = r->read((char *)&c, 1))) {
        if (!isspace(c)) {
          if (c == '>') {
            ////////////////////////////////////////////////////
            // '< >' with >= 0 embedded spaces, ignore it.
            ////////////////////////////////////////////////////
            _scan_state = SCAN_INIT;
            break;

          } else {
            _tag(_tag.length()) = c;
            _scan_state = COPY_TAG;
            break;
          }
        }
      }
      break;
    }
    case COPY_TAG: {
      while ((n = r->read((char *)&c, 1))) {
        if (!isspace(c)) {
          if (c == '>') {
            /////////////////////////////
            // <tag>, ignore it
            /////////////////////////////
            _scan_state = SCAN_INIT;
            break;

          } else if (c == '=') {
            ///////////////////////////////
            // <tag=something>, ignore it
            ///////////////////////////////
            _scan_state = SCAN_INIT;
            break;

          } else {
            if (_tag.length() < MAX_TAG_NAME_LENGTH) {
              _tag(_tag.length()) = c;

            } else {
              ///////////////////////////////////
              // Tag name to long, ignore it
              ///////////////////////////////////
              _scan_state = SCAN_INIT;
              break;
            }
          }

        } else {
          _tag(_tag.length()) = 0;
          if (strcmp(_tag, HTML_COMMENT_TAG) == 0) {
            _scan_state = IGNORE_COMMENT_START;
          } else {
            _scan_state = FIND_ATTR_START;
          }
          break;
        }
      }
      break;
    }
    case IGNORE_COMMENT_START: {
      _comment_end_ptr = (char *)HTML_COMMENT_END;
      _scan_state = IGNORE_COMMENT;
      break;
    }
    case IGNORE_COMMENT: {
      while ((n = r->read((char *)&c, 1))) {
        if (!isspace(c)) {
          if (c == *_comment_end_ptr) {
            _comment_end_ptr++;
            if (!*_comment_end_ptr) {
              _scan_state = SCAN_INIT;
              break;
            }
          } else {
            _comment_end_ptr = (char *)HTML_COMMENT_END;
          }
        }
      }
      break;
    }
    case FIND_ATTR_START: {
      while ((n = r->read((char *)&c, 1))) {
        if (!isspace(c)) {
          if (c == '>') {
            ////////////////////////////////////////////////
            // <tag > with >=1 embedded spaces, ignore it
            ////////////////////////////////////////////////
            _scan_state = SCAN_INIT;
            break;

          } else if (c == '=') {
            //////////////////////////////////////////////////////////
            // <tag =something> with >=1 embedded spaces, ignore it
            //////////////////////////////////////////////////////////
            _scan_state = SCAN_INIT;
            break;

          } else {
            _attr(_attr.length()) = c;
            _scan_state = COPY_ATTR;
            break;
          }
        }
      }
      break;
    }
    case COPY_ATTR: {
      while ((n = r->read((char *)&c, 1))) {
        if (!isspace(c)) {
          if (c == '>') {
            /////////////////////////////
            // <tag attr>, ignore it
            /////////////////////////////
            _scan_state = SCAN_INIT;
            break;

          } else if (c == '=') {
            ///////////////////////////////
            // <tag attr=something>
            ///////////////////////////////
            _attr(_attr.length()) = 0;
            _scan_state = FIND_ATTR_VALUE_START;
            break;

          } else {
            if (_attr.length() < MAX_ATTR_NAME_LENGTH) {
              _attr(_attr.length()) = c;

            } else {
              ///////////////////////////////////
              // Attr name to long, ignore it
              ///////////////////////////////////
              _scan_state = SCAN_INIT;
              break;
            }
          }

        } else {
          _attr(_attr.length()) = 0;
          _scan_state = FIND_ATTR_VALUE_DELIMITER;
          break;
        }
      }
      break;
    }
    case FIND_ATTR_VALUE_DELIMITER: {
      while ((n = r->read((char *)&c, 1))) {
        if (isspace(c) || (c == '=')) {
          if (c == '=') {
            _scan_state = FIND_ATTR_VALUE_START;
            break;
          }
        } else {
          _scan_state = SCAN_INIT;
          break;
        }
      }
      break;
    }
    case FIND_ATTR_VALUE_START: {
      while ((n = r->read((char *)&c, 1))) {
        if (!isspace(c)) {
          if (c == '>') {
            /////////////////////////////
            // <tag attr= >, ignore
            /////////////////////////////
            _scan_state = SCAN_INIT;
            break;

          } else if ((c == '\'') || (c == '\"')) {
            _attr_value_quoted = c;
            _scan_state = COPY_ATTR_VALUE;
            break;

          } else {
            _attr_value_quoted = 0;
            _attr_value(_attr_value.length()) = c;
            _scan_state = COPY_ATTR_VALUE;
            break;
          }
        }
      }
      break;
    }
    case COPY_ATTR_VALUE: {
      while ((n = r->read((char *)&c, 1))) {
        if (_attr_value_quoted) {
          if (c == _attr_value_quoted) {
            ///////////////////////////////////////////
            // We have a complete <tag attr='value'
            ///////////////////////////////////////////
            _attr_value(_attr_value.length()) = 0;
            _scan_state = VALIDATE_ENTRY;
            break;

          } else if (c == '\n') {
            _scan_state = TERMINATE_COPY_ATTR_VALUE;
            break;
          } else {
            _attr_value(_attr_value.length()) = c;
            if (c == '#') {
              _attr_value_hash_char_index = _attr_value.length() - 1;
            }
          }

        } else {
          if (isspace(c)) {
            ///////////////////////////////////////////
            // We have a complete <tag attr=value
            ///////////////////////////////////////////
            _attr_value(_attr_value.length()) = 0;
            _scan_state = VALIDATE_ENTRY;
            break;

          } else if (c == '>') {
            /////////////////////////////////////////
            // We have a complete <tag attr=value>
            /////////////////////////////////////////
            _attr_value(_attr_value.length()) = 0;
            _scan_state = VALIDATE_ENTRY_RESTART;
            break;

          } else {
            _attr_value(_attr_value.length()) = c;
            if (c == '#') {
              _attr_value_hash_char_index = _attr_value.length() - 1;
            }
          }
        }
      }
      break;
    }
    case VALIDATE_ENTRY:
    case VALIDATE_ENTRY_RESTART: {
      if (_scan_state == VALIDATE_ENTRY) {
        _scan_state = RESUME_ATTR_VALUE_SCAN;
      } else {
        _scan_state = SCAN_INIT;
      }
      if (AllowTagAttrValue()) {
        if (ExtractURL(url, url_end)) {
          return 1; // valid URL
        }
      }
      break; // resume scan
    }
    case RESUME_ATTR_VALUE_SCAN: {
      _attr.clear();
      _attr_value.clear();
      _attr_value_hash_char_index = -1;
      _attr_value_quoted = 0;

      _scan_state = FIND_ATTR_START;
      n = -2;
      break;
    }
    case TERMINATE_COPY_ATTR_VALUE: {
      while ((n = r->read((char *)&c, 1))) {
        if (c == _attr_value_quoted) {
          _scan_state = RESUME_ATTR_VALUE_SCAN;
          break;
        }
      }
      break;
    }
    default: {
      ink_release_assert(!"HtmlParser::ScanHtmlForURL bad state");
    }
    } // end of switch

    if (n == 0) {
      return 0; // No more data
    }

  } // end of while
}

int
HtmlParser::AllowTagAttrValue()
{
  struct html_tag *p_tag = allowable_html_tags;
  struct html_tag *p_attr = allowable_html_attrs;

  if (!_tag || !_attr)
    return 0;

  while (p_tag->tag && p_tag->attr) {
    if (!strcasecmp(_tag, p_tag->tag) && !strcasecmp(_attr, p_tag->attr)) {
      if (p_attr == NULL || p_attr->tag == NULL)
        return 1;
      else if (_attr_matched) {
        return 1;
      } else {
        // attributes don't match
        return 0;
      }
    } else {
      if (p_attr && p_attr->tag && p_attr->attr && _attr_value.length() > 0) {
        if (!strcasecmp(_attr, p_attr->tag) && !strcasecmp(_attr_value, p_attr->attr)) {
          _attr_matched = true;
        }
      }
      p_tag++;
      if (p_attr)
        p_attr++;
    }
  }
  return 0;
}

int
HtmlParser::ValidProtoScheme(char *p)
{
  int n;
  for (n = 0; proto_schemes[n].tag; ++n) {
    if (!strncasecmp(p, proto_schemes[n].tag, proto_schemes[n].tag_len)) {
      return 1;
    }
  }
  return 0;
}

int
HtmlParser::ValidSupportedProtoScheme(char *p)
{
  int n;
  for (n = 0; supported_proto_schemes[n].tag; ++n) {
    if (!strncasecmp(p, supported_proto_schemes[n].tag, supported_proto_schemes[n].tag_len)) {
      return 1;
    }
  }
  return 0;
}

int
HtmlParser::ExtractURL(char **url, char **url_end)
{
  intptr_t n;

  // '#' considerations
  if (_attr_value_hash_char_index >= 0) {
    if (!_attr_value_hash_char_index) {
      return 0; // No URL

    } else {
      // '#' terminates _attr_value
      _attr_value.set_length(_attr_value_hash_char_index + 1);
      _attr_value[_attr_value_hash_char_index] = 0;
    }
  }

  if (!strcasecmp(_tag, "base") && !strcasecmp(_attr, "href")) {
    if (_html_doc_base) {
      _html_doc_base.clear();
    }
    for (n = 0; n < _attr_value.length(); ++n) {
      _html_doc_base(_html_doc_base.length()) = _attr_value[n];
    }
    _html_doc_base(_html_doc_base.length()) = 0;
    return 0; // No URL

  } else if (!strcasecmp(_tag, "meta") && !strcasecmp(_attr, "content")) {
    /////////////////////////////////////////////////////////////////
    // General form:
    //      <META HTTP-EQUIV=Refresh CONTENT="0; URL=index.html">
    /////////////////////////////////////////////////////////////////
    if (_attr_value.length()) {
      // Locate start of URL
      for (n = 0; n < _attr_value.length(); ++n) {
        if (!ParseRules::is_digit((unsigned char)_attr_value[n])) {
          break;
        }
      }
      if ((n < _attr_value.length()) && (((unsigned char)_attr_value[n]) == ';')) {
        for (; n < _attr_value.length(); ++n) {
          if (!isspace((unsigned char)_attr_value[n])) {
            break;
          }
        }
        if ((n < _attr_value.length()) && (!strncasecmp(&_attr_value[n], "URL=", 4))) {
          n += 4;
          if ((n < _attr_value.length()) && ((_attr_value.length() - n) > 1)) {
            *url = &_attr_value[n];
            *url_end = &_attr_value[_attr_value.length() - 2];
            return 1;
          }
        }
      }
      return 0; // No URL

    } else {
      return 0; // No URL
    }
  }

  if (_attr_value.length() > 1) {
    *url = &_attr_value[(intptr_t)0];
    *url_end = &_attr_value[_attr_value.length() - 2];
    return 1;

  } else {
    return 0; // No URL
  }
}

int
HtmlParser::ConstructURL(char **url, char **url_end)
{
  unsigned char *p_url = (unsigned char *)*url;
  unsigned char *p_url_end = (unsigned char *)*url_end;

  /////////////////////////////////////////////////////////////////////
  // Handle the <a href="[spaces]URI"> case by skipping over spaces
  /////////////////////////////////////////////////////////////////////
  while (p_url < p_url_end) {
    if (isspace(*p_url)) {
      ++p_url;
    } else {
      break;
    }
  }

  ////////////////////////////////////////////////////
  // Determine if we have a relative or absolute URI
  ////////////////////////////////////////////////////
  int relative_URL = 0;
  int http_needed = 0;
  if (ValidProtoScheme((char *)p_url)) {
    if (!strncasecmp((char *)p_url, "http:", 5) && (strncasecmp((char *)p_url, "http://", 7) != 0)) {
      //////////////////////////////////////////////////////////
      // Bad relative URI references of the form http:URL.
      // Skip over the "http:" part.
      //////////////////////////////////////////////////////////
      p_url += strlen("http:");
      if (p_url > p_url_end) {
        return 0; // Invalid URL
      }
      relative_URL = 1;
    }
  } else {
    relative_URL = 1;
    // problem found with www.slashdot.com
    if (strncasecmp((char *)p_url, "//", 2) == 0)
      http_needed = 1;
  }

  //////////////////////////////////////////////
  // Only handle supported protocol schemes
  //////////////////////////////////////////////
  if (!relative_URL && !ValidSupportedProtoScheme((char *)p_url)) {
    return 0; // Invalid URL
  }

  if (relative_URL) {
    ////////////////////////////////////
    // Compute document base path
    ////////////////////////////////////
    DynArray<char> *base = 0;
    DynArray<char> *absolute_url = 0;

    if (http_needed) {
      absolute_url = PrependString("http:", 5, (char *)p_url, (p_url_end - p_url + 2));
    } else if (_html_doc_base.length()) {
      ///////////////////////////////////////////////////////////////
      // Document base specified via <base href="...">
      ///////////////////////////////////////////////////////////////
      base = MakeURL(_url, _html_doc_base, _html_doc_base.length(), !ValidProtoScheme(_html_doc_base));
      absolute_url = MakeURL(*base, (char *)p_url, (p_url_end - p_url + 2), 1);
    } else {
      absolute_url = MakeURL(_url, (char *)p_url, (p_url_end - p_url + 2), 1);
    }
    _result.clear();
    _result = *absolute_url;
    absolute_url->detach();

    // fix INKqa07208; need to reclaim memory
    delete absolute_url;
    if (base)
      delete base;

    *url = &_result[(intptr_t)0];
    *url_end = &_result[_result.length() - 3]; // -1 (real len)
    // -1 (skip null)
    // -1 (zero base)
  } else {
    *url = (char *)p_url;
    *url_end = (char *)p_url_end;
  }

  //////////////////////////////////////////////////////////////////
  // Determine if we have a terminal or non-terminal URL.
  // URL ending with '/', .htm or .html is considered non-terminal.
  //    Return < 0 ==> Terminal URL
  //    Return > 0 ==> Non terminal URL
  //////////////////////////////////////////////////////////////////
  if (!strncasecmp((char *)(p_url_end - 4), ".html", 5) || !strncasecmp((char *)(p_url_end - 3), ".htm", 4) ||
      !strncasecmp((char *)(p_url_end), "/", 1)) {
    return 1; // Non-terminal URL
  } else {
    return -1; // Terminal URL
  }
}

DynArray<char> *
HtmlParser::MakeURL(char *url, char *sub, int subsize, int relative_url)
{
  int i, n;
  int skip_slashslash;

  DynArray<char> *result = new DynArray<char>(&default_zero_char, 128);

  if (relative_url) {
    if (*sub != '/') {
      int url_len = strlen(url);

      // Locate last '/' in url
      for (i = url_len; i && url[i] != '/'; i--)
        ;

      if (i && (url[i] == url[i - 1])) {
        // http://hostname case with no terminating '/'

        for (n = 0; n < url_len; ++n) {
          (*result)(result->length()) = url[n];
        }
        (*result)(result->length()) = '/';

      } else {
        for (n = 0; n < (i + 1); ++n) {
          (*result)(result->length()) = url[n];
        }
      }

      for (n = 0; n < subsize; ++n) {
        (*result)(result->length()) = sub[n];
      }
      (*result)(result->length()) = '\0';

    } else {
      i = 0;
      do {
        // Locate leading '/'
        for (; url[i] && url[i] != '/'; i++)
          ;

        if (!url[i]) {
          break;
        }
        // Skip over '<scheme>://'
        skip_slashslash = ((url[i] == url[i + 1]) && (url[i + 1] == '/'));

        if (skip_slashslash) {
          i += 2;
        }
      } while (skip_slashslash);

      for (n = 0; n < (i - 1); ++n) {
        (*result)(result->length()) = url[n];
      }

      if (url[n] != '/') {
        (*result)(result->length()) = url[n];
      }

      for (n = 0; n < subsize; ++n) {
        (*result)(result->length()) = sub[n];
      }
      (*result)(result->length()) = '\0';
    }

  } else {
    for (n = 0; n < subsize; ++n) {
      (*result)(result->length()) = sub[n];
    }
    (*result)(result->length()) = '\0';
  }
  return result;
}

DynArray<char> *
HtmlParser::PrependString(const char *pre, int presize, char *sub, int subsize)
{
  int n;

  DynArray<char> *result = new DynArray<char>(&default_zero_char, 128);

  for (n = 0; n < presize; ++n) {
    (*result)(result->length()) = pre[n];
  }
  for (n = 0; n < subsize; ++n) {
    (*result)(result->length()) = sub[n];
  }
  (*result)(result->length()) = '\0';

  return result;
}

///////////////////////////////////////////////////////////////////
// Class ObjectReloadCont
//      Background load URL into local cache
///////////////////////////////////////////////////////////////////
ClassAllocator<ObjectReloadCont> ObjectReloadContAllocator("ObjectReloadCont");

ObjectReloadCont::ObjectReloadCont()
  : Continuation(0), _caller_cont(0), _request_id(0), _send_data(0), _receive_data(0), _start_event(0), _state(START),
    _cur_action(0), _netvc(0), _write_vio(0), _read_vio(0), _read_event_callback(0)
{
  SET_HANDLER((ObjectReloadContHandler)&ObjectReloadCont::ObjectReloadEvent);
}

ObjectReloadCont::~ObjectReloadCont()
{
}

void
ObjectReloadCont::Init(Continuation *cont, char *url, int url_len, char *headers, int headers_len, int http_case,
                       int read_event_callback)
{
  int total_len;

  mutex = new_ProxyMutex();
  _caller_cont = cont;
  _request_id = ink_atomic_increment(&global_id, 1);
  _read_event_callback = read_event_callback;

  // Setup send data buffer by prepending the HTTP GET method to the
  // given NULL terminated URL and terminating with HTTP version

  if (http_case) {
    if (headers_len) {
      total_len = len_GET_METHOD + url_len + len_HTTP_VERSION + len_TERMINATOR + headers_len + len_REQUEST_TERMINATOR;
    } else {
      total_len = len_GET_METHOD + url_len + len_HTTP_VERSION + len_REQUEST_TERMINATOR;
    }
    _send_data = new_MIOBuffer(buffer_size_to_index(total_len + 1)); // allow for NULL

    memcpy(_send_data->end(), GET_METHOD, len_GET_METHOD);
    memcpy(&(_send_data->end())[len_GET_METHOD], url, url_len);
    memcpy(&(_send_data->end())[len_GET_METHOD + url_len], HTTP_VERSION, len_HTTP_VERSION);

    if (headers_len) {
      memcpy(&(_send_data->end())[len_GET_METHOD + url_len + len_HTTP_VERSION], TERMINATOR, len_TERMINATOR);
      memcpy(&(_send_data->end())[len_GET_METHOD + url_len + len_HTTP_VERSION + len_TERMINATOR], headers, headers_len);
      memcpy(&(_send_data->end())[len_GET_METHOD + url_len + len_HTTP_VERSION + len_TERMINATOR + headers_len], REQUEST_TERMINATOR,
             len_REQUEST_TERMINATOR);

      // Add NULL for Debug URL output
      (_send_data->end())[len_GET_METHOD + url_len + len_HTTP_VERSION + len_TERMINATOR + headers_len + len_REQUEST_TERMINATOR] = 0;
    } else {
      memcpy(&(_send_data->end())[len_GET_METHOD + url_len + len_HTTP_VERSION], REQUEST_TERMINATOR, len_REQUEST_TERMINATOR);

      // Add NULL for Debug URL output
      (_send_data->end())[len_GET_METHOD + url_len + len_HTTP_VERSION + len_REQUEST_TERMINATOR] = 0;
    }
    _send_data->fill(total_len);

  } else {
    // Unhandled case... TODO: Do we need to actually handle this?
    ink_assert(false);
  }
  handleEvent(EVENT_IMMEDIATE, (void *)NULL);
}

void
ObjectReloadCont::free()
{
  mutex = 0;
  if (_send_data) {
    free_MIOBuffer(_send_data);
    _send_data = 0;
  }
  if (_receive_data) {
    free_MIOBuffer(_receive_data);
    _receive_data = 0;
  }
}

int
ObjectReloadCont::ObjectReloadEvent(int event, void *d)
{
  switch (_state) {
  case START: {
    IpEndpoint target;
    // Schedule connect to localhost:<proxy port>
    Debug("update-reload", "Connect start id=%d", _request_id);
    _state = ObjectReloadCont::ATTEMPT_CONNECT;
    MUTEX_TRY_LOCK(lock, this->mutex, this_ethread());
    ink_release_assert(lock.is_locked());
    target.setToLoopback(AF_INET);
    target.port() = htons(HttpProxyPort::findHttp(AF_INET)->m_port);
    _cur_action = netProcessor.connect_re(this, &target.sa);
    return EVENT_DONE;
  }
  case ATTEMPT_CONNECT: {
    if (event != NET_EVENT_OPEN) {
      // Connect error, terminate processing
      Debug("update-reload", "Connect fail id=%d", _request_id);
      CallBackUser(event, 0);
      free();
      ObjectReloadContAllocator.free(this);
      return EVENT_DONE;
    }
    _netvc = (class NetVConnection *)d;

    // Start URL write
    Debug("update-reload", "Write start id=%d [%s]", _request_id, _send_data->start());
    _state = ObjectReloadCont::WRITING_URL;
    IOBufferReader *r = _send_data->alloc_reader();
    _write_vio = _netvc->do_io_write(this, r->read_avail(), r);
    return EVENT_DONE;
  }
  case WRITING_URL: {
    ink_release_assert(_write_vio == (VIO *)d);
    if (event == VC_EVENT_WRITE_READY) {
      _write_vio->reenable();
      return EVENT_DONE;
    } else if (event == VC_EVENT_WRITE_COMPLETE) {
      // Write successful, start read
      Debug("update-reload", "Read start id=%d", _request_id);
      _state = ObjectReloadCont::READING_DATA;
      _receive_data = new_MIOBuffer(max_iobuffer_size);
      _receive_data_reader = _receive_data->alloc_reader();
      _read_vio = _netvc->do_io_read(this, INT64_MAX, _receive_data);
      return EVENT_DONE;
    } else {
      // Write error, terminate processing
      Debug("update-reload", "Write fail id=%d", _request_id);
      _netvc->do_io(VIO::CLOSE);
      CallBackUser(event, 0);
      free();
      ObjectReloadContAllocator.free(this);
      return EVENT_DONE;
    }
  }
  case READING_DATA: {
    ink_release_assert(_read_vio == (VIO *)d);
    switch (event) {
    case VC_EVENT_READ_READY: {
      if (_read_event_callback) {
        _caller_cont->handleEvent(event, _receive_data_reader);

      } else {
        int64_t read_bytes = _receive_data_reader->read_avail();
        _receive_data_reader->consume(read_bytes);
        _read_vio->reenable();
      }
      return EVENT_CONT;
    }
    case VC_EVENT_READ_COMPLETE:
    case VC_EVENT_EOS: {
      if (_read_event_callback) {
        _caller_cont->handleEvent(event, _receive_data_reader);
      }
      // Object injected into local cache
      Debug("update-reload", "Fill success id=%d", _request_id);
      break;
    }
    default: {
      Debug("update-reload", "Fill read fail id=%d", _request_id);
      CallBackUser(event, 0);
      break;
    }
    } // End of switch

    _netvc->do_io(VIO::CLOSE);
    free();
    ObjectReloadContAllocator.free(this);
    return EVENT_DONE;
  }
  default: {
    ink_release_assert(!"ObjectReloadEvent invalid state");
  }

  } // End of switch
  return 0;
}

int
ObjectReloadCont::CallBackUser(int event, void *d)
{
  _caller_cont->handleEvent(event, d);
  return 0;
}

// End of Update.cc
