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
#include "ts/ink_platform.h"
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
#include "ts/SimpleTokenizer.h"

#include "ts/ink_apidefs.h"

#define PERIODIC_TASKS_INTERVAL_FALLBACK 5

// Log global objects
inkcoreapi LogObject *Log::error_log = nullptr;
LogFieldList Log::global_field_list;
LogFormat *Log::global_scrap_format = nullptr;
LogObject *Log::global_scrap_object = nullptr;
Log::LoggingMode Log::logging_mode  = LOG_MODE_NONE;

// Flush thread stuff
EventNotify *Log::preproc_notify;
EventNotify *Log::flush_notify;
InkAtomicList *Log::flush_data_list;

// Collate thread stuff
EventNotify Log::collate_notify;
ink_thread Log::collate_thread;
int Log::collation_accept_file_descriptor;
int Log::collation_preproc_threads;
int Log::collation_port;

// Log private objects
int Log::init_status                  = 0;
int Log::config_flags                 = 0;
bool Log::logging_mode_changed        = false;
uint32_t Log::periodic_tasks_interval = PERIODIC_TASKS_INTERVAL_FALLBACK;

// Hash table for LogField symbols
InkHashTable *Log::field_symbol_hash = nullptr;

RecRawStatBlock *log_rsb;

/*-------------------------------------------------------------------------
  Log::change_configuration

  This routine is invoked when the current LogConfig object says it needs
  to be changed (as the result of a manager callback).
  -------------------------------------------------------------------------*/

LogConfig *Log::config       = nullptr;
static unsigned log_configid = 0;

// Downcast from a Ptr<LogFieldAliasTable> to a Ptr<LogFieldAliasMap>.
static Ptr<LogFieldAliasMap>
make_alias_map(Ptr<LogFieldAliasTable> &table)
{
  return make_ptr(static_cast<LogFieldAliasMap *>(table.get()));
}

void
Log::change_configuration()
{
  LogConfig *prev       = Log::config;
  LogConfig *new_config = nullptr;

  Debug("log-config", "Changing configuration ...");

  new_config = new LogConfig;
  ink_assert(new_config != nullptr);
  new_config->read_configuration_variables();

  // grab the _APImutex so we can transfer the api objects to
  // the new config
  //
  ink_mutex_acquire(prev->log_object_manager._APImutex);
  Debug("log-api-mutex", "Log::change_configuration acquired api mutex");

  new_config->init(Log::config);

  // Make the new LogConfig active.
  ink_atomic_swap(&Log::config, new_config);

  // XXX There is a race condition with API objects. If TSTextLogObjectCreate()
  // is called before the Log::config swap, then it will be blocked on the lock
  // on the *old* LogConfig and register it's LogObject with that manager. If
  // this happens, then the new TextLogObject will be immediately lost. Traffic
  // Server would crash the next time the plugin referenced the freed object.

  ink_mutex_release(prev->log_object_manager._APImutex);
  Debug("log-api-mutex", "Log::change_configuration released api mutex");

  // Register the new config in the config processor; the old one will now be scheduled for a
  // future deletion. We don't need to do anything magical with refcounts, since the
  // configProcessor will keep a reference count, and drop it when the deletion is scheduled.
  configProcessor.set(log_configid, new_config);

  // If we replaced the logging configuration, flush any log
  // objects that weren't transferred to the new config ...
  prev->log_object_manager.flush_all_objects();

  Debug("log-config", "... new configuration in place");
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
using PeriodicWakeupHandler = int (PeriodicWakeup::*)(int, void *);
struct PeriodicWakeup : Continuation {
  int m_preproc_threads;
  int m_flush_threads;

  int
  wakeup(int /* event ATS_UNUSED */, Event * /* e ATS_UNUSED */)
  {
    for (int i = 0; i < m_preproc_threads; i++) {
      Log::preproc_notify[i].signal();
    }
    for (int i = 0; i < m_flush_threads; i++) {
      Log::flush_notify[i].signal();
    }
    return EVENT_CONT;
  }

  PeriodicWakeup(int preproc_threads, int flush_threads)
    : Continuation(new_ProxyMutex()), m_preproc_threads(preproc_threads), m_flush_threads(flush_threads)
  {
    SET_HANDLER((PeriodicWakeupHandler)&PeriodicWakeup::wakeup);
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
  Debug("log-api-mutex", "entering Log::periodic_tasks");

  if (logging_mode_changed || Log::config->reconfiguration_needed) {
    Debug("log-config", "Performing reconfiguration, init status = %d", init_status);

    if (logging_mode_changed) {
      int val = (int)REC_ConfigReadInteger("proxy.config.log.logging_enabled");

      if (val < LOG_MODE_NONE || val > LOG_MODE_FULL) {
        logging_mode = LOG_MODE_FULL;
        Warning("proxy.config.log.logging_enabled has an invalid "
                "value setting it to %d",
                logging_mode);
      } else {
        logging_mode = (LoggingMode)val;
      }
      logging_mode_changed = false;
    }
    // even if we are disabling logging, we call change configuration
    // so that log objects are flushed
    //
    change_configuration();
  } else if (logging_mode > LOG_MODE_NONE || config->collation_mode == Log::COLLATION_HOST || config->has_api_objects()) {
    Debug("log-periodic", "Performing periodic tasks");
    Debug("log-periodic", "Periodic task interval = %d", periodic_tasks_interval);

    // Check if space is ok and update the space used
    //
    if (config->space_is_short() || time_now % config->space_used_frequency == 0) {
      Log::config->update_space_used();
    }

    // See if there are any buffers that have expired
    //
    Log::config->log_object_manager.check_buffer_expiration(time_now);

    // Check if we received a request to roll, and roll if so, otherwise
    // give objects a chance to roll if they need to
    //
    if (Log::config->roll_log_files_now) {
      if (error_log) {
        error_log->roll_files(time_now);
      }
      if (global_scrap_object) {
        global_scrap_object->roll_files(time_now);
      }
      Log::config->log_object_manager.roll_files(time_now);
      Log::config->roll_log_files_now = false;
    } else {
      if (error_log) {
        error_log->roll_files(time_now);
      }
      if (global_scrap_object) {
        global_scrap_object->roll_files(time_now);
      }
      Log::config->log_object_manager.roll_files(time_now);
    }
  }
}

/*-------------------------------------------------------------------------
  MAIN INTERFACE
  -------------------------------------------------------------------------*/
struct LoggingPreprocContinuation : public Continuation {
  int m_idx;

  int
  mainEvent(int /* event ATS_UNUSED */, void * /* data ATS_UNUSED */)
  {
    Log::preproc_thread_main((void *)&m_idx);
    return 0;
  }

  LoggingPreprocContinuation(int idx) : Continuation(nullptr), m_idx(idx) { SET_HANDLER(&LoggingPreprocContinuation::mainEvent); }
};

struct LoggingFlushContinuation : public Continuation {
  int m_idx;

  int
  mainEvent(int /* event ATS_UNUSED */, void * /* data ATS_UNUSED */)
  {
    Log::flush_thread_main((void *)&m_idx);
    return 0;
  }

  LoggingFlushContinuation(int idx) : Continuation(nullptr), m_idx(idx) { SET_HANDLER(&LoggingFlushContinuation::mainEvent); }
};

struct LoggingCollateContinuation : public Continuation {
  int
  mainEvent(int /* event ATS_UNUSED */, void * /* data ATS_UNUSED */)
  {
    Log::collate_thread_main(nullptr);
    return 0;
  }

  LoggingCollateContinuation() : Continuation(nullptr) { SET_HANDLER(&LoggingCollateContinuation::mainEvent); }
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
  if (init_status & FIELDS_INITIALIZED) {
    return;
  }

  LogField *field;

  //
  // Initializes material to find a milestone name from their
  // name in a rapid manner.
  LogField::init_milestone_container();

  //
  // Create a hash table that will be used to find the global field
  // objects from their symbol names in a rapid manner.
  //
  field_symbol_hash = ink_hash_table_create(InkHashTableKeyType_String);

  // client -> proxy fields
  field = new LogField("client_host_ip", "chi", LogField::IP, &LogAccess::marshal_client_host_ip, &LogAccess::unmarshal_ip_to_str);
  global_field_list.add(field, false);
  ink_hash_table_insert(field_symbol_hash, "chi", field);

  field =
    new LogField("client_host_port", "chp", LogField::sINT, &LogAccess::marshal_client_host_port, &LogAccess::unmarshal_int_to_str);
  global_field_list.add(field, false);
  ink_hash_table_insert(field_symbol_hash, "chp", field);

  field =
    new LogField("client_host_ip_hex", "chih", LogField::IP, &LogAccess::marshal_client_host_ip, &LogAccess::unmarshal_ip_to_hex);
  global_field_list.add(field, false);
  ink_hash_table_insert(field_symbol_hash, "chih", field);

  // interface ip

  field =
    new LogField("host_interface_ip", "hii", LogField::IP, &LogAccess::marshal_host_interface_ip, &LogAccess::unmarshal_ip_to_str);
  global_field_list.add(field, false);
  ink_hash_table_insert(field_symbol_hash, "hii", field);

  field = new LogField("host_interface_ip_hex", "hiih", LogField::IP, &LogAccess::marshal_host_interface_ip,
                       &LogAccess::unmarshal_ip_to_hex);
  global_field_list.add(field, false);
  ink_hash_table_insert(field_symbol_hash, "hiih", field);
  // interface ip end
  field = new LogField("client_auth_user_name", "caun", LogField::STRING, &LogAccess::marshal_client_auth_user_name,
                       (LogField::UnmarshalFunc)&LogAccess::unmarshal_str);
  global_field_list.add(field, false);
  ink_hash_table_insert(field_symbol_hash, "caun", field);

  field = new LogField("plugin_identity_id", "piid", LogField::sINT, &LogAccess::marshal_plugin_identity_id,
                       reinterpret_cast<LogField::UnmarshalFunc>(&LogAccess::unmarshal_int_to_str));
  global_field_list.add(field, false);
  ink_hash_table_insert(field_symbol_hash, "piid", field);

  field = new LogField("plugin_identity_tag", "pitag", LogField::STRING, &LogAccess::marshal_plugin_identity_tag,
                       reinterpret_cast<LogField::UnmarshalFunc>(&LogAccess::unmarshal_str));
  global_field_list.add(field, false);
  ink_hash_table_insert(field_symbol_hash, "pitag", field);

  field = new LogField("client_req_timestamp_sec", "cqts", LogField::sINT, &LogAccess::marshal_client_req_timestamp_sec,
                       &LogAccess::unmarshal_int_to_str);
  global_field_list.add(field, false);
  ink_hash_table_insert(field_symbol_hash, "cqts", field);

  field = new LogField("client_req_timestamp_hex_sec", "cqth", LogField::sINT, &LogAccess::marshal_client_req_timestamp_sec,
                       &LogAccess::unmarshal_int_to_str_hex);
  global_field_list.add(field, false);
  ink_hash_table_insert(field_symbol_hash, "cqth", field);

  field = new LogField("client_req_timestamp_squid", "cqtq", LogField::sINT, &LogAccess::marshal_client_req_timestamp_ms,
                       &LogAccess::unmarshal_ttmsf);
  global_field_list.add(field, false);
  ink_hash_table_insert(field_symbol_hash, "cqtq", field);

  field = new LogField("client_req_timestamp_netscape", "cqtn", LogField::sINT, &LogAccess::marshal_client_req_timestamp_sec,
                       &LogAccess::unmarshal_int_to_netscape_str);
  global_field_list.add(field, false);
  ink_hash_table_insert(field_symbol_hash, "cqtn", field);

  field = new LogField("client_req_timestamp_date", "cqtd", LogField::sINT, &LogAccess::marshal_client_req_timestamp_sec,
                       &LogAccess::unmarshal_int_to_date_str);
  global_field_list.add(field, false);
  ink_hash_table_insert(field_symbol_hash, "cqtd", field);

  field = new LogField("client_req_timestamp_time", "cqtt", LogField::sINT, &LogAccess::marshal_client_req_timestamp_sec,
                       &LogAccess::unmarshal_int_to_time_str);
  global_field_list.add(field, false);
  ink_hash_table_insert(field_symbol_hash, "cqtt", field);

  field = new LogField("client_req_text", "cqtx", LogField::STRING, &LogAccess::marshal_client_req_text,
                       (LogField::UnmarshalFunc)&LogAccess::unmarshal_http_text);
  global_field_list.add(field, false);
  ink_hash_table_insert(field_symbol_hash, "cqtx", field);

  field = new LogField("client_req_http_method", "cqhm", LogField::STRING, &LogAccess::marshal_client_req_http_method,
                       (LogField::UnmarshalFunc)&LogAccess::unmarshal_str);
  global_field_list.add(field, false);
  ink_hash_table_insert(field_symbol_hash, "cqhm", field);

  field = new LogField("client_req_url", "cqu", LogField::STRING, &LogAccess::marshal_client_req_url,
                       (LogField::UnmarshalFunc)&LogAccess::unmarshal_str, &LogAccess::set_client_req_url);
  global_field_list.add(field, false);
  ink_hash_table_insert(field_symbol_hash, "cqu", field);

  field = new LogField("client_req_url_canonical", "cquc", LogField::STRING, &LogAccess::marshal_client_req_url_canon,
                       (LogField::UnmarshalFunc)&LogAccess::unmarshal_str, &LogAccess::set_client_req_url_canon);
  global_field_list.add(field, false);
  ink_hash_table_insert(field_symbol_hash, "cquc", field);

  field =
    new LogField("client_req_unmapped_url_canonical", "cquuc", LogField::STRING, &LogAccess::marshal_client_req_unmapped_url_canon,
                 (LogField::UnmarshalFunc)&LogAccess::unmarshal_str, &LogAccess::set_client_req_unmapped_url_canon);
  global_field_list.add(field, false);
  ink_hash_table_insert(field_symbol_hash, "cquuc", field);

  field = new LogField("client_req_unmapped_url_path", "cquup", LogField::STRING, &LogAccess::marshal_client_req_unmapped_url_path,
                       (LogField::UnmarshalFunc)&LogAccess::unmarshal_str, &LogAccess::set_client_req_unmapped_url_path);
  global_field_list.add(field, false);
  ink_hash_table_insert(field_symbol_hash, "cquup", field);

  field = new LogField("client_req_unmapped_url_host", "cquuh", LogField::STRING, &LogAccess::marshal_client_req_unmapped_url_host,
                       (LogField::UnmarshalFunc)&LogAccess::unmarshal_str, &LogAccess::set_client_req_unmapped_url_host);
  global_field_list.add(field, false);
  ink_hash_table_insert(field_symbol_hash, "cquuh", field);

  field = new LogField("client_req_url_scheme", "cqus", LogField::STRING, &LogAccess::marshal_client_req_url_scheme,
                       (LogField::UnmarshalFunc)&LogAccess::unmarshal_str);
  global_field_list.add(field, false);
  ink_hash_table_insert(field_symbol_hash, "cqus", field);

  field = new LogField("client_req_url_path", "cqup", LogField::STRING, &LogAccess::marshal_client_req_url_path,
                       (LogField::UnmarshalFunc)&LogAccess::unmarshal_str, &LogAccess::set_client_req_url_path);
  global_field_list.add(field, false);
  ink_hash_table_insert(field_symbol_hash, "cqup", field);

  field = new LogField("client_req_http_version", "cqhv", LogField::dINT, &LogAccess::marshal_client_req_http_version,
                       &LogAccess::unmarshal_http_version);
  global_field_list.add(field, false);
  ink_hash_table_insert(field_symbol_hash, "cqhv", field);

  field = new LogField("client_req_protocol_version", "cqpv", LogField::dINT, &LogAccess::marshal_client_req_protocol_version,
                       (LogField::UnmarshalFunc)&LogAccess::unmarshal_str);
  global_field_list.add(field, false);
  ink_hash_table_insert(field_symbol_hash, "cqpv", field);

  field = new LogField("client_req_header_len", "cqhl", LogField::sINT, &LogAccess::marshal_client_req_header_len,
                       &LogAccess::unmarshal_int_to_str);
  global_field_list.add(field, false);
  ink_hash_table_insert(field_symbol_hash, "cqhl", field);

  field = new LogField("client_req_squid_len", "cqql", LogField::sINT, &LogAccess::marshal_client_req_squid_len,
                       &LogAccess::unmarshal_int_to_str);
  global_field_list.add(field, false);
  ink_hash_table_insert(field_symbol_hash, "cqql", field);

  field = new LogField("cache_lookup_url_canonical", "cluc", LogField::STRING, &LogAccess::marshal_cache_lookup_url_canon,
                       (LogField::UnmarshalFunc)&LogAccess::unmarshal_str);
  global_field_list.add(field, false);
  ink_hash_table_insert(field_symbol_hash, "cluc", field);

  field = new LogField("process_uuid", "puuid", LogField::STRING, &LogAccess::marshal_process_uuid,
                       (LogField::UnmarshalFunc)&LogAccess::unmarshal_str);
  global_field_list.add(field, false);
  ink_hash_table_insert(field_symbol_hash, "puuid", field);

  // TS-4765: This alias is deprecated to be removed in 8.0.
  field = new LogField("client_req_body_len", "cqbl", LogField::sINT, &LogAccess::marshal_client_req_content_len,
                       &LogAccess::unmarshal_int_to_str);
  global_field_list.add(field, false);
  ink_hash_table_insert(field_symbol_hash, "cqbl", field);

  field = new LogField("client_req_content_len", "cqcl", LogField::sINT, &LogAccess::marshal_client_req_content_len,
                       &LogAccess::unmarshal_int_to_str);
  global_field_list.add(field, false);
  ink_hash_table_insert(field_symbol_hash, "cqcl", field);

  field = new LogField("client_req_tcp_reused", "cqtr", LogField::dINT, &LogAccess::marshal_client_req_tcp_reused,
                       &LogAccess::unmarshal_int_to_str);
  global_field_list.add(field, false);
  ink_hash_table_insert(field_symbol_hash, "cqtr", field);

  field = new LogField("client_req_is_ssl", "cqssl", LogField::dINT, &LogAccess::marshal_client_req_is_ssl,
                       &LogAccess::unmarshal_int_to_str);
  global_field_list.add(field, false);
  ink_hash_table_insert(field_symbol_hash, "cqssl", field);

  field = new LogField("client_req_ssl_reused", "cqssr", LogField::dINT, &LogAccess::marshal_client_req_ssl_reused,
                       &LogAccess::unmarshal_int_to_str);
  global_field_list.add(field, false);
  ink_hash_table_insert(field_symbol_hash, "cqssr", field);

  field = new LogField("client_sec_protocol", "cqssv", LogField::STRING, &LogAccess::marshal_client_security_protocol,
                       (LogField::UnmarshalFunc)&LogAccess::unmarshal_str);
  global_field_list.add(field, false);
  ink_hash_table_insert(field_symbol_hash, "cqssv", field);

  field = new LogField("client_cipher_suite", "cqssc", LogField::STRING, &LogAccess::marshal_client_security_cipher_suite,
                       (LogField::UnmarshalFunc)&LogAccess::unmarshal_str);
  global_field_list.add(field, false);
  ink_hash_table_insert(field_symbol_hash, "cqssc", field);

  Ptr<LogFieldAliasTable> finish_status_map = make_ptr(new LogFieldAliasTable);
  finish_status_map->init(N_LOG_FINISH_CODE_TYPES, LOG_FINISH_FIN, "FIN", LOG_FINISH_INTR, "INTR", LOG_FINISH_TIMEOUT, "TIMEOUT");

  field = new LogField("client_finish_status_code", "cfsc", LogField::sINT, &LogAccess::marshal_client_finish_status_code,
                       &LogAccess::unmarshal_finish_status, make_alias_map(finish_status_map));
  global_field_list.add(field, false);
  ink_hash_table_insert(field_symbol_hash, "cfsc", field);

  field =
    new LogField("client_req_id", "crid", LogField::sINT, &LogAccess::marshal_client_req_id, &LogAccess::unmarshal_int_to_str);
  global_field_list.add(field, false);
  ink_hash_table_insert(field_symbol_hash, "crid", field);

  field = new LogField("client_req_uuid", "cruuid", LogField::STRING, &LogAccess::marshal_client_req_uuid,
                       (LogField::UnmarshalFunc)&LogAccess::unmarshal_str);
  global_field_list.add(field, false);
  ink_hash_table_insert(field_symbol_hash, "cruuid", field);

  field = new LogField("client_rx_error_code", "crec", LogField::STRING, &LogAccess::marshal_client_rx_error_code,
                       (LogField::UnmarshalFunc)&LogAccess::unmarshal_str);
  global_field_list.add(field, false);
  ink_hash_table_insert(field_symbol_hash, "crec", field);

  field = new LogField("client_tx_error_code", "ctec", LogField::STRING, &LogAccess::marshal_client_tx_error_code,
                       (LogField::UnmarshalFunc)&LogAccess::unmarshal_str);
  global_field_list.add(field, false);
  ink_hash_table_insert(field_symbol_hash, "ctec", field);

  // proxy -> client fields
  field = new LogField("proxy_resp_content_type", "psct", LogField::STRING, &LogAccess::marshal_proxy_resp_content_type,
                       (LogField::UnmarshalFunc)&LogAccess::unmarshal_str);
  global_field_list.add(field, false);
  ink_hash_table_insert(field_symbol_hash, "psct", field);

  field = new LogField("proxy_resp_reason_phrase", "prrp", LogField::STRING, &LogAccess::marshal_proxy_resp_reason_phrase,
                       (LogField::UnmarshalFunc)&LogAccess::unmarshal_str);
  global_field_list.add(field, false);
  ink_hash_table_insert(field_symbol_hash, "prrp", field);

  field = new LogField("proxy_resp_squid_len", "psql", LogField::sINT, &LogAccess::marshal_proxy_resp_squid_len,
                       &LogAccess::unmarshal_int_to_str);
  global_field_list.add(field, false);
  ink_hash_table_insert(field_symbol_hash, "psql", field);

  field = new LogField("proxy_resp_content_len", "pscl", LogField::sINT, &LogAccess::marshal_proxy_resp_content_len,
                       &LogAccess::unmarshal_int_to_str);
  global_field_list.add(field, false);
  ink_hash_table_insert(field_symbol_hash, "pscl", field);

  field = new LogField("proxy_resp_content_len_hex", "psch", LogField::sINT, &LogAccess::marshal_proxy_resp_content_len,
                       &LogAccess::unmarshal_int_to_str_hex);
  global_field_list.add(field, false);
  ink_hash_table_insert(field_symbol_hash, "psch", field);

  field = new LogField("proxy_resp_status_code", "pssc", LogField::sINT, &LogAccess::marshal_proxy_resp_status_code,
                       &LogAccess::unmarshal_http_status);
  global_field_list.add(field, false);
  ink_hash_table_insert(field_symbol_hash, "pssc", field);

  field = new LogField("proxy_resp_header_len", "pshl", LogField::sINT, &LogAccess::marshal_proxy_resp_header_len,
                       &LogAccess::unmarshal_int_to_str);
  global_field_list.add(field, false);
  ink_hash_table_insert(field_symbol_hash, "pshl", field);

  field = new LogField("proxy_finish_status_code", "pfsc", LogField::sINT, &LogAccess::marshal_proxy_finish_status_code,
                       &LogAccess::unmarshal_finish_status, make_alias_map(finish_status_map));
  global_field_list.add(field, false);
  ink_hash_table_insert(field_symbol_hash, "pfsc", field);

  Ptr<LogFieldAliasTable> cache_code_map = make_ptr(new LogFieldAliasTable);
  cache_code_map->init(
    51, SQUID_LOG_EMPTY, "UNDEFINED", SQUID_LOG_TCP_HIT, "TCP_HIT", SQUID_LOG_TCP_DISK_HIT, "TCP_DISK_HIT", SQUID_LOG_TCP_MEM_HIT,
    "TCP_MEM_HIT", SQUID_LOG_TCP_MISS, "TCP_MISS", SQUID_LOG_TCP_EXPIRED_MISS, "TCP_EXPIRED_MISS", SQUID_LOG_TCP_REFRESH_HIT,
    "TCP_REFRESH_HIT", SQUID_LOG_TCP_REF_FAIL_HIT, "TCP_REFRESH_FAIL_HIT", SQUID_LOG_TCP_REFRESH_MISS, "TCP_REFRESH_MISS",
    SQUID_LOG_TCP_CLIENT_REFRESH, "TCP_CLIENT_REFRESH_MISS", SQUID_LOG_TCP_IMS_HIT, "TCP_IMS_HIT", SQUID_LOG_TCP_IMS_MISS,
    "TCP_IMS_MISS", SQUID_LOG_TCP_SWAPFAIL, "TCP_SWAPFAIL_MISS", SQUID_LOG_TCP_DENIED, "TCP_DENIED", SQUID_LOG_TCP_WEBFETCH_MISS,
    "TCP_WEBFETCH_MISS", SQUID_LOG_TCP_FUTURE_2, "TCP_FUTURE_2", SQUID_LOG_TCP_HIT_REDIRECT, "TCP_HIT_REDIRECT",
    SQUID_LOG_TCP_MISS_REDIRECT, "TCP_MISS_REDIRECT", SQUID_LOG_TCP_HIT_X_REDIRECT, "TCP_HIT_X_REDIRECT",
    SQUID_LOG_TCP_MISS_X_REDIRECT, "TCP_MISS_X_REDIRECT", SQUID_LOG_UDP_HIT, "UDP_HIT", SQUID_LOG_UDP_WEAK_HIT, "UDP_WEAK_HIT",
    SQUID_LOG_UDP_HIT_OBJ, "UDP_HIT_OBJ", SQUID_LOG_UDP_MISS, "UDP_MISS", SQUID_LOG_UDP_DENIED, "UDP_DENIED", SQUID_LOG_UDP_INVALID,
    "UDP_INVALID", SQUID_LOG_UDP_RELOADING, "UDP_RELOADING", SQUID_LOG_UDP_FUTURE_1, "UDP_FUTURE_1", SQUID_LOG_UDP_FUTURE_2,
    "UDP_FUTURE_2", SQUID_LOG_ERR_READ_TIMEOUT, "ERR_READ_TIMEOUT", SQUID_LOG_ERR_LIFETIME_EXP, "ERR_LIFETIME_EXP",
    SQUID_LOG_ERR_POST_ENTITY_TOO_LARGE, "ERR_POST_ENTITY_TOO_LARGE", SQUID_LOG_ERR_NO_CLIENTS_BIG_OBJ, "ERR_NO_CLIENTS_BIG_OBJ",
    SQUID_LOG_ERR_READ_ERROR, "ERR_READ_ERROR", SQUID_LOG_ERR_CLIENT_ABORT, "ERR_CLIENT_ABORT", SQUID_LOG_ERR_CLIENT_READ_ERROR,
    "ERR_CLIENT_READ_ERROR", SQUID_LOG_ERR_CONNECT_FAIL, "ERR_CONNECT_FAIL", SQUID_LOG_ERR_INVALID_REQ, "ERR_INVALID_REQ",
    SQUID_LOG_ERR_UNSUP_REQ, "ERR_UNSUP_REQ", SQUID_LOG_ERR_INVALID_URL, "ERR_INVALID_URL", SQUID_LOG_ERR_NO_FDS, "ERR_NO_FDS",
    SQUID_LOG_ERR_DNS_FAIL, "ERR_DNS_FAIL", SQUID_LOG_ERR_NOT_IMPLEMENTED, "ERR_NOT_IMPLEMENTED", SQUID_LOG_ERR_CANNOT_FETCH,
    "ERR_CANNOT_FETCH", SQUID_LOG_ERR_NO_RELAY, "ERR_NO_RELAY", SQUID_LOG_ERR_DISK_IO, "ERR_DISK_IO",
    SQUID_LOG_ERR_ZERO_SIZE_OBJECT, "ERR_ZERO_SIZE_OBJECT", SQUID_LOG_ERR_PROXY_DENIED, "ERR_PROXY_DENIED",
    SQUID_LOG_ERR_WEBFETCH_DETECTED, "ERR_WEBFETCH_DETECTED", SQUID_LOG_ERR_FUTURE_1, "ERR_FUTURE_1", SQUID_LOG_ERR_UNKNOWN,
    "ERR_UNKNOWN");

  Ptr<LogFieldAliasTable> cache_subcode_map = make_ptr(new LogFieldAliasTable);
  cache_subcode_map->init(2, SQUID_SUBCODE_EMPTY, "NONE", SQUID_SUBCODE_NUM_REDIRECTIONS_EXCEEDED, "NUM_REDIRECTIONS_EXCEEDED");

  Ptr<LogFieldAliasTable> cache_hit_miss_map = make_ptr(new LogFieldAliasTable);
  cache_hit_miss_map->init(21, SQUID_HIT_RESERVED, "HIT", SQUID_HIT_LEVEL_1, "HIT_RAM", // Also SQUID_HIT_RAM
                           SQUID_HIT_LEVEL_2, "HIT_SSD",                                // Also SQUID_HIT_SSD
                           SQUID_HIT_LEVEL_3, "HIT_DISK",                               // Also SQUID_HIT_DISK
                           SQUID_HIT_LEVEL_4, "HIT_CLUSTER",                            // Also SQUID_HIT_CLUSTER
                           SQUID_HIT_LEVEL_5, "HIT_NET",                                // Also SQUID_HIT_NET
                           SQUID_HIT_LEVEL_6, "HIT_LEVEL_6", SQUID_HIT_LEVEL_7, "HIT_LEVEL_7", SQUID_HIT_LEVEL_8, "HIT_LEVEL_8",
                           SQUID_HIT_LEVEl_9, "HIT_LEVEL_9", SQUID_MISS_NONE, "MISS", SQUID_MISS_HTTP_NON_CACHE,
                           "MISS_HTTP_NON_CACHE", SQUID_MISS_HTTP_NO_DLE, "MISS_HTTP_NO_DLE", SQUID_MISS_HTTP_NO_LE,
                           "MISS_HTTP_NO_LE", SQUID_MISS_HTTP_CONTENT, "MISS_HTTP_CONTENT", SQUID_MISS_PRAGMA_NOCACHE,
                           "MISS_PRAGMA_NOCACHE", SQUID_MISS_PASS, "MISS_PASS", SQUID_MISS_PRE_EXPIRED, "MISS_PRE_EXPIRED",
                           SQUID_MISS_ERROR, "MISS_ERROR", SQUID_MISS_CACHE_BYPASS, "MISS_CACHE_BYPASS",
                           SQUID_HIT_MISS_INVALID_ASSIGNED_CODE, "INVALID_CODE");

  field = new LogField("cache_result_code", "crc", LogField::sINT, &LogAccess::marshal_cache_result_code,
                       &LogAccess::unmarshal_cache_code, make_alias_map(cache_code_map));
  global_field_list.add(field, false);
  ink_hash_table_insert(field_symbol_hash, "crc", field);

  // Reuse the unmarshalling code from crc
  field = new LogField("cache_result_subcode", "crsc", LogField::sINT, &LogAccess::marshal_cache_result_subcode,
                       &LogAccess::unmarshal_cache_code, make_alias_map(cache_subcode_map));
  global_field_list.add(field, false);
  ink_hash_table_insert(field_symbol_hash, "crsc", field);

  field = new LogField("cache_hit_miss", "chm", LogField::sINT, &LogAccess::marshal_cache_hit_miss,
                       &LogAccess::unmarshal_cache_hit_miss, make_alias_map(cache_hit_miss_map));
  global_field_list.add(field, false);
  ink_hash_table_insert(field_symbol_hash, "chm", field);

  // proxy -> server fields
  field = new LogField("proxy_req_header_len", "pqhl", LogField::sINT, &LogAccess::marshal_proxy_req_header_len,
                       &LogAccess::unmarshal_int_to_str);
  global_field_list.add(field, false);
  ink_hash_table_insert(field_symbol_hash, "pqhl", field);

  field = new LogField("proxy_req_squid_len", "pqql", LogField::sINT, &LogAccess::marshal_proxy_req_squid_len,
                       &LogAccess::unmarshal_int_to_str);
  global_field_list.add(field, false);
  ink_hash_table_insert(field_symbol_hash, "pqql", field);

  // TS-4765: This alias is deprecated to be removed in 8.0.
  field = new LogField("proxy_req_body_len", "pqbl", LogField::sINT, &LogAccess::marshal_proxy_req_content_len,
                       &LogAccess::unmarshal_int_to_str);
  global_field_list.add(field, false);
  ink_hash_table_insert(field_symbol_hash, "pqbl", field);

  field = new LogField("proxy_req_content_len", "pqcl", LogField::sINT, &LogAccess::marshal_proxy_req_content_len,
                       &LogAccess::unmarshal_int_to_str);
  global_field_list.add(field, false);
  ink_hash_table_insert(field_symbol_hash, "pqcl", field);

  field = new LogField("proxy_req_server_ip", "pqsi", LogField::IP, &LogAccess::marshal_proxy_req_server_ip,
                       &LogAccess::unmarshal_ip_to_str);
  global_field_list.add(field, false);
  ink_hash_table_insert(field_symbol_hash, "pqsi", field);

  field = new LogField("proxy_req_server_port", "pqsp", LogField::sINT, &LogAccess::marshal_proxy_req_server_port,
                       &LogAccess::unmarshal_int_to_str);
  global_field_list.add(field, false);
  ink_hash_table_insert(field_symbol_hash, "pqsp", field);

  field = new LogField("next_hop_ip", "nhi", LogField::IP, &LogAccess::marshal_next_hop_ip, &LogAccess::unmarshal_ip_to_str);
  global_field_list.add(field, false);
  ink_hash_table_insert(field_symbol_hash, "nhi", field);

  field = new LogField("next_hop_port", "nhp", LogField::IP, &LogAccess::marshal_next_hop_port, &LogAccess::unmarshal_int_to_str);
  global_field_list.add(field, false);
  ink_hash_table_insert(field_symbol_hash, "nhp", field);

  Ptr<LogFieldAliasTable> hierarchy_map = make_ptr(new LogFieldAliasTable);
  hierarchy_map->init(
    36, SQUID_HIER_EMPTY, "EMPTY", SQUID_HIER_NONE, "NONE", SQUID_HIER_DIRECT, "DIRECT", SQUID_HIER_SIBLING_HIT, "SIBLING_HIT",
    SQUID_HIER_PARENT_HIT, "PARENT_HIT", SQUID_HIER_DEFAULT_PARENT, "DEFAULT_PARENT", SQUID_HIER_SINGLE_PARENT, "SINGLE_PARENT",
    SQUID_HIER_FIRST_UP_PARENT, "FIRST_UP_PARENT", SQUID_HIER_NO_PARENT_DIRECT, "NO_PARENT_DIRECT", SQUID_HIER_FIRST_PARENT_MISS,
    "FIRST_PARENT_MISS", SQUID_HIER_LOCAL_IP_DIRECT, "LOCAL_IP_DIRECT", SQUID_HIER_FIREWALL_IP_DIRECT, "FIREWALL_IP_DIRECT",
    SQUID_HIER_NO_DIRECT_FAIL, "NO_DIRECT_FAIL", SQUID_HIER_SOURCE_FASTEST, "SOURCE_FASTEST", SQUID_HIER_SIBLING_UDP_HIT_OBJ,
    "SIBLING_UDP_HIT_OBJ", SQUID_HIER_PARENT_UDP_HIT_OBJ, "PARENT_UDP_HIT_OBJ", SQUID_HIER_PASSTHROUGH_PARENT, "PASSTHROUGH_PARENT",
    SQUID_HIER_SSL_PARENT_MISS, "SSL_PARENT_MISS", SQUID_HIER_INVALID_CODE, "INVALID_CODE", SQUID_HIER_TIMEOUT_DIRECT,
    "TIMEOUT_DIRECT", SQUID_HIER_TIMEOUT_SIBLING_HIT, "TIMEOUT_SIBLING_HIT", SQUID_HIER_TIMEOUT_PARENT_HIT, "TIMEOUT_PARENT_HIT",
    SQUID_HIER_TIMEOUT_DEFAULT_PARENT, "TIMEOUT_DEFAULT_PARENT", SQUID_HIER_TIMEOUT_SINGLE_PARENT, "TIMEOUT_SINGLE_PARENT",
    SQUID_HIER_TIMEOUT_FIRST_UP_PARENT, "TIMEOUT_FIRST_UP_PARENT", SQUID_HIER_TIMEOUT_NO_PARENT_DIRECT, "TIMEOUT_NO_PARENT_DIRECT",
    SQUID_HIER_TIMEOUT_FIRST_PARENT_MISS, "TIMEOUT_FIRST_PARENT_MISS", SQUID_HIER_TIMEOUT_LOCAL_IP_DIRECT,
    "TIMEOUT_LOCAL_IP_DIRECT", SQUID_HIER_TIMEOUT_FIREWALL_IP_DIRECT, "TIMEOUT_FIREWALL_IP_DIRECT",
    SQUID_HIER_TIMEOUT_NO_DIRECT_FAIL, "TIMEOUT_NO_DIRECT_FAIL", SQUID_HIER_TIMEOUT_SOURCE_FASTEST, "TIMEOUT_SOURCE_FASTEST",
    SQUID_HIER_TIMEOUT_SIBLING_UDP_HIT_OBJ, "TIMEOUT_SIBLING_UDP_HIT_OBJ", SQUID_HIER_TIMEOUT_PARENT_UDP_HIT_OBJ,
    "TIMEOUT_PARENT_UDP_HIT_OBJ", SQUID_HIER_TIMEOUT_PASSTHROUGH_PARENT, "TIMEOUT_PASSTHROUGH_PARENT",
    SQUID_HIER_TIMEOUT_TIMEOUT_SSL_PARENT_MISS, "TIMEOUT_TIMEOUT_SSL_PARENT_MISS", SQUID_HIER_INVALID_ASSIGNED_CODE,
    "INVALID_ASSIGNED_CODE");

  field = new LogField("proxy_hierarchy_route", "phr", LogField::sINT, &LogAccess::marshal_proxy_hierarchy_route,
                       &LogAccess::unmarshal_hierarchy, make_alias_map(hierarchy_map));
  global_field_list.add(field, false);
  ink_hash_table_insert(field_symbol_hash, "phr", field);

  field = new LogField("proxy_host_name", "phn", LogField::STRING, &LogAccess::marshal_proxy_host_name,
                       (LogField::UnmarshalFunc)&LogAccess::unmarshal_str);
  global_field_list.add(field, false);
  ink_hash_table_insert(field_symbol_hash, "phn", field);

  field = new LogField("proxy_host_ip", "phi", LogField::IP, &LogAccess::marshal_proxy_host_ip, &LogAccess::unmarshal_ip_to_str);
  global_field_list.add(field, false);
  ink_hash_table_insert(field_symbol_hash, "phi", field);

  field =
    new LogField("proxy_host_port", "php", LogField::sINT, &LogAccess::marshal_proxy_host_port, &LogAccess::unmarshal_int_to_str);
  global_field_list.add(field, false);
  ink_hash_table_insert(field_symbol_hash, "php", field);

  field = new LogField("proxy_req_is_ssl", "pqssl", LogField::sINT, &LogAccess::marshal_proxy_req_is_ssl,
                       &LogAccess::unmarshal_int_to_str);
  global_field_list.add(field, false);
  ink_hash_table_insert(field_symbol_hash, "pqssl", field);

  // server -> proxy fields
  field = new LogField("server_host_ip", "shi", LogField::IP, &LogAccess::marshal_server_host_ip, &LogAccess::unmarshal_ip_to_str);

  global_field_list.add(field, false);
  ink_hash_table_insert(field_symbol_hash, "shi", field);

  field = new LogField("server_host_name", "shn", LogField::STRING, &LogAccess::marshal_server_host_name,
                       (LogField::UnmarshalFunc)&LogAccess::unmarshal_str);
  global_field_list.add(field, false);
  ink_hash_table_insert(field_symbol_hash, "shn", field);

  field = new LogField("server_resp_status_code", "sssc", LogField::sINT, &LogAccess::marshal_server_resp_status_code,
                       &LogAccess::unmarshal_http_status);
  global_field_list.add(field, false);
  ink_hash_table_insert(field_symbol_hash, "sssc", field);

  field = new LogField("server_resp_content_len", "sscl", LogField::sINT, &LogAccess::marshal_server_resp_content_len,
                       &LogAccess::unmarshal_int_to_str);
  global_field_list.add(field, false);
  ink_hash_table_insert(field_symbol_hash, "sscl", field);

  field = new LogField("server_resp_header_len", "sshl", LogField::sINT, &LogAccess::marshal_server_resp_header_len,
                       &LogAccess::unmarshal_int_to_str);
  global_field_list.add(field, false);
  ink_hash_table_insert(field_symbol_hash, "sshl", field);

  field = new LogField("server_resp_squid_len", "ssql", LogField::sINT, &LogAccess::marshal_server_resp_squid_len,
                       &LogAccess::unmarshal_int_to_str);
  global_field_list.add(field, false);
  ink_hash_table_insert(field_symbol_hash, "ssql", field);

  field = new LogField("server_resp_http_version", "sshv", LogField::dINT, &LogAccess::marshal_server_resp_http_version,
                       &LogAccess::unmarshal_http_version);
  global_field_list.add(field, false);
  ink_hash_table_insert(field_symbol_hash, "sshv", field);

  field = new LogField("server_resp_time", "stms", LogField::sINT, &LogAccess::marshal_server_resp_time_ms,
                       &LogAccess::unmarshal_int_to_str);
  global_field_list.add(field, false);
  ink_hash_table_insert(field_symbol_hash, "stms", field);

  field = new LogField("server_resp_time_hex", "stmsh", LogField::sINT, &LogAccess::marshal_server_resp_time_ms,
                       &LogAccess::unmarshal_int_to_str_hex);
  global_field_list.add(field, false);
  ink_hash_table_insert(field_symbol_hash, "stmsh", field);

  field = new LogField("server_resp_time_fractional", "stmsf", LogField::sINT, &LogAccess::marshal_server_resp_time_ms,
                       &LogAccess::unmarshal_ttmsf);
  global_field_list.add(field, false);
  ink_hash_table_insert(field_symbol_hash, "stmsf", field);

  field = new LogField("server_resp_time_sec", "sts", LogField::sINT, &LogAccess::marshal_server_resp_time_s,
                       &LogAccess::unmarshal_int_to_str);
  global_field_list.add(field, false);
  ink_hash_table_insert(field_symbol_hash, "sts", field);

  field = new LogField("server_transact_count", "sstc", LogField::sINT, &LogAccess::marshal_server_transact_count,
                       &LogAccess::unmarshal_int_to_str);
  global_field_list.add(field, false);
  ink_hash_table_insert(field_symbol_hash, "sstc", field);

  field = new LogField("server_connect_attempts", "sca", LogField::sINT, &LogAccess::marshal_server_connect_attempts,
                       &LogAccess::unmarshal_int_to_str);
  global_field_list.add(field, false);
  ink_hash_table_insert(field_symbol_hash, "sca", field);

  field = new LogField("cached_resp_status_code", "csssc", LogField::sINT, &LogAccess::marshal_cache_resp_status_code,
                       &LogAccess::unmarshal_http_status);
  global_field_list.add(field, false);
  ink_hash_table_insert(field_symbol_hash, "csssc", field);

  field = new LogField("cached_resp_content_len", "csscl", LogField::sINT, &LogAccess::marshal_cache_resp_content_len,
                       &LogAccess::unmarshal_int_to_str);
  global_field_list.add(field, false);
  ink_hash_table_insert(field_symbol_hash, "csscl", field);

  field = new LogField("cached_resp_header_len", "csshl", LogField::sINT, &LogAccess::marshal_cache_resp_header_len,
                       &LogAccess::unmarshal_int_to_str);
  global_field_list.add(field, false);
  ink_hash_table_insert(field_symbol_hash, "csshl", field);

  field = new LogField("cached_resp_squid_len", "cssql", LogField::sINT, &LogAccess::marshal_cache_resp_squid_len,
                       &LogAccess::unmarshal_int_to_str);
  global_field_list.add(field, false);
  ink_hash_table_insert(field_symbol_hash, "cssql", field);

  field = new LogField("cached_resp_http_version", "csshv", LogField::dINT, &LogAccess::marshal_cache_resp_http_version,
                       &LogAccess::unmarshal_http_version);
  global_field_list.add(field, false);
  ink_hash_table_insert(field_symbol_hash, "csshv", field);

  field = new LogField("client_retry_after_time", "crat", LogField::sINT, &LogAccess::marshal_client_retry_after_time,
                       &LogAccess::unmarshal_int_to_str);
  global_field_list.add(field, false);
  ink_hash_table_insert(field_symbol_hash, "crat", field);

  // cache write fields

  Ptr<LogFieldAliasTable> cache_write_code_map = make_ptr(new LogFieldAliasTable);
  cache_write_code_map->init(N_LOG_CACHE_WRITE_TYPES, LOG_CACHE_WRITE_NONE, "-", LOG_CACHE_WRITE_LOCK_MISSED, "WL_MISS",
                             LOG_CACHE_WRITE_LOCK_ABORTED, "INTR", LOG_CACHE_WRITE_ERROR, "ERR", LOG_CACHE_WRITE_COMPLETE, "FIN");
  field = new LogField("cache_write_result", "cwr", LogField::sINT, &LogAccess::marshal_cache_write_code,
                       &LogAccess::unmarshal_cache_write_code, make_alias_map(cache_write_code_map));
  global_field_list.add(field, false);
  ink_hash_table_insert(field_symbol_hash, "cwr", field);

  field = new LogField("cache_write_transform_result", "cwtr", LogField::sINT, &LogAccess::marshal_cache_write_transform_code,
                       &LogAccess::unmarshal_cache_write_code, make_alias_map(cache_write_code_map));
  global_field_list.add(field, false);
  ink_hash_table_insert(field_symbol_hash, "cwtr", field);

  // other fields

  field = new LogField("transfer_time_ms", "ttms", LogField::sINT, &LogAccess::marshal_transfer_time_ms,
                       &LogAccess::unmarshal_int_to_str);
  global_field_list.add(field, false);
  ink_hash_table_insert(field_symbol_hash, "ttms", field);

  field = new LogField("transfer_time_ms_hex", "ttmh", LogField::sINT, &LogAccess::marshal_transfer_time_ms,
                       &LogAccess::unmarshal_int_to_str_hex);
  global_field_list.add(field, false);
  ink_hash_table_insert(field_symbol_hash, "ttmh", field);

  field = new LogField("transfer_time_ms_fractional", "ttmsf", LogField::sINT, &LogAccess::marshal_transfer_time_ms,
                       &LogAccess::unmarshal_ttmsf);
  global_field_list.add(field, false);
  ink_hash_table_insert(field_symbol_hash, "ttmsf", field);

  field =
    new LogField("transfer_time_sec", "tts", LogField::sINT, &LogAccess::marshal_transfer_time_s, &LogAccess::unmarshal_int_to_str);
  global_field_list.add(field, false);
  ink_hash_table_insert(field_symbol_hash, "tts", field);

  field = new LogField("file_size", "fsiz", LogField::sINT, &LogAccess::marshal_file_size, &LogAccess::unmarshal_int_to_str);
  global_field_list.add(field, false);
  ink_hash_table_insert(field_symbol_hash, "fsiz", field);

  field = new LogField("client_connection_id", "ccid", LogField::sINT, &LogAccess::marshal_client_http_connection_id,
                       &LogAccess::unmarshal_int_to_str);
  global_field_list.add(field, false);
  ink_hash_table_insert(field_symbol_hash, "ccid", field);

  field = new LogField("client_transaction_id", "ctid", LogField::sINT, &LogAccess::marshal_client_http_transaction_id,
                       &LogAccess::unmarshal_int_to_str);
  global_field_list.add(field, false);
  ink_hash_table_insert(field_symbol_hash, "ctid", field);

  Ptr<LogFieldAliasTable> entry_type_map = make_ptr(new LogFieldAliasTable);
  entry_type_map->init(N_LOG_ENTRY_TYPES, LOG_ENTRY_HTTP, "LOG_ENTRY_HTTP");
  field = new LogField("log_entry_type", "etype", LogField::sINT, &LogAccess::marshal_entry_type, &LogAccess::unmarshal_entry_type,
                       make_alias_map(entry_type_map));
  global_field_list.add(field, false);
  ink_hash_table_insert(field_symbol_hash, "etype", field);

  init_status |= FIELDS_INITIALIZED;
}

/*-------------------------------------------------------------------------

  Initialization functions

  -------------------------------------------------------------------------*/
int
Log::handle_logging_mode_change(const char * /* name ATS_UNUSED */, RecDataT /* data_type ATS_UNUSED */,
                                RecData /* data ATS_UNUSED */, void * /* cookie ATS_UNUSED */)
{
  Debug("log-config", "Enabled status changed");
  logging_mode_changed = true;
  return 0;
}

int
Log::handle_periodic_tasks_int_change(const char * /* name ATS_UNUSED */, RecDataT /* data_type ATS_UNUSED */, RecData data,
                                      void * /* cookie ATS_UNSED */)
{
  Debug("log-periodic", "periodic task interval changed");
  if (data.rec_int <= 0) {
    periodic_tasks_interval = PERIODIC_TASKS_INTERVAL_FALLBACK;
    Error("new periodic tasks interval = %d is invalid, falling back to default = %d", (int)data.rec_int,
          PERIODIC_TASKS_INTERVAL_FALLBACK);
  } else {
    periodic_tasks_interval = (uint32_t)data.rec_int;
    Debug("log-periodic", "periodic task interval changed to %u", periodic_tasks_interval);
  }
  return REC_ERR_OKAY;
}

void
Log::init(int flags)
{
  collation_preproc_threads        = 1;
  collation_accept_file_descriptor = NO_FD;

  // store the configuration flags
  //
  config_flags = flags;

  // create the configuration object
  config = new LogConfig();
  ink_assert(config != nullptr);

  log_configid = configProcessor.set(log_configid, config);

  // set the logging_mode and read config variables if needed
  //
  if (config_flags & LOGCAT) {
    logging_mode = LOG_MODE_NONE;
  } else {
    log_rsb = RecAllocateRawStatBlock((int)log_stat_count);
    LogConfig::register_stat_callbacks();

    config->read_configuration_variables();
    collation_port            = config->collation_port;
    collation_preproc_threads = config->collation_preproc_threads;

    if (config_flags & STANDALONE_COLLATOR) {
      logging_mode = LOG_MODE_TRANSACTIONS;
    } else {
      int val = (int)REC_ConfigReadInteger("proxy.config.log.logging_enabled");
      if (val < LOG_MODE_NONE || val > LOG_MODE_FULL) {
        logging_mode = LOG_MODE_FULL;
        Warning("proxy.config.log.logging_enabled has an invalid "
                "value, setting it to %d",
                logging_mode);
      } else {
        logging_mode = (LoggingMode)val;
      }
      // periodic task interval are set on a per instance basis
      MgmtInt pti = REC_ConfigReadInteger("proxy.config.log.periodic_tasks_interval");
      if (pti <= 0) {
        Error("proxy.config.log.periodic_tasks_interval = %" PRId64 " is invalid", pti);
        Note("falling back to default periodic tasks interval = %d", PERIODIC_TASKS_INTERVAL_FALLBACK);
        periodic_tasks_interval = PERIODIC_TASKS_INTERVAL_FALLBACK;
      } else {
        periodic_tasks_interval = static_cast<uint32_t>(pti);
      }

      REC_RegisterConfigUpdateFunc("proxy.config.log.periodic_tasks_interval", &Log::handle_periodic_tasks_int_change, nullptr);
    }
  }

  // if remote management is enabled, do all necessary initialization to
  // be able to handle a logging mode change
  //
  if (!(config_flags & NO_REMOTE_MANAGEMENT)) {
    REC_RegisterConfigUpdateFunc("proxy.config.log.logging_enabled", &Log::handle_logging_mode_change, nullptr);

    REC_RegisterConfigUpdateFunc("proxy.local.log.collation_mode", &Log::handle_logging_mode_change, nullptr);

    // Clear any stat values that need to be reset on startup
    //
    RecSetRawStatSum(log_rsb, log_stat_log_files_open_stat, 0);
    RecSetRawStatCount(log_rsb, log_stat_log_files_open_stat, 0);
  }

  init_fields();
  if (!(config_flags & LOGCAT)) {
    Debug("log-config", "Log::init(): logging_mode = %d init status = %d", logging_mode, init_status);
    if (config_flags & STANDALONE_COLLATOR) {
      config->collation_mode = Log::COLLATION_HOST;
    }
    config->init();
    init_when_enabled();
  }
}

void
Log::init_when_enabled()
{
  // make sure the log config has been initialized
  ink_release_assert(config->initialized == true);

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
    global_scrap_format = MakeTextLogFormat();
    global_scrap_object =
      new LogObject(global_scrap_format, Log::config->logfile_dir, "scrapfile.log", LOG_FILE_BINARY, nullptr,
                    Log::config->rolling_enabled, Log::config->collation_preproc_threads, Log::config->rolling_interval_sec,
                    Log::config->rolling_offset_hr, Log::config->rolling_size_mb);

    // create the flush thread and the collation thread
    create_threads();
    eventProcessor.schedule_every(new PeriodicWakeup(collation_preproc_threads, 1), HRTIME_SECOND, ET_CALL);

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
  char desc[64];
  preproc_notify = new EventNotify[collation_preproc_threads];

  size_t stacksize;
  REC_ReadConfigInteger(stacksize, "proxy.config.thread.default.stacksize");

  // start the preproc threads
  //
  // no need for the conditional var since it will be relying on
  // on the event system.
  for (int i = 0; i < collation_preproc_threads; i++) {
    Continuation *preproc_cont = new LoggingPreprocContinuation(i);
    sprintf(desc, "[LOG_PREPROC %d]", i);
    eventProcessor.spawn_thread(preproc_cont, desc, stacksize);
  }

  // Now, only one flush thread is supported.
  // TODO: Enable multiple flush threads, such as
  //       one flush thread per file.
  //
  flush_notify    = new EventNotify;
  flush_data_list = new InkAtomicList;

  sprintf(desc, "Logging flush buffer list");
  ink_atomiclist_init(flush_data_list, desc, 0);
  Continuation *flush_cont = new LoggingFlushContinuation(0);
  sprintf(desc, "[LOG_FLUSH]");
  eventProcessor.spawn_thread(flush_cont, desc, stacksize);
}

/*-------------------------------------------------------------------------
  Log::access

  Make an entry in the access log for the data supplied by the given
  LogAccess object.
  -------------------------------------------------------------------------*/

int
Log::access(LogAccess *lad)
{
  // See if transaction logging is disabled
  //
  if (!transaction_logging_enabled()) {
    return Log::SKIP;
  }

  ink_assert(init_status & FULLY_INITIALIZED);
  ink_assert(lad != nullptr);

  int ret;
  static long sample = 1;
  long this_sample;
  ProxyMutex *mutex = this_ethread()->mutex.get();

  // See if we're sampling and it is not time for another sample
  //
  if (Log::config->sampling_frequency > 1) {
    this_sample = sample++;
    if (this_sample && this_sample % Log::config->sampling_frequency) {
      Debug("log", "sampling, skipping this entry ...");
      RecIncrRawStat(log_rsb, mutex->thread_holding, log_stat_event_log_access_skip_stat, 1);
      ret = Log::SKIP;
      goto done;
    } else {
      Debug("log", "sampling, LOGGING this entry ...");
      sample = 1;
    }
  }

  if (Log::config->log_object_manager.get_num_objects() == 0) {
    Debug("log", "no log objects, skipping this entry ...");
    RecIncrRawStat(log_rsb, mutex->thread_holding, log_stat_event_log_access_skip_stat, 1);
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
  -------------------------------------------------------------------------*/

int
Log::error(const char *format, ...)
{
  va_list ap;
  int ret;

  va_start(ap, format);
  ret = Log::va_error(format, ap);
  va_end(ap);

  return ret;
}

int
Log::va_error(const char *format, va_list ap)
{
  int ret_val       = Log::SKIP;
  ProxyMutex *mutex = this_ethread()->mutex.get();

  if (error_log) {
    ink_assert(format != nullptr);
    ret_val = error_log->va_log(nullptr, format, ap);

    switch (ret_val) {
    case Log::LOG_OK:
      RecIncrRawStat(log_rsb, mutex->thread_holding, log_stat_event_log_error_ok_stat, 1);
      break;
    case Log::SKIP:
      RecIncrRawStat(log_rsb, mutex->thread_holding, log_stat_event_log_error_skip_stat, 1);
      break;
    case Log::AGGR:
      RecIncrRawStat(log_rsb, mutex->thread_holding, log_stat_event_log_error_aggr_stat, 1);
      break;
    case Log::FULL:
      RecIncrRawStat(log_rsb, mutex->thread_holding, log_stat_event_log_error_full_stat, 1);
      break;
    case Log::FAIL:
      RecIncrRawStat(log_rsb, mutex->thread_holding, log_stat_event_log_error_fail_stat, 1);
      break;
    default:
      ink_release_assert(!"Unexpected result");
    }

    return ret_val;
  }

  RecIncrRawStat(log_rsb, mutex->thread_holding, log_stat_event_log_error_skip_stat, 1);

  return ret_val;
}

/*-------------------------------------------------------------------------
  Log::trace

  These functions are used for wiretracing of incoming SSL connections.
  They are an extension of the existing Log::error functionality but with
  special formatting and handling of the non null terminated buffer.
  -------------------------------------------------------------------------*/

void
Log::trace_in(const sockaddr *peer_addr, uint16_t peer_port, const char *format_string, ...)
{
  va_list ap;
  va_start(ap, format_string);
  trace_va(true, peer_addr, peer_port, format_string, ap);
  va_end(ap);
}

void
Log::trace_out(const sockaddr *peer_addr, uint16_t peer_port, const char *format_string, ...)
{
  va_list ap;
  va_start(ap, format_string);
  trace_va(false, peer_addr, peer_port, format_string, ap);
  va_end(ap);
}

void
Log::trace_va(bool in, const sockaddr *peer_addr, uint16_t peer_port, const char *format_string, va_list ap)
{
  if (!peer_addr || !format_string) {
    return;
  }

  char ip[INET6_ADDRSTRLEN];
  ats_ip_ntop(peer_addr, ip, sizeof(ip));

  struct timeval tp = ink_gettimeofday();

  Log::error("[%9d.%03d] Trace {0x%" PRIx64 "} %s %s:%d: ", (int)tp.tv_sec, (int)(tp.tv_usec / 1000), (uint64_t)ink_thread_self(),
             in ? "RECV" : "SEND", ip, peer_port);
  Log::va_error(format_string, ap);
  Log::error("[End Trace]\n");
}

/*-------------------------------------------------------------------------
  Log::preproc_thread_main

  This function defines the functionality of the logging flush preprocess
  thread, whose purpose is to consume full LogBuffer objects, do some prepare
  work (such as convert to ascii), and then forward to flush thread.
  -------------------------------------------------------------------------*/

void *
Log::preproc_thread_main(void *args)
{
  int idx = *(int *)args;

  Debug("log-preproc", "log preproc thread is alive ...");

  Log::preproc_notify[idx].lock();

  while (true) {
    if (unlikely(shutdown_event_system == true)) {
      return nullptr;
    }
    size_t buffers_preproced = 0;
    LogConfig *current       = (LogConfig *)configProcessor.get(log_configid);

    if (likely(current)) {
      buffers_preproced = current->log_object_manager.preproc_buffers(idx);

      // config->increment_space_used(bytes_to_disk);
      // TODO: the bytes_to_disk should be set to Log

      Debug("log-preproc", "%zu buffers preprocessed from LogConfig %p (refcount=%d) this round", buffers_preproced, current,
            current->refcount());

      configProcessor.release(log_configid, current);
    }

    // wait for more work; a spurious wake-up is ok since we'll just
    // check the queue and find there is nothing to do, then wait
    // again.
    //
    Log::preproc_notify[idx].wait();
  }

  /* NOTREACHED */
  Log::preproc_notify[idx].unlock();
  return nullptr;
}

void *
Log::flush_thread_main(void * /* args ATS_UNUSED */)
{
  LogBuffer *logbuffer;
  LogFlushData *fdata;
  ink_hrtime now, last_time = 0;
  int len, total_bytes;
  SLL<LogFlushData, LogFlushData::Link_link> link, invert_link;
  ProxyMutex *mutex = this_thread()->mutex.get();

  Log::flush_notify->lock();

  while (true) {
    if (unlikely(shutdown_event_system == true)) {
      return nullptr;
    }
    fdata = (LogFlushData *)ink_atomiclist_popall(flush_data_list);

    // invert the list
    //
    link.head = fdata;
    while ((fdata = link.pop())) {
      invert_link.push(fdata);
    }

    // process each flush data
    //
    while ((fdata = invert_link.pop())) {
      char *buf         = nullptr;
      int bytes_written = 0;
      LogFile *logfile  = fdata->m_logfile.get();

      if (logfile->m_file_format == LOG_FILE_BINARY) {
        logbuffer                      = (LogBuffer *)fdata->m_data;
        LogBufferHeader *buffer_header = logbuffer->header();

        buf         = (char *)buffer_header;
        total_bytes = buffer_header->byte_count;

      } else if (logfile->m_file_format == LOG_FILE_ASCII || logfile->m_file_format == LOG_FILE_PIPE) {
        buf         = (char *)fdata->m_data;
        total_bytes = fdata->m_len;

      } else {
        ink_release_assert(!"Unknown file format type!");
      }

      // make sure we're open & ready to write
      logfile->check_fd();
      if (!logfile->is_open()) {
        Warning("File:%s was closed, have dropped (%d) bytes.", logfile->get_name(), total_bytes);

        RecIncrRawStat(log_rsb, mutex->thread_holding, log_stat_bytes_lost_before_written_to_disk_stat, total_bytes);
        delete fdata;
        continue;
      }

      int logfilefd = logfile->get_fd();
      // This should always be true because we just checked it.
      ink_assert(logfilefd >= 0);

      // write *all* data to target file as much as possible
      //
      while (total_bytes - bytes_written) {
        if (Log::config->logging_space_exhausted) {
          Debug("log", "logging space exhausted, failed to write file:%s, have dropped (%d) bytes.", logfile->get_name(),
                (total_bytes - bytes_written));

          RecIncrRawStat(log_rsb, mutex->thread_holding, log_stat_bytes_lost_before_written_to_disk_stat,
                         total_bytes - bytes_written);
          break;
        }

        len = ::write(logfilefd, &buf[bytes_written], total_bytes - bytes_written);

        if (len < 0) {
          Error("Failed to write log to %s: [tried %d, wrote %d, %s]", logfile->get_name(), total_bytes - bytes_written,
                bytes_written, strerror(errno));

          RecIncrRawStat(log_rsb, mutex->thread_holding, log_stat_bytes_lost_before_written_to_disk_stat,
                         total_bytes - bytes_written);
          break;
        }
        Debug("log", "Successfully wrote some stuff to %s", logfile->get_name());
        bytes_written += len;
      }

      RecIncrRawStat(log_rsb, mutex->thread_holding, log_stat_bytes_written_to_disk_stat, bytes_written);

      if (logfile->m_log) {
        ink_atomic_increment(&logfile->m_log->m_bytes_written, bytes_written);
      }

      delete fdata;
    }

    // Time to work on periodic events??
    //
    now = Thread::get_hrtime() / HRTIME_SECOND;
    if (now >= last_time + periodic_tasks_interval) {
      Debug("log-preproc", "periodic tasks for %" PRId64, (int64_t)now);
      periodic_tasks(now);
      last_time = Thread::get_hrtime() / HRTIME_SECOND;
    }

    // wait for more work; a spurious wake-up is ok since we'll just
    // check the queue and find there is nothing to do, then wait
    // again.
    //
    Log::flush_notify->wait();
  }

  /* NOTREACHED */
  Log::flush_notify->unlock();
  return nullptr;
}

/*-------------------------------------------------------------------------
  Log::collate_thread_main

  This function defines the functionality of the log collation thread,
  whose purpose is to collate log buffers from other nodes.
  -------------------------------------------------------------------------*/

void *
Log::collate_thread_main(void * /* args ATS_UNUSED */)
{
  LogSock *sock;
  LogBufferHeader *header;
  LogFormat *format;
  LogObject *obj;
  int bytes_read;
  int sock_id;
  int new_client;

  Debug("log-thread", "Log collation thread is alive ...");

  Log::collate_notify.lock();

  while (true) {
    ink_assert(Log::config != nullptr);

    // wait on the collation condition variable until we're sure that
    // we're a collation host.  The while loop guards against spurious
    // wake-ups.
    //
    while (!Log::config->am_collation_host()) {
      Log::collate_notify.wait();
    }

    // Ok, at this point we know we're a log collation host, so get to
    // work.  We still need to keep checking whether we're a collation
    // host to account for a reconfiguration.
    //
    Debug("log-sock", "collation thread starting, creating LogSock");
    sock = new LogSock(LogSock::LS_CONST_MAX_CONNS);
    ink_assert(sock != nullptr);

    if (sock->listen(Log::config->collation_port) != 0) {
      LogUtils::manager_alarm(LogUtils::LOG_ALARM_ERROR, "Collation server error; could not listen on port %d",
                              Log::config->collation_port);
      Warning("Collation server error; could not listen on port %d", Log::config->collation_port);
      delete sock;
      //
      // go to sleep ...
      //
      Log::collate_notify.wait();
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
            Warning("Unauthorized client connecting to "
                    "log collation port; connection refused.");
            sock->close(new_client);
          }
        }
      }

      sock->check_connections();

      if (!sock->pending_message_any(&sock_id, 0)) {
        continue;
      }

      Debug("log-sock", "pending message ...");
      header = (LogBufferHeader *)sock->read_alloc(sock_id, &bytes_read);
      if (!header) {
        Debug("log-sock", "Error reading LogBuffer from collation client");
        continue;
      }

      if (header->version != LOG_SEGMENT_VERSION) {
        Note("Invalid LogBuffer received; invalid version - buffer = %u, current = %u", header->version, LOG_SEGMENT_VERSION);
        delete[] header;
        continue;
      }

      Debug("log-sock", "message accepted, size = %d", bytes_read);

      obj = match_logobject(header);
      if (!obj) {
        Note("LogObject not found with fieldlist id; "
             "writing LogBuffer to scrap file");
        obj = global_scrap_object;
      }

      format = obj->m_format;
      Debug("log-sock", "Using format '%s'", format->name());

      delete[] header;
    }

    Debug("log", "no longer collation host, deleting LogSock");
    delete sock;
  }

  /* NOTREACHED */
  Log::collate_notify.unlock();
  return nullptr;
}

/*-------------------------------------------------------------------------
  Log::match_logobject

  This routine matches the given buffer with the local list of LogObjects.
  If a match cannot be found, then we'll try to construct a local LogObject
  using the information provided in the header.  If all else fails, we
  return NULL.
  -------------------------------------------------------------------------*/

LogObject *
Log::match_logobject(LogBufferHeader *header)
{
  if (!header) {
    return nullptr;
  }

  LogObject *obj;
  obj = Log::config->log_object_manager.get_object_with_signature(header->log_object_signature);

  if (!obj) {
    // object does not exist yet, create it
    //
    LogFormat fmt("__collation_format__", header->fmt_fieldlist(), header->fmt_printf());

    if (fmt.valid()) {
      LogFileFormat file_format = header->log_object_flags & LogObject::BINARY ?
                                    LOG_FILE_BINARY :
                                    (header->log_object_flags & LogObject::WRITES_TO_PIPE ? LOG_FILE_PIPE : LOG_FILE_ASCII);

      obj = new LogObject(&fmt, Log::config->logfile_dir, header->log_filename(), file_format, nullptr,
                          (Log::RollingEnabledValues)Log::config->rolling_enabled, Log::config->collation_preproc_threads,
                          Log::config->rolling_interval_sec, Log::config->rolling_offset_hr, Log::config->rolling_size_mb, true);

      obj->set_remote_flag();

      if (Log::config->log_object_manager.manage_object(obj)) {
        // object manager can't solve filename conflicts
        // delete the object and return NULL
        //
        delete obj;
        obj = nullptr;
      }
    }
  }

  return obj;
}
