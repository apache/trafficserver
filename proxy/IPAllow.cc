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

#include "ts/ink_platform.h"
#include "Main.h"
#include "IPAllow.h"
#include "ProxyConfig.h"
#include "P_EventSystem.h"
#include "P_Cache.h"
#include "hdrs/HdrToken.h"

#include <sstream>

enum AclOp {
  ACL_OP_ALLOW, ///< Allow access.
  ACL_OP_DENY,  ///< Deny access.
};

const AclRecord IpAllow::ALL_METHOD_ACL(AclRecord::ALL_METHOD_MASK);

int IpAllow::configid = 0;

static ConfigUpdateHandler<IpAllow> *ipAllowUpdate;

//
//   Begin API functions
//
void
IpAllow::startup()
{
  // Should not have been initialized before
  ink_assert(IpAllow::configid == 0);

  ipAllowUpdate = new ConfigUpdateHandler<IpAllow>();
  ipAllowUpdate->attach("proxy.config.cache.ip_allow.filename");

  reconfigure();
}

void
IpAllow::reconfigure()
{
  self *new_table;

  Note("ip_allow.config updated, reloading");

  new_table = new self("proxy.config.cache.ip_allow.filename", "IpAllow", "ip_allow");
  new_table->BuildTable();

  configid = configProcessor.set(configid, new_table);
}

IpAllow *
IpAllow::acquire()
{
  return (IpAllow *)configProcessor.get(configid);
}

void
IpAllow::release(IpAllow *lookup)
{
  configProcessor.release(configid, lookup);
}

//
//   End API functions
//

IpAllow::IpAllow(const char *config_var, const char *name, const char *action_val) : module_name(name), action(action_val)
{
  ats_scoped_str config_path(RecConfigReadConfigPath(config_var));

  config_file_path[0] = '\0';
  ink_release_assert(config_path);

  ink_strlcpy(config_file_path, config_path, sizeof(config_file_path));
}

IpAllow::~IpAllow()
{
}

void
IpAllow::Print()
{
  std::ostringstream s;
  s << _map.getCount() << " ACL entries";
  s << '.';
  for (IpMap::iterator spot(_map.begin()), limit(_map.end()); spot != limit; ++spot) {
    char text[INET6_ADDRSTRLEN];
    AclRecord const *ar = static_cast<AclRecord const *>(spot->data());

    s << std::endl << "  Line " << ar->_src_line << ": " << ats_ip_ntop(spot->min(), text, sizeof text);
    if (0 != ats_ip_addr_cmp(spot->min(), spot->max())) {
      s << " - " << ats_ip_ntop(spot->max(), text, sizeof text);
    }
    s << " method=";
    uint32_t mask = AclRecord::ALL_METHOD_MASK & ar->_method_mask;
    if (AclRecord::ALL_METHOD_MASK == mask) {
      s << "ALL";
    } else if (0 == mask) {
      s << "NONE";
    } else {
      bool leader        = false; // need leading vbar?
      uint32_t test_mask = 1;     // mask for current method.
      for (int i = 0; i < HTTP_WKSIDX_METHODS_CNT; ++i, test_mask <<= 1) {
        if (mask & test_mask) {
          if (leader)
            s << '|';
          s << hdrtoken_index_to_wks(i + HTTP_WKSIDX_CONNECT);
          leader = true;
        }
      }
    }
    if (!ar->_nonstandard_methods.empty()) {
      s << " nonstandard method=";
      bool leader = false; // need leading vbar?
      for (AclRecord::MethodSet::iterator iter = ar->_nonstandard_methods.begin(), end = ar->_nonstandard_methods.end();
           iter != end; ++iter) {
        if (leader) {
          s << '|';
        }
        s << *iter;
        leader = true;
      }
    }
  }
  Debug("ip-allow", "%s", s.str().c_str());
}

int
IpAllow::BuildTable()
{
  char *tok_state    = NULL;
  char *line         = NULL;
  const char *errPtr = NULL;
  char errBuf[1024];
  char *file_buf = NULL;
  int line_num   = 0;
  IpEndpoint addr1;
  IpEndpoint addr2;
  matcher_line line_info;
  bool alarmAlready = false;

  // Table should be empty
  ink_assert(_map.getCount() == 0);

  file_buf = readIntoBuffer(config_file_path, module_name, NULL);

  if (file_buf == NULL) {
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
        snprintf(errBuf, sizeof(errBuf), "%s discarding %s entry at line %d : %s", module_name, config_file_path, line_num, errPtr);
        SignalError(errBuf, alarmAlready);
      } else {
        ink_assert(line_info.type == MATCH_IP);

        errPtr = ExtractIpRange(line_info.line[1][line_info.dest_entry], &addr1.sa, &addr2.sa);

        if (errPtr != NULL) {
          snprintf(errBuf, sizeof(errBuf), "%s discarding %s entry at line %d : %s", module_name, config_file_path, line_num,
                   errPtr);
          SignalError(errBuf, alarmAlready);
        } else {
          // INKqa05845
          // Search for "action=ip_allow method=PURGE method=GET ..." or "action=ip_deny method=PURGE method=GET ...".
          char *label, *val;
          uint32_t acl_method_mask = 0;
          AclRecord::MethodSet nonstandard_methods;
          bool deny_nonstandard_methods = false;
          AclOp op                      = ACL_OP_DENY; // "shut up", I explained to the compiler.
          bool op_found = false, method_found = false;
          for (int i = 0; i < MATCHER_MAX_TOKENS; i++) {
            label = line_info.line[0][i];
            val   = line_info.line[1][i];
            if (label == NULL) {
              continue;
            }
            if (strcasecmp(label, "action") == 0) {
              if (strcasecmp(val, "ip_allow") == 0) {
                op_found = true, op = ACL_OP_ALLOW;
              } else if (strcasecmp(val, "ip_deny") == 0) {
                op_found = true, op = ACL_OP_DENY;
              }
            }
          }
          if (op_found) {
            // Loop again for methods, (in case action= appears after method=)
            for (int i = 0; i < MATCHER_MAX_TOKENS; i++) {
              label = line_info.line[0][i];
              val   = line_info.line[1][i];
              if (label == NULL) {
                continue;
              }
              if (strcasecmp(label, "method") == 0) {
                char *method_name, *sep_ptr = 0;
                // Parse method="GET|HEAD"
                for (method_name = strtok_r(val, "|", &sep_ptr); method_name != NULL; method_name = strtok_r(NULL, "|", &sep_ptr)) {
                  if (strcasecmp(method_name, "ALL") == 0) {
                    method_found = false; // in case someone does method=GET|ALL
                    break;
                  } else {
                    int method_name_len = strlen(method_name);
                    int method_idx      = hdrtoken_tokenize(method_name, method_name_len);
                    if (method_idx < HTTP_WKSIDX_CONNECT || method_idx >= HTTP_WKSIDX_CONNECT + HTTP_WKSIDX_METHODS_CNT) {
                      nonstandard_methods.insert(method_name);
                      Debug("ip-allow", "Found nonstandard method [%s] on line %d", method_name, line_num);
                    } else { // valid method.
                      acl_method_mask |= AclRecord::MethodIdxToMask(method_idx);
                    }
                    method_found = true;
                  }
                }
              }
            }
            // If method not specified, default to ALL
            if (!method_found) {
              method_found    = true;
              acl_method_mask = AclRecord::ALL_METHOD_MASK;
              nonstandard_methods.clear();
            }
            // When deny, use bitwise complement.  (Make the rule 'allow for all
            // methods except those specified')
            if (op == ACL_OP_DENY) {
              acl_method_mask          = AclRecord::ALL_METHOD_MASK & ~acl_method_mask;
              deny_nonstandard_methods = true;
            }
          }

          if (method_found) {
            _acls.push_back(AclRecord(acl_method_mask, line_num, nonstandard_methods, deny_nonstandard_methods));
            // Color with index because at this point the address
            // is volatile.
            _map.fill(&addr1, &addr2, reinterpret_cast<void *>(_acls.length() - 1));
          } else {
            snprintf(errBuf, sizeof(errBuf), "%s discarding %s entry at line %d : %s", module_name, config_file_path, line_num,
                     "Invalid action/method specified"); // changed by YTS Team, yamsat bug id -59022
            SignalError(errBuf, alarmAlready);
          }
        }
      }
    }

    line = tokLine(NULL, &tok_state);
  }

  if (_map.getCount() == 0) {
    Warning("%s No entries in %s. All IP Addresses will be blocked", module_name, config_file_path);
  } else {
    // convert the coloring from indices to pointers.
    for (IpMap::iterator spot(_map.begin()), limit(_map.end()); spot != limit; ++spot) {
      spot->setData(&_acls[reinterpret_cast<size_t>(spot->data())]);
    }
  }

  if (is_debug_tag_set("ip-allow")) {
    Print();
  }

  ats_free(file_buf);
  return 0;
}
