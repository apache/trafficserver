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
 * CoreAPI.cc
 *
 * Implementation of many of TSMgmtAPI functions, but from local side.
 *
 *
 ***************************************************************************/

#include "tscore/ink_platform.h"
#include "tscore/ink_file.h"
#include "tscore/ParseRules.h"
#include "Alarms.h"
#include "MgmtUtils.h"
#include "LocalManager.h"
#include "FileManager.h"
#include "Rollback.h"
#include "WebMgmtUtils.h"
#include "tscore/Diags.h"
#include "ExpandingArray.h"

#include "CoreAPI.h"
#include "CoreAPIShared.h"
#include "EventCallback.h"
#include "tscore/I_Layout.h"
#include "tscore/ink_cap.h"

#include <vector>

// global variable
static CallbackTable *local_event_callbacks;

extern FileManager *configFiles; // global in traffic_manager

/*-------------------------------------------------------------------------
 * Init
 *-------------------------------------------------------------------------
 * performs any necesary initializations for the local API client,
 * eg. set up global structures; called by the TSMgmtAPI::TSInit()
 */
TSMgmtError
Init(const char * /* socket_path ATS_UNUSED */, TSInitOptionT options)
{
  // socket_path should be null; only applies to remote clients
  if (0 == (options & TS_MGMT_OPT_NO_EVENTS)) {
    local_event_callbacks = create_callback_table("local_callbacks");
    if (!local_event_callbacks) {
      return TS_ERR_SYS_CALL;
    }
  } else {
    local_event_callbacks = nullptr;
  }

  return TS_ERR_OKAY;
}

/*-------------------------------------------------------------------------
 * Terminate
 *-------------------------------------------------------------------------
 * performs any necesary cleanup of global structures, etc,
 * for the local API client,
 */
TSMgmtError
Terminate()
{
  delete_callback_table(local_event_callbacks);

  return TS_ERR_OKAY;
}

/***************************************************************************
 * Control Operations
 ***************************************************************************/

// bool ProxyShutdown()
//
//  Attempts to turn the proxy off.  Returns
//    true if the proxy is off when the call returns
//    and false if it is still on
//
static bool
ProxyShutdown()
{
  int i = 0;

  // Check to make sure that we are not already down
  if (!lmgmt->processRunning()) {
    return true;
  }

  lmgmt->processShutdown(false /* only shut down the proxy*/);

  // Wait for awhile for shtudown to happen
  do {
    mgmt_sleep_sec(1);
    i++;
  } while (i < 10 && lmgmt->processRunning());

  // See if we succeeded
  if (lmgmt->processRunning()) {
    return false;
  } else {
    return true;
  }
}
/*-------------------------------------------------------------------------
 * ProxyStateGet
 *-------------------------------------------------------------------------
 * return TS_PROXY_OFF if  Traffic Server is off.
 * return TS_PROXY_ON if Traffic Server is on.
 */
TSProxyStateT
ProxyStateGet()
{
  if (!lmgmt->processRunning()) {
    return TS_PROXY_OFF;
  } else {
    return TS_PROXY_ON;
  }
}

/*-------------------------------------------------------------------------
 * ProxyStateSet
 *-------------------------------------------------------------------------
 * If state == TS_PROXY_ON, will turn on TS (unless it's already running).
 * If state == TS_PROXY_OFF, will turn off TS (unless it's already off).
 * tsArgs  - (optional) a string with space delimited options that user
 *            wants to start traffic Server with
 */
TSMgmtError
ProxyStateSet(TSProxyStateT state, TSCacheClearT clear)
{
  char tsArgs[MAX_BUF_SIZE];
  char *proxy_options;

  ink_zero(tsArgs);

  switch (state) {
  case TS_PROXY_OFF:
    if (!ProxyShutdown()) { // from WebMgmtUtils
      goto Lerror;          // unsuccessful shutdown
    }
    break;
  case TS_PROXY_ON:
    if (lmgmt->processRunning()) { // already on
      break;
    }

    // Start with the default options from records.config.
    if (RecGetRecordString_Xmalloc("proxy.config.proxy_binary_opts", &proxy_options) == REC_ERR_OKAY) {
      snprintf(tsArgs, sizeof(tsArgs), "%s", proxy_options);
      ats_free(proxy_options);
    }

    if (clear & TS_CACHE_CLEAR_CACHE) {
      ink_strlcat(tsArgs, " -K", sizeof(tsArgs));
    }

    if (clear & TS_CACHE_CLEAR_HOSTDB) {
      ink_strlcat(tsArgs, " -k", sizeof(tsArgs));
    }

    mgmt_log("[ProxyStateSet] Traffic Server Args: '%s %s'\n", lmgmt->proxy_options.c_str(), tsArgs);

    lmgmt->run_proxy = true;
    lmgmt->listenForProxy();
    if (!lmgmt->startProxy(tsArgs)) {
      goto Lerror;
    }

    break;

  default:
    goto Lerror;
  }

  return TS_ERR_OKAY;

Lerror:
  return TS_ERR_FAIL; /* failed to set proxy state */
}

#if TS_USE_REMOTE_UNWINDING

#include <libunwind.h>
#include <libunwind-ptrace.h>
#include <sys/ptrace.h>
#include <cxxabi.h>

typedef std::vector<pid_t> threadlist;

static threadlist
threads_for_process(pid_t proc)
{
  DIR *dir             = nullptr;
  struct dirent *entry = nullptr;

  char path[64];
  threadlist threads;

  if (snprintf(path, sizeof(path), "/proc/%ld/task", (long)proc) >= (int)sizeof(path)) {
    goto done;
  }

  dir = opendir(path);
  if (dir == nullptr) {
    goto done;
  }

  while ((entry = readdir(dir))) {
    pid_t threadid;

    if (isdot(entry->d_name) || isdotdot(entry->d_name)) {
      continue;
    }

    threadid = strtol(entry->d_name, nullptr, 10);
    if (threadid > 0) {
      threads.push_back(threadid);
      Debug("backtrace", "found thread %ld", (long)threadid);
    }
  }

done:
  if (dir) {
    closedir(dir);
  }

  return threads;
}

static void
backtrace_for_thread(pid_t threadid, TextBuffer &text)
{
  int status;
  unw_addr_space_t addr_space = nullptr;
  unw_cursor_t cursor;
  void *ap       = nullptr;
  pid_t target   = -1;
  unsigned level = 0;

  // First, attach to the child, causing it to stop.
  status = ptrace(PTRACE_ATTACH, threadid, 0, 0);
  if (status < 0) {
    Debug("backtrace", "ptrace(ATTACH, %ld) -> %s (%d)", (long)threadid, strerror(errno), errno);
    return;
  }

  // Wait for it to stop (XXX should be a timed wait ...)
  target = waitpid(threadid, &status, __WALL | WUNTRACED);
  Debug("backtrace", "waited for target %ld, found PID %ld, %s", (long)threadid, (long)target,
        WIFSTOPPED(status) ? "STOPPED" : "???");
  if (target < 0) {
    goto done;
  }

  ap = _UPT_create(threadid);
  Debug("backtrace", "created UPT %p", ap);
  if (ap == nullptr) {
    goto done;
  }

  addr_space = unw_create_addr_space(&_UPT_accessors, 0 /* byteorder */);
  Debug("backtrace", "created address space %p", addr_space);
  if (addr_space == nullptr) {
    goto done;
  }

  status = unw_init_remote(&cursor, addr_space, ap);
  Debug("backtrace", "unw_init_remote(...) -> %d", status);
  if (status != 0) {
    goto done;
  }

  while (unw_step(&cursor) > 0) {
    unw_word_t ip;
    unw_word_t offset;
    char buf[256];

    unw_get_reg(&cursor, UNW_REG_IP, &ip);

    if (unw_get_proc_name(&cursor, buf, sizeof(buf), &offset) == 0) {
      int status;
      char *name = abi::__cxa_demangle(buf, nullptr, nullptr, &status);
      text.format("%-4u 0x%016llx %s + %p\n", level, (unsigned long long)ip, name ? name : buf, (void *)offset);
      free(name);
    } else {
      text.format("%-4u 0x%016llx 0x0 + %p\n", level, (unsigned long long)ip, (void *)offset);
    }

    ++level;
  }

done:
  if (addr_space) {
    unw_destroy_addr_space(addr_space);
  }

  if (ap) {
    _UPT_destroy(ap);
  }

  status = ptrace(PTRACE_DETACH, target, NULL, NULL);
  Debug("backtrace", "ptrace(DETACH, %ld) -> %d (errno %d)", (long)target, status, errno);
}

TSMgmtError
ServerBacktrace(unsigned /* options */, char **trace)
{
  *trace = nullptr;

  // Unfortunately, we need to be privileged here. We either need to be root or to be holding
  // the CAP_SYS_PTRACE capability. Even though we are the parent traffic_manager, it is not
  // traceable without privilege because the process credentials do not match.
  ElevateAccess access(ElevateAccess::TRACE_PRIVILEGE);
  threadlist threads(threads_for_process(lmgmt->watched_process_pid));
  TextBuffer text(0);

  Debug("backtrace", "tracing %zd threads for traffic_server PID %ld", threads.size(), (long)lmgmt->watched_process_pid);

  for (auto threadid : threads) {
    Debug("backtrace", "tracing thread %ld", (long)threadid);
    // Get the thread name using /proc/PID/comm
    ats_scoped_fd fd;
    char threadname[128];

    snprintf(threadname, sizeof(threadname), "/proc/%ld/comm", (long)threadid);
    fd = open(threadname, O_RDONLY);
    if (fd >= 0) {
      text.format("Thread %ld, ", (long)threadid);
      text.readFromFD(fd);
      text.chomp();
    } else {
      text.format("Thread %ld", (long)threadid);
    }

    text.format(":\n");

    backtrace_for_thread(threadid, text);
    text.format("\n");
  }

  *trace = text.release();
  return TS_ERR_OKAY;
}

#else /* TS_USE_REMOTE_UNWINDING */

TSMgmtError
ServerBacktrace(unsigned /* options */, char **trace)
{
  *trace = nullptr;
  return TS_ERR_NOT_SUPPORTED;
}

#endif

/*-------------------------------------------------------------------------
 * Reconfigure
 *-------------------------------------------------------------------------
 * Rereads configuration files
 */
TSMgmtError
Reconfigure()
{
  configFiles->rereadConfig();                              // TM rereads
  lmgmt->signalEvent(MGMT_EVENT_PLUGIN_CONFIG_UPDATE, "*"); // TS rereads
  RecSetRecordInt("proxy.node.config.reconfigure_time", time(nullptr), REC_SOURCE_DEFAULT);
  RecSetRecordInt("proxy.node.config.reconfigure_required", 0, REC_SOURCE_DEFAULT);

  return TS_ERR_OKAY;
}

/*-------------------------------------------------------------------------
 * Restart
 *-------------------------------------------------------------------------
 * Restarts Traffic Manager. Traffic Cop must be running in order to
 * restart Traffic Manager!!
 */
TSMgmtError
Restart(unsigned options)
{
  lmgmt->mgmt_shutdown_triggered_at = time(nullptr);
  lmgmt->mgmt_shutdown_outstanding  = (options & TS_RESTART_OPT_DRAIN) ? MGMT_PENDING_IDLE_RESTART : MGMT_PENDING_RESTART;

  return TS_ERR_OKAY;
}

/*-------------------------------------------------------------------------
 * Bouncer
 *-------------------------------------------------------------------------
 * Bounces traffic_server process(es).
 */
TSMgmtError
Bounce(unsigned options)
{
  lmgmt->mgmt_shutdown_triggered_at = time(nullptr);
  lmgmt->mgmt_shutdown_outstanding  = (options & TS_RESTART_OPT_DRAIN) ? MGMT_PENDING_IDLE_BOUNCE : MGMT_PENDING_BOUNCE;

  return TS_ERR_OKAY;
}

/*-------------------------------------------------------------------------
 * Stop
 *-------------------------------------------------------------------------
 * Stops traffic_server process(es).
 */
TSMgmtError
Stop(unsigned options)
{
  lmgmt->mgmt_shutdown_triggered_at = time(nullptr);
  lmgmt->mgmt_shutdown_outstanding  = (options & TS_STOP_OPT_DRAIN) ? MGMT_PENDING_IDLE_STOP : MGMT_PENDING_STOP;

  return TS_ERR_OKAY;
}

/*-------------------------------------------------------------------------
 * Drain
 *-------------------------------------------------------------------------
 * Drain requests of traffic_server
 */
TSMgmtError
Drain(unsigned options)
{
  switch (options) {
  case TS_DRAIN_OPT_NONE:
    lmgmt->mgmt_shutdown_outstanding = MGMT_PENDING_DRAIN;
    break;
  case TS_DRAIN_OPT_IDLE:
    lmgmt->mgmt_shutdown_outstanding = MGMT_PENDING_IDLE_DRAIN;
    break;
  case TS_DRAIN_OPT_UNDO:
    lmgmt->mgmt_shutdown_outstanding = MGMT_PENDING_UNDO_DRAIN;
    break;
  default:
    ink_release_assert(!"Not expected to reach here");
  }
  return TS_ERR_OKAY;
}

/*-------------------------------------------------------------------------
 * StorageDeviceCmdOffline
 *-------------------------------------------------------------------------
 * Disable a storage device.
 * [amc] I don't think this is called but is required because of the way the
 * CoreAPI is linked (it must match the remote CoreAPI signature so compiling
 * this source or CoreAPIRemote.cc yields the same set of symbols).
 */
TSMgmtError
StorageDeviceCmdOffline(const char *dev)
{
  lmgmt->signalEvent(MGMT_EVENT_STORAGE_DEVICE_CMD_OFFLINE, dev);
  return TS_ERR_OKAY;
}
/*-------------------------------------------------------------------------
 * Lifecycle Message
 *-------------------------------------------------------------------------
 * Signal plugins.
 */
TSMgmtError
LifecycleMessage(const char *tag, void const *data, size_t data_size)
{
  ink_release_assert(!"Not expected to reach here");
  lmgmt->signalEvent(MGMT_EVENT_LIFECYCLE_MESSAGE, tag);
  return TS_ERR_OKAY;
}
/**************************************************************************
 * RECORD OPERATIONS
 *************************************************************************/
/*-------------------------------------------------------------------------
 * MgmtRecordGet
 *-------------------------------------------------------------------------
 * rec_ele has allocated memory already but with all empty fields.
 * The record info associated with rec_name will be returned in rec_ele.
 */
TSMgmtError
MgmtRecordGet(const char *rec_name, TSRecordEle *rec_ele)
{
  RecDataT rec_type;
  char rec_val[MAX_BUF_SIZE];
  char *str_val;
  MgmtIntCounter counter_val;
  MgmtInt int_val;

  Debug("RecOp", "[MgmtRecordGet] Start");

  // initialize the record name
  rec_ele->rec_name = ats_strdup(rec_name);
  memset(rec_val, 0, MAX_BUF_SIZE);

  // get variable type; returns INVALID if invalid rec_name
  rec_type = varType(rec_name);
  switch (rec_type) {
  case RECD_COUNTER:
    rec_ele->rec_type = TS_REC_COUNTER;
    if (!varCounterFromName(rec_name, &(counter_val))) {
      return TS_ERR_FAIL;
    }
    rec_ele->valueT.counter_val = (TSCounter)counter_val;

    Debug("RecOp", "[MgmtRecordGet] Get Counter Var %s = %" PRId64 "", rec_ele->rec_name, rec_ele->valueT.counter_val);
    break;

  case RECD_INT:
    rec_ele->rec_type = TS_REC_INT;
    if (!varIntFromName(rec_name, &(int_val))) {
      return TS_ERR_FAIL;
    }
    rec_ele->valueT.int_val = (TSInt)int_val;

    Debug("RecOp", "[MgmtRecordGet] Get Int Var %s = %" PRId64 "", rec_ele->rec_name, rec_ele->valueT.int_val);
    break;

  case RECD_FLOAT:
    rec_ele->rec_type = TS_REC_FLOAT;
    if (!varFloatFromName(rec_name, &(rec_ele->valueT.float_val))) {
      return TS_ERR_FAIL;
    }

    Debug("RecOp", "[MgmtRecordGet] Get Float Var %s = %f", rec_ele->rec_name, rec_ele->valueT.float_val);
    break;

  case RECD_STRING:
    if (!varStrFromName(rec_name, rec_val, MAX_BUF_SIZE)) {
      return TS_ERR_FAIL;
    }

    if (rec_val[0] != '\0') { // non-NULL string value
      // allocate memory & duplicate string value
      str_val = ats_strdup(rec_val);
    } else {
      str_val = ats_strdup("NULL");
    }

    rec_ele->rec_type          = TS_REC_STRING;
    rec_ele->valueT.string_val = str_val;
    Debug("RecOp", "[MgmtRecordGet] Get String Var %s = %s", rec_ele->rec_name, rec_ele->valueT.string_val);
    break;

  default: // UNKOWN TYPE
    Debug("RecOp", "[MgmtRecordGet] Get Failed : %d is Unknown Var type %s", rec_type, rec_name);
    return TS_ERR_FAIL;
  }

  return TS_ERR_OKAY;
}

// This is not implemented in the Core side of the API because we don't want
// to buffer up all the matching records in memory. We stream the records
// directory onto the management socket in handle_record_match(). This stub
// is just here for link time dependencies.
TSMgmtError
MgmtRecordGetMatching(const char * /* regex */, TSList /* rec_vals */)
{
  return TS_ERR_FAIL;
}

TSMgmtError
MgmtConfigRecordDescribe(const char * /* rec_name */, unsigned /* flags */, TSConfigRecordDescription * /* val */)
{
  return TS_ERR_NOT_SUPPORTED;
}

TSMgmtError
MgmtConfigRecordDescribeMatching(const char *, unsigned, TSList)
{
  return TS_ERR_NOT_SUPPORTED;
}

/*-------------------------------------------------------------------------
 * reads the RecordsConfig info to determine which type of action is needed
 * when the record rec_name is changed; if the rec_name is invalid,
 * then returns TS_ACTION_UNDEFINED
 */
TSActionNeedT
determine_action_need(const char *rec_name)
{
  RecUpdateT update_t;

  if (REC_ERR_OKAY != RecGetRecordUpdateType(rec_name, &update_t)) {
    return TS_ACTION_UNDEFINED;
  }

  switch (update_t) {
  case RECU_NULL: // default:don't know behaviour
    return TS_ACTION_UNDEFINED;

  case RECU_DYNAMIC: // update dynamically by rereading config files
    return TS_ACTION_RECONFIGURE;

  case RECU_RESTART_TS: // requires TS restart
    return TS_ACTION_RESTART;

  case RECU_RESTART_TM: // requires TM/TS restart
    return TS_ACTION_RESTART;

  default: // shouldn't get here actually
    return TS_ACTION_UNDEFINED;
  }

  return TS_ACTION_UNDEFINED; // ERROR
}

/*-------------------------------------------------------------------------
 * MgmtRecordSet
 *-------------------------------------------------------------------------
 * Uses bool WebMgmtUtils::varSetFromStr(const char*, const char* )
 * Sets the named local manager variable from the value string
 * passed in.  Does the appropriate type conversion on
 * value string to get it to the type of the local manager
 * variable
 *
 *  returns true if the variable was successfully set
 *   and false otherwise
 */
TSMgmtError
MgmtRecordSet(const char *rec_name, const char *val, TSActionNeedT *action_need)
{
  Debug("RecOp", "[MgmtRecordSet] Start");

  if (!rec_name || !val || !action_need) {
    return TS_ERR_PARAMS;
  }

  *action_need = determine_action_need(rec_name);

  if (recordValidityCheck(rec_name, val)) {
    if (varSetFromStr(rec_name, val)) {
      return TS_ERR_OKAY;
    }
  }

  return TS_ERR_FAIL;
}

/*-------------------------------------------------------------------------
 * MgmtRecordSetInt
 *-------------------------------------------------------------------------
 * Use the record's name to look up the record value and its type.
 * Returns TS_ERR_FAIL if the type is not a valid integer.
 * Converts the integer value to a string and call MgmtRecordSet
 */
TSMgmtError
MgmtRecordSetInt(const char *rec_name, MgmtInt int_val, TSActionNeedT *action_need)
{
  if (!rec_name || !action_need) {
    return TS_ERR_PARAMS;
  }

  // convert int value to string for validity check
  char str_val[MAX_RECORD_SIZE];

  memset(str_val, 0, MAX_RECORD_SIZE);
  snprintf(str_val, sizeof(str_val), "%" PRId64 "", int_val);

  return MgmtRecordSet(rec_name, str_val, action_need);
}

/*-------------------------------------------------------------------------
 * MgmtRecordSetCounter
 *-------------------------------------------------------------------------
 * converts the counter_val to a string and uses MgmtRecordSet
 */
TSMgmtError
MgmtRecordSetCounter(const char *rec_name, MgmtIntCounter counter_val, TSActionNeedT *action_need)
{
  if (!rec_name || !action_need) {
    return TS_ERR_PARAMS;
  }

  // convert int value to string for validity check
  char str_val[MAX_RECORD_SIZE];

  memset(str_val, 0, MAX_RECORD_SIZE);
  snprintf(str_val, sizeof(str_val), "%" PRId64 "", counter_val);

  return MgmtRecordSet(rec_name, str_val, action_need);
}

/*-------------------------------------------------------------------------
 * MgmtRecordSetFloat
 *-------------------------------------------------------------------------
 * converts the float value to string (to do record validity check)
 * and calls MgmtRecordSet
 */
TSMgmtError
MgmtRecordSetFloat(const char *rec_name, MgmtFloat float_val, TSActionNeedT *action_need)
{
  if (!rec_name || !action_need) {
    return TS_ERR_PARAMS;
  }

  // convert float value to string for validity check
  char str_val[MAX_RECORD_SIZE];

  memset(str_val, 0, MAX_RECORD_SIZE);
  snprintf(str_val, sizeof(str_val), "%f", float_val);

  return MgmtRecordSet(rec_name, str_val, action_need);
}

/*-------------------------------------------------------------------------
 * MgmtRecordSetString
 *-------------------------------------------------------------------------
 * The string value is copied so it's okay to free the string later
 */
TSMgmtError
MgmtRecordSetString(const char *rec_name, const char *string_val, TSActionNeedT *action_need)
{
  return MgmtRecordSet(rec_name, string_val, action_need);
}

/**************************************************************************
 * EVENTS
 *************************************************************************/
/*-------------------------------------------------------------------------
 * EventSignal
 *-------------------------------------------------------------------------
 * LAN: THIS FUNCTION IS HACKED AND INCOMPLETE!!!!!
 * with the current alarm processor system, the argument list is NOT
 * used; a set description is associated with each alarm already;
 * be careful because this alarm description is used to keep track
 * of alarms in the current alarm processor
 */
TSMgmtError
EventSignal(const char * /* event_name ATS_UNUSED */, va_list /* ap ATS_UNUSED */)
{
  // char *text;
  // int id;

  // id = get_event_id(event_name);
  // text = get_event_text(event_name);
  // lmgmt->alarm_keeper->signalAlarm(id, text, NULL);

  return TS_ERR_OKAY;
}

/*-------------------------------------------------------------------------
 * EventResolve
 *-------------------------------------------------------------------------
 * Resolves the event of the given event_name. If the event is already
 * unresolved, just return TS_ERR_OKAY.

 */
TSMgmtError
EventResolve(const char *event_name)
{
  alarm_t a;

  if (!event_name) {
    return TS_ERR_PARAMS;
  }

  a = get_event_id(event_name);
  lmgmt->alarm_keeper->resolveAlarm(a);

  return TS_ERR_OKAY;
}

/*-------------------------------------------------------------------------
 * ActiveEventGetMlt
 *-------------------------------------------------------------------------
 * returns failure, and an incomplete active_alarms list if any of
 * functions fail for a single event
 * note: returns list of local alarms at that instant of fn call (snapshot)
 */
TSMgmtError
ActiveEventGetMlt(LLQ *active_events)
{
  if (!active_events) {
    return TS_ERR_PARAMS;
  }

  // Alarms stores a hashtable of all active alarms where:
  // key = alarm_t,
  // value = alarm_description defined in Alarms.cc alarmText[] array
  std::unordered_map<std::string, Alarm *> const &event_ht = lmgmt->alarm_keeper->getLocalAlarms();

  // iterate through hash-table and insert event_name's into active_events list
  for (auto &&it : event_ht) {
    // convert key to int; insert into llQ
    int event_id     = ink_atoi(it.first.c_str());
    char *event_name = get_event_name(event_id);
    if (event_name) {
      if (!enqueue(active_events, event_name)) { // returns true if successful
        return TS_ERR_FAIL;
      }
    }
  }

  return TS_ERR_OKAY;
}

/*-------------------------------------------------------------------------
 * EventIsActive
 *-------------------------------------------------------------------------
 * Sets *is_current to true if the event named event_name is currently
 * unresolved; otherwise sets *is_current to false.
 */
TSMgmtError
EventIsActive(const char *event_name, bool *is_current)
{
  alarm_t a;

  if (!event_name || !is_current) {
    return TS_ERR_PARAMS;
  }

  a = get_event_id(event_name);
  // consider an invalid event_name an error
  if (a < 0) {
    return TS_ERR_PARAMS;
  }
  if (lmgmt->alarm_keeper->isCurrentAlarm(a)) {
    *is_current = true; // currently an event
  } else {
    *is_current = false;
  }

  return TS_ERR_OKAY;
}

/*-------------------------------------------------------------------------
 * EventSignalCbRegister
 *-------------------------------------------------------------------------
 * This differs from the remote side callback registered. Technically, I think
 * we need to redesign the alarm processor before we can handle the callback
 * functionality we want to accomplish. Because currently the alarm processor
 * only allow registering callbacks for general alarms.
 * Mimic remote side and have a separate structure (eg. hashtable) of
 * event callback functions for each type of event. The functions are also
 * stored in the the hashtable, not in the TM alarm processor model
 */
TSMgmtError
EventSignalCbRegister(const char *event_name, TSEventSignalFunc func, void *data)
{
  return cb_table_register(local_event_callbacks, event_name, func, data, nullptr);
}

/*-------------------------------------------------------------------------
 * EventSignalCbUnregister
 *-------------------------------------------------------------------------
 * Removes the callback function from the local side CallbackTable
 */
TSMgmtError
EventSignalCbUnregister(const char *event_name, TSEventSignalFunc func)
{
  return cb_table_unregister(local_event_callbacks, event_name, func);
}

/*-------------------------------------------------------------------------
 * HostStatusSetDown
 *-------------------------------------------------------------------------
 * Sets the HOST status to Down
 *
 * 'marshalled_req' is marshalled here, (host_name and down_time, na).
 * 'len' is the length of the 'req' marshaled data.
 * 'na' unused.
 */
TSMgmtError
HostStatusSetDown(const char *marshalled_req, int len, const char *na)
{
  lmgmt->hostStatusSetDown(marshalled_req, len);
  return TS_ERR_OKAY;
}

/*-------------------------------------------------------------------------
 * HostStatusSetUp
 *-------------------------------------------------------------------------
 * Sets the HOST status to Up
 *
 * 'marshalled_req' is marshalled here, host_name.
 * 'len' is the length of 'req'
 * 'na' unused.
 */
TSMgmtError
HostStatusSetUp(const char *marshalled_req, int len, const char *na)
{
  lmgmt->hostStatusSetUp(marshalled_req, len);
  return TS_ERR_OKAY;
}

/*-------------------------------------------------------------------------
 * StatsReset
 *-------------------------------------------------------------------------
 * Iterates through the RecordsConfig table, and for all stats
 * (type PROCESS, NODE, CLUSTER), sets them back to their default value
 * If one stat fails to be set correctly, then continues onto next one,
 * but will return TS_ERR_FAIL. Only returns TS_ERR_OKAY if all
 * stats are set back to defaults successfully.
 */
TSMgmtError
StatsReset(const char *name)
{
  lmgmt->clearStats(name);
  return TS_ERR_OKAY;
}

/*-------------------------------------------------------------
 * rmserver.cfg
 *-------------------------------------------------------------*/
