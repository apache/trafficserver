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
 *  CacheInspectorAllow.cc - Implementation to IP Access Control systtem
 *
 *
 ****************************************************************************/

#include "ink_config.h"
#include <stdio.h>
#include "Main.h"
#include "CacheInspectorAllow.h"
#include "ProxyConfig.h"
#include "StatSystem.h"
#include "P_EventSystem.h"
#include "P_Cache.h"

CacheInspectorAllow *cache_inspector_allow_table = NULL;
Ptr<ProxyMutex> cache_inspector_reconfig_mutex;

//
// struct CacheInspectorAllow_FreerContinuation
// Continuation to free old cache control lists after
//  a timeout
//
struct CacheInspectorAllow_FreerContinuation;
typedef int (CacheInspectorAllow_FreerContinuation::*CacheInspectorAllow_FrContHandler) (int, void *);
struct CacheInspectorAllow_FreerContinuation:Continuation
{
  CacheInspectorAllow *p;
  int freeEvent(int event, Event * e)
  {
    NOWARN_UNUSED(event);
    NOWARN_UNUSED(e);
    Debug("cache-inspector-allow", "Deleting old table");
    delete p;
    delete this;
      return EVENT_DONE;
  }
  CacheInspectorAllow_FreerContinuation(CacheInspectorAllow * ap):Continuation(NULL), p(ap)
  {
    SET_HANDLER((CacheInspectorAllow_FrContHandler) & CacheInspectorAllow_FreerContinuation::freeEvent);
  }
};

// struct CacheInspectorAllow_UpdateContinuation
//
//   Used to read the ip_allow.conf file after the manager signals
//      a change
//
struct CacheInspectorAllow_UpdateContinuation:Continuation
{
  int file_update_handler(int etype, void *data)
  {
    NOWARN_UNUSED(etype);
    NOWARN_UNUSED(data);
    Debug("cache-inspector-allow", "reloading mgmt_allow.config");
    reloadCacheInspectorAllow();
    delete this;
      return EVENT_DONE;
  }
  CacheInspectorAllow_UpdateContinuation(ProxyMutex * m):Continuation(m)
  {
    SET_HANDLER(&CacheInspectorAllow_UpdateContinuation::file_update_handler);
  }


};

int
CacheInspectorAllowFile_CB(const char *config_name, RecDataT type, RecData data, void *cookie)
{
  NOWARN_UNUSED(cookie);
  NOWARN_UNUSED(type);
  NOWARN_UNUSED(config_name);

  eventProcessor.schedule_imm(NEW(new CacheInspectorAllow_UpdateContinuation(cache_inspector_reconfig_mutex)),
                              ET_CACHE);
  return 0;
}

//
//   Begin API functions
//
void
initCacheInspectorAllow()
{

  // Should not have been initialized before
  ink_assert(cache_inspector_allow_table == NULL);

  cache_inspector_reconfig_mutex = new_ProxyMutex();

  cache_inspector_allow_table = NEW(new CacheInspectorAllow("proxy.config.admin.ip_allow.filename",
                                                            "CacheInspectorAllow", "ip_allow"));
  cache_inspector_allow_table->BuildTable();

  REC_RegisterConfigUpdateFunc("proxy.config.admin.ip_allow.filename", CacheInspectorAllowFile_CB, NULL);
}

void
reloadCacheInspectorAllow()
{
  CacheInspectorAllow *new_table;

  Debug("cache-inspector-allow", "mgmt_allow.config updated, reloading");

  // Schedule the current table for deallocation in the future
  eventProcessor.schedule_in(NEW(new CacheInspectorAllow_FreerContinuation(cache_inspector_allow_table)),
                             IP_ALLOW_TIMEOUT, ET_CACHE);

  new_table = NEW(new CacheInspectorAllow("proxy.config.admin.ip_allow.filename", "CacheInspectorAllow", "mgmt_allow"));
  new_table->BuildTable();

  ink_atomic_swap_ptr(&cache_inspector_allow_table, new_table);
}

//
//   End API functions
//


CacheInspectorAllow::CacheInspectorAllow(const char *config_var, const char *name, const char *action_val):
IpLookup(name),
config_file_var(config_var),
module_name(name),
action(action_val),
err_allow_all(false)
{

  char *config_file;

  config_file_var = xstrdup(config_var);

  REC_ReadConfigStringAlloc(config_file, (char *) config_file_var);
  ink_release_assert(config_file != NULL);
  // XXX: If this is config directive it might contain
  //      absolute path for config_file_val ?
  ink_filepath_make(config_file_path, sizeof(config_file_path), system_config_directory, config_file);
  xfree(config_file);
}

CacheInspectorAllow::~CacheInspectorAllow()
{
}

void
CacheInspectorAllow::Print()
{
  // TODO: Use log directives instead directly printing to the stdout
  //
  printf("CacheInspectorAllow Table with %d entries\n", num_el);
  if (err_allow_all == true) {
    printf("\t err_allow_all is true\n");
  }
  IpLookup::Print();
}

int
CacheInspectorAllow::BuildTable()
{
  char *tok_state = NULL;
  char *line = NULL;
  const char *errPtr = NULL;
  char errBuf[1024];
  char *file_buf = NULL;
  int line_num = 0;
  ip_addr_t addr1 = 0;
  ip_addr_t addr2 = 0;
  matcher_line line_info;
  bool alarmAlready = false;

  // Table should be empty
  ink_assert(num_el == 0);

  file_buf = readIntoBuffer(config_file_path, module_name, NULL);

  if (file_buf == NULL) {
    err_allow_all = false;
    Warning("%s Failed to read %s. All IP Address will be allowed", module_name, config_file_path);
    return 1;
  }

  line = tokLine(file_buf, &tok_state);
  while (line != NULL) {

    line_num++;

    // skip all blank spaces at beginning of line
    while (*line && isspace(*line)) {
      line++;
    }

    if (*line != '\0' && *line != '#') {

      errPtr = parseConfigLine(line, &line_info, &ip_allow_tags);

      if (errPtr != NULL) {
        snprintf(errBuf, sizeof(errBuf), "%s discarding %s entry at line %d : %s",
                 module_name, config_file_path, line_num, errPtr);
        SignalError(errBuf, alarmAlready);
      } else {

        ink_assert(line_info.type == MATCH_IP);

        errPtr = ExtractIpRange(line_info.line[1][line_info.dest_entry], &addr1, &addr2);

        if (errPtr != NULL) {
          snprintf(errBuf, sizeof(errBuf), "%s discarding %s entry at line %d : %s",
                   module_name, config_file_path, line_num, errPtr);
          SignalError(errBuf, alarmAlready);
        } else {
          this->NewEntry(addr1, addr2, NULL);
        }
      }
    }

    line = tokLine(NULL, &tok_state);
  }

  if (num_el == 0) {
    Warning("%s No entries in %s. All IP Address will be allowed", module_name, config_file_path);
    err_allow_all = false;
  }

  if (is_debug_tag_set("ip-allow")) {
    Print();
  }

  xfree(file_buf);
  return 0;
}
