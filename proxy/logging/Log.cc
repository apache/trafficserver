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

/***************************************************************************
 Log.cc


 This file defines the implementation of the static Log class, which is
 primarily used as a namespace.  That is, there are no Log objects, but the
 class scope and static members provide a protected namespace for all of
 the logging routines and enumerated types.  When C++ namespaces are more
 widely-implemented, Log could be implemented as a namespace rather than a
 class.


 ***************************************************************************/
#include "libts.h"

#include "Error.h"
#include "Main.h"
#include "P_EventSystem.h"
#include "P_Net.h"
#include "I_Machine.h"
#include "HTTP.h"

#include "LogAccess.h"
#include "LogField.h"
#include "LogFilter.h"
#include "LogFormat.h"
#include "LogFile.h"
#include "LogHost.h"
#include "LogObject.h"
#include "LogConfig.h"
#include "LogBuffer.h"
#include "LogUtils.h"
#include "Log.h"
#include "LogSock.h"
#include "SimpleTokenizer.h"

#include "ink_apidefs.h"

#define FLUSH_THREAD_SLEEP_TIMEOUT (1)
#define FLUSH_THREAD_MIN_FLUSH_COUNTER (FLUSH_ARRAY_SIZE/4)
#define PERIODIC_TASKS_INTERVAL 5 // TODO: Maybe this should be done as a config option

// Log global objects
inkcoreapi TextLogObject *Log::error_log = NULL;
LogConfig *Log::config = NULL;
LogFieldList Log::global_field_list;
LogFormat *Log::global_scrap_format = NULL;
LogObject *Log::global_scrap_object = NULL;
Log::LoggingMode Log::logging_mode = LOG_NOTHING;

// Inactive objects
LogObject **Log::inactive_objects;
size_t Log::numInactiveObjects;
size_t Log::maxInactiveObjects;

// Flush thread stuff
volatile unsigned long Log::flush_counter = 0;
ink_mutex Log::flush_mutex;
ink_cond Log::flush_cond;
ink_thread Log::flush_thread;

// Collate thread stuff
ink_mutex Log::collate_mutex;
ink_cond Log::collate_cond;
ink_thread Log::collate_thread;
int Log::collation_accept_file_descriptor;
int Log::collation_port;

// Log private objects
int Log::init_status = 0;
int Log::config_flags = 0;
bool Log::logging_mode_changed = false;

// Hash table for LogField symbols
InkHashTable *Log::field_symbol_hash = 0;

RecRawStatBlock *log_rsb;

/*-------------------------------------------------------------------------
  Log::change_configuration

  This routine is invoked when the current LogConfig object says it needs
  to be changed (as the result of a manager callback).
  -------------------------------------------------------------------------*/
void
Log::change_configuration()
{
  Debug("log-config", "Changing configuration ...");

  LogConfig *new_config = NEW(new LogConfig);
  ink_assert(new_config != NULL);
  new_config->read_configuration_variables();

  // grab the _APImutex so we can transfer the api objects to
  // the new config
  //
  ink_mutex_acquire(Log::config->log_object_manager._APImutex);
  Debug("log-api-mutex", "Log::change_configuration acquired api mutex");

  new_config->init(Log::config);

  // Swap in the new config object
  //
  ink_atomic_swap_ptr((void *) &Log::config, new_config);

  // Force new buffers for inactive objects
  //
  for (size_t i = 0; i < numInactiveObjects; i++) {
    inactive_objects[i]->force_new_buffer();
  }

  ink_mutex_release(Log::config->log_object_manager._APImutex);
  Debug("log-api-mutex", "Log::change_configuration released api mutex");

  Debug("log-config", "... new configuration in place");
}

void
Log::add_to_inactive(LogObject * object)
{
  if (Log::numInactiveObjects == Log::maxInactiveObjects) {
    Log::maxInactiveObjects += LOG_OBJECT_ARRAY_DELTA;
    LogObject **new_objects = new LogObject *[Log::maxInactiveObjects];

    for (size_t i = 0; i < Log::numInactiveObjects; i++) {
      new_objects[i] = Log::inactive_objects[i];
    }
    delete[]Log::inactive_objects;
    Log::inactive_objects = new_objects;
  }

  Log::inactive_objects[Log::numInactiveObjects++] = object;
}

/*-------------------------------------------------------------------------
  PERIODIC EVENTS

  There are a number of things that need to get done on a periodic basis,
  such as checking the amount of space used, seeing if it's time to roll
  files, and flushing idle log buffers.  Most of these tasks require having
  exclusive access to the back-end structures, which is controlled by the
  flush_thread.  Therefore, we will simply instruct the flush thread to
  execute a periodic_tasks() function once per period.  To ensure that the
  tasks are executed AT LEAST once each period, we'll register a call-back
  with the system and trigger the flush thread's condition variable.  To
  ensure that the tasks are executed AT MOST once per period, the flush
  thread will keep track of executions per period.
  -------------------------------------------------------------------------*/

/*-------------------------------------------------------------------------
  PeriodicWakeup

  This continuation is invoked each second to wake-up the flush thread,
  just in case it's sleeping on the job.
  -------------------------------------------------------------------------*/

struct PeriodicWakeup;
typedef int (PeriodicWakeup::*PeriodicWakeupHandler)(int, void *);
struct PeriodicWakeup : Continuation {

    int wakeup (int event, Event *e)
    {
        NOWARN_UNUSED (event); NOWARN_UNUSED (e);
        ink_cond_signal (&Log::flush_cond);
        return EVENT_CONT;
    }

    PeriodicWakeup () : Continuation (new_ProxyMutex())
    {
        SET_HANDLER ((PeriodicWakeupHandler)&PeriodicWakeup::wakeup);
    }
};


/*-------------------------------------------------------------------------
  Log::periodic_tasks

  This function contains all of the tasks that need to be done each
  PERIODIC_TASKS_INTERVAL seconds.
  -------------------------------------------------------------------------*/

void
Log::periodic_tasks(long time_now)
{
  // delete inactive objects
  //
  // we don't care if we miss an object that may be added to the set of
  // inactive objects just after we have read numInactiveObjects and found
  // it to be zero; we will get a chance to delete it next time
  //

  Debug("log-api-mutex", "entering Log::periodic_tasks");
  if (numInactiveObjects) {
    ink_mutex_acquire(Log::config->log_object_manager._APImutex);
    Debug("log-api-mutex", "Log::periodic_tasks acquired api mutex");
    Debug("log-periodic", "Deleting inactive_objects");
    for (size_t i = 0; i < numInactiveObjects; i++) {
      delete inactive_objects[i];
    }
    numInactiveObjects = 0;
    ink_mutex_release(Log::config->log_object_manager._APImutex);
    Debug("log-api-mutex", "Log::periodic_tasks released api mutex");
  }

  if (logging_mode_changed || Log::config->reconfiguration_needed) {
    Debug("log-config", "Performing reconfiguration, init status = %d", init_status);

    if (logging_mode_changed) {
      int val = (int) LOG_ConfigReadInteger("proxy.config.log.logging_enabled");

      if (val<LOG_NOTHING || val> FULL_LOGGING) {
        logging_mode = FULL_LOGGING;
        Warning("proxy.config.log.logging_enabled has an invalid " "value setting it to %d", logging_mode);
      } else {
        logging_mode = (LoggingMode) val;
      }
      logging_mode_changed = false;
    }
    // even if we are disabling logging, we call change configuration
    // so that log objects are flushed
    //
    change_configuration();
  } else if (logging_mode > LOG_NOTHING || config->collation_mode == LogConfig::COLLATION_HOST ||
             config->has_api_objects()) {
    Debug("log-periodic", "Performing periodic tasks");

    // Check if space is ok and update the space used
    //
    if (config->space_is_short() || time_now % config->space_used_frequency == 0) {
      Log::config->update_space_used();
    }
    // See if there are any buffers that have expired
    //
    Log::config->log_object_manager.check_buffer_expiration(time_now);
    if (error_log) {
      error_log->check_buffer_expiration(time_now);
    }
    // Check if we received a request to roll, and roll if so, otherwise
    // give objects a chance to roll if they need to
    //
    int num_rolled = 0;

    if (Log::config->roll_log_files_now) {
      if (error_log) {
        num_rolled += error_log->roll_files(time_now);
      }
      if (global_scrap_object) {
        num_rolled += global_scrap_object->roll_files(time_now);
      }
      num_rolled += Log::config->log_object_manager.roll_files(time_now);
      Log::config->roll_log_files_now = FALSE;
    } else {
      if (error_log) {
        num_rolled += error_log->roll_files(time_now);
      }
      if (global_scrap_object) {
        num_rolled += global_scrap_object->roll_files(time_now);
      }
      num_rolled += Log::config->log_object_manager.roll_files(time_now);
    }

  }
}

/*-------------------------------------------------------------------------
  MAIN INTERFACE
  -------------------------------------------------------------------------*/
struct LoggingFlushContinuation: public Continuation
{
  int mainEvent(int event, void *data)
  {
    NOWARN_UNUSED(event);
    NOWARN_UNUSED(data);
    Log::flush_thread_main(NULL);
    return 0;
  }

  LoggingFlushContinuation():Continuation(NULL)
  {
    SET_HANDLER(&LoggingFlushContinuation::mainEvent);
  }
};

struct LoggingCollateContinuation: public Continuation
{
  int mainEvent(int event, void *data)
  {
    NOWARN_UNUSED(event);
    NOWARN_UNUSED(data);
    Log::collate_thread_main(NULL);
    return 0;
  }

  LoggingCollateContinuation():Continuation(NULL)
  {
    SET_HANDLER(&LoggingCollateContinuation::mainEvent);
  }
};

/*-------------------------------------------------------------------------
  Log::init_fields

  Define the available logging fields.
  This used to be part of the init() function, but now is separate so that
  standalone programs that do not require more services (e.g., that do not
  need to read records.config) can just call init_fields.

  Note that the LogFields are added to the list with the copy flag false so
  that the LogFieldList destructor will reclaim this memory.
  -------------------------------------------------------------------------*/
void
Log::init_fields()
{
  if (init_status & FIELDS_INITIALIZED)
    return;

  LogField *field;

  //
  // Create a hash table that will be used to find the global field
  // objects from their symbol names in a rapid manner.
  //
  field_symbol_hash = ink_hash_table_create(InkHashTableKeyType_String);

  // client -> proxy fields
  field = NEW(new LogField("client_host_ip", "chi",
                           LogField::IP,
                           &LogAccess::marshal_client_host_ip,
                           &LogAccess::unmarshal_ip_to_str));
  global_field_list.add(field, false);
  ink_hash_table_insert(field_symbol_hash, "chi", field);

  field = NEW(new LogField("client_host_ip_hex", "chih",
                           LogField::IP,
                           &LogAccess::marshal_client_host_ip,
                           &LogAccess::unmarshal_ip_to_hex));
  global_field_list.add(field, false);
  ink_hash_table_insert(field_symbol_hash, "chih", field);

  field = NEW (new LogField ("client_auth_user_name", "caun",
                             LogField::STRING,
                             &LogAccess::marshal_client_auth_user_name,
                             &LogAccess::unmarshal_str));
  global_field_list.add (field, false);
  ink_hash_table_insert (field_symbol_hash, "caun", field);

  field = NEW(new LogField("client_req_timestamp_sec", "cqts",
                           LogField::sINT,
                           &LogAccess::marshal_client_req_timestamp_sec,
                           &LogAccess::unmarshal_int_to_str));
  global_field_list.add(field, false);
  ink_hash_table_insert(field_symbol_hash, "cqts", field);

  field = NEW(new LogField("client_req_timestamp_hex_sec", "cqth",
                           LogField::sINT,
                           &LogAccess::marshal_client_req_timestamp_sec,
                           &LogAccess::unmarshal_int_to_str_hex));
  global_field_list.add(field, false);
  ink_hash_table_insert(field_symbol_hash, "cqth", field);

  field = NEW(new LogField("client_req_timestamp_squid", "cqtq",
                           LogField::sINT,
                           &LogAccess::marshal_client_req_timestamp_sec,
                           &LogAccess::unmarshal_int_to_str));
  global_field_list.add(field, false);
  ink_hash_table_insert(field_symbol_hash, "cqtq", field);

  field = NEW(new LogField("client_req_timestamp_netscape", "cqtn",
                           LogField::sINT,
                           &LogAccess::marshal_client_req_timestamp_sec,
                           &LogAccess::unmarshal_int_to_str));
  global_field_list.add(field, false);
  ink_hash_table_insert(field_symbol_hash, "cqtn", field);

  field = NEW(new LogField("client_req_timestamp_date", "cqtd",
                           LogField::sINT,
                           &LogAccess::marshal_client_req_timestamp_sec,
                           &LogAccess::unmarshal_int_to_str));
  global_field_list.add(field, false);
  ink_hash_table_insert(field_symbol_hash, "cqtd", field);

  field = NEW(new LogField("client_req_timestamp_time", "cqtt",
                           LogField::sINT,
                           &LogAccess::marshal_client_req_timestamp_sec,
                           &LogAccess::unmarshal_int_to_str));
  global_field_list.add(field, false);
  ink_hash_table_insert(field_symbol_hash, "cqtt", field);

  field = NEW(new LogField("client_req_text", "cqtx",
                           LogField::STRING,
                           &LogAccess::marshal_client_req_text,
                           &LogAccess::unmarshal_http_text));
  global_field_list.add(field, false);
  ink_hash_table_insert(field_symbol_hash, "cqtx", field);

  field = NEW(new LogField("client_req_http_method", "cqhm",
                           LogField::STRING,
                           &LogAccess::marshal_client_req_http_method,
                           &LogAccess::unmarshal_str));
  global_field_list.add(field, false);
  ink_hash_table_insert(field_symbol_hash, "cqhm", field);

  field = NEW(new LogField("client_req_url", "cqu",
                           LogField::STRING,
                           &LogAccess::marshal_client_req_url,
                           &LogAccess::unmarshal_str));
  global_field_list.add(field, false);
  ink_hash_table_insert(field_symbol_hash, "cqu", field);

  field = NEW(new LogField("client_req_url_canonical", "cquc",
                           LogField::STRING,
                           &LogAccess::marshal_client_req_url_canon,
                           &LogAccess::unmarshal_str));
  global_field_list.add(field, false);
  ink_hash_table_insert(field_symbol_hash, "cquc", field);

  field = NEW(new LogField("client_req_unmapped_url_canonical", "cquuc",
                           LogField::STRING,
                           &LogAccess::marshal_client_req_unmapped_url_canon,
                           &LogAccess::unmarshal_str));
  global_field_list.add(field, false);
  ink_hash_table_insert(field_symbol_hash, "cquuc", field);

  field = NEW(new LogField("client_req_unmapped_url_path", "cquup",
                           LogField::STRING,
                           &LogAccess::marshal_client_req_unmapped_url_path,
                           &LogAccess::unmarshal_str));
  global_field_list.add(field, false);
  ink_hash_table_insert(field_symbol_hash, "cquup", field);

  field = NEW(new LogField("client_req_unmapped_url_host", "cquuh",
                           LogField::STRING,
                           &LogAccess::marshal_client_req_unmapped_url_host,
                           &LogAccess::unmarshal_str));
  global_field_list.add(field, false);
  ink_hash_table_insert(field_symbol_hash, "cquuh", field);

  field = NEW(new LogField("client_req_url_scheme", "cqus",
                           LogField::STRING,
                           &LogAccess::marshal_client_req_url_scheme,
                           &LogAccess::unmarshal_str));
  global_field_list.add(field, false);
  ink_hash_table_insert(field_symbol_hash, "cqus", field);

  field = NEW(new LogField("client_req_url_path", "cqup",
                           LogField::STRING,
                           &LogAccess::marshal_client_req_url_path,
                           &LogAccess::unmarshal_str));
  global_field_list.add(field, false);
  ink_hash_table_insert(field_symbol_hash, "cqup", field);

  field = NEW(new LogField("client_req_http_version", "cqhv",
                           LogField::dINT,
                           &LogAccess::marshal_client_req_http_version,
                           &LogAccess::unmarshal_http_version));
  global_field_list.add(field, false);
  ink_hash_table_insert(field_symbol_hash, "cqhv", field);

  field = NEW(new LogField("client_req_header_len", "cqhl",
                           LogField::sINT,
                           &LogAccess::marshal_client_req_header_len,
                           &LogAccess::unmarshal_int_to_str));
  global_field_list.add(field, false);
  ink_hash_table_insert(field_symbol_hash, "cqhl", field);

  field = NEW(new LogField("client_req_body_len", "cqbl",
                           LogField::sINT,
                           &LogAccess::marshal_client_req_body_len,
                           &LogAccess::unmarshal_int_to_str));
  global_field_list.add(field, false);
  ink_hash_table_insert(field_symbol_hash, "cqbl", field);

  Ptr<LogFieldAliasTable> finish_status_map = NEW(new LogFieldAliasTable);
  finish_status_map->init(N_LOG_FINISH_CODE_TYPES,
                          LOG_FINISH_FIN, "FIN",
                          LOG_FINISH_INTR, "INTR",
                          LOG_FINISH_TIMEOUT, "TIMEOUT");

  field = NEW(new LogField("client_finish_status_code", "cfsc",
                           LogField::sINT,
                           &LogAccess::marshal_client_finish_status_code,
                           &LogAccess::unmarshal_finish_status, (Ptr<LogFieldAliasMap>) finish_status_map));
  global_field_list.add(field, false);
  ink_hash_table_insert(field_symbol_hash, "cfsc", field);

  field = NEW(new LogField("client_gid", "cgid",
                           LogField::STRING,
                           &LogAccess::marshal_client_gid,
                           &LogAccess::unmarshal_str));
  global_field_list.add(field, false);
  ink_hash_table_insert(field_symbol_hash, "cgid", field);

  // proxy -> client fields
  field = NEW(new LogField("proxy_resp_content_type", "psct",
                           LogField::STRING,
                           &LogAccess::marshal_proxy_resp_content_type,
                           &LogAccess::unmarshal_str));
  global_field_list.add(field, false);
  ink_hash_table_insert(field_symbol_hash, "psct", field);

  field = NEW(new LogField("proxy_resp_squid_len", "psql",
                           LogField::sINT,
                           &LogAccess::marshal_proxy_resp_squid_len,
                           &LogAccess::unmarshal_int_to_str));
  global_field_list.add(field, false);
  ink_hash_table_insert(field_symbol_hash, "psql", field);

  field = NEW(new LogField("proxy_resp_content_len", "pscl",
                           LogField::sINT,
                           &LogAccess::marshal_proxy_resp_content_len,
                           &LogAccess::unmarshal_int_to_str));
  global_field_list.add(field, false);
  ink_hash_table_insert(field_symbol_hash, "pscl", field);

  field = NEW(new LogField("proxy_resp_content_len_hex", "psch",
                           LogField::sINT,
                           &LogAccess::marshal_proxy_resp_content_len,
                           &LogAccess::unmarshal_int_to_str_hex));
  global_field_list.add(field, false);
  ink_hash_table_insert(field_symbol_hash, "psch", field);

  field = NEW(new LogField("proxy_resp_status_code", "pssc",
                           LogField::sINT,
                           &LogAccess::marshal_proxy_resp_status_code,
                           &LogAccess::unmarshal_http_status));
  global_field_list.add(field, false);
  ink_hash_table_insert(field_symbol_hash, "pssc", field);

  field = NEW(new LogField("proxy_resp_header_len", "pshl",
                           LogField::sINT,
                           &LogAccess::marshal_proxy_resp_header_len,
                           &LogAccess::unmarshal_int_to_str));
  global_field_list.add(field, false);
  ink_hash_table_insert(field_symbol_hash, "pshl", field);

  field = NEW(new LogField("proxy_finish_status_code", "pfsc",
                           LogField::sINT,
                           &LogAccess::marshal_proxy_finish_status_code,
                           &LogAccess::unmarshal_finish_status, (Ptr<LogFieldAliasMap>) finish_status_map));
  global_field_list.add(field, false);
  ink_hash_table_insert(field_symbol_hash, "pfsc", field);

  Ptr<LogFieldAliasTable> cache_code_map = NEW(new LogFieldAliasTable);
  cache_code_map->init(62,
                       SQUID_LOG_EMPTY, "UNDEFINED",
                       SQUID_LOG_TCP_HIT, "TCP_HIT",
                       SQUID_LOG_TCP_DISK_HIT, "TCP_DISK_HIT",
                       SQUID_LOG_TCP_MEM_HIT, "TCP_MEM_HIT",
                       SQUID_LOG_TCP_MISS, "TCP_MISS",
                       SQUID_LOG_TCP_EXPIRED_MISS, "TCP_EXPIRED_MISS",
                       SQUID_LOG_TCP_REFRESH_HIT, "TCP_REFRESH_HIT",
                       SQUID_LOG_TCP_REF_FAIL_HIT, "TCP_REFRESH_FAIL_HIT",
                       SQUID_LOG_TCP_REFRESH_MISS, "TCP_REFRESH_MISS",
                       SQUID_LOG_TCP_CLIENT_REFRESH, "TCP_CLIENT_REFRESH_MISS",
                       SQUID_LOG_TCP_IMS_HIT, "TCP_IMS_HIT",
                       SQUID_LOG_TCP_IMS_MISS, "TCP_IMS_MISS",
                       SQUID_LOG_TCP_SWAPFAIL, "TCP_SWAPFAIL_MISS",
                       SQUID_LOG_TCP_DENIED, "TCP_DENIED",
                       SQUID_LOG_TCP_WEBFETCH_MISS, "TCP_WEBFETCH_MISS",
                       SQUID_LOG_TCP_SPIDER_BYPASS, "TCP_SPIDER_BYPASS",
                       SQUID_LOG_TCP_FUTURE_2, "TCP_FUTURE_2",
                       SQUID_LOG_TCP_HIT_REDIRECT, "TCP_HIT_REDIRECT",
                       SQUID_LOG_TCP_MISS_REDIRECT, "TCP_MISS_REDIRECT",
                       SQUID_LOG_TCP_HIT_X_REDIRECT, "TCP_HIT_X_REDIRECT",
                       SQUID_LOG_TCP_MISS_X_REDIRECT, "TCP_MISS_X_REDIRECT",
                       SQUID_LOG_UDP_HIT, "UDP_HIT",
                       SQUID_LOG_UDP_WEAK_HIT, "UDP_WEAK_HIT",
                       SQUID_LOG_UDP_HIT_OBJ, "UDP_HIT_OBJ",
                       SQUID_LOG_UDP_MISS, "UDP_MISS",
                       SQUID_LOG_UDP_DENIED, "UDP_DENIED",
                       SQUID_LOG_UDP_INVALID, "UDP_INVALID",
                       SQUID_LOG_UDP_RELOADING, "UDP_RELOADING",
                       SQUID_LOG_UDP_FUTURE_1, "UDP_FUTURE_1",
                       SQUID_LOG_UDP_FUTURE_2, "UDP_FUTURE_2",
                       SQUID_LOG_ERR_READ_TIMEOUT, "ERR_READ_TIMEOUT",
                       SQUID_LOG_ERR_LIFETIME_EXP, "ERR_LIFETIME_EXP",
                       SQUID_LOG_ERR_NO_CLIENTS_BIG_OBJ, "ERR_NO_CLIENTS_BIG_OBJ",
                       SQUID_LOG_ERR_READ_ERROR, "ERR_READ_ERROR",
                       SQUID_LOG_ERR_CLIENT_ABORT, "ERR_CLIENT_ABORT",
                       SQUID_LOG_ERR_CONNECT_FAIL, "ERR_CONNECT_FAIL",
                       SQUID_LOG_ERR_INVALID_REQ, "ERR_INVALID_REQ",
                       SQUID_LOG_ERR_UNSUP_REQ, "ERR_UNSUP_REQ",
                       SQUID_LOG_ERR_INVALID_URL, "ERR_INVALID_URL",
                       SQUID_LOG_ERR_NO_FDS, "ERR_NO_FDS",
                       SQUID_LOG_ERR_DNS_FAIL, "ERR_DNS_FAIL",
                       SQUID_LOG_ERR_NOT_IMPLEMENTED, "ERR_NOT_IMPLEMENTED",
                       SQUID_LOG_ERR_CANNOT_FETCH, "ERR_CANNOT_FETCH",
                       SQUID_LOG_ERR_NO_RELAY, "ERR_NO_RELAY",
                       SQUID_LOG_ERR_DISK_IO, "ERR_DISK_IO",
                       SQUID_LOG_ERR_ZERO_SIZE_OBJECT, "ERR_ZERO_SIZE_OBJECT",
                       SQUID_LOG_ERR_PROXY_DENIED, "ERR_PROXY_DENIED",
                       SQUID_LOG_ERR_WEBFETCH_DETECTED, "ERR_WEBFETCH_DETECTED",
                       SQUID_LOG_ERR_FUTURE_1, "ERR_FUTURE_1",
                       SQUID_LOG_ERR_SPIDER_MEMBER_ABORTED, "ERR_SPIDER_MEMBER_ABORTED",
                       SQUID_LOG_ERR_SPIDER_PARENTAL_CONTROL_RESTRICTION, "ERR_SPIDER_PARENTAL_CONTROL_RESTRICTION",
                       SQUID_LOG_ERR_SPIDER_UNSUPPORTED_HTTP_VERSION, "ERR_SPIDER_UNSUPPORTED_HTTP_VERSION",
                       SQUID_LOG_ERR_SPIDER_UIF, "ERR_SPIDER_UIF",
                       SQUID_LOG_ERR_SPIDER_FUTURE_USE_1, "ERR_SPIDER_FUTURE_USE_1",
                       SQUID_LOG_ERR_SPIDER_TIMEOUT_WHILE_PASSING, "ERR_SPIDER_TIMEOUT_WHILE_PASSING",
                       SQUID_LOG_ERR_SPIDER_TIMEOUT_WHILE_DRAINING, "ERR_SPIDER_TIMEOUT_WHILE_DRAINING",
                       SQUID_LOG_ERR_SPIDER_GENERAL_TIMEOUT, "ERR_SPIDER_GENERAL_TIMEOUT",
                       SQUID_LOG_ERR_SPIDER_CONNECT_FAILED, "ERR_SPIDER_CONNECT_FAILED",
                       SQUID_LOG_ERR_SPIDER_FUTURE_USE_2, "ERR_SPIDER_FUTURE_USE_2",
                       SQUID_LOG_ERR_SPIDER_NO_RESOURCES, "ERR_SPIDER_NO_RESOURCES",
                       SQUID_LOG_ERR_SPIDER_INTERNAL_ERROR, "ERR_SPIDER_INTERNAL_ERROR",
                       SQUID_LOG_ERR_SPIDER_INTERNAL_IO_ERROR, "ERR_SPIDER_INTERNAL_IO_ERROR",
                       SQUID_LOG_ERR_SPIDER_DNS_TEMP_ERROR, "ERR_SPIDER_DNS_TEMP_ERROR",
                       SQUID_LOG_ERR_SPIDER_DNS_HOST_NOT_FOUND, "ERR_SPIDER_DNS_HOST_NOT_FOUND",
                       SQUID_LOG_ERR_SPIDER_DNS_NO_ADDRESS, "ERR_SPIDER_DNS_NO_ADDRESS", SQUID_LOG_ERR_UNKNOWN, "ERR_UNKNOWN");

  field = NEW(new LogField("cache_result_code", "crc",
                           LogField::sINT,
                           &LogAccess::marshal_cache_result_code,
                           &LogAccess::unmarshal_cache_code, (Ptr<LogFieldAliasMap>) cache_code_map));
  global_field_list.add(field, false);
  ink_hash_table_insert(field_symbol_hash, "crc", field);

  field = NEW(new LogField("proxy_resp_origin_bytes", "prob",
                           LogField::sINT,
                           &LogAccess::marshal_proxy_resp_origin_bytes,
                           &LogAccess::unmarshal_int_to_str));
  global_field_list.add(field, false);
  ink_hash_table_insert(field_symbol_hash, "prob", field);

  field = NEW(new LogField("proxy_resp_cache_bytes", "prcb",
                           LogField::sINT,
                           &LogAccess::marshal_proxy_resp_cache_bytes,
                           &LogAccess::unmarshal_int_to_str));
  global_field_list.add(field, false);
  ink_hash_table_insert(field_symbol_hash, "prcb", field);

  // proxy -> server fields
  field = NEW(new LogField("proxy_req_header_len", "pqhl",
                           LogField::sINT,
                           &LogAccess::marshal_proxy_req_header_len,
                           &LogAccess::unmarshal_int_to_str));
  global_field_list.add(field, false);
  ink_hash_table_insert(field_symbol_hash, "pqhl", field);

  field = NEW(new LogField("proxy_req_body_len", "pqbl",
                           LogField::sINT,
                           &LogAccess::marshal_proxy_req_body_len,
                           &LogAccess::unmarshal_int_to_str));
  global_field_list.add(field, false);
  ink_hash_table_insert(field_symbol_hash, "pqbl", field);

  field = NEW(new LogField("proxy_req_server_name", "pqsn",
                           LogField::STRING,
                           &LogAccess::marshal_proxy_req_server_name,
                           &LogAccess::unmarshal_str));
  global_field_list.add(field, false);
  ink_hash_table_insert(field_symbol_hash, "pqsn", field);

  field = NEW(new LogField("proxy_req_server_ip", "pqsi",
                           LogField::IP,
                           &LogAccess::marshal_proxy_req_server_ip,
      &LogAccess::unmarshal_ip_to_str));
  global_field_list.add(field, false);
  ink_hash_table_insert(field_symbol_hash, "pqsi", field);

  Ptr<LogFieldAliasTable> hierarchy_map = NEW(new LogFieldAliasTable);
  hierarchy_map->init(36,
                      SQUID_HIER_EMPTY, "EMPTY",
                      SQUID_HIER_NONE, "NONE",
                      SQUID_HIER_DIRECT, "DIRECT",
                      SQUID_HIER_SIBLING_HIT, "SIBLING_HIT",
                      SQUID_HIER_PARENT_HIT, "PARENT_HIT",
                      SQUID_HIER_DEFAULT_PARENT, "DEFAULT_PARENT",
                      SQUID_HIER_SINGLE_PARENT, "SINGLE_PARENT",
                      SQUID_HIER_FIRST_UP_PARENT, "FIRST_UP_PARENT",
                      SQUID_HIER_NO_PARENT_DIRECT, "NO_PARENT_DIRECT",
                      SQUID_HIER_FIRST_PARENT_MISS, "FIRST_PARENT_MISS",
                      SQUID_HIER_LOCAL_IP_DIRECT, "LOCAL_IP_DIRECT",
                      SQUID_HIER_FIREWALL_IP_DIRECT, "FIREWALL_IP_DIRECT",
                      SQUID_HIER_NO_DIRECT_FAIL, "NO_DIRECT_FAIL",
                      SQUID_HIER_SOURCE_FASTEST, "SOURCE_FASTEST",
                      SQUID_HIER_SIBLING_UDP_HIT_OBJ, "SIBLING_UDP_HIT_OBJ",
                      SQUID_HIER_PARENT_UDP_HIT_OBJ, "PARENT_UDP_HIT_OBJ",
                      SQUID_HIER_PASSTHROUGH_PARENT, "PASSTHROUGH_PARENT",
                      SQUID_HIER_SSL_PARENT_MISS, "SSL_PARENT_MISS",
                      SQUID_HIER_INVALID_CODE, "INVALID_CODE",
                      SQUID_HIER_TIMEOUT_DIRECT, "TIMEOUT_DIRECT",
                      SQUID_HIER_TIMEOUT_SIBLING_HIT, "TIMEOUT_SIBLING_HIT",
                      SQUID_HIER_TIMEOUT_PARENT_HIT, "TIMEOUT_PARENT_HIT",
                      SQUID_HIER_TIMEOUT_DEFAULT_PARENT, "TIMEOUT_DEFAULT_PARENT",
                      SQUID_HIER_TIMEOUT_SINGLE_PARENT, "TIMEOUT_SINGLE_PARENT",
                      SQUID_HIER_TIMEOUT_FIRST_UP_PARENT, "TIMEOUT_FIRST_UP_PARENT",
                      SQUID_HIER_TIMEOUT_NO_PARENT_DIRECT, "TIMEOUT_NO_PARENT_DIRECT",
                      SQUID_HIER_TIMEOUT_FIRST_PARENT_MISS, "TIMEOUT_FIRST_PARENT_MISS",
                      SQUID_HIER_TIMEOUT_LOCAL_IP_DIRECT, "TIMEOUT_LOCAL_IP_DIRECT",
                      SQUID_HIER_TIMEOUT_FIREWALL_IP_DIRECT, "TIMEOUT_FIREWALL_IP_DIRECT",
                      SQUID_HIER_TIMEOUT_NO_DIRECT_FAIL, "TIMEOUT_NO_DIRECT_FAIL",
                      SQUID_HIER_TIMEOUT_SOURCE_FASTEST, "TIMEOUT_SOURCE_FASTEST",
                      SQUID_HIER_TIMEOUT_SIBLING_UDP_HIT_OBJ, "TIMEOUT_SIBLING_UDP_HIT_OBJ",
                      SQUID_HIER_TIMEOUT_PARENT_UDP_HIT_OBJ, "TIMEOUT_PARENT_UDP_HIT_OBJ",
                      SQUID_HIER_TIMEOUT_PASSTHROUGH_PARENT, "TIMEOUT_PASSTHROUGH_PARENT",
                      SQUID_HIER_TIMEOUT_TIMEOUT_SSL_PARENT_MISS, "TIMEOUT_TIMEOUT_SSL_PARENT_MISS",
                      SQUID_HIER_INVALID_ASSIGNED_CODE, "INVALID_ASSIGNED_CODE");

  field = NEW(new LogField("proxy_hierarchy_route", "phr",
                           LogField::sINT,
                           &LogAccess::marshal_proxy_hierarchy_route,
                           &LogAccess::unmarshal_hierarchy, (Ptr<LogFieldAliasMap>) hierarchy_map));
  global_field_list.add(field, false);
  ink_hash_table_insert(field_symbol_hash, "phr", field);

  field = NEW(new LogField("proxy_host_name", "phn",
                           LogField::STRING,
                           &LogAccess::marshal_proxy_host_name,
                           &LogAccess::unmarshal_str));
  global_field_list.add(field, false);
  ink_hash_table_insert(field_symbol_hash, "phn", field);

  field = NEW(new LogField("proxy_host_ip", "phi",
                           LogField::IP,
                           &LogAccess::marshal_proxy_host_ip,
                           &LogAccess::unmarshal_ip_to_str));
  global_field_list.add(field, false);
  ink_hash_table_insert(field_symbol_hash, "phi", field);

  // X-WAID
  field = NEW(new LogField("accelerator_id", "xid",
                           LogField::STRING,
                           &LogAccess::marshal_client_accelerator_id,
                           &LogAccess::unmarshal_str));
  global_field_list.add(field, false);
  ink_hash_table_insert(field_symbol_hash, "xid", field);
  // X-WAID

  // server -> proxy fields

  field = NEW(new LogField("server_host_ip", "shi",
                           LogField::IP,
                           &LogAccess::marshal_server_host_ip,
      &LogAccess::unmarshal_ip_to_str));

  global_field_list.add(field, false);
  ink_hash_table_insert(field_symbol_hash, "shi", field);

  field = NEW(new LogField("server_host_name", "shn",
                           LogField::STRING, &LogAccess::marshal_server_host_name, &LogAccess::unmarshal_str));
  global_field_list.add(field, false);
  ink_hash_table_insert(field_symbol_hash, "shn", field);

  field = NEW(new LogField("server_resp_status_code", "sssc",
                           LogField::sINT,
                           &LogAccess::marshal_server_resp_status_code, &LogAccess::unmarshal_http_status));
  global_field_list.add(field, false);
  ink_hash_table_insert(field_symbol_hash, "sssc", field);

  field = NEW(new LogField("server_resp_content_len", "sscl",
                           LogField::sINT,
                           &LogAccess::marshal_server_resp_content_len, &LogAccess::unmarshal_int_to_str));
  global_field_list.add(field, false);
  ink_hash_table_insert(field_symbol_hash, "sscl", field);

  field = NEW(new LogField("server_resp_header_len", "sshl",
                           LogField::sINT,
                           &LogAccess::marshal_server_resp_header_len, &LogAccess::unmarshal_int_to_str));
  global_field_list.add(field, false);
  ink_hash_table_insert(field_symbol_hash, "sshl", field);

  field = NEW(new LogField("server_resp_http_version", "sshv",
                           LogField::dINT,
                           &LogAccess::marshal_server_resp_http_version, &LogAccess::unmarshal_http_version));
  global_field_list.add(field, false);
  ink_hash_table_insert(field_symbol_hash, "sshv", field);

  field = NEW(new LogField("cached_resp_status_code", "csssc",
                           LogField::sINT,
                           &LogAccess::marshal_cache_resp_status_code, &LogAccess::unmarshal_http_status));
  global_field_list.add(field, false);
  ink_hash_table_insert(field_symbol_hash, "csssc", field);

  field = NEW(new LogField("cached_resp_content_len", "csscl",
                           LogField::sINT,
                           &LogAccess::marshal_cache_resp_content_len, &LogAccess::unmarshal_int_to_str));
  global_field_list.add(field, false);
  ink_hash_table_insert(field_symbol_hash, "csscl", field);

  field = NEW(new LogField("cached_resp_header_len", "csshl",
                           LogField::sINT,
                           &LogAccess::marshal_cache_resp_header_len, &LogAccess::unmarshal_int_to_str));
  global_field_list.add(field, false);
  ink_hash_table_insert(field_symbol_hash, "csshl", field);

  field = NEW(new LogField("cached_resp_http_version", "csshv",
                           LogField::dINT,
                           &LogAccess::marshal_cache_resp_http_version, &LogAccess::unmarshal_http_version));
  global_field_list.add(field, false);
  ink_hash_table_insert(field_symbol_hash, "csshv", field);

  field = NEW(new LogField("client_retry_after_time", "crat",
                           LogField::sINT,
                           &LogAccess::marshal_client_retry_after_time, &LogAccess::unmarshal_int_to_str));
  global_field_list.add(field, false);
  ink_hash_table_insert(field_symbol_hash, "crat", field);

  // cache write fields

  Ptr<LogFieldAliasTable> cache_write_code_map = NEW(new LogFieldAliasTable);
  cache_write_code_map->init(N_LOG_CACHE_WRITE_TYPES,
                             LOG_CACHE_WRITE_NONE, "-",
                             LOG_CACHE_WRITE_LOCK_MISSED, "WL_MISS",
                             LOG_CACHE_WRITE_LOCK_ABORTED, "INTR",
                             LOG_CACHE_WRITE_ERROR, "ERR", LOG_CACHE_WRITE_COMPLETE, "FIN");
  field = NEW(new LogField("cache_write_result", "cwr",
                           LogField::sINT,
                           &LogAccess::marshal_cache_write_code,
                           &LogAccess::unmarshal_cache_write_code, (Ptr<LogFieldAliasMap>) cache_write_code_map));
  global_field_list.add(field, false);
  ink_hash_table_insert(field_symbol_hash, "cwr", field);

  field = NEW(new LogField("cache_write_transform_result", "cwtr",
                           LogField::sINT,
                           &LogAccess::marshal_cache_write_transform_code,
                           &LogAccess::unmarshal_cache_write_code, (Ptr<LogFieldAliasMap>) cache_write_code_map));
  global_field_list.add(field, false);
  ink_hash_table_insert(field_symbol_hash, "cwtr", field);

  // other fields

  field = NEW(new LogField("transfer_time_ms", "ttms",
                           LogField::sINT, &LogAccess::marshal_transfer_time_ms, &LogAccess::unmarshal_int_to_str));
  global_field_list.add(field, false);
  ink_hash_table_insert(field_symbol_hash, "ttms", field);

  field = NEW(new LogField("transfer_time_ms_hex", "ttmh",
                           LogField::sINT, &LogAccess::marshal_transfer_time_ms, &LogAccess::unmarshal_int_to_str_hex));
  global_field_list.add(field, false);
  ink_hash_table_insert(field_symbol_hash, "ttmh", field);

  field = NEW(new LogField("transfer_time_ms_fractional", "ttmsf",
                           LogField::sINT, &LogAccess::marshal_transfer_time_ms, &LogAccess::unmarshal_ttmsf));
  global_field_list.add(field, false);
  ink_hash_table_insert(field_symbol_hash, "ttmsf", field);

  field = NEW(new LogField("transfer_time_sec", "tts",
                           LogField::sINT, &LogAccess::marshal_transfer_time_s, &LogAccess::unmarshal_int_to_str));
  global_field_list.add(field, false);
  ink_hash_table_insert(field_symbol_hash, "tts", field);

  field = NEW(new LogField("file_size", "fsiz",
                           LogField::sINT, &LogAccess::marshal_file_size, &LogAccess::unmarshal_int_to_str));
  global_field_list.add(field, false);
  ink_hash_table_insert(field_symbol_hash, "fsiz", field);

  Ptr<LogFieldAliasTable> entry_type_map = NEW(new LogFieldAliasTable);
  entry_type_map->init(N_LOG_ENTRY_TYPES,
                       LOG_ENTRY_HTTP, "LOG_ENTRY_HTTP",
                       LOG_ENTRY_ICP, "LOG_ENTRY_ICP");
  field = NEW(new LogField("log_entry_type", "etype",
                           LogField::sINT,
                           &LogAccess::marshal_entry_type,
                           &LogAccess::unmarshal_entry_type, (Ptr<LogFieldAliasMap>) entry_type_map));
  global_field_list.add(field, false);
  ink_hash_table_insert(field_symbol_hash, "etype", field);

  init_status |= FIELDS_INITIALIZED;
}


/*-------------------------------------------------------------------------

  Initialization functions

  -------------------------------------------------------------------------*/
int
Log::handle_logging_mode_change(const char *name, RecDataT data_type, RecData data, void *cookie)
{
  NOWARN_UNUSED(name);
  NOWARN_UNUSED(data_type);
  NOWARN_UNUSED(data);
  NOWARN_UNUSED(cookie);
  Debug("log-config", "Enabled status changed");
  logging_mode_changed = true;
  return 0;
}

void
Log::init(int flags)
{
  maxInactiveObjects = LOG_OBJECT_ARRAY_DELTA;
  numInactiveObjects = 0;
  inactive_objects = new LogObject*[maxInactiveObjects];

  collation_accept_file_descriptor = NO_FD;

  // store the configuration flags
  //
  config_flags = flags;

  // create the configuration object
  //
  config = NEW (new LogConfig);
  ink_assert (config != NULL);

  // set the logging_mode and read config variables if needed
  //
  if (config_flags & LOGCAT) {
    logging_mode = LOG_NOTHING;
  }
  else {
    log_rsb = RecAllocateRawStatBlock((int) log_stat_count);
    LogConfig::register_stat_callbacks();

    config->read_configuration_variables();
    collation_port = config->collation_port;

    if (config_flags & STANDALONE_COLLATOR) {
      logging_mode = LOG_TRANSACTIONS_ONLY;
    }
    else {
      int val = (int) LOG_ConfigReadInteger("proxy.config.log.logging_enabled");
      if (val < LOG_NOTHING || val > FULL_LOGGING) {
        logging_mode = FULL_LOGGING;
        Warning("proxy.config.log.logging_enabled has an invalid "
          "value, setting it to %d", logging_mode);
      }
      else {
        logging_mode = (LoggingMode) val;
      }
    }
  }

  // if remote management is enabled, do all necessary initialization to
  // be able to handle a logging mode change
  //
  if (!(config_flags & NO_REMOTE_MANAGEMENT)) {

    LOG_RegisterConfigUpdateFunc("proxy.config.log.logging_enabled",
        &Log::handle_logging_mode_change, NULL);

    LOG_RegisterLocalUpdateFunc("proxy.local.log.collation_mode",
        &Log::handle_logging_mode_change, NULL);

    // we must create the flush thread since it takes care of the
    // periodic events (should this behavior be reversed ?)
    //
    create_threads();

#ifndef INK_SINGLE_THREADED
    eventProcessor.schedule_every(NEW (new PeriodicWakeup()), HRTIME_SECOND,
        ET_CALL);
#endif
    init_status |= PERIODIC_WAKEUP_SCHEDULED;

    // Clear any stat values that need to be reset on startup
    //
    LOG_CLEAR_DYN_STAT( log_stat_log_files_open_stat);
  }

  if (config_flags & LOGCAT) {
    init_fields();
  }
  else {
    Debug("log-config", "Log::init(): logging_mode = %d "
        "init status = %d", logging_mode, init_status);
    init_when_enabled();
    if (config_flags & STANDALONE_COLLATOR) {
      config->collation_mode = LogConfig::COLLATION_HOST;
    }
    config->init();
  }
}

void
Log::init_when_enabled()
{
  init_fields();

  if (!(init_status & FULLY_INITIALIZED)) {

    if (!(config_flags & STANDALONE_COLLATOR)) {
      // register callbacks
      //
      if (!(config_flags & NO_REMOTE_MANAGEMENT)) {
        LogConfig::register_config_callbacks();
      }

      LogConfig::register_mgmt_callbacks();
    }
    // setup global scrap object
    //
    global_scrap_format = NEW(new LogFormat(TEXT_LOG));
    global_scrap_object =
      NEW(new LogObject(global_scrap_format,
                        Log::config->logfile_dir,
                        "scrapfile.log",
                        BINARY_LOG, NULL,
                        Log::config->rolling_enabled,
                        Log::config->rolling_interval_sec,
                        Log::config->rolling_offset_hr, Log::config->rolling_size_mb));

    // create the flush thread and the collation thread
    //
    create_threads();

    // schedule periodic wakeup
    // ToDo: Why was this removed??
    //
    //      eventProcessor.schedule_every (NEW (new PeriodicWakeup()), HRTIME_SECOND, ET_CALL);

    init_status |= FULLY_INITIALIZED;
  }

  Note("logging initialized[%d], logging_mode = %d", init_status, logging_mode);
  if (is_debug_tag_set("log-config")) {
    config->display();
  }
}

void
Log::create_threads()
{
  if (!(init_status & THREADS_CREATED)) {
    // start the flush thread
    //
    // no need for the conditional var since it will be relying on
    // on the event system.
    ink_mutex_init(&flush_mutex, "Flush thread mutex");
    ink_cond_init(&flush_cond);
    Continuation *flush_continuation = NEW(new LoggingFlushContinuation);
    Event *flush_event = eventProcessor.spawn_thread(flush_continuation, "[LOGGING]");
    flush_thread = flush_event->ethread->tid;

#if !defined(IOCORE_LOG_COLLATION)
    // start the collation thread if we are not using iocore log collation
    //
    // for the collation thread, we start one on each machine (done here)
    // and then block it on a mutex variable that is only released (from
    // LogConfig) on the machine configured to be the collation server.
    // When it is no longer needed (say after a reconfiguration), it will
    // be blocked again on it's condition variable.  This makes it easy to
    // start and stop the collation thread, and assumes that there is not
    // much overhead associated with keeping an ink_thread blocked on a
    // condition variable.
    //
    ink_mutex_init(&collate_mutex, "Collate thread mutex");
    ink_cond_init(&collate_cond);
    Continuation *collate_continuation = NEW(new LoggingCollateContinuation);
    Event *collate_event = eventProcessor.spawn_thread(collate_continuation);
    collate_thread = collate_event->ethread->tid;
#endif
    init_status |= THREADS_CREATED;
  }
}

/*-------------------------------------------------------------------------
  Log::access

  Make an entry in the access log for the data supplied by the given
  LogAccess object.
  -------------------------------------------------------------------------*/

int
Log::access(LogAccess * lad)
{
  // See if transaction logging is disabled
  //
  if (!transaction_logging_enabled()) {
    return Log::SKIP;
  }

  ink_assert(init_status & FULLY_INITIALIZED);
  ink_assert(lad != NULL);

  int ret;
  static long sample = 1;
  long this_sample;

  // See if we're sampling and it is not time for another sample
  //
  if (Log::config->sampling_frequency > 1) {
    this_sample = sample++;
    if (this_sample && this_sample % Log::config->sampling_frequency) {
      Debug("log", "sampling, skipping this entry ...");
      ret = Log::SKIP;
      goto done;
    } else {
      Debug("log", "sampling, LOGGING this entry ...");
      sample = 1;
    }
  }

  if (Log::config->log_object_manager.get_num_objects() == 0) {
    Debug("log", "no log objects, skipping this entry ...");
    ret = Log::SKIP;
    goto done;
  }
  // initialize this LogAccess object and proccess
  //
  lad->init();
  ret = config->log_object_manager.log(lad);

done:
  return ret;
}

/*-------------------------------------------------------------------------
  Log::error

  Make an entry into the current error log.  For convenience, it is given in
  both variable argument (format, ...) and stdarg (format, va_list) forms.

  Note that Log::error could call Log::va_error after calling va_start
  so that va_error handles the statistics update. However, to make
  Log::error slightly more efficient this is not the case. The
  downside is that one has to be careful to update both functions if
  need be.
  -------------------------------------------------------------------------*/

int
Log::error(const char *format, ...)
{
  int ret_val = Log::SKIP;

  if (error_log) {
    ink_debug_assert(format != NULL);
    va_list ap;
    va_start(ap, format);
    ret_val = error_log->va_write(format, ap);
    va_end(ap);

    if (ret_val == Log::LOG_OK) {
      ProxyMutex *mutex = this_ethread()->mutex;
      LOG_INCREMENT_DYN_STAT(log_stat_event_log_error_stat);
    }
  }
  return ret_val;
}

int
Log::va_error(char *format, va_list ap)
{
  int ret_val = Log::SKIP;

  if (error_log) {
    ink_debug_assert(format != NULL);
    ret_val = error_log->va_write(format, ap);

    if (ret_val == Log::LOG_OK) {
      ProxyMutex *mutex = this_ethread()->mutex;
      LOG_INCREMENT_DYN_STAT(log_stat_event_log_error_stat);
    }
  }
  return ret_val;
}

/*-------------------------------------------------------------------------
  Log::flush_thread_main

  This function defines the functionality of the logging flush thread,
  whose purpose is to consume LogBuffer objects from the
  global_buffer_full_list, process them, and destroy them.
  -------------------------------------------------------------------------*/

void *
Log::flush_thread_main(void *args)
{
  NOWARN_UNUSED (args);
  time_t now, last_time = 0;
  size_t buffers_flushed;

  Debug("log-flush", "Log flush thread is alive ...");

  while (true) {
    buffers_flushed = 0;

    buffers_flushed = config->log_object_manager.flush_buffers();

    if (error_log)
      buffers_flushed += error_log->flush_buffers();

    // config->increment_space_used(bytes_to_disk);
    // TODO: the bytes_to_disk should be set to Log

    Debug("log-flush","%zu buffers flushed this round", buffers_flushed);

    // Time to work on periodic events??
    //
    now = time(NULL);
    if (now > last_time) {
      if ((now % PERIODIC_TASKS_INTERVAL) == 0) {
        Debug("log-flush", "periodic tasks for %" PRId64, (int64_t)now);
        periodic_tasks(now);
      }
      last_time = (now = time(NULL));
    }
    // wait for more work; a spurious wake-up is ok since we'll just
    // check the queue and find there is nothing to do, then wait
    // again.
    //
    ink_cond_wait (&flush_cond, &flush_mutex);
  }
  /* NOTREACHED */
  return NULL;
}

/*-------------------------------------------------------------------------
  Log::collate_thread_main

  This function defines the functionality of the log collation thread,
  whose purpose is to collate log buffers from other nodes.
  -------------------------------------------------------------------------*/

void *
Log::collate_thread_main(void *args)
{
  NOWARN_UNUSED(args);
  LogSock *sock;
  LogBufferHeader *header;
  LogFormat *format;
  LogObject *obj;
  int bytes_read;
  int sock_id;
  int new_client;


  Debug("log-thread", "Log collation thread is alive ...");

  while (true) {
    ink_assert(Log::config != NULL);

    // wait on the collation condition variable until we're sure that
    // we're a collation host.  The while loop guards against spurious
    // wake-ups.
    //
    while (!Log::config->am_collation_host()) {
      ink_cond_wait(&collate_cond, &collate_mutex);
    }

    // Ok, at this point we know we're a log collation host, so get to
    // work.  We still need to keep checking whether we're a collation
    // host to account for a reconfiguration.
    //
    Debug("log-sock", "collation thread starting, creating LogSock");
    sock = NEW(new LogSock(LogSock::LS_CONST_CLUSTER_MAX_MACHINES));
    ink_assert(sock != NULL);

    if (sock->listen(Log::config->collation_port) != 0) {
      LogUtils::manager_alarm(LogUtils::LOG_ALARM_ERROR,
                              "Collation server error; could not listen on port %d", Log::config->collation_port);
      Warning("Collation server error; could not listen on port %d", Log::config->collation_port);
      delete sock;
      //
      // go to sleep ...
      //
      ink_cond_wait(&collate_cond, &collate_mutex);
      continue;
    }

    while (true) {

      if (!Log::config->am_collation_host()) {
        break;
      }

      if (sock->pending_connect(0)) {
        Debug("log-sock", "pending connection ...");
        if ((new_client = sock->accept()) < 0) {
          Debug("log-sock", "error accepting new collation client");
        } else {
          Debug("log-sock", "connection %d accepted", new_client);
          if (!sock->authorized_client(new_client, Log::config->collation_secret)) {
            Warning("Unauthorized client connecting to " "log collation port; connection refused.");
            sock->close(new_client);
          }
        }
      }

      sock->check_connections();

      if (!sock->pending_message_any(&sock_id, 0)) {
        continue;
      }

      Debug("log-sock", "pending message ...");
      header = (LogBufferHeader *) sock->read_alloc(sock_id, &bytes_read);
      if (!header) {
        Debug("log-sock", "Error reading LogBuffer from collation client");
        continue;
      }

      if (header->version != LOG_SEGMENT_VERSION) {
        Note("Invalid LogBuffer received; invalid version - buffer = %u, current = %u",
             header->version, LOG_SEGMENT_VERSION);
        delete[]header;
        continue;
      }

      Debug("log-sock", "message accepted, size = %d", bytes_read);
      LOG_SUM_GLOBAL_DYN_STAT(log_stat_bytes_received_from_network_stat, bytes_read);

      obj = match_logobject(header);
      if (!obj) {
        Note("LogObject not found with fieldlist id; " "writing LogBuffer to scrap file");
        obj = global_scrap_object;
      }

      format = obj->m_format;
      Debug("log-sock", "Using format '%s'", format->name());

      delete[]header;
    }

    Debug("log", "no longer collation host, deleting LogSock");
    delete sock;
  }
  /* NOTREACHED */
  return NULL;
}

/*-------------------------------------------------------------------------
  Log::match_logobject

  This routine matches the given buffer with the local list of LogObjects.
  If a match cannot be found, then we'll try to construct a local LogObject
  using the information provided in the header.  If all else fails, we
  return NULL.
  -------------------------------------------------------------------------*/

LogObject *
Log::match_logobject(LogBufferHeader * header)
{
  if (!header)
    return NULL;

  LogObject *obj;
  obj = Log::config->log_object_manager.get_object_with_signature(header->log_object_signature);

  if (!obj) {
    // object does not exist yet, create it
    //
    LogFormat *fmt = NEW(new LogFormat("__collation_format__",
                                       header->fmt_fieldlist(),
                                       header->fmt_printf()));
    if (fmt->valid()) {
      LogFileFormat file_format =
        header->log_object_flags & LogObject::BINARY ? BINARY_LOG :
        (header->log_object_flags & LogObject::WRITES_TO_PIPE ? ASCII_PIPE : ASCII_LOG);

      obj = NEW(new LogObject(fmt, Log::config->logfile_dir,
                              header->log_filename(), file_format, NULL,
                              Log::config->rolling_enabled,
                              Log::config->rolling_interval_sec,
                              Log::config->rolling_offset_hr, Log::config->rolling_size_mb));

      obj->set_remote_flag();

      if (Log::config->log_object_manager.manage_object(obj)) {
        // object manager can't solve filename conflicts
        // delete the object and return NULL
        //
        delete obj;
        obj = NULL;
      }
    }
  }
  return obj;
}
