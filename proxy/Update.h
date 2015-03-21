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

  Update.h


****************************************************************************/

#include "P_EventSystem.h"
#include "URL.h"
#include "HTTP.h"

#ifndef _Update_h_
#define _Update_h_

/////////////////////////////////////////////////////////
// Update subsystem specific events
/////////////////////////////////////////////////////////
#define UPDATE_EVENT_SUCCESS (UPDATE_EVENT_EVENTS_START + 0)
#define UPDATE_EVENT_SUCCESS_NOACTION (UPDATE_EVENT_EVENTS_START + 1)
#define UPDATE_EVENT_FAILED (UPDATE_EVENT_EVENTS_START + 2)

#define MAX_UPDATE_EVENT UPDATE_EVENT_FAILED

#define IS_UPDATE_EVENT(_e) ((((_e) >= UPDATE_EVENT_EVENTS_START) && ((_e) <= MAX_UPDATE_EVENT)) ? 1 : 0)

extern RecRawStatBlock *update_rsb;

enum {
  update_successes_stat,
  update_no_actions_stat,
  update_fails_stat,
  update_unknown_status_stat,
  update_state_machines_stat,

  update_stat_count
};

//////////////////////////////////////////////////////////////////////
// UpdateConfigParams -- Global subsystem configuration parameters
//////////////////////////////////////////////////////////////////////
class UpdateConfigParams : public RefCountObj
{
public:
  UpdateConfigParams();
  UpdateConfigParams(UpdateConfigParams &);
  ~UpdateConfigParams();
  UpdateConfigParams &operator=(UpdateConfigParams &);
  int operator==(UpdateConfigParams &);

  int
  IsEnabled()
  {
    return _enabled;
  }
  int
  ImmediateUpdate()
  {
    return _immediate_update;
  }
  int
  RetryCount()
  {
    return _retry_count;
  }
  int
  RetryInterval()
  {
    return _retry_interval;
  }
  int
  ConcurrentUpdates()
  {
    return _concurrent_updates;
  }
  int
  MaxUpdateSM()
  {
    return _max_update_state_machines;
  }
  int
  MaxMemoryUsageMB()
  {
    return _memory_use_in_mb;
  }

public:
  RecInt _enabled;
  RecInt _immediate_update;
  RecInt _retry_count;
  RecInt _retry_interval;
  RecInt _concurrent_updates;
  RecInt _max_update_state_machines;
  RecInt _memory_use_in_mb;
};

///////////////////////////////////////////////////
// UpdateEntry -- Per update object descriptor
///////////////////////////////////////////////////
class UpdateConfigList;

class UpdateEntry
{
public:
  UpdateEntry();
  ~UpdateEntry();

  enum {
    MIN_OFFSET_HOUR = 0,
    MAX_OFFSET_HOUR = 23,
    MIN_INTERVAL = 0,
    MAX_INTERVAL = 86400,
    MIN_DEPTH = 0,
    MAX_DEPTH = 128,
    SECONDS_PER_HOUR = 3600,
    SECONDS_PER_MIN = 60
  };

  void Init(int derived_url = 0);
  int ValidURL(char *, char *);
  int ValidHeaders(char *);
  int BuildHttpRequest();
  int ValidHeaderNameChar(char);
  int ValidSeparatorChar(char);
  int ValidHour(char *);
  int ValidInterval(char *);
  int ValidDepth(char *);
  int TerminalURL();
  void SetTerminalStatus(int);

  void ComputeScheduleTime();
  int ScheduleNow(time_t);

public:
  LINK(UpdateEntry, link);
  UpdateEntry *_group_link;
  UpdateEntry *_hash_link;

public:
  //////////////////////
  // URL data
  //////////////////////
  int _id;
  char *_url;
  URL _URLhandle;
  INK_MD5 _url_md5;
  int _terminal_url;

  ////////////////////////////
  // Request header data
  ////////////////////////////
  char *_request_headers;
  int _num_request_headers;
  HTTPHdr *_http_hdr;
  HTTPParser _http_parser;

  ///////////////////////////////
  // Configuration data
  ///////////////////////////////
  int _offset_hour;
  int _interval;
  int _max_depth;

  //////////////////////////////////
  // State data
  //////////////////////////////////
  time_t _start_time;
  int _expired;

  int _scheme_index;
  int _update_event_status;

  Ptr<UpdateConfigList> _indirect_list;
};

/////////////////////////////////////////////////////////////
// UpdateConfigList -- Container for UpdateEntry objects
/////////////////////////////////////////////////////////////
class UpdateConfigList : public RefCountObj
{
public:
  UpdateConfigList();
  ~UpdateConfigList();
  void Add(UpdateEntry *);
  int HashAdd(UpdateEntry *);
  UpdateEntry *Remove();
  void AddPending(UpdateEntry *);
  UpdateEntry *RemovePending();
  int
  Entries()
  {
    return _entry_q_elements;
  }
  int
  PendingEntries()
  {
    return _pending_q_elements;
  }

public:
  enum {
    HASH_TABLE_SIZE = 4096,
  };
  int _entry_q_elements;
  Queue<UpdateEntry> _entry_q;
  int _pending_q_elements;
  Queue<UpdateEntry> _pending_q;
  UpdateEntry **_hash_table;
};

////////////////////////////////////////////////////////////////
// UpdateManager -- External interface to Update subsystem
////////////////////////////////////////////////////////////////
class UpdateConfigManager;
class UpdateScheduler;

class UpdateManager
{
public:
  UpdateManager();
  ~UpdateManager();
  int start();

private:
  UpdateConfigManager *_CM;
  UpdateScheduler *_SCH;
};

extern UpdateManager updateManager;

//////////////////////////////////////////////////////////////////////////
// UpdateConfigManager -- Handle Update subsystem global configuration
//                        and URL list updates
//////////////////////////////////////////////////////////////////////////
typedef int (UpdateConfigManager::*UpdateConfigManagerContHandler)(int, void *);

class UpdateConfigManager : public Continuation
{
public:
  UpdateConfigManager();
  ~UpdateConfigManager();
  int init();
  int GetConfigParams(Ptr<UpdateConfigParams> *);
  int GetConfigList(Ptr<UpdateConfigList> *);

  static int URL_list_update_callout(const char *name, RecDataT data_type, RecData data, void *cookie);

  void
  SetFileName(char *f)
  {
    _filename = f;
  }
  char *
  GetFileName()
  {
    return _filename;
  }

  int ProcessUpdate(int event, Event *e);
  UpdateConfigList *BuildUpdateList();
  UpdateConfigList *ParseConfigFile(int);
  int GetDataLine(int, int, char *, int, int);

private:
  Event *_periodic_event;
  char *_filename;
  Ptr<UpdateConfigParams> _CP;
  Ptr<UpdateConfigParams> _CP_actual;
  Ptr<UpdateConfigList> _CL;
};

////////////////////////////////////////////////////////////////////////
// UpdateScheduler -- Handle scheduling of UpdateEntry objects
////////////////////////////////////////////////////////////////////////
typedef int (UpdateScheduler::*UpdateSchedulerContHandler)(int, void *);

class UpdateScheduler : public Continuation
{
public:
  UpdateScheduler(UpdateConfigManager *cm = NULL);
  ~UpdateScheduler();
  int Init();
  int Init(UpdateScheduler *, UpdateEntry *, Ptr<UpdateConfigParams>);

  int ScheduleEvent(int, void *);
  int Schedule(UpdateEntry *e = NULL);
  int ChildExitEventHandler(int, Event *);

private:
  Event *_periodic_event;
  int _recursive_update;
  UpdateConfigManager *_CM;
  Ptr<UpdateConfigParams> _CP;
  Ptr<UpdateConfigList> _CL;
  int _schedule_event_callbacks;
  int _update_state_machines;

  UpdateEntry *_base_EN; // Entry from which recursive
  //   list was derived
  UpdateScheduler *_parent_US; // Parent which created us
};

/////////////////////////////////////////////////////////////////
// UpdateSM -- State machine which handles object update action
/////////////////////////////////////////////////////////////////
class UpdateSM;
typedef int (UpdateSM::*UpdateSMContHandler)(int, void *);

class UpdateSM : public Continuation
{
public:
  enum state_t {
    USM_INIT = 1,
    USM_PROCESS_URL,
    USM_PROCESS_URL_COMPLETION,
    USM_EXIT,
  };

  enum {
    N_SCHEMES = 1,
  };

  static int http_scheme(UpdateSM *);
  static int http_scheme_postproc(UpdateSM *);

  UpdateSM(UpdateScheduler *, Ptr<UpdateConfigParams>, UpdateEntry *);
  ~UpdateSM();
  void Start();
  int HandleSMEvent(int, Event *);

public:
  UpdateEntry *_EN;

private:
  UpdateScheduler *_US;
  Ptr<UpdateConfigParams> _CP;
  state_t _state;
  int _return_status;
  int _retries;
};

struct dispatch_entry {
  const char **scheme;
  int (*func)(UpdateSM *);
};

extern struct dispatch_entry scheme_dispatch_table[UpdateSM::N_SCHEMES];
extern struct dispatch_entry scheme_post_dispatch_table[UpdateSM::N_SCHEMES];

struct html_tag {
  const char *tag;
  const char *attr;
};

/////////////////////////////////////////////////////////////////////////////
// RecursiveHttpGet -- Generate URL list by recursively traversing
//                     non-terminal URL(s) up to the specified depth.
/////////////////////////////////////////////////////////////////////////////
class ObjectReloadCont;
class RecursiveHttpGet;

typedef int (RecursiveHttpGet::*RecursiveHttpGetContHandler)(int, Event *);

class HtmlParser
{
  // Parse Html routines
public:
  static char default_zero_char;

  enum scan_state_t {
    SCAN_INIT = 1,
    SCAN_START,
    FIND_TAG_START,
    COPY_TAG,
    IGNORE_COMMENT_START,
    IGNORE_COMMENT,
    FIND_ATTR_START,
    COPY_ATTR,
    FIND_ATTR_VALUE_DELIMITER,
    FIND_ATTR_VALUE_START,
    COPY_ATTR_VALUE,
    VALIDATE_ENTRY,
    VALIDATE_ENTRY_RESTART,
    RESUME_ATTR_VALUE_SCAN,
    TERMINATE_COPY_ATTR_VALUE
  };

  enum {
    MAX_TAG_NAME_LENGTH = 1024,
    MAX_ATTR_NAME_LENGTH = 1024,
  };

  HtmlParser()
    : _attr_matched(false), _url(0), _comment_end_ptr(0), _scan_state(SCAN_INIT), _tag(&default_zero_char, 32),
      _attr(&default_zero_char, 32), _attr_value(&default_zero_char, 32), _attr_value_hash_char_index(-1), _attr_value_quoted(0),
      _html_doc_base(&default_zero_char, 128), _result(&default_zero_char, 128), allowable_html_tags(0), allowable_html_attrs(0)
  {
  }

  ~HtmlParser() {}

  void
  Init(char *url, struct html_tag *allowed_html_tags, struct html_tag *allowed_html_attrs = NULL)
  {
    _url = url;
    allowable_html_tags = allowed_html_tags;
    allowable_html_attrs = allowed_html_attrs;
    _attr_matched = false;
  }

  int ParseHtml(IOBufferReader *, char **, char **);
  int ScanHtmlForURL(IOBufferReader *, char **, char **);
  int AllowTagAttrValue();
  int ValidProtoScheme(char *);
  int ValidSupportedProtoScheme(char *);
  int ExtractURL(char **, char **);
  int ConstructURL(char **, char **);
  DynArray<char> *MakeURL(char *, char *, int, int);
  DynArray<char> *PrependString(const char *, int, char *, int);
  bool _attr_matched;

  char *_url;
  char *_comment_end_ptr;
  scan_state_t _scan_state;
  DynArray<char> _tag;
  DynArray<char> _attr;
  DynArray<char> _attr_value;
  intptr_t _attr_value_hash_char_index; // '#' char loc
  unsigned char _attr_value_quoted;
  DynArray<char> _html_doc_base;
  DynArray<char> _result;

  struct html_tag *allowable_html_tags;
  struct html_tag *allowable_html_attrs;
};

class RecursiveHttpGet : public Continuation
{
public:
  RecursiveHttpGet();
  ~RecursiveHttpGet();
  void Init(Continuation *, char *, char *, URL *, HTTPHdr *, int, Ptr<UpdateConfigList>, struct html_tag *allowed_html_tags);
  int RecursiveHttpGetEvent(int, Event *);

  int ExitEventHandler(int, Event *);

public:
  int _id;
  Continuation *_caller_cont;
  char *_request_headers;
  URL *_url_data;
  HTTPHdr *_http_hdr;
  int _recursion_depth;
  Ptr<UpdateConfigList> _CL;
  ObjectReloadCont *_OL;
  UpdateEntry *_group_link_head;
  int _active_child_state_machines;

  HtmlParser html_parser;
};

/////////////////////////////////////////////////////////////////////////
// ObjectReloadCont -- Read given URL object via the local proxy port
/////////////////////////////////////////////////////////////////////////
class ObjectReloadCont;
typedef int (ObjectReloadCont::*ObjectReloadContHandler)(int, void *);

class ObjectReloadCont : public Continuation
{
public:
  ObjectReloadCont();
  ~ObjectReloadCont();
  void Init(Continuation *, char *, int, char *, int, int, int);
  void free();
  int ObjectReloadEvent(int, void *);
  int CallBackUser(int, void *);

  enum state_t {
    START = 1,
    ATTEMPT_CONNECT,
    WRITING_URL,
    READING_DATA,
  };

  Continuation *_caller_cont;
  int _request_id;
  MIOBuffer *_send_data;
  MIOBuffer *_receive_data;
  IOBufferReader *_receive_data_reader;
  Event *_start_event;
  state_t _state;
  Action *_cur_action;
  class NetVConnection *_netvc;
  VIO *_write_vio;
  VIO *_read_vio;
  int _read_event_callback;
};

extern ClassAllocator<ObjectReloadCont> ObjectReloadContAllocator;

#endif // _Update_h_

// End of Update.h
