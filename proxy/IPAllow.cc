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
 *  IPAllow.cc - Implementation to IP Access Control systtem
 *
 *
 ****************************************************************************/

#include "libts.h"
#include "Main.h"
#include "IPAllow.h"
#include "ProxyConfig.h"
#include "StatSystem.h"
#include "P_EventSystem.h"
#include "P_Cache.h"

#define IPAllowRegisterConfigUpdateFunc REC_RegisterConfigUpdateFunc
#define IPAllowReadConfigStringAlloc REC_ReadConfigStringAlloc

IpAllow *ip_allow_table = NULL;
Ptr<ProxyMutex> ip_reconfig_mutex;

//
// struct IPAllow_FreerContinuation
// Continuation to free old cache control lists after
//  a timeout
//
struct IPAllow_FreerContinuation;
typedef int (IPAllow_FreerContinuation::*IPAllow_FrContHandler) (int, void *);
struct IPAllow_FreerContinuation: public Continuation
{
  IpAllow *p;
  int freeEvent(int event, Event * e)
  {
    NOWARN_UNUSED(event);
    NOWARN_UNUSED(e);
    Debug("ip-allow", "Deleting old table");
    delete p;
    delete this;
      return EVENT_DONE;
  }
  IPAllow_FreerContinuation(IpAllow * ap):Continuation(NULL), p(ap)
  {
    SET_HANDLER((IPAllow_FrContHandler) & IPAllow_FreerContinuation::freeEvent);
  }
};

// struct IPAllow_UpdateContinuation
//
//   Used to read the ip_allow.conf file after the manager signals
//      a change
//
struct IPAllow_UpdateContinuation: public Continuation
{
  int file_update_handler(int etype, void *data)
  {
    NOWARN_UNUSED(etype);
    NOWARN_UNUSED(data);
    reloadIPAllow();
    delete this;
      return EVENT_DONE;
  }
  IPAllow_UpdateContinuation(ProxyMutex * m):Continuation(m)
  {
    SET_HANDLER(&IPAllow_UpdateContinuation::file_update_handler);
  }
};

int
ipAllowFile_CB(const char *name, RecDataT data_type, RecData data, void *cookie)
{
  NOWARN_UNUSED(name);
  NOWARN_UNUSED(data_type);
  NOWARN_UNUSED(data);
  NOWARN_UNUSED(cookie);
  eventProcessor.schedule_imm(NEW(new IPAllow_UpdateContinuation(ip_reconfig_mutex)), ET_CACHE);
  return 0;
}

//
//   Begin API functions
//
void
initIPAllow()
{

  // Should not have been initialized before
  ink_assert(ip_allow_table == NULL);

  ip_reconfig_mutex = new_ProxyMutex();

  ip_allow_table = NEW(new IpAllow("proxy.config.cache.ip_allow.filename", "IpAllow", "ip_allow"));
  ip_allow_table->BuildTable();

  IPAllowRegisterConfigUpdateFunc("proxy.config.cache.ip_allow.filename", ipAllowFile_CB, NULL);
}

void
reloadIPAllow()
{
  IpAllow *new_table;

  Debug("ip_allow", "ip_allow.config updated, reloading");

  // Schedule the current table for deallocation in the future
  eventProcessor.schedule_in(NEW(new IPAllow_FreerContinuation(ip_allow_table)), IP_ALLOW_TIMEOUT, ET_CACHE);

  new_table = NEW(new IpAllow("proxy.config.cache.ip_allow.filename", "IpAllow", "ip_allow"));
  new_table->BuildTable();

  ink_atomic_swap_ptr(&ip_allow_table, new_table);
}

//
//   End API functions
//


IpAllow::IpAllow(const char *config_var, const char *name, const char *action_val):
IpLookup(name),
config_file_var(config_var),
module_name(name),
action(action_val),
err_allow_all(false)
{

  char *config_file;

  config_file_var = xstrdup(config_var);
  config_file_path[0] = '\0';

  IPAllowReadConfigStringAlloc(config_file, (char *) config_file_var);
  ink_release_assert(config_file != NULL);
  ink_filepath_make(config_file_path, sizeof(config_file_path), system_config_directory, config_file);
  xfree(config_file);
}

IpAllow::~IpAllow()
{
}

void
IpAllow::Print()
{
  printf("IpAllow Table with %d entries\n", num_el);
  if (err_allow_all == true) {
    printf("\t err_allow_all is true\n");
  }
  IpLookup::Print();
}

int
IpAllow::BuildTable()
{
  char *tok_state = NULL;
  char *line = NULL;
  const char *errPtr = NULL;
  char errBuf[1024];
  char *file_buf = NULL;
  int line_num = 0;
  in_addr_t addr1 = 0;
  in_addr_t addr2 = 0;
  matcher_line line_info;
  bool alarmAlready = false;

  // Table should be empty
  ink_assert(num_el == 0);

  file_buf = readIntoBuffer(config_file_path, module_name, NULL);

  if (file_buf == NULL) {
    err_allow_all = false;
    Warning("%s Failed to read %s. All IP Addresses will be blocked", module_name, config_file_path);
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
          // INKqa05845
          // Search for "action=ip_allow" or "action=ip_deny".
          char *label, *val;
          IpAllowRecord *rec;
          for (int i = 0; i < MATCHER_MAX_TOKENS; i++) {
            label = line_info.line[0][i];
            val = line_info.line[1][i];
            if (label == NULL)
              continue;
            if (strcasecmp(label, "action") == 0) {
              if (strcasecmp(val, "ip_allow") == 0) {
                rec = (IpAllowRecord *) xmalloc(sizeof(IpAllowRecord));
                rec->access = IP_ALLOW;
                rec->line_num = line_num;
                this->NewEntry(addr1, addr2, (void *) rec);
              } else if (strcasecmp(val, "ip_deny") == 0) {
                rec = (IpAllowRecord *) xmalloc(sizeof(IpAllowRecord));
                rec->access = IP_DENY;
                rec->line_num = line_num;
                this->NewEntry(addr1, addr2, (void *) rec);
              } else {
                snprintf(errBuf, sizeof(errBuf), "%s discarding %s entry at line %d : %s", module_name, config_file_path, line_num, "Invalid action specified");        //changed by YTS Team, yamsat bug id -59022
                SignalError(errBuf, alarmAlready);
              }
            }
          }
          // this->NewEntry(addr1, addr2, NULL);
        }
      }
    }

    line = tokLine(NULL, &tok_state);
  }

  if (num_el == 0) {
    Warning("%s No entries in %s. All IP Addresses will be blocked", module_name, config_file_path);
    err_allow_all = false;
  }

  if (is_debug_tag_set("ip-allow")) {
    Print();
  }

  xfree(file_buf);
  return 0;
}
