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
#include "tscore/ts_file.h"
#include "tscore/ink_memory.h"

#include "yaml-cpp/yaml.h"

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

template <typename... Args>
void
ParseError(ts::TextView fmt, Args &&... args)
{
  ts::LocalBufferWriter<1024> w;
  w.printv(fmt, std::forward_as_tuple(args...));
  w.write('\0');
  Warning("%s", w.data());
}

} // namespace

namespace ts
{
BufferWriter &
bwformat(BufferWriter &w, BWFSpec const &spec, IpAllow const *obj)
{
  return w.print("{}[{}]", obj->MODULE_NAME, obj->get_config_file().c_str());
}

BufferWriter &
bwformat(BufferWriter &w, BWFSpec const &spec, YAML::Mark const &mark)
{
  return w.print("Line {}", mark.line);
}

BufferWriter &
bwformat(BufferWriter &w, BWFSpec const &spec, std::error_code const &ec)
{
  return w.print("[{}:{}]", ec.value(), ec.message());
}

} // namespace ts

namespace YAML
{
template <> struct convert<ts::TextView> {
  static Node
  encode(ts::TextView const &tv)
  {
    Node zret;
    zret = std::string(tv.data(), tv.size());
    return zret;
  }
  static bool
  decode(const Node &node, ts::TextView &tv)
  {
    if (!node.IsScalar()) {
      return false;
    }
    tv.assign(node.Scalar());
    return true;
  }
};

} // namespace YAML

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

  Note("ip_allow.yaml loading ...");

  new_table = new self_type("proxy.config.cache.ip_allow.filename");
  new_table->BuildTable();

  configid = configProcessor.set(configid, new_table);

  Note("ip_allow.yaml finished loading");
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

IpAllow::IpAllow(const char *config_var) : config_file(ats_scoped_str(RecConfigReadConfigPath(config_var)).get()) {}

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
  // Table should be empty
  ink_assert(_src_map.count() == 0 && _dst_map.count() == 0);

  std::error_code ec;
  std::string content{ts::file::load(config_file, ec)};
  if (ec.value() == 0) {
    // If it's a .yaml or the root tag is present, treat as YAML.
    if (TextView{config_file.view()}.take_suffix_at('.') == "yaml" || std::string::npos != content.find(YAML_TAG_ROOT)) {
      this->YAMLBuildTable(content);
    } else {
      this->ATSBuildTable(content);
    }

    if (_src_map.count() == 0 && _dst_map.count() == 0) {
      ParseError("{} - No entries found. All IP Addresses will be blocked", this);
      return 1;
    }
    // convert the coloring from indices to pointers.
    for (auto &item : _src_map) {
      item.setData(&_src_acls[reinterpret_cast<size_t>(item.data())]);
    }
    for (auto &item : _dst_map) {
      item.setData(&_dst_acls[reinterpret_cast<size_t>(item.data())]);
    }
    if (is_debug_tag_set("ip-allow")) {
      Print();
    }
  } else {
    ParseError("{} Failed to load {}. All IP Addresses will be blocked", this, ec);
    return 1;
  }
  return 0;
}

bool
IpAllow::YAMLLoadMethod(const YAML::Node &node, Record &rec)
{
  const std::string &value{node.Scalar()};

  if (0 == strcasecmp(value, YAML_VALUE_METHODS_ALL)) {
    rec._method_mask = ALL_METHOD_MASK;
  } else {
    int method_idx = hdrtoken_tokenize(value.data(), value.size());
    if (method_idx < HTTP_WKSIDX_CONNECT || method_idx >= HTTP_WKSIDX_CONNECT + HTTP_WKSIDX_METHODS_CNT) {
      rec._nonstandard_methods.push_back(value);
      Debug("ip-allow", "Found nonstandard method '%s' at line %d", value.c_str(), node.Mark().line);
    } else { // valid method.
      rec._method_mask |= ACL::MethodIdxToMask(method_idx);
    }
  }
  return true;
}

bool
IpAllow::YAMLLoadIPAddrRange(const YAML::Node &node, IpMap *map, void *mark)
{
  if (node.IsScalar()) {
    IpAddr min, max;
    if (0 == ats_ip_range_parse(node.Scalar(), min, max)) {
      map->fill(min, max, mark);
      return true;
    } else {
      ParseError("{} {} - '{}' is not a valid range.", this, node.Mark(), node.Scalar());
    }
  }
  return false;
}

bool
IpAllow::YAMLLoadEntry(const YAML::Node &entry)
{
  AclOp op = ACL_OP_DENY; // "shut up", I explained to the compiler.
  YAML::Node node;
  IpAddr min, max;
  std::string value;
  Record rec;
  std::vector<Record> *acls{nullptr};
  IpMap *map = nullptr;

  if (!entry.IsMap()) {
    ParseError("{} {} - ACL items must be maps.", this, entry.Mark());
    return false;
  }

  if (entry[YAML_TAG_APPLY]) {
    auto apply_node{entry[YAML_TAG_APPLY]};
    if (apply_node.IsScalar()) {
      ts::TextView value{apply_node.Scalar()};
      if (0 == strcasecmp(value, YAML_VALUE_APPLY_IN)) {
        acls = &_src_acls;
        map  = &_src_map;
      } else if (0 == strcasecmp(value, YAML_VALUE_APPLY_OUT)) {
        acls = &_dst_acls;
        map  = &_dst_map;
      } else {
        ParseError(R"("{}" value at {} must be "{}" or "{}")", YAML_TAG_APPLY, entry.Mark(), YAML_VALUE_APPLY_IN,
                   YAML_VALUE_APPLY_OUT);
        return false;
      }
    } else {
      ParseError(R"("{}" value at {} must be a scalar, "{}" or "{}")", YAML_TAG_APPLY, entry.Mark(), YAML_VALUE_APPLY_IN,
                 YAML_VALUE_APPLY_OUT);
      return false;
    }
  } else {
    ParseError(R"("Object at {} must have a "{}" key.)", entry.Mark(), YAML_TAG_APPLY);
    return false;
  }

  void *ipmap_mark = reinterpret_cast<void *>(acls->size());
  if (entry[YAML_TAG_IP_ADDRS]) {
    auto addr_node{entry[YAML_TAG_IP_ADDRS]};
    if (addr_node.IsSequence()) {
      for (auto const &n : addr_node) {
        if (!this->YAMLLoadIPAddrRange(n, map, ipmap_mark)) {
          return false;
        }
      }
    } else if (!this->YAMLLoadIPAddrRange(addr_node, map, ipmap_mark)) {
      return false;
    }
  }

  if (!entry[YAML_TAG_ACTION]) {
    ParseError("{} {} - item ignored, required '{}' key not found.", this, entry.Mark(), YAML_TAG_ACTION);
    return false;
  }

  node = entry[YAML_TAG_ACTION];
  if (!node.IsScalar()) {
    ParseError("{} {} - item ignored, value for tag '{}' must be a string", this, node.Mark(), YAML_TAG_ACTION);
    return false;
  }
  value = node.as<std::string>();
  if (value == YAML_VALUE_ACTION_ALLOW) {
    op = ACL_OP_ALLOW;
  } else if (value == YAML_VALUE_ACTION_DENY) {
    op = ACL_OP_DENY;
  } else {
    ParseError("{} {} - item ignored, value for tag '{}' must be '{}' or '{}'", this, node.Mark(), YAML_TAG_ACTION,
               YAML_VALUE_ACTION_ALLOW, YAML_VALUE_ACTION_DENY);
    return false;
  }
  if (!entry[YAML_TAG_METHODS]) {
    rec._method_mask = ALL_METHOD_MASK;
  } else {
    node = entry[YAML_TAG_METHODS];
    if (node.IsScalar()) {
      this->YAMLLoadMethod(node, rec);
    } else if (node.IsSequence()) {
      for (auto const &elt : node) {
        if (elt.IsScalar()) {
          this->YAMLLoadMethod(elt, rec);
          if (rec._method_mask == ALL_METHOD_MASK) {
            break; // we're done here, nothing else matters.
          }
        } else {
          ParseError("{} {} - item ignored, all values for '{}' must be strings.", this, elt.Mark(), YAML_TAG_METHODS);
          return false;
        }
      }
    } else {
      ParseError("{} {} - item ignored, value for '{}' must be a single string or a list of strings.", this, node.Mark(),
                 YAML_TAG_METHODS);
    }
  }
  if (op == ACL_OP_DENY) {
    rec._method_mask              = ALL_METHOD_MASK & ~rec._method_mask;
    rec._deny_nonstandard_methods = true;
  }
  rec._src_line = entry.Mark().line;
  // If we get here, everything parsed OK, add the record.
  acls->emplace_back(std::move(rec));
  return true;
}

int
IpAllow::YAMLBuildTable(std::string const &content)
{
  YAML::Node root{YAML::Load(content)};
  if (!root.IsMap()) {
    ParseError("{} - top level object was not a map. All IP Addresses will be blocked", this);
    return 1;
  }

  YAML::Node data{root[YAML_TAG_ROOT]};
  if (!data) {
    ParseError("{} - root tag '{}' not found. All IP Addresses will be blocked", this, YAML_TAG_ROOT);
  } else if (data.IsSequence()) {
    for (auto const &entry : data) {
      if (!this->YAMLLoadEntry(entry)) {
        return 1;
      }
    }
  } else if (data.IsMap()) {
    this->YAMLLoadEntry(data); // singleton, just load it.
  } else {
    ParseError("{} - root tag '{}' is not an map or sequence. All IP Addresses will be blocked", this, YAML_TAG_ROOT);
    return 1;
  }
  return 0;
}

int
IpAllow::ATSBuildTable(std::string const &content)
{
  int line_num = 0;
  IpAddr addr1;
  IpAddr addr2;
  bool alarmAlready = false;
  ts::LocalBufferWriter<1024> bw_err;

  TextView src(content);
  TextView line;
  auto err_prefix = [&]() -> ts::BufferWriter & {
    return bw_err.reset().print("{} discarding '{}' entry at line {} : ", MODULE_NAME, config_file.c_str(), line_num);
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
                  nonstandard_methods.emplace_back(std::string(method_name.data(), method_name.size()));
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
  return 0;
}
