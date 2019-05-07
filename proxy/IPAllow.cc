/** @file

  User agent control by static IP address.

  This enables specifying the set of methods usable by a user agent based on the remove IP address
  for a user agent connection.

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

#include <sstream>
#include "IPAllow.h"
#include "tscore/BufferWriter.h"

extern char *readIntoBuffer(const char *file_path, const char *module_name, int *read_size_ptr);

using ts::TextView;
namespace
{
void
SignalError(ts::BufferWriter &w, bool &flag)
{
  if (!flag) {
    flag = true;
    pmgmt->signalManager(MGMT_SIGNAL_CONFIG_ERROR, w.data());
  }
  Error("%s", w.data());
}
} // namespace

enum AclOp {
  ACL_OP_ALLOW, ///< Allow access.
  ACL_OP_DENY,  ///< Deny access.
};

const IpAllow::Record IpAllow::ALLOW_ALL_RECORD(ALL_METHOD_MASK);
const IpAllow::ACL IpAllow::DENY_ALL_ACL;

size_t IpAllow::configid     = 0;
bool IpAllow::accept_check_p = true; // initializing global flag for fast deny

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
  self_type *new_table;

  Note("ip_allow.config loading ...");

  new_table = new self_type("proxy.config.cache.ip_allow.filename");
  new_table->BuildTable();

  configid = configProcessor.set(configid, new_table);

  Note("ip_allow.config finished loading");
}

IpAllow *
IpAllow::acquire()
{
  return static_cast<IpAllow *>(configProcessor.get(configid));
}

void
IpAllow::release(IpAllow *config)
{
  configProcessor.release(configid, config);
}

void
IpAllow::release()
{
  configProcessor.release(configid, this);
}

IpAllow::ACL
IpAllow::match(sockaddr const *ip, match_key_t key)
{
  self_type *self = acquire();
  void *raw       = nullptr;
  if (SRC_ADDR == key) {
    self->_src_map.contains(ip, &raw);
    Record *r = static_cast<Record *>(raw);
    // Special check - if checking in accept is enabled and the record is a deny all,
    // then return a missing record instead to force an immediate deny. Otherwise it's delayed
    // until after remap, to allow remap rules to tweak the result.
    if (raw && r->_method_mask == 0 && r->_nonstandard_methods.empty() && accept_check_p) {
      raw = nullptr;
    }
  } else {
    self->_dst_map.contains(ip, &raw);
  }
  if (raw == nullptr) {
    self->release();
    self = nullptr;
  }
  return ACL{static_cast<Record *>(raw), self};
}

//
//   End API functions
//

IpAllow::IpAllow(const char *config_var) : config_file_path(RecConfigReadConfigPath(config_var)) {}

void
IpAllow::PrintMap(IpMap *map)
{
  std::ostringstream s;
  s << map->count() << " ACL entries.";
  for (auto &spot : *map) {
    char text[INET6_ADDRSTRLEN];
    Record const *ar = static_cast<Record const *>(spot.data());

    s << std::endl << "  Line " << ar->_src_line << ": " << ats_ip_ntop(spot.min(), text, sizeof text);
    if (0 != ats_ip_addr_cmp(spot.min(), spot.max())) {
      s << " - " << ats_ip_ntop(spot.max(), text, sizeof text);
    }
    s << " method=";
    uint32_t mask = ALL_METHOD_MASK & ar->_method_mask;
    if (ALL_METHOD_MASK == mask) {
      s << "ALL";
    } else if (0 == mask) {
      s << "NONE";
    } else {
      bool leader        = false; // need leading vbar?
      uint32_t test_mask = 1;     // mask for current method.
      for (int i = 0; i < HTTP_WKSIDX_METHODS_CNT; ++i, test_mask <<= 1) {
        if (mask & test_mask) {
          if (leader) {
            s << '|';
          }
          s << hdrtoken_index_to_wks(i + HTTP_WKSIDX_CONNECT);
          leader = true;
        }
      }
    }
    if (!ar->_nonstandard_methods.empty()) {
      s << " other methods=";
      bool leader = false; // need leading vbar?
      for (const auto &_nonstandard_method : ar->_nonstandard_methods) {
        if (leader) {
          s << '|';
        }
        s << _nonstandard_method;
        leader = true;
      }
    }
  }
  Debug("ip-allow", "%s", s.str().c_str());
}

void
IpAllow::Print()
{
  Debug("ip-allow", "Printing src map");
  PrintMap(&_src_map);
  Debug("ip-allow", "Printing dest map");
  PrintMap(&_dst_map);
}

int
IpAllow::BuildTable()
{
  int file_size = 0;
  int line_num  = 0;
  IpAddr addr1;
  IpAddr addr2;
  bool alarmAlready = false;
  ts::LocalBufferWriter<1024> bw_err;

  // Table should be empty
  ink_assert(_src_map.count() == 0 && _dst_map.count() == 0);

  file_buff = readIntoBuffer(config_file_path, "ip-allow", &file_size);

  if (file_buff == nullptr) {
    Warning("%s Failed to read %s. All IP Addresses will be blocked", MODULE_NAME, config_file_path.get());
    return 1;
  }

  TextView src(file_buff, file_size);
  TextView line;
  auto err_prefix = [&]() -> ts::BufferWriter & {
    return bw_err.reset().print("{} discarding '{}' entry at line {} : ", MODULE_NAME, config_file_path, line_num);
  };

  while (!(line = src.take_prefix_at('\n')).empty()) {
    ++line_num;
    line.trim_if(&isspace);

    if (!line.empty() && *line != '#') {
      TextView token = line.take_prefix_if(&isspace);
      TextView value = token.split_suffix_at('=');
      match_key_t match;
      if (value.empty()) {
        err_prefix().print("No value found in token '{}'.\0", token);
        SignalError(bw_err, alarmAlready);
        continue;
      } else if (strcasecmp(token, OPT_MATCH_SRC) == 0) {
        match = SRC_ADDR;
      } else if (strcasecmp(token, OPT_MATCH_DST) == 0) {
        match = DST_ADDR;
      } else {
        err_prefix().print("'{}' is not a valid key.\0", token);
        SignalError(bw_err, alarmAlready);
        continue;
      }

      if (0 == ats_ip_range_parse(value, addr1, addr2)) {
        uint32_t acl_method_mask      = 0;
        bool op_found_p               = false;
        bool method_found_p           = false;
        bool all_found_p              = false;
        bool deny_nonstandard_methods = false;
        bool line_valid_p             = true;
        AclOp op                      = ACL_OP_DENY; // "shut up", I explained to the compiler.
        MethodNames nonstandard_methods;

        while (line_valid_p && !line.ltrim_if(&isspace).empty()) {
          token = line.take_prefix_if(&isspace);
          value = token.split_suffix_at('=');

          if (value.empty()) {
            err_prefix().print("No value found in token '{}'\0", token);
            SignalError(bw_err, alarmAlready);
            line_valid_p = false;
          } else if (strcasecmp(token, OPT_ACTION_TAG) == 0) {
            if (strcasecmp(value, OPT_ACTION_ALLOW) == 0) {
              op_found_p = true, op = ACL_OP_ALLOW;
            } else if (strcasecmp(value, OPT_ACTION_DENY) == 0) {
              op_found_p = true, op = ACL_OP_DENY;
            } else {
              err_prefix().print("'{}' is not a valid action\0", value);
              SignalError(bw_err, alarmAlready);
              line_valid_p = false;
            }
          } else if (strcasecmp(token, OPT_METHOD) == 0) {
            // Parse method="GET|HEAD"
            while (!value.empty()) {
              TextView method_name = value.take_prefix_at('|');
              if (strcasecmp(method_name, OPT_METHOD_ALL) == 0) {
                all_found_p = true;
                break;
              } else {
                int method_idx = hdrtoken_tokenize(method_name.data(), method_name.size());
                if (method_idx < HTTP_WKSIDX_CONNECT || method_idx >= HTTP_WKSIDX_CONNECT + HTTP_WKSIDX_METHODS_CNT) {
                  nonstandard_methods.push_back(method_name);
                  Debug("ip-allow", "%s",
                        bw_err.reset().print("Found nonstandard method '{}' on line {}\0", method_name, line_num).data());
                } else { // valid method.
                  acl_method_mask |= ACL::MethodIdxToMask(method_idx);
                }
                method_found_p = true;
              }
            }
          } else {
            err_prefix().print("'{}' is not a valid token\0", token);
            SignalError(bw_err, alarmAlready);
            line_valid_p = false;
          }
        }
        if (!line_valid_p) {
          continue; // error parsing the line, go on to the next.
        }
        if (!op_found_p) {
          err_prefix().print("No action found.\0");
          SignalError(bw_err, alarmAlready);
          continue;
        }
        // If method not specified, default to ALL
        if (all_found_p || !method_found_p) {
          method_found_p  = true;
          acl_method_mask = ALL_METHOD_MASK;
          nonstandard_methods.clear();
        }
        // When deny, use bitwise complement.  (Make the rule 'allow for all
        // methods except those specified')
        if (op == ACL_OP_DENY) {
          acl_method_mask          = ALL_METHOD_MASK & ~acl_method_mask;
          deny_nonstandard_methods = true;
        }

        if (method_found_p) {
          std::vector<Record> &acls = match == DST_ADDR ? _dst_acls : _src_acls;
          IpMap &map                = match == DST_ADDR ? _dst_map : _src_map;
          acls.emplace_back(acl_method_mask, line_num, std::move(nonstandard_methods), deny_nonstandard_methods);
          // Color with index in acls because at this point the address is volatile.
          map.fill(addr1, addr2, reinterpret_cast<void *>(acls.size() - 1));
        } else {
          err_prefix().print("No valid method found\0"); // changed by YTS Team, yamsat bug id -59022
          SignalError(bw_err, alarmAlready);
        }
      } else {
        err_prefix().print("'{}' is not a valid IP address range\0", value);
        SignalError(bw_err, alarmAlready);
      }
    }
  }

  if (_src_map.count() == 0 && _dst_map.count() == 0) {
    Warning("%s No entries in %s. All IP Addresses will be blocked", MODULE_NAME, config_file_path.get());
  } else {
    // convert the coloring from indices to pointers.
    for (auto &item : _src_map) {
      item.setData(&_src_acls[reinterpret_cast<size_t>(item.data())]);
    }
    for (auto &item : _dst_map) {
      item.setData(&_dst_acls[reinterpret_cast<size_t>(item.data())]);
    }
  }

  if (is_debug_tag_set("ip-allow")) {
    Print();
  }
  return 0;
}
