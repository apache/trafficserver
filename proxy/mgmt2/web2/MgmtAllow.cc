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

#include "ink_config.h"
#include "ink_platform.h"
#include "MgmtAllow.h"
#include "Main.h"

// Make sure we have LOCAL_MANAGER defined for MatcherUtils.h
#ifndef LOCAL_MANAGER
#define LOCAL_MANAGER
#endif
#include "MatcherUtils.h"

// Get rid of previous definition first ... /leif
#undef SignalError
#define SignalError(_buf, _already)                                     \
{                                                                       \
  if(_already == false) lmgmt->alarm_keeper->signalAlarm(MGMT_ALARM_PROXY_CONFIG_ERROR, _buf); \
  _already = true;                                                      \
  mgmt_log(stderr, _buf);                                               \
}                                                                       \


MgmtAllow *mgmt_allow_table = NULL;

MgmtAllow::MgmtAllow(const char *config_var, const char *name, const char *action_val):
IpLookup(name),
config_file_var(config_var),
module_name(name),
action(action_val),
err_allow_all(false)
{

  bool found;
  char *config_file = NULL;
  char *config_dir = NULL;
  struct stat s;
  int err;

  config_file_var = xstrdup(config_var);
  config_file_path[0] = '\0';

  found = (RecGetRecordString_Xmalloc((char *) config_file_var, &config_file) == REC_ERR_OKAY);
  if (found == false) {
    mgmt_log(stderr, "%s WARNING: Unable to read variable %s.  All IP Addresses will be blocked\n");
    return;
  }

  found = (RecGetRecordString_Xmalloc("proxy.config.config_dir", &config_dir) == REC_ERR_OKAY);
  if (found == false) {
    xfree(config_file);
    mgmt_log(stderr, "%s WARNING: Unable to locate config dir.  All IP Addresses will be blocked\n");
    return;
  }

  if ((err = stat(config_dir, &s)) < 0) {
    xfree(config_dir);
    config_dir = xstrdup(system_config_directory);
    if ((err = stat(config_dir, &s)) < 0) {
      mgmt_log(stderr, "%s WARNING: Unable to locate config dir %s.  All IP Addresses will be blocked\n",config_dir);
      return;
    }
  }

  if (strlen(config_file) + strlen(config_dir) + 1 > PATH_NAME_MAX) {
    mgmt_log(stderr, "%s WARNING: Illegal config file name %s.  All IP Addresses will be blocked\n", config_file);
  } else {
    ink_strncpy(config_file_path, config_dir, sizeof(config_file_path));
    strncat(config_file_path, "/", sizeof(config_file_path) - strlen(config_file_path) - 1);
    strncat(config_file_path, config_file, sizeof(config_file_path) - strlen(config_file_path) - 1);
  }

  xfree(config_file);
  xfree(config_dir);
}

MgmtAllow::~MgmtAllow()
{
}

void
MgmtAllow::Print()
{
  printf("MgmtAllow Table with %d entries\n", num_el);
  if (err_allow_all == true) {
    printf("\t err_allow_all is true\n");
  }
  IpLookup::Print();
}

int
MgmtAllow::BuildTable()
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

  // Make sure we were able to find the location of the configuration
  //  file
  if (*config_file_path == '\0') {
    err_allow_all = false;
    return 1;
  }

  file_buf = readIntoBuffer(config_file_path, module_name, NULL);

  if (file_buf == NULL) {
    err_allow_all = false;
    mgmt_log(stderr, "%s Failed to read %s. All IP Addresses will be blocked\n", module_name, config_file_path);
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
        snprintf(errBuf, sizeof(errBuf), "%s discarding %s entry at line %d : %s\n",
                 module_name, config_file_path, line_num, errPtr);
        SignalError(errBuf, alarmAlready);
      } else {

        ink_assert(line_info.type == MATCH_IP);

        errPtr = ExtractIpRange(line_info.line[1][line_info.dest_entry], &addr1, &addr2);

        if (errPtr != NULL) {
          snprintf(errBuf, sizeof(errBuf), "%s discarding %s entry at line %d : %s\n",
                   module_name, config_file_path, line_num, errPtr);
          SignalError(errBuf, alarmAlready);
        } else {
          // INKqa05845
          // Search for "action=ip_allow" or "action=ip_deny".
          char *label, *val;
          MgmtAllowRecord *rec;
          for (int i = 0; i < MATCHER_MAX_TOKENS; i++) {
            label = line_info.line[0][i];
            val = line_info.line[1][i];
            if (label == NULL)
              continue;
            if (strcasecmp(label, "action") == 0) {
              if (strcasecmp(val, "ip_allow") == 0) {
                rec = (MgmtAllowRecord *) xmalloc(sizeof(MgmtAllowRecord));
                rec->access = MGMT_ALLOW;
                rec->line_num = line_num;
                this->NewEntry(addr1, addr2, (void *) rec);
              } else if (strcasecmp(val, "ip_deny") == 0) {
                rec = (MgmtAllowRecord *) xmalloc(sizeof(MgmtAllowRecord));
                rec->access = MGMT_DENY;
                rec->line_num = line_num;
                this->NewEntry(addr1, addr2, (void *) rec);
              } else {
                snprintf(errBuf, sizeof(errBuf), "%s discarding %s entry at line %d : %s",
                         module_name, config_file_path, line_num, errPtr);
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
    mgmt_log(stderr, "%s No entries in %s. All IP Addresses will be blocked\n", module_name, config_file_path);
    err_allow_all = false;
  }

  xfree(file_buf);
  return 0;
}
