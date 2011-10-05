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

#include <sstream>

#define IPAllowRegisterConfigUpdateFunc REC_RegisterConfigUpdateFunc
#define IPAllowReadConfigStringAlloc REC_ReadConfigStringAlloc

IpAllow* IpAllow::_instance = NULL;

static Ptr<ProxyMutex> ip_reconfig_mutex;

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
    IpAllow::ReloadInstance();
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
IpAllow::InitInstance() {
  // Should not have been initialized before
  ink_assert(_instance == NULL);

  ip_reconfig_mutex = new_ProxyMutex();

  _instance = NEW(new self("proxy.config.cache.ip_allow.filename", "IpAllow", "ip_allow"));
  _instance->BuildTable();

  IPAllowRegisterConfigUpdateFunc("proxy.config.cache.ip_allow.filename", ipAllowFile_CB, NULL);
}

void
IpAllow::ReloadInstance() {
  self *new_table;

  Debug("ip_allow", "ip_allow.config updated, reloading");

  // Schedule the current table for deallocation in the future
  eventProcessor.schedule_in(NEW(new IPAllow_FreerContinuation(_instance)), IP_ALLOW_TIMEOUT, ET_CACHE);

  new_table = NEW(new self("proxy.config.cache.ip_allow.filename", "IpAllow", "ip_allow"));
  new_table->BuildTable();

  ink_atomic_swap_ptr(_instance, new_table);
}

//
//   End API functions
//


IpAllow::IpAllow(
  const char *config_var,
  const char *name,
  const char *action_val
) : config_file_var(config_var),
    module_name(name),
    action(action_val),
    _allow_all(false)
{

  char *config_file;

  config_file_var = ats_strdup(config_var);
  config_file_path[0] = '\0';

  IPAllowReadConfigStringAlloc(config_file, (char *) config_file_var);
  ink_release_assert(config_file != NULL);
  ink_filepath_make(config_file_path, sizeof(config_file_path), system_config_directory, config_file);
  ats_free(config_file);
}

IpAllow::~IpAllow()
{
}

void
IpAllow::Print() {
  std::ostringstream s;
  s << _map.getCount() << " ACL entries";
  if (_allow_all) s << " - ACLs are disabled, all connections are permitted";
  s << '.';
  for ( IpMap::iterator spot(_map.begin()), limit(_map.end())
      ; spot != limit
      ; ++spot
  ) {
    char text[INET6_ADDRSTRLEN];
    AclRecord const* ar = static_cast<AclRecord const*>(spot->data());

    s << std::endl << "  Line " << ar->_src_line << ": "
      << (ACL_OP_ALLOW == ar->_op ? "allow " : "deny  ")
      << ink_inet_ntop(spot->min(), text, sizeof text)
      ;
    if (0 != ink_inet_cmp(spot->min(), spot->max())) {
      s << " - " << ink_inet_ntop(spot->max(), text, sizeof text);
    }
  }
  Debug("ip-allow", "%s", s.str().c_str());
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
  ts_ip_endpoint addr1;
  ts_ip_endpoint addr2;
  matcher_line line_info;
  bool alarmAlready = false;

  // Table should be empty
  ink_assert(_map.getCount() == 0);

  file_buf = readIntoBuffer(config_file_path, module_name, NULL);

  if (file_buf == NULL) {
    _allow_all = false;
    Warning("%s Failed to read %s. All IP Addresses will be blocked", module_name, config_file_path);
    return 1;
  }

  line = tokLine(file_buf, &tok_state);
  while (line != NULL) {

    ++line_num;

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

        errPtr = ExtractIpRange(line_info.line[1][line_info.dest_entry], &addr1.sa, &addr2.sa);

        if (errPtr != NULL) {
          snprintf(errBuf, sizeof(errBuf), "%s discarding %s entry at line %d : %s",
                   module_name, config_file_path, line_num, errPtr);
          SignalError(errBuf, alarmAlready);
        } else {
          // INKqa05845
          // Search for "action=ip_allow" or "action=ip_deny".
          char *label, *val;
          for (int i = 0; i < MATCHER_MAX_TOKENS; i++) {
            label = line_info.line[0][i];
            val = line_info.line[1][i];
            if (label == NULL)
              continue;
            if (strcasecmp(label, "action") == 0) {
              bool found = false;
              AclOp op;
              if (strcasecmp(val, "ip_allow") == 0)
                found = true, op = ACL_OP_ALLOW;
              else if (strcasecmp(val, "ip_deny") == 0)
                found = true, op = ACL_OP_DENY;
              if (found) {
                _acls.push_back(AclRecord(op, line_num));
                // Color with index because at this point the address
                // is volatile.
                _map.mark(
                  &addr1.sa, &addr2.sa,
                  reinterpret_cast<void*>(_acls.size()-1)
                );
              } else {
                snprintf(errBuf, sizeof(errBuf), "%s discarding %s entry at line %d : %s", module_name, config_file_path, line_num, "Invalid action specified");        //changed by YTS Team, yamsat bug id -59022
                SignalError(errBuf, alarmAlready);
              }
            }
          }
        }
      }
    }

    line = tokLine(NULL, &tok_state);
  }

  if (_map.getCount() == 0) {
    Warning("%s No entries in %s. All IP Addresses will be blocked", module_name, config_file_path);
    _allow_all = false;
  } else {
    // convert the coloring from indices to pointers.
    for ( IpMap::iterator spot(_map.begin()), limit(_map.end())
        ; spot != limit
        ; ++spot
    ) {
      spot->setData(&_acls[reinterpret_cast<size_t>(spot->data())]);
    }
  }

  if (is_debug_tag_set("ip-allow")) {
    Print();
  }

  ats_free(file_buf);
  return 0;
}
